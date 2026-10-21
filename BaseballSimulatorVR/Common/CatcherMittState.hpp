#pragma once

#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Common
{
	// A catcher's mitt worn on the hand opposite the throwing hand, posed from that
	// controller's grip, in world coordinates.
	struct CatcherMittState final
	{
		Physics::Vector3 gripWorld{};
		// Controller forward (toward the fingertips of the gloved hand) and up (thumb side).
		Physics::Vector3 forwardWorld{ 0.0, 0.0, -1.0 };
		Physics::Vector3 upWorld{ 0.0, 1.0, 0.0 };
		bool leftHand{ true };
		// 0 open, 1 squeezed shut.
		double closedAmount{ 0.0 };
	};

	// The mitt's pocket and axes: `palm` is the way the pocket faces, `along` runs from the
	// heel to the top of the mitt and `across` toward the thumb.
	struct CatcherMittFrame final
	{
		Physics::Vector3 pocket{};
		Physics::Vector3 palm{ 0.0, 0.0, 1.0 };
		Physics::Vector3 along{ 0.0, 1.0, 0.0 };
		Physics::Vector3 across{ 1.0, 0.0, 0.0 };
	};

	// A ball inside this distance of the pocket's center is in the mitt; one inside the rim
	// distance strikes the leather around it.
	inline constexpr double CatcherMittPocketRadiusMeters = .11;
	inline constexpr double CatcherMittRimRadiusMeters = .19;

	[[nodiscard]] inline CatcherMittFrame BuildCatcherMittFrame(const CatcherMittState& mitt) noexcept
	{
		CatcherMittFrame frame;
		const Physics::Vector3 along = Physics::NormalizeOrZero(mitt.forwardWorld);
		Physics::Vector3 across = mitt.upWorld - along * Physics::Dot(mitt.upWorld, along);
		across = Physics::NormalizeOrZero(across);
		if (Physics::Length(along) < .5 || Physics::Length(across) < .5)
		{
			frame.pocket = mitt.gripWorld;
			return frame;
		}
		// The palm of a left hand holding a controller faces to its right.
		frame.along = along;
		frame.across = across;
		frame.palm = mitt.leftHand ? Physics::Cross(along, across) : Physics::Cross(across, along);
		frame.pocket = mitt.gripWorld + along * .07 + frame.palm * .06;
		return frame;
	}
}
