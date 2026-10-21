#pragma once

#include "../Common/StadiumGeometry.hpp"
#include "../Physics/BallState.hpp"

namespace BaseballSimulatorVR::Simulation
{
	// Where the stadium sits in simulation coordinates: depth (z) of the apex of home
	// plate and the height (y) of the ground.
	struct StadiumPlacement final
	{
		double plateApexDepthMeters{};
		double groundHeightMeters{};
	};

	enum class StadiumContact { None, Wall, Stands, LeftStadium };

	struct StadiumCollisionResult final
	{
		StadiumContact contact{ StadiumContact::None };
		// Set when the ball crossed the line of the walls this frame, hit or cleared.
		bool reachedWall{ false };
		bool clearedWall{ false };
		Common::Stadium::Section wallSection{ Common::Stadium::Section::Outfield };
		double wallBearingDegrees{};
		// Height of the ball's center above the ground where it reached the wall, and the
		// fraction of the frame at which it got there.
		double wallHeightMeters{};
		double wallFrameFraction{};
	};

	// Field coordinates of a simulation position: u toward first base, v toward the
	// pitcher (both from the plate apex) and the height above the ground.
	struct FieldPosition final
	{
		double u{}, v{}, height{};
	};
	[[nodiscard]] FieldPosition ToField(const Physics::Vector3& position, const StadiumPlacement& placement) noexcept;

	// Plays one frame of ball motion (from `previous` to `*current`) against the walls,
	// the backstop netting and the stepped seating. A ball that hits a wall or a seat riser
	// rebounds off it; one that lands on a row of seats bounces and soon stops. The
	// playing field's ground is left to the caller.
	StadiumCollisionResult ApplyStadiumCollision(
	    const Physics::BallState& previous, Physics::BallState* current, const StadiumPlacement& placement);
}
