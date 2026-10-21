// Projection swapchains, MSAA targets and the D3D12 pipelines drawing into them.
#include "OpenXrSystemDetail.hpp"

#if defined(_WIN64)
#pragma comment(lib, "d3dcompiler.lib")
#endif

#if defined(_WIN64)
namespace BaseballSimulatorVR::Xr
{
	using namespace Detail;

	namespace
	{
		constexpr std::array<DXGI_FORMAT, 4> PreferredSwapchainFormats{
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM
		};
		constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
		constexpr UINT RequestedRenderSampleCount = 4U;

#if defined(BASEBALLSIMVR_DISABLE_WORLD_DRAW)
		constexpr bool EnableDebugDraw = false;
#else
		constexpr bool EnableDebugDraw = true;
#endif

		void LogWin32CleanupFailure(
		    const std::wstring_view operationName,
		    const DWORD errorCode)
		{
			const std::wstring message =
			    L"[OpenXR] " + std::wstring{ operationName } + L" failed(win32=" +
			    std::to_wstring(static_cast<unsigned long long>(errorCode)) +
			    L")\n";
			OutputDebugStringW(message.c_str());
		}

		[[nodiscard]] std::wstring BuildDebugPipelineInitializationErrorMessage(
		    const Common::DebugOverlay::PipelineInitializationPhase failedPhase,
		    const HRESULT failedResult)
		{
			switch (failedPhase)
			{
				case Common::DebugOverlay::PipelineInitializationPhase::InvalidArguments:
					return L"debug pipeline initialization arguments invalid";
				case Common::DebugOverlay::PipelineInitializationPhase::SerializeRootSignature:
					return BuildD3d12ErrorMessage(L"D3D12SerializeRootSignature", failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::CreateRootSignature:
					return BuildD3d12ErrorMessage(
					    L"ID3D12Device::CreateRootSignature",
					    failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::CompileVertexShader:
				case Common::DebugOverlay::PipelineInitializationPhase::CompilePixelShader:
					return L"D3DCompile failed";
				case Common::DebugOverlay::PipelineInitializationPhase::CreatePipelineState:
					return BuildD3d12ErrorMessage(
					    L"ID3D12Device::CreateGraphicsPipelineState",
					    failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::None:
				default:
					return L"debug pipeline initialization failed";
			}
		}
	} // namespace

	std::optional<DXGI_FORMAT> OpenXrSystem::SelectSwapchainFormat() const
	{
		uint32_t formatCount = 0;
		const XrResult formatCountResult = xrEnumerateSwapchainFormats(
		    m_sessionHandle,
		    0,
		    &formatCount,
		    nullptr);
		if (XR_FAILED(formatCountResult) || (formatCount == 0))
		{
			return std::nullopt;
		}

		std::vector<int64_t> runtimeFormats(formatCount);
		const XrResult formatReadResult = xrEnumerateSwapchainFormats(
		    m_sessionHandle,
		    formatCount,
		    &formatCount,
		    runtimeFormats.data());
		if (XR_FAILED(formatReadResult) || runtimeFormats.empty())
		{
			return std::nullopt;
		}

		for (const DXGI_FORMAT preferredFormat : PreferredSwapchainFormats)
		{
			const int64_t targetFormat = static_cast<int64_t>(preferredFormat);
			if (std::find(runtimeFormats.begin(), runtimeFormats.end(), targetFormat) != runtimeFormats.end())
			{
				return preferredFormat;
			}
		}

		return static_cast<DXGI_FORMAT>(runtimeFormats.front());
	}

	bool OpenXrSystem::InitializeProjectionSwapchains()
	{
		if (m_sessionHandle == XR_NULL_HANDLE || m_d3d12Device == nullptr)
		{
			m_sessionName = L"Session(unavailable): session/device missing";
			return false;
		}

		const std::optional<DXGI_FORMAT> selectedSwapchainFormat =
		    SelectSwapchainFormat();
		if (!selectedSwapchainFormat.has_value())
		{
			m_sessionName = L"Session(unavailable): xrEnumerateSwapchainFormats failed";
			return false;
		}
		m_colorSwapchainFormat = *selectedSwapchainFormat;

		uint32_t viewConfigurationCount = 0;
		const XrResult viewCountResult = xrEnumerateViewConfigurationViews(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    0,
		    &viewConfigurationCount,
		    nullptr);
		if (XR_FAILED(viewCountResult) || (viewConfigurationCount == 0))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateViewConfigurationViews", viewCountResult);
			return false;
		}

		m_viewConfigurationViews.assign(
		    viewConfigurationCount,
		    XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
		const XrResult viewReadResult = xrEnumerateViewConfigurationViews(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    viewConfigurationCount,
		    &viewConfigurationCount,
		    m_viewConfigurationViews.data());
		if (XR_FAILED(viewReadResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateViewConfigurationViews", viewReadResult);
			return false;
		}

		uint32_t blendModeCount = 0;
		const XrResult blendModeCountResult = xrEnumerateEnvironmentBlendModes(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    0,
		    &blendModeCount,
		    nullptr);
		if (XR_FAILED(blendModeCountResult) || (blendModeCount == 0))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateEnvironmentBlendModes", blendModeCountResult);
			return false;
		}

		std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
		const XrResult blendModeReadResult = xrEnumerateEnvironmentBlendModes(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    blendModeCount,
		    &blendModeCount,
		    blendModes.data());
		if (XR_FAILED(blendModeReadResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateEnvironmentBlendModes", blendModeReadResult);
			return false;
		}

		m_environmentBlendMode = blendModes.front();
		for (const XrEnvironmentBlendMode blendMode : blendModes)
		{
			if (blendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
			{
				m_environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				break;
			}
		}

		m_views.assign(viewConfigurationCount, XrView{ XR_TYPE_VIEW });
		m_projectionLayerViews.assign(
		    viewConfigurationCount,
		    XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
		m_projectionSwapchains.clear();
		m_projectionSwapchains.reserve(viewConfigurationCount);

		m_rtvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_srvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_projectionSampleCount = 1U;
		m_renderSampleCount = 1U;
		if (!m_viewConfigurationViews.empty())
		{
			m_projectionSampleCount = (std::max)(1U,
			                                     m_viewConfigurationViews.front().recommendedSwapchainSampleCount);
			for (const XrViewConfigurationView& viewConfiguration : m_viewConfigurationViews)
			{
				const uint32_t recommendedSampleCount = (std::max)(1U,
				                                                   viewConfiguration.recommendedSwapchainSampleCount);
				m_projectionSampleCount =
				    (std::min)(m_projectionSampleCount, recommendedSampleCount);
			}
		}
		// Runtimes almost always recommend single-sample swapchains, which left
		// only FXAA. Render 4x MSAA into app-owned targets and resolve instead.
		m_renderSampleCount = m_projectionSampleCount;
		if (m_projectionSampleCount == 1U)
		{
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS colorLevels{};
			colorLevels.Format = ResolveRtvFormat(m_colorSwapchainFormat);
			colorLevels.SampleCount = RequestedRenderSampleCount;
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depthLevels{};
			depthLevels.Format = DepthBufferFormat;
			depthLevels.SampleCount = RequestedRenderSampleCount;
			if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			        &colorLevels, sizeof(colorLevels))) && (colorLevels.NumQualityLevels > 0U) &&
			    SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			        &depthLevels, sizeof(depthLevels))) && (depthLevels.NumQualityLevels > 0U))
			{
				m_renderSampleCount = RequestedRenderSampleCount;
			}
		}

		for (uint32_t viewIndex = 0; viewIndex < viewConfigurationCount; ++viewIndex)
		{
			const XrViewConfigurationView& viewConfiguration = m_viewConfigurationViews[viewIndex];
			XrSwapchainCreateInfo swapchainCreateInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swapchainCreateInfo.createFlags = 0;
			swapchainCreateInfo.usageFlags =
			    XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			swapchainCreateInfo.format = static_cast<int64_t>(m_colorSwapchainFormat);
			swapchainCreateInfo.sampleCount = m_projectionSampleCount;
			if (viewConfiguration.recommendedImageRectWidth==0 || viewConfiguration.recommendedImageRectHeight==0 ||
			    viewConfiguration.recommendedImageRectWidth>static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
			    viewConfiguration.recommendedImageRectHeight>static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()))
			{
			    m_sessionName=L"Session(unavailable): OpenXR recommended eye resolution is invalid";
			    DestroyProjectionSwapchains();
			    return false;
			}
			swapchainCreateInfo.width=viewConfiguration.recommendedImageRectWidth;
			swapchainCreateInfo.height=viewConfiguration.recommendedImageRectHeight;
			std::wostringstream resolutionLog;
			resolutionLog << L"[OpenXR] eye " << viewIndex << L": " << swapchainCreateInfo.width << L"x"
			    << swapchainCreateInfo.height << L", MSAA " << m_projectionSampleCount
			    << L", FXAA blend " << m_renderQuality.fxaaBlendFactor << L", UI coverage AA with adaptive font density\n";
			OutputDebugStringW(resolutionLog.str().c_str());
			swapchainCreateInfo.faceCount = 1;
			swapchainCreateInfo.arraySize = 1;
			swapchainCreateInfo.mipCount = 1;

			SwapchainContext swapchainContext{};
			swapchainContext.width = static_cast<int32_t>(swapchainCreateInfo.width);
			swapchainContext.height = static_cast<int32_t>(swapchainCreateInfo.height);
			const auto cleanupCurrentSwapchainAndFail =
			    [&](const std::wstring& failureMessage) -> bool
			{
				m_sessionName = failureMessage;
				if (swapchainContext.swapchainHandle != XR_NULL_HANDLE)
				{
					LogXrCleanupFailure(
					    L"xrDestroySwapchain",
					    xrDestroySwapchain(swapchainContext.swapchainHandle));
					swapchainContext.swapchainHandle = XR_NULL_HANDLE;
				}
				DestroyProjectionSwapchains();
				return false;
			};

			const XrResult swapchainCreateResult = xrCreateSwapchain(
			    m_sessionHandle,
			    &swapchainCreateInfo,
			    &swapchainContext.swapchainHandle);
			if (XR_FAILED(swapchainCreateResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrCreateSwapchain",
				        swapchainCreateResult));
			}

			uint32_t swapchainImageCount = 0;
			const XrResult imageCountResult = xrEnumerateSwapchainImages(
			    swapchainContext.swapchainHandle,
			    0,
			    &swapchainImageCount,
			    nullptr);
			if (XR_FAILED(imageCountResult) || (swapchainImageCount == 0))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrEnumerateSwapchainImages",
				        imageCountResult));
			}

			swapchainContext.images.assign(
			    swapchainImageCount,
			    XrSwapchainImageD3D12KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
			const XrResult imageReadResult = xrEnumerateSwapchainImages(
			    swapchainContext.swapchainHandle,
			    swapchainImageCount,
			    &swapchainImageCount,
			    reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainContext.images.data()));
			if (XR_FAILED(imageReadResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrEnumerateSwapchainImages",
				        imageReadResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
			rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDescription.NumDescriptors = swapchainImageCount;
			rtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			const HRESULT descriptorHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &rtvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.rtvHeap));
			if (FAILED(descriptorHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        descriptorHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
			dsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDescription.NumDescriptors = swapchainImageCount;
			dsvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			const HRESULT dsvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &dsvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.dsvHeap));
			if (FAILED(dsvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        dsvHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC sceneRtvHeapDescription{};
			sceneRtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			sceneRtvHeapDescription.NumDescriptors = swapchainImageCount;
			sceneRtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			const HRESULT sceneRtvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &sceneRtvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.sceneRtvHeap));
			if (FAILED(sceneRtvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        sceneRtvHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC sceneSrvHeapDescription{};
			sceneSrvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			sceneSrvHeapDescription.NumDescriptors = swapchainImageCount;
			sceneSrvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			const HRESULT sceneSrvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &sceneSrvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.sceneSrvHeap));
			if (FAILED(sceneSrvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        sceneSrvHeapResult));
			}

			if (m_dsvDescriptorSize == 0)
			{
				m_dsvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
			    swapchainContext.rtvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
			    swapchainContext.dsvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle =
			    swapchainContext.sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvHandle =
			    swapchainContext.sceneSrvHeap->GetCPUDescriptorHandleForHeapStart();
			swapchainContext.depthBuffers.resize(swapchainImageCount);
			swapchainContext.sceneColorTargets.resize(swapchainImageCount);
			for (uint32_t imageIndex = 0; imageIndex < swapchainImageCount; ++imageIndex)
			{
				ID3D12Resource* renderTargetTexture = swapchainContext.images[imageIndex].texture;
				if (renderTargetTexture == nullptr)
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): swapchain texture missing");
				}

				D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
				renderTargetViewDesc.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				renderTargetViewDesc.ViewDimension =
				    (m_projectionSampleCount > 1U)
				        ? D3D12_RTV_DIMENSION_TEXTURE2DMS
				        : D3D12_RTV_DIMENSION_TEXTURE2D;
				if (renderTargetViewDesc.ViewDimension ==
				    D3D12_RTV_DIMENSION_TEXTURE2D)
				{
					renderTargetViewDesc.Texture2D.MipSlice = 0;
					renderTargetViewDesc.Texture2D.PlaneSlice = 0;
				}
				m_d3d12Device->CreateRenderTargetView(renderTargetTexture, &renderTargetViewDesc, rtvHandle);
				rtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);

				D3D12_HEAP_PROPERTIES sceneHeapProperties{};
				sceneHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				sceneHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				sceneHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				sceneHeapProperties.CreationNodeMask = 1;
				sceneHeapProperties.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC sceneColorDescription{};
				sceneColorDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				sceneColorDescription.Alignment = 0;
				sceneColorDescription.Width = static_cast<UINT64>(swapchainContext.width);
				sceneColorDescription.Height = static_cast<UINT>(swapchainContext.height);
				sceneColorDescription.DepthOrArraySize = 1;
				sceneColorDescription.MipLevels = 1;
				sceneColorDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneColorDescription.SampleDesc.Count = 1;
				sceneColorDescription.SampleDesc.Quality = 0;
				sceneColorDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				sceneColorDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				D3D12_CLEAR_VALUE sceneColorClearValue{};
				sceneColorClearValue.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneColorClearValue.Color[0] = LeftEyeClearColor[0];
				sceneColorClearValue.Color[1] = LeftEyeClearColor[1];
				sceneColorClearValue.Color[2] = LeftEyeClearColor[2];
				sceneColorClearValue.Color[3] = LeftEyeClearColor[3];

				const HRESULT sceneResourceResult = m_d3d12Device->CreateCommittedResource(
				    &sceneHeapProperties,
				    D3D12_HEAP_FLAG_NONE,
				    &sceneColorDescription,
				    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				    &sceneColorClearValue,
				    IID_PPV_ARGS(&swapchainContext.sceneColorTargets[imageIndex]));
				if (FAILED(sceneResourceResult))
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): " +
					    BuildD3d12ErrorMessage(
					        L"ID3D12Device::CreateCommittedResource",
					        sceneResourceResult));
				}

				D3D12_RENDER_TARGET_VIEW_DESC sceneRtvDescription{};
				sceneRtvDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneRtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				sceneRtvDescription.Texture2D.MipSlice = 0;
				sceneRtvDescription.Texture2D.PlaneSlice = 0;
				m_d3d12Device->CreateRenderTargetView(
				    swapchainContext.sceneColorTargets[imageIndex].Get(),
				    &sceneRtvDescription,
				    sceneRtvHandle);
				sceneRtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);

				D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrvDescription{};
				sceneSrvDescription.Shader4ComponentMapping =
				    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				sceneSrvDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				sceneSrvDescription.Texture2D.MostDetailedMip = 0;
				sceneSrvDescription.Texture2D.MipLevels = 1;
				sceneSrvDescription.Texture2D.PlaneSlice = 0;
				sceneSrvDescription.Texture2D.ResourceMinLODClamp = 0.0F;
				m_d3d12Device->CreateShaderResourceView(
				    swapchainContext.sceneColorTargets[imageIndex].Get(),
				    &sceneSrvDescription,
				    sceneSrvHandle);
				sceneSrvHandle.ptr += static_cast<SIZE_T>(m_srvDescriptorSize);

				D3D12_HEAP_PROPERTIES depthHeapProperties{};
				depthHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				depthHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				depthHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				depthHeapProperties.CreationNodeMask = 1;
				depthHeapProperties.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC depthDescription{};
				depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				depthDescription.Alignment = 0;
				depthDescription.Width = static_cast<UINT64>(swapchainContext.width);
				depthDescription.Height = static_cast<UINT>(swapchainContext.height);
				depthDescription.DepthOrArraySize = 1;
				depthDescription.MipLevels = 1;
				depthDescription.Format = DepthBufferFormat;
				depthDescription.SampleDesc.Count = m_projectionSampleCount;
				depthDescription.SampleDesc.Quality = 0;
				depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

				D3D12_CLEAR_VALUE depthClearValue{};
				depthClearValue.Format = DepthBufferFormat;
				depthClearValue.DepthStencil.Depth = 1.0F;
				depthClearValue.DepthStencil.Stencil = 0;

				const HRESULT depthResourceResult = m_d3d12Device->CreateCommittedResource(
				    &depthHeapProperties,
				    D3D12_HEAP_FLAG_NONE,
				    &depthDescription,
				    D3D12_RESOURCE_STATE_DEPTH_WRITE,
				    &depthClearValue,
				    IID_PPV_ARGS(&swapchainContext.depthBuffers[imageIndex]));
				if (FAILED(depthResourceResult))
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): " +
					    BuildD3d12ErrorMessage(
					        L"ID3D12Device::CreateCommittedResource",
					        depthResourceResult));
				}

				D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription{};
				depthStencilViewDescription.Format = DepthBufferFormat;
				depthStencilViewDescription.ViewDimension =
				    (m_projectionSampleCount > 1U)
				        ? D3D12_DSV_DIMENSION_TEXTURE2DMS
				        : D3D12_DSV_DIMENSION_TEXTURE2D;
				depthStencilViewDescription.Flags = D3D12_DSV_FLAG_NONE;
				if (depthStencilViewDescription.ViewDimension ==
				    D3D12_DSV_DIMENSION_TEXTURE2D)
				{
					depthStencilViewDescription.Texture2D.MipSlice = 0;
				}
				m_d3d12Device->CreateDepthStencilView(
				    swapchainContext.depthBuffers[imageIndex].Get(),
				    &depthStencilViewDescription,
				    dsvHandle);
				dsvHandle.ptr += static_cast<SIZE_T>(m_dsvDescriptorSize);
			}

			if (m_renderSampleCount > m_projectionSampleCount)
			{
				D3D12_HEAP_PROPERTIES msaaHeapProperties{};
				msaaHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				D3D12_RESOURCE_DESC msaaDescription{};
				msaaDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				msaaDescription.Width = static_cast<UINT64>(swapchainContext.width);
				msaaDescription.Height = static_cast<UINT>(swapchainContext.height);
				msaaDescription.DepthOrArraySize = 1;
				msaaDescription.MipLevels = 1;
				msaaDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				msaaDescription.SampleDesc.Count = m_renderSampleCount;
				msaaDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				D3D12_CLEAR_VALUE msaaColorClear{};
				msaaColorClear.Format = msaaDescription.Format;
				std::copy(LeftEyeClearColor.begin(), LeftEyeClearColor.end(), msaaColorClear.Color);
				D3D12_DESCRIPTOR_HEAP_DESC msaaRtvHeapDescription{};
				msaaRtvHeapDescription.NumDescriptors = 1;
				msaaRtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				D3D12_DESCRIPTOR_HEAP_DESC msaaDsvHeapDescription{};
				msaaDsvHeapDescription.NumDescriptors = 1;
				msaaDsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
				D3D12_RESOURCE_DESC msaaDepthDescription = msaaDescription;
				msaaDepthDescription.Format = DepthBufferFormat;
				msaaDepthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
				D3D12_CLEAR_VALUE msaaDepthClear{};
				msaaDepthClear.Format = DepthBufferFormat;
				msaaDepthClear.DepthStencil.Depth = 1.0F;
				const bool created =
				    SUCCEEDED(m_d3d12Device->CreateCommittedResource(&msaaHeapProperties, D3D12_HEAP_FLAG_NONE,
				        &msaaDescription, D3D12_RESOURCE_STATE_RENDER_TARGET, &msaaColorClear,
				        IID_PPV_ARGS(&swapchainContext.msaaColorTarget))) &&
				    SUCCEEDED(m_d3d12Device->CreateCommittedResource(&msaaHeapProperties, D3D12_HEAP_FLAG_NONE,
				        &msaaDepthDescription, D3D12_RESOURCE_STATE_DEPTH_WRITE, &msaaDepthClear,
				        IID_PPV_ARGS(&swapchainContext.msaaDepthTarget))) &&
				    SUCCEEDED(m_d3d12Device->CreateDescriptorHeap(&msaaRtvHeapDescription,
				        IID_PPV_ARGS(&swapchainContext.msaaRtvHeap))) &&
				    SUCCEEDED(m_d3d12Device->CreateDescriptorHeap(&msaaDsvHeapDescription,
				        IID_PPV_ARGS(&swapchainContext.msaaDsvHeap)));
				if (!created)
				{
					return cleanupCurrentSwapchainAndFail(L"Session(unavailable): MSAA render target creation failed");
				}
				D3D12_RENDER_TARGET_VIEW_DESC msaaRtvDescription{};
				msaaRtvDescription.Format = msaaDescription.Format;
				msaaRtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				m_d3d12Device->CreateRenderTargetView(swapchainContext.msaaColorTarget.Get(), &msaaRtvDescription,
				    swapchainContext.msaaRtvHeap->GetCPUDescriptorHandleForHeapStart());
				D3D12_DEPTH_STENCIL_VIEW_DESC msaaDsvDescription{};
				msaaDsvDescription.Format = DepthBufferFormat;
				msaaDsvDescription.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				m_d3d12Device->CreateDepthStencilView(swapchainContext.msaaDepthTarget.Get(), &msaaDsvDescription,
				    swapchainContext.msaaDsvHeap->GetCPUDescriptorHandleForHeapStart());
			}

			m_projectionSwapchains.push_back(std::move(swapchainContext));
		}

		if (EnableDebugDraw && !InitializeDebugPipeline())
		{
			DestroyProjectionSwapchains();
			return false;
		}
		for (auto& swapchain : m_projectionSwapchains)
		{
			for (auto& frame : swapchain.frames)
			{
				const HRESULT result = frame.Initialize(m_d3d12Device.Get(),
				    ResolveRtvFormat(m_colorSwapchainFormat), DepthBufferFormat,
				    m_renderSampleCount, MaxDebugVertices);
				if (FAILED(result))
				{
					m_sessionName = L"Session(unavailable): " + BuildD3d12ErrorMessage(L"XR per-view resources", result);
					DestroyProjectionSwapchains();
					return false;
				}
			}
		}

		m_isFxaaAvailable = InitializeFxaaPipeline();
		if (!m_isFxaaAvailable)
		{
			OutputDebugStringW(
			    L"[OpenXR] FXAA pipeline init failed; falling back to direct/copy post-process.\n");
		}
		m_xrPostProcessStatus = m_isFxaaAvailable ? L"fxaa" : L"direct";

		const HRESULT fenceCreateResult = m_d3d12Device->CreateFence(
		    0,
		    D3D12_FENCE_FLAG_NONE,
		    IID_PPV_ARGS(&m_xrFence));
		if (FAILED(fenceCreateResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12Device::CreateFence", fenceCreateResult);
			DestroyProjectionSwapchains();
			return false;
		}

		m_xrFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_xrFenceEvent == nullptr)
		{
			m_sessionName = L"Session(unavailable): CreateEventW failed";
			DestroyProjectionSwapchains();
			return false;
		}

		m_xrFenceValue = 1;
		return true;
	}

	bool OpenXrSystem::InitializeDebugPipeline()
	{
		if (m_d3d12Device == nullptr)
		{
			m_lastProjectionLayerStatus = L"pipeline-init-device-missing";
			m_sessionName = L"Session(unavailable): D3D12 device unavailable";
			return false;
		}

		Common::DebugOverlay::PipelineInitializationPhase failedPhase =
		    Common::DebugOverlay::PipelineInitializationPhase::None;
		HRESULT failedResult = S_OK;
		if (!Common::DebugOverlay::InitializePipeline(
		        m_d3d12Device.Get(),
		        ResolveRtvFormat(m_colorSwapchainFormat),
		        DepthBufferFormat,
		        &m_debugRootSignature,
		        &m_debugPipelineState,
		        &failedPhase,
		        &failedResult,
		        Common::DebugOverlay::PipelineBlendMode::Opaque,
		        Common::DebugOverlay::PipelineDepthWriteMode::Enabled,
		        false,
		        (std::max)(1U, m_renderSampleCount)))
		{
			m_lastProjectionLayerStatus = L"pipeline-init-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildDebugPipelineInitializationErrorMessage(
			                    failedPhase,
			                    failedResult);
			return false;
		}
		if ((m_debugRootSignature.Get() == nullptr) ||
		    (m_debugPipelineState.Get() == nullptr))
		{
			m_lastProjectionLayerStatus = L"pipeline-init-empty";
			m_sessionName = L"Session(unavailable): XR debug pipeline resources missing";
			return false;
		}

		return true;
	}

	bool OpenXrSystem::InitializeFxaaPipeline()
	{
		if (m_d3d12Device == nullptr)
		{
			return false;
		}

		Common::FxaaPipelineObjects fxaaPipelineObjects{};
		if (!Common::BuildFxaaPipeline(
		        m_d3d12Device.Get(),
		        ResolveRtvFormat(m_colorSwapchainFormat),
		        (std::max)(1U, m_projectionSampleCount),
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

	void OpenXrSystem::DestroyProjectionSwapchains()
	{
		// The runtime consumes the same queue. Keep all view uploads, command
		// allocators, textures and pipelines alive until queued work completes.
		if (m_xrFence && m_d3d12Queue && m_xrFenceEvent)
		{
			const UINT64 value = m_xrFenceValue++;
			const bool completed = SUCCEEDED(m_d3d12Queue->Signal(m_xrFence.Get(), value)) &&
			    SUCCEEDED(Common::D3d12RenderFrame::WaitForFence(m_xrFence.Get(), value, m_xrFenceEvent));
			Common::D3d12RenderFrame::EnsureSafeResourceRelease(m_d3d12Device.Get(), completed);
		}
		m_projectionRenderingFailed = false;
		if (m_xrFenceEvent != nullptr)
		{
			if (CloseHandle(m_xrFenceEvent) == FALSE)
			{
				LogWin32CleanupFailure(
				    L"CloseHandle(xrFenceEvent)",
				    GetLastError());
			}
			m_xrFenceEvent = nullptr;
		}

		m_xrFence.Reset();
		m_debugPipelineState.Reset();
		m_debugRootSignature.Reset();
		m_fxaaPipelineState.Reset();
		m_fxaaRootSignature.Reset();
		m_xrFenceValue = 1;
		m_rtvDescriptorSize = 0;
		m_dsvDescriptorSize = 0;
		m_srvDescriptorSize = 0;
		m_projectionSampleCount = 1U;
		m_renderSampleCount = 1U;
		m_isFxaaAvailable = false;
		m_xrPostProcessStatus = L"none";

		for (auto& swapchainContext : m_projectionSwapchains)
		{
			swapchainContext.images.clear();
			swapchainContext.sceneColorTargets.clear();
			swapchainContext.depthBuffers.clear();
			swapchainContext.rtvHeap.Reset();
			swapchainContext.sceneRtvHeap.Reset();
			swapchainContext.sceneSrvHeap.Reset();
			swapchainContext.dsvHeap.Reset();
			if (swapchainContext.swapchainHandle != XR_NULL_HANDLE)
			{
				LogXrCleanupFailure(
				    L"xrDestroySwapchain",
				    xrDestroySwapchain(swapchainContext.swapchainHandle));
				swapchainContext.swapchainHandle = XR_NULL_HANDLE;
			}
		}
		m_projectionSwapchains.clear();
		m_viewConfigurationViews.clear();
		m_views.clear();
		m_projectionLayerViews.clear();
	}
} // namespace BaseballSimulatorVR::Xr
#endif
