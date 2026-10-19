#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"

#include <array>
#include <limits>

void RunRuntimeSettingsStorageTests()
{
	using namespace BaseballSimulatorVR::Data;
	Tests::TempDirectory directory;
	const auto environmentPath = directory.path / "environment.json";
	const auto playPath = directory.path / "play.json";
	RuntimeSettingsJsonStorage storage;
	std::wstring error;
	EnvironmentRuntimeSettings environment{};
	environment.pitchDistanceMeters = 12.25;
	environment.windVelocity = { 3.25, -0.5, 7.125 };
	Tests::Check(storage.SaveEnvironment(environmentPath.wstring(), environment, error),
	    "Finite environment settings save successfully");
	const auto restoredEnvironment = storage.LoadEnvironment(environmentPath.wstring(), error);
	Tests::Check(restoredEnvironment && restoredEnvironment->pitchDistanceMeters == 12.25 &&
	    restoredEnvironment->windVelocity.x == 3.25 && restoredEnvironment->windVelocity.y == -0.5 &&
	    restoredEnvironment->windVelocity.z == 7.125,
	    "Finite environment values survive a save and load");
	const auto validEnvironment = Tests::ReadFile(environmentPath);
	const std::array environmentFields{
		&environment.ballProperties.radiusMeters,
		&environment.ballProperties.massKilograms,
		&environment.ballProperties.restitutionAtReferenceSpeed,
		&environment.pitchDistanceMeters,
		&environment.moundHeightOffsetMeters,
		&environment.windVelocity.x,
		&environment.windVelocity.y,
		&environment.windVelocity.z,
		&environment.windShearFactor,
		&environment.batRadiusMeters,
		&environment.batLengthMeters,
		&environment.batAngleOffsetDegrees,
		&environment.batYawOffsetDegrees,
		&environment.groundFanRadiusMeters
	};
	const std::array invalidValues{
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity()
	};
	for (double* field : environmentFields)
	{
		const double original = *field;
		for (const double invalid : invalidValues)
		{
			*field = invalid;
			Tests::Check(!storage.SaveEnvironment(environmentPath.wstring(), environment, error) &&
			    !error.empty(), "Environment save rejects every nonfinite numeric field");
			Tests::Check(Tests::ReadFile(environmentPath) == validEnvironment,
			    "Invalid environment values leave the existing file intact");
		}
		*field = original;
	}

	PlayRuntimeSettings play{};
	play.duelPitchIntervalSeconds = 6.5;
	play.playbackSpeedPercent = 125.0;
	Tests::Check(storage.SavePlay(playPath.wstring(), play, error),
	    "Finite play settings save successfully");
	const auto restoredPlay = storage.LoadPlay(playPath.wstring(), error);
	Tests::Check(restoredPlay && restoredPlay->duelPitchIntervalSeconds == 6.5 &&
	    restoredPlay->playbackSpeedPercent == 125.0,
	    "Finite playback speed and pitch interval survive a save and load");
	const auto validPlay = Tests::ReadFile(playPath);
	const std::array playFields{
		&play.strikeZoneBottomMeters,
		&play.strikeZoneHeightMeters,
		&play.strikeZoneWidthMeters,
		&play.duelPitchIntervalSeconds,
		&play.playbackSpeedPercent
	};
	for (double* field : playFields)
	{
		const double original = *field;
		for (const double invalid : invalidValues)
		{
			*field = invalid;
			Tests::Check(!storage.SavePlay(playPath.wstring(), play, error) && !error.empty(),
			    "Play save rejects every nonfinite numeric field");
			Tests::Check(Tests::ReadFile(playPath) == validPlay,
			    "Invalid play values leave the existing file intact");
		}
		*field = original;
	}
}
