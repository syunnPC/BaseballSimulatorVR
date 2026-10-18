#pragma once

#include "UiTextAtlasPipeline.hpp"
#include <exception>

namespace BaseballSimulatorVR::Common
{
// Independent CPU upload storage and command allocator for one view/frame.
// Submission never waits for the GPU. Only reuse and destruction require a fence.
class D3d12RenderFrame final
{
  public:
	D3d12RenderFrame() = default;
	D3d12RenderFrame(const D3d12RenderFrame &) = delete;
	D3d12RenderFrame &operator=(const D3d12RenderFrame &) = delete;
	D3d12RenderFrame(D3d12RenderFrame &&) noexcept = default;
	D3d12RenderFrame &operator=(D3d12RenderFrame &&) noexcept = default;
	[[nodiscard]] HRESULT Initialize(ID3D12Device *device, DXGI_FORMAT color, DXGI_FORMAT depth, UINT samples,
									 UINT initialVertexCapacity)
	{
		if (!device)
			return E_INVALIDARG;
		HRESULT result =
			device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
		if (FAILED(result))
			return result;
		result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
										   IID_PPV_ARGS(&commands));
		if (FAILED(result))
			return result;
		result = commands->Close();
		if (FAILED(result))
			return result;
		if (!DebugOverlay::InitializeVertexBuffer(device, initialVertexCapacity, &vertices, &vertexView,
												  &vertexCapacity))
			return E_FAIL;
		if (!text.Initialize(device, color, depth, samples))
			return E_FAIL;
		submittedFenceValue = 0;
		return S_OK;
	}

	[[nodiscard]] static HRESULT WaitForFence(ID3D12Fence *fence, UINT64 value, HANDLE event)
	{
		if (value == 0)
			return S_OK;
		if (!fence || !event)
			return E_INVALIDARG;
		const UINT64 completed = fence->GetCompletedValue();
		if (completed == UINT64_MAX)
			return DXGI_ERROR_DEVICE_REMOVED;
		if (completed >= value)
			return S_OK;
		const HRESULT result = fence->SetEventOnCompletion(value, event);
		if (FAILED(result))
			return result;
		return WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0 ? S_OK : E_FAIL;
	}

	static void EnsureSafeResourceRelease(ID3D12Device *device, bool completed) noexcept
	{
		if (!completed && device && SUCCEEDED(device->GetDeviceRemovedReason()))
		{
			// No completion proof on a live device means the GPU can still
			// dereference every queued resource. A normal destruction is
			// unsafe; only process teardown can release these allocations.
			OutputDebugStringW(
				L"[D3D12] Fatal: unable to drain a live graphics queue before resource destruction.\n");
			std::terminate();
		}
	}

	[[nodiscard]] HRESULT Begin(ID3D12Fence *fence, HANDLE event)
	{
		const HRESULT result = WaitForFence(fence, submittedFenceValue, event);
		if (FAILED(result))
			return result;
		if (!allocator || !commands)
			return E_UNEXPECTED;
		const HRESULT reset = allocator->Reset();
		return FAILED(reset) ? reset : commands->Reset(allocator.Get(), nullptr);
	}

	[[nodiscard]] HRESULT Submit(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
	{
		if (!queue || !fence || value == 0 || !commands)
			return E_INVALIDARG;
		const HRESULT close = commands->Close();
		if (FAILED(close))
			return close;
		ID3D12CommandList *lists[]{commands.Get()};
		queue->ExecuteCommandLists(1, lists);
		const HRESULT result = queue->Signal(fence, value);
		// A failed Signal is terminal for the owner: submitted resources may
		// still be in use and cannot be reused on a speculative fence value.
		if (SUCCEEDED(result))
			submittedFenceValue = value;
		return result;
	}

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertices;
	D3D12_VERTEX_BUFFER_VIEW vertexView{};
	UINT vertexCapacity{};
	UiText::AtlasPipeline text;
	UINT64 submittedFenceValue{};
};
} // namespace BaseballSimulatorVR::Common
