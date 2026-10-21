#pragma once

#include "FieldMarkings.hpp"
#include "StadiumGeometry.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	namespace StadiumStyle
	{
		inline constexpr std::array<float, 4> WarningTrackColor{.30F, .19F, .12F, 1.0F};
		inline constexpr std::array<float, 4> OutfieldWallColor{.035F, .15F, .095F, 1.0F};
		inline constexpr std::array<float, 4> HomeRunLineColor{.96F, .78F, .14F, 1.0F};
		inline constexpr std::array<float, 4> FoulWallColor{.05F, .09F, .19F, 1.0F};
		inline constexpr std::array<float, 4> NetColor{.80F, .82F, .85F, .16F};
		inline constexpr std::array<float, 4> SeatColorA{.08F, .15F, .34F, 1.0F};
		inline constexpr std::array<float, 4> SeatColorB{.10F, .19F, .42F, 1.0F};
		inline constexpr std::array<float, 4> SeatColorC{.07F, .13F, .29F, 1.0F};
		inline constexpr std::array<float, 4> BattersEyeColor{.015F, .05F, .03F, 1.0F};
		inline constexpr std::array<float, 4> ConcourseColor{.17F, .18F, .21F, 1.0F};
		inline constexpr std::array<float, 4> FoulPoleColor{.98F, .80F, .10F, 1.0F};
		inline constexpr std::array<float, 4> FoulPoleScreenColor{.98F, .80F, .10F, .40F};
		inline constexpr std::array<float, 4> TowerColor{.33F, .34F, .38F, 1.0F};
		inline constexpr std::array<float, 4> LampColor{1.0F, .97F, .86F, 1.0F};
		inline constexpr std::array<float, 4> ScoreboardFrameColor{.20F, .21F, .25F, 1.0F};
		inline constexpr std::array<float, 4> ScoreboardFaceColor{.015F, .015F, .025F, 1.0F};
		inline constexpr std::array<float, 4> ScoreboardPanelColor{.05F, .22F, .12F, 1.0F};
		inline constexpr std::array<float, 4> MarkerColor{.95F, .95F, .92F, 1.0F};
	}

	// The ballpark around the field markings: outfield and foul-territory grass mowed in a
	// checkerboard, the warning track, the outfield wall with its home-run line and distance
	// markers, the foul-side walls, the backstop and its netting, the seating bowl, the
	// batter's eye, the foul poles, light towers and a scoreboard. Uses the field
	// coordinates of BuildFieldMarkingsWorldMesh; the grass lies beneath its layers.
	[[nodiscard]] inline IndexedWorldMesh BuildStadiumWorldMesh(const double plateFrontDepth, const double floorHeight)
	{
		namespace S = Stadium;
		using Physics::Vector3;
		IndexedWorldMesh mesh;
		const double apexZ = plateFrontDepth + HomePlateDepthMeters;
		const auto ground = [&](const double u, const double v)
		{
			// The field markings' bottom layer: lifted with distance to keep depth precision.
			return Vector3{u, floorHeight + .003 + .00015 * std::hypot(u, v), apexZ - v};
		};
		const auto at = [&](const double u, const double v, const double height)
		{ return Vector3{u, floorHeight + height, apexZ - v}; };
		const auto vertex = [&](const Vector3& p)
		{
			mesh.vertices.push_back(p);
			return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
		};
		// Flat colors with a fixed light from high behind home plate.
		const auto lit = [](const std::array<float, 4>& color, const Vector3& normal)
		{
			const Vector3 light = Physics::NormalizeOrZero(Vector3{-.25, .85, .45});
			const double facing = std::abs(Physics::Dot(Physics::NormalizeOrZero(normal), light));
			const auto brightness = static_cast<float>(.55 + .45 * facing);
			return std::array<float, 4>{color[0] * brightness, color[1] * brightness, color[2] * brightness, color[3]};
		};
		const auto quad = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
		                      const std::array<float, 4>& color, const bool shaded = true)
		{
			const auto ia = vertex(a), ib = vertex(b), ic = vertex(c), id = vertex(d);
			const auto shade = shaded ? lit(color, Physics::Cross(b - a, d - a)) : color;
			mesh.triangles.push_back({{ia, ib, ic}, shade});
			mesh.triangles.push_back({{ia, ic, id}, shade});
		};
		// An upright box on the ground, as four sides and a top.
		const auto column = [&](const double u, const double v, const double half, const double bottom,
		                        const double top, const std::array<float, 4>& color)
		{
			const std::array<std::array<double, 2>, 4> corners{
			    {{u - half, v - half}, {u + half, v - half}, {u + half, v + half}, {u - half, v + half}}};
			for (std::size_t i = 0; i < 4; ++i)
			{
				const auto& p = corners[i];
				const auto& q = corners[(i + 1) % 4];
				quad(at(p[0], p[1], bottom), at(q[0], q[1], bottom), at(q[0], q[1], top), at(p[0], p[1], top), color);
			}
			quad(at(corners[0][0], corners[0][1], top), at(corners[1][0], corners[1][1], top),
			    at(corners[2][0], corners[2][1], top), at(corners[3][0], corners[3][1], top), color);
		};
		// A flat panel standing at (u, v) facing home plate.
		const auto panel = [&](const double u, const double v, const double width, const double bottom,
		                       const double top, const std::array<float, 4>& color)
		{
			const double length = std::hypot(u, v);
			const double ru = v / length * width * .5, rv = -u / length * width * .5;
			quad(at(u - ru, v - rv, bottom), at(u + ru, v + rv, bottom), at(u + ru, v + rv, top),
			    at(u - ru, v - rv, top), color, false);
		};

		// The wall loop, thinned to about a degree or a few meters between points, keeping
		// both ends of every change of section.
		std::vector<S::BoundaryPoint> boundary;
		{
			const auto& full = S::Boundary();
			for (std::size_t i = 0; i < full.size(); ++i)
			{
				const auto& point = full[i];
				const bool sectionEdge = point.section != full[(i + 1) % full.size()].section ||
				    point.section != full[(i + full.size() - 1) % full.size()].section;
				if (!boundary.empty() && !sectionEdge)
				{
					const auto& last = boundary.back();
					const double turn = std::abs(std::remainder(point.bearingDegrees - last.bearingDegrees, 360.0));
					if (turn < 1.25 && std::hypot(point.u - last.u, point.v - last.v) < 4.0) continue;
				}
				boundary.push_back(point);
			}
		}
		const std::size_t count = boundary.size();
		struct Edge final
		{
			double u, v; // wall base
			double trackU, trackV; // inner edge of the warning track
		};
		std::vector<Edge> edges;
		edges.reserve(count);
		for (const auto& point : boundary)
		{
			edges.push_back({point.u, point.v, point.u - point.normalU * S::WarningTrackWidthMeters,
			    point.v - point.normalV * S::WarningTrackWidthMeters});
		}

		// Grass from home plate out to the warning track, mowed in a checkerboard of 25 ft
		// rings and 7.5 degree wedges. The infield skin, cutouts and lines cover it.
		{
			constexpr double Ring = 25.0 * S::Feet;
			for (std::size_t i = 0; i < count; ++i)
			{
				const Edge& a = edges[i];
				const Edge& b = edges[(i + 1) % count];
				const double lengthA = std::hypot(a.trackU, a.trackV), lengthB = std::hypot(b.trackU, b.trackV);
				const double bearing = std::atan2(a.trackU + b.trackU, a.trackV + b.trackV) * 180.0 / std::numbers::pi;
				const int wedge = static_cast<int>(std::floor((bearing + 180.0) / 7.5));
				const int rings = static_cast<int>(std::ceil((std::max)(lengthA, lengthB) / Ring));
				for (int ring = 0; ring < rings; ++ring)
				{
					const double fa0 = (std::min)(1.0, ring * Ring / lengthA), fa1 = (std::min)(1.0, (ring + 1) * Ring / lengthA);
					const double fb0 = (std::min)(1.0, ring * Ring / lengthB), fb1 = (std::min)(1.0, (ring + 1) * Ring / lengthB);
					if (fa0 >= fa1 && fb0 >= fb1) continue;
					const auto& color = (ring + wedge) % 2 == 0 ? FieldMarkingStyle::InfieldGrassColorA
					                                            : FieldMarkingStyle::InfieldGrassColorB;
					quad(ground(a.trackU * fa0, a.trackV * fa0), ground(b.trackU * fb0, b.trackV * fb0),
					    ground(b.trackU * fb1, b.trackV * fb1), ground(a.trackU * fa1, a.trackV * fa1), color, false);
				}
			}
		}

		// Warning track all the way around, then the walls.
		for (std::size_t i = 0; i < count; ++i)
		{
			const Edge& a = edges[i];
			const Edge& b = edges[(i + 1) % count];
			quad(ground(a.trackU, a.trackV), ground(b.trackU, b.trackV), ground(b.u, b.v), ground(a.u, a.v),
			    StadiumStyle::WarningTrackColor, false);
			const S::Section section = boundary[i].section;
			const bool outfield = section == S::Section::Outfield || section == S::Section::BattersEye;
			const double top = outfield ? S::FenceHeightMeters
			                 : section == S::Section::Backstop ? S::BackstopWallHeightMeters
			                                                   : S::FoulWallHeightMeters;
			const auto& wall = outfield ? StadiumStyle::OutfieldWallColor : StadiumStyle::FoulWallColor;
			if (outfield)
			{
				// The yellow home-run line along the top of the wall.
				constexpr double Line = .12;
				quad(at(a.u, a.v, 0.0), at(b.u, b.v, 0.0), at(b.u, b.v, top - Line), at(a.u, a.v, top - Line), wall);
				quad(at(a.u, a.v, top - Line), at(b.u, b.v, top - Line), at(b.u, b.v, top), at(a.u, a.v, top),
				    StadiumStyle::HomeRunLineColor, false);
			}
			else
			{
				quad(at(a.u, a.v, 0.0), at(b.u, b.v, 0.0), at(b.u, b.v, top), at(a.u, a.v, top), wall);
			}
			if (section == S::Section::Backstop)
			{
				quad(at(a.u, a.v, top), at(b.u, b.v, top), at(b.u, b.v, S::BackstopNetTopMeters),
				    at(a.u, a.v, S::BackstopNetTopMeters), StadiumStyle::NetColor, false);
			}
		}

		// The seating bowl behind every wall, in bands of three rows, with alternating
		// sections; the batter's eye stays dark and seatless. A concourse wall closes off the
		// top, and dividing walls stand where sections of different height meet.
		{
			constexpr int RowsPerBand = 3;
			const auto behind = [&](const S::BoundaryPoint& p, const double distance, const double height)
			{ return at(p.u + p.normalU * distance, p.v + p.normalV * distance, height); };
			const auto crest = [](const S::Section section)
			{ return S::FirstRowHeightMeters(section) + S::RowCount(section) * S::RowRiseMeters + 3.0; };
			for (std::size_t i = 0; i < count; ++i)
			{
				const auto& a = boundary[i];
				const auto& b = boundary[(i + 1) % count];
				const S::Section section = a.section;
				const int rows = S::RowCount(section);
				const double first = S::FirstRowHeightMeters(section);
				const int sectionIndex = static_cast<int>(std::floor((a.bearingDegrees + 180.0) / 9.0));
				for (int row = 0; row < rows; row += RowsPerBand)
				{
					const int last = (std::min)(rows, row + RowsPerBand);
					const double d0 = row * S::RowDepthMeters, d1 = last * S::RowDepthMeters;
					const double h0 = first + row * S::RowRiseMeters, h1 = first + last * S::RowRiseMeters;
					const auto& color = section == S::Section::BattersEye ? StadiumStyle::BattersEyeColor
					                  : (row / RowsPerBand) % 2 == 0 ? (sectionIndex % 2 == 0 ? StadiumStyle::SeatColorA
					                                                                          : StadiumStyle::SeatColorC)
					                                                 : StadiumStyle::SeatColorB;
					quad(behind(a, d0, h0), behind(b, d0, h0), behind(b, d1, h1), behind(a, d1, h1), color);
				}
				const double depth = rows * S::RowDepthMeters;
				const double top = first + rows * S::RowRiseMeters;
				quad(behind(a, depth, top), behind(b, depth, top), behind(b, depth, top + 3.0), behind(a, depth, top + 3.0),
				    StadiumStyle::ConcourseColor);
				if (S::FirstRowHeightMeters(b.section) != first || S::RowCount(b.section) != rows)
				{
					const double reach = (std::max)(depth, S::StandsDepthMeters(b.section));
					const double height = (std::max)(crest(section), crest(b.section));
					quad(behind(b, 0.0, 0.0), behind(b, reach, 0.0), behind(b, reach, height), behind(b, 0.0, height),
					    StadiumStyle::ConcourseColor);
				}
			}
		}

		// Foul poles on the wall at both lines, each with its screen on the fair side.
		for (const double side : {-1.0, 1.0})
		{
			const double pole = S::LineFenceDistanceMeters;
			const double c = 1.0 / std::numbers::sqrt2;
			const double u = side * pole * c, v = pole * c;
			column(u, v, .15, 0.0, S::FoulPoleHeightMeters, StadiumStyle::FoulPoleColor);
			const double su = -side * c * .9, sv = c * .9;
			quad(at(u, v, S::FenceHeightMeters), at(u + su, v + sv, S::FenceHeightMeters),
			    at(u + su, v + sv, S::FoulPoleHeightMeters * .7), at(u, v, S::FoulPoleHeightMeters * .7),
			    StadiumStyle::FoulPoleScreenColor, false);
		}

		// Light towers behind the outfield and down both lines.
		for (const double bearing : {-32.0, 32.0, -100.0, 100.0})
		{
			const S::Section section = S::SectionAt(bearing);
			const double distance = S::BoundaryDistanceMeters(bearing) + S::StandsDepthMeters(section) + 4.0;
			const double u = distance * std::sin(S::Radians(bearing)), v = distance * std::cos(S::Radians(bearing));
			constexpr double Height = 45.0;
			column(u, v, .5, 0.0, Height, StadiumStyle::TowerColor);
			panel(u, v, 10.0, Height - .5, Height + 4.5, StadiumStyle::TowerColor);
			// Lamps on the face toward the field, a little in front of the frame.
			const double inward = (distance - .4) / distance;
			for (int lampRow = 0; lampRow < 3; ++lampRow)
			{
				panel(u * inward, v * inward, 9.0, Height + lampRow * 1.5, Height + lampRow * 1.5 + 1.1,
				    StadiumStyle::LampColor);
			}
		}

		// Scoreboard above the right-center bleachers.
		{
			constexpr double Bearing = 27.0;
			const double distance = S::BoundaryDistanceMeters(Bearing) + S::StandsDepthMeters(S::Section::Outfield) + 2.0;
			const double u = distance * std::sin(S::Radians(Bearing)), v = distance * std::cos(S::Radians(Bearing));
			const double bottom = S::FenceHeightMeters + S::OutfieldRows * S::RowRiseMeters + 3.0;
			column(u, v, .6, 0.0, bottom, StadiumStyle::TowerColor);
			panel(u, v, 28.0, bottom, bottom + 13.0, StadiumStyle::ScoreboardFrameColor);
			const double inward = (distance - .3) / distance;
			panel(u * inward, v * inward, 26.0, bottom + 1.0, bottom + 12.0, StadiumStyle::ScoreboardFaceColor);
			const double front = (distance - .5) / distance;
			for (int line = 0; line < 3; ++line)
			{
				panel(u * front, v * front, 22.0, bottom + 2.0 + line * 3.2, bottom + 4.2 + line * 3.2,
				    StadiumStyle::ScoreboardPanelColor);
			}
		}

		// Distance markers painted on the outfield wall, as seven-segment numerals.
		{
			// Segments a-g as (x0, y0, x1, y1) in a 1 x 2 digit cell.
			constexpr std::array<std::array<double, 4>, 7> Segments{{{0, 2, 1, 2}, {1, 1, 1, 2}, {1, 0, 1, 1},
			    {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 1, 0, 2}, {0, 1, 1, 1}}};
			constexpr std::array<int, 10> Lit{0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
			for (const double bearing : {-44.5, -22.5, 0.0, 22.5, 44.5})
			{
				const double fence = S::FenceDistanceMeters(bearing);
				const int feet = static_cast<int>(std::lround(fence / S::Feet / 5.0)) * 5;
				const std::wstring text = std::to_wstring(feet);
				// In front of the wall face, reading left to right from home plate.
				const double distance = S::BoundaryDistanceMeters(bearing) - .08;
				const double cu = distance * std::sin(S::Radians(bearing)), cv = distance * std::cos(S::Radians(bearing));
				const double ru = std::cos(S::Radians(bearing)), rv = -std::sin(S::Radians(bearing));
				constexpr double Scale = .42, Stroke = .085, Advance = .72;
				const double baseline = 1.25 - Scale;
				const double width = Advance * static_cast<double>(text.size()) - (Advance - Scale);
				for (std::size_t index = 0; index < text.size(); ++index)
				{
					const int digit = text[index] - L'0';
					const double left = -width * .5 + Advance * static_cast<double>(index);
					for (std::size_t segment = 0; segment < Segments.size(); ++segment)
					{
						if ((Lit[static_cast<std::size_t>(digit)] & (1 << segment)) == 0) continue;
						const auto& s = Segments[segment];
						const double x0 = left + s[0] * Scale - Stroke * .5, x1 = left + s[2] * Scale + Stroke * .5;
						const double y0 = baseline + s[1] * Scale - Stroke * .5, y1 = baseline + s[3] * Scale + Stroke * .5;
						quad(at(cu + ru * x0, cv + rv * x0, y0), at(cu + ru * x1, cv + rv * x1, y0),
						    at(cu + ru * x1, cv + rv * x1, y1), at(cu + ru * x0, cv + rv * x0, y1), StadiumStyle::MarkerColor,
						    false);
					}
				}
			}
		}
		return mesh;
	}
} // namespace BaseballSimulatorVR::Common
