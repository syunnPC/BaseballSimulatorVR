#pragma once

#include "IRenderBackend.hpp"

#include <chrono>
#include <array>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN64)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace BaseballSimulatorVR::Render
{
	class Dx12RenderBackend final : public IRenderBackend
	{
	  public:
		Dx12RenderBackend() = default;
		Dx12RenderBackend(const Dx12RenderBackend&) = delete;
		Dx12RenderBackend& operator=(const Dx12RenderBackend&) = delete;
		Dx12RenderBackend(Dx12RenderBackend&&) = delete;
		Dx12RenderBackend& operator=(Dx12RenderBackend&&) = delete;
		~Dx12RenderBackend() override;

		[[nodiscard]] bool Initialize(HWND windowHandle) override;
		void BeginFrame() override;
		void RenderSimulation(const Physics::BallState& ballState) override;
		void EndFrame() override;
		void Shutdown() override;
		[[nodiscard]] std::wstring_view GetBackendName() const noexcept override;
		[[nodiscard]] Common::GraphicsBinding GetGraphicsBinding() const noexcept override;
		void SetDesktopMirrorView(const Common::ViewProjectionState& mirrorViewState) override;
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility) override;
		void SetRenderQuality(const Common::RenderQualitySettings& renderQuality) override;
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>& zonePassMarker) override;
		void SetBatOverlayState(
		    const std::optional<Common::BatOverlayState>& batOverlayState) override;

	  private:
		void WaitForGpu();
		void MoveToNextFrame();
		void AppendTrajectoryPoint(const Physics::BallState& ballState);
		void DrawDebugMarkers(ID3D12GraphicsCommandList* commandList);
		void RepairDebugDrawResources();
		[[nodiscard]] std::wstring BuildDebugResourceMissingReason() const;
		void SetDebugDrawPathStatus(std::wstring_view status);
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtvHandle() const noexcept;
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDsvHandle() const noexcept;
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentSceneRtvHandle() const noexcept;
		[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentSceneSrvGpuHandle() const noexcept;
		[[nodiscard]] bool HasDebugDrawResources() const noexcept;

		static constexpr UINT FrameCount = 2;

		bool m_isInitialized{ false };
		HWND m_windowHandle{ nullptr };
		std::array<float, 4> m_clearColor{ 0.05F, 0.06F, 0.09F, 1.0F };
		Physics::BallState m_latestBallState{};
		Common::ViewProjectionState m_mirrorViewState{};
		double m_lastBallFlightTimeSeconds{ 0.0 };
		std::vector<Physics::Vector3> m_trajectoryHistory{};
		std::chrono::steady_clock::time_point m_lastTrajectoryAdvanceTime{};
		float m_trajectoryFadeScale{ 1.0F };
		std::wstring m_debugDrawPathStatus{ L"init" };
		std::wstring m_postProcessPathStatus{ L"copy" };
		std::wstring m_backendNameCache{ L"DX12(window,dp:init)" };
		Common::OverlayVisibilitySettings m_overlayVisibility{};
		Common::RenderQualitySettings m_renderQuality{};
		std::optional<Physics::Vector3> m_duelZonePassMarker{};
		std::optional<Common::BatOverlayState> m_batOverlayState{};

#if defined(_WIN64)
		[[nodiscard]] bool InitializeDebugPipeline();
		[[nodiscard]] bool InitializeDebugVertexBuffer();
		[[nodiscard]] bool InitializeFxaaResources(DXGI_FORMAT swapChainFormat);
		[[nodiscard]] bool InitializeFxaaPipeline();

		Microsoft::WRL::ComPtr<IDXGIFactory7> m_factory{};
		Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter{};
		Microsoft::WRL::ComPtr<ID3D12Device> m_device{};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue{};
		Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain{};
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap{};
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_msaaRtvHeap{};
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap{};
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneRtvHeap{};
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneSrvHeap{};
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_renderTargets{};
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_msaaRenderTargets{};
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_sceneColorTargets{};
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_depthBuffers{};
		std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> m_commandAllocators{};
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList{};
		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence{};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_debugRootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_debugPipelineState{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_debugAlphaPipelineState{};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_fxaaRootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fxaaPipelineState{};
		Microsoft::WRL::ComPtr<ID3D12Resource> m_debugVertexBuffer{};

		UINT m_rtvDescriptorSize{ 0 };
		UINT m_dsvDescriptorSize{ 0 };
		UINT m_srvDescriptorSize{ 0 };
		UINT m_msaaSampleCount{ 1 };
		UINT m_frameIndex{ 0 };
		std::array<UINT64, FrameCount> m_fenceValues{ 1, 1 };
		D3D12_VERTEX_BUFFER_VIEW m_debugVertexBufferView{};
		UINT m_debugVertexCapacity{ 0 };
		HANDLE m_fenceEvent{ nullptr };
		bool m_isFxaaAvailable{ false };
		UINT m_consecutiveDebugFallbackFrames{ 0U };
#endif
	};
} // namespace BaseballSimulatorVR::Render
