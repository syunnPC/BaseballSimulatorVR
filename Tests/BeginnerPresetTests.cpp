#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/PitchPresetJsonStorage.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"
#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"
#include "../BaseballSimulatorVR/Physics/SweptBatCollision.hpp"
#include "../BaseballSimulatorVR/Physics/BatBallCollision.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace
{
	using namespace BaseballSimulatorVR;
	struct Flight final
	{
		Physics::BallState crossing;
		Physics::BallState beforeContact;
		Physics::BallState afterContact;
	};

	Flight FlyToPlate(const Baseball::PitchParameters& parameters)
	{
		Physics::PitchPhysicsEngine engine;
		Physics::BallState previous;
		previous.position = {0.0, parameters.releaseHeightMeters, 0.0};
		previous.velocity = parameters.initialVelocity;
		previous.properties = parameters.ballProperties;
		previous.spinAxis = parameters.spinAxis;
		previous.spinRpm = parameters.spinRpm;
		previous.aerodynamicsModel = parameters.usePhysicalAerodynamics
		    ? Physics::BallAerodynamicsModel::Physical : Physics::BallAerodynamicsModel::PitchProfile;
		std::vector<Physics::BallState> history{previous};
		for (int step = 0; step < 2000; ++step)
		{
			auto current = engine.Step(previous, parameters, std::chrono::duration<double>(.001));
			Tests::Check(current.position.y > current.properties.radiusMeters,
			    "Beginner deliveries reach the plate without touching the ground");
			if (current.position.z >= 18.44)
			{
				const double blend = (18.44 - previous.position.z) /
				    (current.position.z - previous.position.z);
				auto crossing = current;
				crossing.position = previous.position + (current.position - previous.position) * blend;
				crossing.flightTimeSeconds = previous.flightTimeSeconds + .001 * blend;
				return {crossing, history[history.size() - 11], current};
			}
			history.push_back(current);
			previous = current;
		}
		throw std::runtime_error("Beginner delivery must reach the plate in two seconds");
	}

	void CheckHittable(const Flight& flight, const Baseball::PitchParameters& parameters)
	{
		const auto& centre = flight.crossing.position;
		const Physics::BatSegment bat{
		    centre + Physics::Vector3{-.70, 0, 0}, centre + Physics::Vector3{.14, 0, 0}};
		const auto contact = Physics::FindSweptBatProfileContact(flight.beforeContact.position,
		    flight.afterContact.position, bat, bat, .035, parameters.ballProperties.radiusMeters,
		    flight.afterContact.flightTimeSeconds - flight.beforeContact.flightTimeSeconds);
		Tests::Check(contact.has_value(),
		    "A real beginner trajectory is captured by the swept bat collider at the plate");
		Physics::BatBallCollisionInput input;
		input.ballVelocityWorld = flight.afterContact.velocity;
		input.ballProperties = parameters.ballProperties;
		input.ballAngularVelocityWorld = parameters.spinAxis *
		    (parameters.spinRpm * 2.0 * std::acos(-1.0) / 60.0);
		input.collisionNormal = contact->normal;
		input.batAxisDirectionWorld = contact->batAxis;
		input.batSurfaceOffsetWorld = contact->batSurfaceOffset;
		input.contactDistanceFromHandleMeters = contact->batFraction * .84;
		Tests::Check(Physics::ResolveBatBallCollision(input).hasImpulse,
		    "Beginner softball contact produces a physical bat-ball impulse");
	}
}

void RunBeginnerPresetTests(const std::filesystem::path& sourceRoot)
{
	using namespace BaseballSimulatorVR;
	Data::PitchPresetJsonStorage storage;
	Data::RuntimeSettingsJsonStorage runtimeStorage;
	std::wstring error;
	const auto environmentPath = sourceRoot / L"EnvironmentSettings.Beginner.Softball.json";
	const auto environment = runtimeStorage.LoadEnvironment(environmentPath.wstring(), error);
	Tests::Check(environment && environment->pitchDistanceMeters == 18.44 &&
	    environment->moundHeightOffsetMeters == .18 && !environment->enableMoundHeightOffset &&
	    environment->ballProperties.radiusMeters == .0485 &&
	    environment->ballProperties.massKilograms == .190 &&
	    environment->ballProperties.restitutionAtReferenceSpeed == .45,
	    "Beginner presets use the normal field and the requested larger softball");
	Tests::Check(Tests::ReadFile(environmentPath).find("strikeZone") == std::string::npos,
	    "Beginner environment does not replace the player's calibrated strike zone");
	constexpr std::array<std::wstring_view, 6> names{
	    L"ストレート", L"カーブ", L"スイーパー", L"スライダー", L"シンカー", L"フォーク"};
	constexpr std::array<double, 6> speeds{90, 75, 80, 85, 85, 80};
	constexpr std::array<double, 6> rpm{2400, 2600, 2500, 2400, 2200, 0};
	constexpr std::array<double, 6> horizontal{0, .20, .35, .20, -.30, 0};
	constexpr std::array<double, 6> vertical{.25, -.25, 0, -.05, -.10, 0};
	std::array<std::array<Physics::Vector3, 6>, 2> normalBreaks{};
	std::array<Physics::Vector3, 2> forkCrossings{};
	for (const bool big : {false, true})
	{
		std::array<Flight, 6> rightFlights{};
		for (std::size_t hand = 0; hand < 2; ++hand)
		{
			const std::wstring prefix = big ? L"PitchSettings.Beginner.BigBreak.Softball."
			    : L"PitchSettings.Beginner.Softball.";
			const auto settings = storage.LoadPitchSettings(
			    (sourceRoot / (prefix + (hand == 0 ? L"RHP.json" : L"LHP.json"))).wstring(), error);
			Tests::Check(settings && settings->pitchProfiles.size() == names.size() &&
			    settings->environmentSettingsFile == L"EnvironmentSettings.Beginner.Softball.json" &&
			    settings->pitcherThrowsLeftHanded == (hand == 1) &&
			    settings->activePitchProfileIndex == 0 && settings->randomPitchProfileEnabled,
			    "Both beginner variants load six randomly selectable pitches for either hand");
			Tests::Check(Physics::Length(settings->pitchParameters.initialVelocity -
			    settings->pitchProfiles.front().pitchParameters.initialVelocity) < 1e-12,
			    "Beginner root pitch agrees with its initially selected fastball");
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				auto parameters = settings->pitchProfiles[i].pitchParameters;
				parameters.ballProperties = environment->ballProperties;
				Tests::Check(settings->pitchProfiles[i].name == names[i] &&
				    std::abs(Physics::Length(parameters.initialVelocity) * 3.6 - speeds[i]) < 1e-9,
				    "Beginner pitch names and total release velocities match the requested repertoire");
				Tests::Check(parameters.spinRpm == rpm[i] * (big ? 1.5 : 1.0) &&
				    !parameters.usePhysicalAerodynamics && !parameters.underhandToss &&
				    !parameters.useCoefficientTable && !parameters.enableSeamPerturbation,
				    "Fitted beginner shapes use the specified spins without coefficient-table or seam overrides");
				const auto flight = FlyToPlate(parameters);
				Tests::Check(std::abs(flight.crossing.position.y - .72) < .00002 &&
				    flight.crossing.velocity.y < 0 && flight.crossing.flightTimeSeconds < 1.2 &&
				    Physics::Length(flight.crossing.velocity) > .65 * Physics::Length(parameters.initialVelocity),
				    "Untargeted beginner pitches cross a hittable height with realistic flight time and drag loss");
				auto noLift = parameters;
				noLift.liftCoefficient = 0;
				const auto baseline = FlyToPlate(noLift);
				const Physics::Vector3 induced{
				    flight.crossing.position.x, flight.crossing.position.y - baseline.crossing.position.y, 0};
				Tests::Check(std::abs(induced.x - horizontal[i] * (big ? 1.6 : 1.0) * (hand ? -1 : 1)) < .00002 &&
				    std::abs(induced.y - vertical[i] * (big ? 1.6 : 1.0)) < .00002,
				    "The production physics engine produces each configured horizontal and induced vertical break");
				if (!big) normalBreaks[hand][i] = induced;
				else if (i < 5)
					Tests::Check(Physics::Length(induced) > 1.5 * Physics::Length(normalBreaks[hand][i]),
					    "The larger-break variant increases actual movement rather than only its RPM label");
				if (hand == 0) rightFlights[i] = flight;
				else Tests::Check(std::abs(flight.crossing.position.x + rightFlights[i].crossing.position.x) < 1e-10 &&
				    std::abs(flight.crossing.position.y - rightFlights[i].crossing.position.y) < 1e-10 &&
				    std::abs(flight.crossing.flightTimeSeconds - rightFlights[i].crossing.flightTimeSeconds) < 1e-10,
				    "Handedness mirrors simulated horizontal movement while preserving height and timing");
				if (i == 5)
				{
					Tests::Check(parameters.spinRpm == 0 && parameters.liftCoefficient == 0 &&
					    Physics::Length(induced) < 1e-12,
					    "The fork loses backspin support through zero spin and receives no artificial sinking lift");
					if (!big) forkCrossings[hand] = flight.crossing.position;
					else Tests::Check(Physics::Length(flight.crossing.position - forkCrossings[hand]) < 1e-12,
					    "Both beginner variants retain the same spinless fork trajectory");
				}
				CheckHittable(flight, parameters);
			}
		}
	}
}
