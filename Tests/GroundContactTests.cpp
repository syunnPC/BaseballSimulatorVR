#include "../BaseballSimulatorVR/Common/FieldMarkings.hpp"
#include "../BaseballSimulatorVR/Physics/GroundContact.hpp"
#include "TestSupport.hpp"

#include <cmath>
#include <numbers>

namespace
{
	using namespace BaseballSimulatorVR;

	// A ball arriving at the ground with the given velocity and backspin (positive) or
	// topspin (negative) about the axis across its horizontal path (+x travel).
	Physics::BallState Landing(const double forward, const double down, const double backspinRpm)
	{
		Physics::BallState ball;
		ball.velocity = {forward, -down, 0.0};
		// Travelling +x, backspin turns about +z (the bottom moves forward).
		ball.spinAxis = {0.0, 0.0, backspinRpm >= 0.0 ? 1.0 : -1.0};
		ball.spinRpm = std::abs(backspinRpm);
		return ball;
	}

	void CheckBounce()
	{
		Tests::Check(Physics::GroundRestitution(Physics::GrassSurface, 1.0) >
		            Physics::GroundRestitution(Physics::GrassSurface, 20.0) &&
		        Physics::GroundRestitution(Physics::GrassSurface, 100.0) == Physics::GrassSurface.minimumRestitution &&
		        Physics::GroundRestitution(Physics::GrassSurface, 0.0) == Physics::GrassSurface.restitutionAtRest,
		    "A ball rebounds less the harder it lands, within the surface's limits");

		auto drop = Landing(0.0, 5.0, 0.0);
		Physics::BounceOffGround(drop, Physics::DirtSurface);
		Tests::Check(Tests::Near(drop.velocity.y, 5.0 * Physics::GroundRestitution(Physics::DirtSurface, 5.0), 1e-12) &&
		        drop.velocity.x == 0.0 && drop.spinRpm == 0.0,
		    "A ball dropped without spin bounces straight back up");

		auto skid = Landing(20.0, 6.0, 0.0);
		Physics::BounceOffGround(skid, Physics::GrassSurface);
		Tests::Check(skid.velocity.x < 20.0 && skid.velocity.x > 10.0 && skid.spinAxis.z < 0.0,
		    "Skidding on the bounce takes pace off a ball and starts it turning over (topspin)");
		auto plain = Landing(8.0, 6.0, 0.0);
		auto backspin = Landing(8.0, 6.0, 2500.0);
		auto topspin = Landing(8.0, 6.0, -2500.0);
		for (auto* ball : {&plain, &backspin, &topspin}) Physics::BounceOffGround(*ball, Physics::GrassSurface);
		Tests::Check(backspin.velocity.x < plain.velocity.x && topspin.velocity.x > 8.0,
		    "Backspin checks a ball up on the bounce and topspin kicks it on");
	}

	void CheckRolling()
	{
		for (const auto* surface : {&Physics::GrassSurface, &Physics::DirtSurface})
		{
			auto ball = Landing(15.0, 0.0, 0.0);
			ball.velocity.y = 0.0;
			double distance = 0.0, seconds = 0.0;
			bool gripped = false;
			while (std::hypot(ball.velocity.x, ball.velocity.z) > 1e-6 && seconds < 60.0)
			{
				Physics::RollOnGround(ball, *surface, .01);
				distance += ball.velocity.x * .01;
				seconds += .01;
				const double slip = ball.velocity.x + ball.properties.radiusMeters * (ball.spinAxis.z * ball.spinRpm * 2.0 *
				    std::numbers::pi / 60.0);
				gripped = gripped || std::abs(slip) < Physics::RollingSpeedThresholdMetersPerSecond;
			}
			Tests::Check(gripped && seconds < 30.0 && distance > 10.0 && distance < 120.0,
			    "A skidding ball grips the ground, rolls and comes to rest");
		}
		const auto rollOut = [](const Physics::GroundSurface& surface)
		{
			auto ball = Landing(12.0, 0.0, 0.0);
			ball.velocity.y = 0.0;
			double distance = 0.0;
			for (int step = 0; step < 6000 && ball.velocity.x > 1e-6; ++step)
			{
				Physics::RollOnGround(ball, surface, .01);
				distance += ball.velocity.x * .01;
			}
			return distance;
		};
		Tests::Check(rollOut(Physics::GrassSurface) < rollOut(Physics::DirtSurface),
		    "Grass slows a rolling ball more than the infield dirt");
	}

	void CheckSurfaceMap()
	{
		using Common::FieldSurface;
		constexpr double Feet = .3048;
		const auto at = [](const double distanceFeet, const double bearingDegrees)
		{
			const double r = distanceFeet * Feet, a = bearingDegrees * std::numbers::pi / 180.0;
			return Common::FieldSurfaceAt(r * std::sin(a), r * std::cos(a));
		};
		Tests::Check(at(5.0, 120.0) == FieldSurface::Dirt && at(145.0, -20.0) == FieldSurface::Dirt &&
		        at(45.0, 44.5) == FieldSurface::Dirt && at(390.0, 0.0) == FieldSurface::Dirt,
		    "The plate circle, the infield skin, the base paths and the warning track are dirt");
		Tests::Check(at(80.0, 0.0) == FieldSurface::Grass && at(250.0, 10.0) == FieldSurface::Grass &&
		        at(60.0, 70.0) == FieldSurface::Grass,
		    "The infield grass, the outfield and foul territory are grass");
	}
}

void RunGroundContactTests()
{
	CheckBounce();
	CheckRolling();
	CheckSurfaceMap();
}
