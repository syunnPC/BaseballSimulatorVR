#pragma once

#include "../Common/BatOverlayState.hpp"
#include "../Common/FieldPresentationState.hpp"
#include "../Common/GraphicsBinding.hpp"
#include "../Common/OverlayVisibilitySettings.hpp"
#include "../Common/RenderQualitySettings.hpp"
#include "../Common/ViewProjectionState.hpp"
#include "../Framework.hpp"
#include "../Physics/BallState.hpp"

#include <optional>
#include <string_view>

namespace BaseballSimulatorVR::Render
{
	class IRenderBackend
	{
	  public:
		virtual ~IRenderBackend() = default;

		[[nodiscard]] virtual bool Initialize(HWND windowHandle) = 0;
		virtual void BeginFrame() = 0;
		virtual void RenderSimulation(const Physics::BallState& ballState) = 0;
		virtual void EndFrame() = 0;
		virtual void Shutdown() = 0;
		[[nodiscard]] virtual std::wstring_view GetBackendName() const noexcept = 0;
		[[nodiscard]] virtual Common::GraphicsBinding GetGraphicsBinding() const noexcept = 0;
		virtual void SetFieldPresentationState(const Common::FieldPresentationState& state) = 0;
		virtual void SetDesktopMirrorView(const Common::ViewProjectionState& mirrorViewState) = 0;
		virtual void SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility) = 0;
		virtual void SetRenderQuality(const Common::RenderQualitySettings& renderQuality) = 0;
		virtual void SetDuelZonePassMarker(
		    const std::optional<Physics::Vector3>& zonePassMarker) = 0;
		virtual void SetBatOverlayState(
		    const std::optional<Common::BatOverlayState>& batOverlayState) = 0;
	};
} // namespace BaseballSimulatorVR::Render
