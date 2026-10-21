#pragma once

#include "HomePlateGeometry.hpp"
#include "IndexedWorldMesh.hpp"
#include "StadiumGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	// Official playing-field layout (Official Baseball Rules 2.01 and its field
	// diagrams), in meters. The infield skin follows the common MLB pattern: a 95 ft
	// grass line around the front of the pitcher's plate, 13 ft dirt circles at home,
	// first and third base, and 6 ft dirt paths along the baselines.
	namespace FieldDimensions
	{
		inline constexpr double FeetToMeters = .3048;
		inline constexpr double InchesToMeters = FeetToMeters / 12.0;
		inline constexpr double BaseDistanceMeters = 90.0 * FeetToMeters;
		inline constexpr double SecondBaseDistanceMeters = (127.0 * 12.0 + 3.375) * InchesToMeters;
		inline constexpr double PitchersPlateDistanceMeters = 60.5 * FeetToMeters;
		inline constexpr double InfieldArcRadiusMeters = 95.0 * FeetToMeters;
		inline constexpr double HomeCircleRadiusMeters = 13.0 * FeetToMeters;
		inline constexpr double BaseCutoutRadiusMeters = 13.0 * FeetToMeters;
		inline constexpr double BasePathHalfWidthMeters = 3.0 * FeetToMeters;
		// 18 in square bases (MLB since 2023); bases are 3-5 in tall.
		inline constexpr double BaseSizeMeters = 18.0 * InchesToMeters;
		inline constexpr double BaseHeightMeters = 4.0 * InchesToMeters;
		inline constexpr double LineWidthMeters = 3.0 * InchesToMeters;
		// Batter's boxes: 4 ft by 6 ft, 6 in from the plate, centered on the plate lengthwise.
		inline constexpr double BattersBoxWidthMeters = 4.0 * FeetToMeters;
		inline constexpr double BattersBoxLengthMeters = 6.0 * FeetToMeters;
		inline constexpr double BattersBoxPlateGapMeters = 6.0 * InchesToMeters;
	}

	namespace FieldMarkingStyle
	{
		inline constexpr std::array<float, 4> DirtColor{.36F, .22F, .13F, 1.0F};
		// The two floor tile greens, mowed in diagonal stripes across the infield.
		inline constexpr std::array<float, 4> InfieldGrassColorA{.042F, .084F, .060F, 1.0F};
		inline constexpr std::array<float, 4> InfieldGrassColorB{.050F, .102F, .070F, 1.0F};
		inline constexpr std::array<float, 4> ChalkColor{.88F, .88F, .84F, 1.0F};
		inline constexpr std::array<float, 4> BaseColor{.95F, .95F, .93F, 1.0F};
		inline constexpr std::array<float, 4> BaseSideColor{.74F, .74F, .72F, 1.0F};
		// Foul lines extend this far past the foul poles' angle, so the floor covers them.
		inline constexpr double FoulTerritoryMarginDegrees = 1.5;
	}

	enum class FieldSurface { Grass, Dirt };

	// The surface at a field point (u toward first base, v toward the pitcher, from the plate
	// apex), matching what BuildFieldMarkingsWorldMesh and the stadium draw: the dirt of the
	// home-plate circle, the base paths and cutouts, the infield skin and the warning track,
	// grass everywhere else.
	[[nodiscard]] inline FieldSurface FieldSurfaceAt(const double u, const double v) noexcept
	{
		namespace D = FieldDimensions;
		const double diagonal = D::BaseDistanceMeters / std::numbers::sqrt2;
		if (std::hypot(u, v) <= D::HomeCircleRadiusMeters ||
		    std::hypot(u - diagonal, v - diagonal) <= D::BaseCutoutRadiusMeters ||
		    std::hypot(u + diagonal, v - diagonal) <= D::BaseCutoutRadiusMeters)
			return FieldSurface::Dirt;
		for (const double side : {-1.0, 1.0})
		{
			const double along = (side * u + v) / std::numbers::sqrt2;
			const double across = std::abs(side * u - v) / std::numbers::sqrt2;
			if (along >= 0.0 && along <= D::BaseDistanceMeters && across <= D::BasePathHalfWidthMeters)
				return FieldSurface::Dirt;
		}
		const double bearing = std::atan2(u, v) * 180.0 / std::numbers::pi;
		if (std::abs(bearing) <= 45.0 + FieldMarkingStyle::FoulTerritoryMarginDegrees &&
		    std::hypot(u, v - D::PitchersPlateDistanceMeters) <= D::InfieldArcRadiusMeters)
		{
			const double grassHalf = diagonal - D::BasePathHalfWidthMeters * std::numbers::sqrt2;
			return std::abs(u) + std::abs(v - diagonal) <= grassHalf ? FieldSurface::Grass : FieldSurface::Dirt;
		}
		if (Stadium::IsInsidePlayingField(u, v) &&
		    Stadium::BehindWall(u, v).behindWallMeters >= -Stadium::WarningTrackWidthMeters)
			return FieldSurface::Dirt;
		return FieldSurface::Grass;
	}

	// Field markings drawn over the floor: infield dirt and grass, chalk lines, bases
	// and batter's boxes. Field coordinates are measured from the apex of home plate:
	// u toward the first-base side (+X), v toward the pitcher (-Z). Layers are lifted
	// above the floor in order (the stadium's grass, dirt, infield grass, dirt cutouts,
	// chalk) by a gap that grows with distance so depth precision keeps them apart.
	[[nodiscard]] inline IndexedWorldMesh BuildFieldMarkingsWorldMesh(
	    const double plateFrontDepth, const double floorHeight, const double fieldRadiusMeters)
	{
		namespace D = FieldDimensions;
		using Physics::Vector3;
		IndexedWorldMesh mesh;
		const double apexZ = plateFrontDepth + HomePlateDepthMeters;
		// The skin reaches the far side of the grass arc, so the field is never smaller.
		const double radius = (std::max)(fieldRadiusMeters, D::PitchersPlateDistanceMeters + D::InfieldArcRadiusMeters);
		enum Layer { Dirt = 2, Grass = 3, Cutout = 4, Chalk = 5 };
		const auto world = [&](const double u, const double v, const Layer layer)
		{
			const double distance = std::hypot(u, apexZ - v);
			return Vector3{u, floorHeight + static_cast<int>(layer) * (.003 + .00015 * distance), apexZ - v};
		};
		const auto shade = [&](const std::array<float, 4>& color, const double u, const double v)
		{
			const auto brightness = static_cast<float>(std::clamp(1.0 - .26 * std::hypot(u, v) / radius, .7, 1.0));
			return std::array<float, 4>{color[0] * brightness, color[1] * brightness, color[2] * brightness, color[3]};
		};
		const auto vertex = [&](const Vector3& p)
		{
			mesh.vertices.push_back(p);
			return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
		};
		// Flat pieces face up regardless of the order their corners were listed in.
		const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, const std::array<float, 4>& color)
		{
			const auto& pa = mesh.vertices[a];
			if (Physics::Cross(mesh.vertices[b] - pa, mesh.vertices[c] - pa).y < 0.0) std::swap(b, c);
			mesh.triangles.push_back({{a, b, c}, color});
		};
		using Point = std::array<double, 2>;
		const auto polygon = [&](const std::vector<Point>& points, const Layer layer, const std::array<float, 4>& color)
		{
			double cu = 0.0, cv = 0.0;
			for (const auto& p : points) { cu += p[0]; cv += p[1]; }
			cu /= static_cast<double>(points.size());
			cv /= static_cast<double>(points.size());
			const auto center = vertex(world(cu, cv, layer));
			std::vector<std::uint32_t> ring;
			for (const auto& p : points) ring.push_back(vertex(world(p[0], p[1], layer)));
			for (std::size_t i = 0; i < ring.size(); ++i)
				triangle(center, ring[i], ring[(i + 1) % ring.size()], shade(color, cu, cv));
		};
		const auto circle = [&](const double u, const double v, const double r, const Layer layer,
		                        const std::array<float, 4>& color)
		{
			std::vector<Point> points;
			constexpr int Segments = 48;
			for (int i = 0; i < Segments; ++i)
			{
				const double a = 2.0 * std::numbers::pi * i / Segments;
				points.push_back({u + r * std::cos(a), v + r * std::sin(a)});
			}
			polygon(points, layer, color);
		};
		// A straight band, split so its lift follows the distance along long lines.
		const auto band = [&](const Point& from, const Point& to, const double halfWidth, const Layer layer,
		                      const std::array<float, 4>& color)
		{
			const double length = std::hypot(to[0] - from[0], to[1] - from[1]);
			const double du = (to[0] - from[0]) / length, dv = (to[1] - from[1]) / length;
			const double nu = -dv * halfWidth, nv = du * halfWidth;
			const int pieces = (std::max)(1, static_cast<int>(std::ceil(length / 2.0)));
			for (int i = 0; i < pieces; ++i)
			{
				const double s0 = length * i / pieces, s1 = length * (i + 1) / pieces;
				const Point a{from[0] + du * s0, from[1] + dv * s0}, b{from[0] + du * s1, from[1] + dv * s1};
				const auto p0 = vertex(world(a[0] + nu, a[1] + nv, layer));
				const auto p1 = vertex(world(a[0] - nu, a[1] - nv, layer));
				const auto p2 = vertex(world(b[0] - nu, b[1] - nv, layer));
				const auto p3 = vertex(world(b[0] + nu, b[1] + nv, layer));
				const auto color0 = shade(color, (a[0] + b[0]) * .5, (a[1] + b[1]) * .5);
				triangle(p0, p1, p2, color0);
				triangle(p0, p2, p3, color0);
			}
		};

		const double diagonal = D::BaseDistanceMeters / std::numbers::sqrt2;
		const Point home{0.0, 0.0}, first{diagonal, diagonal}, second{0.0, D::SecondBaseDistanceMeters},
		    third{-diagonal, diagonal};

		// Infield skin: everything in fair territory within the grass line around the
		// front of the pitcher's plate, as a fan from the plate apex.
		{
			const double halfAngle = (45.0 + FieldMarkingStyle::FoulTerritoryMarginDegrees) * std::numbers::pi / 180.0;
			constexpr int Steps = 72;
			const double rubber = D::PitchersPlateDistanceMeters;
			const double arc = D::InfieldArcRadiusMeters;
			std::vector<std::uint32_t> edge;
			for (int i = 0; i <= Steps; ++i)
			{
				const double a = -halfAngle + 2.0 * halfAngle * i / Steps;
				const double along = rubber * std::cos(a);
				const double r = along + std::sqrt(along * along - rubber * rubber + arc * arc);
				edge.push_back(vertex(world(r * std::sin(a), r * std::cos(a), Dirt)));
			}
			const auto apex = vertex(world(0.0, 0.0, Dirt));
			for (int i = 0; i < Steps; ++i)
			{
				const auto& p = mesh.vertices[edge[static_cast<std::size_t>(i)]];
				triangle(apex, edge[static_cast<std::size_t>(i)], edge[static_cast<std::size_t>(i) + 1],
				    shade(FieldMarkingStyle::DirtColor, p.x * .5, (apexZ - p.z) * .5));
			}
		}
		// Dirt paths along both foul-line baselines.
		band(home, first, D::BasePathHalfWidthMeters, Dirt, FieldMarkingStyle::DirtColor);
		band(home, third, D::BasePathHalfWidthMeters, Dirt, FieldMarkingStyle::DirtColor);

		// Infield grass: the square inside the base paths, in mowing stripes parallel to
		// the first-base line.
		{
			const double inset = D::BasePathHalfWidthMeters * std::numbers::sqrt2;
			const double half = diagonal - inset;
			const Point corner{0.0, diagonal - half};
			const double side = half * std::numbers::sqrt2;
			const double c = 1.0 / std::numbers::sqrt2;
			constexpr int Stripes = 8;
			for (int stripe = 0; stripe < Stripes; ++stripe)
			{
				const double a0 = side * stripe / Stripes, a1 = side * (stripe + 1) / Stripes;
				const auto at = [&](const double along, const double across)
				{ return Point{corner[0] + (along - across) * c, corner[1] + (along + across) * c}; };
				polygon({at(0.0, a0), at(side, a0), at(side, a1), at(0.0, a1)}, Grass,
				    stripe % 2 == 0 ? FieldMarkingStyle::InfieldGrassColorA : FieldMarkingStyle::InfieldGrassColorB);
			}
		}

		// Dirt circle around home plate and cutouts at first and third base.
		circle(0.0, 0.0, D::HomeCircleRadiusMeters, Cutout, FieldMarkingStyle::DirtColor);
		circle(first[0], first[1], D::BaseCutoutRadiusMeters, Cutout, FieldMarkingStyle::DirtColor);
		circle(third[0], third[1], D::BaseCutoutRadiusMeters, Cutout, FieldMarkingStyle::DirtColor);

		// Batter's boxes on both sides of the plate.
		const double plateCenterV = HomePlateDepthMeters * .5;
		const double boxFront = plateCenterV + D::BattersBoxLengthMeters * .5;
		{
			const double inner = HomePlateWidthMeters * .5 + D::BattersBoxPlateGapMeters;
			const double outer = inner + D::BattersBoxWidthMeters;
			const double back = plateCenterV - D::BattersBoxLengthMeters * .5;
			const double front = boxFront;
			const double w = D::LineWidthMeters * .5;
			for (const double side : {-1.0, 1.0})
			{
				band({side * inner, back - w}, {side * inner, front + w}, w, Chalk, FieldMarkingStyle::ChalkColor);
				band({side * outer, back - w}, {side * outer, front + w}, w, Chalk, FieldMarkingStyle::ChalkColor);
				band({side * inner, back}, {side * outer, back}, w, Chalk, FieldMarkingStyle::ChalkColor);
				band({side * inner, front}, {side * outer, front}, w, Chalk, FieldMarkingStyle::ChalkColor);
			}
		}

		// Foul lines from the front of the batter's boxes, where they leave the boxes, to the
		// foul poles.
		const double lineReach = Stadium::LineFenceDistanceMeters / std::numbers::sqrt2;
		band({boxFront, boxFront}, {lineReach, lineReach}, D::LineWidthMeters * .5, Chalk, FieldMarkingStyle::ChalkColor);
		band({-boxFront, boxFront}, {-lineReach, lineReach}, D::LineWidthMeters * .5, Chalk, FieldMarkingStyle::ChalkColor);

		// Bases: first and third lie in fair territory against their corner, second is
		// centered on its corner.
		{
			const double s = D::BaseSizeMeters;
			const double c = 1.0 / std::numbers::sqrt2;
			const Point towardFirst{c, c}, towardThird{-c, c};
			const auto bag = [&](const Point& origin, const Point& axisA, const Point& axisB)
			{
				const std::array<Point, 4> corners{origin, Point{origin[0] + axisA[0] * s, origin[1] + axisA[1] * s},
				    Point{origin[0] + (axisA[0] + axisB[0]) * s, origin[1] + (axisA[1] + axisB[1]) * s},
				    Point{origin[0] + axisB[0] * s, origin[1] + axisB[1] * s}};
				std::array<std::uint32_t, 4> bottom{}, top{};
				for (std::size_t i = 0; i < 4; ++i)
				{
					const Vector3 base = world(corners[i][0], corners[i][1], Chalk);
					bottom[i] = vertex(base);
					top[i] = vertex(base + Vector3{0.0, D::BaseHeightMeters, 0.0});
				}
				const auto color = shade(FieldMarkingStyle::BaseColor, origin[0], origin[1]);
				triangle(top[0], top[1], top[2], color);
				triangle(top[0], top[2], top[3], color);
				const auto side = shade(FieldMarkingStyle::BaseSideColor, origin[0], origin[1]);
				for (std::size_t i = 0; i < 4; ++i)
				{
					const std::size_t j = (i + 1) % 4;
					mesh.triangles.push_back({{bottom[i], bottom[j], top[j]}, side});
					mesh.triangles.push_back({{bottom[i], top[j], top[i]}, side});
				}
			};
			bag(first, {-towardFirst[0], -towardFirst[1]}, towardThird);
			bag(third, {-towardThird[0], -towardThird[1]}, towardFirst);
			bag({second[0], second[1] - s * c}, towardFirst, towardThird);
		}
		return mesh;
	}
} // namespace BaseballSimulatorVR::Common
