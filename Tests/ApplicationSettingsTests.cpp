#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Core/Application.hpp"
#include "../BaseballSimulatorVR/Common/AppLog.hpp"
#include "../BaseballSimulatorVR/Common/AtomicFileWriter.hpp"
#include "../BaseballSimulatorVR/Common/CrashReporting.hpp"
#include "../BaseballSimulatorVR/Common/DebugWorldProjection.hpp"
#include "../BaseballSimulatorVR/Common/TextConversion.hpp"
#include "../BaseballSimulatorVR/Physics/BatGeometry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Settings persistence, read-only presets, startup recovery and session-log semantics of the
// application (run from the bundled settings suite).
namespace
{
	using namespace BaseballSimulatorVR;

	class SettingsTestXrSystem final : public Xr::IXrSystem
	{
	  public:
		Xr::HandPoseState rightHand{};
		bool Initialize(HWND) override { return true; }
		bool AttachGraphicsBinding(const Common::GraphicsBinding&) override { return true; }
		void SetBallState(const Physics::BallState&) override {}
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings&) override {}
		void SetRenderQuality(const Common::RenderQualitySettings&) override {}
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>&) override {}
		void SetBatOverlayState(const std::optional<Common::BatOverlayState>&) override {}
		void SetFieldPresentationState(const Common::FieldPresentationState&) override {}
		void SetUiTelemetry(const Xr::UiTelemetry&) override {}
		void PollInput() override {}
		std::vector<Xr::UiCommand> ConsumePendingUiCommands() override { return {}; }
		void Shutdown() override {}
		std::wstring_view GetRuntimeName() const noexcept override { return L"test"; }
		std::wstring_view GetSessionName() const noexcept override { return L"test"; }
		Common::ViewProjectionState GetLeftEyeViewState() const noexcept override { return {}; }
		Common::FloorEyeHeightSample GetFloorEyeHeightSample() const noexcept override { return {}; }
		Xr::HandPoseState GetLeftHandPose() const noexcept override { return {}; }
		Xr::HandPoseState GetRightHandPose() const noexcept override { return rightHand; }
	};

	class SettingsTestRenderBackend final : public Render::IRenderBackend
	{
	  public:
		bool Initialize(HWND) override { return true; }
		void BeginFrame() override {}
		void RenderSimulation(const Physics::BallState&) override {}
		void EndFrame() override {}
		void Shutdown() override {}
		std::wstring_view GetBackendName() const noexcept override { return L"test"; }
		Common::GraphicsBinding GetGraphicsBinding() const noexcept override { return {}; }
		void SetFieldPresentationState(const Common::FieldPresentationState&) override {}
		void SetDesktopMirrorView(const Common::ViewProjectionState&) override {}
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings&) override {}
		void SetRenderQuality(const Common::RenderQualitySettings&) override {}
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>&) override {}
		void SetBatOverlayState(const std::optional<Common::BatOverlayState>&) override {}
	};

	struct RuntimeGeometryGuard final
	{
		double pitchDistance{ Common::GetPitchDistanceMeters() };
		Common::StrikeZoneRuntimeSettings zone{ Common::GetStrikeZoneRuntimeSettings() };
		Common::GroundRuntimeSettings ground{ Common::GetGroundRuntimeSettings() };
		~RuntimeGeometryGuard()
		{
			Common::SetPitchDistanceMeters(pitchDistance);
			Common::SetStrikeZoneRuntimeSettings(zone);
			Common::SetGroundRuntimeSettings(ground);
		}
	};

	// Minimal RFC 4180 reader for the exported session CSV (BOM, CRLF, quoted fields).
	std::vector<std::vector<std::string>> ParseCsv(std::string text)
	{
		if (text.starts_with("\xEF\xBB\xBF"))
			text.erase(0, 3);
		std::vector<std::vector<std::string>> rows(1);
		std::string field;
		bool quoted = false;
		for (std::size_t index = 0; index < text.size(); ++index)
		{
			const char c = text[index];
			if (quoted)
			{
				if (c == '"' && index + 1 < text.size() && text[index + 1] == '"')
				{
					field += '"';
					++index;
				}
				else if (c == '"')
					quoted = false;
				else
					field += c;
			}
			else if (c == '"')
				quoted = true;
			else if (c == ',')
			{
				rows.back().push_back(field);
				field.clear();
			}
			else if (c == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
			{
				rows.back().push_back(field);
				field.clear();
				rows.emplace_back();
				++index;
			}
			else
				field += c;
		}
		if (rows.back().empty())
			rows.pop_back();
		return rows;
	}

	struct CsvTable final
	{
		std::map<std::string, std::size_t> columns;
		std::vector<std::vector<std::string>> rows;

		[[nodiscard]] const std::string& Cell(const std::vector<std::string>& row, const std::string& column) const
		{
			return row.at(columns.at(column));
		}
	};

	CsvTable ReadCsv(const std::filesystem::path& path)
	{
		auto rows = ParseCsv(Tests::ReadFile(path));
		Tests::Check(!rows.empty(), "Exported CSV has a header");
		CsvTable table;
		for (std::size_t column = 0; column < rows.front().size(); ++column)
			table.columns[rows.front()[column]] = column;
		table.rows.assign(rows.begin() + 1, rows.end());
		return table;
	}

	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return left.lexically_normal() == right.lexically_normal();
	}

	std::string Utf8(const std::wstring_view text)
	{
		return Common::TextConversion::TryWideToUtf8(text).value_or(std::string{});
	}
}

namespace BaseballSimulatorVR::Core
{
	struct ApplicationSettingsTestAccess final
	{
		static std::unique_ptr<Application> MakeApplication(
		    const std::filesystem::path& sourceRoot, const std::filesystem::path& userDirectory)
		{
			auto app = std::make_unique<Application>(std::make_unique<SettingsTestXrSystem>(),
			    std::make_unique<SettingsTestRenderBackend>(), Data::PresetRepository{});
			app->m_bundledContentDirectoryPath = (sourceRoot / L"Presets").wstring();
			app->m_userDataDirectoryPath = userDirectory.wstring();
			return app;
		}

		static std::map<std::filesystem::path, std::string> SnapshotBundled(const std::filesystem::path& sourceRoot)
		{
			std::map<std::filesystem::path, std::string> files;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot / L"Presets"))
				if (entry.is_regular_file())
					files[entry.path()] = Tests::ReadFile(entry.path());
			return files;
		}

		static void TestBuiltInPresetsAreTransient(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory user;
			const auto bundled = SnapshotBundled(sourceRoot);
			auto app = MakeApplication(sourceRoot, user.path);
			std::wstring error;
			Tests::Check(app->EnsureRequiredUserSettingsFiles(error) &&
			    app->LoadUserPresetFromJsonFile(L"PitchSettings.Difficulty.Normal.RHP.json") &&
			    app->LoadEnvironmentSettingsFromJson(), "Load a bundled training preset");
			const auto profiles = app->m_pitchProfiles;
			const int activeProfile = app->m_activePitchProfileIndex;
			for (const auto key : {VK_F1, VK_F2, VK_F3, VK_F4})
			{
				app->HandleKeyDown(static_cast<WPARAM>(key));
				Tests::Check(app->m_pitchProfiles.size() == profiles.size() && app->m_activePitchProfileIndex == activeProfile,
				    "Built-in presets leave the loaded profiles and selection untouched");
				for (std::size_t index = 0; index < profiles.size(); ++index)
				{
					Tests::Check(app->m_pitchProfiles[index].name == profiles[index].name &&
					    app->m_pitchProfiles[index].pitchParameters.spinRpm == profiles[index].pitchParameters.spinRpm &&
					    app->m_pitchProfiles[index].pitchParameters.initialVelocity.z ==
					        profiles[index].pitchParameters.initialVelocity.z,
					    "F1-F4 never overwrite a loaded profile's pitch parameters");
				}
			}
			Tests::Check(app->m_activePresetType == Data::PitchPresetType::Changeup &&
			    app->BuildXrUiTelemetry().presetName == L"Changeup" &&
			    app->m_activePitchParameters.spinRpm ==
			        app->m_presetRepository.LoadPitchParameters(Data::PitchPresetType::Changeup).spinRpm,
			    "The built-in pitch is thrown transiently under its own name");
			app->AdjustSpin(100.0);
			Tests::Check(app->m_pitchProfiles[static_cast<std::size_t>(activeProfile)].pitchParameters.spinRpm ==
			    profiles[static_cast<std::size_t>(activeProfile)].pitchParameters.spinRpm,
			    "Adjusting a transient built-in pitch does not leak into the selected profile");
			app->FlushPendingSettingsSaves(true);
			Tests::Check(SnapshotBundled(sourceRoot) == bundled, "Built-in presets never write bundled files");
			Tests::Check(!std::filesystem::exists(user.path / L"PitchSettings.Difficulty.Normal.RHP.json"),
			    "Built-in presets never create preset copies");
			app->SelectPitchProfile(1, L"test_return_to_profile");
			Tests::Check(app->m_activePresetType == Data::PitchPresetType::Custom &&
			    app->m_activePitchParameters.spinRpm == profiles[1].pitchParameters.spinRpm,
			    "Selecting a profile returns to the loaded repertoire");
		}

		static void TestReadOnlyPresetsAndSelectionPersistence(const std::filesystem::path& sourceRoot)
		{
			const RuntimeGeometryGuard geometryGuard;
			Tests::TempDirectory user;
			const auto bundled = SnapshotBundled(sourceRoot);
			std::wstring error;
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->EnsureRequiredUserSettingsFiles(error) &&
				    app->LoadUserPresetFromJsonFile(L"PitchSettings.Difficulty.Normal.LHP.json") &&
				    app->LoadEnvironmentSettingsFromJson(), "Load a generated left-handed bundled variant");
				Tests::Check(app->m_pitcherThrowsLeftHanded &&
				    app->BuildXrUiTelemetry().pitchSettingsFileName == L"難易度 標準 / 左投げ",
				    "The generated variant throws left-handed and is labelled from displayName");
				app->SelectPitchProfile(2, L"test_select");
				app->ToggleRandomPitchProfile(L"test_random");
				app->SetOperationMode(Application::OperationMode::BattingDuel, L"test_mode_flush");
				const auto play = app->m_runtimeSettingsJsonStorage.LoadPlay(
				    (user.path / L"PlaySettings.user.json").wstring(), error);
				Tests::Check(play && play->selectedPitchSettingsFile == L"PitchSettings.Difficulty.Normal.LHP.json" &&
				    play->activePitchProfileIndex == 2 && play->randomPitchProfileEnabled == true,
				    "Profile selection and the random flag are persisted in PlaySettings on the mode switch");
				Tests::Check(SnapshotBundled(sourceRoot) == bundled,
				    "Selecting profiles and toggling random never rewrite bundled presets");
				for (const auto& entry : std::filesystem::directory_iterator(user.path))
					Tests::Check(entry.path().filename().wstring().find(L"Difficulty") == std::wstring::npos,
					    "Selecting profiles never copies presets into the user directory");
				app->SetOperationMode(Application::OperationMode::PitchTrajectory, L"test_mode_back");
				app->SelectPitchProfile(2, L"test_select_again");
				app->Shutdown();
			}
			{
				auto restarted = MakeApplication(sourceRoot, user.path);
				Tests::Check(restarted->LoadStartupSettings(error) &&
				    restarted->m_activePitchSettingsFileName == L"PitchSettings.Difficulty.Normal.LHP.json" &&
				    restarted->m_activePitchProfileIndex == 2 && !restarted->m_randomPitchProfileEnabled &&
				    restarted->m_pitcherThrowsLeftHanded && restarted->m_startupNotices.empty(),
				    "A restart restores the selected entry, profile and random flag from PlaySettings");
				Tests::Check(restarted->m_activePitchParameters.spinRpm ==
				    restarted->m_pitchProfiles[2].pitchParameters.spinRpm,
				    "The restored profile is the active pitch");

				// Explicit save of a bundled entry creates a same-named user override.
				restarted->SaveUserPresetToJson();
				const auto overridePath = user.path / L"PitchSettings.Difficulty.Normal.LHP.json";
				const auto saved = restarted->m_pitchPresetJsonStorage.LoadPitchSettings(overridePath.wstring(), error);
				Tests::Check(saved && saved->pitcherThrowsLeftHanded && !saved->mirrorForLeftHandedPitcher &&
				    saved->displayName == L"難易度 標準 / 左投げ" && saved->pitchProfiles.size() ==
				        restarted->m_pitchProfiles.size(),
				    "Saving a generated variant materializes a left-handed user copy");
				const Data::PresetCatalog catalog = restarted->BuildPresetCatalog();
				const auto entry = catalog.FindPitchSettings(L"PitchSettings.Difficulty.Normal.LHP.json");
				const auto rightEntry = catalog.FindPitchSettings(L"PitchSettings.Difficulty.Normal.RHP.json");
				Tests::Check(entry && entry->origin == Data::PresetOrigin::User && !entry->mirrored &&
				    entry->BuildLabel() == L"難易度 標準 / 左投げ" && rightEntry &&
				    rightEntry->origin == Data::PresetOrigin::Bundled,
				    "The customized copy overrides only its own entry");
				Tests::Check(SnapshotBundled(sourceRoot) == bundled, "Explicit saves never modify bundled presets");

				// Editing a bundled environment writes a customized user copy.
				Tests::Check(restarted->LoadUserPresetFromJsonFile(L"PitchSettings.Toss.Baseball.json") &&
				    restarted->LoadEnvironmentSettingsFromJson(), "Load a preset with a bundled environment");
				Tests::Check(SamePath(restarted->m_environmentSettingsFilePath,
				    sourceRoot / L"Presets" / L"Environment" / L"EnvironmentSettings.Toss.Baseball.json"),
				    "Bundled environments are read from the Presets folder");
				restarted->AdjustBatAngleOffset(2.5, L"test_bundled_environment");
				Tests::Check(!std::filesystem::exists(user.path / L"EnvironmentSettings.Toss.Baseball.json"),
				    "Environment edits are debounced");
				restarted->FlushPendingSettingsSaves(true);
				const auto copy = restarted->m_runtimeSettingsJsonStorage.LoadEnvironment(
				    (user.path / L"EnvironmentSettings.Toss.Baseball.json").wstring(), error);
				Tests::Check(copy && copy->batAngleOffsetDegrees == 2.5 && copy->pitchDistanceMeters == 9.0 &&
				    SamePath(restarted->m_environmentSettingsFilePath, user.path / L"EnvironmentSettings.Toss.Baseball.json"),
				    "The customized environment copy is written to the user directory and used from then on");
				Tests::Check(SnapshotBundled(sourceRoot) == bundled, "Bundled environments are never written");
			}
		}

		static void TestDebouncedSettingsAndKeyRepeat(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory user;
			auto app = MakeApplication(sourceRoot, user.path);
			const auto playPath = user.path / L"PlaySettings.user.json";
			for (int step = 0; step < 10; ++step)
				app->AdjustPlaybackSpeedPercent(-5.0);
			app->HandleKeyDown('1');
			app->FlushPendingSettingsSaves(false);
			Tests::Check(!std::filesystem::exists(playPath) && app->m_playSettingsDirty,
			    "Slider steps and toggles only mark the play settings dirty within the debounce interval");
			app->m_lastSettingsChangeTime -= std::chrono::seconds(1);
			app->FlushPendingSettingsSaves(false);
			std::wstring error;
			const auto saved = app->m_runtimeSettingsJsonStorage.LoadPlay(playPath.wstring(), error);
			Tests::Check(saved && saved->playbackSpeedPercent == 50.0 && !app->m_playSettingsDirty &&
			    !std::filesystem::exists(Common::BuildBackupFilePath(playPath)),
			    "One write persists the final value after the changes settle");

			const auto displayMode = app->m_overlayVisibility.pitchTrajectoryDisplayMode;
			const bool mound = app->m_environmentSettings.enableMoundHeightOffset;
			const bool preview = app->m_duelPitchPreviewEnabled;
			app->HandleKeyDown('1', true);
			app->HandleKeyDown(VK_F12, true);
			app->HandleKeyDown('Q', true);
			app->HandleKeyDown('K', true);
			Tests::Check(app->m_overlayVisibility.pitchTrajectoryDisplayMode == displayMode &&
			    app->m_environmentSettings.enableMoundHeightOffset == mound && app->m_duelPitchPreviewEnabled == preview &&
			    !app->m_playSettingsDirty && !app->m_environmentSettingsDirty &&
			    !std::filesystem::exists(user.path / L"PitchSettings.user.json"),
			    "Keyboard auto-repeat never re-triggers toggles or saves");
			const double spin = app->m_activePitchParameters.spinRpm;
			app->HandleKeyDown(VK_NEXT, true);
			app->HandleKeyDown(VK_OEM_PLUS, true);
			Tests::Check(app->m_activePitchParameters.spinRpm == spin - 100.0 && app->m_playbackSpeedPercent == 55.0,
			    "Auto-repeat still drives continuous adjustments");
			app->Shutdown();
			const auto final = app->m_runtimeSettingsJsonStorage.LoadPlay(playPath.wstring(), error);
			Tests::Check(final && final->playbackSpeedPercent == 55.0, "Shutdown flushes pending settings");
		}

		static void TestStartupSettingsRecovery(const std::filesystem::path& sourceRoot)
		{
			const RuntimeGeometryGuard geometryGuard;
			Tests::TempDirectory user;
			std::wstring error;
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->EnsureRequiredUserSettingsFiles(error) && app->LoadStartupSettings(error) &&
				    app->m_startupNotices.empty(), "A clean installation starts without notices");
				Tests::Check(app->m_activePitchSettingsFileName == L"PitchSettings.user.json" &&
				    app->BuildXrUiTelemetry().pitchSettingsFileName == L"ユーザー設定",
				    "The user's own pitch settings are selected first");
			}
			const auto writeFile = [](const std::filesystem::path& path, const std::string& text)
			{
				std::ofstream output(path, std::ios::binary | std::ios::trunc);
				output << text;
			};
			const auto brokenFiles = [&](const std::wstring& stem)
			{
				std::size_t count = 0;
				for (const auto& entry : std::filesystem::directory_iterator(user.path))
				{
					const auto name = entry.path().filename().wstring();
					count += name.starts_with(stem + L".broken-") && name.ends_with(L".json") ? 1U : 0U;
				}
				return count;
			};

			writeFile(user.path / L"PlaySettings.user.json", "{\n  \"strikeZoneConfigured\": false,\n  \"batMaterial\": Wood\n}\n");
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->LoadStartupSettings(error), "A corrupted play settings file does not stop the start");
				Tests::Check(app->m_startupNotices.size() == 1U &&
				    app->m_startupNotices.front().find(L"PlaySettings.user.json") != std::wstring::npos &&
				    app->m_startupNotices.front().find(L"line 3") != std::wstring::npos &&
				    app->m_startupNotices.front().find(L"restored") != std::wstring::npos,
				    "The user is told which file and line were bad and how it was restored");
				Tests::Check(brokenFiles(L"PlaySettings.user") == 1U &&
				    app->m_runtimeSettingsJsonStorage.LoadPlay((user.path / L"PlaySettings.user.json").wstring(), error),
				    "The bad file is kept as .broken-<timestamp>.json and a loadable file is restored");
			}

			writeFile(user.path / L"EnvironmentSettings.user.json", "{ \"pitchDistanceMeters\": 18.44, }");
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->LoadStartupSettings(error) && app->m_startupNotices.size() == 1U &&
				    app->m_startupNotices.front().find(L"EnvironmentSettings.user.json") != std::wstring::npos &&
				    brokenFiles(L"EnvironmentSettings.user") == 1U &&
				    SamePath(app->m_environmentSettingsFilePath, user.path / L"EnvironmentSettings.user.json"),
				    "A corrupted environment is renamed and restored from its bundled default");
			}

			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->LoadStartupSettings(error), "Start once more");
				app->SaveUserPresetToJson();
				app->SaveUserPresetToJson();
			}
			const auto userPitchPath = user.path / L"PitchSettings.user.json";
			Tests::Check(std::filesystem::exists(Common::BuildBackupFilePath(userPitchPath)),
			    "Saving the user's pitch settings keeps a backup of the previous version");
			writeFile(userPitchPath, "\xEF\xBB\xBF{\n  \"initialSpeedMetersPerSecond\": \"fast\"\n}");
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->LoadStartupSettings(error) && app->m_startupNotices.size() == 1U &&
				    app->m_startupNotices.front().find(L"initialSpeedMetersPerSecond") != std::wstring::npos &&
				    app->m_startupNotices.front().find(L"backup") != std::wstring::npos &&
				    app->m_activePitchSettingsFileName == L"PitchSettings.user.json",
				    "A corrupted user pitch file is restored from its .bak and stays selected");
			}

			writeFile(user.path / L"PitchSettings.Yamamoto.json", "{ broken");
			{
				auto app = MakeApplication(sourceRoot, user.path);
				Tests::Check(app->LoadStartupSettings(error) && app->m_startupNotices.size() == 1U &&
				    app->m_startupNotices.front().find(L"bundled preset") != std::wstring::npos &&
				    !std::filesystem::exists(user.path / L"PitchSettings.Yamamoto.json"),
				    "A corrupted customized preset is set aside and the bundled preset is used again");
				const auto entry = app->BuildPresetCatalog().FindPitchSettings(L"PitchSettings.Yamamoto.json");
				Tests::Check(entry && entry->origin == Data::PresetOrigin::Bundled, "The bundled Yamamoto preset is listed");
			}
		}

		static void TestSessionLogSemantics(const std::filesystem::path& sourceRoot)
		{
			const RuntimeGeometryGuard geometryGuard;
			Tests::TempDirectory user;
			auto app = MakeApplication(sourceRoot, user.path);
			std::wstring error;
			app->m_isSessionLogAvailable =
			    app->m_sessionLogRepository.Initialize((user.path / L"SessionHistory.sqlite3").wstring()).succeeded;
			Tests::Check(app->m_isSessionLogAvailable && app->EnsureRequiredUserSettingsFiles(error) &&
			    app->LoadUserPresetFromJsonFile(L"PitchSettings.Difficulty.Easy.RHP.json") &&
			    app->LoadEnvironmentSettingsFromJson(), "Load a preset with session logging");
			app->m_operationMode = Application::OperationMode::PitchTrajectory;
			app->SelectPitchProfile(1, L"test_profile");
			const std::int64_t pitchEvent = app->m_lastLoggedPitchEventId;
			const std::wstring profileName = app->m_pitchProfiles[1].name;
			app->ToggleDuelPitchPreview(L"test_config");
			app->CyclePitchTrajectoryDisplayMode();
			app->AdjustPlaybackSpeedPercent(-5.0);
			Tests::Check(app->m_lastLoggedPitchEventId == pitchEvent,
			    "UI and configuration events never become the target of pitch results");
			app->ApplyAutoZoneResultTag(Common::PitchZoneCall::Strike, L"test_auto");

			// Fly the selected pitch to the plate; its resolved outcome is recorded on the next reset.
			for (int frame = 0; frame < 300 && !app->m_pitchFlightAnalysis.IsResolved(); ++frame)
			{
				for (int step = 0; step < 10; ++step)
					app->AdvancePitchSimulation(std::chrono::duration<double>(.001));
				auto ball = app->m_pitchSimulationService.GetBallState();
				app->UpdateModeSpecificBallState(&ball);
			}
			Tests::Check(app->m_pitchFlightAnalysis.IsResolved(), "The trajectory pitch reaches the plate");
			const auto call = app->m_pitchFlightAnalysis.Actual().call;
			app->ResetPitchSimulation();

			// A real tee hit records a batted ball.
			app->m_operationMode = Application::OperationMode::TeeBatting;
			app->m_environmentSettings.moundHeightOffsetMeters = 0.0;
			app->m_teeBallPosition = { 0.0, 1.0, app->m_environmentSettings.pitchDistanceMeters };
			app->m_lastFrameDeltaSeconds = 0.01;
			app->m_lastSimulationDeltaSeconds = 0.01;
			app->ResetPitchSimulation();
			auto& xr = static_cast<SettingsTestXrSystem&>(*app->m_xrSystem);
			const auto moveBat = [&](const double x, const double time)
			{
				xr.rightHand = { true, time, 1,
				    { x, 1.0, -2.1 + Physics::BatGripCenterRatio * app->m_environmentSettings.batLengthMeters },
				    { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
				app->RefreshBatPoseFromXrInput();
				auto ball = app->m_pitchSimulationService.GetBallState();
				app->UpdateModeSpecificBallState(&ball);
			};
			moveBat(-0.2, 1.0);
			moveBat(0.2, 1.01);
			Tests::Check(app->m_teeBattedMetrics.hasContact && app->m_lastBattedBallEventId > 0,
			    "A real tee contact is logged as a batted ball");

			const auto csvPath = user.path / L"session.csv";
			Tests::Check(app->m_sessionLogRepository.ExportCurrentSessionCsv(csvPath).succeeded, "Export the session");
			const CsvTable table = ReadCsv(csvPath);
			bool sawPitch = false;
			bool sawBattedBall = false;
			for (const auto& row : table.rows)
			{
				const auto& category = table.Cell(row, "event_category");
				Tests::Check(table.Cell(row, "preset_name") != "Custom", "Logged preset names are real profile names");
				if (std::stoll(table.Cell(row, "id")) == pitchEvent)
				{
					sawPitch = true;
					Tests::Check(category == "pitch" && table.Cell(row, "preset_name") == Utf8(profileName) &&
					    table.Cell(row, "profile_name") == Utf8(profileName) &&
					    table.Cell(row, "pitch_mode") == "trajectory" &&
					    table.Cell(row, "pitch_settings_file") == "PitchSettings.Difficulty.Easy.RHP.json" &&
					    table.Cell(row, "result_label") == "strike",
					    "The automatic result is attached to the pitch row with its real profile name");
					Tests::Check(!table.Cell(row, "plate_x").empty() && !table.Cell(row, "plate_y").empty() &&
					    table.Cell(row, "zone_call") == (call == Common::PitchZoneCall::Strike ? "strike" : "ball") &&
					    !table.Cell(row, "horizontal_break").empty(),
					    "The resolved plate crossing, call and break are stored with the pitch");
				}
				else if (category != "pitch")
				{
					Tests::Check(table.Cell(row, "result_label").empty(), "Configuration events never receive results");
				}
				if (category == "batted_ball")
				{
					sawBattedBall = true;
					Tests::Check(table.Cell(row, "event_type") == "bat_contact" &&
					    std::stod(table.Cell(row, "exit_speed")) > 1.0 &&
					    table.Cell(row, "batted_mode") == "tee-batting" &&
					    std::abs(std::stod(table.Cell(row, "contact_fraction")) - app->m_teeBattedMetrics.batContactFraction) < 1e-9 &&
					    table.Cell(row, "carry_distance").empty() && table.Cell(row, "bat_material") == "Wood",
					    "Batted-ball metrics are stored in normalized columns");
				}
			}
			Tests::Check(sawPitch && sawBattedBall, "The export contains the pitch and the batted ball");
			app->Shutdown();
		}

		static void TestLoggingAndCrashDumps()
		{
			Tests::TempDirectory directory;
			const auto logs = directory.path / L"logs";
			Common::AppLog::Options options{};
			options.maximumFileBytes = 400U;
			options.retainedRotatedFiles = 2;
			Tests::Check(Common::AppLog::Initialize(logs, options) &&
			    Common::AppLog::GetLogFilePath() == logs / L"app.log", "The file logger starts in the logs directory");
			for (int line = 0; line < 40; ++line)
				Common::AppLog::Error(L"Settings", L"設定エラー " + std::to_wstring(line));
			Common::AppLog::Shutdown();
			const auto current = Tests::ReadFile(logs / L"app.log");
			Tests::Check(std::filesystem::exists(logs / L"app.1.log") && std::filesystem::exists(logs / L"app.2.log") &&
			    !std::filesystem::exists(logs / L"app.3.log") && current.size() <= 400U,
			    "The log rotates at its size cap and keeps a bounded number of old files");
			Tests::Check(current.find(Utf8(L"[ERROR] [Settings] 設定エラー 39")) != std::string::npos &&
			    current.ends_with("\r\n"), "Log lines are UTF-8 with timestamps, levels and categories");
			Tests::Check(Common::AppLog::GetLogFilePath().empty(), "Shutdown detaches the file logger");

			const std::wstring systemError = Common::TextConversion::DescribeErrorCode(
			    std::error_code(ERROR_FILE_NOT_FOUND, std::system_category()));
			const std::wstring genericError = Common::TextConversion::DescribeErrorCode(
			    std::make_error_code(std::errc::permission_denied));
			Tests::Check(systemError.find(L"Windows error 2") != std::wstring::npos && systemError.size() > 20U &&
			    genericError.find(L"13") != std::wstring::npos,
			    "Error codes are described through FormatMessageW or the ANSI code page, not byte-wise");

			std::wstring error;
			const auto dump = Common::CrashReporting::WriteMiniDump(nullptr, directory.path / L"crashdumps", error);
			Tests::Check(dump && std::filesystem::file_size(*dump) > 0U &&
			    dump->filename().wstring().starts_with(L"crash-") && dump->extension() == L".dmp",
			    "Crash reports write a minidump into the crash dump directory");
		}

		static void Run(const std::filesystem::path& sourceRoot)
		{
			TestLoggingAndCrashDumps();
			TestBuiltInPresetsAreTransient(sourceRoot);
			TestReadOnlyPresetsAndSelectionPersistence(sourceRoot);
			TestDebouncedSettingsAndKeyRepeat(sourceRoot);
			TestStartupSettingsRecovery(sourceRoot);
			TestSessionLogSemantics(sourceRoot);
		}
	};
}

void RunApplicationSettingsTests(const std::filesystem::path& sourceRoot)
{
	BaseballSimulatorVR::Core::ApplicationSettingsTestAccess::Run(sourceRoot);
}
