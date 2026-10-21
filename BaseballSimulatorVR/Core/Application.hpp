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
#include "../Simulation/PitchDeliveryController.hpp"
#include "../Simulation/PitchFlightAnalysis.hpp"
#include "../Common/StrikeZoneCalibration.hpp"
#include "../Common/StrikeZoneSettings.hpp"
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
		friend struct ApplicationTestAccess;
		enum class OperationMode
		{
			PitchTrajectory,
			BattingDuel,
			TeeBatting
		};

		// Single definition shared with the JSON storage (see Data/RuntimeSettingsJsonStorage.hpp).
		using EnvironmentSettings = Data::EnvironmentRuntimeSettings;

		[[nodiscard]] bool InitializeWindow(HINSTANCE instanceHandle, int commandShow);
		void ApplyPreset(Data::PitchPresetType presetType);
		void ResetPitchSimulation();
		void AdvancePitchSimulation(std::chrono::duration<double> delta);
		void CommitPitchAnalysis(const Physics::Vector3& endpoint, double cutoffTime);
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
		void ExportCurrentSessionCsv();
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
		void CyclePitchTrajectoryDisplayMode();
		void ToggleBattedTrajectoryVisibility();
		void CycleStrikeZoneDisplayMode();
		void ToggleFloorMeshVisibility();
		void ApplyRenderQualityChanges(std::wstring_view note = L"");
		void AdjustFxaaQualityPreset(int delta);
		void AdjustTrailOpacityScale(double delta);
		void ApplyEnvironmentSettingsChanges(
		    std::wstring_view note = L"",
		    bool persistToJson = true);
		void ToggleMoundHeightOffset();
		void BeginStrikeZoneCalibration(
		    std::wstring_view note = L"");
		void UpdateStrikeZoneCalibration();
		void ApplyStrikeZoneSettingsChanges(bool persist);
		void AdjustStrikeZoneBoundary(bool upper, double deltaMeters);
		[[nodiscard]] Common::FieldPresentationState BuildFieldPresentationState() const;
		void AdjustPitchDistance(double deltaMeters);
		void AdjustDuelPitchInterval(double deltaSeconds, std::wstring_view note = L"");
		void ToggleDuelPitchPreview(std::wstring_view note = L"");
		void ToggleBatHandedness(std::wstring_view note = L"");
		void CycleBatMaterial(std::wstring_view note = L"");
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
		void ApplyAutoZoneResultTag(Common::PitchZoneCall call, std::wstring_view note);
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
			double sampleTimeSeconds{ 0.0 };
			std::uint64_t referenceSpaceRevision{ 0 };
			Physics::Vector3 upDirectionWorld{ 0.0, 1.0, 0.0 };
		};

		struct BattedBallMetrics final
		{
			bool hasContact{ false };
			bool hasFirstGroundContact{ false };
			Physics::Vector3 contactPosition{};
			Physics::Vector3 launchVelocity{};
			bool hasBatContactPosition{false};
			double batContactFraction{}, contactBatLengthMeters{.84};
			double contactFlightTimeSeconds{};
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
		Simulation::PitchDeliveryController m_pitchDeliveryController;
		Simulation::PitchFlightAnalysis m_pitchFlightAnalysis;
		std::vector<Physics::BallState> m_pendingPitchSamples{};
		UI::DesktopHudViewModel m_desktopHudViewModel;
		FrameStatistics m_frameStatistics;
		Baseball::PitchParameters m_activePitchParameters{};
		std::wstring m_currentPitchPresentationName{};
		Common::OverlayVisibilitySettings m_overlayVisibility{};
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
		bool m_pitcherThrowsLeftHanded{ false };
		double m_pitcherArmSpeedScale{ 1.0 };
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
		Common::StrikeZoneRuntimeSettings m_strikeZoneSettings{};
		bool m_strikeZoneConfigured{ false };
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
		Physics::BatMaterial m_batMaterial{ Physics::BatMaterial::Wood };
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
		bool m_lastBatSampleUsedRightHand{ true };
		double m_batCollisionSuppressRemainingSeconds{ 0.0 };
		bool m_isTeeBallInFlight{ false };
		bool m_hasTeeBattedBallBounced{ false };
		double m_teePostBounceElapsedSeconds{ 0.0 };
		BattedBallMetrics m_teeBattedMetrics{};
		std::uint64_t m_battedContactSequence{ 0 };
		bool m_hasPreviousTeeBallState{ false };
		Physics::BallState m_previousTeeBallState{};
		Physics::Vector3 m_teeBallPosition{ 0.0, 1.0, 18.44 };
		double m_pitchMetricsSpeedMetersPerSecond{ 0.0 };
		double m_lastFrameDeltaSeconds{ 0.0 };
		double m_lastSimulationDeltaSeconds{ 0.0 };
		bool m_forceTrajectoryResetOnNextFrame{ false };
		Common::StrikeZoneHeightCalibrator m_strikeZoneCalibrator;
		std::wstring m_strikeZoneCalibrationNote{ L"auto_calibrate_startup" };
		double m_trajectoryZoneBiasLateral{ 0.0 };
		double m_trajectoryZoneBiasVertical{ 0.0 };

		HWND m_windowHandle{ nullptr };
		bool m_isRunning{ false };
	};
} // namespace BaseballSimulatorVR::Core
