#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/PitchPresetJsonStorage.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchSimulationService.hpp"
#include "../BaseballSimulatorVR/Physics/BattedBallAerodynamics.hpp"
#include "../BaseballSimulatorVR/Physics/BatBallCollision.hpp"

#include <array>
#include <cmath>
#include <fstream>

namespace
{
	using namespace BaseballSimulatorVR;
	bool Near(double a, double b) { return Tests::Near(a, b, 1e-10, 0.0); }
}

void RunTossPhysicsTests(const std::filesystem::path& sourceRoot)
{
	Data::PitchPresetJsonStorage pitchStorage;
	Data::RuntimeSettingsJsonStorage environmentStorage;
	std::wstring error;
	std::array<Physics::BallProperties, 2> properties{};
	for (std::size_t index = 0; index < properties.size(); ++index)
	{
		const std::wstring kind = index == 0 ? L"Baseball" : L"Softball";
		auto preset = pitchStorage.LoadPitchSettings((sourceRoot / (L"PitchSettings.Toss." + kind + L".json")).wstring(), error);
		Tests::Check(preset.has_value(), "Toss preset loads with the production parser");
		auto environment = environmentStorage.LoadEnvironment((sourceRoot / preset->environmentSettingsFile).wstring(), error);
		Tests::Check(environment && environment->pitchDistanceMeters == 9.0 &&
			environment->moundHeightOffsetMeters == 0.0 && !environment->enableMoundHeightOffset,
			"Toss environment uses a nine-meter distance and zero mound offset");
		properties[index] = environment->ballProperties;
		Tests::Check(Near(properties[index].radiusMeters, index == 0 ? .037 : .0485) &&
			Near(properties[index].massKilograms, index == 0 ? .145 : .190),
			"Toss balls use the configured diameter and mass");
		Tests::Check(preset->pitchProfiles.size() == 1 && !preset->randomPitchProfileEnabled,
			"Each toss preset selects its single ball profile without random pitch selection");
		for (auto parameters : {preset->pitchParameters, preset->pitchProfiles.front().pitchParameters})
		{
			Tests::Check(parameters.underhandToss && parameters.usePhysicalAerodynamics &&
				Near(parameters.initialVelocity.z, 8.0) && parameters.initialVelocity.y > 5.0 &&
				Near(parameters.launchElevationDegrees, 35.0) && Near(parameters.releaseHeightMeters, 1.0),
				"Toss launches upward from a low release with physical aerodynamics");
			parameters.ballProperties = properties[index];
			Simulation::PitchSimulationService simulation;
			simulation.Reset(parameters);
			auto current = simulation.GetBallState();
			Tests::Check(current.aerodynamicsModel == Physics::BallAerodynamicsModel::Physical &&
				Near(current.properties.radiusMeters, properties[index].radiusMeters) &&
				Near(current.properties.massKilograms, properties[index].massKilograms),
				"Reset transfers physical ball dimensions and mass into flight state");
			double apex = current.position.y;
			bool reached = false;
			for (int step = 0; step < 2000; ++step)
			{
				const auto previous = current;
				simulation.Tick(std::chrono::duration<double>(.001));
				current = simulation.GetBallState();
				apex = (std::max)(apex, current.position.y);
				if (current.position.z >= 9.0)
				{
					const double blend = (9.0 - previous.position.z) / (current.position.z - previous.position.z);
					const double height = previous.position.y + (current.position.y - previous.position.y) * blend;
					Tests::Check(apex > 2.3 && apex < 2.8 && height > .4 && height < 1.5 &&
						current.velocity.y < 0 && current.flightTimeSeconds > 1.05 && current.flightTimeSeconds < 1.4,
						"The production toss rises in an arc and descends into hitting height at nine meters");
					reached = true;
					break;
				}
			}
			Tests::Check(reached, "Both toss balls reach the plate within two seconds");
		}

		Tests::TempDirectory output;
		const auto pitchPath = output.path / "pitch.json";
		const auto environmentPath = output.path / "environment.json";
		Tests::Check(pitchStorage.SavePitchSettings(pitchPath.wstring(), *preset, error) &&
			environmentStorage.SaveEnvironment(environmentPath.wstring(), *environment, error),
			"Toss settings save through production storage");
		const auto restoredPitch = pitchStorage.LoadPitchSettings(pitchPath.wstring(), error);
		const auto restoredEnvironment = environmentStorage.LoadEnvironment(environmentPath.wstring(), error);
		Tests::Check(restoredPitch && restoredEnvironment && restoredPitch->pitchParameters.underhandToss &&
			restoredPitch->pitchParameters.usePhysicalAerodynamics &&
			Near(restoredPitch->pitchParameters.initialVelocity.y, preset->pitchParameters.initialVelocity.y) &&
			Near(restoredEnvironment->ballProperties.radiusMeters, environment->ballProperties.radiusMeters) &&
			Near(restoredEnvironment->ballProperties.massKilograms, environment->ballProperties.massKilograms),
			"Toss angle, delivery, aerodynamics, radius and mass survive a save/load cycle");
		{
			std::ofstream invalid(environmentPath);
			invalid << "{\"pitchDistanceMeters\":9,\"enableMoundHeightOffset\":false,\"moundHeightOffsetMeters\":0,\"ballMassKilograms\":0,\"ballRestitutionAtReferenceSpeed\":0.45}";
		}
		Tests::Check(!environmentStorage.LoadEnvironment(environmentPath.wstring(), error),
			"An invalid ball mass is rejected instead of replacing it with another ball");
	}

	const auto baseballDrag = Physics::ComputeBattedBallAerodynamicAcceleration({0, 0, 20}, {1, 0, 0}, 0, properties[0]);
	const auto softballDrag = Physics::ComputeBattedBallAerodynamicAcceleration({0, 0, 20}, {1, 0, 0}, 0, properties[1]);
	const double dragRatio = properties[1].radiusMeters * properties[1].radiusMeters / properties[1].massKilograms /
		(properties[0].radiusMeters * properties[0].radiusMeters / properties[0].massKilograms);
	Tests::Check(Near(softballDrag.z / baseballDrag.z, dragRatio) && dragRatio > 1,
		"Physical drag scales with the actual cross-sectional area divided by ball mass");
	std::array<double, 2> exits{};
	for (std::size_t i = 0; i < properties.size(); ++i)
	{
		Physics::BatBallCollisionInput input;
		input.ballProperties = properties[i];
		input.ballVelocityWorld = {0, 0, 40};
		input.batContactVelocityWorld = {0, 0, -30};
		input.collisionNormal = {0, 0, -1};
		input.batAxisDirectionWorld = {1, 0, 0};
		input.batSurfaceOffsetWorld = input.collisionNormal * input.batRadiusMeters;
		input.contactDistanceFromHandleMeters = .564;
		const auto impact = Physics::ResolveBatBallCollision(input);
		const double impulse = (1 + impact.restitution) * 70 / (1 / properties[i].massKilograms + 1 / .885);
		Tests::Check(impact.hasImpulse && Near(impact.normalImpulseNewtonSeconds, impulse) &&
			Near(impact.ballVelocityWorld.z, 40 - impulse / properties[i].massKilograms),
			"Normal impact uses the configured ball mass in momentum transfer");
		exits[i] = -impact.ballVelocityWorld.z;
	}
	Tests::Check(exits[1] < exits[0], "The heavier softball receives a lower exit speed for the same normal impact");
}
