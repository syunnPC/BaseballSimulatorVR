#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"

#include <cmath>
#include <numbers>

namespace
{
	using namespace BaseballSimulatorVR;

	bool Near(const Physics::Vector3& a, const Physics::Vector3& b, double tolerance = 1e-10)
	{
		return Physics::Length(a - b) <= tolerance;
	}

	Physics::Vector3 InitialAcceleration(const Physics::BallState& state,
	    Baseball::PitchParameters parameters)
	{
		// Euler measures the force at the starting state without gravity or intermediate stages.
		Physics::PitchPhysicsEngine engine({0, 0, 0});
		parameters.integratorType = Physics::PitchIntegratorType::SemiImplicitEuler;
		constexpr double stepSeconds = .01;
		const auto next = engine.Step(state, parameters, std::chrono::duration<double>(stepSeconds));
		return (next.velocity - state.velocity) / stepSeconds;
	}

	void CheckTransverseSpinAndWind()
	{
		Physics::BallState ball;
		ball.velocity = {0, 0, 40};
		ball.spinAxis = {-1, 0, 0};
		ball.spinRpm = 2400;
		Baseball::PitchParameters parameters;
		parameters.useCoefficientTable = false;
		parameters.dragCoefficient = .008;
		parameters.liftCoefficient = .15;
		const auto fullSpin = InitialAcceleration(ball, parameters);
		Tests::Check(Near(fullSpin, {0, 4.8, -12.8}),
		    "Fully transverse profile spin retains its configured lift and quadratic drag");

		for (const bool useTable : {false, true})
		{
			parameters.useCoefficientTable = useTable;
			const auto reference = InitialAcceleration(ball, parameters);
			for (const double transverseFraction : {1., .5, .1, .001, 1e-8, 0.})
			{
				auto tilted = ball;
				tilted.spinAxis = {-transverseFraction, 0,
				    std::sqrt(1. - transverseFraction * transverseFraction)};
				const auto acceleration = InitialAcceleration(tilted, parameters);
				Tests::Check(std::abs(acceleration.y - reference.y * transverseFraction) < 1e-11 &&
				    std::abs(acceleration.x) < 1e-11 && std::abs(acceleration.z - reference.z) < 1e-11,
				    "Profile Magnus lift approaches zero continuously with transverse spin while drag is retained");
			}

			auto reversed = ball;
			reversed.spinAxis = -ball.spinAxis;
			const auto mirrored = InitialAcceleration(reversed, parameters);
			Tests::Check(Near(mirrored, {reference.x, -reference.y, reference.z}),
			    "Reversing the spin axis reverses Magnus lift without reversing drag");
			auto stoppedSpin = ball;
			stoppedSpin.spinRpm = 0;
			const auto noSpin = InitialAcceleration(stoppedSpin, parameters);
			Tests::Check(noSpin.x == 0 && noSpin.y == 0 && noSpin.z < 0,
			    "A nonspinning pitch retains drag but has no Magnus lift");

			auto movingAir = parameters;
			movingAir.windVelocity = {3, -2, 5};
			auto movingBall = ball;
			movingBall.velocity += movingAir.windVelocity;
			Tests::Check(Near(InitialAcceleration(movingBall, movingAir), reference),
			    "Uniform shifts of ball and wind velocity preserve the profile aerodynamic force");
			movingAir.windVelocity = ball.velocity;
			Tests::Check(Near(InitialAcceleration(ball, movingAir), {}),
			    "A pitch moving with the air has no aerodynamic force");
		}

		parameters.useCoefficientTable = false;
		parameters.windVelocity = {10, 0, 0};
		ball.spinAxis = {0, 0, 1};
		const auto crosswind = InitialAcceleration(ball, parameters);
		Tests::Check(std::abs(crosswind.y + 1.2) < 1e-11,
		    "Magnus lift uses spin transverse to relative airflow, including crosswind on a gyro pitch");
	}

	void CheckSeamHeuristicContinuity()
	{
		// The existing seam waveform is a heuristic, not a measured seam-force model.
		// Check its continuity without changing its amplitude for fully transverse spin.
		Physics::BallState ball;
		ball.velocity = {0, 0, 40};
		ball.spinAxis = {-1, 0, 0};
		ball.spinRpm = 2400;
		Baseball::PitchParameters parameters;
		parameters.dragCoefficient = .008;
		parameters.liftCoefficient = .15;
		parameters.enableSeamPerturbation = true;
		parameters.seamPerturbationAmplitude = .1;
		parameters.seamPerturbationFrequencyHz = 0;
		parameters.seamPerturbationPhase = std::numbers::pi / 2.;
		for (const bool useTable : {false, true})
		{
			parameters.useCoefficientTable = useTable;
			const auto reference = InitialAcceleration(ball, parameters);
			Tests::Check(reference.x < 0,
			    "The configured seam waveform still produces a side perturbation at full transverse spin");
			if (!useTable)
				Tests::Check(std::abs(reference.x + .15 * 1.04 * 40. * .28 * .1) < 1e-11,
				    "Fully transverse seam perturbation retains its existing configured amplitude");
			for (const double fraction : {.1, .001, 1e-8, 0., -1e-8, -.001, -.1})
			{
				auto tilted = ball;
				tilted.spinAxis = {-fraction, 0, std::sqrt(1. - fraction * fraction)};
				const auto acceleration = InitialAcceleration(tilted, parameters);
				Tests::Check(std::abs(acceleration.x - reference.x * fraction) < 1e-11 &&
				    std::abs(acceleration.y - reference.y * fraction) < 1e-11,
				    "The heuristic seam side force and Magnus force pass continuously through gyro alignment");
				Tests::Check(std::abs(acceleration.z - reference.z) < 1e-10,
				    "Removing the transverse projection singularity leaves seam drag modulation unchanged");
			}
		}
	}

	void CheckIntegratedPitchBreak()
	{
		Physics::PitchPhysicsEngine engine;
		Baseball::PitchParameters parameters;
		parameters.useCoefficientTable = false;
		parameters.dragCoefficient = .0066;
		parameters.liftCoefficient = .15;
		Physics::BallState transverse;
		transverse.velocity = {0, 0, 40};
		transverse.spinAxis = {0, 1, 0};
		transverse.spinRpm = 2400;
		auto mostlyGyro = transverse;
		mostlyGyro.spinAxis = {0, .1, std::sqrt(.99)};
		for (int step = 0; step < 400; ++step)
		{
			transverse = engine.Step(transverse, parameters, std::chrono::duration<double>(.001));
			mostlyGyro = engine.Step(mostlyGyro, parameters, std::chrono::duration<double>(.001));
		}
		Tests::Check(transverse.position.x > .1 && mostlyGyro.position.x > 0 &&
		    mostlyGyro.position.x < .25 * transverse.position.x,
		    "Mostly gyro spin produces substantially less lateral break during integrated pitched flight");
		Tests::Check(transverse.position.y > 0 && mostlyGyro.position.y > 0 &&
		    std::isfinite(mostlyGyro.position.z),
		    "Both representative pitches remain finite and airborne during the trajectory comparison");
	}
}

void RunPitchAerodynamicsTests()
{
	CheckTransverseSpinAndWind();
	CheckSeamHeuristicContinuity();
	CheckIntegratedPitchBreak();
}
