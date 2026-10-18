#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Physics/SweptBatCollision.hpp"
#include "../BaseballSimulatorVR/Physics/BatBallCollision.hpp"
#include "../BaseballSimulatorVR/Common/BatPoseUtilities.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace
{
	[[nodiscard]] bool Near(double a, double b, double tolerance = 1e-8)
	{
		return std::abs(a - b) <= tolerance;
	}
}

void RunSweptBatCollisionTests()
{
	using namespace BaseballSimulatorVR::Physics;
	using BaseballSimulatorVR::Common::BuildBatAxisDirection;
	const BatSegment bat{ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };
	const auto direct = FindSweptBatContact({ -1.0, 0.5, 0.0 }, { 1.0, 0.5, 0.0 }, bat, bat, 0.07, 0.01);
	Tests::Check(direct.has_value(), "Swept ball cannot tunnel through a stationary bat");
	Tests::Check(Near(direct->frameFraction, 0.465) && Near(direct->batFraction, 0.5),
	    "Contact is the earliest capsule entry rather than the closest crossing");
	Tests::Check(Near(direct->ballPosition.x, -0.07) && Near(direct->normal.x, -1.0),
	    "First-contact normal points from bat toward incoming ball");
	const auto fast = FindSweptBatContact({ -10.0, 0.5, 0.0 }, { 10.0, 0.5, 0.0 }, bat, bat, 0.07, 0.01);
	Tests::Check(fast && Near(fast->frameFraction, 0.4965), "Even a 2000 m/s stress case retains first contact");
	const auto end = FindSweptBatContact({ -1.0, 1.04, 0.0 }, { 1.0, 1.04, 0.0 }, bat, bat, 0.05, 0.01);
	Tests::Check(end && Near(end->frameFraction, 0.485) && Near(end->batFraction, 1.0),
	    "Rounded bat end has the correct sphere entry time");
	const auto beyondEnd = FindSweptBatContact({ -1.0, 1.06, 0.0 }, { 1.0, 1.06, 0.0 }, bat, bat, 0.05, 0.01);
	Tests::Check(!beyondEnd, "The infinite cylinder extension cannot hit beyond the rounded bat end");
	const auto tangent = FindSweptBatContact({ -1.0, 0.5, 0.07 }, { 1.0, 0.5, 0.07 }, bat, bat, 0.07, 0.01);
	Tests::Check(tangent && Near(tangent->frameFraction, 0.5, 1e-6),
	    "A tangent double root is detected without advancement iteration limits");
	const auto nearMiss = FindSweptBatContact({ -1.0, 0.5, 0.0701 }, { 1.0, 0.5, 0.0701 }, bat, bat, 0.07, 0.01);
	Tests::Check(!nearMiss, "A close pass outside the capsule remains a miss");
	const auto endTangent = FindSweptBatContact({ -1.0, 1.07, 0.0 }, { 1.0, 1.07, 0.0 }, bat, bat, 0.07, 0.01);
	Tests::Check(endTangent && Near(endTangent->frameFraction, 0.5, 1e-6),
	    "Endpoint sphere tangent contacts are detected");

	const BatSegment previousMoving{ { -1.5, 0.0, 0.0 }, { -1.5, 1.0, 0.0 } };
	const BatSegment currentMoving{ { 0.5, 0.0, 0.0 }, { 0.5, 1.0, 0.0 } };
	Tests::Check(!FindSweptBatContact({ -1.0, 0.5, 0.0 }, { 1.0, 0.5, 0.0 }, previousMoving, currentMoving, 0.07, 0.01),
	    "Crossing an old ball path with the current bat cannot invent a simultaneous contact");
	const BatSegment approachingStart{ { -1.0, 0.0, 0.0 }, { -1.0, 1.0, 0.0 } };
	const BatSegment approachingEnd{ { 1.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 } };
	const auto movingBat = FindSweptBatContact({ 0.0, 0.5, 0.0 }, { 0.0, 0.5, 0.0 }, approachingStart, approachingEnd, 0.07, 0.01);
	Tests::Check(movingBat && Near(movingBat->frameFraction, 0.465) && Near(movingBat->batContactVelocity.x, 200.0),
	    "A bat can sweep into a stationary tee ball at its actual contact velocity");

	const BatSegment rotated{ {}, { 1.0, 0.0, 0.0 } };
	const Vector3 stationaryBall{ 0.35, 0.35, 0.0 };
	const auto rotation = FindSweptBatContact(stationaryBall, stationaryBall, bat, rotated, 0.02, 0.01);
	Tests::Check(rotation && rotation->frameFraction > 0.4 && rotation->frameFraction < 0.5,
	    "A bat rotating about a fixed hand still reaches a tee ball");
	Tests::Check(Near(rotation->batContactVelocity.x, 100.0 * rotation->batFraction) &&
	    Near(rotation->batContactVelocity.y, -100.0 * rotation->batFraction),
	    "Fixed-hand rotation includes endpoint motion in material contact-point velocity");
	const double interpolatedAxisSquared = rotation->frameFraction * rotation->frameFraction +
	    (1.0 - rotation->frameFraction) * (1.0 - rotation->frameFraction);
	Tests::Check(Near(rotation->batAngularVelocity.z, -100.0 / interpolatedAxisSquared) &&
	    Near(rotation->batAngularVelocity.x, 0.0) && Near(rotation->batAngularVelocity.y, 0.0),
	    "The swept contact also reports instantaneous angular velocity for barrel surface motion");
	Tests::Check(Near(Length(movingBat->batAngularVelocity), 0.0),
	    "Pure bat translation cannot invent angular surface velocity");
	BatBallCollisionInput responseInput{};
	responseInput.ballVelocityWorld = {};
	responseInput.batContactVelocityWorld = rotation->batContactVelocity;
	responseInput.collisionNormal = rotation->normal;
	responseInput.batAxisDirectionWorld = rotation->batAxis;
	responseInput.batLengthMeters = 1.0;
	responseInput.contactDistanceFromHandleMeters = rotation->batFraction;
	Tests::Check(ResolveBatBallCollision(responseInput).hasImpulse,
	    "The fixed-hand rotating bat produces a real approaching collision response");

	const auto separating = FindSweptBatContact({ -0.06, 0.5, 0.0 }, { -0.3, 0.5, 0.0 }, bat, bat, 0.07, 0.01);
	Tests::Check(separating && separating->frameFraction == 0.0, "An initially overlapping ball still reports geometry");
	responseInput.ballVelocityWorld = { -24.0, 0.0, 0.0 };
	responseInput.batContactVelocityWorld = separating->batContactVelocity;
	responseInput.collisionNormal = separating->normal;
	responseInput.batAxisDirectionWorld = separating->batAxis;
	responseInput.contactDistanceFromHandleMeters = separating->batFraction;
	Tests::Check(!ResolveBatBallCollision(responseInput).hasImpulse,
	    "An overlapping but departing ball receives no second bat impulse");
	Tests::Check(!FindSweptBatContact({ 0.3, 0.5, 0.0 }, { 0.3, 0.5, 0.0 }, bat, bat, 0.07, 0.01),
	    "Separated stationary shapes do not collide");
	const auto frameEnd = FindSweptBatContact({ -1.0, 0.5, 0.0 }, { -0.07, 0.5, 0.0 }, bat, bat, 0.07, 0.01);
	Tests::Check(frameEnd && Near(frameEnd->frameFraction, 1.0), "Contact on the exact frame boundary is retained");

	const Vector3 commonMotion{ 1000.0, -2000.0, 3000.0 };
	BatSegment translatedBat = bat;
	translatedBat.start += commonMotion;
	translatedBat.end += commonMotion;
	const auto common = FindSweptBatContact({ -1.0, 0.5, 0.0 }, Vector3{ 1.0, 0.5, 0.0 } + commonMotion,
	    bat, translatedBat, 0.07, 0.01);
	Tests::Check(common && Near(common->frameFraction, direct->frameFraction),
	    "Large common frame motion does not exhaust iterations or change the collision time");

	const auto axis = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, 0.0, 0.0);
	Tests::Check(axis && Near(axis->z, -1.0), "Zero offsets retain controller forward");
	const auto yaw = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, 0.0, 90.0);
	Tests::Check(yaw && Near(yaw->x, -1.0) && Near(yaw->z, 0.0), "Yaw rotates around controller up");
	const auto pitch = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, 90.0, 0.0);
	Tests::Check(pitch && Near(pitch->y, 1.0), "Positive pitch rotates forward toward up");
	const auto tilted = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 1.0, 0.0, 0.0 }, 90.0, 0.0);
	Tests::Check(tilted && Near(tilted->x, 1.0), "Bat pitch follows controller-local up rather than world up");
	const auto combined = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, 45.0, 90.0);
	Tests::Check(combined && Near(combined->x, -std::sqrt(0.5)) && Near(combined->y, std::sqrt(0.5)),
	    "Yaw and pitch compose using the yawed right axis");
	const auto complete = BaseballSimulatorVR::Common::BuildBatOrientation(
	    { 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, 90.0, 90.0);
	Tests::Check(complete && Near(Length(complete->axis), 1.0) &&
	    Near(Length(complete->up), 1.0) && Near(Dot(complete->axis, complete->up), 0.0),
	    "Bat offsets rotate a complete orthonormal frame even when pitch is ninety degrees");
	const BaseballSimulatorVR::Common::BatOrientation beforeRoll{ { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
	const double rollAngle = 0.3;
	const BaseballSimulatorVR::Common::BatOrientation afterRoll{ { 0.0, 0.0, 1.0 },
	    { -std::sin(rollAngle), std::cos(rollAngle), 0.0 } };
	const auto roll = BaseballSimulatorVR::Common::ComputeBatAngularVelocity(beforeRoll, afterRoll, 0.01);
	Tests::Check(roll && Near(roll->z, 30.0) && Near(roll->x, 0.0) && Near(roll->y, 0.0),
	    "A pure thirty-radian-per-second barrel roll is observed even when the bat endpoints do not move");
	Tests::Check(roll && Near(Cross(*roll, Vector3{ 0.035, 0.0, 0.0 }).y, 1.05),
	    "Longitudinal controller rotation contributes its physical barrel-surface velocity");
	const auto stillFrame = BaseballSimulatorVR::Common::ComputeBatAngularVelocity(beforeRoll, beforeRoll, 0.01);
	Tests::Check(stillFrame && Near(Length(*stillFrame), 0.0),
	    "Identical full bat orientations create zero angular velocity");
	Tests::Check(!BuildBatAxisDirection({}, { 0.0, 1.0, 0.0 }, 0.0, 0.0) &&
	    !BuildBatAxisDirection({ 0.0, 1.0, 0.0 }, { 0.0, 1.0, 0.0 }, 0.0, 0.0),
	    "Degenerate controller axes are rejected");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	Tests::Check(!BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 }, nan, 0.0),
	    "Nonfinite bat angle offsets are rejected");
	const auto largeAngle = BuildBatAxisDirection({ 0.0, 0.0, -1.0 }, { 0.0, 1.0, 0.0 },
	    std::numeric_limits<double>::max(), 0.0);
	Tests::Check(largeAngle && Near(Length(*largeAngle), 1.0),
	    "Finite angle conversion cannot overflow to a nonfinite bat direction");
	for (double invalidSeconds : std::array{ 0.0, -0.01, nan })
		Tests::Check(!FindSweptBatContact({ -1.0, 0.5, 0.0 }, { 1.0, 0.5, 0.0 }, bat, bat, 0.07, invalidSeconds),
		    "Nonpositive or nonfinite frame durations are rejected");
	Tests::Check(!FindSweptBatContact({ nan, 0.5, 0.0 }, { 1.0, 0.5, 0.0 }, bat, bat, 0.07, 0.01),
	    "Nonfinite position cannot create a contact");
	Tests::Check(!FindSweptBatContact({ -1.0, 0.5, 0.0 }, { 1.0, 0.5, 0.0 }, bat, bat, 0.0, 0.01),
	    "Zero radius cannot create a contact");
}
