#include "TestSupport.hpp"

#include <functional>
#include <iostream>
#include <utility>
#include <vector>

void RunJsonParserTests();
void RunSessionLogTests();
void RunFrameStatisticsTests();
void RunPitchDeliveryTests();
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
void RunBattedBallFlightTests();
void RunDesktopRenderResizeTests();
void RunApplicationSimulationTests();
void RunBundledSettingsTests(const std::filesystem::path& sourceRoot);

int wmain(int argc, wchar_t** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: BaseballSimulatorVR.Tests.exe <repository-root>\n";
		return 2;
	}
	const std::filesystem::path sourceRoot(argv[1]);
	const std::vector<std::pair<std::string_view, std::function<void()>>> suites{
		{ "JSON settings parser", RunJsonParserTests },
		{ "Session log and CSV export", RunSessionLogTests },
		{ "Frame statistics", RunFrameStatisticsTests },
		{ "Pitch delivery and release timing", RunPitchDeliveryTests },
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
		{ "Physical batted-ball aerodynamics and immutable pitch settings", RunBattedBallFlightTests },
		{ "Application mode resets, collisions and calibration", RunApplicationSimulationTests },
		{ "Bundled pitch and runtime settings", [&] { RunBundledSettingsTests(sourceRoot); } }
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
