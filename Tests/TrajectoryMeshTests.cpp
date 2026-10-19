#include "../BaseballSimulatorVR/Common/DebugWorldProjection.hpp"
#include "TestSupport.hpp"

#include <map>

namespace
{
	using namespace BaseballSimulatorVR;
	bool Same(const Physics::Vector3& a, const Physics::Vector3& b)
	{ return a.x == b.x && a.y == b.y && a.z == b.z; }

	void CheckWorldGeometry()
	{
		const std::vector<Physics::Vector3> path{{0.,1.,-2.}, {.1,1.2,-2.3},
		    {.2,1.3,-2.6}, {.3,1.2,-2.9}, {.4,.5,-3.2}, {.5,.04,-3.5},
		    {.6,.2,-3.8}, {.8,.04,-4.1}, {1.,.04,-4.5}};
		constexpr double Radius = .03;
		Common::TrajectoryTubeMesh mesh, prefix;
		mesh.Build(path, {}, Radius);
		Tests::Check(mesh.vertices.size() == path.size() * 4,
		    "Trajectory has four shared world vertices per sample rather than disconnected ribbons");
		for (std::size_t i = 0; i < path.size(); ++i)
		for (std::size_t side = 0; side < 4; ++side)
			Tests::Check(std::abs(Physics::Length(mesh.vertices[i * 4 + side] - path[i]) - Radius) < 1e-12,
			    "Airborne, bounce and ground segments keep the same physical trail radius");
		std::vector<Physics::Vector3> partial(path.begin(), path.begin() + 5);
		prefix.Build(partial, {}, Radius);
		for (std::size_t i = 0; i < prefix.vertices.size(); ++i)
			Tests::Check(Same(prefix.vertices[i], mesh.vertices[i]),
			    "Appending flight samples never shifts or resizes previously drawn rings");
		std::map<std::pair<std::uint32_t,std::uint32_t>, unsigned> edges;
		std::size_t triangles = 0;
		mesh.ForEachTriangle([&](auto a, auto b, auto c)
		{
			++triangles;
			Tests::Check(Physics::LengthSquared(Physics::Cross(mesh.vertices[b] - mesh.vertices[a],
			    mesh.vertices[c] - mesh.vertices[a])) > 1e-15,
			    "Curved and bouncing trajectory faces retain nonzero area");
			for (const auto edge : {std::pair{a,b}, std::pair{b,c}, std::pair{c,a}})
				++edges[(std::minmax)(edge.first, edge.second)];
		});
		Tests::Check(triangles == 8 * (path.size() - 1) + 4,
		    "Four-sided trajectory uses eight side triangles per sample and four endpoint cap triangles");
		for (const auto& [edge, uses] : edges)
		{
			(void)edge;
			Tests::Check(uses == 2, "Every mesh edge has two incident faces, including joins and endpoint caps");
		}
		const auto original = mesh.vertices;
		Common::ViewProjectionState view;
		view.isValid = true;
		Common::TrajectoryTubeProjectionWorkspace workspace;
		for (const double eye : {-.032, .032, -.2, .2})
		{
			view.position = {eye,1.6,0.};
			workspace.Emit(mesh, view, 1024, 1024, .05, 400., Common::DebugWorldStyle::TrailMeshColor.data(),
			    [](const auto&, const auto&, const auto&, const float*) {});
			for (std::size_t i = 0; i < original.size(); ++i)
				Tests::Check(Same(original[i], mesh.vertices[i]),
				    "Head movement and both XR eyes project the identical fixed world geometry");
		}
		std::vector<Physics::Vector3> vertical{{0.,0.,-2.}, {0.,1.,-2.},
		    {0.,1.,-2.}, {0.,2.,-2.}, {0.,1.,-2.}};
		mesh.Build(vertical, {}, Radius);
		Tests::Check(mesh.vertices.size() == 16, "Repeated samples do not create zero-length trajectory spans");
		for (const auto& p : mesh.vertices)
			Tests::Check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
			    "Vertical spans and exact direction reversals retain finite transported frames");
	}

	void CheckEndOnAndNearClipping()
	{
		Common::ViewProjectionState view;
		view.isValid = true;
		Common::TrajectoryTubeMesh mesh;
		Common::TrajectoryTubeProjectionWorkspace workspace;
		mesh.Build({{0.,0.,-2.}, {0.,0.,-3.}, {0.,0.,-4.}}, {}, .03);
		for (const double eye : {0., -.032, .032})
		{
			view.position = {eye,0.,0.};
			std::size_t count = 0;
			double area = 0.;
			workspace.Emit(mesh, view, 1024, 1024, .05, 400., Common::DebugWorldStyle::TrailMeshColor.data(),
			    [&](const auto& a, const auto& b, const auto& c, const float*)
			    {
				    ++count;
				    area += std::abs((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x));
			    });
			Tests::Check(count > 0 && area > 1.,
			    "A trajectory aimed directly toward the head retains visible nondegenerate coverage in both eyes");
		}
		view.position = {};
		mesh.Build({{-.1,0.,-.01}, {.1,0.,-.2}}, {}, .01);
		std::size_t count = 0;
		double minimumDepth = 1.;
		workspace.Emit(mesh, view, 1024, 1024, .05, 400., Common::DebugWorldStyle::TrailMeshColor.data(),
		    [&](const auto& a, const auto& b, const auto& c, const float*)
		    {
			    ++count;
			    for (const auto& point : {a,b,c})
			    {
				    minimumDepth = (std::min)(minimumDepth, static_cast<double>(point.depth));
				    Tests::Check(std::isfinite(point.x) && std::isfinite(point.y) && point.depth >= 0.F,
				        "Near-clipped trajectory faces project finite visible vertices");
			    }
		    });
		Tests::Check(count > 0 && minimumDepth < 1e-8,
		    "A span crossing the near plane keeps its visible portion up to the clipped boundary");
		mesh.Build({{-.1,0.,.1}, {.1,0.,.2}}, {}, .01);
		count = 0;
		workspace.Emit(mesh, view, 1024, 1024, .05, 400., Common::DebugWorldStyle::TrailMeshColor.data(),
		    [&](const auto&, const auto&, const auto&, const float*) { ++count; });
		Tests::Check(count == 0, "A trajectory wholly behind the viewer emits no projected triangles");
	}

	void CheckRoutingAndCost()
	{
		std::vector<Physics::Vector3> path;
		for (int i = 0; i < 1500; ++i) path.push_back({i * .04, 1.6, -4. - i * .01});
		Common::TrajectoryTubeMesh mesh;
		mesh.Build(path, {}, .03);
		const auto* allocation = mesh.vertices.data();
		const auto capacity = mesh.vertices.capacity();
		mesh.Build(path, {}, .03);
		Tests::Check(mesh.vertices.data() == allocation && mesh.vertices.capacity() == capacity &&
		    mesh.vertices.size() == 6000,
		    "A 1500-sample trajectory reuses its bounded four-vertices-per-point storage each view");
		std::size_t triangles = 0;
		mesh.ForEachTriangle([&](auto, auto, auto) { ++triangles; });
		Tests::Check(triangles == 11996, "Long trajectories retain a fixed low polygon cost without decimation");
		const double offset = Common::GetPitchDistanceMeters() + Common::DebugWorldLayout::BatterToPlateDistanceMeters;
		const std::vector<Physics::Vector3> visible{{-.4,1.6,offset-4.}, {0.,1.6,offset-4.}, {.4,1.6,offset-4.}};
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {0.,1.6,0.};
		Physics::BallState ball;
		std::size_t uniform = 0;
		Common::EmitProjectedBallAndTrajectoryMeshOverlay(view,1024,1024,visible,ball,
		    [&](const auto&,const auto&,const auto&,const float* color)
		    {
			    ++uniform;
			    Tests::Check(color[0] == Common::DebugWorldStyle::TrailMeshColor[0] &&
			        std::abs(color[3] - Common::DebugWorldStyle::TrailMeshColor[3] * .5F) < 1e-6F,
			        "Batted mesh retains uniform color and requested opacity throughout its complete history");
		    }, .5F, 1.F, false, Common::TrajectoryColorMode::Uniform);
		Tests::Check(uniform > 0, "Completed batted paths remain visible even after pitch fade progress reaches one");
		std::size_t fadedPitch = 0;
		Common::EmitProjectedBallAndTrajectoryMeshOverlay(view,1024,1024,visible,ball,
		    [&](const auto&,const auto&,const auto&,const float*) { ++fadedPitch; },
		    1.F, 1.F, false, Common::TrajectoryColorMode::Fading);
		Tests::Check(fadedPitch == 0, "Pitch ribbon fading remains unchanged by the batted world mesh");
	}
}

void RunTrajectoryMeshTests()
{
	CheckWorldGeometry();
	CheckEndOnAndNearClipping();
	CheckRoutingAndCost();
}
