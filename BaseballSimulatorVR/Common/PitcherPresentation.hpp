#pragma once

#include "FieldPresentationState.hpp"
#include "IndexedWorldMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

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

	enum class PitcherDeliveryStyle { Overhand, Sidearm, Submarine, UnderhandToss };
	inline constexpr double SubmarineReleaseHeightLimitMeters = .65;
	inline constexpr double SidearmReleaseHeightLimitMeters = 1.50;
	inline constexpr double PitcherArmSegmentMeters = .38;
	inline constexpr double PitcherLegSegmentMeters = .54;

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

	[[nodiscard]] inline PitcherPose BuildPitcherPose(const PitcherPresentationState& state)
	{
		using Physics::Vector3;
		struct Keyframe final
		{
			double time;
			Vector3 pelvis, chest, hand, glove, leftFoot, rightFoot;
			double hipYaw, shoulderYaw;
			Vector3 throwBend, gloveBend, leftKneeBend, rightKneeBend;
		};
		// Right-handed delivery facing +Z. The final arm acceleration ends at
		// the simulation release point; every pose is grounded on the mound.
		static constexpr std::array<Keyframe, 7> overhandFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.39, -.12}, {-.06, 1.24, .11}, {.04, 1.25, .13},
		         {.06, .07, .06}, {-.15, .07, -.14}, .68, .74,
		         {-.65, -.70, -.25}, {.65, -.70, -.20}, {-.30, .25, .92}, {-.60, .20, .75}},
		        {-.85, {.02, .95, -.20}, {.03, 1.43, -.23}, {-.06, 1.40, .02}, {.05, 1.41, .06},
		            {.06, .66, .08}, {-.15, .07, -.14}, 1.05, 1.18,
		            {-.68, -.60, -.28}, {.65, -.55, -.15}, {-.58, .48, .66}, {-.65, .22, .72}},
		        {-.42, {0, .84, -.05}, {-.02, 1.36, -.15}, {-.56, 1.19, -.56}, {.26, 1.38, .44},
		            {.17, .12, .68}, {-.15, .07, -.14}, .46, .92,
		            {-.76, .24, -.48}, {.65, -.32, .42}, {.06, .38, .92}, {-.55, .18, .80}},
		        {-.12, {0, .82, .12}, {0, 1.35, .13}, {-.56, 1.80, -.06}, {.20, 1.14, .42},
		            {.18, .07, .87}, {-.15, .07, -.12}, .04, .22,
		            {-.72, .16, .52}, {.72, -.45, -.35}, {.08, .36, .93}, {-.35, -.05, .93}},
		        {0, {0, .80, .19}, {0, 1.30, .32}, {-.42, 1.82, .72}, {.18, 1.03, .36},
		            {.18, .07, .87}, {-.15, .09, -.02}, -.12, -.20,
		            {-.80, .12, -.35}, {.75, -.48, -.45}, {.06, .34, .94}, {-.28, -.12, .95}},
		        {.22, {.02, .77, .37}, {.02, 1.16, .64}, {.22, .88, .96}, {.18, .92, .60},
		            {.18, .07, .87}, {-.18, .53, .45}, -.22, -.46,
		            {-.55, .36, .65}, {.74, -.42, -.48}, {.05, .32, .95}, {-.18, .38, .91}},
		        {.6, {.01, .87, .43}, {.01, 1.37, .49}, {-.18, .98, .66}, {.22, 1.02, .70},
		            {.18, .07, .87}, {-.16, .07, .40}, -.04, -.06,
		            {-.62, -.45, .35}, {.66, -.45, .25}, {.06, .32, .95}, {-.12, .28, .95}}}};
		static constexpr std::array<Keyframe, 7> underhandFrames{
		    {{-1.2, {0, .91, -.08}, {0, 1.40, -.06}, {-.20, 1.00, .18}, {.18, 1.02, .18},
		         {.14, .07, .03}, {-.15, .07, -.12}, .24, .28,
		         {-.55, -.65, -.45}, {.62, -.60, -.20}, {.05, .30, .95}, {-.25, .22, .94}},
		        {-.85, {0, .91, -.08}, {0, 1.40, -.06}, {-.28, .80, -.10}, {.22, 1.05, .20},
		            {.15, .14, .05}, {-.15, .07, -.12}, .36, .42,
		            {-.45, -.55, -.68}, {.64, -.52, -.10}, {-.15, .38, .91}, {-.30, .22, .93}},
		        {-.42, {0, .90, -.04}, {0, 1.38, -.01}, {-.32, .58, -.30}, {.22, 1.04, .22},
		            {.16, .08, .16}, {-.15, .07, -.12}, .22, .30,
		            {-.38, -.48, -.78}, {.65, -.45, .10}, {.06, .35, .93}, {-.25, .20, .95}},
		        {-.12, {0, .90, .02}, {0, 1.37, .06}, {-.31, .62, .15}, {.21, 1.03, .23},
		            {.18, .07, .30}, {-.15, .07, -.10}, .06, .08,
		            {-.36, -.55, -.75}, {.66, -.48, -.15}, {.06, .35, .93}, {-.20, .15, .97}},
		        {0, {0, .91, .06}, {0, 1.38, .15}, {-.30, .82, .45}, {.20, 1.04, .24},
		            {.18, .07, .30}, {-.15, .07, -.08}, -.04, -.08,
		            {-.35, -.58, -.74}, {.68, -.50, -.25}, {.06, .34, .94}, {-.18, .12, .97}},
		        {.22, {0, .92, .10}, {0, 1.40, .16}, {-.26, 1.10, .62}, {.20, 1.05, .25},
		            {.18, .07, .30}, {-.15, .10, -.02}, -.10, -.16,
		            {-.38, -.52, -.65}, {.68, -.50, -.28}, {.06, .34, .94}, {-.15, .20, .97}},
		        {.6, {0, .92, .10}, {0, 1.41, .14}, {-.20, .96, .35}, {.19, 1.04, .24},
		            {.18, .07, .30}, {-.15, .07, .03}, -.02, -.04,
		            {-.48, -.58, -.45}, {.65, -.52, -.20}, {.06, .32, .95}, {-.12, .22, .97}}}};
		static constexpr std::array<Keyframe, 7> sidearmFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.40, -.12}, {-.08, 1.26, .12}, {.06, 1.27, .14},
		         {.08, .07, .04}, {-.16, .07, -.16}, .70, .76,
		         {-.65, -.68, -.25}, {.65, -.68, -.20}, {-.30, .25, .92}, {-.60, .20, .75}},
		        {-.85, {.04, .94, -.22}, {.02, 1.43, -.27}, {-.18, 1.36, -.02}, {.08, 1.35, .08},
		            {.12, .56, .12}, {-.16, .07, -.16}, 1.08, 1.22,
		            {-.68, -.52, -.32}, {.65, -.52, -.12}, {-.56, .46, .68}, {-.65, .22, .72}},
		        {-.42, {.05, .85, -.06}, {-.10, 1.32, -.15}, {-.68, 1.24, -.42}, {.32, 1.24, .45},
		            {.24, .12, .74}, {-.16, .07, -.16}, .48, .94,
		            {-.72, .22, -.56}, {.65, -.30, .42}, {.08, .38, .92}, {-.55, .18, .80}},
		        {-.12, {.04, .82, .12}, {-.17, 1.25, .29}, {-.82, 1.22, .15}, {.23, 1.12, .55},
		            {.25, .07, .89}, {-.16, .08, -.10}, .05, .20,
		            {-.70, .22, -.52}, {.72, -.42, -.32}, {.08, .36, .93}, {-.35, -.04, .93}},
		        {0, {.03, .84, .20}, {-.12, 1.26, .49}, {-.78, 1.20, .82}, {.20, 1.15, .62},
		            {.25, .07, .89}, {-.16, .10, -.02}, -.10, -.12,
		            {-.70, .25, -.60}, {.75, -.45, -.42}, {.08, .34, .94}, {-.28, -.10, .95}},
		        {.22, {.08, .78, .36}, {.08, 1.14, .69}, {.38, .91, .98}, {.22, .99, .73},
		            {.25, .07, .89}, {-.27, .39, .35}, -.24, -.48,
		            {-.52, .34, .66}, {.74, -.40, -.46}, {.06, .32, .95}, {-.20, .36, .91}},
		        {.6, {.06, .87, .42}, {.02, 1.36, .48}, {-.12, 1.03, .65}, {.23, 1.07, .73},
		            {.25, .07, .89}, {-.20, .07, .40}, -.04, -.06,
		            {-.60, -.44, .34}, {.66, -.44, .24}, {.06, .32, .95}, {-.12, .28, .95}}}};
		static constexpr std::array<Keyframe, 7> submarineFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.40, -.10}, {-.16, 1.14, .16}, {.14, 1.16, .18},
		         {.10, .07, .04}, {-.16, .07, -.17}, .70, .78,
		         {-.65, -.65, -.28}, {.65, -.65, -.20}, {-.30, .25, .92}, {-.60, .20, .75}},
		        {-.85, {.05, .86, -.21}, {-.04, 1.34, -.23}, {-.34, 1.06, -.14}, {.20, 1.18, .18},
		            {.18, .44, .16}, {-.16, .07, -.17}, 1.06, 1.20,
		            {-.68, -.35, -.42}, {.66, -.45, -.10}, {-.52, .44, .72}, {-.64, .22, .73}},
		        {-.42, {.08, .71, -.04}, {-.17, 1.02, .28}, {-.65, .56, -.13}, {.26, .90, .63},
		            {.29, .10, .80}, {-.16, .07, -.15}, .44, .82,
		            {-.70, .28, -.44}, {.68, -.28, .38}, {.10, .38, .92}, {-.52, .16, .84}},
		        {-.12, {.08, .65, .15}, {-.19, .64, .61}, {-.58, .19, .26}, {.21, .64, .80},
		            {.29, .07, 1.00}, {-.16, .09, -.04}, .04, .16,
		            {-.70, .30, -.40}, {.72, -.35, -.30}, {.10, .36, .93}, {-.32, -.06, .94}},
		        {0, {.08, .66, .24}, {-.12, .55, .70}, {-.42, .18, .94}, {.18, .62, .90},
		            {.29, .07, 1.00}, {-.16, .11, .05}, -.08, -.10,
		            {-.70, .30, -.40}, {.74, -.38, -.38}, {.10, .34, .94}, {-.26, -.10, .96}},
		        {.22, {.06, .68, .40}, {.12, .76, .91}, {.36, .67, 1.18}, {.25, .80, .96},
		            {.29, .07, 1.00}, {-.29, .40, .42}, -.22, -.42,
		            {-.50, .35, .64}, {.74, -.38, -.42}, {.08, .32, .94}, {-.22, .35, .91}},
		        {.6, {.03, .88, .47}, {.02, 1.36, .56}, {-.15, .93, .79}, {.23, 1.02, .78},
		            {.29, .07, 1.00}, {-.20, .07, .45}, -.04, -.06,
		            {-.60, -.44, .34}, {.66, -.44, .24}, {.08, .32, .94}, {-.12, .28, .95}}}};
		const auto style = GetPitcherDeliveryStyle(state);
		const auto& frames = style == PitcherDeliveryStyle::UnderhandToss ? underhandFrames :
		    style == PitcherDeliveryStyle::Submarine ? submarineFrames :
		    style == PitcherDeliveryStyle::Sidearm ? sidearmFrames : overhandFrames;
		// Keep windup start and release fixed on the delivery clock. Reallocate
		// windup time so the final forward swing and follow-through use the
		// preset's fastest-pitch tempo, independent of the current pitch type.
		const double rawSpeedScale = std::isfinite(state.armSpeedScale) ? state.armSpeedScale : 1.0;
		const double speedScale = std::clamp(rawSpeedScale, .25, 2.0);
		const double swingStart = -.12 / speedScale;
		const double rawSeconds = std::isfinite(state.secondsFromRelease) ? state.secondsFromRelease : 0.0;
		const double clockTime = std::clamp(
		    rawSeconds, frames.front().time, frames.back().time / speedScale);
		const double time =
		    speedScale == 1.0
		        ? clockTime
		        : (clockTime >= swingStart
		                  ? clockTime * speedScale
		                  : frames.front().time + (clockTime - frames.front().time) *
		                                              (-.12 - frames.front().time) /
		                                              (swingStart - frames.front().time));
		std::size_t index = 0;
		while ((index + 2 < frames.size()) && (time > frames[index + 1].time))
		{
			++index;
		}
		const auto& a = frames[index];
		const auto& b = frames[index + 1];
		const double span = b.time - a.time;
		const double u = std::clamp((time - a.time) / span, 0.0, 1.0);
		const double h00 = (1.0 + 2.0 * u) * (1.0 - u) * (1.0 - u);
		const double h10 = u * (1.0 - u) * (1.0 - u);
		const double h01 = u * u * (3.0 - 2.0 * u);
		const double h11 = u * u * (u - 1.0);
		// Monotone cubic Hermite interpolation preserves exact keyframe values
		// and ground bounds while maintaining continuous velocity through release.
		const auto monotoneTangentScalar = [&](std::size_t k, auto getter) -> double
		{
			if (k == 0 || k + 1 >= frames.size())
			{
				return 0.0;
			}
			const double hPrev = frames[k].time - frames[k - 1].time;
			const double hNext = frames[k + 1].time - frames[k].time;
			const double dPrev = (getter(frames[k]) - getter(frames[k - 1])) / hPrev;
			const double dNext = (getter(frames[k + 1]) - getter(frames[k])) / hNext;
			if (dPrev * dNext <= 0.0)
			{
				return 0.0;
			}
			const double w1 = 2.0 * hNext + hPrev;
			const double w2 = hNext + 2.0 * hPrev;
			return 0.65 * (w1 + w2) / (w1 / dPrev + w2 / dNext);
		};
		const auto blendScalar = [&](auto getter) -> double
		{
			const double v0 = getter(a);
			const double v1 = getter(b);
			const double m0 = monotoneTangentScalar(index, getter);
			const double m1 = monotoneTangentScalar(index + 1, getter);
			return h00 * v0 + h10 * span * m0 + h01 * v1 + h11 * span * m1;
		};
		const auto blendVec = [&](auto member) -> Vector3
		{
			return {
			    blendScalar([&](const Keyframe& f) { return (f.*member).x; }),
			    blendScalar([&](const Keyframe& f) { return (f.*member).y; }),
			    blendScalar([&](const Keyframe& f) { return (f.*member).z; })};
		};
		PitcherPose pose{};
		const double releaseHeight = state.releasePositionWorld.y - state.moundHeightMeters;
		pose.scale = style == PitcherDeliveryStyle::Overhand ? std::clamp(releaseHeight / 1.82, 1., 1.2) : 1.;
		const auto& releaseFrame = frames[4];
		const Vector3 base{
		    state.releasePositionWorld.x - releaseFrame.hand.x * pose.scale,
		    state.moundHeightMeters,
		    state.releasePositionWorld.z - releaseFrame.hand.z * pose.scale};
		pose.moundOrigin = base;
		const auto world = [&](const Vector3& p) { return base + p * pose.scale; };
		pose.pelvis = world(blendVec(&Keyframe::pelvis));
		pose.chest = world(blendVec(&Keyframe::chest));
		const double enteringStride = std::clamp((time + .85) / .43, 0., 1.);
		const double deliveryWeight = enteringStride * enteringStride * (3. - 2. * enteringStride);
		const double highExtension = style == PitcherDeliveryStyle::Overhand ?
		    std::clamp((releaseHeight - 1.82 * pose.scale) / .316, 0., 1.) : 0.;
		const double crouch = style == PitcherDeliveryStyle::Sidearm ?
		    std::clamp(1.20 - releaseHeight, 0., .55) * .5 * deliveryWeight : 0.;
		const double bodyLift = highExtension * .18 * pose.scale - crouch;
		pose.pelvis.y += bodyLift;
		pose.chest += Vector3{0., bodyLift, highExtension * .10 * pose.scale};
		const auto torsoDirection = Physics::NormalizeOrZero(pose.chest - pose.pelvis);
		pose.chest = pose.pelvis + torsoDirection * (.53 * pose.scale);
		const double hipYaw = blendScalar([](const Keyframe& f) { return f.hipYaw; });
		const double shoulderYaw = blendScalar([](const Keyframe& f) { return f.shoulderYaw; });
		const Vector3 hipHorizLateral{std::cos(hipYaw), 0.0, std::sin(hipYaw)};
		const Vector3 shoulderHorizLateral{std::cos(shoulderYaw), 0.0, std::sin(shoulderYaw)};
		const auto pelvisUp = Physics::NormalizeOrZero(torsoDirection + Vector3{0.0, 0.70, 0.0});
		pose.pelvisLateral = Physics::NormalizeOrZero(
		    hipHorizLateral - pelvisUp * Physics::Dot(hipHorizLateral, pelvisUp));
		if (Physics::Length(pose.pelvisLateral) < 1e-6)
		{
			pose.pelvisLateral = {1.0, 0.0, 0.0};
		}
		pose.chestLateral = Physics::NormalizeOrZero(
		    shoulderHorizLateral - torsoDirection * Physics::Dot(shoulderHorizLateral, torsoDirection));
		if (Physics::Length(pose.chestLateral) < 1e-6)
		{
			pose.chestLateral = {1.0, 0.0, 0.0};
		}
		pose.head = pose.chest + (torsoDirection * .14 + Vector3{0., .18, -.02}) * pose.scale;
		// Keep eyes focused toward home plate (+Z) through release, relaxing slightly in follow-through.
		const double headYaw = shoulderYaw * (time <= 0.0 ? 0.14 : 0.30);
		pose.headForward = Physics::NormalizeOrZero(
		    Vector3{-std::sin(headYaw), torsoDirection.y * 0.16 - 0.14, std::cos(headYaw)});
		pose.rightShoulder = pose.chest + (torsoDirection * .06 - pose.chestLateral * .23) * pose.scale;
		pose.leftShoulder = pose.chest + (torsoDirection * .06 + pose.chestLateral * .23) * pose.scale;
		pose.rightHand = world(blendVec(&Keyframe::hand));
		pose.rightHand.y += (releaseHeight - releaseFrame.hand.y * pose.scale) * deliveryWeight;
		pose.leftHand = world(blendVec(&Keyframe::glove));
		pose.rightHip = pose.pelvis - pose.pelvisLateral * (.115 * pose.scale);
		pose.leftHip = pose.pelvis + pose.pelvisLateral * (.115 * pose.scale);
		pose.rightFoot = world(blendVec(&Keyframe::rightFoot));
		pose.leftFoot = world(blendVec(&Keyframe::leftFoot));
		pose.leftFoot.z -= highExtension * .22 * pose.scale;
		const double pivotProgress = std::clamp((time + 0.32) / 0.38, 0.0, 1.0);
		const double pivotSmooth = pivotProgress * pivotProgress * (3.0 - 2.0 * pivotProgress);
		const double rightFootYaw = (style == PitcherDeliveryStyle::UnderhandToss ? 0.32 : 1.15) * (1.0 - pivotSmooth);
		pose.rightFootForward = Physics::NormalizeOrZero(Vector3{-std::sin(rightFootYaw), 0.0, std::cos(rightFootYaw)});
		const double strideProgress = std::clamp((time + 0.85) / 0.43, 0.0, 1.0);
		const double leftFootYaw = 0.55 * (1.0 - strideProgress);
		pose.leftFootForward = Physics::NormalizeOrZero(Vector3{-std::sin(leftFootYaw), 0.0, std::cos(leftFootYaw)});
		const auto reachableHand = [&](const Vector3& shoulder, const Vector3& target)
		{
			const auto delta = target - shoulder;
			const double distance = Physics::Length(delta);
			const double reach = 2. * PitcherArmSegmentMeters * pose.scale - 1e-6;
			return distance > reach ? shoulder + delta * (reach / distance) : target;
		};
		pose.rightHand = reachableHand(pose.rightShoulder, pose.rightHand);
		pose.leftHand = reachableHand(pose.leftShoulder, pose.leftHand);
		pose.heldBall = pose.rightHand;
		const auto joint =
		    [](const Vector3& start, const Vector3& end, const Vector3& bend, const double length)
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
			const double height =
			    std::sqrt((std::max)(0.0, length * length - distance * distance * .25));
			return (start + end) * .5 + normal * height;
		};
		const auto throwingBend = blendVec(&Keyframe::throwBend);
		const auto gloveBend = blendVec(&Keyframe::gloveBend);
		const auto leftKneeBend = blendVec(&Keyframe::leftKneeBend);
		const auto rightKneeBend = blendVec(&Keyframe::rightKneeBend);
		pose.rightElbow = joint(pose.rightShoulder, pose.rightHand, throwingBend,
		    PitcherArmSegmentMeters * pose.scale);
		pose.leftElbow = joint(pose.leftShoulder, pose.leftHand, gloveBend, PitcherArmSegmentMeters * pose.scale);
		pose.rightKnee = joint(pose.rightHip, pose.rightFoot, rightKneeBend, PitcherLegSegmentMeters * pose.scale);
		pose.leftKnee = joint(pose.leftHip, pose.leftFoot, leftKneeBend, PitcherLegSegmentMeters * pose.scale);
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

		if (state.secondsFromRelease < 0.0)
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
				       a.underhandToss == b.underhandToss;
			};
			if (!m_valid || !same(state, m_state))
			{
				// Mark the cache invalid until mesh construction succeeds.
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
