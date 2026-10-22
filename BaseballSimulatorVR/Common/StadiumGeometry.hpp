#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

namespace BaseballSimulatorVR::Common::Stadium
{
	// A classic symmetric major-league park: 330 ft down the lines, about 380 ft in the
	// gaps and 400 ft to center (meeting the Official Baseball Rules 2.01 minimums of 325 ft
	// and 400 ft) behind an 8 ft wall with a 15 ft warning track. Foul territory is 60 ft
	// deep behind home plate (rule 2.01's recommended backstop distance) and stays that wide
	// as far as the bases before closing in to meet the wall at the foul poles. Seats rise
	// behind every wall; netting above the backstop wall covers the seats behind the plate.
	//
	// Field coordinates are measured from the apex of home plate: u toward the first-base
	// side, v toward the pitcher. Bearings are in degrees from the line through second base,
	// positive toward first base, the same convention as Statcast's spray angle (the foul
	// lines are at +-45 and home plate's back is at 180).
	inline constexpr double Feet = .3048;
	inline constexpr double LineFenceDistanceMeters = 330.0 * Feet;
	inline constexpr double CenterFenceDistanceMeters = 400.0 * Feet;
	inline constexpr double FenceHeightMeters = 8.0 * Feet;
	inline constexpr double WarningTrackWidthMeters = 15.0 * Feet;
	inline constexpr double BackstopDistanceMeters = 60.0 * Feet;
	inline constexpr double FoulTerritoryFullWidthToMeters = 90.0 * Feet;
	inline constexpr double FoulWallHeightMeters = 4.0 * Feet;
	inline constexpr double BackstopWallHeightMeters = 8.0 * Feet;
	inline constexpr double BackstopNetTopMeters = 30.0 * Feet;
	// The netting runs from behind the plate out to this bearing on both sides.
	inline constexpr double NetEndBearingDegrees = 100.0;
	inline constexpr double FoulPoleHeightMeters = 90.0 * Feet;
	// The dark, seatless section straight out behind center field the batter picks the
	// ball up against.
	inline constexpr double BattersEyeHalfAngleDegrees = 8.0;
	// Seating rows: 33 in deep, 16.5 in rise (a common stadium riser).
	inline constexpr double RowDepthMeters = 33.0 / 12.0 * Feet;
	inline constexpr double RowRiseMeters = 16.5 / 12.0 * Feet;
	inline constexpr int OutfieldRows = 22;
	inline constexpr int InfieldRows = 28;

	enum class Section { Outfield, BattersEye, Foul, Backstop };

	[[nodiscard]] inline double Radians(const double degrees) noexcept
	{
		return degrees * std::numbers::pi / 180.0;
	}

	// The outfield wall: 330 ft at the poles, 400 ft to center, rounding through the gaps.
	[[nodiscard]] inline double FenceDistanceMeters(const double bearingDegrees) noexcept
	{
		const double fromCenter = (std::min)(std::abs(bearingDegrees), 45.0);
		return LineFenceDistanceMeters +
		    (CenterFenceDistanceMeters - LineFenceDistanceMeters) * std::cos(Radians(2.0 * fromCenter));
	}

	[[nodiscard]] inline Section SectionAt(const double bearingDegrees) noexcept
	{
		const double from = std::abs(bearingDegrees);
		if (from < BattersEyeHalfAngleDegrees) return Section::BattersEye;
		if (from <= 45.0) return Section::Outfield;
		return from >= NetEndBearingDegrees ? Section::Backstop : Section::Foul;
	}

	// The barrier at the edge of the playing field a ball has to clear to leave it: the
	// outfield wall, the low wall in front of the foul-side seats, or the backstop and the
	// netting above it.
	[[nodiscard]] inline double WallHeightMeters(const Section section) noexcept
	{
		switch (section)
		{
		case Section::Outfield:
		case Section::BattersEye: return FenceHeightMeters;
		case Section::Foul: return FoulWallHeightMeters;
		case Section::Backstop: break;
		}
		return BackstopNetTopMeters;
	}

	// Height of the first row of seats, right behind the wall.
	[[nodiscard]] inline double FirstRowHeightMeters(const Section section) noexcept
	{
		return section == Section::Outfield || section == Section::BattersEye ? FenceHeightMeters
		                                                                      : FoulWallHeightMeters;
	}

	[[nodiscard]] inline int RowCount(const Section section) noexcept
	{
		return section == Section::Outfield || section == Section::BattersEye ? OutfieldRows : InfieldRows;
	}

	[[nodiscard]] inline double StandsDepthMeters(const Section section) noexcept
	{
		return RowCount(section) * RowDepthMeters;
	}

	// Height of the seat tread a given distance behind the wall.
	[[nodiscard]] inline double StandsTreadHeightMeters(const Section section, const double behindWallMeters) noexcept
	{
		const int row = std::clamp(static_cast<int>(std::floor(behindWallMeters / RowDepthMeters)), 0,
		    RowCount(section) - 1);
		return FirstRowHeightMeters(section) + row * RowRiseMeters;
	}

	struct BoundaryPoint final
	{
		double u{}, v{};
		double bearingDegrees{};
		// Outward horizontal normal of the wall face.
		double normalU{}, normalV{};
		Section section{ Section::Outfield };
	};

	namespace Detail
	{
		// Foul-side wall as an offset of the first-base line: `along` meters from home,
		// `width` meters out into foul territory. The third-base side mirrors it.
		[[nodiscard]] inline double FoulTerritoryWidthMeters(const double along) noexcept
		{
			if (along <= FoulTerritoryFullWidthToMeters) return BackstopDistanceMeters;
			const double blend = (along - FoulTerritoryFullWidthToMeters) /
			    (LineFenceDistanceMeters - FoulTerritoryFullWidthToMeters);
			return BackstopDistanceMeters * (1.0 - std::clamp(blend, 0.0, 1.0));
		}

		[[nodiscard]] inline std::vector<BoundaryPoint> BuildBoundary()
		{
			std::vector<BoundaryPoint> points;
			const auto add = [&](const double u, const double v)
			{
				BoundaryPoint point;
				point.u = u;
				point.v = v;
				point.bearingDegrees = std::atan2(u, v) * 180.0 / std::numbers::pi;
				point.section = SectionAt(point.bearingDegrees);
				points.push_back(point);
			};
			const auto foulSide = [&](const double along, const double side)
			{
				const double c = 1.0 / std::numbers::sqrt2;
				const double width = FoulTerritoryWidthMeters(along);
				// Along the line (c, c) and out into foul territory (c, -c) on the first-base side.
				add(side * (along * c + width * c), along * c - width * c);
			};
			// In order of increasing bearing: behind the plate from -180, the third-base side
			// out to the left-field pole, around the outfield, and back in on the first-base side.
			constexpr double ArcStepDegrees = 2.0;
			for (double bearing = -180.0; bearing < -135.0 - 1e-9; bearing += ArcStepDegrees)
				add(BackstopDistanceMeters * std::sin(Radians(bearing)), BackstopDistanceMeters * std::cos(Radians(bearing)));
			// Both foul sides at the same distances along their lines, so they mirror exactly.
			const int foulSteps = static_cast<int>(std::ceil(LineFenceDistanceMeters)) - 1;
			for (int step = 0; step <= foulSteps; ++step) foulSide(step, -1.0);
			for (double bearing = -45.0; bearing <= 45.0 + 1e-9; bearing += 1.0)
				add(FenceDistanceMeters(bearing) * std::sin(Radians(bearing)),
				    FenceDistanceMeters(bearing) * std::cos(Radians(bearing)));
			for (int step = foulSteps; step >= 1; --step) foulSide(step, 1.0);
			for (double bearing = 135.0; bearing < 180.0 - 1e-9; bearing += ArcStepDegrees)
				add(BackstopDistanceMeters * std::sin(Radians(bearing)), BackstopDistanceMeters * std::cos(Radians(bearing)));
			// Outward normals from the neighbors on either side; the field is convex, so the
			// outward one points away from home plate.
			const std::size_t count = points.size();
			for (std::size_t i = 0; i < count; ++i)
			{
				const BoundaryPoint& previous = points[(i + count - 1) % count];
				const BoundaryPoint& next = points[(i + 1) % count];
				double nu = next.v - previous.v, nv = -(next.u - previous.u);
				if (nu * points[i].u + nv * points[i].v < 0.0)
				{
					nu = -nu;
					nv = -nv;
				}
				const double length = std::hypot(nu, nv);
				points[i].normalU = nu / length;
				points[i].normalV = nv / length;
			}
			return points;
		}
	}

	// The edge of the playing field (the base of the walls) as a closed loop ordered by bearing.
	[[nodiscard]] inline const std::vector<BoundaryPoint>& Boundary()
	{
		static const std::vector<BoundaryPoint> boundary = Detail::BuildBoundary();
		return boundary;
	}

	// Distance from home plate to the wall along a bearing.
	[[nodiscard]] inline double BoundaryDistanceMeters(const double bearingDegrees) noexcept
	{
		const auto& points = Boundary();
		const double bearing = std::remainder(bearingDegrees, 360.0);
		const double du = std::sin(Radians(bearing)), dv = std::cos(Radians(bearing));
		const auto upper = std::upper_bound(points.begin(), points.end(), bearing,
		    [](const double value, const BoundaryPoint& point) { return value < point.bearingDegrees; });
		const BoundaryPoint& b = upper == points.end() ? points.front() : *upper;
		const BoundaryPoint& a = upper == points.begin() ? points.back() : *(upper - 1);
		// Where the ray from home plate crosses the wall segment from a to b.
		const double eu = b.u - a.u, ev = b.v - a.v;
		const double denominator = du * ev - dv * eu;
		if (std::abs(denominator) < 1e-12) return std::hypot(a.u, a.v);
		return (a.u * ev - a.v * eu) / denominator;
	}

	[[nodiscard]] inline bool IsInsidePlayingField(const double u, const double v) noexcept
	{
		return std::hypot(u, v) < BoundaryDistanceMeters(std::atan2(u, v) * 180.0 / std::numbers::pi);
	}

	struct WallProximity final
	{
		// Shortest horizontal distance outside the wall (zero or less inside the field).
		double behindWallMeters{};
		BoundaryPoint nearest{};
	};

	// How far behind the wall a point outside the playing field lies, and the wall section
	// it is behind.
	[[nodiscard]] inline WallProximity BehindWall(const double u, const double v) noexcept
	{
		const auto& points = Boundary();
		WallProximity best;
		best.behindWallMeters = std::numeric_limits<double>::infinity();
		const std::size_t count = points.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const BoundaryPoint& a = points[i];
			const BoundaryPoint& b = points[(i + 1) % count];
			const double eu = b.u - a.u, ev = b.v - a.v;
			const double lengthSquared = eu * eu + ev * ev;
			const double t = lengthSquared > 0.0 ? std::clamp(((u - a.u) * eu + (v - a.v) * ev) / lengthSquared, 0.0, 1.0)
			                                     : 0.0;
			const double distance = std::hypot(u - (a.u + eu * t), v - (a.v + ev * t));
			if (distance < best.behindWallMeters)
			{
				best.behindWallMeters = distance;
				best.nearest = t < .5 ? a : b;
			}
		}
		if (IsInsidePlayingField(u, v)) best.behindWallMeters = -best.behindWallMeters;
		return best;
	}
}
