#pragma once

#include "IXrSystem.hpp"

#include <chrono>

#if defined(_WIN64)
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D12
#endif
#include "../Common/D3d12RenderFrame.hpp"
#include "../Common/PhaseTrajectoryTrails.hpp"
#include <Unknwn.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#endif

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Xr
{
	class NullXrSystem final : public IXrSystem
	{
	  public:
		[[nodiscard]] bool Initialize(HWND windowHandle) override;
		[[nodiscard]] bool AttachGraphicsBinding(const Common::GraphicsBinding& graphicsBinding) override;
		void SetBallState(const Physics::BallState& ballState) override;
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility) override;
		void SetRenderQuality(const Common::RenderQualitySettings& renderQuality) override;
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>& zonePassMarker) override;
		void SetFieldPresentationState(const Common::FieldPresentationState& state) override;
		void SetBatOverlayState(
		    const std::optional<Common::BatOverlayState>& batOverlayState) override;
		void SetUiTelemetry(const UiTelemetry& telemetry) override;
		void PollInput() override;
		void SubmitFrame() override;
		[[nodiscard]] FrameTiming GetFrameTiming() const noexcept override;
		[[nodiscard]] bool IsInputFocused() const noexcept override;
		[[nodiscard]] bool IsExitRequested() const noexcept override;
		void ApplyHapticPulse(bool rightHand, double amplitude, double durationSeconds) override;
		[[nodiscard]] std::vector<UiCommand> ConsumePendingUiCommands() override;
		void Shutdown() override;
		[[nodiscard]] std::wstring_view GetRuntimeName() const noexcept override;
		[[nodiscard]] std::wstring_view GetSessionName() const noexcept override;
		[[nodiscard]] Common::ViewProjectionState GetLeftEyeViewState() const noexcept override;
		[[nodiscard]] Common::FloorEyeHeightSample GetFloorEyeHeightSample() const noexcept override;
		[[nodiscard]] HandPoseState GetLeftHandPose() const noexcept override;
		[[nodiscard]] HandPoseState GetRightHandPose() const noexcept override;

	  private:
		[[nodiscard]] static std::wstring QueryActiveRuntimePath();
		[[nodiscard]] static std::wstring BuildRuntimeDisplayName(const std::wstring& runtimePath);
#if defined(_WIN64)
		// Two frames of per-view command, vertex and glyph resources, so recording
		// a view does not wait for the GPU to finish that view's previous frame.
		static constexpr std::size_t FramesInFlight = 2;
		struct SwapchainContext final
		{
			std::array<Common::D3d12RenderFrame, FramesInFlight> frames;
			std::size_t nextFrame{ 0 };
			XrSwapchain swapchainHandle{ XR_NULL_HANDLE };
			int32_t width{ 0 };
			int32_t height{ 0 };
			std::vector<XrSwapchainImageD3D12KHR> images{};
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap{};
			std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> sceneColorTargets{};
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneRtvHeap{};
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneSrvHeap{};
			std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> depthBuffers{};
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap{};
		};

		void TryInitializeOpenXrInstance();
		void TryInitializeOpenXrSystem();
		[[nodiscard]] bool TryInitializeOpenXrSession(const Common::GraphicsBinding& graphicsBinding);
		[[nodiscard]] bool TryInitializeGraphicsDevice(const Common::GraphicsBinding& graphicsBinding, const XrGraphicsRequirementsD3D12KHR& graphicsRequirements);
		[[nodiscard]] bool TryCreateGraphicsDeviceFromAdapterLuid(LUID adapterLuid, D3D_FEATURE_LEVEL minimumFeatureLevel);
		[[nodiscard]] bool EnsureGraphicsQueueForCurrentDevice();
		[[nodiscard]] std::optional<DXGI_FORMAT> SelectSwapchainFormat() const;
		[[nodiscard]] bool InitializeProjectionSwapchains();
		[[nodiscard]] bool InitializeDebugPipeline();
		[[nodiscard]] bool InitializeFxaaPipeline();
		[[nodiscard]] bool InitializeInputActions();
		[[nodiscard]] bool UpdateReferenceSpace();
		void DestroyProjectionSwapchains();
		void DestroyInputActions();
		void UpdateUiPanelInteraction(XrTime predictedDisplayTime);
		void HandleSessionStateChanged(XrSessionState sessionState);
		void HandleInteractionProfileChanged();
		void TryBeginFrame();
		void EndFrame(bool hasProjectionLayer);
		[[nodiscard]] bool LocateFrameViews(XrTime predictedDisplayTime);
		[[nodiscard]] bool RenderLocatedViews();
		void DestroySession();
		void ScheduleRecovery(std::wstring_view reason);
		void TryRecover();
		void RecenterToBattingPosition();
		void AppendTrajectoryPoint(const Physics::BallState& ballState);
		[[nodiscard]] bool ClearSwapchainImage(
		    SwapchainContext& swapchainContext,
		    uint32_t imageIndex,
		    const float* clearColor,
		    const XrView& view);
		void DestroyOpenXrInstance();
		[[nodiscard]] static std::wstring BuildRuntimeDisplayNameFromInstance(XrInstance instanceHandle);
		void UpdateSessionName();
#endif

		bool m_isInitialized{ false };
		std::wstring m_runtimeName{ L"OpenXR(uninitialized)" };
		std::wstring m_sessionName{ L"Session(unavailable)" };
		Common::ViewProjectionState m_leftEyeViewState{};
		Common::FloorEyeHeightSample m_floorEyeHeightSample{};
		std::uint64_t m_floorReferenceRevision{1};
		std::uint64_t m_handReferenceRevision{1};
		Common::FieldPresentationState m_fieldPresentation{};
		Common::OverlayVisibilitySettings m_overlayVisibility{};
		Common::RenderQualitySettings m_renderQuality{};
#if defined(_WIN64)
		Common::UiText::GlyphAtlas m_uiTextAtlas;
		Common::UiText::TextLayoutCache m_uiTextLayouts;
		bool m_projectionRenderingFailed{false};
		bool m_isFrameBegun{false};
		bool m_hasLocatedViews{false};
		uint32_t m_locatedViewCount{0};
		XrFrameState m_frameState{ XR_TYPE_FRAME_STATE };
		FrameTiming m_frameTiming{};
		bool m_exitRequested{false};
		bool m_recoveryPending{false};
		std::chrono::steady_clock::time_point m_nextRecoveryAttempt{};
		Common::GraphicsBinding m_graphicsBinding{};
		bool m_hasGraphicsBinding{false};
		bool m_hasLocalFloorExtension{false};
		std::wstring m_interactionProfileName{};
		XrInstance m_instanceHandle{ XR_NULL_HANDLE };
		XrSystemId m_systemId{ XR_NULL_SYSTEM_ID };
		XrSession m_sessionHandle{ XR_NULL_HANDLE };
		XrSpace m_referenceSpaceHandle{ XR_NULL_HANDLE };
		XrReferenceSpaceType m_referenceSpaceType{ XR_REFERENCE_SPACE_TYPE_LOCAL };
		XrSessionState m_sessionState{ XR_SESSION_STATE_UNKNOWN };
		bool m_isGraphicsBound{ false };
		bool m_usesDedicatedGraphicsDevice{ false };
		bool m_isSessionRunning{ false };
		XrTime m_lastPredictedDisplayTime{ 0 };
		XrTime m_pendingReferenceChangeTime{ 0 };
		std::wstring m_lastFrameSummary{ L"frame(pending)" };
		std::wstring m_lastProjectionLayerStatus{ L"pending" };
		PFN_xrGetD3D12GraphicsRequirementsKHR m_xrGetD3d12GraphicsRequirementsKhr{ nullptr };
		XrEnvironmentBlendMode m_environmentBlendMode{ XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
		DXGI_FORMAT m_colorSwapchainFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
		UINT m_projectionSampleCount{ 1U };
		XrActionSet m_inputActionSet{ XR_NULL_HANDLE };
		XrAction m_aimPoseAction{ XR_NULL_HANDLE };
		XrAction m_gripPoseAction{ XR_NULL_HANDLE };
		XrAction m_selectClickAction{ XR_NULL_HANDLE };
		XrAction m_triggerValueAction{ XR_NULL_HANDLE };
		XrAction m_moveStickAction{ XR_NULL_HANDLE };
		XrAction m_viewStickAction{ XR_NULL_HANDLE };
		XrAction m_recenterAction{ XR_NULL_HANDLE };
		XrAction m_hapticAction{ XR_NULL_HANDLE };
		bool m_wasRecenterPressed{ false };
		XrSpace m_leftAimActionSpace{ XR_NULL_HANDLE };
		XrSpace m_rightAimActionSpace{ XR_NULL_HANDLE };
		XrSpace m_leftGripActionSpace{ XR_NULL_HANDLE };
		XrSpace m_rightGripActionSpace{ XR_NULL_HANDLE };
		XrPath m_leftHandPath{ XR_NULL_PATH };
		XrPath m_rightHandPath{ XR_NULL_PATH };
		Physics::Vector3 m_locomotionPositionOffset{};
		double m_viewYawOffsetRadians{ 0.0 };
		Physics::Vector3 m_appliedReferenceSpacePositionOffset{};
		double m_appliedReferenceSpaceYawOffsetRadians{ 0.0 };
		XrTime m_lastLocomotionSampleTime{ 0 };
		bool m_wasResultPanelSelectPressed{ false };
		bool m_hasResultPanelLaser{ false };
		bool m_showMainControlPanel{ false };
		bool m_showPitchControlPanel{ false };
		bool m_showMetricsPanel{ false };
		bool m_showBattingMetricsPanel{ false };
		bool m_pitchPanelMinimized{ false };
		bool m_metricsPanelMinimized{ false };
		bool m_battingMetricsPanelMinimized{ false };
		bool m_isPitchPlaybackSliderDragging{ false };
		enum class SubPanelDragTarget
		{
			None,
			Pitch,
			Metrics,
			BattingMetrics
		};
		SubPanelDragTarget m_subPanelDragTarget{ SubPanelDragTarget::None };
		double m_subPanelDragGrabLocalX{ 0.0 };
		double m_subPanelDragGrabLocalY{ 0.0 };
		double m_subPanelDragCenterDistanceMeters{ 0.90 };
		double m_subPanelDragGrabDistanceMeters{ 0.90 };
		double m_pitchPanelArcYawRadians{ -0.30 };
		double m_pitchPanelArcRadiusMeters{ 0.72 };
		double m_pitchPanelHeightOffsetMeters{ -0.18 };
		double m_metricsPanelArcYawRadians{ 0.16 };
		double m_metricsPanelArcRadiusMeters{ 0.68 };
		double m_metricsPanelHeightOffsetMeters{ 0.00 };
		double m_battingMetricsPanelArcYawRadians{ 0.50 };
		double m_battingMetricsPanelArcRadiusMeters{ 0.72 };
		double m_battingMetricsPanelHeightOffsetMeters{ -0.10 };
		Physics::Vector3 m_mainControlPanelCenter{
			-0.95,
			1.20,
			-1.20
		};
		Physics::Vector3 m_pitchControlPanelCenter{
			-0.95,
			0.88,
			-1.20
		};
		Physics::Vector3 m_metricsPanelCenter{
			-0.58,
			1.20,
			-1.20
		};
		Physics::Vector3 m_battingMetricsPanelCenter{
			-0.58,
			1.20,
			-1.20
		};
		Physics::Vector3 m_uiPanelForward{
			0.0,
			0.0,
			-1.0
		};
		Physics::Vector3 m_nonMainPanelForward{
			0.0,
			0.0,
			-1.0
		};
		Physics::Vector3 m_pitchPanelForward{
			0.0,
			0.0,
			1.0
		};
		Physics::Vector3 m_metricsPanelForward{
			0.0,
			0.0,
			1.0
		};
		Physics::Vector3 m_battingMetricsPanelForward{
			0.0,
			0.0,
			1.0
		};
		Physics::Vector3 m_uiPanelRight{
			1.0,
			0.0,
			0.0
		};
		Physics::Vector3 m_uiPanelUp{
			0.0,
			1.0,
			0.0
		};
		Physics::Vector3 m_resultPanelLaserOrigin{};
		Physics::Vector3 m_resultPanelLaserTarget{};
		std::wstring m_resultPanelLaserStatus{ L"none" };

		Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device{};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12Queue{};
		Microsoft::WRL::ComPtr<ID3D12Fence> m_xrFence{};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_debugRootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_debugPipelineState{};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_fxaaRootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fxaaPipelineState{};
		HANDLE m_xrFenceEvent{ nullptr };
		UINT64 m_xrFenceValue{ 1 };
		UINT m_rtvDescriptorSize{ 0 };
		UINT m_dsvDescriptorSize{ 0 };
		UINT m_srvDescriptorSize{ 0 };
		bool m_isFxaaAvailable{ false };
		std::wstring m_xrPostProcessStatus{ L"none" };

		std::vector<SwapchainContext> m_projectionSwapchains{};
		std::vector<XrViewConfigurationView> m_viewConfigurationViews{};
		std::vector<XrView> m_views{};
		std::vector<XrCompositionLayerProjectionView> m_projectionLayerViews{};
		XrCompositionLayerProjection m_projectionLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };

		Physics::BallState m_latestBallState{};
		Common::PhaseTrajectoryTrails m_trajectoryTrails{};
		std::optional<Physics::Vector3> m_duelZonePassMarker{};
		std::optional<Common::BatOverlayState> m_batOverlayState{};
		UiTelemetry m_uiTelemetry{};
		HandPoseState m_leftHandPose{};
		HandPoseState m_rightHandPose{};
		std::vector<UiCommand> m_pendingUiCommands{};
#endif
	};
} // namespace BaseballSimulatorVR::Xr
