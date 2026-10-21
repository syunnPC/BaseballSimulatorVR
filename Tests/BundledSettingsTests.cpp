#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/PitchPresetJsonStorage.hpp"
#include "../BaseballSimulatorVR/Data/PresetCatalog.hpp"
#include "../BaseballSimulatorVR/Common/PitchPresetUi.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"
#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"

#include <array>
#include <optional>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
	// Loads a catalog entry (including generated opposite-hand variants) from the bundled Presets.
	std::optional<BaseballSimulatorVR::Data::PitchRuntimeSettings> LoadBundledPitch(
	    const std::filesystem::path& sourceRoot, const std::wstring_view id, std::wstring& error)
	{
		const BaseballSimulatorVR::Data::PresetCatalog catalog{ sourceRoot / L"Presets", {} };
		const auto entry = catalog.FindPitchSettings(id);
		if (!entry)
		{
			error = L"no bundled entry " + std::wstring{ id };
			return std::nullopt;
		}
		return catalog.LoadPitchSettings(*entry, error);
	}

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
		previous.properties = parameters.ballProperties;
		previous.aerodynamicsModel = parameters.usePhysicalAerodynamics
		    ? BallAerodynamicsModel::Physical : BallAerodynamicsModel::PitchProfile;
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

	void CheckTrainingPresets(const std::filesystem::path& sourceRoot)
	{
		using namespace BaseballSimulatorVR;
		struct Pitch { std::wstring_view name; double kmh, horizontal, vertical, rpm; };
		struct Level { std::wstring_view name; std::vector<Pitch> pitches; double releaseHeightMeters{ 1.8 }; };
		const std::array levels{
			Level{L"VeryEasy", {{L"ストレート",110,0,.40,2300}, {L"スライダー",100,.20,-.05,2500},
				{L"カーブ",90,.25,-.35,2600}, {L"シンカー",100,-.30,-.10,2200}}},
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
				{L"カーブ",135,.25,-.35,2600}, {L"カット",160,.10,.25,2400}, {L"スイーパー",140,.40,.05,2600}}},
			Level{L"Challenge1", {{L"ストレート",120,0,.40,2300}, {L"カーブ",100,.25,-.35,2600},
				{L"シンカー",110,-.30,-.10,2200}}, .1},
			Level{L"Challenge2", {{L"ツーシーム",170,-.30,.25,2200}}}
		};
		std::wstring error;
		for (const auto& level : levels)
		{
			const std::wstring prefix = (level.name.starts_with(L"Challenge")
				? L"PitchSettings." : L"PitchSettings.Difficulty.") + std::wstring{level.name};
			const auto right = LoadBundledPitch(sourceRoot, prefix + L".RHP.json", error);
			const auto left = LoadBundledPitch(sourceRoot, prefix + L".LHP.json", error);
			Tests::Check(right && left, "Each training set includes a loadable right- and left-handed preset");
			Tests::Check(!right->pitcherThrowsLeftHanded && left->pitcherThrowsLeftHanded,
				"Preset handedness reaches the production runtime setting");
			Tests::Check(right->pitchProfiles.size() == level.pitches.size() &&
				left->pitchProfiles.size() == level.pitches.size(), "Training repertoire has exactly the requested pitches");
			Tests::Check(right->randomPitchProfileEnabled && left->randomPitchProfileEnabled &&
				right->activePitchProfileIndex == 0 && left->activePitchProfileIndex == 0,
				"Training preset starts with its first profile and enables uniform random repertoire selection");
			for (std::size_t i = 0; i < level.pitches.size(); ++i)
			{
				const auto& expected = level.pitches[i];
				const auto& r = right->pitchProfiles[i].pitchParameters;
				const auto& l = left->pitchProfiles[i].pitchParameters;
				Tests::Check(right->pitchProfiles[i].name == expected.name && left->pitchProfiles[i].name == expected.name,
					"Training profiles preserve the requested pitch labels and order");
				Tests::Check(std::abs(Physics::Length(r.initialVelocity) * 3.6 - expected.kmh) < 1e-9 &&
					std::abs(Physics::Length(l.initialVelocity) * 3.6 - expected.kmh) < 1e-9,
					"Every requested release speed is converted from km/h to m/s exactly");
				Tests::Check(r.initialVelocity.x == 0 && r.initialVelocity.y == 0 &&
					l.initialVelocity.x == 0 && l.initialVelocity.y == 0 &&
					r.releaseHeightMeters == level.releaseHeightMeters &&
					l.releaseHeightMeters == level.releaseHeightMeters,
					"Training profiles retain exact speeds and the preset release height");
				Tests::Check(std::abs(Physics::Length(r.spinAxis) - 1) < 1e-9 &&
					std::abs(Physics::Length(l.spinAxis) - 1) < 1e-9 &&
					r.spinAxis.x == l.spinAxis.x && r.spinAxis.y == -l.spinAxis.y &&
					r.spinAxis.z == 0 && l.spinAxis.z == 0,
					"Axial spin vectors mirror horizontally without reversing vertical Magnus force");
				Tests::Check(r.spinRpm == expected.rpm && l.spinRpm == expected.rpm &&
					r.liftCoefficient == l.liftCoefficient && r.dragCoefficient == l.dragCoefficient &&
					!r.usePhysicalAerodynamics && !l.usePhysicalAerodynamics &&
					!r.underhandToss && !l.underhandToss &&
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
				const auto& initialProfile = settings->pitchProfiles.front().pitchParameters;
				Tests::Check(Physics::Length(root.initialVelocity - initialProfile.initialVelocity) < 1e-12 &&
					Physics::Length(root.spinAxis - initialProfile.spinAxis) < 1e-12 &&
					root.releaseHeightMeters == initialProfile.releaseHeightMeters &&
					root.spinRpm == initialProfile.spinRpm && root.liftCoefficient == initialProfile.liftCoefficient &&
					root.dragCoefficient == initialProfile.dragCoefficient && !root.useCoefficientTable &&
					settings->environmentSettingsFile == L"EnvironmentSettings.user.json",
					"Root pitch matches the initial profile including its release height and retains the user's environment");
			}
		}
	}

	void CheckSlowFastballPresets(const std::filesystem::path& sourceRoot)
	{
		using namespace BaseballSimulatorVR;
		Data::RuntimeSettingsJsonStorage runtimeStorage;
		std::wstring error;
		constexpr std::array<double, 4> speedsKmh{70, 80, 90, 100};
		for (const std::wstring_view kind : {L"Baseball", L"Softball"})
		{
			const std::wstring environmentName = L"EnvironmentSettings.SlowFastball." + std::wstring(kind) + L".json";
			const auto environment = runtimeStorage.LoadEnvironment(
			    (sourceRoot / L"Presets" / L"Environment" / environmentName).wstring(), error);
			const double radius = kind == L"Baseball" ? .037 : .0485;
			const double mass = kind == L"Baseball" ? .145 : .190;
			Tests::Check(environment && environment->pitchDistanceMeters == 18.44 &&
			    environment->moundHeightOffsetMeters == .18 && !environment->enableMoundHeightOffset &&
			    environment->ballProperties.radiusMeters == radius &&
			    environment->ballProperties.massKilograms == mass &&
			    environment->ballProperties.restitutionAtReferenceSpeed == .45,
			    "Slow fastballs retain the normal distance and mound with their selected ball properties");
			std::array<Physics::Vector3, 4> rightHandedVelocities{};
			for (const std::wstring_view hand : {L"RHP", L"LHP"})
			{
				const auto settings = LoadBundledPitch(sourceRoot,
				    L"PitchSettings.SlowFastball." + std::wstring(kind) + L"." + std::wstring(hand) + L".json", error);
				Tests::Check(settings && settings->pitchProfiles.size() == speedsKmh.size() &&
				    settings->environmentSettingsFile == environmentName && settings->randomPitchProfileEnabled &&
				    settings->activePitchProfileIndex == 0 && settings->pitcherThrowsLeftHanded == (hand == L"LHP"),
				    "Each ball has a right- and left-handed uniform four-speed fastball preset");
				for (std::size_t index = 0; index < speedsKmh.size(); ++index)
				{
					const auto& profile = settings->pitchProfiles[index];
					auto parameters = profile.pitchParameters;
					Tests::Check(profile.name == L"ストレート " + std::to_wstring(static_cast<int>(speedsKmh[index])) + L"km/h" &&
					    std::abs(Physics::Length(parameters.initialVelocity) * 3.6 - speedsKmh[index]) < 1e-9 &&
					    parameters.releaseHeightMeters == 1.8 && parameters.usePhysicalAerodynamics &&
					    !parameters.underhandToss && parameters.spinAxis.x == -1 &&
					    parameters.spinAxis.y == 0 && parameters.spinAxis.z == 0 && parameters.spinRpm == 2300 &&
					    !parameters.useCoefficientTable && !parameters.enableSeamPerturbation,
					    "The selectable profiles contain only overhand backspin fastballs at 70, 80, 90 and 100 km/h");
					parameters.ballProperties = environment->ballProperties;
					const auto crossing = SimulatePlateCrossing(parameters);
					Tests::Check(std::abs(crossing.position.y - 1.05) < 1e-6 &&
					    std::abs(crossing.position.x) < 1e-12 && crossing.velocity.y < 0,
					    "Every untargeted physical fastball reaches the normal-distance strike-zone centre while descending");
					if (hand == L"RHP") rightHandedVelocities[index] = parameters.initialVelocity;
					else Tests::Check(Physics::Length(parameters.initialVelocity - rightHandedVelocities[index]) < 1e-12,
					    "Changing pitcher handedness preserves the vertical backspin fastball flight");
				}
				Tests::Check(Physics::Length(settings->pitchParameters.initialVelocity -
				    settings->pitchProfiles.front().pitchParameters.initialVelocity) < 1e-12,
				    "The root pitch matches the initial 70 km/h profile");
			}
		}
	}
}

namespace
{
	void CheckPresetLayoutAndLabels(const std::filesystem::path& sourceRoot)
	{
		using namespace BaseballSimulatorVR;
		for (const auto& entry : std::filesystem::directory_iterator(sourceRoot))
		{
			const auto name = entry.path().filename().wstring();
			Tests::Check(!(name.ends_with(L".json") && (name.starts_with(L"PitchSettings.") ||
			    name.starts_with(L"EnvironmentSettings.") || name.starts_with(L"PlaySettings."))),
			    "Settings presets live under Presets/ instead of the repository root");
		}
		const auto presets = sourceRoot / L"Presets";
		Tests::Check(Data::PresetCatalog::IsBundledPresetsDirectory(presets),
		    "Presets/ contains the pitch, environment and play defaults");
		Data::PitchPresetJsonStorage storage;
		std::wstring error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(presets))
		{
			const auto name = entry.path().filename().wstring();
			Tests::Check(!name.ends_with(L".sample.json") && !name.ends_with(L".user.json"),
			    "Bundled presets use *.default.json instead of sample or user copies");
			if (!name.starts_with(L"PitchSettings."))
				continue;
			Tests::Check(!name.ends_with(L".RHP.json") && !name.ends_with(L".LHP.json"),
			    "Only one handedness file is shipped per pitch family");
			const auto settings = storage.LoadPitchSettings(entry.path().wstring(), error);
			Tests::Check(settings && !settings->displayName.empty() && !settings->pitcherThrowsLeftHanded,
			    "Every bundled pitch preset declares its displayName and the authored right hand");
		}

		const Data::PresetCatalog catalog{ presets, {} };
		const auto entries = catalog.EnumeratePitchSettings();
		const auto label = [&](const std::wstring& id)
		{
			for (const auto& entry : entries)
				if (entry.id == id) return entry.BuildLabel();
			return std::wstring{};
		};
		for (const auto hand : {L"RHP", L"LHP"})
		{
			const std::wstring expectedHand = std::wstring_view{hand} == L"RHP" ? L"右投げ" : L"左投げ";
			for (const auto variant : {L"Beginner.Softball.", L"Beginner.BigBreak.Softball."})
			{
				const auto text = label(std::wstring{L"PitchSettings."} + variant + hand + L".json");
				Tests::Check(text.find(L"初心者") != std::wstring::npos && text.find(L"ソフトボール") != std::wstring::npos &&
				    text.find(expectedHand) != std::wstring::npos,
				    "Both beginner variants identify training level, ball type and throwing hand");
				Tests::Check(text.find(std::wstring_view{variant}.starts_with(L"Beginner.BigBreak") ?
				    L"大きな変化" : L"標準変化") != std::wstring::npos,
				    "Normal and large beginner breaking pitches remain distinguishable");
			}
			for (const auto ball : {L"Baseball", L"Softball"})
			{
				const auto text = label(std::wstring{L"PitchSettings.SlowFastball."} + ball + L"." + hand + L".json");
				Tests::Check(text.find(L"低速ストレート") != std::wstring::npos && text.find(expectedHand) != std::wstring::npos &&
				    text.find(std::wstring_view{ball} == L"Baseball" ? L"硬式野球" : L"ソフトボール") != std::wstring::npos,
				    "Slow fastball labels preserve ball type and throwing hand");
			}
			for (const auto difficulty : {L"VeryEasy", L"Easy", L"Normal", L"Hard", L"Expert"})
			{
				const auto text = label(std::wstring{L"PitchSettings.Difficulty."} + difficulty + L"." + hand + L".json");
				Tests::Check(text.find(L"難易度") != std::wstring::npos && text.ends_with(L" / " + expectedHand),
				    "Every bundled difficulty identifies the throwing hand");
			}
			Tests::Check(label(std::wstring{L"PitchSettings.Difficulty.VeryEasy."} + hand + L".json") ==
			    L"難易度 ベリーイージー / " + expectedHand,
			    "The very easy preset displays its Japanese name and throwing hand");
			for (const auto challenge : {L"Challenge1", L"Challenge2"})
			{
				Tests::Check(label(std::wstring{L"PitchSettings."} + challenge + L"." + hand + L".json") ==
				    std::wstring{std::wstring_view{challenge} == L"Challenge1" ? L"チャレンジ1 / " : L"チャレンジ2 / "} + expectedHand,
				    "Challenge presets display distinct Japanese labels with their throwing hand");
			}
		}
		Tests::Check(label(L"PitchSettings.Toss.Baseball.json") == L"トス\n硬式野球" &&
		    label(L"PitchSettings.Toss.Softball.json") == L"トス\nソフトボール" &&
		    label(L"PitchSettings.Yamamoto.json") == L"山本由伸\n硬式野球 / 右投げ",
		    "Single-hand presets keep their labels without a generated hand suffix");
		for (const auto& entry : entries)
		{
			if (!entry.id.ends_with(L".LHP.json"))
				continue;
			const auto settings = catalog.LoadPitchSettings(entry, error);
			Tests::Check(entry.mirrored && entry.pitcherThrowsLeftHanded == true && settings &&
			    settings->pitcherThrowsLeftHanded && entry.sourcePath.filename().wstring().find(L"LHP") == std::wstring::npos,
			    "Left-handed variants are generated from the right-handed file at load time");
		}
		const auto defaultPitch = storage.LoadPitchSettings(
		    (presets / L"Pitch" / L"PitchSettings.default.json").wstring(), error);
		Tests::Check(defaultPitch && defaultPitch->displayName == L"ユーザー設定" &&
		    defaultPitch->environmentSettingsFile == L"EnvironmentSettings.user.json" &&
		    !catalog.FindPitchSettings(L"PitchSettings.default.json"),
		    "The default pitch template seeds the user's file and is not listed itself");
	}
}

void RunApplicationSettingsTests(const std::filesystem::path& sourceRoot);

void RunBundledSettingsTests(const std::filesystem::path& sourceRoot)
{
	// Application-level persistence of these settings (read-only presets, selection state,
	// debounced saves, startup recovery and session-log semantics).
	RunApplicationSettingsTests(sourceRoot);
	using namespace BaseballSimulatorVR;
	std::wstring error;
	Data::RuntimeSettingsJsonStorage runtimeStorage;
	CheckTrainingPresets(sourceRoot);
	CheckSlowFastballPresets(sourceRoot);
	CheckPresetLayoutAndLabels(sourceRoot);
	const Data::PresetCatalog catalog{ sourceRoot / L"Presets", {} };
	const auto entries = catalog.EnumeratePitchSettings();
	Tests::Check(entries.size() == 27U, "The bundled catalog offers every preset and both hands of mirrored families");
	for (const auto hand : {L"RHP", L"LHP"})
	{
		const auto knuckleballer = LoadBundledPitch(sourceRoot, std::wstring{L"PitchSettings.Knuckleball."} + hand + L".json", error);
		Tests::Check(knuckleballer && knuckleballer->pitchProfiles.size() == 1 &&
		        knuckleballer->pitchProfiles.front().name == L"ナックル" &&
		        knuckleballer->pitchProfiles.front().pitchParameters.knuckleball &&
		        knuckleballer->pitchProfiles.front().pitchParameters.spinRpm < 100.0 &&
		        std::abs(knuckleballer->pitchProfiles.front().pitchParameters.initialVelocity.z * 3.6 - 120.0) < .5,
		    "The knuckleballer throws only a 120 km/h knuckleball with almost no spin");
	}
	for (const auto& entry : entries)
	{
		const auto settings = catalog.LoadPitchSettings(entry, error);
		if (!settings) std::wcerr << entry.id << L": " << error << L'\n';
		Tests::Check(settings.has_value(), "All bundled pitch settings load with production loader");
		const auto environmentPath = catalog.ResolveEnvironmentReference(settings->environmentSettingsFile, entry.sourcePath);
		Tests::Check(runtimeStorage.LoadEnvironment(environmentPath.wstring(), error).has_value(),
		             "Referenced environment file loads");
	}
	for (const auto& directory : {L"Environment", L"Play"})
	{
		for (const auto& entry : std::filesystem::directory_iterator(sourceRoot / L"Presets" / directory))
		{
			const auto name = entry.path().filename().wstring();
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
	}
	const auto yamamoto = LoadBundledPitch(sourceRoot, L"PitchSettings.Yamamoto.json", error);
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
