#pragma once

#include "IRenderBackend.hpp"

#include <deque>
#include <optional>
#include <vector>

namespace BaseballSimulatorVR::Render
{
	class NullRenderBackend final : public IRenderBackend
	{
	  public:
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
		[[nodiscard]] static RECT BuildSideViewRect(const RECT& clientRect);
		[[nodiscard]] static RECT BuildTopViewRect(const RECT& clientRect);
		[[nodiscard]] static POINT ProjectSideViewPoint(const Physics::Vector3& position, const RECT& sideViewRect);
		[[nodiscard]] static POINT ProjectTopViewPoint(const Physics::Vector3& position, const RECT& topViewRect);
		[[nodiscard]] bool ShouldResetTrajectorySamples(const Physics::BallState& ballState) const noexcept;

		void PushTrajectorySample(const Physics::Vector3& position);

		bool m_isInitialized{ false };
		HWND m_windowHandle{ nullptr };
		std::deque<Physics::Vector3> m_trajectorySamples{};
		double m_lastBallFlightTimeSeconds{ 0.0 };
		Common::OverlayVisibilitySettings m_overlayVisibility{};
		Common::RenderQualitySettings m_renderQuality{};
		std::optional<Physics::Vector3> m_duelZonePassMarker{};
		std::optional<Common::BatOverlayState> m_batOverlayState{};
	};
} // namespace BaseballSimulatorVR::Render
