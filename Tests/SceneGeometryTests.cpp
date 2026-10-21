#include "../BaseballSimulatorVR/Common/ProjectedSceneMeshes.hpp"
#include "TestSupport.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

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
		// The fan spreads past the foul lines so the infield and chalk have ground beneath.
		const double halfAngle = (std::max)(std::atan2(halfChord, depth),
		    (45. + Common::FieldMarkingStyle::FoulTerritoryMarginDegrees) * std::numbers::pi / 180.);
		Tests::Check(halfAngle > std::numbers::pi / 4., "The floor reaches beyond both foul lines");
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

	void CheckFieldMarkings(const double radius)
	{
		namespace D = Common::FieldDimensions;
		const double plateFront = -Common::DebugWorldLayout::BatterToPlateDistanceMeters;
		const double floor = Common::DebugWorldStyle::FloorMeshHeightMeters;
		const auto mesh = Common::BuildFieldMarkingsWorldMesh(plateFront, floor, radius);
		const double apexZ = plateFront + Common::HomePlateDepthMeters;
		Tests::Check(!mesh.triangles.empty(), "The field markings emit geometry");
		const auto hasVertex = [&](const double x, const double z, const double minimumHeight)
		{
			for (const auto& v : mesh.vertices)
				if (std::abs(v.x - x) < 1e-9 && std::abs(v.z - z) < 1e-9 && v.y >= minimumHeight) return true;
			return false;
		};
		for (const auto& triangle : mesh.triangles)
			for (const auto index : triangle.indices)
			{
				Tests::Check(index < mesh.vertices.size(), "Field marking triangles reference existing vertices");
				const auto& v = mesh.vertices[index];
				Tests::Check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.y > floor &&
				    std::hypot(v.x, v.z - apexZ) <=
				        (std::max)(radius, D::PitchersPlateDistanceMeters + D::InfieldArcRadiusMeters) + D::LineWidthMeters,
				    "Field markings lie above the floor and within the field");
			}
		// Bases at the official distances from the plate apex, standing on the field.
		const double diagonal = D::BaseDistanceMeters / std::numbers::sqrt2;
		const double bagTop = floor + D::BaseHeightMeters;
		Tests::Check(hasVertex(diagonal, apexZ - diagonal, bagTop) && hasVertex(-diagonal, apexZ - diagonal, bagTop),
		    "First and third bases sit against their 90 ft corners");
		const double s = D::BaseSizeMeters / std::numbers::sqrt2;
		Tests::Check(hasVertex(0., apexZ - D::SecondBaseDistanceMeters + s, bagTop) &&
		    hasVertex(0., apexZ - D::SecondBaseDistanceMeters - s, bagTop),
		    "Second base is centered 127 ft 3 3/8 in from the plate apex");
		// The grass line is 95 ft from the front of the pitcher's plate.
		int arcVertices = 0;
		for (const auto& v : mesh.vertices)
			if (std::abs(std::hypot(v.x, apexZ - v.z - D::PitchersPlateDistanceMeters) - D::InfieldArcRadiusMeters) < 1e-9)
				++arcVertices;
		Tests::Check(arcVertices > 60, "The infield skin ends on the 95 ft arc around the pitcher's plate");
		// Batter's boxes: 4 ft by 6 ft, 6 in from the plate, centered on it lengthwise.
		const double inner = Common::HomePlateWidthMeters * .5 + D::BattersBoxPlateGapMeters;
		const double outer = inner + D::BattersBoxWidthMeters;
		const double front = Common::HomePlateDepthMeters * .5 + D::BattersBoxLengthMeters * .5;
		const double back = front - D::BattersBoxLengthMeters;
		const double w = D::LineWidthMeters * .5;
		for (const double side : {-1., 1.})
			// Line corners: the side lines run past the box ends by half a line width.
			Tests::Check(hasVertex(side * inner + w, apexZ - (front + w), floor) &&
			    hasVertex(side * outer - w, apexZ - (back - w), floor) &&
			    std::abs(outer - inner - 4. * .3048) < 1e-12 && std::abs(front - back - 6. * .3048) < 1e-12,
			    "Both batter's boxes have the official size and position beside the plate");
		// Layers stay in order above the floor and beneath the plate near home.
		const auto heightAt = [&](const double x, const double z)
		{
			double best = -1.;
			for (const auto& v : mesh.vertices)
				if (std::abs(v.x - x) < 1e-9 && std::abs(v.z - z) < 1e-9) best = (std::max)(best, v.y);
			return best;
		};
		Tests::Check(heightAt(0., apexZ) < Common::DebugWorldStyle::HomePlateHeightMeters,
		    "Dirt around home stays beneath the plate");
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

	void CheckNearPlaneClippingAndPerspectiveDepth()
	{
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {0., .03, 0.};
		view.fovLeftRadians = -.8;
		view.fovRightRadians = .8;
		view.fovUpRadians = .8;
		view.fovDownRadians = -.8;
		const Common::PreparedWorldProjection projection(view, 1024, 1024,
		    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
		Common::IndexedWorldMesh mesh;
		mesh.vertices = {{-1., 0., 1.}, {1., 0., 1.}, {0., 0., -3.}};
		mesh.triangles = {{{0, 2, 1}, {1.F, 1.F, 1.F, 1.F}}};
		Common::WorldMeshProjectionWorkspace workspace;
		std::size_t count = 0;
		double minimumDepth = 1.;
		workspace.Emit(mesh, projection, [&](const auto& a, const auto& b, const auto& c, const float*)
		    {
			    ++count;
			    for (const auto& point : {a, b, c})
			    {
				    Tests::Check(std::isfinite(point.x) && std::isfinite(point.y),
				        "Near-clipped floor faces project finite vertices");
				    minimumDepth = (std::min)(minimumDepth, static_cast<double>(point.depth));
			    }
		    });
		Tests::Check(count > 0 && minimumDepth < 1e-6,
		    "A floor face extending behind the eye keeps its visible part up to the near plane");

		// The rasterizer interpolates z/w affinely in screen space. With the
		// rebuilt clip depth an interior point gets exactly its own depth.
		view.position = {};
		const Common::PreparedWorldProjection flat(view, 1024, 1024,
		    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
		const std::array<BaseballSimulatorVR::Physics::Vector3, 3> corners{
		    BaseballSimulatorVR::Physics::Vector3{-2., -1., -1.}, BaseballSimulatorVR::Physics::Vector3{3., -1., -60.}, BaseballSimulatorVR::Physics::Vector3{-1., 2., -20.}};
		const BaseballSimulatorVR::Physics::Vector3 interior = corners[0] * .6 + corners[1] * .3 + corners[2] * .1;
		std::array<Common::ScreenPoint, 3> projected{};
		Common::ScreenPoint center{};
		for (std::size_t i = 0; i < corners.size(); ++i)
			Tests::Check(flat.Project(corners[i], &projected[i], false), "Depth test corners are visible");
		Tests::Check(flat.Project(interior, &center, false), "Depth test interior point is visible");
		const auto area = [](const Common::ScreenPoint& a, const Common::ScreenPoint& b, const Common::ScreenPoint& c)
		{ return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
		const double total = area(projected[0], projected[1], projected[2]);
		const std::array<double, 3> barycentric{area(center, projected[1], projected[2]) / total,
		    area(projected[0], center, projected[2]) / total, area(projected[0], projected[1], center) / total};
		const auto hardwareDepth = [](const Common::ScreenPoint& point)
		{
			const auto clip = Common::ClipDepthFromNormalizedDepth(point.depth,
			    Common::DebugWorldLayout::NearClipMeters, Common::DebugWorldLayout::FarClipMeters);
			return static_cast<double>(clip.z) / clip.w;
		};
		double interpolated = 0.;
		double linear = 0.;
		for (std::size_t i = 0; i < 3; ++i)
		{
			interpolated += barycentric[i] * hardwareDepth(projected[i]);
			linear += barycentric[i] * projected[i].depth;
		}
		Tests::Check(std::abs(interpolated - hardwareDepth(center)) < 2e-6,
		    "Screen-affine interpolation of the clip depth reproduces the interior point's depth");
		Tests::Check(std::abs(linear - center.depth) > 1e-3,
		    "Interpolating normalized linear depth directly would mis-order large triangles");
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
	for (double radius : {10., 100., 300.})
	{
		CheckFieldMarkings(radius);
	}
	CheckCacheInvalidation();
	CheckIndexedProjection();
	CheckNearPlaneClippingAndPerspectiveDepth();
}
