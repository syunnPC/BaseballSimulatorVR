#pragma once

#include "FieldPresentationState.hpp"
#include "IndexedWorldMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace BaseballSimulatorVR::Common
{
	inline constexpr double PitcherWindupSeconds = 1.2;
	inline constexpr double PitcherFollowThroughSeconds = 0.6;
	inline constexpr double ReferencePitcherSpeedMetersPerSecond = 145.0 / 3.6;
	[[nodiscard]] inline double PitcherArmSpeedScale(double fastestPresetSpeedMetersPerSecond)
	{
		return std::clamp(
		    fastestPresetSpeedMetersPerSecond / ReferencePitcherSpeedMetersPerSecond, .25, 2.0);
	}

	// Between duel pitches the pitcher walks back to the rubber over this long and then holds
	// the set position briefly before the next windup.
	inline constexpr double PitcherRecoverySeconds = 1.5;
	inline constexpr double PitcherSetHoldSeconds = .5;
	[[nodiscard]] inline double PitcherRecoveryProgress(const double secondsUntilWindup)
	{
		return std::clamp((PitcherRecoverySeconds + PitcherSetHoldSeconds - secondsUntilWindup) / PitcherRecoverySeconds,
		    0.0, 1.0);
	}

	enum class PitcherDeliveryStyle { Overhand, Sidearm, Submarine, UnderhandToss };
	inline constexpr double SubmarineReleaseHeightLimitMeters = .65;
	inline constexpr double SidearmReleaseHeightLimitMeters = 1.50;
	inline constexpr double PitcherArmSegmentMeters = .38;
	inline constexpr double PitcherLegSegmentMeters = .47;

	[[nodiscard]] inline PitcherDeliveryStyle GetPitcherDeliveryStyle(const PitcherPresentationState& state)
	{
		if (state.underhandToss) return PitcherDeliveryStyle::UnderhandToss;
		if (state.releasePositionWorld.y <= state.moundHeightMeters + SubmarineReleaseHeightLimitMeters)
			return PitcherDeliveryStyle::Submarine;
		if (state.releasePositionWorld.y <= state.moundHeightMeters + SidearmReleaseHeightLimitMeters)
			return PitcherDeliveryStyle::Sidearm;
		return PitcherDeliveryStyle::Overhand;
	}

	struct PitcherPose final
	{
		Physics::Vector3 pelvis{}, chest{}, head{};
		Physics::Vector3 rightShoulder{}, rightElbow{}, rightHand{};
		Physics::Vector3 leftShoulder{}, leftElbow{}, leftHand{};
		Physics::Vector3 rightHip{}, rightKnee{}, rightFoot{};
		Physics::Vector3 leftHip{}, leftKnee{}, leftFoot{};
		Physics::Vector3 heldBall{};
		Physics::Vector3 moundOrigin{};
		Physics::Vector3 pelvisLateral{1.0, 0.0, 0.0};
		Physics::Vector3 chestLateral{1.0, 0.0, 0.0};
		Physics::Vector3 headForward{0.0, 0.0, 1.0};
		Physics::Vector3 rightFootForward{0.0, 0.0, 1.0};
		Physics::Vector3 leftFootForward{0.0, 0.0, 1.0};
		double scale{1.0};
	};

	namespace PitcherMotion
	{
		using Physics::Vector3;
		inline constexpr double Degrees = std::numbers::pi / 180.0;

		// One authored delivery key for a right-handed pitcher in the mound frame: +Z toward
		// home plate, +X to the glove side, +Y up, meters before body scale. Angles are in
		// degrees; yaw 0 faces the plate and 90 is fully closed (glove side to the plate).
		// Hands are relative to their shoulder in the trunk frame (x: glove side, y: trunk
		// up, z: chest forward), with an elbow direction hint in the same frame.
		struct Key final
		{
			double time;
			Vector3 pelvis;
			double hipYaw, trunkYaw, trunkFlexion, trunkTilt;
			Vector3 throwHand, throwElbow, gloveHand, gloveElbow;
			Vector3 leftFoot, rightFoot;
			double leftFootYaw, rightFootYaw;
			// Weights of the release-height adaptation and of the stride-length change.
			double slot, stride;
		};
		// Release-height adaptation, interpolated between anchors at slot 0, 1 and 2.
		struct SlotAdjustment final
		{
			double trunkTilt, trunkFlexion, pelvisHeight, strideLength, armSwing;
		};
		inline constexpr std::size_t KeyCount = 12;
		inline constexpr std::size_t ReleaseKey = 7;
		struct Delivery final
		{
			std::array<Key, KeyCount> keys;
			std::array<SlotAdjustment, 3> slot;
		};

		struct Frame final
		{
			Vector3 lateral, up, forward;
		};
		// Trunk orientation from the shoulder-line yaw, forward flexion and contralateral
		// tilt (positive tilt raises the throwing shoulder, as in an overhand slot).
		[[nodiscard]] inline Frame TrunkFrame(const double yaw, const double flexion, const double tilt)
		{
			const Vector3 lateral{std::cos(yaw), 0.0, std::sin(yaw)};
			const Vector3 forward{-std::sin(yaw), 0.0, std::cos(yaw)};
			const Vector3 up{0.0, 1.0, 0.0};
			const Vector3 flexedUp = up * std::cos(flexion) + forward * std::sin(flexion);
			const Vector3 flexedForward = forward * std::cos(flexion) - up * std::sin(flexion);
			return {lateral * std::cos(tilt) - flexedUp * std::sin(tilt),
			    flexedUp * std::cos(tilt) + lateral * std::sin(tilt), flexedForward};
		}
		[[nodiscard]] inline Vector3 ToWorld(const Frame& frame, const Vector3& local)
		{
			return frame.lateral * local.x + frame.up * local.y + frame.forward * local.z;
		}
		// Places the middle joint of a two-segment limb of equal segment lengths on the side
		// given by the bend hint, keeping both segments at their exact length.
		[[nodiscard]] inline Vector3 BendJoint(
		    const Vector3& start, const Vector3& end, const Vector3& bend, const double length)
		{
			const Vector3 delta = end - start;
			const double distance = Physics::Length(delta);
			const Vector3 axis = Physics::NormalizeOrZero(delta);
			auto perpendicular = bend - axis * Physics::Dot(bend, axis);
			if (Physics::Length(perpendicular) < 1e-6)
			{
				const Vector3 seed = std::abs(axis.y) < .9 ? Vector3{0., 1., 0.} : Vector3{1., 0., 0.};
				perpendicular = seed - axis * Physics::Dot(seed, axis);
			}
			const Vector3 normal = Physics::NormalizeOrZero(perpendicular);
			const double height = std::sqrt((std::max)(0.0, length * length - distance * distance * .25));
			return (start + end) * .5 + normal * height;
		}
		// Rotates a trunk-frame vector about the trunk's lateral axis, swinging -up toward
		// +forward for positive angles (an underhand arm swing).
		[[nodiscard]] inline Vector3 SwingForward(const Vector3& v, const double angle)
		{
			return {v.x, v.y * std::cos(angle) + v.z * std::sin(angle), -v.y * std::sin(angle) + v.z * std::cos(angle)};
		}

		struct ArmDirections final
		{
			Vector3 upper, fore;
		};
		[[nodiscard]] inline ArmDirections SolveArm(Vector3 hand, const Vector3& elbowHint)
		{
			const double reach = 2.0 * PitcherArmSegmentMeters * .999;
			const double distance = Physics::Length(hand);
			if (distance > reach) hand = hand * (reach / distance);
			const Vector3 elbow = BendJoint({}, hand, elbowHint, PitcherArmSegmentMeters);
			return {elbow / PitcherArmSegmentMeters, (hand - elbow) / PitcherArmSegmentMeters};
		}

		[[nodiscard]] inline SlotAdjustment SlotAt(const Delivery& delivery, const double slot)
		{
			const double s = std::clamp(slot, 0.0, 2.0);
			const auto& a = delivery.slot[s <= 1.0 ? 0 : 1];
			const auto& b = delivery.slot[s <= 1.0 ? 1 : 2];
			const double u = s <= 1.0 ? s : s - 1.0;
			const auto mix = [u](double x, double y) { return x + (y - x) * u; };
			return {mix(a.trunkTilt, b.trunkTilt), mix(a.trunkFlexion, b.trunkFlexion),
			    mix(a.pelvisHeight, b.pelvisHeight), mix(a.strideLength, b.strideLength),
			    mix(a.armSwing, b.armSwing)};
		}

		// Keys after the release-height adaptation, with arm directions solved per key.
		struct AdjustedKeys final
		{
			std::array<Key, KeyCount> keys;
			std::array<ArmDirections, KeyCount> throwArm, gloveArm;
		};
		[[nodiscard]] inline AdjustedKeys Adjust(const Delivery& delivery, const double slot)
		{
			const SlotAdjustment adjustment = SlotAt(delivery, slot);
			AdjustedKeys result{delivery.keys, {}, {}};
			for (std::size_t k = 0; k < KeyCount; ++k)
			{
				Key& key = result.keys[k];
				key.trunkTilt += adjustment.trunkTilt * key.slot;
				key.trunkFlexion += adjustment.trunkFlexion * key.slot;
				key.pelvis.y += adjustment.pelvisHeight * key.slot;
				const double stride = adjustment.strideLength * key.stride;
				key.leftFoot.z += stride;
				key.pelvis.z += stride * .6;
				// The trailing foot follows the stride change once it has swung forward.
				key.rightFoot.z += stride * std::clamp((key.rightFoot.z + .1) / 1.0, 0.0, 1.0);
				const double swing = adjustment.armSwing * key.slot * Degrees;
				result.throwArm[k] = SolveArm(SwingForward(key.throwHand, swing), SwingForward(key.throwElbow, swing));
				result.gloveArm[k] = SolveArm(key.gloveHand, key.gloveElbow);
			}
			return result;
		}

		struct Sample final
		{
			Vector3 pelvis, leftFoot, rightFoot;
			double hipYaw, trunkYaw, trunkFlexion, trunkTilt, leftFootYaw, rightFootYaw;
			ArmDirections throwArm, gloveArm;
		};
		// Cubic Hermite through the keys. Smooth (non-uniform Catmull-Rom) tangents keep the
		// body flowing through each key; monotone tangents keep planted feet and the pelvis
		// from overshooting their keys.
		[[nodiscard]] inline Sample SampleAt(const AdjustedKeys& adjusted, const double time)
		{
			const auto& keys = adjusted.keys;
			std::size_t index = 0;
			while ((index + 2 < KeyCount) && (time > keys[index + 1].time))
			{
				++index;
			}
			const double span = keys[index + 1].time - keys[index].time;
			const double u = std::clamp((time - keys[index].time) / span, 0.0, 1.0);
			const double h00 = (1.0 + 2.0 * u) * (1.0 - u) * (1.0 - u);
			const double h10 = u * (1.0 - u) * (1.0 - u);
			const double h01 = u * u * (3.0 - 2.0 * u);
			const double h11 = u * u * (u - 1.0);
			const auto tangent = [&](const std::size_t k, auto value, const bool monotone) -> double
			{
				if (k == 0 || k + 1 >= KeyCount)
				{
					return 0.0;
				}
				const double hPrev = keys[k].time - keys[k - 1].time;
				const double hNext = keys[k + 1].time - keys[k].time;
				const double dPrev = (value(k) - value(k - 1)) / hPrev;
				const double dNext = (value(k + 1) - value(k)) / hNext;
				if (!monotone)
				{
					return (dPrev * hNext + dNext * hPrev) / (hPrev + hNext);
				}
				if (dPrev * dNext <= 0.0)
				{
					return 0.0;
				}
				const double w1 = 2.0 * hNext + hPrev;
				const double w2 = hNext + 2.0 * hPrev;
				return (w1 + w2) / (w1 / dPrev + w2 / dNext);
			};
			const auto blend = [&](auto value, const bool monotone)
			{
				return h00 * value(index) + h10 * span * tangent(index, value, monotone) +
				       h01 * value(index + 1) + h11 * span * tangent(index + 1, value, monotone);
			};
			const auto scalar = [&](double Key::*member, const bool monotone)
			{
				return blend([&](std::size_t k) { return keys[k].*member; }, monotone);
			};
			const auto vector = [&](Vector3 Key::*member, const bool monotone) -> Vector3
			{
				return {blend([&](std::size_t k) { return (keys[k].*member).x; }, monotone),
				    blend([&](std::size_t k) { return (keys[k].*member).y; }, monotone),
				    blend([&](std::size_t k) { return (keys[k].*member).z; }, monotone)};
			};
			const auto direction = [&](const std::array<ArmDirections, KeyCount>& arm, Vector3 ArmDirections::*member)
			{
				return Physics::NormalizeOrZero(Vector3{
				    blend([&](std::size_t k) { return (arm[k].*member).x; }, false),
				    blend([&](std::size_t k) { return (arm[k].*member).y; }, false),
				    blend([&](std::size_t k) { return (arm[k].*member).z; }, false)});
			};
			return {vector(&Key::pelvis, true), vector(&Key::leftFoot, true), vector(&Key::rightFoot, true),
			    scalar(&Key::hipYaw, false), scalar(&Key::trunkYaw, false), scalar(&Key::trunkFlexion, false),
			    scalar(&Key::trunkTilt, false), scalar(&Key::leftFootYaw, false), scalar(&Key::rightFootYaw, false),
			    {direction(adjusted.throwArm, &ArmDirections::upper), direction(adjusted.throwArm, &ArmDirections::fore)},
			    {direction(adjusted.gloveArm, &ArmDirections::upper), direction(adjusted.gloveArm, &ArmDirections::fore)}};
		}

		// Walk from the finish back to the set position: the stride foot steps back first,
		// then the pivot foot returns to the rubber while the body turns side-on again.
		[[nodiscard]] inline Sample Recover(const Sample& finish, const Sample& set, const double progress)
		{
			const auto ease = [](const double u)
			{
				const double c = std::clamp(u, 0.0, 1.0);
				return c * c * (3.0 - 2.0 * c);
			};
			const double body = ease(progress);
			const double left = ease(progress / .55);
			const double right = ease((progress - .45) / .55);
			const auto mix = [](const double a, const double b, const double u) { return a + (b - a) * u; };
			const auto mixVector = [](const Vector3& a, const Vector3& b, const double u) { return a + (b - a) * u; };
			const auto mixArm = [&](const ArmDirections& a, const ArmDirections& b)
			{
				return ArmDirections{Physics::NormalizeOrZero(mixVector(a.upper, b.upper, body)),
				    Physics::NormalizeOrZero(mixVector(a.fore, b.fore, body))};
			};
			Sample result{};
			result.pelvis = mixVector(finish.pelvis, set.pelvis, body);
			result.leftFoot = mixVector(finish.leftFoot, set.leftFoot, left) +
			                  Vector3{0.0, .10 * std::sin(std::numbers::pi * left), 0.0};
			result.rightFoot = mixVector(finish.rightFoot, set.rightFoot, right) +
			                   Vector3{0.0, .10 * std::sin(std::numbers::pi * right), 0.0};
			result.hipYaw = mix(finish.hipYaw, set.hipYaw, body);
			result.trunkYaw = mix(finish.trunkYaw, set.trunkYaw, body);
			result.trunkFlexion = mix(finish.trunkFlexion, set.trunkFlexion, body);
			result.trunkTilt = mix(finish.trunkTilt, set.trunkTilt, body);
			result.leftFootYaw = mix(finish.leftFootYaw, set.leftFootYaw, left);
			result.rightFootYaw = mix(finish.rightFootYaw, set.rightFootYaw, right);
			result.throwArm = mixArm(finish.throwArm, set.throwArm);
			result.gloveArm = mixArm(finish.gloveArm, set.gloveArm);
			return result;
		}

		[[nodiscard]] inline PitcherPose ComposePose(const Sample& sample, const Vector3& base, const double scale)
		{
			PitcherPose pose{};
			pose.scale = scale;
			pose.moundOrigin = base;
			const auto world = [&](const Vector3& p) { return base + p * scale; };
			const Frame trunk = TrunkFrame(sample.trunkYaw * Degrees, sample.trunkFlexion * Degrees,
			    sample.trunkTilt * Degrees);
			pose.pelvis = world(sample.pelvis);
			pose.leftFoot = world(sample.leftFoot);
			pose.rightFoot = world(sample.rightFoot);
			pose.leftFoot.y = (std::max)(pose.leftFoot.y, base.y);
			pose.rightFoot.y = (std::max)(pose.rightFoot.y, base.y);
			// The pelvis follows only part of the trunk lean, as the lumbar spine bends.
			const Vector3 hipLateral{std::cos(sample.hipYaw * Degrees), 0.0, std::sin(sample.hipYaw * Degrees)};
			const Vector3 pelvisUp = Physics::NormalizeOrZero(trunk.up * .35 + Vector3{0.0, .65, 0.0});
			pose.pelvisLateral = Physics::NormalizeOrZero(hipLateral - pelvisUp * Physics::Dot(hipLateral, pelvisUp));
			// Lower the pelvis wherever a leg could not otherwise reach its foot.
			const double hipOffset = .115 * scale;
			const double legReach = 2.0 * PitcherLegSegmentMeters * scale * .995;
			double drop = 0.0;
			for (const double side : {-1.0, 1.0})
			{
				const Vector3 hip = pose.pelvis + pose.pelvisLateral * (side * hipOffset);
				const Vector3& foot = side < 0.0 ? pose.rightFoot : pose.leftFoot;
				const double horizontal = (hip.x - foot.x) * (hip.x - foot.x) + (hip.z - foot.z) * (hip.z - foot.z);
				const double highest = foot.y + std::sqrt((std::max)(0.0, legReach * legReach - horizontal));
				drop = (std::max)(drop, hip.y - highest);
			}
			pose.pelvis.y -= drop;
			pose.rightHip = pose.pelvis - pose.pelvisLateral * hipOffset;
			pose.leftHip = pose.pelvis + pose.pelvisLateral * hipOffset;
			pose.chest = pose.pelvis + trunk.up * (.53 * scale);
			pose.chestLateral = trunk.lateral;
			pose.head = pose.chest + (trunk.up * .14 + Vector3{0., .18, -.02}) * scale;
			// Eyes stay on the target; the neck absorbs most of the trunk rotation.
			const double headYaw = std::clamp(sample.trunkYaw * .2, -15.0, 25.0) * Degrees;
			pose.headForward = Physics::NormalizeOrZero(Vector3{
			    -std::sin(headYaw), -.12 - .25 * std::sin(sample.trunkFlexion * Degrees), std::cos(headYaw)});
			pose.rightShoulder = pose.chest + (trunk.up * .06 - trunk.lateral * .23) * scale;
			pose.leftShoulder = pose.chest + (trunk.up * .06 + trunk.lateral * .23) * scale;
			const double arm = PitcherArmSegmentMeters * scale;
			pose.rightElbow = pose.rightShoulder + ToWorld(trunk, sample.throwArm.upper) * arm;
			pose.rightHand = pose.rightElbow + ToWorld(trunk, sample.throwArm.fore) * arm;
			pose.leftElbow = pose.leftShoulder + ToWorld(trunk, sample.gloveArm.upper) * arm;
			pose.leftHand = pose.leftElbow + ToWorld(trunk, sample.gloveArm.fore) * arm;
			pose.heldBall = pose.rightHand;
			const auto footForward = [](const double yaw)
			{ return Vector3{-std::sin(yaw * Degrees), 0.0, std::cos(yaw * Degrees)}; };
			pose.rightFootForward = footForward(sample.rightFootYaw);
			pose.leftFootForward = footForward(sample.leftFootYaw);
			// Knees bend over the toes; a foot trailing behind the hip (drive leg, follow-through
			// kick) turns the knee down toward the mound instead. Knees never dip into it.
			const double leg = PitcherLegSegmentMeters * scale;
			const auto kneeHint = [&](const Vector3& hip, const Vector3& foot, const Vector3& forward)
			{
				const double behind = std::clamp(-Physics::Dot(foot - hip, forward) / (.3 * scale), 0.0, 1.0);
				return (forward + Vector3{0., .25, 0.}) * (1.0 - behind) + (forward * .3 + Vector3{0., -1., 0.}) * behind;
			};
			pose.rightKnee = BendJoint(pose.rightHip, pose.rightFoot,
			    kneeHint(pose.rightHip, pose.rightFoot, pose.rightFootForward), leg);
			pose.leftKnee = BendJoint(pose.leftHip, pose.leftFoot,
			    kneeHint(pose.leftHip, pose.leftFoot, pose.leftFootForward), leg);
			pose.rightKnee.y = (std::max)(pose.rightKnee.y, base.y + .04 * scale);
			pose.leftKnee.y = (std::max)(pose.leftKnee.y, base.y + .04 * scale);
			return pose;
		}

		// Stretch delivery with a high leg lift. Phases: set, lift, balance point, drift,
		// stride, foot contact, maximum external rotation, release, maximum internal
		// rotation, follow-through, recovery and fielding position.
		[[nodiscard]] inline const Delivery& OverhandDelivery()
		{
			static constexpr Delivery delivery{{{
			    {-1.20, {-.04, .96, .07}, 88, 90, 6, 0, {.20, -.30, .25}, {-.4, -1, 0}, {-.20, -.28, .27}, {.4, -1, 0},
			        {.00, .07, .33}, {-.10, .07, -.12}, 80, 92, 0, 0},
			    {-.95, {-.06, .96, .02}, 92, 96, 4, 0, {.20, -.27, .25}, {-.4, -1, 0}, {-.20, -.25, .27}, {.4, -1, 0},
			        {-.06, .18, .22}, {-.10, .07, -.12}, 85, 92, 0, 0},
			    {-.72, {-.08, .95, -.04}, 100, 108, 0, -4, {.20, -.22, .24}, {-.4, -1, 0}, {-.19, -.20, .26}, {.4, -1, 0},
			        {-.40, .43, .00}, {-.10, .07, -.12}, 95, 92, 0, 0},
			    {-.50, {-.06, .90, .12}, 98, 106, 2, -8, {.04, -.45, .16}, {-.5, -.8, .2}, {-.06, -.32, .30}, {.4, -1, .2},
			        {-.14, .32, .40}, {-.10, .07, -.12}, 70, 90, 0, .2},
			    {-.30, {-.04, .82, .38}, 75, 100, 4, -8, {-.45, -.25, -.45}, {-.3, .2, -1}, {.40, -.02, .25}, {.1, -1, .2},
			        {.08, .14, .95}, {-.10, .07, -.11}, 30, 80, .1, .8},
			    {-.14, {-.01, .70, .52}, 38, 85, 6, -2, {-.357, .29, .11}, {-1, -.1, -.35}, {.45, .10, .30}, {.2, -.4, 1},
			        {.12, .07, 1.22}, {-.11, .08, -.06}, 15, 55, .4, 1},
			    {-.035, {.02, .74, .66}, -5, 15, 22, 20, {-.377, .131, -.334}, {-1, 0, 0}, {.30, -.10, .32}, {.3, -1, .4},
			        {.12, .07, 1.22}, {-.10, .09, .04}, 15, 25, .9, 1},
			    {0, {.03, .80, .72}, -15, -12, 35, 24, {-.718, .204, .102}, {0, -1, .2}, {.05, -.20, .35}, {.5, -1, .3},
			        {.12, .07, 1.22}, {-.09, .10, .10}, 15, 15, 1, 1},
			    {.03, {.04, .80, .76}, -22, -28, 45, 22, {-.558, .034, .504}, {-1, -.5, -1}, {.00, -.25, .33}, {.5, -1, .3},
			        {.12, .07, 1.22}, {-.08, .12, .16}, 15, 10, .9, 1},
			    {.14, {.06, .76, .86}, -35, -45, 58, 10, {.25, -.55, .35}, {-.3, -.2, 1}, {.05, -.35, .25}, {.5, -1, 0},
			        {.12, .07, 1.22}, {-.14, .48, .34}, 15, 5, .5, 1},
			    {.32, {.04, .80, .95}, -25, -25, 35, 5, {.10, -.60, .20}, {-.2, 0, 1}, {.00, -.30, .30}, {.5, -1, 0},
			        {.12, .07, 1.22}, {-.24, .26, .86}, 15, 0, .2, 1},
			    {.60, {-.02, .86, 1.00}, -5, -3, 15, 0, {.15, -.35, .30}, {-.5, -1, 0}, {-.15, -.25, .35}, {.5, -1, 0},
			        {.16, .07, 1.24}, {-.28, .07, 1.02}, 5, -5, 0, 1}}},
			    {{{-30, 0, 0, 0, 0}, {16, 0, 0, 0, 0}, {26, -30, .20, -.45, 0}}}};
			return delivery;
		}

		// Same lower body with a level shoulder line: the arm comes around horizontally.
		[[nodiscard]] inline const Delivery& SidearmDelivery()
		{
			static constexpr Delivery delivery{{{
			    {-1.20, {-.04, .96, .07}, 88, 90, 6, 0, {.20, -.30, .25}, {-.4, -1, 0}, {-.20, -.28, .27}, {.4, -1, 0},
			        {.00, .07, .33}, {-.10, .07, -.12}, 80, 92, 0, 0},
			    {-.95, {-.06, .96, .02}, 92, 96, 4, 0, {.20, -.27, .25}, {-.4, -1, 0}, {-.20, -.25, .27}, {.4, -1, 0},
			        {-.06, .18, .22}, {-.10, .07, -.12}, 85, 92, 0, 0},
			    {-.72, {-.08, .95, -.04}, 100, 108, 2, -4, {.20, -.22, .24}, {-.4, -1, 0}, {-.19, -.20, .26}, {.4, -1, 0},
			        {-.40, .43, .00}, {-.10, .07, -.12}, 95, 92, 0, 0},
			    {-.50, {-.06, .89, .12}, 98, 106, 4, -8, {.04, -.45, .16}, {-.5, -.8, .2}, {-.06, -.32, .30}, {.4, -1, .2},
			        {-.14, .32, .42}, {-.10, .07, -.12}, 70, 90, 0, .2},
			    {-.30, {-.04, .80, .40}, 75, 100, 8, -10, {-.48, -.30, -.40}, {-.3, .2, -1}, {.40, -.05, .25}, {.1, -1, .2},
			        {.08, .14, .98}, {-.10, .07, -.11}, 30, 80, .2, .8},
			    {-.14, {-.01, .68, .54}, 38, 85, 10, -6, {-.36, .12, -.08}, {-1, -.2, -.3}, {.45, .02, .30}, {.2, -.5, 1},
			        {.13, .07, 1.25}, {-.11, .08, -.05}, 15, 55, .5, 1},
			    {-.045, {.02, .71, .68}, -5, 15, 18, 2, {-.38, .04, -.33}, {-1, 0, 0}, {.30, -.12, .32}, {.3, -1, .4},
			        {.13, .07, 1.25}, {-.10, .09, .05}, 15, 25, .9, 1},
			    {0, {.03, .74, .74}, -15, -12, 28, -2, {-.70, .05, .20}, {0, -.2, -1}, {.05, -.22, .35}, {.5, -1, .3},
			        {.13, .07, 1.25}, {-.09, .10, .11}, 15, 15, 1, 1},
			    {.03, {.04, .74, .78}, -22, -30, 36, -2, {-.50, -.05, .50}, {-.5, 0, -1}, {.00, -.26, .33}, {.5, -1, .3},
			        {.13, .07, 1.25}, {-.08, .12, .17}, 15, 10, .9, 1},
			    {.14, {.06, .72, .88}, -35, -48, 48, 0, {.20, -.40, .40}, {-.3, -.2, 1}, {.05, -.35, .25}, {.5, -1, 0},
			        {.13, .07, 1.25}, {-.14, .46, .36}, 15, 5, .5, 1},
			    {.32, {.04, .79, .97}, -25, -25, 30, 0, {.10, -.60, .20}, {-.2, 0, 1}, {.00, -.30, .30}, {.5, -1, 0},
			        {.13, .07, 1.25}, {-.24, .26, .88}, 15, 0, .2, 1},
			    {.60, {-.02, .86, 1.02}, -5, -3, 15, 0, {.15, -.35, .30}, {-.5, -1, 0}, {-.15, -.25, .35}, {.5, -1, 0},
			        {.17, .07, 1.27}, {-.28, .07, 1.04}, 5, -5, 0, 1}}},
			    {{{-28, 10, -.12, .05, 0}, {0, 0, 0, 0, 0}, {14, -6, .03, 0, 0}}}};
			return delivery;
		}

		// Long stride with a deep drive-leg bend: the trunk tilts hard to the arm side and
		// the arm sweeps under, releasing just above the mound in front of the stride knee.
		[[nodiscard]] inline const Delivery& SubmarineDelivery()
		{
			static constexpr Delivery delivery{{{
			    {-1.20, {-.04, .96, .07}, 88, 90, 6, 0, {.20, -.30, .25}, {-.4, -1, 0}, {-.20, -.28, .27}, {.4, -1, 0},
			        {.00, .07, .33}, {-.10, .07, -.12}, 80, 92, 0, 0},
			    {-.95, {-.06, .95, .02}, 92, 96, 6, 0, {.20, -.27, .25}, {-.4, -1, 0}, {-.20, -.25, .27}, {.4, -1, 0},
			        {-.06, .17, .22}, {-.10, .07, -.12}, 85, 92, 0, 0},
			    {-.72, {-.08, .93, -.04}, 100, 108, 4, -4, {.20, -.24, .24}, {-.4, -1, 0}, {-.19, -.22, .26}, {.4, -1, 0},
			        {-.38, .38, .02}, {-.10, .07, -.12}, 95, 92, 0, 0},
			    {-.50, {-.06, .86, .14}, 96, 106, 10, -10, {.02, -.48, .15}, {-.5, -.6, .3}, {-.05, -.32, .30}, {.4, -1, .2},
			        {-.12, .28, .46}, {-.10, .07, -.12}, 70, 90, .1, .2},
			    {-.30, {-.04, .72, .44}, 72, 100, 18, -14, {-.35, -.40, -.40}, {-.4, 0, -1}, {.38, -.10, .25}, {.1, -1, .2},
			        {.10, .13, 1.05}, {-.10, .07, -.10}, 30, 80, .3, .8},
			    {-.14, {.00, .58, .60}, 35, 85, 30, -22, {-.42, -.05, -.20}, {-1, -.2, -.3}, {.48, .02, .25}, {.2, -.5, 1},
			        {.15, .07, 1.32}, {-.11, .08, -.04}, 15, 55, .6, 1},
			    {-.045, {.02, .48, .74}, -5, 5, 52, -38, {-.40, -.10, -.33}, {-1, 0, 0}, {.32, -.10, .32}, {.3, -1, .4},
			        {.15, .07, 1.32}, {-.10, .08, .06}, 15, 25, .9, 1},
			    {0, {.03, .44, .80}, -18, -25, 66, -40, {-.66, -.12, .30}, {-1, .2, .2}, {.10, -.20, .35}, {.5, -1, .3},
			        {.15, .07, 1.32}, {-.09, .08, .12}, 15, 15, 1, 1},
			    {.03, {.04, .44, .84}, -24, -38, 68, -34, {-.45, -.10, .55}, {-.8, .3, .4}, {.05, -.25, .33}, {.5, -1, .3},
			        {.15, .07, 1.32}, {-.08, .09, .18}, 15, 10, .9, 1},
			    {.14, {.06, .52, .92}, -32, -48, 55, -15, {.05, .05, .55}, {-.5, -.5, .5}, {.02, -.35, .25}, {.5, -1, 0},
			        {.15, .07, 1.32}, {-.14, .38, .46}, 15, 5, .5, 1},
			    {.32, {.04, .70, 1.00}, -20, -25, 30, -5, {.05, -.45, .30}, {-.3, -.3, .8}, {.00, -.30, .30}, {.5, -1, 0},
			        {.15, .07, 1.32}, {-.24, .24, .94}, 15, 0, .2, 1},
			    {.60, {-.02, .86, 1.08}, -5, -3, 12, 0, {.15, -.35, .30}, {-.5, -1, 0}, {-.15, -.25, .35}, {.5, -1, 0},
			        {.18, .07, 1.33}, {-.28, .07, 1.10}, 5, -5, 0, 1}}},
			    {{{-14, 8, -.08, 0, 0}, {0, 0, 0, 0, 0}, {16, -14, .12, 0, 0}}}};
			return delivery;
		}

		// Batting-practice toss facing the target: a short step with a pendulum arm swing,
		// a pause at the top of the backswing and a follow-through toward the target.
		[[nodiscard]] inline const Delivery& UnderhandTossDelivery()
		{
			static constexpr Delivery delivery{{{
			    {-1.20, {-.02, .97, .03}, 12, 12, 8, 0, {.18, -.48, .22}, {-.3, -1, 0}, {-.18, -.46, .22}, {.3, -1, 0},
			        {.15, .07, .12}, {-.18, .07, -.05}, 5, 10, 0, 0},
			    {-.95, {-.02, .97, .02}, 12, 12, 8, 0, {.17, -.48, .22}, {-.3, -1, 0}, {-.18, -.46, .22}, {.3, -1, 0},
			        {.15, .07, .12}, {-.18, .07, -.05}, 5, 10, 0, 0},
			    {-.70, {-.03, .96, .00}, 14, 15, 9, 0, {.05, -.62, .16}, {-.3, -1, .2}, {-.12, -.40, .30}, {.3, -1, .2},
			        {.15, .07, .12}, {-.18, .07, -.05}, 5, 10, 0, 0},
			    {-.45, {-.04, .96, -.02}, 18, 20, 10, -2, {-.02, -.73, .02}, {-.1, 0, -1}, {-.04, -.30, .38}, {.3, -1, .2},
			        {.15, .07, .12}, {-.18, .07, -.05}, 5, 10, 0, 0},
			    {-.25, {-.04, .96, .02}, 22, 25, 11, -4, {-.04, -.66, -.32}, {-.1, 0, -1}, {.02, -.22, .42}, {.3, -1, .2},
			        {.14, .16, .32}, {-.18, .07, -.05}, 3, 10, 0, 0},
			    {-.12, {-.03, .95, .12}, 20, 22, 12, -4, {-.06, -.50, -.52}, {-.1, 0, -1}, {.05, -.18, .44}, {.3, -1, .2},
			        {.13, .10, .50}, {-.18, .07, -.05}, 2, 10, 0, 0},
			    {-.06, {-.02, .94, .18}, 14, 15, 13, -3, {-.06, -.46, -.56}, {-.1, 0, -1}, {.05, -.20, .42}, {.3, -1, .2},
			        {.12, .07, .58}, {-.18, .07, -.05}, 0, 10, 0, 0},
			    {0, {.00, .93, .26}, 5, 5, 15, 0, {-.05, -.66, .32}, {-.1, 0, -1}, {.00, -.30, .36}, {.3, -1, .2},
			        {.12, .07, .58}, {-.18, .07, -.05}, 0, 10, 1, 0},
			    {.10, {.00, .93, .31}, 0, -2, 12, 2, {-.05, -.35, .64}, {-.1, -.3, -1}, {-.05, -.35, .32}, {.3, -1, .2},
			        {.12, .07, .58}, {-.18, .07, -.05}, 0, 10, .5, 0},
			    {.22, {.00, .94, .33}, -3, -5, 10, 1, {-.04, -.15, .70}, {-.1, -1, -.3}, {-.08, -.37, .30}, {.3, -1, .2},
			        {.12, .07, .58}, {-.18, .07, -.05}, 0, 10, .1, 0},
			    {.40, {-.01, .95, .33}, -2, -3, 9, 0, {.04, -.38, .50}, {-.2, -1, 0}, {-.12, -.39, .28}, {.3, -1, 0},
			        {.12, .07, .58}, {-.17, .11, .12}, 0, 5, 0, 0},
			    {.60, {-.02, .96, .30}, 0, 0, 8, 0, {.12, -.48, .26}, {-.3, -1, 0}, {-.18, -.40, .26}, {.3, -1, 0},
			        {.12, .07, .58}, {-.17, .07, .22}, 0, 0, 0, 0}}},
			    {{{0, 0, 0, 0, -10}, {0, 0, 0, 0, 10}, {0, 0, 0, 0, 50}}}};
			return delivery;
		}

		[[nodiscard]] inline const Delivery& DeliveryFor(const PitcherDeliveryStyle style)
		{
			switch (style)
			{
			case PitcherDeliveryStyle::UnderhandToss: return UnderhandTossDelivery();
			case PitcherDeliveryStyle::Submarine: return SubmarineDelivery();
			case PitcherDeliveryStyle::Sidearm: return SidearmDelivery();
			case PitcherDeliveryStyle::Overhand: break;
			}
			return OverhandDelivery();
		}
	} // namespace PitcherMotion

	[[nodiscard]] inline PitcherPose BuildPitcherPose(const PitcherPresentationState& state)
	{
		using Physics::Vector3;
		using namespace PitcherMotion;
		const auto style = GetPitcherDeliveryStyle(state);
		const Delivery& delivery = DeliveryFor(style);
		const auto& keys = delivery.keys;
		// Keep windup start and release fixed on the delivery clock. Reallocate
		// windup time so the final forward swing and follow-through use the
		// preset's fastest-pitch tempo, independent of the current pitch type.
		const double rawSpeedScale = std::isfinite(state.armSpeedScale) ? state.armSpeedScale : 1.0;
		const double speedScale = std::clamp(rawSpeedScale, .25, 2.0);
		const double swingStart = -.12 / speedScale;
		const double rawSeconds = std::isfinite(state.secondsFromRelease) ? state.secondsFromRelease : 0.0;
		const double clockTime = std::clamp(
		    rawSeconds, keys.front().time, keys.back().time / speedScale);
		const double time =
		    speedScale == 1.0
		        ? clockTime
		        : (clockTime >= swingStart
		                  ? clockTime * speedScale
		                  : keys.front().time + (clockTime - keys.front().time) *
		                                            (-.12 - keys.front().time) /
		                                            (swingStart - keys.front().time));

		// Fit the arm slot to the configured release height: the trunk tilt, knee bend and
		// stride (or the toss arm angle) are solved so the hand reaches it at release.
		const double releaseHeight = state.releasePositionWorld.y - state.moundHeightMeters;
		const double scale = style == PitcherDeliveryStyle::Overhand ? std::clamp(releaseHeight / 1.82, 1., 1.2) : 1.;
		const Vector3 groundBase{0.0, state.moundHeightMeters, 0.0};
		const auto releaseHand = [&](const double slot)
		{ return ComposePose(SampleAt(Adjust(delivery, slot), 0.0), groundBase, scale).rightHand; };
		const double target = state.releasePositionWorld.y;
		// The fit depends only on the form and release height, so per-frame evaluation reuses it.
		struct SlotFit final
		{
			PitcherDeliveryStyle style;
			double target, mound, slot;
		};
		static thread_local std::optional<SlotFit> lastFit;
		double slot = 0.0;
		if (lastFit && lastFit->style == style && lastFit->target == target &&
		    lastFit->mound == state.moundHeightMeters)
		{
			slot = lastFit->slot;
		}
		else
		{
			double low = 0.0, high = 2.0;
			slot = target <= releaseHand(low).y ? low : (target >= releaseHand(high).y ? high : -1.0);
			if (slot < 0.0)
			{
				for (int iteration = 0; iteration < 60; ++iteration)
				{
					const double middle = .5 * (low + high);
					(releaseHand(middle).y < target ? low : high) = middle;
				}
				slot = .5 * (low + high);
			}
			lastFit = SlotFit{style, target, state.moundHeightMeters, slot};
		}
		const AdjustedKeys adjusted = Adjust(delivery, slot);
		const Vector3 release = ComposePose(SampleAt(adjusted, 0.0), groundBase, scale).rightHand;
		const Vector3 base{state.releasePositionWorld.x - release.x, state.moundHeightMeters,
		    state.releasePositionWorld.z - release.z};
		Sample sample = SampleAt(adjusted, time);
		const double recovery =
		    std::isfinite(state.recoveryProgress) ? std::clamp(state.recoveryProgress, 0.0, 1.0) : 0.0;
		if (recovery > 0.0)
		{
			sample = Recover(sample, SampleAt(adjusted, keys.front().time), recovery);
		}
		PitcherPose pose = ComposePose(sample, base, scale);
		// Heights beyond the fitted range carry their remaining error on the hand around
		// release; within range this only removes rounding.
		const double residual = target - release.y;
		const double nearRelease = std::clamp(1.0 - std::abs(time) / .35, 0.0, 1.0);
		const double weight = nearRelease * nearRelease * (3.0 - 2.0 * nearRelease);
		if (residual != 0.0 && weight > 0.0)
		{
			const double arm = PitcherArmSegmentMeters * pose.scale;
			Vector3 hand = pose.rightHand + Vector3{0.0, residual * weight, 0.0};
			if (std::abs(residual * weight) > 1e-12)
			{
				const Vector3 delta = hand - pose.rightShoulder;
				const double reach = 2. * arm - 1e-6;
				if (Physics::Length(delta) > reach) hand = pose.rightShoulder + delta * (reach / Physics::Length(delta));
				pose.rightElbow = BendJoint(pose.rightShoulder, hand,
				    pose.rightElbow - (pose.rightShoulder + hand) * .5, arm);
			}
			pose.rightHand = hand;
			pose.heldBall = hand;
		}
		if (state.throwsLeftHanded)
		{
			for (auto* point : {&pose.pelvis, &pose.chest, &pose.head, &pose.rightShoulder,
			         &pose.rightElbow, &pose.rightHand, &pose.leftShoulder, &pose.leftElbow,
			         &pose.leftHand, &pose.rightHip, &pose.rightKnee, &pose.rightFoot,
			         &pose.leftHip, &pose.leftKnee, &pose.leftFoot, &pose.heldBall, &pose.moundOrigin})
			{
				point->x = 2.0 * state.releasePositionWorld.x - point->x;
			}
			std::swap(pose.rightShoulder, pose.leftShoulder);
			std::swap(pose.rightElbow, pose.leftElbow);
			std::swap(pose.rightHand, pose.leftHand);
			std::swap(pose.rightHip, pose.leftHip);
			std::swap(pose.rightKnee, pose.leftKnee);
			std::swap(pose.rightFoot, pose.leftFoot);
			pose.pelvisLateral.y = -pose.pelvisLateral.y;
			pose.pelvisLateral.z = -pose.pelvisLateral.z;
			pose.chestLateral.y = -pose.chestLateral.y;
			pose.chestLateral.z = -pose.chestLateral.z;
			pose.headForward.x = -pose.headForward.x;
			pose.rightFootForward.x = -pose.rightFootForward.x;
			pose.leftFootForward.x = -pose.leftFootForward.x;
			std::swap(pose.rightFootForward, pose.leftFootForward);
		}
		return pose;
	}

	// Cache fixed tessellation angles.
	template <std::size_t Segments>
	[[nodiscard]] inline const std::array<std::array<double, 2>, Segments + 1>& CircleTrigTable()
	{
		static const auto table = []
		{
			std::array<std::array<double, 2>, Segments + 1> result;
			for (std::size_t n = 0; n <= Segments; ++n)
			{
				const double angle = 2 * std::numbers::pi * static_cast<double>(n) / Segments;
				result[n] = {std::cos(angle), std::sin(angle)};
			}
			return result;
		}();
		return table;
	}
	inline void BuildPitcherWorldMesh(const PitcherPresentationState& state, IndexedWorldMesh& mesh)
	{
		mesh.vertices.clear();
		mesh.triangles.clear();
		if (!state.visible)
		{
			return;
		}
		mesh.vertices.reserve(5400);
		mesh.triangles.reserve(8600);
		using Physics::Vector3;
		using Color = std::array<float, 4>;
		auto canonicalState = state;
		canonicalState.throwsLeftHanded = false;
		const auto pose = BuildPitcherPose(canonicalState);
		const double s = pose.scale;
		constexpr Color uniform{.91F, .94F, .98F, 1}, uniformTrim{.80F, .85F, .92F, 1},
		    navy{.045F, .115F, .265F, 1}, navyDark{.025F, .065F, .155F, 1},
		    red{.82F, .11F, .16F, 1}, gold{.88F, .72F, .24F, 1},
		    skin{.82F, .56F, .39F, 1}, skinShade{.72F, .46F, .31F, 1},
		    leather{.38F, .18F, .07F, 1}, leatherDark{.24F, .105F, .04F, 1},
		    eyeWhite{.96F, .97F, .99F, 1}, ball{1, .98F, .87F, 1};
		const auto vertex = [&](const Vector3& p)
		{
			const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
			auto position = p;
			if (state.throwsLeftHanded)
			{
				position.x = 2.0 * state.releasePositionWorld.x - position.x;
			}
			mesh.vertices.push_back(position);
			return index;
		};
		const Vector3 keyLight = Physics::NormalizeOrZero(Vector3{-.34, .74, .58});
		const Vector3 fillLight = Physics::NormalizeOrZero(Vector3{.42, .24, .88});
		const Vector3 halfVec = Physics::NormalizeOrZero(keyLight + Vector3{0.0, 0.15, 0.99});
		const auto surface =
		    [&](std::uint32_t i, std::uint32_t j, std::uint32_t k, const Color& color,
		        double specularity = 0.02)
		{
			// Reflection reverses orientation. Restore outward winding before
			// computing world-light shading for the reflected surface.
			if (state.throwsLeftHanded)
			{
				std::swap(j, k);
			}
			const auto& a = mesh.vertices[i];
			const auto& b = mesh.vertices[j];
			const auto& c = mesh.vertices[k];
			const auto normal = Physics::NormalizeOrZero(Physics::Cross(b - a, c - a));
			const double key = (std::max)(0.0, Physics::Dot(normal, keyLight));
			const double fill = (std::max)(0.0, Physics::Dot(normal, fillLight));
			const double hemi = 0.5 + 0.5 * normal.y;
			const double light = 0.34 + 0.44 * key + 0.12 * fill + 0.10 * hemi;
			const double spec = specularity *
			    std::pow((std::max)(0.0, Physics::Dot(normal, halfVec)), 18.0);
			const auto channel = [&](float base)
			{
				return std::clamp(
				    static_cast<float>(base * light + spec), 0.0F, 1.0F);
			};
			const Color shaded{channel(color[0]), channel(color[1]), channel(color[2]), color[3]};
			mesh.triangles.push_back({{i, j, k}, shaded});
		};
		const auto orientedEllipsoid = [&](const Vector3& center, const Vector3& rightAxis,
		                                   const Vector3& upAxis, const Vector3& forwardAxis,
		                                   const Vector3& radius, const Color& color,
		                                   double specularity = 0.02)
		{
			constexpr int slices = 12, rings = 8;
			static const auto latitude = []
			{
				std::array<std::array<double, 2>, rings + 1> result;
				for (int r = 0; r <= rings; ++r)
				{
					const double angle = -std::numbers::pi / 2 + std::numbers::pi * r / rings;
					result[r] = {std::cos(angle), std::sin(angle)};
				}
				return result;
			}();
			const auto& longitude = CircleTrigTable<slices>();
			std::array<std::uint32_t, (rings + 1) * (slices + 1)> indices;
			for (int r = 0; r <= rings; ++r)
			{
				for (int n = 0; n <= slices; ++n)
				{
					const Vector3 local{
					    radius.x * latitude[r][0] * longitude[n][0],
					    radius.y * latitude[r][1],
					    radius.z * latitude[r][0] * longitude[n][1]};
					indices[r * (slices + 1) + n] = vertex(
					    center + rightAxis * local.x + upAxis * local.y + forwardAxis * local.z);
				}
			}
			for (int r = 0; r < rings; ++r)
			{
				for (int n = 0; n < slices; ++n)
				{
					const auto a = indices[r * (slices + 1) + n],
					           b = indices[(r + 1) * (slices + 1) + n];
					const auto c = indices[(r + 1) * (slices + 1) + n + 1],
					           d = indices[r * (slices + 1) + n + 1];
					surface(a, b, c, color, specularity);
					surface(a, c, d, color, specularity);
				}
			}
		};
		const auto ellipsoid = [&](const Vector3& center, const Vector3& radius, const Color& color,
		                           double specularity = 0.02)
		{
			orientedEllipsoid(center, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, radius, color, specularity);
		};
		// Multi-ring limb tube with outward normals and no internal joint end-caps.
		const auto profileLimb = [&]<std::size_t Rings>(const Vector3& start, const Vector3& end,
		                             const std::array<double, Rings>& fractions,
		                             const std::array<double, Rings>& radii,
		                             const std::array<Color, Rings - 1>& colors,
		                             bool capEnds = false, double specularity = 0.02)
		{
			const auto axis = Physics::NormalizeOrZero(end - start);
			const auto u = Physics::NormalizeOrZero(
			    Physics::Cross(axis, std::abs(axis.y) < .9 ? Vector3{0, 1, 0} : Vector3{1, 0, 0}));
			const auto v = Physics::Cross(axis, u);
			const auto& angles = CircleTrigTable<12>();
			std::array<std::array<std::uint32_t, 13>, Rings> ringIndices;
			for (std::size_t r = 0; r < Rings; ++r)
			{
				const Vector3 ringCenter = start + (end - start) * fractions[r];
				const double radius = radii[r];
				for (std::size_t n = 0; n < angles.size(); ++n)
				{
					const auto radial = u * angles[n][0] + v * angles[n][1];
					ringIndices[r][n] = vertex(ringCenter + radial * radius);
				}
			}
			for (std::size_t r = 0; r + 1 < Rings; ++r)
			{
				const auto& col = colors[r];
				for (int n = 0; n < 12; ++n)
				{
					const auto a = ringIndices[r][n], b = ringIndices[r + 1][n];
					const auto c = ringIndices[r + 1][n + 1], d = ringIndices[r][n + 1];
					surface(a, d, c, col, specularity);
					surface(a, c, b, col, specularity);
				}
			}
			if (capEnds)
			{
				const auto first = vertex(start), last = vertex(end);
				for (int n = 0; n < 12; ++n)
				{
					surface(first, ringIndices[0][n + 1], ringIndices[0][n], colors.front(), specularity);
					surface(last, ringIndices[Rings - 1][n], ringIndices[Rings - 1][n + 1], colors.back(), specularity);
				}
			}
		};
		const auto cappedCapsule = [&](const Vector3& start, const Vector3& end, double radiusStart,
		                               double radiusEnd, const Color& color, double specularity = 0.02)
		{
			profileLimb(start, end, std::array{0.0, 0.5, 1.0},
			    std::array{radiusStart, 0.5 * (radiusStart + radiusEnd), radiusEnd},
			    std::array{color, color}, true, specularity);
		};

		// 1. Contoured pitcher's mound & 3D pitching rubber aligned with the pitcher's stance.
		const Vector3 moundCenter = pose.moundOrigin;
		constexpr Color dirtTop{.43F, .28F, .15F, 1}, dirtSlope{.39F, .25F, .13F, 1},
		    dirtApron{.35F, .22F, .115F, 1}, rubber{.92F, .91F, .84F, 1};
		const double outerY = (std::min)(state.moundHeightMeters, .002);
		const double plateauY = (std::max)(state.moundHeightMeters, outerY);
		const double r1 = 0.52 * s;
		const double r2 = (std::max)(1.20, 1.12 * s);
		const double r3 = (std::max)(1.95, r2 + 0.45);
		const double r4 = (std::max)(2.74, r2 + 0.90);
		const double y1 = plateauY;
		const double y2 = outerY + (plateauY - outerY) * 0.88;
		const double y3 = outerY + (plateauY - outerY) * 0.36;
		const double y4 = outerY;
		const auto& moundAngles = CircleTrigTable<64>();
		std::array<std::uint32_t, 65> ring1, ring2, ring3, ring4;
		const auto moundApex = vertex({moundCenter.x, plateauY, moundCenter.z});
		for (std::size_t n = 0; n < moundAngles.size(); ++n)
		{
			const double cx = moundAngles[n][0], sz = moundAngles[n][1];
			ring1[n] = vertex({moundCenter.x + cx * r1, y1, moundCenter.z + sz * r1});
			ring2[n] = vertex({moundCenter.x + cx * r2, y2, moundCenter.z + sz * r2});
			ring3[n] = vertex({moundCenter.x + cx * r3, y3, moundCenter.z + sz * r3});
			ring4[n] = vertex({moundCenter.x + cx * r4, y4, moundCenter.z + sz * r4});
		}
		for (int n = 0; n < 64; ++n)
		{
			surface(moundApex, ring1[n + 1], ring1[n], dirtTop, 0.0);
			surface(ring1[n], ring1[n + 1], ring2[n + 1], dirtTop, 0.0);
			surface(ring1[n], ring2[n + 1], ring2[n], dirtTop, 0.0);
			surface(ring2[n], ring2[n + 1], ring3[n + 1], dirtSlope, 0.0);
			surface(ring2[n], ring3[n + 1], ring3[n], dirtSlope, 0.0);
			surface(ring3[n], ring3[n + 1], ring4[n + 1], dirtApron, 0.0);
			surface(ring3[n], ring4[n + 1], ring4[n], dirtApron, 0.0);
		}
		const Vector3 plateCenter{moundCenter.x - 0.08 * s, plateauY, moundCenter.z - 0.18 * s};
		const double plateTopY = plateauY + 0.014;
		const auto pT0 = vertex({plateCenter.x - .305, plateTopY, plateCenter.z - .076});
		const auto pT1 = vertex({plateCenter.x + .305, plateTopY, plateCenter.z - .076});
		const auto pT2 = vertex({plateCenter.x + .305, plateTopY, plateCenter.z + .076});
		const auto pT3 = vertex({plateCenter.x - .305, plateTopY, plateCenter.z + .076});
		const auto pB0 = vertex({plateCenter.x - .312, plateauY, plateCenter.z - .082});
		const auto pB1 = vertex({plateCenter.x + .312, plateauY, plateCenter.z - .082});
		const auto pB2 = vertex({plateCenter.x + .312, plateauY, plateCenter.z + .082});
		const auto pB3 = vertex({plateCenter.x - .312, plateauY, plateCenter.z + .082});
		surface(pT0, pT2, pT1, rubber, 0.04);
		surface(pT0, pT3, pT2, rubber, 0.04);
		surface(pB0, pT0, pT1, rubber, 0.02);
		surface(pB0, pT1, pB1, rubber, 0.02);
		surface(pB1, pT1, pT2, rubber, 0.02);
		surface(pB1, pT2, pB2, rubber, 0.02);
		surface(pB2, pT2, pT3, rubber, 0.02);
		surface(pB2, pT3, pB3, rubber, 0.02);
		surface(pB3, pT3, pT0, rubber, 0.02);
		surface(pB3, pT0, pB0, rubber, 0.02);

		// 2. Anatomical elliptical torso, pelvis saddle, belt & jersey collar.
		const auto torsoUp = Physics::NormalizeOrZero(pose.chest - pose.pelvis);
		const auto pelvisLateral = pose.pelvisLateral;
		const auto pelvisForward = Physics::NormalizeOrZero(Physics::Cross(pelvisLateral, torsoUp));
		const auto chestLateral = pose.chestLateral;
		const auto chestForward = Physics::NormalizeOrZero(Physics::Cross(chestLateral, torsoUp));
		orientedEllipsoid(pose.pelvis - torsoUp * (.025 * s), pelvisLateral, torsoUp, pelvisForward,
		    {.208 * s, .128 * s, .118 * s}, uniform, 0.02);

		struct TorsoSection final
		{
			double heightScale;
			double halfWidth;
			double halfDepth;
			double yawBlend;
			Color color;
		};
		static constexpr std::array<TorsoSection, 8> torsoSections{{
		    {-.02, .202, .116, 0.0, uniform},
		    {.05, .182, .116, 0.05, navy},
		    {.12, .182, .116, 0.12, navy},
		    {.15, .176, .110, 0.18, uniform},
		    {.48, .192, .118, 0.55, uniform},
		    {.82, .218, .125, 0.90, uniform},
		    {1.00, .226, .116, 1.0, uniform},
		    {1.08, .096, .084, 1.0, uniformTrim}}};
		const auto& torsoAngles = CircleTrigTable<16>();
		std::array<std::array<std::uint32_t, 17>, torsoSections.size()> torsoRings;
		const double spineLength = 0.53 * s;
		for (std::size_t r = 0; r < torsoSections.size(); ++r)
		{
			const auto& sec = torsoSections[r];
			const Vector3 center = pose.pelvis + torsoUp * (sec.heightScale * spineLength);
			const auto ringLat = Physics::NormalizeOrZero(
			    pelvisLateral * (1.0 - sec.yawBlend) + chestLateral * sec.yawBlend);
			const auto ringFwd = Physics::NormalizeOrZero(Physics::Cross(ringLat, torsoUp));
			for (std::size_t n = 0; n < torsoAngles.size(); ++n)
			{
				const Vector3 offset = ringLat * (sec.halfWidth * s * torsoAngles[n][0]) +
				                       ringFwd * (sec.halfDepth * s * torsoAngles[n][1]);
				torsoRings[r][n] = vertex(center + offset);
			}
		}
		for (std::size_t r = 0; r + 1 < torsoSections.size(); ++r)
		{
			const Color& ringColor = torsoSections[r + 1].color == navy && torsoSections[r].color == navy
			    ? navy : uniform;
			const double spec = ringColor == navy ? 0.08 : 0.02;
			for (int n = 0; n < 16; ++n)
			{
				const auto a = torsoRings[r][n], b = torsoRings[r + 1][n];
				const auto c = torsoRings[r + 1][n + 1], d = torsoRings[r][n + 1];
				surface(a, b, c, ringColor, spec);
				surface(a, c, d, ringColor, spec);
			}
		}
		// Belt buckle on front waist.
		const Vector3 buckleCenter =
		    pose.pelvis + torsoUp * (.085 * spineLength) + pelvisForward * (.118 * s);
		orientedEllipsoid(buckleCenter, pelvisLateral, torsoUp, pelvisForward,
		    {.030 * s, .022 * s, .009 * s}, gold, 0.18);

		// Jersey center placket piping and chest crest oriented with the chest.
		const Vector3 placketBottom =
		    pose.pelvis + torsoUp * (.18 * spineLength) + chestForward * (.116 * s);
		const Vector3 placketTop =
		    pose.pelvis + torsoUp * (1.02 * spineLength) + chestForward * (.116 * s);
		cappedCapsule(placketBottom, placketTop, .006 * s, .006 * s, red, 0.03);
		const Vector3 crest = pose.chest + chestLateral * (.088 * s) - torsoUp * (.065 * s) +
		                      chestForward * (.126 * s);
		cappedCapsule(crest - chestLateral * (.026 * s) - torsoUp * (.048 * s),
		    crest - chestLateral * (.026 * s) + torsoUp * (.048 * s), .009 * s, .009 * s, red, 0.04);
		cappedCapsule(crest - chestLateral * (.026 * s) + torsoUp * (.042 * s),
		    crest + chestLateral * (.028 * s) + torsoUp * (.018 * s), .009 * s, .009 * s, red, 0.04);
		cappedCapsule(crest - chestLateral * (.026 * s) - torsoUp * (.042 * s),
		    crest + chestLateral * (.028 * s) - torsoUp * (.015 * s), .009 * s, .009 * s, red, 0.04);

		// 3. Legs, stirrup socks, cleats, shoulders, jersey sleeves & forearms.
		for (int side = 0; side < 2; ++side)
		{
			const auto hip = side ? pose.leftHip : pose.rightHip;
			const auto knee = side ? pose.leftKnee : pose.rightKnee;
			const auto foot = side ? pose.leftFoot : pose.rightFoot;
			const auto footFwd = side ? pose.leftFootForward : pose.rightFootForward;
			const Vector3 footUp{0.0, 1.0, 0.0};
			const auto footRight = Physics::NormalizeOrZero(Physics::Cross(footUp, footFwd));

			// Thigh with muscular quad taper.
			orientedEllipsoid(hip, pelvisLateral, torsoUp, pelvisForward,
			    {.096 * s, .092 * s, .095 * s}, uniform, 0.02);
			profileLimb(hip, knee,
			    std::array{0.0, 0.35, 0.72, 1.0},
			    std::array{.102 * s, .100 * s, .088 * s, .077 * s},
			    std::array{uniform, uniform, uniform}, false, 0.02);
			ellipsoid(knee, {.076 * s, .079 * s, .076 * s}, uniform, 0.02);

			// Lower leg: baseball pant leg tapering to red/navy stirrup sock.
			profileLimb(knee, foot,
			    std::array{0.0, 0.30, 0.66, 0.73, 0.80, 1.0},
			    std::array{.075 * s, .079 * s, .064 * s, .056 * s, .054 * s, .048 * s},
			    std::array{uniform, uniform, uniformTrim, red, navy}, false, 0.03);

			// Oriented baseball cleat with outsole & toe box.
			const Vector3 cleatCenter = foot + footFwd * (.042 * s) - footUp * (.016 * s);
			orientedEllipsoid(cleatCenter, footRight, footUp, footFwd,
			    {.062 * s, .044 * s, .128 * s}, navy, 0.09);
			orientedEllipsoid(cleatCenter - footUp * (.018 * s) + footFwd * (.006 * s),
			    footRight, footUp, footFwd,
			    {.065 * s, .018 * s, .132 * s}, uniform, 0.04);

			// Shoulder deltoid, jersey sleeve with trim, navy undershirt & forearm.
			const auto shoulder = side ? pose.leftShoulder : pose.rightShoulder;
			const auto elbow = side ? pose.leftElbow : pose.rightElbow;
			const auto hand = side ? pose.leftHand : pose.rightHand;
			const auto upperArmUp = Physics::NormalizeOrZero(shoulder - elbow);
			const auto upperArmRight = Physics::NormalizeOrZero(
			    Physics::Cross(upperArmUp, chestForward));
			const auto upperArmFwd = Physics::NormalizeOrZero(
			    Physics::Cross(upperArmRight, upperArmUp));
			orientedEllipsoid(shoulder - upperArmUp * (.012 * s), upperArmRight, upperArmUp, upperArmFwd,
			    {.084 * s, .092 * s, .084 * s}, uniform, 0.02);
			profileLimb(shoulder, elbow,
			    std::array{0.0, 0.48, 0.56, 0.62, 1.0},
			    std::array{.082 * s, .076 * s, .077 * s, .067 * s, .060 * s},
			    std::array{uniform, red, navy, navy}, false, 0.03);
			ellipsoid(elbow, {.059 * s, .059 * s, .059 * s}, navy, 0.03);
			profileLimb(elbow, hand,
			    std::array{0.0, 0.28, 0.68, 1.0},
			    std::array{.057 * s, .061 * s, .049 * s, .039 * s},
			    std::array{skin, skin, skin}, false, 0.03);
		}

		// 4. Oriented baseball mitt on leftHand with upright fielding/pitching posture.
		const auto forearmDir = Physics::NormalizeOrZero(pose.leftHand - pose.leftElbow);
		const auto gloveUp = Physics::NormalizeOrZero(
		    torsoUp * 0.72 + Vector3{0.0, 0.45, 0.0} + forearmDir * 0.35);
		auto gloveFace = Physics::NormalizeOrZero(
		    chestForward * 0.75 + forearmDir * 0.35 - chestLateral * 0.20);
		auto gloveRight = Physics::NormalizeOrZero(Physics::Cross(gloveUp, gloveFace));
		if (Physics::Length(gloveRight) < 1e-6)
		{
			gloveRight = chestLateral;
		}
		gloveFace = Physics::NormalizeOrZero(Physics::Cross(gloveRight, gloveUp));
		const Vector3 mittCenter = pose.leftHand + forearmDir * (.020 * s) + gloveUp * (.015 * s);
		orientedEllipsoid(mittCenter, gloveRight, gloveUp, gloveFace,
		    {.088 * s, .092 * s, .046 * s}, leather, 0.08);
		orientedEllipsoid(mittCenter - gloveFace * (.016 * s) + gloveUp * (.008 * s),
		    gloveRight, gloveUp, gloveFace,
		    {.066 * s, .070 * s, .030 * s}, leatherDark, 0.04);
		for (int finger = 0; finger < 4; ++finger)
		{
			const double spread = (-0.048 + 0.032 * finger) * s;
			const double len = (0.105 - 0.007 * std::abs(finger - 1.5)) * s;
			const Vector3 fStart = mittCenter + gloveRight * spread + gloveUp * (.038 * s);
			const Vector3 fEnd = fStart + gloveUp * len + gloveRight * (spread * 0.14) -
			                     gloveFace * (.022 * s);
			cappedCapsule(fStart, fEnd, .020 * s, .016 * s, leather, 0.08);
		}
		const Vector3 thumbStart = mittCenter - gloveRight * (.064 * s) + gloveUp * (.012 * s);
		const Vector3 thumbEnd =
		    thumbStart - gloveRight * (.024 * s) + gloveUp * (.088 * s) - gloveFace * (.020 * s);
		cappedCapsule(thumbStart, thumbEnd, .022 * s, .018 * s, leather, 0.08);
		const Vector3 indexTip =
		    mittCenter - gloveRight * (.048 * s) + gloveUp * (.128 * s) - gloveFace * (.018 * s);
		cappedCapsule(thumbEnd, indexTip, .014 * s, .014 * s, leatherDark, 0.05);
		cappedCapsule((thumbStart + thumbEnd) * 0.5,
		    mittCenter - gloveRight * (.048 * s) + gloveUp * (.080 * s) - gloveFace * (.014 * s),
		    .012 * s, .012 * s, leatherDark, 0.05);

		// 5. Oriented throwing hand & fingers on rightHand.
		const auto throwAxis = Physics::NormalizeOrZero(pose.rightHand - pose.rightElbow);
		const auto throwRight = Physics::NormalizeOrZero(
		    Physics::Cross(throwAxis, std::abs(throwAxis.y) < .9 ? Vector3{0, 1, 0} : Vector3{1, 0, 0}));
		const auto throwFace = Physics::NormalizeOrZero(Physics::Cross(throwRight, throwAxis));
		const Vector3 palmCenter = pose.rightHand - throwAxis * (.022 * s) - throwFace * (.026 * s);
		orientedEllipsoid(palmCenter, throwRight, throwAxis, throwFace,
		    {.044 * s, .052 * s, .028 * s}, skin, 0.03);
		for (int finger = -1; finger <= 1; ++finger)
		{
			const Vector3 fBase = palmCenter + throwRight * (finger * .018 * s) + throwAxis * (.028 * s);
			const Vector3 fTip =
			    fBase + throwAxis * (.048 * s) + throwFace * (.028 * s);
			cappedCapsule(fBase, fTip, .010 * s, .008 * s, skin, 0.03);
		}
		const Vector3 rThumbBase = palmCenter + throwRight * (.034 * s);
		const Vector3 rThumbTip =
		    rThumbBase + throwRight * (.016 * s) + throwAxis * (.038 * s) + throwFace * (.026 * s);
		cappedCapsule(rThumbBase, rThumbTip, .012 * s, .009 * s, skin, 0.03);

		// 6. Neck, sculpted head, ears, facial features & fitted baseball cap with curved visor.
		const Vector3 neckStart = pose.chest + torsoUp * (.04 * spineLength);
		const Vector3 headCenter = pose.head;
		const Vector3 neckEnd = headCenter - Physics::NormalizeOrZero(headCenter - neckStart) * (.045 * s);
		profileLimb(neckStart, neckEnd,
		    std::array{0.0, 0.34, 1.0},
		    std::array{.072 * s, .062 * s, .055 * s},
		    std::array{navy, skin}, false, 0.02);

		const Vector3 headUp = Physics::NormalizeOrZero(torsoUp * 0.28 + Vector3{0.0, 0.72, 0.0});
		const Vector3 headRight = Physics::NormalizeOrZero(Physics::Cross(headUp, pose.headForward));
		const Vector3 headFwd = Physics::NormalizeOrZero(Physics::Cross(headRight, headUp));

		orientedEllipsoid(headCenter, headRight, headUp, headFwd,
		    {.098 * s, .124 * s, .104 * s}, skin, 0.03);
		orientedEllipsoid(headCenter - headUp * (.040 * s) + headFwd * (.016 * s),
		    headRight, headUp, headFwd,
		    {.074 * s, .066 * s, .078 * s}, skin, 0.03);
		for (const double sideSign : {-1.0, 1.0})
		{
			orientedEllipsoid(headCenter + headRight * (sideSign * .096 * s) - headUp * (.008 * s),
			    headRight, headUp, headFwd,
			    {.015 * s, .030 * s, .021 * s}, skinShade, 0.02);
		}
		orientedEllipsoid(headCenter - headUp * (.006 * s) + headFwd * (.102 * s),
		    headRight, headUp, headFwd,
		    {.015 * s, .024 * s, .022 * s}, skinShade, 0.03);
		for (const double eyeSign : {-1.0, 1.0})
		{
			const Vector3 eyePos =
			    headCenter + headRight * (eyeSign * .035 * s) + headUp * (.016 * s) + headFwd * (.092 * s);
			orientedEllipsoid(eyePos, headRight, headUp, headFwd,
			    {.014 * s, .010 * s, .008 * s}, eyeWhite, 0.06);
			orientedEllipsoid(eyePos + headFwd * (.005 * s), headRight, headUp, headFwd,
			    {.007 * s, .008 * s, .005 * s}, navyDark, 0.12);
		}
		// Cap crown, top button, front emblem & curved visor.
		orientedEllipsoid(headCenter + headUp * (.054 * s) - headFwd * (.006 * s),
		    headRight, headUp, headFwd,
		    {.105 * s, .086 * s, .112 * s}, navy, 0.07);
		orientedEllipsoid(headCenter + headUp * (.138 * s) - headFwd * (.006 * s),
		    headRight, headUp, headFwd,
		    {.014 * s, .008 * s, .014 * s}, red, 0.08);
		orientedEllipsoid(headCenter + headUp * (.078 * s) + headFwd * (.099 * s),
		    headRight, headUp, headFwd,
		    {.012 * s, .015 * s, .006 * s}, red, 0.06);
		const Vector3 brimCenter = headCenter + headUp * (.044 * s) + headFwd * (.100 * s);
		const Vector3 brimUp = Physics::NormalizeOrZero(headUp - headFwd * 0.18);
		const Vector3 brimFwd = Physics::NormalizeOrZero(Physics::Cross(headRight, brimUp));
		orientedEllipsoid(brimCenter, headRight, brimUp, brimFwd,
		    {.102 * s, .013 * s, .090 * s}, navy, 0.08);

		// The ball is in hand before release and again once the pitcher is set for the next pitch.
		if (state.secondsFromRelease < 0.0 || state.recoveryProgress >= 1.0)
		{
			const double radius = state.heldBallRadiusMeters;
			ellipsoid(pose.heldBall, {radius, radius, radius}, ball, 0.06);
		}
	}
	class PitcherWorldMeshCache final
	{
	  public:
		[[nodiscard]] const IndexedWorldMesh& Get(PitcherPresentationState state)
		{
			state.secondsFromRelease = std::clamp(state.secondsFromRelease, -PitcherWindupSeconds,
			    PitcherFollowThroughSeconds / std::clamp(state.armSpeedScale, .25, 2.0));
			const auto same = [](const auto& a, const auto& b)
			{
				return a.visible == b.visible && a.throwsLeftHanded == b.throwsLeftHanded &&
				       a.armSpeedScale == b.armSpeedScale &&
				       a.secondsFromRelease == b.secondsFromRelease &&
				       a.releasePositionWorld.x == b.releasePositionWorld.x &&
				       a.releasePositionWorld.y == b.releasePositionWorld.y &&
				       a.releasePositionWorld.z == b.releasePositionWorld.z &&
				       a.moundHeightMeters == b.moundHeightMeters &&
				       a.heldBallRadiusMeters == b.heldBallRadiusMeters &&
				       a.underhandToss == b.underhandToss && a.recoveryProgress == b.recoveryProgress;
			};
			if (!m_valid || !same(state, m_state))
			{
				// The cache stays invalid if mesh construction throws.
				m_valid = false;
				BuildPitcherWorldMesh(state, m_mesh);
				m_state = state;
				m_valid = true;
				++m_buildCount;
			}
			return m_mesh;
		}
		[[nodiscard]] std::uint64_t BuildCount() const noexcept
		{
			return m_buildCount;
		}

	  private:
		IndexedWorldMesh m_mesh;
		PitcherPresentationState m_state;
		bool m_valid{};
		std::uint64_t m_buildCount{};
	};
	[[nodiscard]] inline const IndexedWorldMesh& GetPitcherWorldMesh(
	    const PitcherPresentationState& state)
	{
		static thread_local PitcherWorldMeshCache cache;
		return cache.Get(state);
	}
	template <typename TTriangle>
	void EmitPitcherMesh(const PitcherPresentationState& state, TTriangle&& triangle)
	{
		const auto& mesh = GetPitcherWorldMesh(state);
		for (const auto& face : mesh.triangles)
		{
			triangle(mesh.vertices[face.indices[0]], mesh.vertices[face.indices[1]],
			    mesh.vertices[face.indices[2]], face.color.data());
		}
	}
} // namespace BaseballSimulatorVR::Common
