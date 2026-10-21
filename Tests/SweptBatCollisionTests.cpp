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
		return Tests::Near(a, b, tolerance, 0.0);
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
	responseInput.batSurfaceOffsetWorld = rotation->normal * responseInput.batRadiusMeters;
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
	responseInput.batSurfaceOffsetWorld = separating->normal * responseInput.batRadiusMeters;
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

	constexpr double barrelRadius = .035;
	constexpr double ballRadius = .01;
	const auto barrelHit = FindSweptBatProfileContact({ -.2, .8, 0.0 }, { .2, .8, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(barrelHit && Near(barrelHit->frameFraction, .3875, 1e-7) &&
	    Near(barrelHit->batSurfacePosition.x, -barrelRadius, 1e-8) && Near(barrelHit->batFraction, .8),
	    "Profile CCD hits the true cylindrical barrel surface at first entry");
	Tests::Check(barrelHit && Near(Length(barrelHit->batSurfaceOffset), barrelRadius) &&
	    Near(Dot(barrelHit->batSurfaceOffset, barrelHit->batAxis), 0.0),
	    "Profile contact carries the actual perpendicular surface lever arm");
	const auto handleMiss = FindSweptBatProfileContact({ -.2, .15, .03 }, { .2, .15, .03 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(!handleMiss, "A ball beside the narrow grip does not strike an imaginary barrel-width handle");
	const auto concentricProfile = FindSweptBatProfileContact({ 0.0, .8, 0.0 }, { 0.0, .8, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(concentricProfile && Near(concentricProfile->frameFraction, 0.0) &&
	    Near(Length(concentricProfile->normal), 1.0),
	    "A stationary ball initially inside the bat receives a real nearest-surface normal");
	const double handleSlope = barrelRadius * (.34 - .32) / (.2 - .1);
	const double handleEntryRadius = BatRadiusAtFraction(.15, barrelRadius) +
	    ballRadius * std::sqrt(1.0 + handleSlope * handleSlope);
	const auto handleHit = FindSweptBatProfileContact({ -.2, .15, 0.0 }, { .2, .15, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(handleHit && Near(handleHit->frameFraction, (.2 - handleEntryRadius) / .4, 1e-7) &&
	    Length(handleHit->batSurfaceOffset) < barrelRadius * .35 && handleHit->normal.y < 0.0,
	    "Thin handle contact respects its local radius and the taper surface normal");
	const double taperSlope = barrelRadius * (.84 - .67) / (.55 - .48);
	const double taperEntryRadius = BatRadiusAtFraction(.515, barrelRadius) +
	    ballRadius * std::sqrt(1.0 + taperSlope * taperSlope);
	const auto taperHit = FindSweptBatProfileContact({ -.2, .515, 0.0 }, { .2, .515, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(taperHit && Near(taperHit->frameFraction, (.2 - taperEntryRadius) / .4, 1e-7) &&
	    Near(taperHit->normal.y / taperHit->normal.x, taperSlope, 1e-6),
	    "The conical taper uses its slanted closest surface instead of axial radius sampling");
	const auto knobHit = FindSweptBatProfileContact({ 0.0, -.1, 0.0 }, { 0.0, .1, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(knobHit && Near(knobHit->frameFraction, .45, 1e-7) &&
	    Near(knobHit->batFraction, 0.0) && Near(knobHit->normal.y, -1.0) &&
	    Near(Length(knobHit->batSurfaceOffset), 0.0),
	    "The flat knob face starts exactly at the configured bat endpoint");
	Tests::Check(!FindSweptBatProfileContact({ -.2, -.02, 0.0 }, { .2, -.02, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01),
	    "The knob does not gain an invented rounded cap beyond the configured length");
	Tests::Check(!FindSweptBatProfileContact({ -.2, 1.02, 0.0 }, { .2, 1.02, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01),
	    "The rounded profile tip remains bounded by the configured bat endpoint");
	const auto tipHit = FindSweptBatProfileContact({ 0.0, 1.1, 0.0 }, { 0.0, .9, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(tipHit && Near(tipHit->frameFraction, .45, 1e-6) && Near(tipHit->batFraction, 1.0, 1e-6),
	    "An axial tip strike contacts the actual end of the silhouette");
	const auto profileTangent = FindSweptBatProfileContact({ -.2, .8, .045 }, { .2, .8, .045 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(profileTangent && Near(profileTangent->frameFraction, .5, 1e-6),
	    "A grazing profile contact is retained without sign-changing penetration");
	const auto taperTangent = FindSweptBatProfileContact({ -.2, .515, taperEntryRadius },
	    { .2, .515, taperEntryRadius }, bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(taperTangent && Near(taperTangent->frameFraction, .5, 1e-6),
	    "An even-multiplicity grazing root on a slanted taper is retained");
	const double tipSlope = barrelRadius * (.80 - .94) / (.990 - .980);
	const double tipEntryRadius = BatRadiusAtFraction(.985, barrelRadius) +
	    ballRadius * std::sqrt(1.0 + tipSlope * tipSlope);
	const auto tipTangent = FindSweptBatProfileContact({ -.2, .985, tipEntryRadius },
	    { .2, .985, tipEntryRadius }, bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(tipTangent && Near(tipTangent->frameFraction, .5, 1e-6),
	    "The shrinking rounded tip retains a grazing contact on its true profile");
	Tests::Check(!FindSweptBatProfileContact({ -.2, .8, .04501 }, { .2, .8, .04501 },
	    bat, bat, barrelRadius, ballRadius, .01),
	    "A close barrel-profile pass remains a miss");
	const auto profileFast = FindSweptBatProfileContact({ -10.0, .8, 0.0 }, { 10.0, .8, 0.0 },
	    bat, bat, barrelRadius, ballRadius, .01);
	Tests::Check(profileFast && Near(profileFast->frameFraction, .49775, 1e-7),
	    "A high-speed ball cannot tunnel through the profiled barrel");
	const auto movingProfile = FindSweptBatProfileContact({ 0.0, .8, 0.0 }, { 0.0, .8, 0.0 },
	    approachingStart, approachingEnd, barrelRadius, ballRadius, .01);
	Tests::Check(movingProfile && Near(movingProfile->frameFraction, .4775, 1e-7) &&
	    Near(movingProfile->batContactVelocity.x, 200.0),
	    "The finite profile sweep retains real material velocity when striking a stationary ball");
	const auto rotatingProfile = FindSweptBatProfileContact(stationaryBall, stationaryBall,
	    bat, rotated, barrelRadius, ballRadius, .01);
	Tests::Check(rotatingProfile && rotatingProfile->frameFraction > .4 && rotatingProfile->frameFraction < .5 &&
	    Near(Length(rotatingProfile->ballPosition - rotatingProfile->batSurfacePosition), ballRadius, 1e-7),
	    "A rotating tapered bat produces a true surface contact with a stationary ball");
	const auto commonProfile = FindSweptBatProfileContact({ -.2, .8, 0.0 },
	    Vector3{ .2, .8, 0.0 } + commonMotion, bat, translatedBat, barrelRadius, ballRadius, .01);
	Tests::Check(commonProfile && barrelHit && Near(commonProfile->frameFraction, barrelHit->frameFraction, 1e-7),
	    "Profile time of impact is invariant under large common frame translation");
	Tests::Check(!FindSweptBatProfileContact({ -.2, .8, 0.0 }, { .2, .8, 0.0 },
	    bat, bat, barrelRadius, 0.0, .01) &&
	    !FindSweptBatProfileContact({ nan, .8, 0.0 }, { .2, .8, 0.0 },
	        bat, bat, barrelRadius, ballRadius, .01),
	    "Profile CCD rejects invalid ball geometry and nonfinite motion");
}
