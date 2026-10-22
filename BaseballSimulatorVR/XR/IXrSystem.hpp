#pragma once

#include "../Common/BatOverlayState.hpp"
#include "../Common/FieldPresentationState.hpp"
#include "../Common/StrikeZoneCalibration.hpp"
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

namespace BaseballSimulatorVR::Xr {
enum class UiCommandType {
  SetModeTrajectory,
  SetModeBattingDuel,
  SetModeTeeBatting,
  SetModeCatcher,
  SetPresetFastball,
  SetPresetSlider,
  SetPresetCurveball,
  SetPresetChangeup,
  CyclePitchTrajectoryDisplayMode,
  ToggleBattedTrajectoryVisibility,
  CycleStrikeZoneDisplayMode,
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
  CycleBatMaterial,
  AdjustBatAngleDown,
  AdjustBatAngleUp,
  AdjustBatYawDown,
  AdjustBatYawUp,
  SetPlaybackSpeedPercent,
  RecalibrateStrikeZone,
  AdjustStrikeZoneBottomDown,
  AdjustStrikeZoneBottomUp,
  AdjustStrikeZoneTopDown,
  AdjustStrikeZoneTopUp
};

enum class UiOperationMode {
  PitchTrajectory,
  BattingDuel,
  TeeBatting,
  Catcher,
};

struct UiCommand final {
  UiCommandType type{UiCommandType::SetModeTrajectory};
  int intValue{0};
};

struct UiTelemetry final {
  UiOperationMode operationMode{UiOperationMode::PitchTrajectory};
  std::wstring presetName{L"Fastball"};
  double pitchDistanceMeters{18.44};
  double duelIntervalSeconds{5.0};
  double playbackSpeedPercent{100.0};
  bool playbackPaused{false};
  bool moundHeightOffsetEnabled{false};
  double moundHeightOffsetMeters{0.18};
  std::vector<std::wstring> pitchProfileNames{};
  int pitchProfileCount{0};
  int activePitchProfileIndex{-1};
  std::wstring pitchSettingsFileName{L"PitchSettings.user.json"};
  int pitchSettingsFileCount{0};
  bool randomPitchProfileEnabled{true};
  bool pitchInformationVisible{true};
  std::uint32_t duelTargetZoneMask{0U};
  bool duelPitchPreviewEnabled{true};
  bool batUseRightHand{true};
  Physics::BatMaterial batMaterial{Physics::BatMaterial::Wood};
  double batLengthMeters{0.84};
  double batRadiusMeters{0.035};
  double batAngleOffsetDegrees{0.0};
  double batYawOffsetDegrees{0.0};
  double pitchMetricsSpeedMetersPerSecond{0.0};
  bool hasBattedMetrics{false};
  bool battedMetricsLive{false};
  bool battedMetricsHasGroundContact{false};
  double battedFirstGroundDistanceMeters{0.0};
  // Scorer's call for the batted ball once it has come down; empty before.
  std::wstring battedResultLabel{};
  double battedMaxHeightMeters{0.0};
  Physics::Vector3 battedLaunchVelocity{};
  Common::StrikeZoneCalibrationProgress strikeZoneCalibration{};
  double strikeZoneBottomMeters{0.55};
  double strikeZoneTopMeters{1.55};
  double strikeZoneHeightMeters{1.0};
};

struct HandPoseState final {
  bool isValid{false};
  double sampleTimeSeconds{0.0};
  std::uint64_t referenceSpaceRevision{0};
  Physics::Vector3 position{};
  Physics::Vector3 forward{};
  Physics::Vector3 up{0.0, 1.0, 0.0};
  // Runtime-reported grip velocities in the reference space, when available.
  bool hasLinearVelocity{false};
  Physics::Vector3 linearVelocity{};
  bool hasAngularVelocity{false};
  Physics::Vector3 angularVelocity{};
  // False while the runtime only infers the pose (e.g. a fast swing out of
  // tracking-camera view); the pose is still usable.
  bool isTracked{false};
};

// Buttons of one controller, sampled with the frame's hand poses.
struct ControllerButtonState final {
  bool isActive{false};
  bool triggerPressed{false};
  // The lower and upper face buttons: A and B on the right hand, X and Y on the
  // left (other controllers map their nearest equivalents).
  bool primaryPressed{false};
  bool secondaryPressed{false};
};

struct ControllerInputState final {
  ControllerButtonState left{};
  ControllerButtonState right{};
  // True while the UI laser is out; its trigger then belongs to the panels.
  bool uiPointerActive{false};
};

struct FrameTiming final {
  bool isValid{false};
  double predictedDisplayTimeSeconds{0.0};
  double predictedDisplayPeriodSeconds{0.0};
};

class IXrSystem {
public:
  virtual ~IXrSystem() = default;

  [[nodiscard]] virtual bool Initialize(HWND windowHandle) = 0;
  [[nodiscard]] virtual bool
  AttachGraphicsBinding(const Common::GraphicsBinding &graphicsBinding) = 0;
  virtual void SetBallState(const Physics::BallState &ballState) = 0;
  virtual void SetOverlayVisibility(
      const Common::OverlayVisibilitySettings &overlayVisibility) = 0;
  virtual void
  SetRenderQuality(const Common::RenderQualitySettings &renderQuality) = 0;
  virtual void SetDuelZonePassMarker(
      const std::optional<Physics::Vector3> &zonePassMarker) = 0;
  virtual void SetBatOverlayState(
      const std::optional<Common::BatOverlayState> &batOverlayState) = 0;
  virtual void SetFieldPresentationState(const Common::FieldPresentationState& state) = 0;
  virtual void SetUiTelemetry(const UiTelemetry &telemetry) = 0;
  // Processes runtime events, waits for and begins the next frame, and samples
  // views, hand poses and UI input for that frame's predicted display time.
  virtual void PollInput() = 0;
  // Renders the state set since PollInput and ends the frame it began, so the
  // HMD shows the simulation advanced to the same display time as the poses.
  virtual void SubmitFrame() = 0;
  [[nodiscard]] virtual FrameTiming GetFrameTiming() const noexcept = 0;
  // False while the runtime shows its own UI over the running app.
  [[nodiscard]] virtual bool IsInputFocused() const noexcept = 0;
  // The user or runtime ended the XR experience; the app should exit rather
  // than restart it.
  [[nodiscard]] virtual bool IsExitRequested() const noexcept = 0;
  virtual void ApplyHapticPulse(bool rightHand, double amplitude,
                                double durationSeconds) = 0;
  [[nodiscard]] virtual std::vector<UiCommand> ConsumePendingUiCommands() = 0;
  virtual void Shutdown() = 0;
  [[nodiscard]] virtual std::wstring_view GetRuntimeName() const noexcept = 0;
  [[nodiscard]] virtual std::wstring_view GetSessionName() const noexcept = 0;
  [[nodiscard]] virtual Common::ViewProjectionState
  GetLeftEyeViewState() const noexcept = 0;
  [[nodiscard]] virtual Common::FloorEyeHeightSample GetFloorEyeHeightSample() const noexcept = 0;
  [[nodiscard]] virtual HandPoseState GetLeftHandPose() const noexcept = 0;
  [[nodiscard]] virtual HandPoseState GetRightHandPose() const noexcept = 0;
  [[nodiscard]] virtual ControllerInputState GetControllerInputState() const noexcept = 0;
};
} // namespace BaseballSimulatorVR::Xr
