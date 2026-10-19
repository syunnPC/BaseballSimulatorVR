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
		double scale{1.0};
	};

	[[nodiscard]] inline PitcherPose BuildPitcherPose(const PitcherPresentationState& state)
	{
		using Physics::Vector3;
		struct Keyframe final
		{
			double time;
			Vector3 pelvis, chest, hand, glove, leftFoot, rightFoot;
		};
		// Right-handed delivery facing +Z. The final arm acceleration ends at
		// the simulation release point; every pose is grounded on the mound.
		static constexpr std::array<Keyframe, 7> overhandFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.39, -.12}, {-.08, 1.25, .12}, {.05, 1.25, .13},
		         {.16, .07, .03}, {-.15, .07, -.14}},
		        {-.85, {.02, .95, -.20}, {.03, 1.43, -.23}, {-.06, 1.42, .02}, {.06, 1.43, .06},
		            {.15, .68, .12}, {-.15, .07, -.14}},
		        {-.42, {0, .84, -.05}, {-.02, 1.36, -.15}, {-.57, 1.17, -.58}, {.27, 1.40, .44},
		            {.18, .13, .68}, {-.15, .07, -.14}},
		        {-.12, {0, .82, .12}, {0, 1.35, .13}, {-.58, 1.82, -.08}, {.20, 1.14, .42},
		            {.18, .07, .87}, {-.15, .07, -.12}},
		        {0, {0, .80, .19}, {0, 1.30, .32}, {-.42, 1.82, .72}, {.16, 1.02, .36},
		            {.18, .07, .87}, {-.15, .09, -.02}},
		        {.22, {.02, .77, .37}, {.02, 1.16, .64}, {.23, .89, .97}, {.16, .91, .63},
		            {.18, .07, .87}, {-.18, .53, .45}},
		        {.6, {.01, .87, .43}, {.01, 1.37, .49}, {-.22, .96, .66}, {.25, 1.00, .70},
		            {.18, .07, .87}, {-.17, .07, .33}}}};
		static constexpr std::array<Keyframe, 7> underhandFrames{
		    {{-1.2, {0, .91, -.08}, {0, 1.40, -.06}, {-.20, 1.00, .18}, {.18, 1.02, .18},
		         {.16, .07, .03}, {-.15, .07, -.12}},
		        {-.85, {0, .91, -.08}, {0, 1.40, -.06}, {-.28, .80, -.10}, {.22, 1.05, .20},
		            {.16, .07, .03}, {-.15, .07, -.12}},
		        {-.42, {0, .90, -.04}, {0, 1.38, -.01}, {-.32, .58, -.30}, {.22, 1.04, .22},
		            {.16, .07, .16}, {-.15, .07, -.12}},
		        {-.12, {0, .90, .02}, {0, 1.37, .06}, {-.31, .62, .15}, {.21, 1.03, .23},
		            {.18, .07, .30}, {-.15, .07, -.10}},
		        {0, {0, .91, .06}, {0, 1.38, .15}, {-.30, .82, .45}, {.20, 1.04, .24},
		            {.18, .07, .30}, {-.15, .07, -.08}},
		        {.22, {0, .92, .10}, {0, 1.40, .16}, {-.28, 1.10, .62}, {.20, 1.05, .25},
		            {.18, .07, .30}, {-.15, .10, -.02}},
		        {.6, {0, .92, .10}, {0, 1.41, .14}, {-.20, .96, .35}, {.19, 1.04, .24},
		            {.18, .07, .30}, {-.15, .07, .03}}}};
		static constexpr std::array<Keyframe, 7> sidearmFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.40, -.12}, {-.10, 1.28, .13}, {.08, 1.28, .15},
		         {.17, .07, .02}, {-.16, .07, -.16}},
		        {-.85, {.04, .94, -.22}, {.02, 1.43, -.27}, {-.22, 1.38, -.03}, {.10, 1.35, .10},
		            {.25, .55, .15}, {-.16, .07, -.16}},
		        {-.42, {.05, .85, -.06}, {-.10, 1.32, -.15}, {-.69, 1.24, -.42}, {.34, 1.24, .45},
		            {.25, .12, .74}, {-.16, .07, -.16}},
		        {-.12, {.04, .82, .12}, {-.17, 1.25, .29}, {-.83, 1.22, .15}, {.23, 1.12, .55},
			            {.25, .07, .89}, {-.16, .08, -.10}},
		        {0, {.03, .84, .20}, {-.12, 1.26, .49}, {-.78, 1.20, .82}, {.18, 1.16, .64},
			            {.25, .07, .89}, {-.16, .10, -.02}},
		        {.22, {.08, .78, .36}, {.08, 1.14, .69}, {.40, .91, .99}, {.22, .99, .75},
			            {.25, .07, .89}, {-.27, .39, .35}},
		        {.6, {.06, .87, .42}, {.02, 1.36, .48}, {-.12, 1.03, .65}, {.23, 1.07, .73},
			            {.25, .07, .89}, {-.20, .07, .40}}}};
		static constexpr std::array<Keyframe, 7> submarineFrames{
		    {{-1.2, {0, .91, -.12}, {0, 1.40, -.10}, {-.18, 1.14, .18}, {.16, 1.16, .20},
		         {.18, .07, .02}, {-.16, .07, -.17}},
		        {-.85, {.05, .86, -.21}, {-.04, 1.34, -.23}, {-.36, 1.07, -.14}, {.22, 1.18, .19},
		            {.27, .42, .19}, {-.16, .07, -.17}},
		        {-.42, {.08, .71, -.04}, {-.17, 1.02, .28}, {-.66, .56, -.13}, {.26, .90, .63},
		            {.29, .10, .80}, {-.16, .07, -.15}},
		        {-.12, {.08, .65, .15}, {-.19, .64, .61}, {-.59, .19, .26}, {.19, .64, .82},
		            {.29, .07, 1.00}, {-.16, .09, -.04}},
		        {0, {.08, .66, .24}, {-.12, .55, .70}, {-.42, .18, .94}, {.14, .62, .93},
		            {.29, .07, 1.00}, {-.16, .11, .05}},
		        {.22, {.06, .68, .40}, {.12, .76, .91}, {.38, .67, 1.19}, {.25, .80, .98},
		            {.29, .07, 1.00}, {-.29, .40, .42}},
		        {.6, {.03, .88, .47}, {.02, 1.36, .56}, {-.15, .93, .79}, {.23, 1.02, .78},
		            {.29, .07, 1.00}, {-.20, .07, .45}}}};
		const auto style = GetPitcherDeliveryStyle(state);
		const auto& frames = style == PitcherDeliveryStyle::UnderhandToss ? underhandFrames :
		    style == PitcherDeliveryStyle::Submarine ? submarineFrames :
		    style == PitcherDeliveryStyle::Sidearm ? sidearmFrames : overhandFrames;
		// Keep windup start and release fixed on the delivery clock. Reallocate
		// windup time so the final forward swing and follow-through use the
		// preset's fastest-pitch tempo, independent of the current pitch type.
		const double speedScale = std::clamp(state.armSpeedScale, .25, 2.0);
		const double swingStart = -.12 / speedScale;
		const double clockTime = std::clamp(
		    state.secondsFromRelease, frames.front().time, frames.back().time / speedScale);
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
		const double t = (time - a.time) / (b.time - a.time);
		const double smooth = t * t * (3.0 - 2.0 * t);
		const auto blend = [smooth](const Vector3& p, const Vector3& q)
		{ return p + (q - p) * smooth; };
		PitcherPose pose{};
		const double releaseHeight = state.releasePositionWorld.y - state.moundHeightMeters;
		pose.scale = style == PitcherDeliveryStyle::Overhand ? std::clamp(releaseHeight / 1.82, 1., 1.2) : 1.;
		const auto& releaseFrame = frames[4];
		const Vector3 base{
		    state.releasePositionWorld.x - releaseFrame.hand.x * pose.scale,
		    state.moundHeightMeters,
		    state.releasePositionWorld.z - releaseFrame.hand.z * pose.scale};
		const auto world = [&](const Vector3& p) { return base + p * pose.scale; };
		pose.pelvis = world(blend(a.pelvis, b.pelvis));
		pose.chest = world(blend(a.chest, b.chest));
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
		const auto lateral = Physics::NormalizeOrZero(Vector3{1., 0., 0.} -
		    torsoDirection * torsoDirection.x);
		pose.head = pose.chest + (torsoDirection * .14 + Vector3{0., .18, -.02}) * pose.scale;
		pose.rightShoulder = pose.chest + (torsoDirection * .06 - lateral * .23) * pose.scale;
		pose.leftShoulder = pose.chest + (torsoDirection * .06 + lateral * .23) * pose.scale;
		pose.rightHand = world(blend(a.hand, b.hand));
		pose.rightHand.y += (releaseHeight - releaseFrame.hand.y * pose.scale) * deliveryWeight;
		pose.leftHand = world(blend(a.glove, b.glove));
		pose.rightHip = pose.pelvis + Vector3{-.13, 0, 0} * pose.scale;
		pose.leftHip = pose.pelvis + Vector3{.13, 0, 0} * pose.scale;
		pose.rightFoot = world(blend(a.rightFoot, b.rightFoot));
		pose.leftFoot = world(blend(a.leftFoot, b.leftFoot));
		pose.leftFoot.z -= highExtension * .22 * pose.scale;
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
		const auto throwingBend = style == PitcherDeliveryStyle::Submarine ? Vector3{-.7, .3, -.4} :
		    style == PitcherDeliveryStyle::Sidearm ? Vector3{-.7, .25, -.6} : Vector3{-.8, 0., -.5};
		pose.rightElbow = joint(pose.rightShoulder, pose.rightHand, throwingBend,
		    PitcherArmSegmentMeters * pose.scale);
		pose.leftElbow = joint(pose.leftShoulder, pose.leftHand, {.8, -.2, 0}, PitcherArmSegmentMeters * pose.scale);
		pose.rightKnee = joint(pose.rightHip, pose.rightFoot, {0, .2, .9}, PitcherLegSegmentMeters * pose.scale);
		pose.leftKnee = joint(pose.leftHip, pose.leftFoot, {0, .4, 1}, PitcherLegSegmentMeters * pose.scale);
		if (state.throwsLeftHanded)
		{
			for (auto* point : {&pose.pelvis, &pose.chest, &pose.head, &pose.rightShoulder,
			         &pose.rightElbow, &pose.rightHand, &pose.leftShoulder, &pose.leftElbow,
			         &pose.leftHand, &pose.rightHip, &pose.rightKnee, &pose.rightFoot,
			         &pose.leftHip, &pose.leftKnee, &pose.leftFoot, &pose.heldBall})
			{
				point->x = 2.0 * state.releasePositionWorld.x - point->x;
			}
			std::swap(pose.rightShoulder, pose.leftShoulder);
			std::swap(pose.rightElbow, pose.leftElbow);
			std::swap(pose.rightHand, pose.leftHand);
			std::swap(pose.rightHip, pose.leftHip);
			std::swap(pose.rightKnee, pose.leftKnee);
			std::swap(pose.rightFoot, pose.leftFoot);
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
		mesh.vertices.reserve(2800);
		mesh.triangles.reserve(4600);
		using Physics::Vector3;
		using Color = std::array<float, 4>;
		auto canonicalState = state;
		canonicalState.throwsLeftHanded = false;
		const auto pose = BuildPitcherPose(canonicalState);
		const double s = pose.scale;
		constexpr Color uniform{.88F, .92F, .97F, 1}, navy{.035F, .10F, .24F, 1},
		    red{.78F, .10F, .14F, 1}, skin{.79F, .52F, .34F, 1}, leather{.30F, .13F, .045F, 1},
		    ball{1, .98F, .87F, 1};
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
		const auto surface =
		    [&](std::uint32_t i, std::uint32_t j, std::uint32_t k, const Color& color)
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
			const double light =
			    .53 + .47 * (std::max)(0.0, Physics::Dot(normal, Vector3{-.35, .75, .56}));
			const Color shaded{color[0] * static_cast<float>(light),
			    color[1] * static_cast<float>(light), color[2] * static_cast<float>(light),
			    color[3]};
			mesh.triangles.push_back({{i, j, k}, shaded});
		};
		const auto ellipsoid = [&](const Vector3& center, const Vector3& radius, const Color& color)
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
					indices[r * (slices + 1) + n] =
					    vertex(center + Vector3{radius.x * latitude[r][0] * longitude[n][0],
					                        radius.y * latitude[r][1],
					                        radius.z * latitude[r][0] * longitude[n][1]});
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
					surface(a, b, c, color);
					surface(a, c, d, color);
				}
			}
		};
		const auto limb = [&](const Vector3& start, const Vector3& end, double radiusStart,
		                      double radiusEnd, const Color& color)
		{
			const auto axis = Physics::NormalizeOrZero(end - start);
			const auto u = Physics::NormalizeOrZero(
			    Physics::Cross(axis, std::abs(axis.y) < .9 ? Vector3{0, 1, 0} : Vector3{1, 0, 0}));
			const auto v = Physics::Cross(axis, u);
			const auto& angles = CircleTrigTable<12>();
			std::array<std::uint32_t, 13> starts, ends;
			for (std::size_t n = 0; n < angles.size(); ++n)
			{
				const auto p = u * angles[n][0] + v * angles[n][1];
				starts[n] = vertex(start + p * radiusStart);
				ends[n] = vertex(end + p * radiusEnd);
			}
			const auto first = vertex(start), last = vertex(end);
			for (int n = 0; n < 12; ++n)
			{
				const auto a = starts[n], b = ends[n], c = ends[n + 1], d = starts[n + 1];
				surface(a, b, c, color);
				surface(a, c, d, color);
				surface(first, a, d, color);
				surface(last, c, b, color);
			}
		};
		const Vector3 moundCenter{state.releasePositionWorld.x + .42 * s, state.moundHeightMeters,
		    state.releasePositionWorld.z - .72 * s};
		constexpr Color dirt{.39F, .25F, .13F, 1}, rubber{.89F, .88F, .78F, 1};
		const double topRadius = (std::max)(1.2, 1.10 * s),
		             apronRadius = (std::max)(2.74, topRadius + .6);
		const auto& angles = CircleTrigTable<64>();
		std::array<std::uint32_t, 65> inner, outer;
		const auto mound = vertex(moundCenter);
		for (std::size_t n = 0; n < angles.size(); ++n)
		{
			inner[n] = vertex(
			    moundCenter + Vector3{angles[n][0] * topRadius, 0, angles[n][1] * topRadius});
			outer[n] = vertex({moundCenter.x + angles[n][0] * apronRadius, .002,
			    moundCenter.z + angles[n][1] * apronRadius});
		}
		for (int n = 0; n < 64; ++n)
		{
			surface(mound, inner[n + 1], inner[n], dirt);
			surface(inner[n], inner[n + 1], outer[n + 1], dirt);
			surface(inner[n], outer[n + 1], outer[n], dirt);
		}
		const auto plate = moundCenter + Vector3{0, .008, -.14 * s};
		const auto plateA = vertex(plate + Vector3{-.305, 0, -.076}),
		           plateB = vertex(plate + Vector3{.305, 0, -.076});
		const auto plateC = vertex(plate + Vector3{.305, 0, .076}),
		           plateD = vertex(plate + Vector3{-.305, 0, .076});
		surface(plateA, plateC, plateB, rubber);
		surface(plateA, plateD, plateC, rubber);
		limb(pose.pelvis, pose.chest, .20 * s, .27 * s, uniform);
		ellipsoid(pose.pelvis, {.21 * s, .15 * s, .15 * s}, uniform);
		limb(pose.pelvis + Vector3{0, .045 * s, 0}, pose.pelvis + Vector3{0, .085 * s, 0}, .208 * s,
		    .208 * s, navy);
		for (int side = 0; side < 2; ++side)
		{
			const auto hip = side ? pose.leftHip : pose.rightHip;
			const auto knee = side ? pose.leftKnee : pose.rightKnee;
			const auto foot = side ? pose.leftFoot : pose.rightFoot;
			limb(hip, knee, .115 * s, .085 * s, uniform);
			ellipsoid(knee, {.087 * s, .087 * s, .087 * s}, uniform);
			limb(knee, foot, .083 * s, .06 * s, uniform);
			ellipsoid(foot + Vector3{0, -.015 * s, .04 * s}, {.09 * s, .055 * s, .16 * s}, navy);
			const auto shoulder = side ? pose.leftShoulder : pose.rightShoulder;
			const auto elbow = side ? pose.leftElbow : pose.rightElbow;
			const auto hand = side ? pose.leftHand : pose.rightHand;
			limb(shoulder, elbow, .105 * s, .078 * s, navy);
			ellipsoid(shoulder, {.106 * s, .106 * s, .106 * s}, navy);
			limb(elbow, hand, .065 * s, .043 * s, skin);
			ellipsoid(elbow, {.067 * s, .067 * s, .067 * s}, skin);
		}
		ellipsoid(pose.leftHand, {.13 * s, .16 * s, .09 * s}, leather);
		for (int finger = 0; finger < 4; ++finger)
		{
			limb(pose.leftHand + Vector3{(-.075 + .05 * finger) * s, -.025 * s, .075 * s},
			    pose.leftHand + Vector3{(-.075 + .05 * finger) * s, .11 * s, .065 * s}, .012 * s,
			    .012 * s, navy);
		}
		ellipsoid(pose.rightHand + Vector3{0, -.025 * s, -.025 * s}, {.055 * s, .065 * s, .045 * s},
		    skin);
		limb(pose.chest, pose.head, .075 * s, .07 * s, skin);
		ellipsoid(pose.head, {.125 * s, .155 * s, .115 * s}, skin);
		ellipsoid(pose.head + Vector3{0, .105 * s, -.01 * s}, {.135 * s, .09 * s, .123 * s}, navy);
		ellipsoid(pose.head + Vector3{0, .085 * s, .105 * s}, {.145 * s, .018 * s, .12 * s}, navy);
		ellipsoid(
		    pose.head + Vector3{0, -.015 * s, .117 * s}, {.025 * s, .035 * s, .032 * s}, skin);
		for (const double eye : {-.044, .044})
		{
			ellipsoid(pose.head + Vector3{eye * s, .028 * s, .107 * s},
			    {.009 * s, .011 * s, .008 * s}, navy);
		}
		// Jersey piping and an authored chest crest.
		const Vector3 crest = pose.chest + Vector3{.10 * s, -.08 * s, .23 * s};
		limb(crest + Vector3{-.03 * s, -.06 * s, 0}, crest + Vector3{-.03 * s, .06 * s, 0},
		    .012 * s, .012 * s, red);
		limb(crest + Vector3{-.03 * s, .05 * s, 0}, crest + Vector3{.035 * s, .02 * s, 0}, .012 * s,
		    .012 * s, red);
		limb(crest + Vector3{-.03 * s, -.05 * s, 0}, crest + Vector3{.035 * s, -.015 * s, 0},
		    .012 * s, .012 * s, red);
		if (state.secondsFromRelease < 0.0)
		{
			const double radius = state.heldBallRadiusMeters;
			ellipsoid(pose.heldBall, {radius, radius, radius}, ball);
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
