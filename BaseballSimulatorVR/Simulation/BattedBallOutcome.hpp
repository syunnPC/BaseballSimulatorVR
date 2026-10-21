#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Simulation
{
	// Statcast batted-ball types by launch angle (MLB glossary): ground ball below 10 degrees,
	// line drive 10-25, fly ball 25-50, pop-up above 50.
	enum class BattedBallType { GroundBall, LineDrive, FlyBall, PopUp };
	enum class BattedBallResult { Out, InfieldHit, Single, Double, Triple, HomeRun, Foul };
	enum class Fielder
	{
		None, Pitcher, Catcher, FirstBase, SecondBase, ThirdBase, Shortstop, LeftField, CenterField, RightField
	};

	struct BattedBallFlight final
	{
		double exitSpeedMetersPerSecond{};
		double launchAngleDegrees{};
		// Statcast spray convention: 0 toward center field, -45 along the third-base line and
		// +45 along the first-base line.
		double sprayAngleDegrees{};
		// From home plate to the first ground contact, and the time spent in the air until then.
		double landingDistanceMeters{};
		double hangTimeSeconds{};
		double apexHeightMeters{};
		bool rightHandedBatter{ true };
		// Height of the ball above the ground where it reached the outfield wall, when the
		// simulation saw it get there before it came down; the landing distance and hang
		// time are then those at the wall.
		std::optional<double> wallHeightMeters{};
	};

	struct BattedBallOutcome final
	{
		BattedBallType type{ BattedBallType::GroundBall };
		BattedBallResult result{ BattedBallResult::Out };
		Fielder fielder{ Fielder::None };
		// Japanese scorebook-style call such as "センターフライ" or "右中間二塁打"; empty when the
		// flight could not be judged.
		std::wstring label;
	};

	[[nodiscard]] BattedBallType ClassifyBattedBallType(double launchAngleDegrees) noexcept;
	[[nodiscard]] std::wstring_view FielderName(Fielder fielder) noexcept;
	// The pitcher, the catcher and the four infielders.
	[[nodiscard]] bool IsInfield(Fielder fielder) noexcept;
	// The stadium's outfield wall (see Common/StadiumGeometry.hpp): 330 ft down the foul
	// lines, about 380 ft in the gaps and 400 ft to center field, 8 ft high.
	[[nodiscard]] double FenceDistanceMeters(double sprayAngleDegrees) noexcept;

	// Judges a batted ball against a standard defense. Fielders start at the middle of MLB's
	// Statcast standard positioning zones and react, accelerate and run to the ball; air balls
	// (foul flies included) are outs when a fielder reaches them at catchable height, ground
	// balls when a fielder gets to the ball and the throw beats the batter to first base. A
	// hit that reaches the outfield goes for as many bases as the outfielder's throw allows.
	[[nodiscard]] BattedBallOutcome JudgeBattedBall(const BattedBallFlight& flight);
}
