#pragma once

#include "BallState.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace BaseballSimulatorVR::Physics
{
	// How a playing surface returns a ball. A ball rebounds less the harder it lands
	// (normal restitution falls with impact speed), slips against the surface under kinetic
	// friction until its bottom stops sliding, then rolls against rolling resistance and the
	// drag of grass blades.
	struct GroundSurface final
	{
		double restitutionAtRest{};
		double restitutionLossPerMeterPerSecond{};
		double minimumRestitution{};
		double friction{};
		double rollingDecelerationMetersPerSecond2{};
		double rollingDragPerSecond{};
	};

	// Cut grass is softer and grippier than the infield dirt, and slows a rolling ball more.
	inline constexpr GroundSurface GrassSurface{.55, .012, .32, .50, 1.6, .25};
	inline constexpr GroundSurface DirtSurface{.50, .012, .30, .42, 1.0, .10};

	// A baseball's moment of inertia is about 0.4 m r^2 (a nearly uniform sphere).
	inline constexpr double BallInertiaRatio = .40;
	inline constexpr double RollingSpeedThresholdMetersPerSecond = .35;

	[[nodiscard]] inline double GroundRestitution(const GroundSurface& surface, const double impactSpeed) noexcept
	{
		return std::clamp(surface.restitutionAtRest - surface.restitutionLossPerMeterPerSecond * impactSpeed,
		    surface.minimumRestitution, surface.restitutionAtRest);
	}

	namespace Detail
	{
		[[nodiscard]] inline Vector3 AngularVelocity(const BallState& ball) noexcept
		{
			return NormalizeOrZero(ball.spinAxis) * (ball.spinRpm * 2.0 * std::numbers::pi / 60.0);
		}

		inline void SetAngularVelocity(BallState& ball, const Vector3& omega) noexcept
		{
			const double rate = Length(omega);
			if (rate > 1e-9)
			{
				ball.spinAxis = omega / rate;
				ball.spinRpm = rate * 60.0 / (2.0 * std::numbers::pi);
			}
			else
			{
				ball.spinRpm = 0.0;
			}
		}

		// Velocity of the bottom of the ball over the ground (level ground, up is +y).
		[[nodiscard]] inline Vector3 ContactSlip(const BallState& ball) noexcept
		{
			const Vector3 omega = AngularVelocity(ball);
			const double r = ball.properties.radiusMeters;
			return Vector3{ball.velocity.x + r * omega.z, 0.0, ball.velocity.z - r * omega.x};
		}

		// Applies a horizontal impulse per unit mass at the bottom of the ball: it changes
		// the ball's velocity and, through the lever arm, its spin.
		inline void ApplyContactImpulse(BallState& ball, const Vector3& impulse) noexcept
		{
			const double r = ball.properties.radiusMeters;
			ball.velocity.x += impulse.x;
			ball.velocity.z += impulse.z;
			const Vector3 omega = AngularVelocity(ball) +
			    Vector3{-impulse.z, 0.0, impulse.x} / (BallInertiaRatio * r);
			SetAngularVelocity(ball, omega);
		}

		// The impulse per unit mass that would bring the slip to rest, limited by friction.
		[[nodiscard]] inline Vector3 FrictionImpulse(const BallState& ball, const double limit) noexcept
		{
			const Vector3 slip = ContactSlip(ball);
			const double slipSpeed = Length(slip);
			if (slipSpeed <= 1e-9)
			{
				return {};
			}
			const double stopping = slipSpeed * BallInertiaRatio / (1.0 + BallInertiaRatio);
			return slip * (-(std::min)(stopping, limit) / slipSpeed);
		}
	}

	// The ball strikes level ground moving down: it rebounds and its spin and horizontal
	// motion trade against friction at the contact (after Cross, "Grip-slip behavior of a
	// bouncing ball", Am. J. Phys. 70, 2002). Backspin checks a ball up, topspin skips it on.
	inline void BounceOffGround(BallState& ball, const GroundSurface& surface) noexcept
	{
		const double impact = -ball.velocity.y;
		if (impact <= 0.0)
		{
			return;
		}
		const double restitution = GroundRestitution(surface, impact);
		ball.velocity.y = restitution * impact;
		Detail::ApplyContactImpulse(ball, Detail::FrictionImpulse(ball, surface.friction * (1.0 + restitution) * impact));
	}

	// The ball runs along level ground for `seconds`: it slides under kinetic friction until
	// its bottom grips, then rolls, slowed by rolling resistance and the grass.
	inline void RollOnGround(BallState& ball, const GroundSurface& surface, const double seconds) noexcept
	{
		if (!(seconds > 0.0))
		{
			return;
		}
		constexpr double Gravity = 9.80665;
		ball.velocity.y = 0.0;
		Detail::ApplyContactImpulse(ball, Detail::FrictionImpulse(ball, surface.friction * Gravity * seconds));
		const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
		if (speed <= 1e-9)
		{
			ball.velocity = {};
			ball.spinRpm = 0.0;
			return;
		}
		if (Length(Detail::ContactSlip(ball)) < RollingSpeedThresholdMetersPerSecond)
		{
			const double slowed = (std::max)(0.0, speed - (surface.rollingDecelerationMetersPerSecond2 +
			    surface.rollingDragPerSecond * speed) * seconds);
			ball.velocity.x *= slowed / speed;
			ball.velocity.z *= slowed / speed;
			// Rolling without slipping: the spin follows the ball across the ground.
			const double r = ball.properties.radiusMeters;
			Detail::SetAngularVelocity(ball, Vector3{ball.velocity.z / r, 0.0, -ball.velocity.x / r});
		}
	}
}
