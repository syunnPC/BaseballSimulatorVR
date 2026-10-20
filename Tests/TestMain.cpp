#include "TestSupport.hpp"

#include <functional>
#include <iostream>
#include <utility>
#include <vector>

void RunJsonParserTests();
void RunSessionLogTests();
void RunFrameStatisticsTests();
void RunPitchDeliveryTests();
void RunPitchAerodynamicsTests();
void RunBatBallCollisionTests();
void RunSweptBatCollisionTests();
void RunFieldPresentationTests();
void RunFieldStatisticsCompactTests();
void RunStrikeZoneCalibrationTests();
void RunUiTextRenderingTests();
void RunUiPanelRenderingTests();
void RunD3d12RenderFrameTests();
void RunSceneGeometryTests();
void RunTrajectoryTrailTests();
void RunTrajectoryMeshTests();
void RunBattedBallFlightTests();
void RunTossPhysicsTests(const std::filesystem::path& sourceRoot);
void RunBallRenderingTests();
void RunStrikeZoneCalibrationUiTests();
void RunMetricsPanelControlsTests();
void RunUiPointerInputTests();
void RunDesktopRenderResizeTests();
void RunApplicationSimulationTests(const std::filesystem::path& sourceRoot);
void RunBundledSettingsTests(const std::filesystem::path& sourceRoot);
void RunBeginnerPresetTests(const std::filesystem::path& sourceRoot);
void RunPitchPresetUiTests();
void RunHomePlateGeometryTests();
void RunPitchFlightAnalysisTests();

int wmain(int argc, wchar_t** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: BaseballSimulatorVR.Tests.exe <repository-root>\n";
		return 2;
	}
	const std::filesystem::path sourceRoot(argv[1]);
	const std::vector<std::pair<std::string_view, std::function<void()>>> suites{
		{ "Readable pitch preset labels and full-width layout", RunPitchPresetUiTests },
		{ "Swept ball and home plate prism geometry", RunHomePlateGeometryTests },
		{ "Pitch verdict, frontal trace and total bending", RunPitchFlightAnalysisTests },
		{ "JSON settings parser", RunJsonParserTests },
		{ "Session log and CSV export", RunSessionLogTests },
		{ "Frame statistics", RunFrameStatisticsTests },
		{ "Pitch delivery and release timing", RunPitchDeliveryTests },
		{ "Transverse pitch spin and continuous seam perturbation", RunPitchAerodynamicsTests },
		{ "Bat-ball impact physics", RunBatBallCollisionTests },
		{ "Synchronized bat sweep and grip geometry", RunSweptBatCollisionTests },
		{ "Pitcher and outfield statistics geometry", RunFieldPresentationTests },
		{ "Concise field scoreboard and concealed pitch values", RunFieldStatisticsCompactTests },
		{ "Floor-relative strike-zone calibration", RunStrikeZoneCalibrationTests },
		{ "Antialiased UI text and bounded cache", RunUiTextRenderingTests },
		{ "Desktop rendering resize and restore", RunDesktopRenderResizeTests },
		{ "GPU oriented UI panels, atlas reuse and transparency", RunUiPanelRenderingTests },
		{ "Asynchronous per-view GPU submission and safe reuse", RunD3d12RenderFrameTests },
		{ "Cached scene geometry and prepared projection equivalence", RunSceneGeometryTests },
		{ "Independent pitch and batted trajectory history and rendering", RunTrajectoryTrailTests },
		{ "Stable world trajectory mesh and near-plane clipping", RunTrajectoryMeshTests },
		{ "Physical batted-ball aerodynamics and immutable pitch settings", RunBattedBallFlightTests },
		{ "Ball dimensions and underhand toss rendering", RunBallRenderingTests },
		{ "Independent strike-zone calibration UI and progress", RunStrikeZoneCalibrationUiTests },
		{ "Metrics controls and independent manual zone buttons", RunMetricsPanelControlsTests },
		{ "Batting pointer visibility and intentional panel clicks", RunUiPointerInputTests },
		{ "Nine-meter lob toss presets and variable ball physics", [&] { RunTossPhysicsTests(sourceRoot); } },
		{ "Application mode resets, collisions and calibration", [&] { RunApplicationSimulationTests(sourceRoot); } },
		{ "Bundled pitch and runtime settings", [&] { RunBundledSettingsTests(sourceRoot); } },
		{ "Beginner softball presets and physical pitch shapes", [&] { RunBeginnerPresetTests(sourceRoot); } }
	};
	int failures = 0;
	for (const auto& [name, run] : suites)
	{
		try
		{
			run();
			std::cout << "PASS: " << name << '\n';
		}
		catch (const std::exception& error)
		{
			++failures;
			std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
		}
	}
	std::cout << Tests::checkCount << " checks, " << failures << " failed suites\n";
	return failures == 0 ? 0 : 1;
}
