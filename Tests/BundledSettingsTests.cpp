#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/PitchPresetJsonStorage.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"
#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
	BaseballSimulatorVR::Physics::BallState SimulatePlateCrossing(
	    const BaseballSimulatorVR::Baseball::PitchParameters& parameters)
	{
		using namespace BaseballSimulatorVR::Physics;
		PitchPhysicsEngine engine;
		BallState previous;
		previous.position = { 0.0, parameters.releaseHeightMeters, 0.0 };
		previous.velocity = parameters.initialVelocity;
		previous.spinAxis = parameters.spinAxis;
		previous.spinRpm = parameters.spinRpm;
		for (int step = 0; step < 2000; ++step)
		{
			BallState current = engine.Step(previous, parameters, std::chrono::duration<double>(0.001));
			if (current.position.z >= 18.44)
			{
				const double blend = (18.44 - previous.position.z) / (current.position.z - previous.position.z);
				current.position = previous.position + (current.position - previous.position) * blend;
				current.flightTimeSeconds = previous.flightTimeSeconds +
				    (current.flightTimeSeconds - previous.flightTimeSeconds) * blend;
				return current;
			}
			previous = current;
		}
		throw std::runtime_error("Pitch must reach home plate within 2 simulated seconds");
	}

	void CheckDifficultyPresets(const std::filesystem::path& sourceRoot)
	{
		using namespace BaseballSimulatorVR;
		struct Pitch { std::wstring_view name; double kmh, horizontal, vertical, rpm; };
		struct Level { std::wstring_view name; std::vector<Pitch> pitches; };
		const std::array levels{
			Level{L"Easy", {{L"ストレート",135,0,.40,2300}, {L"カーブ",110,.25,-.35,2600},
				{L"スライダー",115,.20,-.05,2500}}},
			Level{L"Normal", {{L"ストレート",145,0,.40,2300}, {L"チェンジアップ",120,-.25,.12,1600},
				{L"カーブ",120,.25,-.35,2600}, {L"スライダー",125,.20,-.05,2500}, {L"フォーク",115,0,.02,900}}},
			Level{L"Hard", {{L"ストレート",155,0,.40,2300}, {L"シュート",150,-.40,.18,2100},
				{L"カーブ",130,.25,-.35,2600}, {L"スライダー",135,.20,-.05,2500},
				{L"フォーク",130,0,.02,900}, {L"カット",150,.10,.25,2400}}},
			Level{L"Expert", {{L"ストレート",165,0,.40,2300}, {L"ツーシーム",165,-.30,.25,2200},
				{L"シュート",155,-.40,.18,2100}, {L"チェンジアップ",145,-.25,.12,1600},
				{L"スライダー",145,.20,-.05,2500}, {L"フォーク",135,0,.02,900},
				{L"カーブ",135,.25,-.35,2600}, {L"カット",160,.10,.25,2400}, {L"スイーパー",140,.40,.05,2600}}}
		};
		Data::PitchPresetJsonStorage storage;
		std::wstring error;
		for (const auto& level : levels)
		{
			const std::wstring prefix = L"PitchSettings.Difficulty." + std::wstring{level.name};
			const auto right = storage.LoadPitchSettings((sourceRoot / (prefix + L".RHP.json")).wstring(), error);
			const auto left = storage.LoadPitchSettings((sourceRoot / (prefix + L".LHP.json")).wstring(), error);
			Tests::Check(right && left, "Each difficulty includes a loadable right- and left-handed preset");
			Tests::Check(!right->pitcherThrowsLeftHanded && left->pitcherThrowsLeftHanded,
				"Difficulty handedness reaches the production runtime setting");
			Tests::Check(right->pitchProfiles.size() == level.pitches.size() &&
				left->pitchProfiles.size() == level.pitches.size(), "Difficulty repertoire has exactly the requested pitches");
			Tests::Check(right->randomPitchProfileEnabled && left->randomPitchProfileEnabled &&
				right->activePitchProfileIndex == 0 && left->activePitchProfileIndex == 0,
				"Difficulty starts with the fastball and enables uniform random repertoire selection");
			for (std::size_t i = 0; i < level.pitches.size(); ++i)
			{
				const auto& expected = level.pitches[i];
				const auto& r = right->pitchProfiles[i].pitchParameters;
				const auto& l = left->pitchProfiles[i].pitchParameters;
				Tests::Check(right->pitchProfiles[i].name == expected.name && left->pitchProfiles[i].name == expected.name,
					"Difficulty profiles preserve the requested pitch labels and order");
				Tests::Check(std::abs(Physics::Length(r.initialVelocity) * 3.6 - expected.kmh) < 1e-9 &&
					std::abs(Physics::Length(l.initialVelocity) * 3.6 - expected.kmh) < 1e-9,
					"Every requested release speed is converted from km/h to m/s exactly");
				Tests::Check(r.initialVelocity.x == 0 && r.initialVelocity.y == 0 &&
					l.initialVelocity.x == 0 && l.initialVelocity.y == 0 &&
					r.releaseHeightMeters == 1.8 && l.releaseHeightMeters == 1.8,
					"Difficulty files retain the single-speed schema and common release height");
				Tests::Check(std::abs(Physics::Length(r.spinAxis) - 1) < 1e-9 &&
					std::abs(Physics::Length(l.spinAxis) - 1) < 1e-9 &&
					r.spinAxis.x == l.spinAxis.x && r.spinAxis.y == -l.spinAxis.y &&
					r.spinAxis.z == 0 && l.spinAxis.z == 0,
					"Axial spin vectors mirror horizontally without reversing vertical Magnus force");
				Tests::Check(r.spinRpm == expected.rpm && l.spinRpm == expected.rpm &&
					r.liftCoefficient == l.liftCoefficient && r.dragCoefficient == l.dragCoefficient &&
					!r.useCoefficientTable && !l.useCoefficientTable &&
					!r.enableSeamPerturbation && !l.enableSeamPerturbation,
					"Fitted pitch shapes use matching coefficients without table or seam overrides");
				const auto rCross = SimulatePlateCrossing(r);
				const auto lCross = SimulatePlateCrossing(l);
				auto reference = r;
				reference.liftCoefficient = 0;
				const auto baseline = SimulatePlateCrossing(reference);
				Tests::Check(std::abs(rCross.position.x - expected.horizontal) < .0001 &&
					std::abs(rCross.position.y - baseline.position.y - expected.vertical) < .0001,
					"Every speed-specific profile produces the documented general pitch shape");
				Tests::Check(std::abs(rCross.position.x + lCross.position.x) < 1e-10 &&
					std::abs(rCross.position.y - lCross.position.y) < 1e-10 &&
					std::abs(rCross.flightTimeSeconds - lCross.flightTimeSeconds) < 1e-10,
					"Left-handed profiles mirror real simulated flight while preserving height and arrival time");
			}
			for (const auto* settings : {&*right, &*left})
			{
				const auto& root = settings->pitchParameters;
				const auto& fastball = settings->pitchProfiles.front().pitchParameters;
				Tests::Check(Physics::Length(root.initialVelocity - fastball.initialVelocity) < 1e-12 &&
					Physics::Length(root.spinAxis - fastball.spinAxis) < 1e-12 &&
					root.spinRpm == fastball.spinRpm && root.liftCoefficient == fastball.liftCoefficient &&
					root.dragCoefficient == fastball.dragCoefficient && !root.useCoefficientTable &&
					settings->environmentSettingsFile == L"EnvironmentSettings.user.json",
					"Root pitch matches the initial fastball and retains the user's environment");
			}
		}
	}
}

void RunBundledSettingsTests(const std::filesystem::path& sourceRoot)
{
	using namespace BaseballSimulatorVR;
	std::wstring error;
	Data::PitchPresetJsonStorage pitchStorage;
	Data::RuntimeSettingsJsonStorage runtimeStorage;
	CheckDifficultyPresets(sourceRoot);
	for (const auto& entry : std::filesystem::directory_iterator(sourceRoot))
	{
		const auto name = entry.path().filename().wstring();
		if (name.starts_with(L"PitchSettings.") && name.ends_with(L".json"))
		{
			const auto settings = pitchStorage.LoadPitchSettings(entry.path().wstring(), error);
			if (!settings) std::wcerr << name << L": " << error << L'\n';
			Tests::Check(settings.has_value(), "All bundled pitch settings load with production loader");
			const auto environmentPath = sourceRoot / settings->environmentSettingsFile;
			Tests::Check(runtimeStorage.LoadEnvironment(environmentPath.wstring(), error).has_value(),
			             "Referenced environment file loads");
		}
		if (name.starts_with(L"EnvironmentSettings.") && name.ends_with(L".json"))
		{
			Tests::Check(runtimeStorage.LoadEnvironment(entry.path().wstring(), error).has_value(),
			             "All bundled environment settings load");
		}
		if (name.starts_with(L"PlaySettings.") && name.ends_with(L".json"))
		{
			Tests::Check(runtimeStorage.LoadPlay(entry.path().wstring(), error).has_value(),
			             "All bundled play settings load");
		}
	}
	const auto yamamoto = pitchStorage.LoadPitchSettings((sourceRoot / L"PitchSettings.Yamamoto.json").wstring(), error);
	Tests::Check(yamamoto && yamamoto->pitchProfiles.size() == 6, "Six Yamamoto pitch profiles");
	Tests::Check(!yamamoto->randomPitchProfileEnabled && yamamoto->activePitchProfileIndex == 0,
	             "Yamamoto starts on FourSeam with explicit profile selection");
	constexpr std::array<std::wstring_view, 6> names{
		L"Yamamoto_FourSeam", L"Yamamoto_Splitter", L"Yamamoto_Curveball",
		L"Yamamoto_Cutter", L"Yamamoto_Sinker", L"Yamamoto_Slider"
	};
	constexpr std::array<double, 6> speedsMph{ 95.4, 90.9, 76.8, 91.1, 94.8, 86.3 };
	constexpr std::array<double, 6> spinRpm{ 2200.0, 1428.0, 2760.0, 2435.0, 2240.0, 2781.0 };
	constexpr std::array<double, 6> horizontalBreakInches{ -6.9, -11.3, 12.0, 3.5, -13.6, 5.9 };
	constexpr std::array<double, 6> inducedVerticalBreakInches{ 16.0, 2.0, -16.1, 7.8, 12.0, 0.7 };
	for (std::size_t index = 0; index < names.size(); ++index)
	{
		const auto& profile = yamamoto->pitchProfiles[index];
		const auto& parameters = profile.pitchParameters;
		Tests::Check(profile.name == names[index], "Expected Yamamoto pitch profile name");
		Tests::Check(std::abs(parameters.initialVelocity.z - speedsMph[index] * 0.44704) < 1e-8,
		             "Observed mph converted to m/s");
		Tests::Check(parameters.spinRpm == spinRpm[index], "Observed spin rate preserved");
		Tests::Check(std::abs(Physics::Length(parameters.spinAxis) - 1.0) < 1e-7, "Fitted spin axis normalized");
		Tests::Check(!parameters.useCoefficientTable && !parameters.enableSeamPerturbation,
		             "Use fitted coefficients without unobserved seam perturbation");
		const auto crossing = SimulatePlateCrossing(parameters);
		auto reference = parameters;
		reference.liftCoefficient = 0.0;
		const auto referenceCrossing = SimulatePlateCrossing(reference);
		const double horizontalBreak = crossing.position.x / 0.0254;
		const double verticalBreak = (crossing.position.y - referenceCrossing.position.y) / 0.0254;
		Tests::Check(std::abs(horizontalBreak - horizontalBreakInches[index]) < 0.02,
		             "C++ simulation reproduces documented fitted horizontal break");
		Tests::Check(std::abs(verticalBreak - inducedVerticalBreakInches[index]) < 0.02,
		             "C++ simulation reproduces documented fitted induced vertical break");
	}
}
