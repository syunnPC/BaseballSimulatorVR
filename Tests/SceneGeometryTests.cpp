#include "../BaseballSimulatorVR/Common/ProjectedSceneMeshes.hpp"
#include "TestSupport.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace
{
	namespace Common = BaseballSimulatorVR::Common;
	using BaseballSimulatorVR::Physics::Vector3;

	// Quantized triangle snapshots cover positions, colors, topology and order.
	std::uint64_t Fingerprint(const Common::IndexedWorldMesh& mesh)
	{
		std::uint64_t hash = 14695981039346656037ULL;
		const auto add = [&](double value, double scale)
		{
			const auto quantized =
			    static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(value * scale)));
			for (int byte = 0; byte < 8; ++byte)
			{
				hash ^= (quantized >> (byte * 8)) & 255;
				hash *= 1099511628211ULL;
			}
		};
		for (const auto& face : mesh.triangles)
		{
			for (const auto index : face.indices)
			{
				Tests::Check(index < mesh.vertices.size(),
				    "Indexed mesh triangle references an existing vertex");
				const auto& point = mesh.vertices[index];
				add(point.x, 1e8);
				add(point.y, 1e8);
				add(point.z, 1e8);
			}
			for (const float color : face.color)
			{
				add(color, 1e6);
			}
		}
		return hash;
	}

	Common::PitcherPresentationState PitcherState(double time, int shape)
	{
		return {true, time,
		    {-.31, shape == 0 ? .7 : (shape == 1 ? 1.98 : 4.2), shape == 2 ? -30. : -19.94},
		    shape == 0 ? 0. : (shape == 1 ? .18 : .5)};
	}

	void CheckPitcherMeshStyles()
	{
		constexpr std::array times{-1.2, -.85, -.42, -.12, 0., .22, .6};
		Common::IndexedWorldMesh mesh;
		Common::PitcherWorldMeshCache cache;
		for (const double time : times)
		{
			for (const double releaseHeight : {.1, 1.2, 1.8})
			{
				auto state = PitcherState(time, 1);
				state.releasePositionWorld.y = state.moundHeightMeters + releaseHeight;
				Common::BuildPitcherWorldMesh(state, mesh);
				Tests::Check(mesh.triangles.size() == (time < 0 ? 8482 : 8290),
				    "Every delivery style retains the complete pitcher, mound and held ball tessellation");
				Tests::Check(Fingerprint(cache.Get(state)) == Fingerprint(mesh),
				    "Cached geometry matches the direct mesh across every style and animation key");
				Tests::Check(mesh.vertices.size() < mesh.triangles.size(),
				    "Adjacent actor faces share vertices for projection");
			}
		}
	}

	void CheckOriginalFloor(double width, double radius)
	{
		const auto mesh = Common::BuildFloorWorldMesh(width, radius);
		const double chord = (std::max)(.20, width) + 2.;
		const double depth =
		    (std::max)(.10, std::abs(Common::DebugWorldLayout::BatterToPlateDistanceMeters));
		const double halfChord = chord * .5;
		const double halfAngle = std::atan2(halfChord, depth);
		const double inner = std::sqrt(depth * depth + halfChord * halfChord);
		const double outer = (std::max)(inner + 1., radius);
		const double step = (std::max)(Common::DebugWorldStyle::FloorMeshTileSizeMeters * 2., .50);
		const int rows = std::clamp(static_cast<int>(std::ceil((outer - inner) / step)), 1, 120);
		const int columns =
		    std::clamp(static_cast<int>(std::ceil(halfAngle * 2. * outer / step)), 16, 180);
		Tests::Check(mesh.triangles.size() == static_cast<std::size_t>(rows) * columns * 2,
		    "Floor retains its original tessellation at all configured sizes");
		Tests::Check(mesh.vertices.size() == static_cast<std::size_t>(rows + 1) * (columns + 1),
		    "Floor grid shares vertices without removing any cells");
		const auto point = [&](int row, int column)
		{
			const double r = inner + (outer - inner) * (static_cast<double>(row) / rows);
			const double a = -halfAngle + halfAngle * 2. * (static_cast<double>(column) / columns);
			return Vector3{
			    std::sin(a) * r, Common::DebugWorldStyle::FloorMeshHeightMeters, -std::cos(a) * r};
		};
		for (int row = 0; row < rows; ++row)
		{
			for (int column = 0; column < columns; ++column)
			{
				const std::array expected{point(row, column), point(row, column + 1),
				    point(row + 1, column + 1), point(row, column), point(row + 1, column + 1),
				    point(row + 1, column)};
				const auto& color = (row + column) % 2 == 0
				                        ? Common::DebugWorldStyle::FloorMeshTileColorA
				                        : Common::DebugWorldStyle::FloorMeshTileColorB;
				const double radialBlend = rows > 1 ? static_cast<double>(row) / (rows - 1) : 0.;
				const double angleBlend =
				    std::abs((static_cast<double>(column) + .5) / columns - .5) * 2.;
				const float brightness = static_cast<float>(
				    std::clamp(.94 - radialBlend * .26 - angleBlend * .08, .55, 1.));
				for (int face = 0; face < 2; ++face)
				{
					const auto& triangle =
					    mesh.triangles[(static_cast<std::size_t>(row) * columns + column) * 2 +
					                   face];
					for (int corner = 0; corner < 3; ++corner)
					{
						const auto actual = mesh.vertices[triangle.indices[corner]];
						Tests::Check(BaseballSimulatorVR::Physics::Length(
						                 actual - expected[face * 3 + corner]) == 0.,
						    "Indexed floor preserves every original triangle corner exactly");
					}
					Tests::Check(
					    triangle.color == std::array{color[0] * brightness, color[1] * brightness,
					                          color[2] * brightness, color[3]},
					    "Indexed floor retains exact tile shading");
				}
			}
		}
	}

	void CheckCacheInvalidation()
	{
		Common::FloorWorldMeshCache floor;
		const auto firstFloor = Fingerprint(floor.Get(.46, 100));
		(void)floor.Get(.46, 100);
		Tests::Check(floor.BuildCount() == 1,
		    "Unchanged ground settings build the world mesh once across eyes and frames");
		Tests::Check(Fingerprint(floor.Get(.6, 100)) != firstFloor && floor.BuildCount() == 2,
		    "Changing strike zone width updates floor geometry");
		(void)floor.Get(.6, 145);
		Tests::Check(floor.BuildCount() == 3, "Changing field radius updates floor geometry");
		(void)floor.Get(.6, 100);
		Tests::Check(floor.BuildCount() == 4,
		    "Floor cache retains one configuration rather than growing with edits");

		Common::PitcherWorldMeshCache actor;
		auto state = PitcherState(-1.2, 1);
		const auto initial = Fingerprint(actor.Get(state));
		const auto* vertices = actor.Get(state).vertices.data();
		const auto* triangles = actor.Get(state).triangles.data();
		Tests::Check(actor.BuildCount() == 1, "Both eyes reuse a single posed actor world mesh");
		state.secondsFromRelease = -2.;
		(void)actor.Get(state);
		Tests::Check(actor.BuildCount() == 1, "Clamped windup time shares the same cached pose");
		state.secondsFromRelease = -.5;
		Tests::Check(Fingerprint(actor.Get(state)) != initial && actor.BuildCount() == 2,
		    "Animation advances invalidate the actor pose");
		Tests::Check(vertices == actor.Get(state).vertices.data() &&
		                 triangles == actor.Get(state).triangles.data(),
		    "Animated poses reuse allocated mesh storage");
		state.releasePositionWorld.x += .1;
		(void)actor.Get(state);
		state.releasePositionWorld.y += .1;
		(void)actor.Get(state);
		state.releasePositionWorld.z += .1;
		(void)actor.Get(state);
		state.moundHeightMeters += .1;
		(void)actor.Get(state);
		Tests::Check(actor.BuildCount() == 6,
		    "All release coordinates and mound height invalidate the actor mesh");
		state.visible = false;
		Tests::Check(actor.Get(state).triangles.empty(),
		    "Hiding an actor clears the cached visible geometry");
		state.visible = true;
		Tests::Check(
		    !actor.Get(state).triangles.empty(), "Showing an actor rebuilds its visible geometry");
		state.secondsFromRelease = 2.;
		(void)actor.Get(state);
		const auto count = actor.BuildCount();
		state.secondsFromRelease = .6;
		(void)actor.Get(state);
		Tests::Check(
		    actor.BuildCount() == count, "Clamped follow-through time shares the same cached pose");
	}

	void CheckIndexedProjection()
	{
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {.032, 1.65, .1};
		view.orientation = {.12, -.08, .03};
		view.orientationW =
		    std::sqrt(1. - BaseballSimulatorVR::Physics::Dot(view.orientation, view.orientation));
		view.fovLeftRadians = -.82;
		view.fovRightRadians = .70;
		view.fovUpRadians = .69;
		view.fovDownRadians = -.78;
		Common::WorldMeshProjectionWorkspace workspace;
		const auto floor = Common::BuildFloorWorldMesh(.46, 100.);
		for (const LONG width : {1L, 800L, 2048L})
		{
			const Common::PreparedWorldProjection projection(view, width, 1920,
			    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
			std::size_t next = 0;
			workspace.Emit(floor, projection,
			    [&](const auto& a, const auto& b, const auto& c, const float* color)
			    {
				    Common::ScreenPoint expected[3];
				    while (next < floor.triangles.size())
				    {
					    const auto& face = floor.triangles[next++];
					    const bool visible =
					        Common::TryProjectWorldTriangle(floor.vertices[face.indices[0]],
					            floor.vertices[face.indices[1]], floor.vertices[face.indices[2]],
					            view, width, 1920, &expected[0], &expected[1], &expected[2]);
					    if (!visible)
					    {
						    continue;
					    }
					    const std::array actual{a, b, c};
					    for (std::size_t corner = 0; corner < 3; ++corner)
					    {
						    Tests::Check(actual[corner].x == expected[corner].x &&
						                     actual[corner].y == expected[corner].y &&
						                     actual[corner].depth == expected[corner].depth,
						        "Shared vertex projection preserves coordinates and depth exactly");
					    }
					    Tests::Check(std::equal(face.color.begin(), face.color.end(), color),
					        "Indexed projection retains face colors");
					    return;
				    }
				    Tests::Check(false, "Indexed projection emits no extra triangle");
			    });
			for (; next < floor.triangles.size(); ++next)
			{
				const auto& face = floor.triangles[next];
				Common::ScreenPoint a, b, c;
				Tests::Check(!Common::TryProjectWorldTriangle(floor.vertices[face.indices[0]],
				                 floor.vertices[face.indices[1]], floor.vertices[face.indices[2]],
				                 view, width, 1920, &a, &b, &c),
				    "Indexed projection never omits a triangle accepted by the direct path");
			}
		}
		const Common::PreparedWorldProjection projection(view, 2048, 1920,
		    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
		Tests::Check(!projection.Project({0, 0, -1}, nullptr),
		    "Prepared projection rejects a null destination");
		view.isValid = false;
		Common::ScreenPoint point;
		Tests::Check(!Common::PreparedWorldProjection(view, 2048, 1920, .05, 400)
		                 .Project({0, 0, -1}, &point),
		    "An invalid view cannot reuse a previously prepared projection");
	}
} // namespace

void RunSceneGeometryTests()
{
	CheckPitcherMeshStyles();
	for (double radius : {10., 100., 300.})
	{
		for (double width : {.1, .46, 1.2})
		{
			CheckOriginalFloor(width, radius);
		}
	}
	CheckCacheInvalidation();
	CheckIndexedProjection();
}
