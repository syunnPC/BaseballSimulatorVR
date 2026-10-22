#include "BattedBallOutcome.hpp"

#include "../Common/StadiumGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace BaseballSimulatorVR::Simulation
{
	namespace
	{
		constexpr double Feet = .3048;
		constexpr double Degrees = std::numbers::pi / 180.0;

		struct Point final
		{
			double u{}, v{}; // toward first base side, toward center field (meters from home)
		};
		[[nodiscard]] Point FromPolar(const double distanceMeters, const double angleDegrees) noexcept
		{
			return {distanceMeters * std::sin(angleDegrees * Degrees), distanceMeters * std::cos(angleDegrees * Degrees)};
		}
		[[nodiscard]] double Distance(const Point& a, const Point& b) noexcept
		{
			return std::hypot(a.u - b.u, a.v - b.v);
		}

		struct Mover final
		{
			double reactionSeconds, accelerationMetersPerSecond2, topSpeedMetersPerSecond;
		};
		// Reaction, first-step acceleration and top speed. Outfielders run at Statcast's 27 ft/s
		// average sprint speed, which puts them about 45 ft from where they stood after 3 s of
		// hang time, 70 ft after 4 s and 100 ft after 5 s (with reach), close to the even-odds
		// line of Statcast's Catch Probability; infielders cover shorter, lateral distances.
		constexpr Mover Infielder{.20, 5.5, 7.6};
		constexpr Mover Outfielder{.55, 4.5, 8.2};
		constexpr Mover Battery{.75, 4.0, 6.5};

		[[nodiscard]] double TimeToCover(const double distance, const Mover& mover) noexcept
		{
			if (distance <= 0.0) return mover.reactionSeconds;
			const double accelerationTime = mover.topSpeedMetersPerSecond / mover.accelerationMetersPerSecond2;
			const double accelerationDistance = .5 * mover.topSpeedMetersPerSecond * accelerationTime;
			const double running = distance <= accelerationDistance
			    ? std::sqrt(2.0 * distance / mover.accelerationMetersPerSecond2)
			    : accelerationTime + (distance - accelerationDistance) / mover.topSpeedMetersPerSecond;
			return mover.reactionSeconds + running;
		}

		struct Station final
		{
			Fielder fielder;
			Point start;
			Mover mover;
		};
		// Midpoints of MLB's Statcast standard positioning zones against left-handed batters
		// (Fielding Alignments glossary): 1B 85-130 ft at 31..42 degrees, 2B 130-160 ft at
		// 9..31, SS 130-160 ft at -18..0, 3B 80-130 ft at -37..-17, LF 260-320 ft at -33..-21,
		// CF 280-350 ft at -8..7, RF 260-320 ft at 21..33. Against right-handed batters the
		// glossary puts 1B at 23..38 and 2B at 0..18 (the mirrored shortstop zone); the
		// shortstop and third baseman mirror the left-handed 2B and 1B zones. The pitcher
		// finishes the delivery a little in front of the rubber; the catcher is behind home.
		[[nodiscard]] std::array<Station, 9> Defense(const bool rightHandedBatter) noexcept
		{
			const double mirror = rightHandedBatter ? -1.0 : 1.0;
			return {{
			    {Fielder::Pitcher, FromPolar(55.0 * Feet, 0.0), Battery},
			    {Fielder::Catcher, Point{0.0, -1.0}, Battery},
			    {rightHandedBatter ? Fielder::ThirdBase : Fielder::FirstBase, FromPolar(107.5 * Feet, 36.5 * mirror), Infielder},
			    {rightHandedBatter ? Fielder::Shortstop : Fielder::SecondBase, FromPolar(145.0 * Feet, 20.0 * mirror), Infielder},
			    {rightHandedBatter ? Fielder::SecondBase : Fielder::Shortstop, FromPolar(145.0 * Feet, -9.0 * mirror), Infielder},
			    rightHandedBatter ? Station{Fielder::FirstBase, FromPolar(107.5 * Feet, 30.5), Infielder}
			                      : Station{Fielder::ThirdBase, FromPolar(105.0 * Feet, -27.0), Infielder},
			    {Fielder::LeftField, FromPolar(290.0 * Feet, -27.0), Outfielder},
			    {Fielder::CenterField, FromPolar(315.0 * Feet, 0.0), Outfielder},
			    {Fielder::RightField, FromPolar(290.0 * Feet, 27.0), Outfielder},
			}};
		}

		// Who calls the others off a fly ball: outfielders coming in over infielders going
		// back, and the infielders over the catcher and the pitcher.
		[[nodiscard]] int CatchPriority(const Fielder fielder) noexcept
		{
			switch (fielder)
			{
			case Fielder::LeftField:
			case Fielder::CenterField:
			case Fielder::RightField: return 0;
			case Fielder::FirstBase:
			case Fielder::SecondBase:
			case Fielder::ThirdBase:
			case Fielder::Shortstop: return 1;
			case Fielder::Catcher: return 2;
			case Fielder::Pitcher:
			case Fielder::None: break;
			}
			return 3;
		}

		[[nodiscard]] bool IsInfielder(const Fielder fielder) noexcept
		{
			return fielder != Fielder::LeftField && fielder != Fielder::CenterField && fielder != Fielder::RightField;
		}

		// Field regions by spray angle, as a scorer names where the ball went.
		enum class Region { LeftLine, Left, LeftCenter, Center, RightCenter, Right, RightLine };
		[[nodiscard]] Region RegionOf(const double spray) noexcept
		{
			if (spray < -33.0) return Region::LeftLine;
			if (spray < -20.0) return Region::Left;
			if (spray < -7.0) return Region::LeftCenter;
			if (spray <= 7.0) return Region::Center;
			if (spray <= 20.0) return Region::RightCenter;
			if (spray <= 33.0) return Region::Right;
			return Region::RightLine;
		}
		[[nodiscard]] std::wstring_view RegionName(const Region region) noexcept
		{
			switch (region)
			{
			case Region::LeftLine: return L"レフト線";
			case Region::Left: return L"レフト";
			case Region::LeftCenter: return L"左中間";
			case Region::Center: return L"センター";
			case Region::RightCenter: return L"右中間";
			case Region::Right: return L"ライト";
			case Region::RightLine: break;
			}
			return L"ライト線";
		}
		[[nodiscard]] std::wstring_view FieldSide(const double spray) noexcept
		{
			return spray < -15.0 ? L"レフト" : (spray > 15.0 ? L"ライト" : L"センター");
		}
		[[nodiscard]] std::wstring HomeRunLabel(const double spray)
		{
			switch (RegionOf(spray))
			{
			case Region::LeftLine:
			case Region::Left: return L"レフトへホームラン";
			case Region::LeftCenter: return L"左中間へホームラン";
			case Region::Center: return L"センターへホームラン";
			case Region::RightCenter: return L"右中間へホームラン";
			case Region::Right:
			case Region::RightLine: break;
			}
			return L"ライトへホームラン";
		}

		constexpr double Gravity = 9.80665;
		constexpr double ContactHeightMeters = 1.0;
		// Fielders catch within reach up to a jump's height.
		constexpr double CatchableHeightMeters = 2.6;
		constexpr double AirReachMeters = 1.2;
		constexpr double GroundReachMeters = 1.5;
		// Ground balls fly to their first bounce, where they lose horizontal speed in
		// proportion to how hard they were driven down, then skip and roll, slowing on the
		// grass both in proportion to their speed and by rolling friction. The first hop
		// rebounds less the harder it lands; a topped chopper can hop over a fielder's glove.
		constexpr double BounceSpeedLoss = .75;
		constexpr double GroundSpeedDecayPerSecond = .35;
		constexpr double GroundFrictionMetersPerSecond2 = 2.0;
		[[nodiscard]] double BounceRestitution(const double impactSpeed) noexcept
		{
			return std::clamp(.6 - .012 * impactSpeed, .3, .6);
		}
		// Fielding to first base: about a second from glove to release, an 80 mph throw (about
		// 33 m/s on average along its arc), and the batter's run, about 4.4 s for a right-handed
		// and 4.3 s for a left-handed batter (90 ft at Statcast's 27 ft/s average sprint speed,
		// including the swing's finish and acceleration).
		constexpr double TransferSeconds = 1.0;
		constexpr double ThrowSpeedMetersPerSecond = 33.0;
		constexpr double FirstBaseDistanceMeters = 90.0 * Feet;
		constexpr double SecondBaseDistanceMeters = 127.28 * Feet;
		[[nodiscard]] double HomeToFirstSeconds(const BattedBallFlight& flight) noexcept
		{
			return flight.rightHandedBatter ? 4.4 : 4.3;
		}
		// Each further base takes an average runner about 3.5 s rounding the bag (about 7.9 s
		// home to second, 11.4 s to third), and only tries for it when the throw will
		// clearly be late. Outfielders gather a rolling ball and throw on a longer, slower arc.
		constexpr double ExtraBaseSeconds = 3.5;
		constexpr double ExtraBaseMarginSeconds = .3;
		constexpr double OutfieldTransferSeconds = 1.0;
		constexpr double OutfieldThrowSpeedMetersPerSecond = 30.0;
		// Air balls that drop in arrive at about three quarters of their average horizontal
		// speed and skip on, the less the steeper they come down.
		constexpr double DropInSpeedFraction = .75;
		constexpr double DropInSkipRetention = .6;
		// Past this the outfield, not an infielder, fields a ground ball.
		constexpr double InfieldLimitMeters = 50.0;
		constexpr double FenceHeightMeters = Common::Stadium::FenceHeightMeters;
		// Balls stop against the wall just in front of it.
		constexpr double WallClearanceMeters = .5;

		// Where a batted ball is along its spray line at a given time, and how high it is:
		// before its first bounce (ground balls), on its first hop and rolling.
		class BallPath final
		{
		public:
			[[nodiscard]] static BallPath Grounder(const BattedBallFlight& flight) noexcept
			{
				const double launch = flight.launchAngleDegrees * Degrees;
				const double horizontal = flight.exitSpeedMetersPerSecond * std::cos(launch);
				const double vertical = flight.exitSpeedMetersPerSecond * std::sin(launch);
				const double impact = std::sqrt(vertical * vertical + 2.0 * Gravity * ContactHeightMeters);
				BallPath path;
				path.m_firstDistance = (std::max)(flight.landingDistanceMeters, 0.0);
				path.m_firstTime = (std::max)(flight.hangTimeSeconds, 1e-3);
				path.m_apex = (std::max)(flight.apexHeightMeters, ContactHeightMeters);
				path.m_speed = (std::max)(.3 * horizontal, horizontal - BounceSpeedLoss * impact);
				path.m_hopSpeed = BounceRestitution(impact) * impact;
				path.m_wall = FenceDistanceMeters(flight.sprayAngleDegrees) - WallClearanceMeters;
				return path;
			}

			// An air ball that came down at `distance` after `seconds` and skips on at `speed`.
			[[nodiscard]] static BallPath DropIn(const double distance, const double seconds, const double speed,
			    const double fence) noexcept
			{
				BallPath path;
				path.m_firstDistance = distance;
				path.m_firstTime = (std::max)(seconds, 1e-3);
				path.m_speed = speed;
				path.m_wall = (std::max)(distance, fence - WallClearanceMeters);
				return path;
			}

			[[nodiscard]] double FirstBounceSeconds() const noexcept { return m_firstTime; }
			[[nodiscard]] double StopSeconds() const noexcept
			{
				constexpr double Ratio = GroundFrictionMetersPerSecond2 / GroundSpeedDecayPerSecond;
				return m_firstTime + std::log((m_speed + Ratio) / Ratio) / GroundSpeedDecayPerSecond;
			}

			[[nodiscard]] double DistanceAt(const double t) const noexcept
			{
				if (t < m_firstTime) return m_firstDistance * t / m_firstTime;
				constexpr double Ratio = GroundFrictionMetersPerSecond2 / GroundSpeedDecayPerSecond;
				const double rolling = (std::min)(t, StopSeconds()) - m_firstTime;
				const double rolled = (m_speed + Ratio) * (1.0 - std::exp(-GroundSpeedDecayPerSecond * rolling)) /
				    GroundSpeedDecayPerSecond - Ratio * rolling;
				return (std::min)(m_firstDistance + rolled, m_wall);
			}

			[[nodiscard]] double HeightAt(const double t) const noexcept
			{
				if (t < m_firstTime)
				{
					const double f = t / m_firstTime;
					return ContactHeightMeters * (1.0 - f) + 4.0 * (m_apex - .5 * ContactHeightMeters) * f * (1.0 - f);
				}
				const double hop = t - m_firstTime;
				return (std::max)(0.0, m_hopSpeed * hop - .5 * Gravity * hop * hop);
			}

		private:
			double m_firstDistance{}, m_firstTime{ 1e-3 }, m_apex{ ContactHeightMeters }, m_speed{}, m_hopSpeed{};
			double m_wall{};
		};

		enum class Fielders { Infield, Outfield, All };
		struct Retrieval final
		{
			Fielder fielder{ Fielder::None };
			double seconds{ std::numeric_limits<double>::infinity() };
			double distance{};
		};
		// The first of `fielders` to glove the ball from `from` seconds on, before it passes
		// `limit` meters. A ball that has stopped waits where it lies.
		[[nodiscard]] Retrieval Retrieve(const BattedBallFlight& flight, const BallPath& path, const Fielders fielders,
		    const double from, const double limit = std::numeric_limits<double>::infinity())
		{
			constexpr double Step = .01;
			Retrieval best;
			const double stop = path.StopSeconds();
			for (const Station& station : Defense(flight.rightHandedBatter))
			{
				if (fielders != Fielders::All && IsInfielder(station.fielder) != (fielders == Fielders::Infield)) continue;
				for (double t = from; t < best.seconds; t += Step)
				{
					const double along = path.DistanceAt(t);
					if (along > limit) break;
					const bool stopped = t >= stop;
					if (!stopped && path.HeightAt(t) > CatchableHeightMeters) continue;
					const double fielderSeconds = TimeToCover((std::max)(0.0,
					    Distance(FromPolar(along, flight.sprayAngleDegrees), station.start) - GroundReachMeters), station.mover);
					if (fielderSeconds <= t || stopped)
					{
						const double arrival = (std::max)(fielderSeconds, t);
						if (arrival < best.seconds) best = {station.fielder, arrival, along};
						break;
					}
				}
			}
			return best;
		}

		// An infielder who gloves the ball throws to first base (a first baseman near the bag
		// carries it there unassisted).
		[[nodiscard]] BattedBallOutcome PlayAtFirst(const BattedBallFlight& flight, const Retrieval& play,
		    BattedBallOutcome outcome)
		{
			const double toBag = Distance(FromPolar(play.distance, flight.sprayAngleDegrees),
			    FromPolar(FirstBaseDistanceMeters, 45.0));
			const double seconds = play.fielder == Fielder::FirstBase && toBag < 4.0
			    ? play.seconds + toBag / Infielder.topSpeedMetersPerSecond
			    : play.seconds + TransferSeconds + toBag / ThrowSpeedMetersPerSecond;
			outcome.fielder = play.fielder;
			outcome.result = seconds < HomeToFirstSeconds(flight) ? BattedBallResult::Out : BattedBallResult::InfieldHit;
			outcome.label = std::wstring{FielderName(play.fielder)} +
			    (outcome.result == BattedBallResult::Out ? L"ゴロ" : L"内野安打");
			return outcome;
		}

		// A ball an outfielder picks up is a single, double or triple depending on whether the
		// throw would beat the batter to second or third. `landing` is where it came down (or
		// reached the outfield, for ground balls); past the fielder's starting depth it went
		// over the fielder.
		[[nodiscard]] BattedBallOutcome OutfieldHit(const BattedBallFlight& flight, const Retrieval& play,
		    const double landing, const bool offWall, BattedBallOutcome outcome)
		{
			const double spray = flight.sprayAngleDegrees;
			const Point at = FromPolar(play.distance, spray);
			const auto throwTo = [&](const Point& base)
			{
				return play.seconds + OutfieldTransferSeconds + Distance(at, base) / OutfieldThrowSpeedMetersPerSecond;
			};
			const double toFirst = HomeToFirstSeconds(flight);
			outcome.fielder = play.fielder;
			outcome.result = BattedBallResult::Single;
			if (throwTo(FromPolar(SecondBaseDistanceMeters, 0.0)) > toFirst + ExtraBaseSeconds + ExtraBaseMarginSeconds)
			{
				outcome.result = throwTo(FromPolar(FirstBaseDistanceMeters, -45.0)) >
				    toFirst + 2.0 * ExtraBaseSeconds + ExtraBaseMarginSeconds ? BattedBallResult::Triple : BattedBallResult::Double;
			}
			double depth = 0.0;
			for (const Station& station : Defense(flight.rightHandedBatter))
			{
				if (station.fielder == play.fielder) depth = std::hypot(station.start.u, station.start.v);
			}
			const bool overFielder = landing > depth;
			const Region region = RegionOf(spray);
			if (outcome.result == BattedBallResult::Single)
			{
				// "センター前ヒット" in front of the fielder, "左中間へのヒット" past.
				outcome.label = offWall       ? std::wstring{FieldSide(spray)} + L"フェンス直撃のヒット"
				              : overFielder ? std::wstring{RegionName(region)} + L"へのヒット"
				                            : std::wstring{FielderName(play.fielder)} + L"前ヒット";
				return outcome;
			}
			const std::wstring_view hit = outcome.result == BattedBallResult::Double ? L"二塁打" : L"三塁打";
			const bool gapOrLine = region != Region::Left && region != Region::Center && region != Region::Right;
			if (offWall) outcome.label = std::wstring{FieldSide(spray)} + L"フェンス直撃の" + std::wstring{hit};
			else if (gapOrLine) outcome.label = std::wstring{RegionName(region)} + std::wstring{hit};
			else outcome.label = std::wstring{RegionName(region)} + (overFielder ? L"オーバーの" : L"への") + std::wstring{hit};
			return outcome;
		}

		[[nodiscard]] BattedBallOutcome JudgeGroundBall(const BattedBallFlight& flight, BattedBallOutcome outcome)
		{
			const BallPath path = BallPath::Grounder(flight);
			Retrieval play = Retrieve(flight, path, Fielders::Infield, 0.0, InfieldLimitMeters);
			// Through the infield: an outfielder runs it down.
			if (play.fielder == Fielder::None) play = Retrieve(flight, path, Fielders::Outfield, 0.0);
			if (play.seconds < path.FirstBounceSeconds())
			{
				// Snared before it bounced: a line drive out with nobody running.
				outcome.fielder = play.fielder;
				outcome.result = BattedBallResult::Out;
				outcome.label = std::wstring{FielderName(play.fielder)} + L"ライナー";
				return outcome;
			}
			if (IsInfield(play.fielder)) return PlayAtFirst(flight, play, outcome);
			return OutfieldHit(flight, play, (std::max)(InfieldLimitMeters, flight.landingDistanceMeters), false, outcome);
		}

		[[nodiscard]] BattedBallOutcome JudgeAirBall(const BattedBallFlight& flight, BattedBallOutcome outcome)
		{
			const double fence = FenceDistanceMeters(flight.sprayAngleDegrees);
			// A ball the simulation saw reach the wall ends its flight there, at that height.
			const bool atWall = flight.wallHeightMeters.has_value() && std::isfinite(*flight.wallHeightMeters);
			const double distance = atWall ? fence : (std::max)(flight.landingDistanceMeters, .1);
			const double hang = (std::max)(flight.hangTimeSeconds, .05);
			const double end = atWall ? (std::max)(*flight.wallHeightMeters, 0.0) : 0.0;
			const double apex = (std::max)({flight.apexHeightMeters, ContactHeightMeters, end});
			const auto heightAt = [&](const double f)
			{
				return ContactHeightMeters * (1.0 - f) + end * f +
				    4.0 * (apex - .5 * (ContactHeightMeters + end)) * f * (1.0 - f);
			};
			// Each fielder's earliest point on the flight within reach of a catchable ball. On
			// its way out whoever gets there first takes it; coming down, the fielder with
			// priority calls the others off, the one playing nearest to where it comes down
			// among equals.
			struct Catch final
			{
				Fielder fielder{ Fielder::None };
				double fraction{ 1.0 };
				double toLanding{};
			};
			Catch taken;
			const Point landingPoint = FromPolar(distance, flight.sprayAngleDegrees);
			const auto preferred = [](const Catch& a, const Catch& b)
			{
				if ((a.fraction < .5 || b.fraction < .5) && a.fraction != b.fraction) return a.fraction < b.fraction;
				const int pa = CatchPriority(a.fielder), pb = CatchPriority(b.fielder);
				return pa != pb ? pa < pb : a.toLanding < b.toLanding;
			};
			constexpr int Samples = 400;
			for (const Station& station : Defense(flight.rightHandedBatter))
			{
				for (int sample = 1; sample <= Samples; ++sample)
				{
					const double f = static_cast<double>(sample) / Samples;
					if (heightAt(f) > CatchableHeightMeters) continue;
					// Fielders reach up to the wall, not beyond it.
					if (distance * f > fence) break;
					const Point at = FromPolar(distance * f, flight.sprayAngleDegrees);
					const double needed = (std::max)(0.0, Distance(at, station.start) - AirReachMeters);
					if (TimeToCover(needed, station.mover) > hang * f) continue;
					const Catch candidate{station.fielder, f, Distance(landingPoint, station.start)};
					if (taken.fielder == Fielder::None || preferred(candidate, taken)) taken = candidate;
					break;
				}
			}
			const bool foul = std::abs(flight.sprayAngleDegrees) > 45.0;
			if (taken.fielder != Fielder::None)
			{
				outcome.fielder = taken.fielder;
				outcome.result = BattedBallResult::Out;
				outcome.label = std::wstring{FielderName(taken.fielder)} +
				    (foul ? L"ファウルフライ" : outcome.type == BattedBallType::LineDrive ? L"ライナー" : L"フライ");
				return outcome;
			}
			if (foul)
			{
				outcome.result = BattedBallResult::Foul;
				outcome.label = L"ファウル";
				return outcome;
			}
			const bool offWall = atWall || distance >= fence;
			if (atWall ? end >= FenceHeightMeters : offWall && heightAt(fence / distance) >= FenceHeightMeters)
			{
				outcome.result = BattedBallResult::HomeRun;
				outcome.label = HomeRunLabel(flight.sprayAngleDegrees);
				return outcome;
			}
			// It dropped in (or came off the wall) and skips on toward the wall until someone
			// picks it up.
			const double horizontal = DropInSpeedFraction * distance / hang;
			const double descent = std::atan2(std::sqrt(2.0 * Gravity * apex), horizontal);
			const double skip = offWall ? 0.0 : DropInSkipRetention * horizontal * (1.0 - descent / (.5 * std::numbers::pi));
			const double landing = offWall ? fence - WallClearanceMeters : distance;
			const BallPath path = BallPath::DropIn(landing, offWall ? hang * fence / distance : hang, skip, fence);
			const Retrieval play = Retrieve(flight, path, Fielders::All, path.FirstBounceSeconds());
			if (IsInfield(play.fielder)) return PlayAtFirst(flight, play, outcome);
			return OutfieldHit(flight, play, landing, offWall, outcome);
		}

		[[nodiscard]] bool IsFinite(const BattedBallFlight& flight) noexcept
		{
			return std::isfinite(flight.exitSpeedMetersPerSecond) && std::isfinite(flight.launchAngleDegrees) &&
			    std::isfinite(flight.sprayAngleDegrees) && std::isfinite(flight.landingDistanceMeters) &&
			    std::isfinite(flight.hangTimeSeconds) && std::isfinite(flight.apexHeightMeters) &&
			    (!flight.wallHeightMeters || std::isfinite(*flight.wallHeightMeters));
		}
	}

	BattedBallType ClassifyBattedBallType(const double launchAngleDegrees) noexcept
	{
		if (launchAngleDegrees < 10.0) return BattedBallType::GroundBall;
		if (launchAngleDegrees < 25.0) return BattedBallType::LineDrive;
		if (launchAngleDegrees <= 50.0) return BattedBallType::FlyBall;
		return BattedBallType::PopUp;
	}

	bool IsInfield(const Fielder fielder) noexcept
	{
		return fielder != Fielder::None && IsInfielder(fielder);
	}

	std::wstring_view FielderName(const Fielder fielder) noexcept
	{
		switch (fielder)
		{
		case Fielder::Pitcher: return L"ピッチャー";
		case Fielder::Catcher: return L"キャッチャー";
		case Fielder::FirstBase: return L"ファースト";
		case Fielder::SecondBase: return L"セカンド";
		case Fielder::ThirdBase: return L"サード";
		case Fielder::Shortstop: return L"ショート";
		case Fielder::LeftField: return L"レフト";
		case Fielder::CenterField: return L"センター";
		case Fielder::RightField: return L"ライト";
		case Fielder::None: break;
		}
		return {};
	}

	double FenceDistanceMeters(const double sprayAngleDegrees) noexcept
	{
		return Common::Stadium::FenceDistanceMeters(sprayAngleDegrees);
	}

	BattedBallOutcome JudgeBattedBall(const BattedBallFlight& flight)
	{
		BattedBallOutcome outcome{};
		if (!IsFinite(flight) || flight.exitSpeedMetersPerSecond <= 0.0) return outcome;
		outcome.type = ClassifyBattedBallType(flight.launchAngleDegrees);
		if (std::abs(flight.sprayAngleDegrees) > 45.0 && outcome.type == BattedBallType::GroundBall)
		{
			outcome.result = BattedBallResult::Foul;
			outcome.label = L"ファウル";
			return outcome;
		}
		return outcome.type == BattedBallType::GroundBall ? JudgeGroundBall(flight, outcome) : JudgeAirBall(flight, outcome);
	}
}
