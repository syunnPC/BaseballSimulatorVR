#include "../BaseballSimulatorVR/Common/StadiumMesh.hpp"
#include "../BaseballSimulatorVR/Simulation/BattedBallOutcome.hpp"
#include "../BaseballSimulatorVR/Simulation/StadiumCollision.hpp"
#include "TestSupport.hpp"

#include <cmath>
#include <numbers>
#include <string>

namespace
{
	using namespace BaseballSimulatorVR;
	namespace S = Common::Stadium;
	constexpr double Feet = .3048;

	void CheckLayout()
	{
		Tests::Check(Tests::Near(S::BoundaryDistanceMeters(0.0), 400.0 * Feet, 1e-6) &&
		        Tests::Near(S::BoundaryDistanceMeters(45.0), 330.0 * Feet, 1e-6) &&
		        Tests::Near(S::BoundaryDistanceMeters(-45.0), 330.0 * Feet, 1e-6),
		    "The outfield wall stands 400 ft to center and 330 ft at the foul poles");
		for (const double bearing : {135.0, 150.0, 180.0, -150.0, -135.0})
		{
			Tests::Check(Tests::Near(S::BoundaryDistanceMeters(bearing), 60.0 * Feet, .01),
			    "The backstop is 60 ft behind home plate");
		}
		double previous = S::BoundaryDistanceMeters(45.0);
		for (double bearing = 46.0; bearing <= 135.0; bearing += 1.0)
		{
			const double distance = S::BoundaryDistanceMeters(bearing);
			Tests::Check(distance < previous && Tests::Near(distance, S::BoundaryDistanceMeters(-bearing), 1e-6),
			    "The foul-side walls close in symmetrically from the poles toward the backstop");
			previous = distance;
		}
		// 60 ft of foul territory beside first base.
		const double c = 1.0 / std::numbers::sqrt2;
		const double bag = 90.0 * Feet;
		Tests::Check(S::IsInsidePlayingField(bag * c + 17.5 * c, bag * c - 17.5 * c) &&
		        !S::IsInsidePlayingField(bag * c + 19.0 * c, bag * c - 19.0 * c),
		    "Foul territory reaches 60 ft beyond the first-base line at the bag");
		Tests::Check(S::IsInsidePlayingField(0.0, 121.0) && !S::IsInsidePlayingField(0.0, 123.0) &&
		        S::IsInsidePlayingField(0.0, -18.0) && !S::IsInsidePlayingField(0.0, -18.6),
		    "The playing field ends at the center-field wall and the backstop");
		for (const auto& point : S::Boundary())
		{
			Tests::Check(Tests::Near(std::hypot(point.normalU, point.normalV), 1.0, 1e-9) &&
			        point.normalU * point.u + point.normalV * point.v > 0.0,
			    "Wall normals are unit length and face away from home plate");
		}
		const auto behind = S::BehindWall(0.0, 400.0 * Feet + 2.0);
		Tests::Check(Tests::Near(behind.behindWallMeters, 2.0, 1e-6) && behind.nearest.section == S::Section::BattersEye &&
		        S::BehindWall(0.0, 100.0).behindWallMeters < 0.0,
		    "Distances behind the wall find the section, and points in the field are inside");
		Tests::Check(S::StandsTreadHeightMeters(S::Section::Outfield, 0.0) == S::FenceHeightMeters &&
		        S::StandsTreadHeightMeters(S::Section::Outfield, 10.0) > S::StandsTreadHeightMeters(S::Section::Outfield, 5.0) &&
		        S::StandsTreadHeightMeters(S::Section::Foul, 0.0) == S::FoulWallHeightMeters,
		    "The seats start level with the top of the wall and step up behind it");
	}

	Physics::BallState Ball(const S::BoundaryPoint& edge, const double behind, const double height,
	    const Physics::Vector3& velocity, const Simulation::StadiumPlacement& placement)
	{
		Physics::BallState ball;
		ball.position = {edge.u + edge.normalU * behind, placement.groundHeightMeters + height,
		    placement.plateApexDepthMeters - (edge.v + edge.normalV * behind)};
		ball.velocity = velocity;
		return ball;
	}

	void CheckCollision()
	{
		const Simulation::StadiumPlacement placement{18.44 + Common::HomePlateDepthMeters, -.18};
		S::BoundaryPoint center;
		center.v = S::CenterFenceDistanceMeters;
		center.normalV = 1.0;
		// Low drive into the center-field wall.
		auto from = Ball(center, -2.0, 1.5, {0.0, -1.0, -30.0}, placement);
		auto to = Ball(center, 2.0, 1.4, {0.0, -1.0, -30.0}, placement);
		auto result = Simulation::ApplyStadiumCollision(from, &to, placement);
		Tests::Check(result.contact == Simulation::StadiumContact::Wall && result.reachedWall && !result.clearedWall &&
		        Tests::Near(result.wallFrameFraction, .5, 1e-6) &&
		        Tests::Near(placement.plateApexDepthMeters - to.position.z, S::CenterFenceDistanceMeters - .037, 1e-6) &&
		        Tests::Near(to.velocity.z, 30.0 * .45, 1e-9),
		    "A ball below the top of the wall rebounds off its padding at the wall face");
		// Over the wall into the bleachers.
		from = Ball(center, -2.0, 4.0, {0.0, -2.0, -30.0}, placement);
		to = Ball(center, 2.0, 3.8, {0.0, -2.0, -30.0}, placement);
		result = Simulation::ApplyStadiumCollision(from, &to, placement);
		Tests::Check(result.reachedWall && result.clearedWall && result.contact == Simulation::StadiumContact::None &&
		        Tests::Near(to.velocity.z, -30.0, 1e-12) && Tests::Near(result.wallHeightMeters, 3.9, 1e-6),
		    "A ball over the top of the wall carries on into the seats");
		// Dropping onto a row of seats.
		from = Ball(center, 5.0, 4.9, {0.0, -3.0, -2.0}, placement);
		to = Ball(center, 5.0, 4.5, {0.0, -3.0, -2.0}, placement);
		result = Simulation::ApplyStadiumCollision(from, &to, placement);
		const double tread = S::StandsTreadHeightMeters(S::Section::BattersEye, 5.0);
		Tests::Check(result.contact == Simulation::StadiumContact::Stands &&
		        Tests::Near(to.position.y - placement.groundHeightMeters, tread + .037, 1e-9) &&
		        Tests::Near(to.velocity.y, .9, 1e-9),
		    "A ball dropping into the seats bounces weakly off the row it lands on");
		// Out over the top of the stands.
		to = Ball(center, 40.0, 5.0, {0.0, -3.0, -30.0}, placement);
		result = Simulation::ApplyStadiumCollision(to, &to, placement);
		Tests::Check(result.contact == Simulation::StadiumContact::LeftStadium, "A ball beyond the stands has left the park");
		// A wild pitch into the backstop netting.
		S::BoundaryPoint backstop;
		backstop.v = -S::BackstopDistanceMeters;
		backstop.normalV = -1.0;
		from = Ball(backstop, -1.0, 4.0, {0.0, 0.0, 30.0}, placement);
		to = Ball(backstop, 1.0, 4.0, {0.0, 0.0, 30.0}, placement);
		result = Simulation::ApplyStadiumCollision(from, &to, placement);
		Tests::Check(result.contact == Simulation::StadiumContact::Wall && result.wallSection == S::Section::Backstop &&
		        Tests::Near(to.velocity.z, -30.0 * .12, .1),
		    "The netting behind the plate stops balls dead");
		// Inside the field nothing happens.
		from = Ball(center, -30.0, 1.0, {0.0, -1.0, -30.0}, placement);
		to = Ball(center, -29.0, 1.0, {0.0, -1.0, -30.0}, placement);
		const auto before = to;
		result = Simulation::ApplyStadiumCollision(from, &to, placement);
		Tests::Check(result.contact == Simulation::StadiumContact::None && !result.reachedWall &&
		        to.position.z == before.position.z && to.velocity.z == before.velocity.z,
		    "Balls in the field are left alone");
	}

	void CheckMesh()
	{
		const double floor = -.01;
		const auto mesh = Common::BuildStadiumWorldMesh(-1.5, floor);
		const double apexZ = -1.5 + Common::HomePlateDepthMeters;
		Tests::Check(mesh.triangles.size() > 2000 && mesh.triangles.size() < 20000,
		    "The stadium mesh is detailed but bounded");
		bool poleTop = false, wallTop = false;
		for (const auto& triangle : mesh.triangles)
		{
			for (const auto index : triangle.indices)
			{
				Tests::Check(index < mesh.vertices.size(), "Stadium triangles reference existing vertices");
				const auto& v = mesh.vertices[index];
				Tests::Check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.y >= floor - 1e-9,
				    "Stadium geometry is finite and stands on the ground");
			}
		}
		for (const auto& v : mesh.vertices)
		{
			const double u = v.x, along = apexZ - v.z, height = v.y - floor;
			if (std::abs(std::abs(u) - S::LineFenceDistanceMeters / std::numbers::sqrt2) < .2 &&
			    std::abs(along - S::LineFenceDistanceMeters / std::numbers::sqrt2) < .2 &&
			    Tests::Near(height, S::FoulPoleHeightMeters, 1e-9))
				poleTop = true;
			const double bearing = std::atan2(u, along) * 180.0 / std::numbers::pi;
			if (std::abs(bearing) < 3.0 && Tests::Near(std::hypot(u, along), S::FenceDistanceMeters(bearing), 1e-6) &&
			    Tests::Near(height, S::FenceHeightMeters, 1e-9))
				wallTop = true;
		}
		Tests::Check(poleTop && wallTop, "The foul poles rise at 330 ft and the wall is 8 ft high at 400 ft");
	}

	void CheckWallCalls()
	{
		Simulation::BattedBallFlight flight{44.0, 28.0, 0.0, 0.0, 5.0, 30.0, true, 3.0};
		auto outcome = Simulation::JudgeBattedBall(flight);
		Tests::Check(outcome.result == Simulation::BattedBallResult::HomeRun && outcome.label == L"センターへホームラン",
		    "A ball that cleared the wall in the simulation is a home run");
		flight = {44.0, 18.0, -14.0, 0.0, 3.4, 14.0, true, 1.2};
		outcome = Simulation::JudgeBattedBall(flight);
		Tests::Check((outcome.result == Simulation::BattedBallResult::Double ||
		                 outcome.result == Simulation::BattedBallResult::Triple) &&
		        outcome.label.find(L"フェンス直撃") != std::wstring::npos,
		    "A drive the simulation saw hit the wall in the gap is an extra-base hit off the wall");
		flight = {38.0, 35.0, 0.0, 0.0, 6.0, 30.0, true, 2.0};
		outcome = Simulation::JudgeBattedBall(flight);
		Tests::Check(outcome.result == Simulation::BattedBallResult::Out && outcome.label == L"センターフライ",
		    "A high fly that comes down at the wall gives the center fielder time to catch it on the track");
	}
}

void RunStadiumTests()
{
	CheckLayout();
	CheckCollision();
	CheckMesh();
	CheckWallCalls();
}
