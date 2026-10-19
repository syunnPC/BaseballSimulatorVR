#include "../BaseballSimulatorVR/Common/HomePlateGeometry.hpp"
#include "../BaseballSimulatorVR/Common/PhaseTrajectoryOverlay.hpp"
#include "TestSupport.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace
{
	namespace Common = BaseballSimulatorVR::Common;
	using BaseballSimulatorVR::Physics::Vector3;
	constexpr double Front = 18.44;
	constexpr double Bottom = 0.55;
	constexpr double Top = 1.55;
	constexpr double Mid = (Bottom + Top) * 0.5;
	constexpr double Radius = 0.0366;
	constexpr double HalfWidth = Common::HomePlateWidthMeters * 0.5;
	constexpr double Depth = Common::HomePlateDepthMeters;
	constexpr double Gap = 1e-8;

	bool Touches(Vector3 from, Vector3 to, double radius = Radius)
	{
		return Common::SweptBallTouchesStrikeZone(from, to, radius, Front, Bottom, Top);
	}

	void CheckStationaryContacts()
	{
		const auto touches = [](Vector3 point) { return Touches(point, point); };
		Tests::Check(touches({0, Mid, Front - Radius}), "Sphere tangent to front cap is a strike");
		Tests::Check(!touches({0, Mid, Front - Radius - Gap}), "Sphere clear of front cap is a ball");
		Tests::Check(touches({0, Mid, Front + Depth + Radius}), "Sphere tangent to rear tip is a strike");
		Tests::Check(!touches({0, Mid, Front + Depth + Radius + Gap}), "Rear cap has finite extent");
		Tests::Check(touches({HalfWidth + Radius, Mid, Front + .1}), "Right side tangency counts");
		Tests::Check(touches({-HalfWidth - Radius, Mid, Front + .1}), "Left side tangency counts");
		Tests::Check(!touches({HalfWidth + Radius + Gap, Mid, Front + .1}), "Side gap remains a ball");
		Tests::Check(touches({0, Top + Radius, Front + .1}), "Top tangency counts");
		Tests::Check(touches({0, Bottom - Radius, Front + .1}), "Bottom tangency counts");
		Tests::Check(!touches({0, Top + Radius + Gap, Front + .1}), "Top gap remains a ball");
		Tests::Check(!touches({0, Bottom - Radius - Gap, Front + .1}), "Bottom gap remains a ball");

		const double diagonal = Radius / std::sqrt(2.0);
		Tests::Check(touches({HalfWidth + diagonal, Mid, Front - diagonal}),
		    "Euclidean tangency at front vertical corner counts");
		Tests::Check(!touches({HalfWidth + Radius * .8, Mid, Front - Radius * .8}),
		    "Independently expanded side and front planes cannot create a corner strike");
		Tests::Check(touches({HalfWidth + diagonal, Top + diagonal, Front + .1}),
		    "Tangency at top side edge counts");
		Tests::Check(!touches({HalfWidth + Radius * .8, Top + Radius * .8, Front + .1}),
		    "Independently expanded height and width cannot create an edge strike");
		const double corner = Radius / std::sqrt(3.0);
		Tests::Check(touches({HalfWidth + corner, Top + corner, Front - corner}),
		    "Three-dimensional corner tangency counts");
		Tests::Check(!touches({HalfWidth + Radius * .7, Top + Radius * .7, Front - Radius * .7}),
		    "A sphere missing a three-dimensional corner stays a ball");
		Tests::Check(touches({HalfWidth * .5 + diagonal, Mid, Front + Depth * .75 + diagonal}),
		    "Rear diagonal side uses its normal distance");
		Tests::Check(!touches({HalfWidth * .5 + diagonal + Gap, Mid, Front + Depth * .75 + diagonal + Gap}),
		    "A gap from the diagonal side stays a ball");
		Tests::Check(!touches({.2, Mid, Front + Depth * .9}),
		    "Removed rear square corners are outside the physical strike zone");
		Tests::Check(touches({Radius * .6, Mid, Front + Depth + Radius * .8}),
		    "Rear vertex contact uses radial distance rather than a depth-only test");
		Tests::Check(!touches({Radius * .8, Mid, Front + Depth + Radius * .8}),
		    "Rear vertex near miss cannot pass expanded diagonal planes");
	}

	void CheckSweeps()
	{
		const Vector3 before{0, Mid, Front - 10}, after{0, Mid, Front + 10};
		Tests::Check(Touches(before, after), "Fast sweep cannot skip the entire plate prism");
		Tests::Check(Touches(after, before), "Sweep direction does not change contact");
		Tests::Check(Touches(before, after, 0), "Zero-radius segment through the prism intersects");
		Tests::Check(!Touches({HalfWidth + Radius + Gap, Mid, Front - 10},
		                 {HalfWidth + Radius + Gap, Mid, Front + 10}),
		    "A fast lateral near miss stays a ball");
		Tests::Check(Touches({HalfWidth + .2, Mid, Front - .2}, {HalfWidth - .2, Mid, Front + .2}),
		    "Oblique sweep can enter through a front corner between both endpoints");
		const double diagonal = Radius / std::sqrt(2.0);
		const Vector3 tangent{HalfWidth + diagonal, Mid, Front - diagonal};
		Tests::Check(Touches(tangent - Vector3{.1, 0, .1}, tangent + Vector3{.1, 0, .1}),
		    "Interior sweep minimum detects a grazing vertical corner");
		const Vector3 miss = tangent + Vector3{Gap, 0, -Gap};
		Tests::Check(!Touches(miss - Vector3{.1, 0, .1}, miss + Vector3{.1, 0, .1}),
		    "Oblique grazing near miss is measured in Euclidean distance");
		Tests::Check(Touches({0, Top + Radius, Front - 1}, {0, Top + Radius, Front + 1}),
		    "Sweep parallel to the top face detects tangency");
		Tests::Check(!Touches({0, Top + Radius + Gap, Front - 1}, {0, Top + Radius + Gap, Front + 1}),
		    "Sweep parallel to the top face preserves a real gap");
		Tests::Check(Touches({0, Mid, Front - Radius}, {0, Mid, Front - .5}),
		    "Contact at the first segment endpoint counts while moving away");
		Tests::Check(Touches({0, Mid, Front - .5}, {0, Mid, Front - Radius}),
		    "Contact at the final segment endpoint counts");
		Tests::Check(!Touches({HalfWidth + .04, Mid, Front - 1}, {HalfWidth + .04, Mid, Front + 1}, .0366),
		    "Baseball radius cannot reach a four-centimetre side gap");
		Tests::Check(Touches({HalfWidth + .04, Mid, Front - 1}, {HalfWidth + .04, Mid, Front + 1}, .0485),
		    "Softball radius can reach the same gap");
		Tests::Check(Touches({0, Mid, Front + .1}, {0, Mid, Front + .1}, 0),
		    "Stationary zero-radius point inside counts");
		Tests::Check(!Touches({HalfWidth + Gap, Mid, Front + .1}, {HalfWidth + Gap, Mid, Front + .1}, 0),
		    "Zero-radius point outside does not gain a finite collision margin");
		Tests::Check(Common::SweptBallTouchesStrikeZone({0, Mid, -1}, {0, Mid, 1}, Radius, 0, Bottom, Top),
		    "Plate depth translates independently of pitch distance");
		Tests::Check(!Touches(before, after, -1), "Negative sphere radius is rejected");
		Tests::Check(!Touches({0, Mid, (std::numeric_limits<double>::quiet_NaN)()}, after),
		    "Nonfinite positions cannot produce a strike");
	}

	void CheckPlateAndZoneRendering()
	{
		const auto plate = Common::GetHomePlateVertices(Front);
		Tests::Check(plate.size() == 5 && plate[0].x == -HalfWidth && plate[1].x == HalfWidth &&
		                 plate[0].z == Front && plate[1].z == Front &&
		                 plate[2].z == Front + Depth * .5 && plate[3].x == 0 &&
		                 plate[3].z == Front + Depth,
		    "Shared plate polygon has the official dimensions with its tip toward the batter");
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {0, .8, 0};
		view.fovDownRadians = -1.2;
		std::size_t triangleCount = 0;
		const auto whiteTriangle = [&](const auto& a, const auto& b, const auto& c, const float* color)
		{
			Tests::Check(std::equal(Common::DebugWorldStyle::PlateColor.begin(),
				Common::DebugWorldStyle::PlateColor.end(), color), "Home plate triangles are opaque white");
			Tests::Check(std::isfinite(a.x) && std::isfinite(b.x) && std::isfinite(c.x),
			    "Home plate projects finite triangle coordinates");
			++triangleCount;
		};
		Common::EmitProjectedHomePlateOverlay(view, 1200, 900, whiteTriangle);
		Tests::Check(triangleCount == 3, "Filled pentagonal plate uses three triangles");
		Common::OverlayVisibilitySettings hidden{Common::PitchTrajectoryDisplayMode::None, false,
            Common::StrikeZoneDisplayMode::None, false};
		Common::PhaseTrajectoryTrails trails;
		triangleCount = 0;
		Common::EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
		    view, 1200, 900, trails, hidden, {}, whiteTriangle, 0.0F, false);
		Tests::Check(triangleCount == 3,
		    "Shared XR and desktop overlay draws home plate with trajectories, floor, zone and ball hidden");
		view.isValid = false;
		triangleCount = 0;
		Common::EmitProjectedHomePlateOverlay(view, 1200, 900, whiteTriangle);
		Tests::Check(triangleCount == 0, "Invalid camera emits no home plate");

		view.isValid = true;
		const auto savedZone = Common::GetStrikeZoneRuntimeSettings();
		const auto collectZone = [&](Common::StrikeZoneDisplayMode mode)
		{
			std::vector<std::array<double, 4>> lines;
			Common::EmitProjectedDebugWorldOverlay(view, 1200, 900, {}, {},
			    [](const auto&, LONG, const float*) {},
			    [&](const auto& a, const auto& b, LONG, const float*)
			    { lines.push_back({a.x, a.y, b.x, b.y}); }, false, false, mode, false);
			return lines;
		};
		auto settings = savedZone;
		settings.widthMeters = .20;
		Common::SetStrikeZoneRuntimeSettings(settings);
		const auto narrow = collectZone(Common::StrikeZoneDisplayMode::Box);
		settings.widthMeters = 1.2;
		Common::SetStrikeZoneRuntimeSettings(settings);
		const auto wide = collectZone(Common::StrikeZoneDisplayMode::Box);
		Common::SetStrikeZoneRuntimeSettings(savedZone);
		Tests::Check(narrow.size() == 15 && narrow == wide,
		    "Visible physical zone is the 15-edge plate prism regardless of configurable aiming-grid width");
		Tests::Check(collectZone(Common::StrikeZoneDisplayMode::Plane).size() == 4 &&
		                 collectZone(Common::StrikeZoneDisplayMode::None).empty(),
		    "Shared XR and desktop projection emits a four-edge Plane and no zone wires for None");
		std::size_t planeEdges = 0;
		Common::ForEachStrikeZoneWireSegment(Common::StrikeZoneDisplayMode::Plane,
		    {Front, Bottom, Top}, [&](const auto& a, const auto& b) {
			    ++planeEdges;
			    Tests::Check(a.z == Front && b.z == Front &&
			                     std::abs(a.x) == HalfWidth && std::abs(b.x) == HalfWidth &&
			                     (a.y == Bottom || a.y == Top) && (b.y == Bottom || b.y == Top),
			        "Plane is the front rectangle of the same official-width physical plate volume");
		    });
		Tests::Check(planeEdges == 4, "Front plane has exactly four distinct boundary edges");
	}
}

void RunHomePlateGeometryTests()
{
	CheckStationaryContacts();
	CheckSweeps();
	CheckPlateAndZoneRendering();
}
