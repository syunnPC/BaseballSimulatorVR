#include "../BaseballSimulatorVR/Physics/PitchPhysicsEngine.hpp"
#include "../BaseballSimulatorVR/Simulation/BattedBallOutcome.hpp"
#include "TestSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

namespace
{
	using namespace BaseballSimulatorVR;
	using Simulation::BattedBallResult;
	using Simulation::Fielder;

	constexpr double Feet = .3048;

	struct Carry final
	{
		double distance{}, hangTime{}, apex{};
	};

	// Straightaway flight with the production physical aerodynamics and the backspin typical
	// for the launch angle; spray only rotates it, so it stays out of the integration.
	Carry Fly(const double exitSpeed, const double launchAngleDegrees)
	{
		Physics::PitchPhysicsEngine engine;
		Baseball::PitchParameters parameters;
		parameters.integratorType = Physics::PitchIntegratorType::Rk4;
		Physics::BallState state;
		state.aerodynamicsModel = Physics::BallAerodynamicsModel::Physical;
		const double angle = launchAngleDegrees * std::numbers::pi / 180.0;
		state.position = {0.0, 1.0, 0.0};
		state.velocity = {0.0, exitSpeed * std::sin(angle), -exitSpeed * std::cos(angle)};
		state.spinRpm = std::clamp(1000.0 + 50.0 * launchAngleDegrees, 500.0, 3000.0);
		state.spinAxis = {1.0, 0.0, 0.0};
		constexpr double Step = .005;
		Carry carry{0.0, 0.0, 1.0};
		Physics::BallState previous = state;
		for (double t = 0.0; t < 15.0 && state.position.y > 0.0; t += Step)
		{
			previous = state;
			state = engine.Step(state, parameters, std::chrono::duration<double>(Step));
			carry.hangTime = t + Step;
			carry.apex = (std::max)(carry.apex, state.position.y);
		}
		const double f = previous.position.y / (previous.position.y - state.position.y);
		carry.distance = -(previous.position.z + (state.position.z - previous.position.z) * f);
		carry.hangTime -= Step * (1.0 - f);
		return carry;
	}

	Simulation::BattedBallFlight Flight(const double exitSpeed, const double launchAngle, const double spray,
	    const bool rightHanded = true)
	{
		const Carry carry = Fly(exitSpeed, launchAngle);
		return {exitSpeed, launchAngle, spray, carry.distance, carry.hangTime, carry.apex, rightHanded};
	}

	Simulation::BattedBallOutcome Judge(const double exitSpeed, const double launchAngle, const double spray,
	    const bool rightHanded = true)
	{
		return Simulation::JudgeBattedBall(Flight(exitSpeed, launchAngle, spray, rightHanded));
	}

	bool EndsWith(const std::wstring& text, const std::wstring_view suffix)
	{
		return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	void CheckClassificationAndPark()
	{
		using Simulation::BattedBallType;
		using Simulation::ClassifyBattedBallType;
		Tests::Check(ClassifyBattedBallType(9.99) == BattedBallType::GroundBall &&
		        ClassifyBattedBallType(10.0) == BattedBallType::LineDrive &&
		        ClassifyBattedBallType(24.99) == BattedBallType::LineDrive &&
		        ClassifyBattedBallType(25.0) == BattedBallType::FlyBall &&
		        ClassifyBattedBallType(50.0) == BattedBallType::FlyBall &&
		        ClassifyBattedBallType(50.01) == BattedBallType::PopUp,
		    "Batted-ball types follow the Statcast launch-angle bands");

		Tests::Check(Tests::Near(Simulation::FenceDistanceMeters(0.0), 400.0 * Feet, 1e-9) &&
		        Tests::Near(Simulation::FenceDistanceMeters(45.0), 325.0 * Feet, 1e-9) &&
		        Tests::Near(Simulation::FenceDistanceMeters(-45.0), 325.0 * Feet, 1e-9) &&
		        Tests::Near(Simulation::FenceDistanceMeters(60.0), 325.0 * Feet, 1e-9),
		    "The wall is 400 ft to center and 325 ft down the lines");
		for (double spray = 0.0; spray < 45.0; spray += 1.0)
		{
			Tests::Check(Simulation::FenceDistanceMeters(spray + 1.0) < Simulation::FenceDistanceMeters(spray) &&
			        Simulation::FenceDistanceMeters(-spray) == Simulation::FenceDistanceMeters(spray),
			    "The wall comes in symmetrically toward the foul lines");
		}
	}

	void CheckRepresentativeCalls()
	{
		auto outcome = Judge(38.0, -5.0, -20.0);
		Tests::Check(outcome.result == BattedBallResult::Out && outcome.fielder == Fielder::Shortstop &&
		        outcome.label == L"ショートゴロ",
		    "An average grounder at the shortstop is a shortstop groundout");
		outcome = Judge(38.0, -5.0, 20.0, false);
		Tests::Check(outcome.result == BattedBallResult::Out && outcome.fielder == Fielder::SecondBase &&
		        outcome.label == L"セカンドゴロ",
		    "The defense mirrors for a left-handed batter's pulled grounder");
		outcome = Judge(42.0, -2.0, 0.0);
		Tests::Check(outcome.result == BattedBallResult::Single && outcome.fielder == Fielder::CenterField &&
		        outcome.label == L"センター前ヒット",
		    "A hard grounder up the middle gets through for a single");
		outcome = Judge(45.0, -3.0, -30.0);
		Tests::Check(outcome.result == BattedBallResult::Single && outcome.label == L"レフト前ヒット",
		    "A hard grounder through the left side is a single to left");
		outcome = Judge(18.0, -20.0, -38.0);
		Tests::Check(outcome.result == BattedBallResult::InfieldHit && outcome.label == L"サード内野安打",
		    "A slow chopper down the third-base line beats the throw");

		outcome = Judge(40.0, 35.0, 0.0);
		Tests::Check(outcome.result == BattedBallResult::Out && outcome.fielder == Fielder::CenterField &&
		        outcome.label == L"センターフライ",
		    "A routine fly to center is caught");
		outcome = Judge(38.0, 12.0, 3.0);
		Tests::Check(outcome.result == BattedBallResult::Single && outcome.label == L"センター前ヒット",
		    "A liner that drops in front of the center fielder is a single");
		outcome = Judge(45.0, 20.0, -15.0);
		Tests::Check(outcome.result == BattedBallResult::Double && outcome.label == L"左中間二塁打",
		    "A drive into the left-center gap is a double");
		outcome = Judge(48.0, 28.0, 0.0);
		Tests::Check(outcome.result == BattedBallResult::HomeRun && outcome.fielder == Fielder::None &&
		        outcome.label == L"センターへホームラン",
		    "A barreled fly to center clears the 400 ft wall");
		outcome = Judge(30.0, 62.0, 0.0);
		Tests::Check(outcome.result == BattedBallResult::Out && outcome.fielder != Fielder::Pitcher &&
		        outcome.fielder != Fielder::Catcher && Simulation::IsInfield(outcome.fielder) &&
		        EndsWith(outcome.label, L"フライ"),
		    "An infield pop-up is taken by an infielder, not the pitcher");
		outcome = Judge(35.0, 30.0, 50.0);
		Tests::Check(outcome.result == BattedBallResult::Foul && outcome.label == L"ファウル",
		    "Balls outside the foul lines are fouls");
		outcome = Judge(25.0, 75.0, -60.0);
		Tests::Check(outcome.result == BattedBallResult::Out && outcome.label == L"サードファウルフライ",
		    "A pop-up just outside the third-base line is a foul out to the third baseman");
		outcome = Judge(35.0, -5.0, -48.0);
		Tests::Check(outcome.result == BattedBallResult::Foul, "Grounders outside the lines are fouls");

		const Carry deep = Fly(40.0, 30.0);
		Simulation::BattedBallFlight flight{40.0, 30.0, 0.0, deep.distance, deep.hangTime, deep.apex, true};
		Tests::Check(deep.distance < Simulation::FenceDistanceMeters(0.0) &&
		        Simulation::JudgeBattedBall(flight).result == BattedBallResult::Out,
		    "A fly short of the center-field wall stays in the park");
		flight.landingDistanceMeters = Simulation::FenceDistanceMeters(0.0) + 15.0;
		flight.apexHeightMeters = 30.0;
		Tests::Check(Simulation::JudgeBattedBall(flight).result == BattedBallResult::HomeRun,
		    "A fly that comes down well beyond the wall is a home run");
		// A screaming liner into the gap that reaches the wall below its top.
		const Simulation::BattedBallFlight liner{50.0, 15.0, -14.0, Simulation::FenceDistanceMeters(-14.0) + .2, 2.6,
		    3.0, true};
		const auto offWall = Simulation::JudgeBattedBall(liner);
		Tests::Check(offWall.result != BattedBallResult::HomeRun && offWall.result != BattedBallResult::Out &&
		        offWall.label.find(L"フェンス直撃") != std::wstring::npos,
		    "A low drive that reaches the wall below its top is played off the wall");
	}

	void CheckEveryFlightGetsAConsistentCall()
	{
		for (double exitSpeed = 5.0; exitSpeed <= 55.0; exitSpeed += 10.0)
		{
			for (double launch = -40.0; launch <= 80.0; launch += 15.0)
			{
				const Carry carry = Fly(exitSpeed, launch);
				for (double spray = -50.0; spray <= 50.0; spray += 10.0)
				{
					for (const bool rightHanded : {true, false})
					{
						const auto outcome = Simulation::JudgeBattedBall(
						    {exitSpeed, launch, spray, carry.distance, carry.hangTime, carry.apex, rightHanded});
						Tests::Check(!outcome.label.empty(), "Every batted ball gets a call");
						Tests::Check((outcome.result == BattedBallResult::Foul) <= (std::abs(spray) > 45.0) &&
						        (std::abs(spray) <= 45.0 || outcome.result == BattedBallResult::Foul ||
						            (outcome.result == BattedBallResult::Out && EndsWith(outcome.label, L"ファウルフライ"))),
						    "Balls outside the lines are fouls unless caught on the fly");
						const bool infielder = outcome.fielder != Fielder::None && Simulation::IsInfield(outcome.fielder);
						Tests::Check(outcome.result != BattedBallResult::InfieldHit || infielder,
						    "Infield hits are credited to the infielder who fielded them");
						Tests::Check((outcome.result != BattedBallResult::Single &&
						                 outcome.result != BattedBallResult::Double &&
						                 outcome.result != BattedBallResult::Triple) ||
						        (outcome.fielder != Fielder::None && !infielder),
						    "Clean hits are run down by an outfielder");
						Tests::Check(outcome.result != BattedBallResult::Out || outcome.fielder != Fielder::None,
						    "Every out names its fielder");
						Tests::Check((outcome.result == BattedBallResult::HomeRun ||
						                 outcome.result == BattedBallResult::Foul) == (outcome.fielder == Fielder::None),
						    "Only home runs and fouls are untouched");
					}
				}
			}
		}

		const double nan = std::numeric_limits<double>::quiet_NaN();
		Tests::Check(Simulation::JudgeBattedBall({nan, 10.0, 0.0, 50.0, 2.0, 5.0, true}).label.empty() &&
		        Simulation::JudgeBattedBall({40.0, 10.0, 0.0, std::numeric_limits<double>::infinity(), 2.0, 5.0, true})
		            .label.empty() &&
		        Simulation::JudgeBattedBall({0.0, 10.0, 0.0, 0.0, 0.0, 1.0, true}).label.empty(),
		    "Unusable flights are left unjudged");
	}

	// Averages over typical major-league contact (Statcast-like exit velocity, launch angle and
	// pull tendencies for a right-handed batter) land near the MLB batting averages by type:
	// roughly .24 on grounders, .65 on liners, .22 on fly balls and almost nothing on pop-ups.
	void CheckLeagueLikeAverages()
	{
		struct Contact final
		{
			double exitSpeedMean, exitSpeedDeviation, launchLow, launchHigh, sprayMean;
			double averageLow, averageHigh;
		};
		constexpr std::array<Contact, 4> contacts{{
		    {37.5, 6.5, -30.0, 10.0, -8.0, .15, .30},
		    {42.0, 5.0, 10.0, 25.0, 0.0, .42, .70},
		    {41.0, 5.5, 25.0, 50.0, 3.0, .14, .30},
		    {36.0, 6.0, 50.0, 70.0, 0.0, 0.0, .06},
		}};
		const auto gaussian = [](const double x, const double mean, const double deviation)
		{
			return std::exp(-.5 * (x - mean) * (x - mean) / (deviation * deviation));
		};
		for (std::size_t type = 0; type < contacts.size(); ++type)
		{
			const Contact& contact = contacts[type];
			double total = 0.0, hits = 0.0, infieldHits = 0.0, homeRuns = 0.0, pitcher = 0.0;
			for (double exitSpeed = contact.exitSpeedMean - 2.5 * contact.exitSpeedDeviation;
			    exitSpeed <= contact.exitSpeedMean + 2.5 * contact.exitSpeedDeviation + 1e-9;
			    exitSpeed += contact.exitSpeedDeviation / 4.0)
			{
				for (double launch = contact.launchLow + 1.25; launch < contact.launchHigh; launch += 2.5)
				{
					const Carry carry = Fly(exitSpeed, launch);
					for (double spray = -44.0; spray <= 44.0; spray += 4.0)
					{
						const double weight = gaussian(exitSpeed, contact.exitSpeedMean, contact.exitSpeedDeviation) *
						    gaussian(spray, contact.sprayMean, 20.0);
						const auto outcome = Simulation::JudgeBattedBall(
						    {exitSpeed, launch, spray, carry.distance, carry.hangTime, carry.apex, true});
						total += weight;
						if (outcome.result != BattedBallResult::Out) hits += weight;
						if (outcome.result == BattedBallResult::InfieldHit) infieldHits += weight;
						if (outcome.result == BattedBallResult::HomeRun) homeRuns += weight;
						if (outcome.fielder == Fielder::Pitcher) pitcher += weight;
					}
				}
			}
			const double average = hits / total;
			Tests::Check(average >= contact.averageLow && average <= contact.averageHigh,
			    "Batting average by batted-ball type stays near major-league levels");
			if (type == 0)
			{
				Tests::Check(infieldHits > 0.0 && infieldHits / total < .08,
				    "Some, but not many, grounders are infield hits");
				Tests::Check(pitcher / total < .12, "The pitcher fields only a small share of grounders");
			}
			if (type == 2)
			{
				Tests::Check(homeRuns / total > .08 && homeRuns / total < .28,
				    "A plausible share of fly balls leave the park");
			}
		}
	}
}

void RunBattedBallOutcomeTests()
{
	CheckClassificationAndPark();
	CheckRepresentativeCalls();
	CheckEveryFlightGetsAConsistentCall();
	CheckLeagueLikeAverages();
}
