#pragma once

#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Framework.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Tests
{
	// A deterministic offscreen WARP target exercises the production shaders
	// without an OpenXR session or a visible desktop window.
	class GpuTestContext final
	{
	  public:
		using Pixel = std::array<std::uint8_t, 4>;
		static constexpr DXGI_FORMAT ColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		static constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

		GpuTestContext(UINT targetWidth, UINT targetHeight) : width(targetWidth), height(targetHeight)
		{
			Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
			Check(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))), "Create offscreen DXGI factory");
			Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
			Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))), "Acquire deterministic WARP adapter");
			Check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))),
			      "Create offscreen D3D12 device");
			D3D12_COMMAND_QUEUE_DESC queueDescription{};
			queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			Check(SUCCEEDED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue))), "Create offscreen queue");
			Check(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))),
			      "Create offscreen allocator");
			Check(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			                                        IID_PPV_ARGS(&commands))), "Create offscreen command list");
			Check(SUCCEEDED(commands->Close()), "Close initial offscreen command list");
			Check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))), "Create offscreen fence");
			D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
			heapDescription.NumDescriptors = 1;
			heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			Check(SUCCEEDED(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&rtvHeap))), "Create offscreen RTV heap");
			heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			Check(SUCCEEDED(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&dsvHeap))), "Create offscreen DSV heap");
			D3D12_HEAP_PROPERTIES properties{};
			properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC texture{};
			texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			texture.Width = width;
			texture.Height = height;
			texture.DepthOrArraySize = 1;
			texture.MipLevels = 1;
			texture.Format = ColorFormat;
			texture.SampleDesc.Count = 1;
			texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			D3D12_CLEAR_VALUE clear{};
			clear.Format = ColorFormat;
			clear.Color[3] = 1.0F;
			Check(SUCCEEDED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture,
			    D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target))), "Create offscreen color target");
			device->CreateRenderTargetView(target.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());
			device->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &readbackBytes);
			texture.Format = DepthFormat;
			texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			clear.Format = DepthFormat;
			clear.DepthStencil = { 1.0F, 0 };
			Check(SUCCEEDED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture,
			    D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth))), "Create offscreen depth target");
			device->CreateDepthStencilView(depth.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
			properties.Type = D3D12_HEAP_TYPE_READBACK;
			D3D12_RESOURCE_DESC buffer{};
			buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			buffer.Width = readbackBytes;
			buffer.Height = 1;
			buffer.DepthOrArraySize = 1;
			buffer.MipLevels = 1;
			buffer.SampleDesc.Count = 1;
			buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			Check(SUCCEEDED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer,
			    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))), "Create GPU pixel readback");
		}

		void Begin(const std::array<float, 4>& clear = { 0.0F, 0.0F, 0.0F, 1.0F })
		{
			Check(SUCCEEDED(allocator->Reset()) && SUCCEEDED(commands->Reset(allocator.Get(), nullptr)),
			      "Begin offscreen test frame");
			if (copied)
			{
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition = { target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET };
				commands->ResourceBarrier(1, &barrier);
			}
			const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
			const auto dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
			commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
			commands->ClearRenderTargetView(rtv, clear.data(), 0, nullptr);
			commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
			const D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
			const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			commands->RSSetViewports(1, &viewport);
			commands->RSSetScissorRects(1, &scissor);
		}

		[[nodiscard]] std::vector<Pixel> ReadPixels()
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition = { target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE };
			commands->ResourceBarrier(1, &barrier);
			D3D12_TEXTURE_COPY_LOCATION source{};
			source.pResource = target.Get();
			source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			D3D12_TEXTURE_COPY_LOCATION destination{};
			destination.pResource = readback.Get();
			destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			destination.PlacedFootprint = footprint;
			commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			Check(SUCCEEDED(commands->Close()), "Submit offscreen drawing");
			ID3D12CommandList* lists[]{ commands.Get() };
			queue->ExecuteCommandLists(1, lists);
			const UINT64 value = ++fenceValue;
			Check(SUCCEEDED(queue->Signal(fence.Get(), value)), "Signal GPU drawing completion");
			const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			Check(event != nullptr, "Create GPU completion event");
			const HRESULT eventResult = fence->SetEventOnCompletion(value, event);
			const DWORD wait = SUCCEEDED(eventResult)
			    ? WaitForSingleObject(event, ScaleTimeoutMilliseconds(10000)) : WAIT_FAILED;
			CloseHandle(event);
			Check(wait == WAIT_OBJECT_0, "Offscreen GPU drawing completes within ten seconds");
			copied = true;
			const D3D12_RANGE range{ 0, static_cast<SIZE_T>(readbackBytes) };
			void* mapped = nullptr;
			Check(SUCCEEDED(readback->Map(0, &range, &mapped)), "Read rendered GPU pixels");
			std::vector<Pixel> pixels(static_cast<std::size_t>(width) * height);
			for (UINT row = 0; row < height; ++row)
				std::memcpy(pixels.data() + static_cast<std::size_t>(row) * width,
				    static_cast<const std::byte*>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
				    static_cast<std::size_t>(width) * sizeof(Pixel));
			const D3D12_RANGE written{ 0, 0 };
			readback->Unmap(0, &written);
			return pixels;
		}

		UINT width, height;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;

	  private:
		bool copied{ false };
		UINT64 fenceValue{ 0 }, readbackBytes{ 0 };
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		Microsoft::WRL::ComPtr<ID3D12Resource> target, depth, readback;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap;
	};
}
