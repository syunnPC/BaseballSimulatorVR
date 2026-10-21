#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Core/Application.hpp"
#include "../BaseballSimulatorVR/Framework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
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

namespace
{
	// Exit codes: 0 = no suite failed (skips allowed), 1 = a suite failed,
	// 2 = usage or setup error (bad option, no repository root, no suite selected).
	constexpr int ExitSuccess = 0;
	constexpr int ExitFailure = 1;
	constexpr int ExitUsage = 2;

	constexpr std::wstring_view SolutionFileName = L"BaseballSimulatorVR.slnx";
	constexpr const wchar_t* TimeoutScaleVariable = L"BASEBALLSIM_TEST_TIMEOUT_SCALE";
	constexpr const wchar_t* RandomSeedVariable = L"BASEBALLSIM_RANDOM_SEED";
	// Used when neither --seed nor BASEBALLSIM_RANDOM_SEED is given, so test runs repeat exactly.
	constexpr std::uint32_t DefaultRandomSeed = std::mt19937::default_seed;

	struct Suite final
	{
		std::string_view name;
		std::function<void()> run;
		// Needs a D3D12 device (WARP is enough); --skip-gpu reports it as SKIP.
		bool usesGpu{ false };
	};

	struct Options final
	{
		bool help{ false };
		bool list{ false };
		bool skipGpu{ false };
		std::vector<std::wstring> filters;
		std::optional<std::wstring> seed;
		std::optional<std::filesystem::path> repositoryRoot;
	};

	void PrintUsage(std::ostream& out)
	{
		out << "Usage: BaseballSimulatorVR.Tests.exe [options] [<repository-root>]\n"
		       "\n"
		       "Runs the test suites in order. Without <repository-root>, the repository is\n"
		       "found by walking up from the executable directory, then from the current\n"
		       "directory, to the folder that contains BaseballSimulatorVR.slnx.\n"
		       "\n"
		       "Options:\n"
		       "  --list            List the suites (GPU suites are tagged [gpu]) and exit.\n"
		       "  --filter <text>   Run only suites whose name contains <text>, ignoring case.\n"
		       "                    Repeat the option to run every suite matching any filter.\n"
		       "  --skip-gpu        Report the suites that need D3D12/DXGI as SKIP instead of\n"
		       "                    running them.\n"
		       "  --seed <n|random> Seed for random zone and pitch selection, re-applied before\n"
		       "                    every suite (default: BASEBALLSIM_RANDOM_SEED, else 5489).\n"
		       "                    \"random\" picks a fresh seed and prints it.\n"
		       "  --help, -h        Show this help and exit.\n"
		       "\n"
		       "Environment:\n"
		       "  BASEBALLSIM_TEST_TIMEOUT_SCALE  Multiplies the GPU watchdog and deadline\n"
		       "                                  timeouts (default 1, at most 1000).\n"
		       "  BASEBALLSIM_RANDOM_SEED         Default for --seed (also read by the app).\n"
		       "\n"
		       "Exit codes: 0 = no suite failed (skipped suites allowed), 1 = a suite failed,\n"
		       "            2 = usage or setup error.\n";
	}

	[[nodiscard]] std::string ToUtf8(const std::wstring_view text)
	{
		if (text.empty()) return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
		                                       nullptr, nullptr);
		std::string result(static_cast<std::size_t>((std::max)(length, 0)), '\0');
		if (length > 0)
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
			                    nullptr);
		return result;
	}

	[[nodiscard]] constexpr wchar_t LowerAscii(const wchar_t character) noexcept
	{
		return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a') : character;
	}

	// Suite names are ASCII, so an ASCII case fold is enough.
	[[nodiscard]] bool ContainsIgnoringCase(const std::string_view text, const std::wstring_view part)
	{
		if (part.size() > text.size()) return false;
		for (std::size_t start = 0; start + part.size() <= text.size(); ++start)
		{
			bool matches = true;
			for (std::size_t index = 0; matches && index < part.size(); ++index)
				matches = LowerAscii(static_cast<wchar_t>(static_cast<unsigned char>(text[start + index]))) ==
				          LowerAscii(part[index]);
			if (matches) return true;
		}
		return false;
	}

	[[nodiscard]] bool ParseArguments(const int argc, wchar_t** const argv, Options& options, std::string& error)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::wstring_view argument = argv[index];
			if (argument == L"--help" || argument == L"-h" || argument == L"/?")
				options.help = true;
			else if (argument == L"--list")
				options.list = true;
			else if (argument == L"--skip-gpu")
				options.skipGpu = true;
			else if (argument == L"--filter")
			{
				if (index + 1 >= argc)
				{
					error = "--filter needs a suite name substring";
					return false;
				}
				options.filters.emplace_back(argv[++index]);
			}
			else if (argument.starts_with(L"--filter="))
				options.filters.emplace_back(argument.substr(std::wstring_view(L"--filter=").size()));
			else if (argument == L"--seed")
			{
				if (index + 1 >= argc)
				{
					error = "--seed needs a number or \"random\"";
					return false;
				}
				options.seed = argv[++index];
			}
			else if (argument.starts_with(L"--seed="))
				options.seed = std::wstring(argument.substr(std::wstring_view(L"--seed=").size()));
			else if (argument.starts_with(L"-"))
			{
				error = "unknown option " + ToUtf8(argument);
				return false;
			}
			else if (options.repositoryRoot)
			{
				error = "more than one repository root given";
				return false;
			}
			else
				options.repositoryRoot = std::filesystem::path(argument);
		}
		return true;
	}

	[[nodiscard]] std::filesystem::path ExecutableDirectory()
	{
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) return {};
			if (length < buffer.size())
			{
				buffer.resize(length);
				return std::filesystem::path(buffer).parent_path();
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	[[nodiscard]] std::optional<std::filesystem::path> FindRepositoryRootFrom(const std::filesystem::path& start)
	{
		if (start.empty()) return std::nullopt;
		std::error_code error;
		std::filesystem::path directory = std::filesystem::absolute(start, error);
		if (error || directory.empty()) return std::nullopt;
		for (;;)
		{
			if (std::filesystem::is_regular_file(directory / SolutionFileName, error)) return directory;
			const std::filesystem::path parent = directory.parent_path();
			if (parent.empty() || parent == directory) return std::nullopt;
			directory = parent;
		}
	}

	[[nodiscard]] std::optional<std::filesystem::path> FindRepositoryRoot()
	{
		if (auto root = FindRepositoryRootFrom(ExecutableDirectory())) return root;
		std::error_code error;
		const std::filesystem::path current = std::filesystem::current_path(error);
		return error ? std::nullopt : FindRepositoryRootFrom(current);
	}

	[[nodiscard]] std::optional<std::wstring> ReadEnvironmentVariable(const wchar_t* const name)
	{
		const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
		if (size == 0) return std::nullopt;
		std::wstring value(size, L'\0');
		const DWORD length = GetEnvironmentVariableW(name, value.data(), size);
		if (length == 0 || length >= size) return std::nullopt;
		value.resize(length);
		return value;
	}

	[[nodiscard]] bool ReadTimeoutScale(double& scale, std::string& error)
	{
		const std::optional<std::wstring> text = ReadEnvironmentVariable(TimeoutScaleVariable);
		if (!text || text->empty()) return true;
		wchar_t* end = nullptr;
		const double value = std::wcstod(text->c_str(), &end);
		if (end != text->c_str() + text->size() || !std::isfinite(value) || value <= 0.0 || value > 1000.0)
		{
			error = "BASEBALLSIM_TEST_TIMEOUT_SCALE must be a number in (0, 1000], got \"" + ToUtf8(*text) + "\"";
			return false;
		}
		scale = value;
		return true;
	}

	[[nodiscard]] std::optional<std::uint32_t> ParseSeed(const std::wstring_view text)
	{
		if (text.empty() || text.size() > 10) return std::nullopt;
		std::uint64_t value = 0;
		for (const wchar_t digit : text)
		{
			if (digit < L'0' || digit > L'9') return std::nullopt;
			value = value * 10 + static_cast<std::uint64_t>(digit - L'0');
		}
		if (value > UINT32_MAX) return std::nullopt;
		return static_cast<std::uint32_t>(value);
	}

	// --seed wins over BASEBALLSIM_RANDOM_SEED, which wins over DefaultRandomSeed.
	[[nodiscard]] bool ResolveRandomSeed(const Options& options, std::uint32_t& seed, std::string& error)
	{
		if (options.seed)
		{
			if (*options.seed == L"random")
			{
				seed = std::random_device{}();
				return true;
			}
			const auto parsed = ParseSeed(*options.seed);
			if (!parsed)
			{
				error = "--seed must be a 32-bit unsigned integer or \"random\", got \"" + ToUtf8(*options.seed) + "\"";
				return false;
			}
			seed = *parsed;
			return true;
		}
		if (const auto text = ReadEnvironmentVariable(RandomSeedVariable); text && !text->empty())
		{
			const auto parsed = ParseSeed(*text);
			if (!parsed)
			{
				error = "BASEBALLSIM_RANDOM_SEED must be a 32-bit unsigned integer, got \"" + ToUtf8(*text) + "\"";
				return false;
			}
			seed = *parsed;
			return true;
		}
		seed = DefaultRandomSeed;
		return true;
	}

	[[nodiscard]] long long ElapsedMilliseconds(const std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	}

	// Flushed per suite so CI logs stay in order and show progress live, even
	// when stdout is a fully buffered pipe and failures go to stderr.
	void FlushStandardOutput()
	{
		std::cout.flush();
		std::fflush(stdout);
	}

	void ReportFailure(const std::string_view suite, const std::string_view message)
	{
		FlushStandardOutput();
		std::cerr << "FAIL: " << suite << ": " << message << std::endl;
	}
}

int wmain(int argc, wchar_t** argv)
{
	Options options;
	std::string error;
	if (!ParseArguments(argc, argv, options, error))
	{
		std::cerr << "error: " << error << "\n\n";
		PrintUsage(std::cerr);
		return ExitUsage;
	}
	if (options.help)
	{
		PrintUsage(std::cout);
		return ExitSuccess;
	}

	std::filesystem::path sourceRoot;
	const std::vector<Suite> suites{
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
		{ "Desktop rendering resize and restore", RunDesktopRenderResizeTests, true },
		{ "GPU oriented UI panels, atlas reuse and transparency", RunUiPanelRenderingTests, true },
		{ "Asynchronous per-view GPU submission and safe reuse", RunD3d12RenderFrameTests, true },
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

	if (options.list)
	{
		for (const Suite& suite : suites)
			std::cout << suite.name << (suite.usesGpu ? " [gpu]" : "") << '\n';
		return ExitSuccess;
	}

	std::vector<const Suite*> selected;
	for (const Suite& suite : suites)
	{
		bool matches = options.filters.empty();
		for (const std::wstring& filter : options.filters)
			matches = matches || ContainsIgnoringCase(suite.name, filter);
		if (matches) selected.push_back(&suite);
	}
	if (selected.empty())
	{
		std::cerr << "error: no suite matches the --filter arguments (see --list)\n";
		return ExitUsage;
	}

	if (options.repositoryRoot)
	{
		std::error_code directoryError;
		if (!std::filesystem::is_directory(*options.repositoryRoot, directoryError))
		{
			std::cerr << "error: repository root " << ToUtf8(options.repositoryRoot->native()) << " is not a directory\n";
			return ExitUsage;
		}
		sourceRoot = *options.repositoryRoot;
	}
	else if (const auto discovered = FindRepositoryRoot())
		sourceRoot = *discovered;
	else
	{
		std::cerr << "error: no <repository-root> given and no BaseballSimulatorVR.slnx found above the executable "
		             "or the current directory\n\n";
		PrintUsage(std::cerr);
		return ExitUsage;
	}

	if (!ReadTimeoutScale(Tests::timeoutScale, error))
	{
		std::cerr << "error: " << error << '\n';
		return ExitUsage;
	}

	std::uint32_t randomSeed = DefaultRandomSeed;
	if (!ResolveRandomSeed(options, randomSeed, error))
	{
		std::cerr << "error: " << error << '\n';
		return ExitUsage;
	}

	std::cout << "Repository root: " << ToUtf8(sourceRoot.native()) << '\n';
	std::cout << "Random seed: " << randomSeed << " (reproduce with --seed " << randomSeed << ")\n";
	if (Tests::timeoutScale != 1.0) std::cout << "GPU timeout scale: " << Tests::timeoutScale << '\n';

	int passed = 0;
	int failed = 0;
	int skipped = 0;
	for (const Suite* suite : selected)
	{
		if (options.skipGpu && suite->usesGpu)
		{
			++skipped;
			std::cout << "SKIP: " << suite->name << ": --skip-gpu\n";
			FlushStandardOutput();
			continue;
		}
		// Every suite starts from the same random sequence, whatever --filter selected before it.
		BaseballSimulatorVR::Core::SeedRandomEngine(randomSeed);
		const auto start = std::chrono::steady_clock::now();
		try
		{
			suite->run();
			++passed;
			std::cout << "PASS: " << suite->name << " (" << ElapsedMilliseconds(start) << " ms)\n";
		}
		catch (const Tests::SkipSuite& skip)
		{
			++skipped;
			std::cout << "SKIP: " << suite->name << ": " << skip.reason << '\n';
		}
		catch (const std::exception& failure)
		{
			++failed;
			ReportFailure(suite->name, failure.what());
		}
		catch (...)
		{
			++failed;
			ReportFailure(suite->name, "unknown exception");
		}
		FlushStandardOutput();
	}
	std::cout << Tests::checkCount << " checks, " << failed << " failed suites (" << passed << " passed, " << skipped
	          << " skipped, " << selected.size() << " selected of " << suites.size() << ")\n";
	return failed == 0 ? ExitSuccess : ExitFailure;
}
