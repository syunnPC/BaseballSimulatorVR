#include "StadiumCollision.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace BaseballSimulatorVR::Simulation
{
	namespace
	{
		namespace S = Common::Stadium;

		// Padded walls take most of the pace off; the netting gives like a curtain; seats are
		// hard but uneven and soak up the rest.
		constexpr double WallRestitution = .45;
		constexpr double NetRestitution = .12;
		constexpr double WallFrictionRetention = .75;
		constexpr double SeatRestitution = .30;
		constexpr double SeatFrictionRetention = .55;
		// Below this the ball stays where it landed in the seats.
		constexpr double SeatRestSpeedMetersPerSecond = .8;

		[[nodiscard]] double BearingDegrees(const double u, const double v) noexcept
		{
			return std::atan2(u, v) * 180.0 / std::numbers::pi;
		}

		[[nodiscard]] double Outside(const FieldPosition& p) noexcept
		{
			return std::hypot(p.u, p.v) - S::BoundaryDistanceMeters(BearingDegrees(p.u, p.v));
		}

		// Velocity in field axes (u, v, up) and back.
		[[nodiscard]] Physics::Vector3 FieldVelocity(const Physics::Vector3& velocity) noexcept
		{
			return {velocity.x, -velocity.z, velocity.y};
		}
		[[nodiscard]] Physics::Vector3 SimulationVelocity(const Physics::Vector3& field) noexcept
		{
			return {field.x, field.z, -field.y};
		}

		void Rebound(Physics::BallState* const ball, const double normalU, const double normalV, const double restitution,
		    const double retention) noexcept
		{
			Physics::Vector3 velocity = FieldVelocity(ball->velocity);
			const double into = velocity.x * normalU + velocity.y * normalV;
			if (into <= 0.0) return;
			const double tu = velocity.x - into * normalU, tv = velocity.y - into * normalV;
			velocity.x = tu * retention - restitution * into * normalU;
			velocity.y = tv * retention - restitution * into * normalV;
			velocity.z *= retention;
			ball->velocity = SimulationVelocity(velocity);
		}

		void SetField(Physics::BallState* const ball, const double u, const double v, const StadiumPlacement& placement) noexcept
		{
			ball->position.x = u;
			ball->position.z = placement.plateApexDepthMeters - v;
		}
	}

	FieldPosition ToField(const Physics::Vector3& position, const StadiumPlacement& placement) noexcept
	{
		return {position.x, placement.plateApexDepthMeters - position.z, position.y - placement.groundHeightMeters};
	}

	StadiumCollisionResult ApplyStadiumCollision(
	    const Physics::BallState& previous, Physics::BallState* const current, const StadiumPlacement& placement)
	{
		StadiumCollisionResult result;
		if (current == nullptr) return result;
		const double radius = current->properties.radiusMeters;
		const FieldPosition from = ToField(previous.position, placement);
		FieldPosition to = ToField(current->position, placement);
		const double fromOutside = Outside(from);
		const double toOutside = Outside(to);

		// Crossing the line of the walls from the field: find where along the frame.
		if (fromOutside < 0.0 && toOutside >= 0.0)
		{
			double low = 0.0, high = 1.0;
			for (int iteration = 0; iteration < 30; ++iteration)
			{
				const double middle = .5 * (low + high);
				const FieldPosition p{from.u + (to.u - from.u) * middle, from.v + (to.v - from.v) * middle, 0.0};
				(Outside(p) < 0.0 ? low : high) = middle;
			}
			const double t = high;
			const double u = from.u + (to.u - from.u) * t, v = from.v + (to.v - from.v) * t;
			const double height = from.height + (to.height - from.height) * t;
			const double bearing = BearingDegrees(u, v);
			const S::Section section = S::SectionAt(bearing);
			result.reachedWall = true;
			result.wallSection = section;
			result.wallBearingDegrees = bearing;
			result.wallHeightMeters = height;
			result.wallFrameFraction = t;
			if (height - radius < S::WallHeightMeters(section))
			{
				const auto wall = S::BehindWall(u, v).nearest;
				const bool net = section == S::Section::Backstop && height > S::BackstopWallHeightMeters;
				// Back off the wall face by the ball's radius and rebound.
				SetField(current, u - wall.normalU * radius, v - wall.normalV * radius, placement);
				current->position.y = placement.groundHeightMeters + (std::max)(height, radius);
				Rebound(current, wall.normalU, wall.normalV, net ? NetRestitution : WallRestitution, WallFrictionRetention);
				result.contact = StadiumContact::Wall;
				return result;
			}
			result.clearedWall = true;
		}
		if (toOutside < 0.0) return result;

		// In the seats: rows of treads and risers stepping up behind the wall.
		const auto behind = S::BehindWall(to.u, to.v);
		const S::Section section = behind.nearest.section;
		if (behind.behindWallMeters > S::StandsDepthMeters(section))
		{
			result.contact = StadiumContact::LeftStadium;
			return result;
		}
		const double tread = S::StandsTreadHeightMeters(section, behind.behindWallMeters);
		if (to.height - radius >= tread) return result;
		result.contact = StadiumContact::Stands;
		if (from.height - radius >= tread - 1e-6)
		{
			// Landed on a row of seats.
			current->position.y = placement.groundHeightMeters + tread + radius;
			Physics::Vector3 velocity = FieldVelocity(current->velocity);
			velocity.x *= SeatFrictionRetention;
			velocity.y *= SeatFrictionRetention;
			velocity.z = velocity.z < 0.0 ? -velocity.z * SeatRestitution : velocity.z;
			if (Physics::Length(velocity) < SeatRestSpeedMetersPerSecond) velocity = {};
			current->velocity = SimulationVelocity(velocity);
			return result;
		}
		// Ran into the riser of the next row up: back to where it was (just over the wall if
		// it came from the field this frame) and rebound.
		if (result.clearedWall)
		{
			const double t = result.wallFrameFraction;
			const auto wall = S::BehindWall(from.u + (to.u - from.u) * t, from.v + (to.v - from.v) * t).nearest;
			SetField(current, from.u + (to.u - from.u) * t + wall.normalU * (radius + .01),
			    from.v + (to.v - from.v) * t + wall.normalV * (radius + .01), placement);
			current->position.y = placement.groundHeightMeters +
			    (std::max)(to.height, S::FirstRowHeightMeters(wall.section) + radius);
		}
		else
		{
			SetField(current, from.u, from.v, placement);
		}
		Rebound(current, behind.nearest.normalU, behind.nearest.normalV, SeatRestitution, SeatFrictionRetention);
		return result;
	}
}
