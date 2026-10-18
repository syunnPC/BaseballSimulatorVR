#pragma once

#include "Vector3.hpp"

namespace BaseballSimulatorVR::Physics
{
	inline constexpr double BatBallReferenceImpactSpeedMetersPerSecond = 71.5264;
	inline constexpr double DefaultBatBallRestitution = 0.45;
	inline constexpr double BaseballMassKilograms = 0.145;
	inline constexpr double BaseballRadiusMeters = 0.037;
	inline constexpr double BaseballInertiaFactor = 0.4;
	// Kensrud, Nathan & Smith (2017), Table II: wooden bat / baseball.
	inline constexpr double WoodBatTangentialRestitution = 0.464;
	inline constexpr double BatBallFrictionCoefficient = 0.5;

	struct BatBallCollisionInput final
	{
		Vector3 ballVelocityWorld{};
		Vector3 ballAngularVelocityWorld{}; // radians / second, including incoming pitch spin
		// Velocity AT CONTACT before impact, including angular velocity cross lever arm.
		Vector3 batContactVelocityWorld{};
		// Points from bat to ball. Both directions are normalized by the solver.
		Vector3 collisionNormal{};
		Vector3 batAxisDirectionWorld{};
		double contactDistanceFromHandleMeters{ 0.0 };
		double batLengthMeters{ 0.84 };
		double batRadiusMeters{ 0.035 };
		// Normal COR at 160 mph closing speed; not apparent COR/collision efficiency.
		double restitutionAtReferenceSpeed{ DefaultBatBallRestitution };
	};

	struct BatBallCollisionResult final
	{
		bool hasImpulse{ false };
		Vector3 ballVelocityWorld{};
		Vector3 ballAngularVelocityWorld{};
		Vector3 tangentialImpulseWorld{};
		bool tangentialRestitutionLimitedByFriction{ false };
		bool tangentialRestitutionLimitedByEnergy{ false };
		double normalImpulseNewtonSeconds{ 0.0 };
		double restitution{ 0.0 };
		double effectiveBatMassKilograms{ 0.0 };
		double collisionEfficiency{ 0.0 };
	};

	// Free rigid wood bat model with measured inertia and speed-dependent COR.
	// Research references are listed in README.md.
	// Separating/tangential contacts or invalid input produce no impulse. Oblique
	// approaching impacts exchange tangential momentum and ball spin at the surface.
	[[nodiscard]] BatBallCollisionResult ResolveBatBallCollision(
	    const BatBallCollisionInput& input) noexcept;
} // namespace BaseballSimulatorVR::Physics
