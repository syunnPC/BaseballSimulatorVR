#include "Dx12RenderBackend.hpp"
#include "../Common/D3d12DebugOverlay.hpp"
#include "../Common/D3d12RenderFrame.hpp"
#include "../Common/DebugWorldProjection.hpp"
#include "../Common/FieldPresentationOverlay.hpp"
#include "../Common/FxaaPipelineUtilities.hpp"
#include "../Common/MathUtilities.hpp"
#include "../Common/PhaseTrajectoryOverlay.hpp"

#if defined(_WIN64)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace BaseballSimulatorVR::Render
{
	void Dx12RenderBackend::SetFieldPresentationState(const Common::FieldPresentationState& state)
	{
		m_fieldPresentation = state;
	}

	namespace
	{
		constexpr UINT MaxDebugVertices = 262144;
		constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
		constexpr UINT RequestedMsaaSampleCount = 4U;
		// The flip-model swap chain stays UNORM; every render target view and
		// intermediate target uses sRGB like the XR swapchains, so the mirror
		// shows the same colors and blends in linear space like the headset.
		constexpr DXGI_FORMAT DesktopColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#if defined(BASEBALLSIMVR_DISABLE_WORLD_DRAW)
		constexpr bool EnableDebugDraw = false;
#else
		constexpr bool EnableDebugDraw = true;
#endif
		using Common::ScreenPoint;
	} // namespace

	Dx12RenderBackend::~Dx12RenderBackend()
	{
		Shutdown();
	}

	bool Dx12RenderBackend::Initialize(const HWND windowHandle)
	{
		if (m_isInitialized)
		{
			return true;
		}

		SetDebugDrawPathStatus(L"initializing");

		if (windowHandle == nullptr)
		{
			return false;
		}

		m_windowHandle = windowHandle;

		if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory))))
		{
			Shutdown();
			return false;
		}

		for (UINT adapterIndex = 0;; ++adapterIndex)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapterCandidate{};
			const HRESULT adapterResult = m_factory->EnumAdapterByGpuPreference(
			        adapterIndex,
			        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			        IID_PPV_ARGS(&adapterCandidate));
			if (adapterResult == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}
			if (FAILED(adapterResult))
			{
				Shutdown();
				return false;
			}

			DXGI_ADAPTER_DESC1 adapterDescription{};
			if (FAILED(adapterCandidate->GetDesc1(&adapterDescription)))
			{
				continue;
			}

			if ((adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U)
			{
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(
			        adapterCandidate.Get(),
			        D3D_FEATURE_LEVEL_11_0,
			        __uuidof(ID3D12Device),
			        nullptr)))
			{
				if (FAILED(adapterCandidate.As(&m_adapter)))
				{
					Shutdown();
					return false;
				}
				break;
			}
		}

		if (m_adapter == nullptr)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter{};
			if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
			{
				Shutdown();
				return false;
			}

			if (FAILED(warpAdapter.As(&m_adapter)))
			{
				Shutdown();
				return false;
			}
		}

		if (FAILED(D3D12CreateDevice(
		        m_adapter.Get(),
		        D3D_FEATURE_LEVEL_11_0,
		        IID_PPV_ARGS(&m_device))))
		{
			Shutdown();
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC commandQueueDescription{};
		commandQueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		if (FAILED(m_device->CreateCommandQueue(
		        &commandQueueDescription,
		        IID_PPV_ARGS(&m_commandQueue))))
		{
			Shutdown();
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
		swapChainDescription.BufferCount = FrameCount;
		swapChainDescription.Width = 0;
		swapChainDescription.Height = 0;
		swapChainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDescription.SampleDesc.Count = 1;
		swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDescription.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain{};
		if (FAILED(m_factory->CreateSwapChainForHwnd(
		        m_commandQueue.Get(),
		        m_windowHandle,
		        &swapChainDescription,
		        nullptr,
		        nullptr,
		        &swapChain)))
		{
			Shutdown();
			return false;
		}

		(void)m_factory->MakeWindowAssociation(m_windowHandle, DXGI_MWA_NO_ALT_ENTER);

		if (FAILED(swapChain.As(&m_swapChain)))
		{
			Shutdown();
			return false;
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		if (FAILED(m_swapChain->SetMaximumFrameLatency(1)))
		{
			Shutdown();
			return false;
		}
		m_frameLatencyEvent = m_swapChain->GetFrameLatencyWaitableObject();
		if (!m_frameLatencyEvent)
		{
			Shutdown();
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
		rtvHeapDescription.NumDescriptors = FrameCount;
		rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		if (FAILED(m_device->CreateDescriptorHeap(
		        &rtvHeapDescription,
		        IID_PPV_ARGS(&m_rtvHeap))))
		{
			Shutdown();
			return false;
		}

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_msaaSampleCount = 1U;
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels{};
		msaaQualityLevels.Format = DesktopColorFormat;
		msaaQualityLevels.SampleCount = RequestedMsaaSampleCount;
		msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		msaaQualityLevels.NumQualityLevels = 0;
		if (SUCCEEDED(m_device->CheckFeatureSupport(
		        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		        &msaaQualityLevels,
		        sizeof(msaaQualityLevels))) &&
		    (msaaQualityLevels.NumQualityLevels > 0U))
		{
			m_msaaSampleCount = RequestedMsaaSampleCount;
		}
		else
		{
			msaaQualityLevels.SampleCount = 2U;
			msaaQualityLevels.NumQualityLevels = 0;
			if (SUCCEEDED(m_device->CheckFeatureSupport(
			        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			        &msaaQualityLevels,
			        sizeof(msaaQualityLevels))) &&
			    (msaaQualityLevels.NumQualityLevels > 0U))
			{
				m_msaaSampleCount = 2U;
			}
		}

		if (m_msaaSampleCount > 1U)
		{
			D3D12_DESCRIPTOR_HEAP_DESC msaaRtvHeapDescription{};
			msaaRtvHeapDescription.NumDescriptors = FrameCount;
			msaaRtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			msaaRtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(m_device->CreateDescriptorHeap(
			        &msaaRtvHeapDescription,
			        IID_PPV_ARGS(&m_msaaRtvHeap))))
			{
				Shutdown();
				return false;
			}
		}

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
		dsvHeapDescription.NumDescriptors = FrameCount;
		dsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(
		        &dsvHeapDescription,
		        IID_PPV_ARGS(&m_dsvHeap))))
		{
			Shutdown();
			return false;
		}

		m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		if (!InitializeWindowSizeResources())
		{
			Shutdown();
			return false;
		}
		for (UINT bufferIndex = 0; bufferIndex < FrameCount; ++bufferIndex)
		{
			if (FAILED(m_device->CreateCommandAllocator(
			        D3D12_COMMAND_LIST_TYPE_DIRECT,
			        IID_PPV_ARGS(&m_commandAllocators[bufferIndex]))))
			{
				Shutdown();
				return false;
			}
		}
		if (FAILED(m_device->CreateCommandList(
		        0,
		        D3D12_COMMAND_LIST_TYPE_DIRECT,
		        m_commandAllocators[m_frameIndex].Get(),
		        nullptr,
		        IID_PPV_ARGS(&m_commandList))))
		{
			Shutdown();
			return false;
		}

		if (FAILED(m_commandList->Close()))
		{
			Shutdown();
			return false;
		}

		if (EnableDebugDraw &&
		    (!InitializeDebugPipeline() || !InitializeDebugVertexBuffer() || !InitializeTextAtlasPipelines()))
		{
			SetDebugDrawPathStatus(L"init-failed");
			Shutdown();
			return false;
		}

		m_isFxaaAvailable = InitializeFxaaPipeline();
		if (!m_isFxaaAvailable)
		{
			OutputDebugStringW(
			    L"[Dx12RenderBackend] FXAA pipeline init failed; using copy post-process fallback.\n");
		}
		m_postProcessPathStatus = m_isFxaaAvailable ? L"fxaa" : L"copy";

		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
		{
			Shutdown();
			return false;
		}

		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			Shutdown();
			return false;
		}

		m_fenceValues.fill(0);
		m_nextFenceValue = 1;
		m_isInitialized = true;
		m_desktopRenderStatus.suspended = false;
		SetDebugDrawPathStatus(
		    EnableDebugDraw ? L"ready" : L"world-draw-disabled");
		return true;
	}

	bool Dx12RenderBackend::InitializeWindowSizeResources()
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE msaaRtvHandle{};
		if (m_msaaRtvHeap != nullptr)
		{
			msaaRtvHandle = m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
		}

		for (UINT bufferIndex = 0; bufferIndex < FrameCount; ++bufferIndex)
		{
			if (FAILED(m_swapChain->GetBuffer(
			        bufferIndex,
			        IID_PPV_ARGS(&m_renderTargets[bufferIndex]))))
			{
				return false;
			}

			D3D12_RENDER_TARGET_VIEW_DESC backBufferRtvDescription{};
			backBufferRtvDescription.Format = DesktopColorFormat;
			backBufferRtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			m_device->CreateRenderTargetView(
			    m_renderTargets[bufferIndex].Get(),
			    &backBufferRtvDescription,
			    rtvHandle);
			rtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);

			const D3D12_RESOURCE_DESC renderTargetDescription =
			    m_renderTargets[bufferIndex]->GetDesc();
			if ((m_msaaSampleCount > 1U) && (m_msaaRtvHeap != nullptr))
			{
				D3D12_HEAP_PROPERTIES msaaHeapProperties{};
				msaaHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				msaaHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				msaaHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				msaaHeapProperties.CreationNodeMask = 1;
				msaaHeapProperties.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC msaaRenderTargetDescription =
				    renderTargetDescription;
				msaaRenderTargetDescription.Format = DesktopColorFormat;
				msaaRenderTargetDescription.SampleDesc.Count = m_msaaSampleCount;
				msaaRenderTargetDescription.SampleDesc.Quality = 0;
				msaaRenderTargetDescription.Flags =
				    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				D3D12_CLEAR_VALUE renderTargetClearValue{};
				renderTargetClearValue.Format = DesktopColorFormat;
				renderTargetClearValue.Color[0] = m_clearColor[0];
				renderTargetClearValue.Color[1] = m_clearColor[1];
				renderTargetClearValue.Color[2] = m_clearColor[2];
				renderTargetClearValue.Color[3] = m_clearColor[3];

				if (FAILED(m_device->CreateCommittedResource(
				        &msaaHeapProperties,
				        D3D12_HEAP_FLAG_NONE,
				        &msaaRenderTargetDescription,
				        D3D12_RESOURCE_STATE_RENDER_TARGET,
				        &renderTargetClearValue,
				        IID_PPV_ARGS(&m_msaaRenderTargets[bufferIndex]))))
				{
					return false;
				}

				D3D12_RENDER_TARGET_VIEW_DESC msaaRtvDescription{};
				msaaRtvDescription.Format = DesktopColorFormat;
				msaaRtvDescription.ViewDimension =
				    D3D12_RTV_DIMENSION_TEXTURE2DMS;
				m_device->CreateRenderTargetView(
				    m_msaaRenderTargets[bufferIndex].Get(),
				    &msaaRtvDescription,
				    msaaRtvHandle);
				msaaRtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);
			}

			const UINT depthSampleCount =
			    (m_msaaSampleCount > 1U)
			        ? m_msaaSampleCount
			        : renderTargetDescription.SampleDesc.Count;
			D3D12_HEAP_PROPERTIES depthHeapProperties{};
			depthHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			depthHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			depthHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			depthHeapProperties.CreationNodeMask = 1;
			depthHeapProperties.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC depthDescription{};
			depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			depthDescription.Alignment = 0;
			depthDescription.Width = renderTargetDescription.Width;
			depthDescription.Height = static_cast<UINT>(renderTargetDescription.Height);
			depthDescription.DepthOrArraySize = 1;
			depthDescription.MipLevels = 1;
			depthDescription.Format = DepthBufferFormat;
			depthDescription.SampleDesc.Count = depthSampleCount;
			depthDescription.SampleDesc.Quality = 0;
			depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE depthClearValue{};
			depthClearValue.Format = DepthBufferFormat;
			depthClearValue.DepthStencil.Depth = 1.0F;
			depthClearValue.DepthStencil.Stencil = 0;

			if (FAILED(m_device->CreateCommittedResource(
			        &depthHeapProperties,
			        D3D12_HEAP_FLAG_NONE,
			        &depthDescription,
			        D3D12_RESOURCE_STATE_DEPTH_WRITE,
			        &depthClearValue,
			        IID_PPV_ARGS(&m_depthBuffers[bufferIndex]))))
			{
				return false;
			}

			D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription{};
			depthStencilViewDescription.Format = DepthBufferFormat;
			depthStencilViewDescription.ViewDimension =
			    (depthSampleCount > 1U)
			        ? D3D12_DSV_DIMENSION_TEXTURE2DMS
			        : D3D12_DSV_DIMENSION_TEXTURE2D;
			depthStencilViewDescription.Flags = D3D12_DSV_FLAG_NONE;
			if (depthStencilViewDescription.ViewDimension ==
			    D3D12_DSV_DIMENSION_TEXTURE2D)
			{
				depthStencilViewDescription.Texture2D.MipSlice = 0;
			}
			m_device->CreateDepthStencilView(
			    m_depthBuffers[bufferIndex].Get(),
			    &depthStencilViewDescription,
			    dsvHandle);
			dsvHandle.ptr += static_cast<SIZE_T>(m_dsvDescriptorSize);

		}

		if (!InitializeFxaaResources(DesktopColorFormat))
		{
			return false;
		}

		const D3D12_RESOURCE_DESC actual = m_renderTargets[0]->GetDesc();
		m_desktopRenderStatus.width = static_cast<std::uint32_t>(actual.Width);
		m_desktopRenderStatus.height = actual.Height;
		m_desktopRenderStatus.sampleCount = m_msaaSampleCount;
		return true;
	}

	bool Dx12RenderBackend::InitializeDebugPipeline()
	{
		if (m_device == nullptr)
		{
			return false;
		}

		const UINT sampleCount = (std::max)(1U, m_msaaSampleCount);

		if (!Common::DebugOverlay::InitializePipeline(
		        m_device.Get(),
		        DesktopColorFormat,
		        DepthBufferFormat,
		        &m_debugRootSignature,
		        &m_debugPipelineState,
		        nullptr,
		        nullptr,
		        Common::DebugOverlay::PipelineBlendMode::Opaque,
		        Common::DebugOverlay::PipelineDepthWriteMode::Enabled,
		        false,
		        sampleCount))
		{
			return false;
		}

		return true;
	}

	bool Dx12RenderBackend::InitializeTextAtlasPipelines()
	{
		for (auto& pipeline : m_textAtlasPipelines)
			if (!pipeline.Initialize(m_device.Get(), DesktopColorFormat,
			    DepthBufferFormat, m_msaaSampleCount)) return false;
		return true;
	}

	bool Dx12RenderBackend::InitializeDebugVertexBuffer()
	{
		if (m_device == nullptr)
		{
			return false;
		}

		for (UINT frame = 0; frame < FrameCount; ++frame)
		{
			if (!Common::DebugOverlay::InitializeVertexBuffer(m_device.Get(), MaxDebugVertices,
			    &m_frameDebugVertexBuffers[frame], &m_frameDebugVertexViews[frame],
			    &m_frameDebugVertexCapacities[frame])) return false;
		}
		m_debugVertexBuffer = m_frameDebugVertexBuffers[m_frameIndex];
		m_debugVertexBufferView = m_frameDebugVertexViews[m_frameIndex];
		m_debugVertexCapacity = m_frameDebugVertexCapacities[m_frameIndex];
		return true;
	}

	bool Dx12RenderBackend::InitializeFxaaResources(const DXGI_FORMAT swapChainFormat)
	{
		if ((m_device == nullptr) || (m_srvDescriptorSize == 0U))
		{
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC sceneRtvHeapDescription{};
		sceneRtvHeapDescription.NumDescriptors = FrameCount;
		sceneRtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		sceneRtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(
		        &sceneRtvHeapDescription,
		        IID_PPV_ARGS(&m_sceneRtvHeap))))
		{
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC sceneSrvHeapDescription{};
		sceneSrvHeapDescription.NumDescriptors = FrameCount;
		sceneSrvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sceneSrvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(
		        &sceneSrvHeapDescription,
		        IID_PPV_ARGS(&m_sceneSrvHeap))))
		{
			return false;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle =
		    m_sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvHandle =
		    m_sceneSrvHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT bufferIndex = 0; bufferIndex < FrameCount; ++bufferIndex)
		{
			if (m_renderTargets[bufferIndex] == nullptr)
			{
				return false;
			}

			const D3D12_RESOURCE_DESC backBufferDescription =
			    m_renderTargets[bufferIndex]->GetDesc();
			D3D12_RESOURCE_DESC sceneDescription = backBufferDescription;
			sceneDescription.Format = swapChainFormat;
			sceneDescription.SampleDesc.Count = 1;
			sceneDescription.SampleDesc.Quality = 0;
			sceneDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

			D3D12_HEAP_PROPERTIES heapProperties{};
			heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapProperties.CreationNodeMask = 1;
			heapProperties.VisibleNodeMask = 1;

			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = swapChainFormat;
			clearValue.Color[0] = m_clearColor[0];
			clearValue.Color[1] = m_clearColor[1];
			clearValue.Color[2] = m_clearColor[2];
			clearValue.Color[3] = m_clearColor[3];

			if (FAILED(m_device->CreateCommittedResource(
			        &heapProperties,
			        D3D12_HEAP_FLAG_NONE,
			        &sceneDescription,
			        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			        &clearValue,
			        IID_PPV_ARGS(&m_sceneColorTargets[bufferIndex]))))
			{
				return false;
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtvDescription{};
			rtvDescription.Format = swapChainFormat;
			rtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtvDescription.Texture2D.MipSlice = 0;
			rtvDescription.Texture2D.PlaneSlice = 0;
			m_device->CreateRenderTargetView(
			    m_sceneColorTargets[bufferIndex].Get(),
			    &rtvDescription,
			    sceneRtvHandle);
			sceneRtvHandle.ptr +=
			    static_cast<SIZE_T>(m_rtvDescriptorSize);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
			srvDescription.Shader4ComponentMapping =
			    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDescription.Format = swapChainFormat;
			srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDescription.Texture2D.MostDetailedMip = 0;
			srvDescription.Texture2D.MipLevels = 1;
			srvDescription.Texture2D.PlaneSlice = 0;
			srvDescription.Texture2D.ResourceMinLODClamp = 0.0F;
			m_device->CreateShaderResourceView(
			    m_sceneColorTargets[bufferIndex].Get(),
			    &srvDescription,
			    sceneSrvHandle);
			sceneSrvHandle.ptr +=
			    static_cast<SIZE_T>(m_srvDescriptorSize);
		}

		return true;
	}

	bool Dx12RenderBackend::InitializeFxaaPipeline()
	{
		if ((m_device == nullptr) || (m_sceneSrvHeap == nullptr))
		{
			return false;
		}

		Common::FxaaPipelineObjects fxaaPipelineObjects{};
		if (!Common::BuildFxaaPipeline(
		        m_device.Get(),
		        DesktopColorFormat,
		        1U,
		        &fxaaPipelineObjects))
		{
			m_fxaaRootSignature.Reset();
			m_fxaaPipelineState.Reset();
			return false;
		}

		m_fxaaRootSignature = std::move(fxaaPipelineObjects.rootSignature);
		m_fxaaPipelineState = std::move(fxaaPipelineObjects.pipelineState);
		return true;
	}

	void Dx12RenderBackend::SetDebugDrawPathStatus(const std::wstring_view status)
	{
		if (status.empty())
		{
			return;
		}

		m_debugDrawPathStatus.assign(status);
		m_backendNameCache = L"DX12(window,dp:";
		m_backendNameCache += m_debugDrawPathStatus;
		m_backendNameCache += L",desktop:" + std::to_wstring(m_desktopRenderStatus.width) +
		    L"x" + std::to_wstring(m_desktopRenderStatus.height);
		m_backendNameCache += L",samples:" + std::to_wstring(m_desktopRenderStatus.sampleCount);
		m_backendNameCache += L",text:atlas";
		if (m_msaaSampleCount > 1U)
		{
			m_backendNameCache += L",aa:MSAAx";
			m_backendNameCache += std::to_wstring(m_msaaSampleCount);
		}
		else
		{
			m_backendNameCache += L",aa:off";
		}
		if (m_postProcessPathStatus.empty())
		{
			m_backendNameCache += L",pp:unknown";
		}
		else
		{
			m_backendNameCache += L",pp:";
			m_backendNameCache += m_postProcessPathStatus;
		}
		m_backendNameCache += L")";
	}

	bool Dx12RenderBackend::HasDebugDrawResources() const noexcept
	{
		return (m_debugRootSignature != nullptr) &&
		       (m_debugPipelineState != nullptr) &&
		       (m_debugVertexBuffer != nullptr) &&
		       (m_debugVertexCapacity != 0U) &&
		       (m_debugVertexBufferView.SizeInBytes != 0U) &&
		       (m_debugVertexBufferView.StrideInBytes != 0U);
	}

	std::wstring Dx12RenderBackend::BuildDebugResourceMissingReason() const
	{
		std::wstring reason{};
		auto appendToken = [&](std::wstring_view token) -> void
		{
			if (!reason.empty())
			{
				reason += L"-";
			}
			reason += token;
		};

		if (m_debugRootSignature == nullptr)
		{
			appendToken(L"root");
		}
		if (m_debugPipelineState == nullptr)
		{
			appendToken(L"pso");
		}
		if (m_debugVertexBuffer == nullptr)
		{
			appendToken(L"vb");
		}
		if (m_debugVertexCapacity == 0U)
		{
			appendToken(L"vbcap");
		}
		if (m_debugVertexBufferView.SizeInBytes == 0U)
		{
			appendToken(L"vbvsize");
		}
		if (m_debugVertexBufferView.StrideInBytes == 0U)
		{
			appendToken(L"vbvstride");
		}

		if (reason.empty())
		{
			reason = L"none";
		}
		return reason;
	}

	void Dx12RenderBackend::RepairDebugDrawResources()
	{
		if ((m_debugVertexBuffer != nullptr) && (m_debugVertexCapacity == 0U))
		{
			const D3D12_RESOURCE_DESC vertexBufferDescription =
			    m_debugVertexBuffer->GetDesc();
			if (vertexBufferDescription.Width >= sizeof(Common::DebugOverlay::Vertex))
			{
				const UINT recoveredVertexCapacity = static_cast<UINT>(
				    vertexBufferDescription.Width /
				    sizeof(Common::DebugOverlay::Vertex));
				if (recoveredVertexCapacity > 0U)
				{
					m_debugVertexCapacity = recoveredVertexCapacity;
				}
			}
		}

		const bool vertexViewMissing =
		    (m_debugVertexBufferView.BufferLocation == 0U) ||
		    (m_debugVertexBufferView.SizeInBytes == 0U) ||
		    (m_debugVertexBufferView.StrideInBytes == 0U);
		if ((m_debugVertexBuffer != nullptr) &&
		    (m_debugVertexCapacity > 0U) &&
		    vertexViewMissing)
		{
			D3D12_VERTEX_BUFFER_VIEW repairedVertexBufferView{};
			repairedVertexBufferView.BufferLocation =
			    m_debugVertexBuffer->GetGPUVirtualAddress();
			repairedVertexBufferView.StrideInBytes =
			    static_cast<UINT>(sizeof(Common::DebugOverlay::Vertex));
			repairedVertexBufferView.SizeInBytes =
			    static_cast<UINT>(sizeof(Common::DebugOverlay::Vertex)) *
			    m_debugVertexCapacity;
			m_debugVertexBufferView = repairedVertexBufferView;
		}
	}

	void Dx12RenderBackend::BeginFrame()
	{
		m_frameReady = false;
		m_desktopRenderStatus.mirrorThrottled = false;
		if (!m_isInitialized || m_desktopRenderStatus.failed) return;
		RECT client{};
		if (GetClientRect(m_windowHandle, &client) == FALSE)
		{
			FailDesktopRendering(L"clientrect-failed");
			return;
		}
		const LONG width = client.right - client.left;
		const LONG height = client.bottom - client.top;
		if (IsIconic(m_windowHandle) || width <= 0 || height <= 0)
		{
			m_desktopRenderStatus.suspended = true;
			SetDebugDrawPathStatus(L"minimized");
			return;
		}
		if ((static_cast<UINT>(width) != m_desktopRenderStatus.width ||
		     static_cast<UINT>(height) != m_desktopRenderStatus.height) &&
		    !ResizeWindowResources(static_cast<UINT>(width), static_cast<UINT>(height)))
		{
			FailDesktopRendering(L"resize-failed");
			return;
		}
		m_desktopRenderStatus.suspended = false;
		const bool nonBlockingMirror = m_mirrorViewState.isValid;
		const auto skipMirror = [&]
		{
			m_desktopRenderStatus.mirrorThrottled = true;
			++m_desktopRenderStatus.skippedMirrorFrameCount;
		};
		if (m_presentPending && !TryPresentFrame())
		{
			if (!m_desktopRenderStatus.failed) skipMirror();
			return;
		}
		// A backbuffer's uploads and allocator stay intact until its last GPU
		// submission completes. A busy desktop must never throttle the HMD.
		const UINT64 completed = m_fence->GetCompletedValue();
		if (completed == UINT64_MAX)
		{
			FailDesktopRendering(L"device-removed");
			return;
		}
		if (completed < m_fenceValues[m_frameIndex])
		{
			if (nonBlockingMirror) { skipMirror(); return; }
			if (FAILED(Common::D3d12RenderFrame::WaitForFence(m_fence.Get(),
			    m_fenceValues[m_frameIndex], m_fenceEvent)))
			{
				FailDesktopRendering(L"frame-fence-wait-failed");
				return;
			}
		}
		// Keep desktop VSync and all rendering quality settings. Only start a
		// mirror draw when DXGI has room for it; XR supplies the main frame pace.
		const DWORD ready = WaitForSingleObject(m_frameLatencyEvent, nonBlockingMirror ? 0 : INFINITE);
		if (ready == WAIT_TIMEOUT) { skipMirror(); return; }
		if (ready != WAIT_OBJECT_0)
		{
			FailDesktopRendering(L"frame-latency-wait-failed");
			return;
		}
		m_textGlyphAtlas.BeginFrame();
		m_frameReady = true;
	}

	void Dx12RenderBackend::FailDesktopRendering(const std::wstring_view reason)
	{
		m_frameReady = false;
		m_desktopRenderStatus.failed = true;
		m_desktopRenderStatus.suspended = true;
		SetDebugDrawPathStatus(reason);
		const std::wstring message = L"[DX12] desktop rendering stopped: " +
		    std::wstring(reason) + L"\n";
		OutputDebugStringW(message.c_str());
	}

	void Dx12RenderBackend::ReleaseWindowSizeResources()
	{
		for (auto& target : m_renderTargets) target.Reset();
		for (auto& target : m_msaaRenderTargets) target.Reset();
		for (auto& target : m_sceneColorTargets) target.Reset();
		for (auto& target : m_depthBuffers) target.Reset();
		m_sceneRtvHeap.Reset();
		m_sceneSrvHeap.Reset();
		m_desktopRenderStatus.width = 0;
		m_desktopRenderStatus.height = 0;
	}

	bool Dx12RenderBackend::ResizeWindowResources(const UINT width, const UINT height)
	{
		if (!WaitForGpu()) return false;
		DXGI_SWAP_CHAIN_DESC1 description{};
		if (FAILED(m_swapChain->GetDesc1(&description))) return false;
		// The queue is idle. Discard recorded commands before releasing every
		// direct/indirect backbuffer reference as required by ResizeBuffers.
		m_commandList.Reset();
		for (auto& allocator : m_commandAllocators)
			if (FAILED(allocator->Reset())) return false;
		ReleaseWindowSizeResources();
		if (FAILED(m_swapChain->ResizeBuffers(FrameCount, width, height,
		    description.Format, description.Flags))) return false;
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		m_presentPending = false;
		if (!InitializeWindowSizeResources())
		{
			ReleaseWindowSizeResources();
			return false;
		}
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		    m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList))) ||
		    FAILED(m_commandList->Close())) return false;
		SetDebugDrawPathStatus(L"resized");
		const std::wstring message = L"[DX12] desktop resources resized: " +
		    std::to_wstring(m_desktopRenderStatus.width) + L"x" +
		    std::to_wstring(m_desktopRenderStatus.height) + L", MSAAx" +
		    std::to_wstring(m_msaaSampleCount) + L"\n";
		OutputDebugStringW(message.c_str());
		return true;
	}

	void Dx12RenderBackend::RenderSimulation(const Physics::BallState& ballState)
	{
		m_latestBallState = ballState;
		AppendTrajectoryPoint(ballState);

		const double speed = Physics::Length(ballState.velocity);

		const float xTint = Common::Math::Clamp01(
		    static_cast<float>((ballState.position.x + 1.5) / 3.0));
		const float yTint = Common::Math::Clamp01(
		    static_cast<float>((ballState.position.y + 1.0) / 2.0));
		const float speedTint = Common::Math::Clamp01(
		    static_cast<float>(speed / 45.0));
		m_clearColor[0] = 0.02F + (xTint * 0.07F);
		m_clearColor[1] = 0.02F + (yTint * 0.05F);
		m_clearColor[2] = 0.06F + (speedTint * 0.12F);
		m_clearColor[3] = 1.0F;
	}

	void Dx12RenderBackend::EndFrame()
	{
		if (!m_isInitialized || !m_frameReady || m_desktopRenderStatus.failed)
		{
			return;
		}
		m_frameReady = false;
		m_debugVertexBuffer = m_frameDebugVertexBuffers[m_frameIndex];
		m_debugVertexBufferView = m_frameDebugVertexViews[m_frameIndex];
		m_debugVertexCapacity = m_frameDebugVertexCapacities[m_frameIndex];

		if (FAILED(m_commandAllocators[m_frameIndex]->Reset()))
		{
			FailDesktopRendering(L"allocator-reset-failed");
			return;
		}

		if (FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr)))
		{
			FailDesktopRendering(L"cmdlist-reset-failed");
			return;
		}

		const UINT currentFrame = m_frameIndex;
		const bool useMsaaRenderTarget =
		    (m_msaaSampleCount > 1U) &&
		    (m_msaaRenderTargets[currentFrame] != nullptr);
		if (m_sceneColorTargets[currentFrame] == nullptr)
		{
			FailDesktopRendering(L"scene-target-missing");
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE swapChainRtvHandle =
		    m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		swapChainRtvHandle.ptr += static_cast<SIZE_T>(currentFrame) *
		                          static_cast<SIZE_T>(m_rtvDescriptorSize);
		const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle =
		    GetCurrentSceneRtvHandle();
		const D3D12_CPU_DESCRIPTOR_HANDLE drawRtvHandle =
		    useMsaaRenderTarget ? GetCurrentRtvHandle() : sceneRtvHandle;
		const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCurrentDsvHandle();

		D3D12_RESOURCE_BARRIER toRenderTargetBarriers[2]{};
		UINT toRenderTargetBarrierCount = 0;

		D3D12_RESOURCE_BARRIER& backBufferToRenderBarrier =
		    toRenderTargetBarriers[toRenderTargetBarrierCount++];
		backBufferToRenderBarrier.Type =
		    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		backBufferToRenderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		backBufferToRenderBarrier.Transition.pResource =
		    m_renderTargets[currentFrame].Get();
		backBufferToRenderBarrier.Transition.StateBefore =
		    D3D12_RESOURCE_STATE_PRESENT;
		backBufferToRenderBarrier.Transition.StateAfter =
		    D3D12_RESOURCE_STATE_RENDER_TARGET;
		backBufferToRenderBarrier.Transition.Subresource =
		    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		D3D12_RESOURCE_BARRIER& sceneToDrawBarrier =
		    toRenderTargetBarriers[toRenderTargetBarrierCount++];
		sceneToDrawBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		sceneToDrawBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		sceneToDrawBarrier.Transition.pResource =
		    m_sceneColorTargets[currentFrame].Get();
		sceneToDrawBarrier.Transition.StateBefore =
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		sceneToDrawBarrier.Transition.StateAfter = useMsaaRenderTarget
		                                               ? D3D12_RESOURCE_STATE_RESOLVE_DEST
		                                               : D3D12_RESOURCE_STATE_RENDER_TARGET;
		sceneToDrawBarrier.Transition.Subresource =
		    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		m_commandList->ResourceBarrier(
		    toRenderTargetBarrierCount,
		    toRenderTargetBarriers);

		m_commandList->ClearRenderTargetView(
		    drawRtvHandle,
		    m_clearColor.data(),
		    0,
		    nullptr);
		m_commandList->ClearDepthStencilView(
		    dsvHandle,
		    D3D12_CLEAR_FLAG_DEPTH,
		    1.0F,
		    0,
		    0,
		    nullptr);
		m_commandList->OMSetRenderTargets(1, &drawRtvHandle, FALSE, &dsvHandle);
		if (EnableDebugDraw)
		{
			DrawDebugMarkers(m_commandList.Get());
			if (m_desktopRenderStatus.failed)
			{
				(void)m_commandList->Close();
				return;
			}
		}
		else
		{
			SetDebugDrawPathStatus(L"world-draw-disabled");
		}

		if (useMsaaRenderTarget)
		{
			D3D12_RESOURCE_BARRIER msaaToResolveBarrier{};
			msaaToResolveBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			msaaToResolveBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			msaaToResolveBarrier.Transition.pResource =
			    m_msaaRenderTargets[currentFrame].Get();
			msaaToResolveBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			msaaToResolveBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			msaaToResolveBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_commandList->ResourceBarrier(1, &msaaToResolveBarrier);

			const DXGI_FORMAT resolveFormat = DesktopColorFormat;
			m_commandList->ResolveSubresource(
			    m_sceneColorTargets[currentFrame].Get(),
			    0,
			    m_msaaRenderTargets[currentFrame].Get(),
			    0,
			    resolveFormat);

			D3D12_RESOURCE_BARRIER msaaBackToRenderBarrier{};
			msaaBackToRenderBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			msaaBackToRenderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			msaaBackToRenderBarrier.Transition.pResource =
			    m_msaaRenderTargets[currentFrame].Get();
			msaaBackToRenderBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			msaaBackToRenderBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			msaaBackToRenderBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_commandList->ResourceBarrier(1, &msaaBackToRenderBarrier);
		}

		D3D12_RESOURCE_BARRIER sceneToShaderBarrier{};
		sceneToShaderBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		sceneToShaderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		sceneToShaderBarrier.Transition.pResource =
		    m_sceneColorTargets[currentFrame].Get();
		sceneToShaderBarrier.Transition.StateBefore = useMsaaRenderTarget
		                                                  ? D3D12_RESOURCE_STATE_RESOLVE_DEST
		                                                  : D3D12_RESOURCE_STATE_RENDER_TARGET;
		sceneToShaderBarrier.Transition.StateAfter =
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		sceneToShaderBarrier.Transition.Subresource =
		    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_commandList->ResourceBarrier(1, &sceneToShaderBarrier);

		bool usedFxaaPass = false;
		if (m_isFxaaAvailable &&
		    (m_fxaaRootSignature != nullptr) &&
		    (m_fxaaPipelineState != nullptr) &&
		    (m_sceneSrvHeap != nullptr))
		{
			const D3D12_RESOURCE_DESC sceneDescription =
			    m_sceneColorTargets[currentFrame]->GetDesc();
			const float width = static_cast<float>(sceneDescription.Width);
			const float height =
			    static_cast<float>(sceneDescription.Height);
			if ((width > 0.5F) && (height > 0.5F))
			{
				D3D12_VIEWPORT viewport{};
				viewport.TopLeftX = 0.0F;
				viewport.TopLeftY = 0.0F;
				viewport.Width = width;
				viewport.Height = height;
				viewport.MinDepth = 0.0F;
				viewport.MaxDepth = 1.0F;
				D3D12_RECT scissorRect{};
				scissorRect.left = 0;
				scissorRect.top = 0;
				scissorRect.right =
				    static_cast<LONG>(sceneDescription.Width);
				scissorRect.bottom =
				    static_cast<LONG>(sceneDescription.Height);

				ID3D12DescriptorHeap* const descriptorHeaps[] = {
					m_sceneSrvHeap.Get()
				};
				m_commandList->SetDescriptorHeaps(
				    static_cast<UINT>(std::size(descriptorHeaps)),
				    descriptorHeaps);
				m_commandList->RSSetViewports(1, &viewport);
				m_commandList->RSSetScissorRects(1, &scissorRect);
				m_commandList->OMSetRenderTargets(
				    1,
				    &swapChainRtvHandle,
				    FALSE,
				    nullptr);
				m_commandList->SetGraphicsRootSignature(
				    m_fxaaRootSignature.Get());
				m_commandList->SetPipelineState(
				    m_fxaaPipelineState.Get());
				m_commandList->IASetPrimitiveTopology(
				    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				const float fxaaParameters[Common::FxaaParameterCount]{
					1.0F / width,
					1.0F / height,
					m_renderQuality.fxaaEdgeThreshold,
					m_renderQuality.fxaaEdgeThresholdMin,
					m_renderQuality.fxaaBlendFactor,
					1.0F
				};
				m_commandList->SetGraphicsRootDescriptorTable(
				    0,
				    GetCurrentSceneSrvGpuHandle());
				m_commandList->SetGraphicsRoot32BitConstants(
				    1,
				    Common::FxaaParameterCount,
				    fxaaParameters,
				    0);
				m_commandList->DrawInstanced(3, 1, 0, 0);
				usedFxaaPass = true;
			}
		}

		if (usedFxaaPass)
		{
			m_postProcessPathStatus = L"fxaa";
			SetDebugDrawPathStatus(m_debugDrawPathStatus);

			D3D12_RESOURCE_BARRIER toPresentBarrier{};
			toPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			toPresentBarrier.Transition.pResource =
			    m_renderTargets[currentFrame].Get();
			toPresentBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			toPresentBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_PRESENT;
			toPresentBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_commandList->ResourceBarrier(1, &toPresentBarrier);
		}
		else
		{
			m_postProcessPathStatus = L"copy";
			SetDebugDrawPathStatus(m_debugDrawPathStatus);

			D3D12_RESOURCE_BARRIER toCopyBarriers[2]{};
			D3D12_RESOURCE_BARRIER& backBufferToCopyBarrier =
			    toCopyBarriers[0];
			backBufferToCopyBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			backBufferToCopyBarrier.Flags =
			    D3D12_RESOURCE_BARRIER_FLAG_NONE;
			backBufferToCopyBarrier.Transition.pResource =
			    m_renderTargets[currentFrame].Get();
			backBufferToCopyBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			backBufferToCopyBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_COPY_DEST;
			backBufferToCopyBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			D3D12_RESOURCE_BARRIER& sceneToCopyBarrier = toCopyBarriers[1];
			sceneToCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneToCopyBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneToCopyBarrier.Transition.pResource =
			    m_sceneColorTargets[currentFrame].Get();
			sceneToCopyBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			sceneToCopyBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_COPY_SOURCE;
			sceneToCopyBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_commandList->ResourceBarrier(
			    static_cast<UINT>(std::size(toCopyBarriers)),
			    toCopyBarriers);

			m_commandList->CopyResource(
			    m_renderTargets[currentFrame].Get(),
			    m_sceneColorTargets[currentFrame].Get());

			D3D12_RESOURCE_BARRIER fromCopyBarriers[2]{};
			D3D12_RESOURCE_BARRIER& sceneFromCopyBarrier =
			    fromCopyBarriers[0];
			sceneFromCopyBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneFromCopyBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneFromCopyBarrier.Transition.pResource =
			    m_sceneColorTargets[currentFrame].Get();
			sceneFromCopyBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_COPY_SOURCE;
			sceneFromCopyBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			sceneFromCopyBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			D3D12_RESOURCE_BARRIER& backBufferFromCopyBarrier =
			    fromCopyBarriers[1];
			backBufferFromCopyBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			backBufferFromCopyBarrier.Flags =
			    D3D12_RESOURCE_BARRIER_FLAG_NONE;
			backBufferFromCopyBarrier.Transition.pResource =
			    m_renderTargets[currentFrame].Get();
			backBufferFromCopyBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_COPY_DEST;
			backBufferFromCopyBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_PRESENT;
			backBufferFromCopyBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_commandList->ResourceBarrier(
			    static_cast<UINT>(std::size(fromCopyBarriers)),
			    fromCopyBarriers);
		}

		if (FAILED(m_commandList->Close()))
		{
			FailDesktopRendering(L"cmdlist-close-failed");
			return;
		}

		ID3D12CommandList* commandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(static_cast<UINT>(std::size(commandLists)), commandLists);

		const UINT64 signal = m_nextFenceValue++;
		if (FAILED(m_commandQueue->Signal(m_fence.Get(), signal)))
		{
			FailDesktopRendering(L"frame-signal-failed");
			return;
		}
		m_fenceValues[m_frameIndex] = signal;
		m_presentPending = true;
		(void)TryPresentFrame();
	}

	void Dx12RenderBackend::AppendTrajectoryPoint(const Physics::BallState& ballState)
	{
		Common::UpdatePhaseTrajectoryTrails(m_trajectoryTrails, ballState,
		    m_fieldPresentation, std::chrono::steady_clock::now());
	}

	void Dx12RenderBackend::DrawDebugMarkers(ID3D12GraphicsCommandList* const commandList)
	{
		if ((commandList == nullptr) || (m_windowHandle == nullptr))
		{
			SetDebugDrawPathStatus(L"frame-invalid");
			return;
		}

		const LONG width = static_cast<LONG>(m_desktopRenderStatus.width);
		const LONG height = static_cast<LONG>(m_desktopRenderStatus.height);
		if ((width <= 0) || (height <= 0))
		{
			SetDebugDrawPathStatus(L"invalid-extent");
			return;
		}

		RepairDebugDrawResources();
		bool debugDrawUsable = HasDebugDrawResources();
		if (!debugDrawUsable && (m_device != nullptr))
		{
			const bool pipelineReady =
			    (m_debugRootSignature != nullptr) &&
			    (m_debugPipelineState != nullptr);
			if (!pipelineReady && !InitializeDebugPipeline())
			{
				SetDebugDrawPathStatus(L"pipeline-reinit-failed");
			}

			const bool vertexBufferReady =
			    (m_debugVertexBuffer != nullptr) &&
			    (m_debugVertexCapacity != 0U) &&
			    (m_debugVertexBufferView.SizeInBytes != 0U) &&
			    (m_debugVertexBufferView.StrideInBytes != 0U);
			if (!vertexBufferReady && !InitializeDebugVertexBuffer() &&
			    (m_debugDrawPathStatus != L"pipeline-reinit-failed"))
			{
				SetDebugDrawPathStatus(L"vertex-reinit-failed");
			}

			RepairDebugDrawResources();
			debugDrawUsable = HasDebugDrawResources();
			if (debugDrawUsable)
			{
				SetDebugDrawPathStatus(L"resource-reinit-ok");
			}
		}
		if (debugDrawUsable)
		{
			m_consecutiveDebugFallbackFrames = 0U;
		}

		if (!debugDrawUsable)
		{
			++m_consecutiveDebugFallbackFrames;
			const bool hasPersistentFallback =
			    (m_consecutiveDebugFallbackFrames >= 120U);
			const std::wstring fallbackPrefix =
			    hasPersistentFallback ? L"persistent-" : L"";
			const std::wstring missingReason = BuildDebugResourceMissingReason();
			const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = GetCurrentRtvHandle();
			bool drewDynamicOverlay = false;
			bool usedWorldFallback = false;
			auto drawRect = [&](LONG left,
			                    LONG top,
			                    LONG right,
			                    LONG bottom,
			                    const float* const color) -> void
			{
				const LONG clampedLeft = (std::max)(0L, left);
				const LONG clampedTop = (std::max)(0L, top);
				const LONG clampedRight = (std::min)(width, right);
				const LONG clampedBottom = (std::min)(height, bottom);
				if ((clampedLeft >= clampedRight) || (clampedTop >= clampedBottom))
				{
					return;
				}

				const D3D12_RECT rect{
					clampedLeft,
					clampedTop,
					clampedRight,
					clampedBottom
				};
				commandList->ClearRenderTargetView(rtvHandle, color, 1, &rect);
			};
			auto drawFallbackMarker = [&](const ScreenPoint& point,
			                              const LONG halfSize,
			                              const float* const color) -> void
			{
				const LONG centerX = static_cast<LONG>(std::llround(point.x));
				const LONG centerY = static_cast<LONG>(std::llround(point.y));
				drawRect(
				    centerX - halfSize,
				    centerY - halfSize,
				    centerX + halfSize + 1,
				    centerY + halfSize + 1,
				    color);
				drewDynamicOverlay = true;
			};
			auto drawFallbackLine = [&](const ScreenPoint& startPoint,
			                            const ScreenPoint& endPoint,
			                            const LONG halfSize,
			                            const float* const color) -> void
			{
				const double dx = endPoint.x - startPoint.x;
				const double dy = endPoint.y - startPoint.y;
				const double length = std::sqrt((dx * dx) + (dy * dy));
				const int stepCount = (std::max)(1,
				                                 static_cast<int>(std::ceil(length / 8.0)));
				for (int stepIndex = 0; stepIndex <= stepCount; ++stepIndex)
				{
					const double blend = static_cast<double>(stepIndex) /
					                     static_cast<double>(stepCount);
					const ScreenPoint point{
						startPoint.x + (dx * blend),
						startPoint.y + (dy * blend),
						startPoint.depth + static_cast<float>((endPoint.depth - startPoint.depth) * blend)
					};
					drawFallbackMarker(point, halfSize, color);
				}
			};
			const Common::ViewProjectionState mirrorViewState = m_mirrorViewState;
			if (mirrorViewState.isValid)
			{
				usedWorldFallback = true;
				Common::EmitProjectedPhaseDebugWorldOverlay(
				    mirrorViewState,
				    width,
				    height,
				    m_trajectoryTrails, m_overlayVisibility,
				    m_latestBallState,
				    [&](const ScreenPoint& screenPoint, const LONG halfSize, const float* color) -> void
				    {
					    drawFallbackMarker(screenPoint, halfSize, color);
				    },
				    [&](const ScreenPoint& startPoint,
				        const ScreenPoint& endPoint,
				        const LONG halfSize,
				        const float* color) -> void
				    {
					    drawFallbackLine(startPoint, endPoint, halfSize, color);
				    },
				    true,
				    m_overlayVisibility.showFloorMesh,
				    true,
			    m_fieldPresentation.ballVisible);
				Common::EmitProjectedZonePassCircleOverlay(
				    mirrorViewState,
				    width,
				    height,
				    m_duelZonePassMarker,
				    m_latestBallState.properties.radiusMeters,
				    [&](const ScreenPoint& startPoint,
				        const ScreenPoint& endPoint,
				        const LONG halfSize,
				        const float* color) -> void
				    {
					    drawFallbackLine(startPoint, endPoint, halfSize, color);
				    });
				Common::EmitProjectedBatOverlay(
				    mirrorViewState,
				    width,
				    height,
				    m_batOverlayState,
				    [&](const ScreenPoint& startPoint,
				        const ScreenPoint& endPoint,
				        const LONG halfSize,
				        const float* color) -> void
				    {
					    drawFallbackLine(startPoint, endPoint, halfSize, color);
				    });
			}
			else
			{
					m_trajectoryTrails.ForEachVisible(m_overlayVisibility, [&](const Common::TrajectoryRenderSpan& trail) {
for (const Physics::Vector3& trailPoint : trail.Points())
					{
						const ScreenPoint screenPoint =
						    Common::ProjectLegacyWorldToScreen(trailPoint, width, height);
						drawFallbackMarker(
						    screenPoint,
						    Common::DebugWorldStyle::TrailHalfSizePixels,
						    Common::DebugWorldStyle::TrailColor.data());
					}
});
				const ScreenPoint ballScreenPoint =
				    Common::ProjectLegacyWorldToScreen(m_latestBallState.position, width, height);
				if (m_fieldPresentation.ballVisible) drawFallbackMarker(
				    ballScreenPoint,
				    Common::LegacyBallMarkerHalfSizePixels(m_latestBallState.properties.radiusMeters),
				    Common::DebugWorldStyle::BallColor.data());
				if (m_duelZonePassMarker.has_value())
				{
					constexpr int ZoneCircleSegmentCount = 24;
					const Physics::Vector3 centerWorldPoint =
					    Common::ConvertSimulationPositionToWorld(
					        m_duelZonePassMarker.value());
					const double CircleRadiusMeters = m_latestBallState.properties.radiusMeters;
					for (int segmentIndex = 0; segmentIndex < ZoneCircleSegmentCount;
					     ++segmentIndex)
					{
						const double startAngle =
						    (2.0 * std::numbers::pi *
						     static_cast<double>(segmentIndex)) /
						    static_cast<double>(ZoneCircleSegmentCount);
						const double endAngle =
						    (2.0 * std::numbers::pi *
						     static_cast<double>(segmentIndex + 1)) /
						    static_cast<double>(ZoneCircleSegmentCount);
						const Physics::Vector3 startWorldPoint{
							centerWorldPoint.x + (std::cos(startAngle) * CircleRadiusMeters),
							centerWorldPoint.y + (std::sin(startAngle) * CircleRadiusMeters),
							centerWorldPoint.z
						};
						const Physics::Vector3 endWorldPoint{
							centerWorldPoint.x + (std::cos(endAngle) * CircleRadiusMeters),
							centerWorldPoint.y + (std::sin(endAngle) * CircleRadiusMeters),
							centerWorldPoint.z
						};
						const ScreenPoint segmentStart =
						    Common::ProjectLegacyWorldToScreen(
						        startWorldPoint,
						        width,
						        height);
						const ScreenPoint segmentEnd =
						    Common::ProjectLegacyWorldToScreen(
						        endWorldPoint,
						        width,
						        height);
						drawFallbackLine(
						    segmentStart,
						    segmentEnd,
						    Common::DebugWorldStyle::WorldLineHalfSizePixels,
						    Common::DebugWorldStyle::ZoneColor.data());
					}
				}
			}
			if (!drewDynamicOverlay)
			{
				const LONG centerX = width / 2;
				const LONG centerY = height / 2;
				const LONG armLength = (std::max)(8L, (std::min)(width, height) / 10L);
				const LONG lineHalfThickness = 2L;
				const LONG markerHalfSize = 8L;
				drawRect(
				    centerX - armLength,
				    centerY - lineHalfThickness,
				    centerX + armLength + 1,
				    centerY + lineHalfThickness + 1,
				    Common::DebugWorldStyle::ZoneColor.data());
				drawRect(
				    centerX - lineHalfThickness,
				    centerY - armLength,
				    centerX + lineHalfThickness + 1,
				    centerY + armLength + 1,
				    Common::DebugWorldStyle::ZoneColor.data());
				drawRect(
				    centerX - markerHalfSize,
				    centerY - markerHalfSize,
				    centerX + markerHalfSize + 1,
				    centerY + markerHalfSize + 1,
				    Common::DebugWorldStyle::PlateColor.data());
				SetDebugDrawPathStatus(
				    fallbackPrefix + L"center-rect-fallback-" + missingReason);
			}
			else if (usedWorldFallback)
			{
				SetDebugDrawPathStatus(
				    fallbackPrefix + L"world-rect-fallback-" + missingReason);
			}
			else
			{
				SetDebugDrawPathStatus(
				    fallbackPrefix + L"legacy-rect-fallback-" + missingReason);
			}

			return;
		}

		std::vector<Common::DebugOverlay::Vertex> opaqueVertices{};
		std::vector<Common::UiText::AtlasVertex> alphaVertices{};
		std::vector<Common::DebugOverlay::Vertex> alphaScratch{};
		opaqueVertices.reserve(8192);
		alphaVertices.reserve(1024);
		alphaScratch.reserve(6);
		Common::UiText::AtlasDrawContext textContext{ m_textGlyphAtlas, m_textLayouts, alphaVertices };
		bool hasVisibleCoverage = false;
		const bool hasWorldMirror = m_mirrorViewState.isValid;
		bool usedCenterCoverageFallback = false;

		auto selectVertexBucket =
		    [&](const float* const color) -> std::vector<Common::DebugOverlay::Vertex>*
		{
			if ((color != nullptr) && (color[3] < 0.999F))
			{
				alphaScratch.clear();
				return &alphaScratch;
			}

			return &opaqueVertices;
		};
		const auto appendAlphaScratch = [&](const float* color)
		{
			if (color == nullptr || color[3] >= 0.999F) return;
			constexpr float whiteUv = 1.5F / Common::UiText::GlyphAtlas::Width;
			for (const auto& vertex : alphaScratch)
			{
				const auto clip = Common::ClipDepthFromNormalizedDepth(vertex.position[2],
				    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
				alphaVertices.push_back({ { vertex.position[0] * clip.w, vertex.position[1] * clip.w,
				    clip.z, clip.w }, { whiteUv, whiteUv },
				    { vertex.color[0], vertex.color[1], vertex.color[2], vertex.color[3] } });
			}
		};

		auto appendMarker = [&](const ScreenPoint& centerPoint, const LONG halfSize, const float* color) -> void
		{
			auto* const vertices = selectVertexBucket(color);
			Common::DebugOverlay::AppendMarkerQuad(
			    vertices,
			    centerPoint.x,
			    centerPoint.y,
			    halfSize,
			    color,
			    width,
			    height,
			    centerPoint.depth);
			appendAlphaScratch(color);
			const double markerMinX = centerPoint.x - static_cast<double>(halfSize);
			const double markerMaxX = centerPoint.x + static_cast<double>(halfSize) + 1.0;
			const double markerMinY = centerPoint.y - static_cast<double>(halfSize);
			const double markerMaxY = centerPoint.y + static_cast<double>(halfSize) + 1.0;
			const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
			const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
			if ((markerMaxX >= 0.0) && (markerMinX <= screenMaxX) &&
			    (markerMaxY >= 0.0) && (markerMinY <= screenMaxY))
			{
				hasVisibleCoverage = true;
			}
		};

		auto appendLine = [&](const ScreenPoint& startPoint,
		                      const ScreenPoint& endPoint,
		                      const LONG halfSize,
		                      const float* color) -> void
		{
			auto* const vertices = selectVertexBucket(color);
			Common::DebugOverlay::AppendThickScreenLine(
			    vertices,
			    startPoint,
			    endPoint,
			    halfSize,
			    color,
			    width,
			    height);
			appendAlphaScratch(color);
			const double linePadding = static_cast<double>(halfSize) + 1.0;
			const double lineMinX = (std::min)(startPoint.x, endPoint.x) - linePadding;
			const double lineMaxX = (std::max)(startPoint.x, endPoint.x) + linePadding;
			const double lineMinY = (std::min)(startPoint.y, endPoint.y) - linePadding;
			const double lineMaxY = (std::max)(startPoint.y, endPoint.y) + linePadding;
			const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
			const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
			if ((lineMaxX >= 0.0) && (lineMinX <= screenMaxX) &&
			    (lineMaxY >= 0.0) && (lineMinY <= screenMaxY))
			{
				hasVisibleCoverage = true;
			}
		};

		auto appendTriangle = [&](const ScreenPoint& p0,
		                          const ScreenPoint& p1,
		                          const ScreenPoint& p2,
		                          const float* color) -> void
		{
			if (color != nullptr && color[3] < 0.999F)
				Common::UiText::AppendUntexturedTriangle(alphaVertices, p0, p1, p2, color, width, height);
			else
				Common::DebugOverlay::AppendScreenTriangle(&opaqueVertices, p0, p1, p2, color, width, height);
			const double triangleMinX = (std::min)(p0.x, (std::min)(p1.x, p2.x));
			const double triangleMaxX = (std::max)(p0.x, (std::max)(p1.x, p2.x));
			const double triangleMinY = (std::min)(p0.y, (std::min)(p1.y, p2.y));
			const double triangleMaxY = (std::max)(p0.y, (std::max)(p1.y, p2.y));
			const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
			const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
			if ((triangleMaxX >= 0.0) && (triangleMinX <= screenMaxX) &&
			    (triangleMaxY >= 0.0) && (triangleMinY <= screenMaxY))
			{
				hasVisibleCoverage = true;
			}
		};

		const Common::ViewProjectionState mirrorViewState = m_mirrorViewState;
		if (!mirrorViewState.isValid)
		{
			m_trajectoryTrails.ForEachVisible(m_overlayVisibility, [&](const Common::TrajectoryRenderSpan& trail) {
for (const Physics::Vector3& trailPoint : trail.Points())
			{
				const ScreenPoint screenPoint =
				    Common::ProjectLegacyWorldToScreen(trailPoint, width, height);
				appendMarker(screenPoint, Common::DebugWorldStyle::TrailHalfSizePixels, Common::DebugWorldStyle::TrailColor.data());
			}
});

			const ScreenPoint ballScreenPoint =
			    Common::ProjectLegacyWorldToScreen(m_latestBallState.position, width, height);
			if (m_fieldPresentation.ballVisible) appendMarker(ballScreenPoint, Common::LegacyBallMarkerHalfSizePixels(m_latestBallState.properties.radiusMeters), Common::DebugWorldStyle::BallColor.data());
			if (m_duelZonePassMarker.has_value())
			{
				constexpr int ZoneCircleSegmentCount = 24;
				const double radiusPixels =
				    static_cast<double>(Common::LegacyBallMarkerHalfSizePixels(m_latestBallState.properties.radiusMeters));
				const ScreenPoint centerPoint =
				    Common::ProjectLegacyWorldToScreen(
				        m_duelZonePassMarker.value(),
				        width,
				        height);
				for (int segmentIndex = 0; segmentIndex < ZoneCircleSegmentCount;
				     ++segmentIndex)
				{
					const double startAngle =
					    (2.0 * std::numbers::pi * static_cast<double>(segmentIndex)) /
					    static_cast<double>(ZoneCircleSegmentCount);
					const double endAngle =
					    (2.0 * std::numbers::pi *
					     static_cast<double>(segmentIndex + 1)) /
					    static_cast<double>(ZoneCircleSegmentCount);
					const ScreenPoint segmentStart{
						centerPoint.x + (std::cos(startAngle) * radiusPixels),
						centerPoint.y + (std::sin(startAngle) * radiusPixels),
						centerPoint.depth
					};
					const ScreenPoint segmentEnd{
						centerPoint.x + (std::cos(endAngle) * radiusPixels),
						centerPoint.y + (std::sin(endAngle) * radiusPixels),
						centerPoint.depth
					};
					appendLine(
					    segmentStart,
					    segmentEnd,
					    Common::DebugWorldStyle::WorldLineHalfSizePixels,
					    Common::DebugWorldStyle::ZoneColor.data());
				}
			}
		}
		else
		{
			Common::UiText::EmitProjectedFieldPresentation(m_fieldPresentation, mirrorViewState,
			    width, height, textContext, appendTriangle);

			if (m_overlayVisibility.showFloorMesh)
			{
				Common::EmitProjectedFloorMeshOverlay(
				    mirrorViewState,
				    width,
				    height,
				    [&](const ScreenPoint& p0,
				        const ScreenPoint& p1,
				        const ScreenPoint& p2,
				        const float* color) -> void
				    {
					    appendTriangle(p0, p1, p2, color);
				    });
			}

			Common::EmitProjectedPhaseDebugWorldOverlay(
			    mirrorViewState,
			    width,
			    height,
			    m_trajectoryTrails, m_overlayVisibility,
			    m_latestBallState,
			    [&](const ScreenPoint& screenPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    appendMarker(screenPoint, halfSize, color);
			    },
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    appendLine(
				        startPoint,
				        endPoint,
				        halfSize,
				        color);
			    },
			    false,
			    false,
			    true,
			    false);
			Common::EmitProjectedZonePassCircleOverlay(
			    mirrorViewState,
			    width,
			    height,
			    m_duelZonePassMarker,
			    m_latestBallState.properties.radiusMeters,
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    appendLine(
				        startPoint,
				        endPoint,
				        halfSize,
				        color);
			    });
			Common::EmitProjectedSolidBatOverlay(
			    mirrorViewState,
			    width,
			    height,
			    m_batOverlayState,
			    [&](const ScreenPoint& p0,
			        const ScreenPoint& p1,
			        const ScreenPoint& p2,
			        const float* color) -> void
			    {
				    appendTriangle(p0, p1, p2, color);
			    });

			if (m_fieldPresentation.ballVisible)
			{
				Common::EmitProjectedBallShadow(mirrorViewState, width, height, m_latestBallState,
				    [&](const ScreenPoint& p0, const ScreenPoint& p1, const ScreenPoint& p2, const float* color) -> void
				    { appendTriangle(p0, p1, p2, color); });
			}
			Common::EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
			    mirrorViewState,
			    width,
			    height,
			    m_trajectoryTrails, m_overlayVisibility,
			    m_latestBallState,
			    [&](const ScreenPoint& p0,
			        const ScreenPoint& p1,
			        const ScreenPoint& p2,
			        const float* color) -> void
			    {
				    appendTriangle(p0, p1, p2, color);
			    },
			    std::clamp(
			        m_renderQuality.trailOpacityScale,
			        0.0F,
			        1.5F), m_fieldPresentation.ballVisible);
		}

		if (!hasVisibleCoverage && !hasWorldMirror)
		{
			m_trajectoryTrails.ForEachVisible(m_overlayVisibility, [&](const Common::TrajectoryRenderSpan& trail) {
for (const Physics::Vector3& trailPoint : trail.Points())
			{
				const ScreenPoint screenPoint =
				    Common::ProjectLegacyWorldToScreen(trailPoint, width, height);
				appendMarker(
				    screenPoint,
				    Common::DebugWorldStyle::TrailHalfSizePixels,
				    Common::DebugWorldStyle::TrailColor.data());
			}
});

			const ScreenPoint ballScreenPoint =
			    Common::ProjectLegacyWorldToScreen(m_latestBallState.position, width, height);
			if (m_fieldPresentation.ballVisible) appendMarker(
			    ballScreenPoint,
			    Common::LegacyBallMarkerHalfSizePixels(m_latestBallState.properties.radiusMeters),
			    Common::DebugWorldStyle::BallColor.data());
		}

		if (!hasVisibleCoverage && !hasWorldMirror)
		{
			usedCenterCoverageFallback = true;
			const double centerX =
			    0.5 * static_cast<double>((std::max)(1L, width - 1));
			const double centerY =
			    0.5 * static_cast<double>((std::max)(1L, height - 1));
			const double reticleHalfSpan =
			    (std::max)(4.0,
			               (std::min)(static_cast<double>(width),
			                          static_cast<double>(height)) *
			                   0.08);
			constexpr float kFallbackDepth = 0.05F;
			const ScreenPoint centerPoint{ centerX, centerY, kFallbackDepth };
			const ScreenPoint horizontalStart{
				centerX - reticleHalfSpan,
				centerY,
				kFallbackDepth
			};
			const ScreenPoint horizontalEnd{
				centerX + reticleHalfSpan,
				centerY,
				kFallbackDepth
			};
			const ScreenPoint verticalStart{
				centerX,
				centerY - reticleHalfSpan,
				kFallbackDepth
			};
			const ScreenPoint verticalEnd{
				centerX,
				centerY + reticleHalfSpan,
				kFallbackDepth
			};
			appendLine(
			    horizontalStart,
			    horizontalEnd,
			    Common::DebugWorldStyle::WorldLineHalfSizePixels,
			    Common::DebugWorldStyle::ZoneColor.data());
			appendLine(
			    verticalStart,
			    verticalEnd,
			    Common::DebugWorldStyle::WorldLineHalfSizePixels,
			    Common::DebugWorldStyle::ZoneColor.data());
			appendMarker(
			    centerPoint,
			    Common::DebugWorldStyle::BallHalfSizePixels,
			    Common::DebugWorldStyle::PlateColor.data());
		}

		const std::size_t requiredVertices = opaqueVertices.size();
		constexpr std::size_t maximumVertices = (std::numeric_limits<UINT>::max)() /
		    sizeof(Common::DebugOverlay::Vertex);
		if (requiredVertices > maximumVertices)
		{
			FailDesktopRendering(L"vertex-buffer-size-overflow");
			return;
		}
		if (requiredVertices > m_debugVertexCapacity)
		{
			std::size_t capacity = (std::max)(std::size_t{ MaxDebugVertices },
			    static_cast<std::size_t>(m_debugVertexCapacity));
			while (capacity < requiredVertices)
				capacity = capacity <= maximumVertices / 2 ? capacity * 2 : requiredVertices;
			if (!Common::DebugOverlay::InitializeVertexBuffer(m_device.Get(), static_cast<UINT>(capacity),
			    &m_frameDebugVertexBuffers[m_frameIndex], &m_frameDebugVertexViews[m_frameIndex],
			    &m_frameDebugVertexCapacities[m_frameIndex]))
			{
				FailDesktopRendering(L"vertex-buffer-grow-failed");
				return;
			}
			m_debugVertexBuffer = m_frameDebugVertexBuffers[m_frameIndex];
			m_debugVertexBufferView = m_frameDebugVertexViews[m_frameIndex];
			m_debugVertexCapacity = m_frameDebugVertexCapacities[m_frameIndex];
		}
		Common::UiText::SortAtlasTrianglesBackToFront(alphaVertices);
		const UINT opaqueVertexCount = static_cast<UINT>(opaqueVertices.size());
		if ((opaqueVertexCount == 0U) && alphaVertices.empty())
		{
			m_desktopRenderStatus.atlasVertexCount = 0;
			SetDebugDrawPathStatus(hasWorldMirror ? L"empty-world-shader" : L"empty-shader");
			return;
		}

		if (opaqueVertexCount > 0U && !Common::DebugOverlay::UploadVertices(
		        m_debugVertexBuffer.Get(),
		        opaqueVertices))
		{
			SetDebugDrawPathStatus(L"upload-failed");
			return;
		}

		if ((opaqueVertexCount > 0U) &&
		    !Common::DebugOverlay::IssueDrawCommands(
		        commandList,
		        width,
		        height,
		        m_debugRootSignature.Get(),
		        m_debugPipelineState.Get(),
		        m_debugVertexBufferView,
		        opaqueVertexCount,
		        0U))
		{
			SetDebugDrawPathStatus(L"opaque-draw-failed");
			return;
		}

		if (!m_textAtlasPipelines[m_frameIndex].UploadAndDraw(commandList, m_textGlyphAtlas,
		    alphaVertices, width, height))
		{
			FailDesktopRendering(L"text-atlas-draw-failed");
			return;
		}
		m_desktopRenderStatus.atlasGlyphCount = static_cast<std::uint32_t>(m_textGlyphAtlas.Size());
		m_desktopRenderStatus.atlasLayoutCount = static_cast<std::uint32_t>(m_textLayouts.Size());
		m_desktopRenderStatus.atlasVertexCount = static_cast<std::uint32_t>(alphaVertices.size());
		m_desktopRenderStatus.atlasUploadCount = 0;
		for (const auto& pipeline : m_textAtlasPipelines)
			m_desktopRenderStatus.atlasUploadCount += pipeline.UploadCount();

		if (usedCenterCoverageFallback)
		{
			SetDebugDrawPathStatus(L"center-shader-fallback");
		}
		else if (hasWorldMirror)
		{
			SetDebugDrawPathStatus(L"world-shader");
		}
		else
		{
			SetDebugDrawPathStatus(L"legacy-shader");
		}
	}

	void Dx12RenderBackend::Shutdown()
	{
		if (m_isInitialized)
		{
			Common::D3d12RenderFrame::EnsureSafeResourceRelease(m_device.Get(), WaitForGpu());
		}

		if (m_fenceEvent != nullptr)
		{
			if (CloseHandle(m_fenceEvent) == FALSE)
			{
				OutputDebugStringW(L"[DX12] failed to close fence event handle.\n");
			}
			m_fenceEvent = nullptr;
		}
		if (m_frameLatencyEvent)
		{
			CloseHandle(m_frameLatencyEvent);
			m_frameLatencyEvent = nullptr;
		}

		m_commandList.Reset();
		m_debugVertexBuffer.Reset();
		for (auto& buffer : m_frameDebugVertexBuffers) buffer.Reset();
		m_frameDebugVertexViews = {};
		m_frameDebugVertexCapacities = {};
		m_debugPipelineState.Reset();
		m_textAtlasPipelines = {};
		m_debugRootSignature.Reset();
		m_fxaaPipelineState.Reset();
		m_fxaaRootSignature.Reset();
		for (auto& allocator : m_commandAllocators)
		{
			allocator.Reset();
		}

		for (auto& renderTarget : m_renderTargets)
		{
			renderTarget.Reset();
		}

		for (auto& msaaRenderTarget : m_msaaRenderTargets)
		{
			msaaRenderTarget.Reset();
		}

		for (auto& sceneColorTarget : m_sceneColorTargets)
		{
			sceneColorTarget.Reset();
		}

		for (auto& depthBuffer : m_depthBuffers)
		{
			depthBuffer.Reset();
		}

		m_rtvHeap.Reset();
		m_msaaRtvHeap.Reset();
		m_dsvHeap.Reset();
		m_sceneRtvHeap.Reset();
		m_sceneSrvHeap.Reset();
		m_swapChain.Reset();
		m_commandQueue.Reset();
		m_device.Reset();
		m_adapter.Reset();
		m_factory.Reset();
		m_fence.Reset();

		m_debugVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
		m_debugVertexCapacity = 0;
		m_rtvDescriptorSize = 0;
		m_dsvDescriptorSize = 0;
		m_srvDescriptorSize = 0;
		m_msaaSampleCount = 1U;
		m_isFxaaAvailable = false;
		m_frameIndex = 0;
		m_fenceValues = {};
		m_nextFenceValue = 1;
		m_presentPending = false;
		m_windowHandle = nullptr;
		m_isInitialized = false;
		m_frameReady = false;
		m_desktopRenderStatus = {};
		m_postProcessPathStatus = L"copy";
		m_trajectoryTrails.Clear();
		m_duelZonePassMarker.reset();
		m_batOverlayState.reset();
		SetDebugDrawPathStatus(L"shutdown");
	}

	std::wstring_view Dx12RenderBackend::GetBackendName() const noexcept
	{
		return m_backendNameCache;
	}

	Common::GraphicsBinding Dx12RenderBackend::GetGraphicsBinding() const noexcept
	{
		return Common::GraphicsBinding{
			m_device.Get(),
			m_commandQueue.Get()
		};
	}

	void Dx12RenderBackend::SetDesktopMirrorView(const Common::ViewProjectionState& mirrorViewState)
	{
		m_mirrorViewState = mirrorViewState;
	}

	void Dx12RenderBackend::SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility)
	{
		m_overlayVisibility = overlayVisibility;
	}

	void Dx12RenderBackend::SetRenderQuality(
	    const Common::RenderQualitySettings& renderQuality)
	{
		m_renderQuality = Common::NormalizeRenderQualitySettings(renderQuality);
	}

	void Dx12RenderBackend::SetDuelZonePassMarker(
	    const std::optional<Physics::Vector3>& zonePassMarker)
	{
		m_duelZonePassMarker = zonePassMarker;
	}

	void Dx12RenderBackend::SetBatOverlayState(
	    const std::optional<Common::BatOverlayState>& batOverlayState)
	{
		m_batOverlayState = batOverlayState;
	}

	bool Dx12RenderBackend::WaitForGpu()
	{
		if (m_fence == nullptr || m_commandQueue == nullptr || m_fenceEvent == nullptr)
		{
			return false;
		}

		const UINT64 signalValue = m_nextFenceValue++;
		if (FAILED(m_commandQueue->Signal(m_fence.Get(), signalValue)))
		{
			return false;
		}

		if (FAILED(Common::D3d12RenderFrame::WaitForFence(m_fence.Get(), signalValue, m_fenceEvent)))
		{
			SetDebugDrawPathStatus(L"fence-wait-failed");
			return false;
		}
		m_fenceValues.fill(0);
		return true;
	}

	bool Dx12RenderBackend::TryPresentFrame()
	{
		if (!m_presentPending) return true;
		const UINT flags = m_mirrorViewState.isValid ? DXGI_PRESENT_DO_NOT_WAIT : 0;
		const HRESULT result = m_swapChain->Present(1, flags);
		if (result == DXGI_ERROR_WAS_STILL_DRAWING) return false;
		if (FAILED(result))
		{
			FailDesktopRendering(L"present-failed");
			return false;
		}
		m_presentPending = false;
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		++m_desktopRenderStatus.presentedFrameCount;
		return true;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Dx12RenderBackend::GetCurrentRtvHandle() const noexcept
	{
		const bool useMsaaRenderTarget =
		    (m_msaaSampleCount > 1U) && (m_msaaRtvHeap != nullptr);
		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		    useMsaaRenderTarget
		        ? m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart()
		        : m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * static_cast<SIZE_T>(m_rtvDescriptorSize);
		return descriptorHandle;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Dx12RenderBackend::GetCurrentDsvHandle() const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * static_cast<SIZE_T>(m_dsvDescriptorSize);
		return descriptorHandle;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Dx12RenderBackend::GetCurrentSceneRtvHandle() const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		    m_sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr +=
		    static_cast<SIZE_T>(m_frameIndex) *
		    static_cast<SIZE_T>(m_rtvDescriptorSize);
		return descriptorHandle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE Dx12RenderBackend::GetCurrentSceneSrvGpuHandle() const noexcept
	{
		D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		    m_sceneSrvHeap->GetGPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr +=
		    static_cast<UINT64>(m_frameIndex) *
		    static_cast<UINT64>(m_srvDescriptorSize);
		return descriptorHandle;
	}
} // namespace BaseballSimulatorVR::Render

#else

namespace BaseballSimulatorVR::Render
{
	Dx12RenderBackend::~Dx12RenderBackend() = default;

	bool Dx12RenderBackend::Initialize(const HWND)
	{
		return false;
	}

	void Dx12RenderBackend::BeginFrame()
	{
	}

	void Dx12RenderBackend::RenderSimulation(const Physics::BallState&)
	{
	}

	void Dx12RenderBackend::EndFrame()
	{
	}

	void Dx12RenderBackend::Shutdown()
	{
	}

	std::wstring_view Dx12RenderBackend::GetBackendName() const noexcept
	{
		return L"DX12(unavailable)";
	}

	Common::GraphicsBinding Dx12RenderBackend::GetGraphicsBinding() const noexcept
	{
		return Common::GraphicsBinding{};
	}

	void Dx12RenderBackend::SetDesktopMirrorView(const Common::ViewProjectionState&)
	{
	}

	void Dx12RenderBackend::SetOverlayVisibility(const Common::OverlayVisibilitySettings&)
	{
	}

	void Dx12RenderBackend::SetRenderQuality(const Common::RenderQualitySettings&)
	{
	}

	void Dx12RenderBackend::SetDuelZonePassMarker(
	    const std::optional<Physics::Vector3>&)
	{
	}

	void Dx12RenderBackend::SetBatOverlayState(
	    const std::optional<Common::BatOverlayState>&)
	{
	}
} // namespace BaseballSimulatorVR::Render

#endif
