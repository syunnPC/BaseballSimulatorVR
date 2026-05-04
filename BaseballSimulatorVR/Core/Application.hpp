#pragma once

#include "../Data/PitchPresetJsonStorage.hpp"
#include "../Data/PresetRepository.hpp"
#include "../Data/RuntimeSettingsJsonStorage.hpp"
#include "../Data/SessionLogRepository.hpp"
#include "../Framework.hpp"
#include "../Common/BatOverlayState.hpp"
#include "../Common/OverlayVisibilitySettings.hpp"
#include "../Common/RenderQualitySettings.hpp"
#include "../Render/IRenderBackend.hpp"
#include "../Simulation/PitchSimulationService.hpp"
#include "../UI/DesktopHudViewModel.hpp"
#include "../XR/IXrSystem.hpp"
#include "FrameStatistics.hpp"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace BaseballSimulatorVR::Core
{
	class Application final
	{
	  public:
		Application(std::unique_ptr<Xr::IXrSystem> xrSystem, std::unique_ptr<Render::IRenderBackend> renderBackend, Data::PresetRepository presetRepository);

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;
		Application(Application&&) = delete;
		Application& operator=(Application&&) = delete;

		~Application() = default;

		[[nodiscard]] int Run(HINSTANCE instanceHandle, int commandShow);

	  private:
		enum class OperationMode
		{
			PitchTrajectory,
			BattingDuel,
			TeeBatting
		};

		struct EnvironmentSettings final
		{
			double pitchDistanceMeters{ 18.44 };
			bool enableMoundHeightOffset{ false };
			double moundHeightOffsetMeters{ 0.18 };
			double strikeZoneBottomMeters{ 0.55 };
			double strikeZoneHeightMeters{ 1.00 };
			double strikeZoneWidthMeters{ 0.44 };
			Physics::Vector3 windVelocity{};
			bool enableHeightDependentWind{ false };
			double windShearFactor{ 0.08 };
			double batRadiusMeters{ 0.035 };
			double batLengthMeters{ 0.84 };
			double batBallRestitution{ 0.50 };
			double batAngleOffsetDegrees{ 0.0 };
			double batYawOffsetDegrees{ 0.0 };
			double groundFanRadiusMeters{ 100.0 };
			Physics::PitchIntegratorType integratorType{
				Physics::PitchIntegratorType::Rk4
			};
		};

		[[nodiscard]] bool InitializeWindow(HINSTANCE instanceHandle, int commandShow);
		void ApplyPreset(Data::PitchPresetType presetType);
		void ApplyPitchParameterChanges(
		    bool preserveDirectionalVelocityForCurrentPitch = false);
		void ApplyPitchParameterChangesAndLog(
		    std::wstring_view eventType,
		    std::wstring_view note,
		    bool preserveDirectionalVelocityForCurrentPitch = false);
		void HandleKeyDown(WPARAM keyCode);
		void HandleXrUiCommands();
		void HandleXrUiCommand(const Xr::UiCommand& uiCommand);
		void ToggleIntegrator();
		void ToggleHeightDependentWind();
		void ToggleSeamPerturbation();
		void AdjustForwardSpeed(double deltaMetersPerSecond);
		void AdjustHorizontalSpeed(double deltaMetersPerSecond);
		void AdjustSpin(double deltaRpm);
		void AdjustWindForward(double deltaMetersPerSecond);
		void AdjustWindCross(double deltaMetersPerSecond);
		void AdjustWindShearFactor(double delta);
		void AdjustSeamAmplitude(double delta);
		void AdjustSeamFrequency(double deltaHertz);
		void AdjustPlaybackSpeedPercent(double deltaPercent);
		void ResetPlaybackSpeedPercent();
		void TogglePlaybackPause(std::wstring_view note = L"");
		void SaveUserPresetToJson();
		[[nodiscard]] bool LoadUserPresetFromJson();
		[[nodiscard]] bool LoadUserPresetFromJsonFile(
		    std::wstring_view filePath,
		    std::wstring_view loadEventNote = L"json_load");
		void RefreshPitchSettingsFileList();
		void SaveEnvironmentSettingsToJson();
		[[nodiscard]] bool LoadEnvironmentSettingsFromJson();
		void SavePlaySettingsToJson();
		[[nodiscard]] bool LoadPlaySettingsFromJson();
		void TagLatestPitchEventResult(
		    std::wstring_view resultLabel,
		    double resultScore,
		    std::wstring_view note);
		void AdjustLatestPitchEventResultScore(double deltaScore);
		[[nodiscard]] bool LogPitchEvent(
		    std::wstring_view eventType,
		    std::wstring_view note);
		void ApplyOverlayVisibilityChanges(std::wstring_view note = L"");
		void ToggleTrajectoryVisibility();
		void ToggleStrikeZoneVisibility();
		void ToggleFloorMeshVisibility();
		void ApplyRenderQualityChanges(std::wstring_view note = L"");
		void AdjustFxaaQualityPreset(int delta);
		void AdjustTrailOpacityScale(double delta);
		void ApplyEnvironmentSettingsChanges(
		    std::wstring_view note = L"",
		    bool persistToJson = true);
		void ToggleMoundHeightOffset();
		[[nodiscard]] bool CalibrateStrikeZoneFromPlayerHeight(
		    std::wstring_view note = L"");
		void AdjustPitchDistance(double deltaMeters);
		void AdjustDuelPitchInterval(double deltaSeconds, std::wstring_view note = L"");
		void ToggleDuelPitchPreview(std::wstring_view note = L"");
		void ToggleBatHandedness(std::wstring_view note = L"");
		void AdjustBatAngleOffset(double deltaDegrees, std::wstring_view note = L"");
		void AdjustBatYawOffset(double deltaDegrees, std::wstring_view note = L"");
		void SetDuelTargetZoneBias(int zoneIndex);
		void ClearDuelTargetZoneBias();
		void SelectPitchProfile(int profileIndex, std::wstring_view note = L"");
		void SelectPitchProfileByOffset(int profileOffset, std::wstring_view note = L"");
		void SelectPitchSettingsFileByOffset(
		    int fileOffset,
		    std::wstring_view note = L"");
		void ToggleRandomPitchProfile(std::wstring_view note = L"");
		void ApplyAutoZoneResultTag(const Physics::Vector3& zonePassPoint, std::wstring_view note);
		void SetOperationMode(OperationMode mode, std::wstring_view note = L"");
		void StartRandomBattingDuelPitch(std::wstring_view note = L"");
		[[nodiscard]] bool IsWaitingForNextDuelPitch() const noexcept;
		[[nodiscard]] Xr::UiTelemetry BuildXrUiTelemetry() const;
		void RefreshBatPoseFromXrInput();
		void UpdateModeSpecificBallState(Physics::BallState* ballState);
		[[nodiscard]] bool TryApplyBatCollision(Physics::BallState* ballState);
		void ApplyEnvironmentToBallState(Physics::BallState* ballState) const;
		[[nodiscard]] bool HasDuelTargetZoneSelection() const noexcept;
		void RefreshPitchTrajectoryFromZoneSelection(
		    std::wstring_view note = L"");
		void RefreshTeeBallSpawnFromZoneSelection(
		    std::wstring_view note = L"");
		void AdjustTeeBallPosition(double deltaX, double deltaY, double deltaZ);
		void ResetTeeBallPosition();
		[[nodiscard]] Physics::BallState BuildTeeBallState() const;
		[[nodiscard]] bool GetTrajectoryVisibilityForCurrentMode() const noexcept;
		void SetTrajectoryVisibilityForCurrentMode(bool visible);
		void SyncOverlayTrajectoryVisibilityForCurrentMode();
		void EnsureDefaultPitchProfiles();
		[[nodiscard]] bool InitializeStoragePaths(std::wstring& errorMessage);
		[[nodiscard]] bool EnsureRequiredUserSettingsFiles(
		    std::wstring& errorMessage) const;
		[[nodiscard]] std::filesystem::path ResolveUserDataPath(
		    std::wstring_view filePath) const;
		[[nodiscard]] std::filesystem::path ResolveBundledContentPath(
		    std::wstring_view filePath) const;
		[[nodiscard]] std::filesystem::path ResolveInputFilePath(
		    std::wstring_view filePath) const;

		void UpdateWindowTitle();
		void Shutdown();

		static LRESULT CALLBACK StaticWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);
		[[nodiscard]] LRESULT WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

		struct BatPoseState final
		{
			bool isValid{ false };
			Physics::Vector3 originWorld{};
			Physics::Vector3 axisDirectionWorld{ 0.0, 0.0, 1.0 };
			Physics::Vector3 upDirectionWorld{ 0.0, 1.0, 0.0 };
			Physics::Vector3 linearVelocityWorld{};
		};

		struct BattedBallMetrics final
		{
			bool hasContact{ false };
			bool hasFirstGroundContact{ false };
			Physics::Vector3 contactPosition{};
			Physics::Vector3 launchVelocity{};
			double firstGroundDistanceMeters{ 0.0 };
			double maxHeightMeters{ 0.0 };
		};

		std::unique_ptr<Xr::IXrSystem> m_xrSystem;
		std::unique_ptr<Render::IRenderBackend> m_renderBackend;
		Data::PresetRepository m_presetRepository;
		Data::PitchPresetJsonStorage m_pitchPresetJsonStorage;
		Data::RuntimeSettingsJsonStorage m_runtimeSettingsJsonStorage;
		Data::SessionLogRepository m_sessionLogRepository;
		Simulation::PitchSimulationService m_pitchSimulationService;
		UI::DesktopHudViewModel m_desktopHudViewModel;
		FrameStatistics m_frameStatistics;
		Baseball::PitchParameters m_activePitchParameters{};
		Common::OverlayVisibilitySettings m_overlayVisibility{};
		bool m_pitchTrajectoryVisible{ true };
		bool m_battedTrajectoryVisible{ true };
		Data::PitchPresetType m_activePresetType{ Data::PitchPresetType::Fastball };
		std::vector<Data::PitchProfileEntry> m_pitchProfiles{};
		int m_activePitchProfileIndex{ 0 };
		std::vector<std::wstring> m_pitchSettingsFileNames{};
		int m_activePitchSettingsFileIndex{ -1 };
		std::wstring m_activePitchSettingsFileName{
			L"PitchSettings.user.json"
		};
		std::wstring m_activePitchSettingsFilePath{};
		bool m_randomPitchProfileEnabled{ true };
		bool m_isSessionLogAvailable{ false };
		std::int64_t m_lastLoggedPitchEventId{ 0 };
		std::wstring m_bundledContentDirectoryPath{};
		std::wstring m_userDataDirectoryPath{};
		std::wstring m_latestPitchResultLabel{ L"pending" };
		double m_latestPitchResultScore{ 0.0 };
		double m_playbackSpeedPercent{ 100.0 };
		bool m_playbackPaused{ false };
		Common::RenderQualitySettings m_renderQuality{};
		int m_fxaaQualityPresetIndex{ 2 };
		std::wstring m_fxaaQualityPresetLabel{ L"balanced" };
		OperationMode m_operationMode{ OperationMode::PitchTrajectory };
		EnvironmentSettings m_environmentSettings{};
		std::wstring m_environmentSettingsFilePath{
			L"EnvironmentSettings.user.json"
		};
		std::optional<Physics::Vector3> m_duelZonePassMarker{};
		std::optional<Physics::Vector3> m_duelPitchPredictedZonePassPoint{};
		std::uint32_t m_duelTargetZoneMask{ 0U };
		double m_duelPitchIntervalSeconds{ 5.0 };
		double m_duelPitchCooldownRemainingSeconds{ 0.0 };
		double m_duelPitchElapsedSeconds{ 0.0 };
		bool m_duelPitchPreviewEnabled{ true };
		bool m_batUseRightHand{ true };
		bool m_isWaitingForNextDuelPitch{ false };
		bool m_hasCurrentDuelPitchPassedZone{ false };
		bool m_hasDuelBallBeenBatted{ false };
		bool m_hasDuelBattedBallBounced{ false };
		double m_duelPostBounceElapsedSeconds{ 0.0 };
		BattedBallMetrics m_duelBattedMetrics{};
		bool m_hasPreviousDuelBallState{ false };
		Physics::BallState m_previousDuelBallState{};
		BatPoseState m_batPoseState{};
		BatPoseState m_previousBatPoseState{};
		std::optional<Common::BatOverlayState> m_batOverlayState{};
		Physics::Vector3 m_lastBatSampleOriginWorld{};
		bool m_hasLastBatSampleOrigin{ false };
		bool m_lastBatSampleUsedRightHand{ true };
		double m_batCollisionSuppressRemainingSeconds{ 0.0 };
		bool m_isTeeBallInFlight{ false };
		bool m_hasTeeBattedBallBounced{ false };
		double m_teePostBounceElapsedSeconds{ 0.0 };
		BattedBallMetrics m_teeBattedMetrics{};
		BattedBallMetrics m_teeLastBattedMetrics{};
		bool m_hasTeeLastBattedMetrics{ false };
		bool m_hasPreviousTeeBallState{ false };
		Physics::BallState m_previousTeeBallState{};
		Physics::Vector3 m_teeBallPosition{ 0.0, 1.0, 18.44 };
		double m_pitchMetricsSpeedMetersPerSecond{ 0.0 };
		double m_lastFrameDeltaSeconds{ 0.0 };
		bool m_forceTrajectoryResetOnNextFrame{ false };
		bool m_hasStrikeZoneAutoCalibrated{ false };
		double m_trajectoryZoneBiasLateral{ 0.0 };
		double m_trajectoryZoneBiasVertical{ 0.0 };

		HWND m_windowHandle{ nullptr };
		bool m_isRunning{ false };
	};
} // namespace BaseballSimulatorVR::Core
