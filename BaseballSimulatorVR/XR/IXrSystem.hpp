#pragma once

#include "../Common/BatOverlayState.hpp"
#include "../Common/GraphicsBinding.hpp"
#include "../Common/OverlayVisibilitySettings.hpp"
#include "../Common/RenderQualitySettings.hpp"
#include "../Common/ViewProjectionState.hpp"
#include "../Framework.hpp"
#include "../Physics/BallState.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BaseballSimulatorVR::Xr
{
	enum class UiCommandType
	{
		SetModeTrajectory,
		SetModeBattingDuel,
		SetModeTeeBatting,
		SetPresetFastball,
		SetPresetSlider,
		SetPresetCurveball,
		SetPresetChangeup,
		ToggleTrajectoryVisibility,
		ToggleStrikeZoneVisibility,
		AdjustDuelIntervalDown,
		AdjustDuelIntervalUp,
		AdjustPlaybackSpeedDown,
		AdjustPlaybackSpeedUp,
		ResetPlaybackSpeed,
		ToggleMoundHeightOffset,
		AdjustPitchDistanceDown,
		AdjustPitchDistanceUp,
		AdjustForwardSpeedDown,
		AdjustForwardSpeedUp,
		AdjustSpinDown,
		AdjustSpinUp,
		ToggleDuelPitchPreview,
		SetDuelTargetZone,
		ClearDuelTargetZoneBias,
		SelectPitchProfile,
		SelectPreviousPitchProfile,
		SelectNextPitchProfile,
		SelectPreviousPitchSettingsFile,
		SelectNextPitchSettingsFile,
		ToggleRandomPitchProfile,
		TogglePlaybackPause,
		ToggleBatHandedness,
		AdjustBatAngleDown,
		AdjustBatAngleUp,
		AdjustBatYawDown,
		AdjustBatYawUp,
		SetPlaybackSpeedPercent
	};

	enum class UiOperationMode
	{
		PitchTrajectory,
		BattingDuel,
		TeeBatting,
	};

	struct UiCommand final
	{
		UiCommandType type{ UiCommandType::SetModeTrajectory };
		int intValue{ 0 };
	};

	struct UiTelemetry final
	{
		UiOperationMode operationMode{ UiOperationMode::PitchTrajectory };
		std::wstring presetName{ L"Fastball" };
		double pitchDistanceMeters{ 18.44 };
		double duelIntervalSeconds{ 0.80 };
		double playbackSpeedPercent{ 100.0 };
		bool playbackPaused{ false };
		bool moundHeightOffsetEnabled{ false };
		double moundHeightOffsetMeters{ 0.18 };
		std::array<std::wstring, 4> pitchProfileNames{};
		int pitchProfileCount{ 0 };
		int activePitchProfileIndex{ -1 };
		std::wstring pitchSettingsFileName{ L"PitchSettings.user.json" };
		int pitchSettingsFileCount{ 0 };
		bool randomPitchProfileEnabled{ true };
		std::uint32_t duelTargetZoneMask{ 0U };
		bool duelPitchPreviewEnabled{ true };
		bool batUseRightHand{ true };
		double batAngleOffsetDegrees{ 0.0 };
		double batYawOffsetDegrees{ 0.0 };
		double pitchMetricsSpeedMetersPerSecond{ 0.0 };
		bool hasBattedMetrics{ false };
		bool battedMetricsLive{ false };
		bool battedMetricsHasGroundContact{ false };
		double battedFirstGroundDistanceMeters{ 0.0 };
		double battedMaxHeightMeters{ 0.0 };
		Physics::Vector3 battedLaunchVelocity{};
	};

	struct HandPoseState final
	{
		bool isValid{ false };
		Physics::Vector3 position{};
		Physics::Vector3 forward{};
		Physics::Vector3 up{ 0.0, 1.0, 0.0 };
	};

	class IXrSystem
	{
	  public:
		virtual ~IXrSystem() = default;

		[[nodiscard]] virtual bool Initialize(HWND windowHandle) = 0;
		[[nodiscard]] virtual bool AttachGraphicsBinding(const Common::GraphicsBinding& graphicsBinding) = 0;
		virtual void SetBallState(const Physics::BallState& ballState) = 0;
		virtual void SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility) = 0;
		virtual void SetRenderQuality(const Common::RenderQualitySettings& renderQuality) = 0;
		virtual void SetDuelZonePassMarker(
		    const std::optional<Physics::Vector3>& zonePassMarker) = 0;
		virtual void SetBatOverlayState(
		    const std::optional<Common::BatOverlayState>& batOverlayState) = 0;
		virtual void SetUiTelemetry(const UiTelemetry& telemetry) = 0;
		virtual void PollInput() = 0;
		[[nodiscard]] virtual std::vector<UiCommand> ConsumePendingUiCommands() = 0;
		virtual void Shutdown() = 0;
		[[nodiscard]] virtual std::wstring_view GetRuntimeName() const noexcept = 0;
		[[nodiscard]] virtual std::wstring_view GetSessionName() const noexcept = 0;
		[[nodiscard]] virtual Common::ViewProjectionState GetLeftEyeViewState() const noexcept = 0;
		[[nodiscard]] virtual HandPoseState GetLeftHandPose() const noexcept = 0;
		[[nodiscard]] virtual HandPoseState GetRightHandPose() const noexcept = 0;
	};
} // namespace BaseballSimulatorVR::Xr
