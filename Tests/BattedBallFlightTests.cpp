#include "../BaseballSimulatorVR/Physics/BattedBallAerodynamics.hpp"
#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchSimulationService.hpp"
#include "TestSupport.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace
{
	using namespace BaseballSimulatorVR;

	bool Near(const Physics::Vector3& a, const Physics::Vector3& b, double tolerance = 1e-10)
	{
		return Physics::Length(a - b) <= tolerance;
	}

	struct FlightResult
	{
		double range{};
		double maximumHeight{};
		double time{};
	};

	FlightResult Fly(double speed, double angleDegrees, double spinRpm,
	    const Baseball::PitchParameters& parameters = {}, double stepSeconds = .001)
	{
		Physics::PitchPhysicsEngine engine;
		Physics::BallState state;
		state.aerodynamicsModel = Physics::BallAerodynamicsModel::BattedBall;
		state.position = {0, 1, 0};
		const double angle = angleDegrees * std::numbers::pi / 180.;
		state.velocity = {0, speed * std::sin(angle), -speed * std::cos(angle)};
		state.spinAxis = {1, 0, 0};
		state.spinRpm = spinRpm;
		FlightResult result;
		result.maximumHeight = state.position.y;
		for (int step = 0; step < 30000; ++step)
		{
			const auto previous = state;
			state = engine.Step(state, parameters, std::chrono::duration<double>(stepSeconds));
			result.maximumHeight = (std::max)(result.maximumHeight, state.position.y);
			if (state.position.y <= Physics::AerodynamicBaseballRadiusMeters)
			{
				const double blend =
				    (previous.position.y - Physics::AerodynamicBaseballRadiusMeters) /
				    (previous.position.y - state.position.y);
				const auto landing =
				    previous.position + (state.position - previous.position) * blend;
				result.range = std::hypot(landing.x, landing.z);
				result.time = previous.flightTimeSeconds + stepSeconds * blend;
				return result;
			}
		}
		throw std::runtime_error("Batted ball must land within the test horizon");
	}

	void CheckCoefficientsAndForce()
	{
		const Physics::Vector3 velocity{0, 0, -50};
		const auto coefficients =
		    Physics::ResolveBattedBallAerodynamicCoefficients(velocity, {1, 0, 0}, 2500);
		const double expectedSpinFactor = .037 * (2500. * 2. * std::numbers::pi / 60.) / 50.;
		Tests::Check(std::abs(coefficients.drag - .370) < 1e-14,
		    "Nathan 2017 dimensionless drag uses total spin in rpm/1000");
		Tests::Check(
		    std::abs(coefficients.transverseSpinFactor - expectedSpinFactor) < 1e-14 &&
		        std::abs(coefficients.lift -
		                 1.120 * expectedSpinFactor / (.583 + 2.333 * expectedSpinFactor)) < 1e-14,
		    "Lift uses radius times angular velocity in rad/s divided by air speed");
		const double scale = .5 * 1.225 * std::numbers::pi * .037 * .037 / .145;
		const auto acceleration =
		    Physics::ComputeBattedBallAerodynamicAcceleration(velocity, {1, 0, 0}, 2500);
		Tests::Check(Near(acceleration, {0, scale * coefficients.lift * 2500, scale * .370 * 2500}),
		    "Drag and Magnus acceleration include air density, area, and ball mass");
		Tests::Check(acceleration.z > 16 && acceleration.z < 17,
		    "A 50m/s batted ball has physical drag rather than the old roughly 1m/s2 profile drag");
		const auto noSpin =
		    Physics::ComputeBattedBallAerodynamicAcceleration(velocity, {1, 0, 0}, 0);
		Tests::Check(
		    noSpin.y == 0 && noSpin.z > 0, "Zero spin removes Magnus lift while retaining drag");
		const auto gyro =
		    Physics::ResolveBattedBallAerodynamicCoefficients(velocity, {0, 0, -1}, 2500);
		Tests::Check(
		    gyro.lift == 0 && gyro.transverseSpinFactor == 0 && Near(gyro.liftDirection, {}),
		    "Pure gyrospin generates no Magnus lift");
		const auto tilted = Physics::ResolveBattedBallAerodynamicCoefficients(
		    velocity, {.5, 0, std::sqrt(.75)}, 2500);
		Tests::Check(tilted.lift > 0 && tilted.lift < coefficients.lift &&
		                 std::abs(tilted.transverseSpinFactor - expectedSpinFactor * .5) < 1e-14,
		    "Partial gyrospin reduces lift continuously through transverse angular speed");
		const auto reverse =
		    Physics::ComputeBattedBallAerodynamicAcceleration(velocity, {1, 0, 0}, -2500);
		Tests::Check(reverse.y == -acceleration.y && reverse.z == acceleration.z,
		    "Reversing spin reverses lift without changing drag");
		const auto scaledAxis =
		    Physics::ComputeBattedBallAerodynamicAcceleration(velocity, {7, 0, 0}, 2500);
		Tests::Check(
		    Near(acceleration, scaledAxis), "Spin axis magnitude does not change spin rate");
		Tests::Check(
		    Near(Physics::ComputeBattedBallAerodynamicAcceleration({}, {1, 0, 0}, 2500), {}),
		    "A ball stationary relative to air has no aerodynamic force");
		Tests::Check(Physics::Dot(acceleration, velocity) < 0 && coefficients.liftDirection.z == 0,
		    "Aerodynamics dissipates translation energy while Magnus acts perpendicular to air "
		    "velocity");
	}

	void CheckFlightReferences()
	{
		// Independent RK4 reference implementation of Nathan 2017 Eqs. 7-11,
		// R=.037m, m=.145kg, rho=1.225kg/m3, contact center height1m, ground center .037m.
		// Reference distances are numerical integrations of the equations.
		constexpr std::array<std::array<double, 4>, 3> expected{
		    {{119.624, 123.357, 123.654, 121.000}, {136.877, 139.612, 138.778, 134.863},
		        {152.887, 154.561, 152.586, 147.431}}};
		for (std::size_t speed = 0; speed < expected.size(); ++speed)
		{
			for (std::size_t angle = 0; angle < expected[speed].size(); ++angle)
			{
				const auto result = Fly(45. + speed * 5., 20. + angle * 5., 2500);
				Tests::Check(std::abs(result.range - expected[speed][angle]) < .002,
				    "Production batted flight reproduces independently integrated physical "
				    "reference distances");
				Tests::Check(result.range < 160 && result.maximumHeight > 10 && result.time > 3,
				    "Representative hard-hit fly balls have natural distance, height, and airtime");
			}
		}
		// Nathan's 2015 Statcast study reports 401ft mean excluding Denver for
		// 101-105mph and 25-30deg. Spin, weather and the fitted ball era differ.
		// Check the measured scale with a declared 10m model tolerance, not a per-hit fit.
		const auto representative = Fly(103. * .44704, 27., 2000.);
		Tests::Check(std::abs(representative.range - 401. * .3048) < 10.,
		    "A typical Statcast hard-hit fly ball is near the observed roughly 122m mean, not "
		    "200m");
		const auto spin1500 = Fly(50, 30, 1500);
		const auto spin4000 = Fly(50, 30, 4000);
		Tests::Check(
		    spin1500.range > spin4000.range && spin4000.maximumHeight > spin1500.maximumHeight,
		    "More backspin can increase height while spin-dependent drag reduces carry");
		const auto fine = Fly(50, 30, 2500, {}, .0005);
		const auto coarse = Fly(50, 30, 2500, {}, .004);
		Tests::Check(
		    std::abs(fine.range - coarse.range) < .001 && std::abs(fine.time - coarse.time) < 1e-5,
		    "RK4 range is converged across practical fixed integration steps");
		Baseball::PitchParameters euler;
		euler.integratorType = Physics::PitchIntegratorType::SemiImplicitEuler;
		Tests::Check(std::abs(Fly(50, 30, 2500, euler).range - fine.range) < .10 &&
		                 std::abs(Fly(50, 30, 2500, euler, .0005).range - fine.range) < .05,
		    "Semi-implicit Euler remains close at the production 1ms step");
		Baseball::PitchParameters tailwind;
		tailwind.windVelocity = {0, 0, -5};
		auto headwind = tailwind;
		headwind.windVelocity.z = 5;
		Tests::Check(Fly(50, 30, 2500, tailwind).range > fine.range &&
		                 Fly(50, 30, 2500, headwind).range < fine.range,
		    "Inherited environmental wind changes carry with the physical relative air speed");
	}

	void CheckIntegrationAndStateAuthority()
	{
		Physics::PitchPhysicsEngine weightless({0, 0, 0});
		Baseball::PitchParameters parameters;
		Physics::BallState state;
		state.position = {};
		state.velocity = {0, 0, 40};
		state.spinRpm = 0;
		state.aerodynamicsModel = Physics::BallAerodynamicsModel::BattedBall;
		for (int n = 0; n < 500; ++n)
		{
			state = weightless.Step(state, parameters, std::chrono::duration<double>(.001));
		}
		const double drag = .5 * 1.225 * std::numbers::pi * .037 * .037 / .145 * .297;
		Tests::Check(std::abs(state.velocity.z - 40. / (1. + drag * 40. * .5)) < 1e-10 &&
		                 std::abs(state.position.z - std::log(1. + drag * 40. * .5) / drag) < 1e-10,
		    "Zero-spin RK4 matches the analytic quadratic-drag solution");

		Physics::PitchPhysicsEngine engine;
		Physics::BallState batted;
		batted.velocity = {0, 20, -40};
		batted.spinAxis = {1, 0, 0};
		batted.spinRpm = 2500;
		batted.aerodynamicsModel = Physics::BallAerodynamicsModel::BattedBall;
		const auto nominal = engine.Step(batted, parameters, std::chrono::duration<double>(.01));
		auto unrelatedProfile = parameters;
		unrelatedProfile.spinAxis = {0, 0, 1};
		unrelatedProfile.spinRpm = 9999;
		unrelatedProfile.useCoefficientTable = false;
		unrelatedProfile.dragCoefficient = 100;
		unrelatedProfile.liftCoefficient = 100;
		unrelatedProfile.enableSeamPerturbation = true;
		unrelatedProfile.seamPerturbationAmplitude = .5;
		const auto ignoredProfile =
		    engine.Step(batted, unrelatedProfile, std::chrono::duration<double>(.01));
		Tests::Check(Near(nominal.position, ignoredProfile.position) &&
		                 Near(nominal.velocity, ignoredProfile.velocity),
		    "A batted ball cannot inherit pitch-specific drag, lift, table, seam, or spin "
		    "configuration");
		Tests::Check(nominal.aerodynamicsModel == Physics::BallAerodynamicsModel::BattedBall &&
		                 nominal.spinRpm == batted.spinRpm &&
		                 Near(nominal.spinAxis, batted.spinAxis),
		    "Integrator preserves the selected aerodynamic model and physical spin state");

		auto movingAir = parameters;
		movingAir.windVelocity = {3, 0, -5};
		auto movingBall = batted;
		movingBall.velocity += movingAir.windVelocity;
		const auto movingResult =
		    engine.Step(movingBall, movingAir, std::chrono::duration<double>(.01));
		Tests::Check(
		    Near(movingResult.velocity - movingAir.windVelocity, nominal.velocity) &&
		        Near(movingResult.position - movingAir.windVelocity * .01, nominal.position),
		    "Uniform air and ball velocity shifts preserve aerodynamic acceleration");
		auto shear = movingAir;
		shear.enableHeightDependentWind = true;
		shear.windShearFactor = .2;
		movingBall.position.y = 10.;
		auto equivalentWind = shear;
		equivalentWind.enableHeightDependentWind = false;
		equivalentWind.windVelocity.x += .2 * 9.;
		const auto shortStep = std::chrono::duration<double>(1e-8);
		const auto shearResult = engine.Step(movingBall, shear, shortStep);
		const auto constantResult = engine.Step(movingBall, equivalentWind, shortStep);
		Tests::Check(Near(shearResult.velocity, constantResult.velocity, 1e-12),
		    "Height-dependent environmental wind also applies to batted flight");

		Physics::BallState pitched;
		pitched.velocity = {0, 0, 40};
		pitched.spinAxis = {-1, 0, 0};
		pitched.spinRpm = 2200;
		parameters.useCoefficientTable = false;
		parameters.dragCoefficient = .00045;
		parameters.liftCoefficient = .15;
		parameters.spinAxis = {0, 1, 0};
		parameters.spinRpm = 0;
		const auto pitchedResult = engine.Step(pitched, parameters, shortStep);
		const double lift = .15 * 40. * (2200. / 3000.);
		Tests::Check(std::abs((pitchedResult.velocity.y - pitched.velocity.y) / shortStep.count() -
		                      (-9.80665 + lift)) < 1e-6,
		    "Pitch-profile formula is unchanged but reads the current ball spin");
		pitched.spinRpm = 0;
		const auto noSpin = engine.Step(pitched, parameters, shortStep);
		Tests::Check(std::abs(noSpin.velocity.y / shortStep.count() + 9.80665) < 1e-6,
		    "Changing current pitch spin to zero removes lift independently of its saved profile");
	}

	void CheckFrozenPitchConfiguration()
	{
		Simulation::PitchSimulationService simulation;
		Baseball::PitchParameters delivered;
		delivered.initialVelocity = {1, -2, 42};
		delivered.releaseHeightMeters = 1.8;
		delivered.spinAxis = {-1, 0, 0};
		delivered.spinRpm = 2200;
		delivered.liftCoefficient = .15;
		simulation.Reset(delivered);
		auto contact = simulation.GetBallState();
		contact.position = {0, 1, 18.44};
		contact.velocity = {0, 25, -43.301270189};
		contact.spinAxis = {1, 0, 0};
		contact.spinRpm = 2500;
		contact.aerodynamicsModel = Physics::BallAerodynamicsModel::BattedBall;
		simulation.SetBallState(contact);
		const auto& saved = simulation.GetPitchParameters();
		Tests::Check(Near(saved.initialVelocity, delivered.initialVelocity) &&
		                 saved.releaseHeightMeters == delivered.releaseHeightMeters &&
		                 Near(saved.spinAxis, delivered.spinAxis) &&
		                 saved.spinRpm == delivered.spinRpm &&
		                 saved.liftCoefficient == delivered.liftCoefficient,
		    "Setting a contacted ball cannot rewrite the delivered pitch configuration");
		simulation.Tick(std::chrono::duration<double>(.001));
		Tests::Check(simulation.GetBallState().velocity.z > contact.velocity.z &&
		                 simulation.GetBallState().aerodynamicsModel ==
		                     Physics::BallAerodynamicsModel::BattedBall,
		    "Service advances the contacted ball with its selected physical mode");
		simulation.Reset(delivered);
		Tests::Check(simulation.GetBallState().aerodynamicsModel ==
		                     Physics::BallAerodynamicsModel::PitchProfile &&
		                 Near(simulation.GetBallState().velocity, delivered.initialVelocity) &&
		                 simulation.GetBallState().spinRpm == delivered.spinRpm,
		    "The next delivery restores pitch-profile aerodynamics and the original pitch spin");
	}
} // namespace

void RunBattedBallFlightTests()
{
	CheckCoefficientsAndForce();
	CheckFlightReferences();
	CheckIntegrationAndStateAuthority();
	CheckFrozenPitchConfiguration();
}
