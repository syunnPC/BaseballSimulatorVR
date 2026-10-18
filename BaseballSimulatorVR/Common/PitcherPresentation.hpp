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
		return std::clamp(fastestPresetSpeedMetersPerSecond / ReferencePitcherSpeedMetersPerSecond,
		    .25, 2.0);
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
		static constexpr std::array<Keyframe, 7> frames{
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
		// Keep windup start and release fixed on the delivery clock. Reallocate
		// windup time so the final forward swing and follow-through use the
		// preset's fastest-pitch tempo, independent of the current pitch type.
		const double speedScale = std::clamp(state.armSpeedScale, .25, 2.0);
		const double swingStart = -.12 / speedScale;
		const double clockTime = std::clamp(state.secondsFromRelease,
		    frames.front().time, frames.back().time / speedScale);
		const double time = speedScale == 1.0 ? clockTime
		    : (clockTime >= swingStart ? clockTime * speedScale
		        : frames.front().time + (clockTime - frames.front().time) *
		            (-.12 - frames.front().time) / (swingStart - frames.front().time));
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
		// Adapt the body to the configured release height without translating
		// feet below/above the mound surface.
		pose.scale = (state.releasePositionWorld.y - state.moundHeightMeters) / 1.82;
		const Vector3 base{state.releasePositionWorld.x + .42 * pose.scale, state.moundHeightMeters,
		    state.releasePositionWorld.z - .72 * pose.scale};
		const auto world = [&](const Vector3& p) { return base + p * pose.scale; };
		pose.pelvis = world(blend(a.pelvis, b.pelvis));
		pose.chest = world(blend(a.chest, b.chest));
		pose.head = pose.chest + Vector3{0, .32, -.02} * pose.scale;
		pose.rightShoulder = pose.chest + Vector3{-.23, .06, 0} * pose.scale;
		pose.leftShoulder = pose.chest + Vector3{.23, .06, 0} * pose.scale;
		pose.rightHand = world(blend(a.hand, b.hand));
		pose.leftHand = world(blend(a.glove, b.glove));
		pose.heldBall = pose.rightHand;
		pose.rightHip = pose.pelvis + Vector3{-.13, 0, 0} * pose.scale;
		pose.leftHip = pose.pelvis + Vector3{.13, 0, 0} * pose.scale;
		pose.rightFoot = world(blend(a.rightFoot, b.rightFoot));
		pose.leftFoot = world(blend(a.leftFoot, b.leftFoot));
		const auto joint =
		    [](const Vector3& start, const Vector3& end, const Vector3& bend, const double length)
		{
			const Vector3 delta = end - start;
			const double distance = Physics::Length(delta);
			const Vector3 axis = Physics::NormalizeOrZero(delta);
			const Vector3 normal = Physics::NormalizeOrZero(bend - axis * Physics::Dot(bend, axis));
			const double height =
			    std::sqrt((std::max)(0.0, length * length - distance * distance * .25));
			return (start + end) * .5 + normal * height;
		};
		pose.rightElbow =
		    joint(pose.rightShoulder, pose.rightHand, {-.8, 0, -.5}, .36 * pose.scale);
		pose.leftElbow = joint(pose.leftShoulder, pose.leftHand, {.8, -.2, 0}, .34 * pose.scale);
		pose.rightKnee = joint(pose.rightHip, pose.rightFoot, {0, 0, .9}, .48 * pose.scale);
		pose.leftKnee = joint(pose.leftHip, pose.leftFoot, {0, .4, 1}, .48 * pose.scale);
		if (state.throwsLeftHanded)
		{
			for (auto* point : { &pose.pelvis, &pose.chest, &pose.head,
			         &pose.rightShoulder, &pose.rightElbow, &pose.rightHand,
			         &pose.leftShoulder, &pose.leftElbow, &pose.leftHand,
			         &pose.rightHip, &pose.rightKnee, &pose.rightFoot,
			         &pose.leftHip, &pose.leftKnee, &pose.leftFoot, &pose.heldBall })
				point->x = 2.0 * state.releasePositionWorld.x - point->x;
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
				position.x = 2.0 * state.releasePositionWorld.x - position.x;
			mesh.vertices.push_back(position);
			return index;
		};
		const auto surface =
		    [&](std::uint32_t i, std::uint32_t j, std::uint32_t k, const Color& color)
		{
			// Reflection reverses orientation. Restore outward winding before
			// computing world-light shading for the reflected surface.
			if (state.throwsLeftHanded) std::swap(j, k);
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
			ellipsoid(pose.heldBall, {.037, .037, .037}, ball);
		}
	}
	class PitcherWorldMeshCache final
	{
	  public:
		[[nodiscard]] const IndexedWorldMesh& Get(PitcherPresentationState state)
		{
			state.secondsFromRelease = std::clamp(
			    state.secondsFromRelease, -PitcherWindupSeconds,
			    PitcherFollowThroughSeconds / std::clamp(state.armSpeedScale, .25, 2.0));
			const auto same = [](const auto& a, const auto& b)
			{
				return a.visible == b.visible && a.throwsLeftHanded == b.throwsLeftHanded &&
				       a.armSpeedScale == b.armSpeedScale && a.secondsFromRelease == b.secondsFromRelease &&
				       a.releasePositionWorld.x == b.releasePositionWorld.x &&
				       a.releasePositionWorld.y == b.releasePositionWorld.y &&
				       a.releasePositionWorld.z == b.releasePositionWorld.z &&
				       a.moundHeightMeters == b.moundHeightMeters;
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
