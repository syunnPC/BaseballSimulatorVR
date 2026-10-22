#pragma once

#include "CatcherMittMesh.hpp"
#include "DebugWorldProjection.hpp"
#include "HomePlateGeometry.hpp"
#include "PitcherPresentation.hpp"
#include "ProjectedSceneMeshes.hpp"
#include "UiTextRasterization.hpp"
#include "../Physics/BatMaterial.hpp"
#include "../Physics/BatGeometry.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	struct FieldScoreboardLayout final
	{
		double centerZ{0.0};
		double width{44.0};
		double bottom{4.0};
		double height{17.0};
		double unitsPerLayoutUnit{1.0};
	};

	[[nodiscard]] inline FieldScoreboardLayout BuildFieldScoreboardLayout()
	{
		// Fixed to center field; never follows the viewer or a hand panel. It stands behind the
		// center-field bleachers as the park's video board, its foot above their top row.
		namespace S = Stadium;
		const double bleachers = S::CenterFenceDistanceMeters + S::StandsDepthMeters(S::Section::BattersEye) + 4.0;
		const double distance = (std::max)({GetGroundFanRadiusMeters(), GetPitchDistanceMeters() + 10.0, bleachers});
		const double scale = distance / 100.0;
		const double topRow = S::FirstRowHeightMeters(S::Section::BattersEye) +
		    S::RowCount(S::Section::BattersEye) * S::RowRiseMeters + 4.0;
		return {-(distance + DebugWorldLayout::BatterToPlateDistanceMeters) + .5, 44.0 * scale,
		    (std::max)(4.0 * scale, topRow), 17.0 * scale, scale};
	}

	[[nodiscard]] inline std::vector<std::wstring> BuildFieldStatisticsLines(
	    const FieldStatisticsState& state)
	{
		const auto value = [](const double number, const int precision)
		{
			std::wostringstream stream;
			stream << std::fixed << std::setprecision(precision) << number;
			return stream.str();
		};
		std::vector<std::wstring> rows;
		rows.reserve(4);
		if (state.pitchMetricsVisible)
		{
			rows.push_back(L"球種  " + state.pitchName);
			rows.push_back(L"球速  " + value(state.pitchSpeedMetersPerSecond * 3.6, 1) +
			    L" km/h    回転  " + value(state.pitchSpinRpm, 0) + L" rpm");
		}
		else
		{
			rows.push_back(L"球種  --");
			rows.push_back(L"球速  -- km/h    回転  -- rpm");
		}
		if (state.catcherMode)
		{
			rows.push_back(L"捕球  " + std::to_wstring(state.catcherCaught) + L" / " +
			    std::to_wstring(state.catcherPitches) + L" 球");
			rows.push_back(L"結果  " + (state.catcherResult.empty() ? std::wstring{L"--"} : state.catcherResult));
		}
		else if (state.hasBattedMetrics)
		{
			rows.push_back(L"初速  " + value(state.battedLaunchSpeedMetersPerSecond * 3.6, 1) +
			               L" km/h    角度  " + value(state.battedLaunchAngleDegrees, 1) + L"°");
			rows.push_back((state.battedHasGroundContact
			                       ? L"飛距離  " + value(state.battedDistanceMeters, 1) + L" m"
			                       : std::wstring{L"飛距離  -- m"}) +
			               L"    最高点  " + value(state.battedMaxHeightMeters, 1) + L" m");
		}
		else
		{
			rows.push_back(L"初速  -- km/h    角度  --°");
			rows.push_back(L"飛距離  -- m    最高点  -- m");
		}
		return rows;
	}

	[[nodiscard]] inline std::array<std::wstring, 2> BuildFieldPitchAnalysisLines(
	    const FieldStatisticsState& state)
	{
		std::wstring call = L"--";
		if (state.pitchMetricsVisible)
		{
			switch (state.zoneCall)
			{
			case PitchZoneCall::Strike: call = L"ストライク"; break;
			case PitchZoneCall::Ball: call = L"ボール"; break;
			case PitchZoneCall::Contact: call = L"打撃"; break;
			case PitchZoneCall::Pending: break;
			}
		}
		const auto signedCentimeters = [](double meters)
		{
			std::wostringstream stream;
			// Round before formatting and print the sign explicitly so a tiny
			// negative displacement reads +0.0, never -0.0. A select that maps -0.0
			// to 0.0 is not enough: the MSVC optimizer can fold it away.
			const double cm = std::round(meters * 1000.0) / 10.0;
			stream << (cm < 0.0 ? L'-' : L'+') << std::fixed << std::setprecision(1) << std::abs(cm);
			return stream.str();
		};
		if (!state.battedResult.empty()) call = state.battedResult;
		const bool reveal = state.pitchMetricsVisible && state.hasPitchBreak;
		return {L"判定  " + call,
		    L"変化  横 " + (reveal ? signedCentimeters(state.pitchHorizontalBreakMeters) : L"--") +
		    L" cm    縦 " + (reveal ? signedCentimeters(state.pitchVerticalBreakMeters) : L"--") + L" cm"};
	}

	// The pitch's flight drawn two ways side by side: as the catcher sees it from behind the
	// plate (left) and from straight above with the pitcher at the top and the plate at the
	// bottom (right). Both show home plate, so where the ball crossed it is plain.
	enum class FieldPitchPlotView
	{
		Catcher,
		Overhead
	};

	// The drawing area is 11.8 layout units wide, split into two panels with a gap between.
	inline constexpr double FieldPitchPlotPanelGap = .3;
	inline constexpr double FieldPitchPlotPanelWidth = (11.8 - FieldPitchPlotPanelGap) * .5;
	// The catcher's view is taken from about the umpire's eyes, just behind the catcher: far
	// enough back to take in the whole zone and the plate.
	inline constexpr double FieldPitchViewEyeBehindPlateMeters = 2.0;
	inline constexpr double FieldPitchViewEyeHeightMeters = 1.1;
	// From above, the flight's depth is squeezed evenly into the panel, so the path bends only
	// where the ball did. Home plate is drawn in its true shape at the sideways scale (at most
	// 5 units per meter), its front edge at the plate front, so it reads at a glance where the
	// ball crossed it.
	inline constexpr double FieldPitchOverheadPlateUnitsPerMeter = 5.0;

	// Home plate on the ground (y = 0) at the given plate-front depth, front edge first.
	[[nodiscard]] inline std::array<Physics::Vector3, 5> BuildFieldPitchPlotPlate(const double plateFrontDepth)
	{
		constexpr double Half = HomePlateWidthMeters * .5;
		return {{{-Half, 0.0, plateFrontDepth}, {Half, 0.0, plateFrontDepth},
		    {Half, 0.0, plateFrontDepth + HomePlateDepthMeters * .5},
		    {0.0, 0.0, plateFrontDepth + HomePlateDepthMeters},
		    {-Half, 0.0, plateFrontDepth + HomePlateDepthMeters * .5}}};
	}

	// The strike zone's outline at the plate front: bottom left, bottom right, top right, top left.
	[[nodiscard]] inline std::array<Physics::Vector3, 4> BuildFieldPitchPlotZone(const FieldStatisticsState& state)
	{
		const double half = state.zoneWidthMeters * .5, depth = state.plateFrontDepthMeters;
		return {{{-half, state.zoneBottomMeters, depth}, {half, state.zoneBottomMeters, depth},
		    {half, state.zoneTopMeters, depth}, {-half, state.zoneTopMeters, depth}}};
	}

	struct FieldPitchPlotLayout final
	{
		double left{}, bottom{}, width{}, height{};
		double z{};
		double endpointRadius{};
		FieldPitchPlotView view{FieldPitchPlotView::Catcher};
		double plateFrontDepthMeters{};
		// Catcher: the eye and a pinhole projection, framed on the panel.
		Physics::Vector3 eye{};
		double focalUnits{}, centerU{}, centerV{};
		// Overhead: the sideways scale (also the plate's) and the even depth scale.
		double centerXMeters{}, unitsPerMeter{};
		double topDepthMeters{}, depthUnitsPerMeter{}, contentTop{};

		[[nodiscard]] double DistanceFromEye(const Physics::Vector3& point) const
		{
			return (std::max)(eye.z - point.z, .05);
		}

		[[nodiscard]] Physics::Vector3 Project(const Physics::Vector3& point) const
		{
			if (view == FieldPitchPlotView::Overhead)
			{
				// Catcher faces -z, so simulation x points right in both views.
				return {left + width * .5 + (point.x - centerXMeters) * unitsPerMeter,
				    contentTop - (point.z - topDepthMeters) * depthUnitsPerMeter, z};
			}
			const double distance = DistanceFromEye(point);
			return {left + width * .5 + focalUnits * ((point.x - eye.x) / distance - centerU),
			    bottom + height * .5 + focalUnits * ((point.y - eye.y) / distance - centerV), z};
		}

		// A corner of home plate: in perspective from behind, true to shape from above.
		[[nodiscard]] Physics::Vector3 ProjectPlateCorner(const Physics::Vector3& corner) const
		{
			auto projected = Project(corner);
			if (view == FieldPitchPlotView::Overhead)
			{
				projected.y = contentTop - (plateFrontDepthMeters - topDepthMeters) * depthUnitsPerMeter -
				    (corner.z - plateFrontDepthMeters) * unitsPerMeter;
			}
			return projected;
		}
	};

	// Each view frames home plate, the zone (from behind) and the whole flight in its panel.
	[[nodiscard]] inline FieldPitchPlotLayout BuildFieldPitchPlotLayout(
	    const FieldStatisticsState& state, const FieldPitchPlotView view = FieldPitchPlotView::Catcher)
	{
		const auto board = BuildFieldScoreboardLayout();
		const double s = board.unitsPerLayoutUnit;
		const double areaLeft = -board.width * .5 + 30.6 * s;
		FieldPitchPlotLayout layout;
		layout.view = view;
		layout.left = view == FieldPitchPlotView::Catcher
		    ? areaLeft : areaLeft + (FieldPitchPlotPanelWidth + FieldPitchPlotPanelGap) * s;
		layout.bottom = board.bottom + 4.8 * s;
		layout.width = FieldPitchPlotPanelWidth * s;
		layout.height = 9.6 * s;
		layout.z = board.centerZ + .08;
		layout.endpointRadius = .18 * s;
		layout.plateFrontDepthMeters = state.plateFrontDepthMeters;
		const double availableWidth = layout.width - .9 * s, availableHeight = layout.height - .9 * s;

		std::vector<Physics::Vector3> content;
		const auto plate = BuildFieldPitchPlotPlate(state.plateFrontDepthMeters);
		content.assign(plate.begin(), plate.end());
		if (state.pitchMetricsVisible)
		{
			content.insert(content.end(), state.pitchTrace.begin(), state.pitchTrace.end());
		}
		if (view == FieldPitchPlotView::Catcher)
		{
			const auto zone = BuildFieldPitchPlotZone(state);
			content.insert(content.end(), zone.begin(), zone.end());
			layout.eye = {0.0, FieldPitchViewEyeHeightMeters,
			    state.plateFrontDepthMeters + FieldPitchViewEyeBehindPlateMeters};
			double minU = 1e300, maxU = -1e300, minV = 1e300, maxV = -1e300;
			for (const auto& point : content)
			{
				const double distance = layout.DistanceFromEye(point);
				const double u = (point.x - layout.eye.x) / distance, v = (point.y - layout.eye.y) / distance;
				minU = (std::min)(minU, u);
				maxU = (std::max)(maxU, u);
				minV = (std::min)(minV, v);
				maxV = (std::max)(maxV, v);
			}
			layout.centerU = .5 * (minU + maxU);
			layout.centerV = .5 * (minV + maxV);
			layout.focalUnits = (std::min)(availableWidth / (std::max)(maxU - minU, 1e-9),
			    availableHeight / (std::max)(maxV - minV, 1e-9));
			return layout;
		}

		double minX = 1e300, maxX = -1e300, minZ = 0.0, maxZ = state.plateFrontDepthMeters;
		for (const auto& point : content)
		{
			minX = (std::min)(minX, point.x);
			maxX = (std::max)(maxX, point.x);
			minZ = (std::min)(minZ, point.z);
			maxZ = (std::max)(maxZ, point.z);
		}
		layout.centerXMeters = .5 * (minX + maxX);
		layout.unitsPerMeter = (std::min)({availableWidth / (std::max)(maxX - minX, 1e-9),
		    FieldPitchOverheadPlateUnitsPerMeter * s, .5 * availableHeight / HomePlateDepthMeters});
		layout.topDepthMeters = minZ;
		// The flight down to the plate front fills the height left above the plate's point; a
		// flight carried on past the plate's point fills it all.
		const double toPlate = state.plateFrontDepthMeters - minZ;
		const double plateUnits = HomePlateDepthMeters * layout.unitsPerMeter;
		layout.depthUnitsPerMeter = toPlate > 1e-9 ? (availableHeight - plateUnits) / toPlate : layout.unitsPerMeter;
		if ((maxZ - state.plateFrontDepthMeters) * layout.depthUnitsPerMeter > plateUnits)
		{
			layout.depthUnitsPerMeter = availableHeight / (maxZ - minZ);
		}
		layout.depthUnitsPerMeter = (std::min)(layout.depthUnitsPerMeter, layout.unitsPerMeter);
		const double below = (std::max)(plateUnits, (maxZ - state.plateFrontDepthMeters) * layout.depthUnitsPerMeter);
		layout.contentTop = layout.bottom + layout.height * .5 + (toPlate * layout.depthUnitsPerMeter + below) * .5;
		return layout;
	}

	template <typename TTriangle, typename TText>
	void EmitFieldScoreboardGeometry(
	    const FieldStatisticsState& state, TTriangle&& triangle, TText&& text)
	{
		if (!state.visible)
		{
			return;
		}
		using Physics::Vector3;
		using Color = std::array<float, 4>;
		const auto layout = BuildFieldScoreboardLayout();
		const auto rect =
		    [&](double left, double bottom, double right, double top, double z, const Color& color)
		{
			const Vector3 a{left, bottom, z}, b{right, bottom, z}, c{right, top, z},
			    d{left, top, z};
			triangle(a, b, c, color.data());
			triangle(a, c, d, color.data());
		};
		constexpr Color frame{.16F, .23F, .30F, 1}, screen{.008F, .023F, .040F, 1},
		    header{.025F, .20F, .29F, 1}, white{.88F, .96F, 1, 1}, accent{.24F, .91F, .77F, 1},
		    line{.08F, .21F, .27F, 1}, green{.27F, .88F, .43F, 1},
		    amber{1.F, .69F, .22F, 1}, wood{.55F, .37F, .20F, 1};
		const double left = -layout.width * .5, right = layout.width * .5;
		const double bottom = layout.bottom, top = bottom + layout.height;
		const double z = layout.centerZ;
		const double sizeScale = layout.unitsPerLayoutUnit;
		rect(left - .5 * sizeScale, bottom - .5 * sizeScale, right + .5 * sizeScale,
		    top + .5 * sizeScale, z - .05, frame);
		rect(left, bottom, right, top, z, screen);
		rect(left, top - 2.8 * sizeScale, left + 29.2 * sizeScale, top, z + .02, header);
		rect(left + 29.2 * sizeScale, bottom + .6 * sizeScale,
		    left + 29.24 * sizeScale, top - .6 * sizeScale, z + .02, line);
		for (const double x : {-layout.width * .32, layout.width * .32})
		{
			rect(x - .30 * sizeScale, 0, x + .30 * sizeScale, bottom, z - .1, frame);
		}
		const auto rows = BuildFieldStatisticsLines(state);
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			const double rowTop = top - (.5 + static_cast<double>(row) * 2.65) * sizeScale;
			const Color& color = row == 0 ? accent : white;
			text(rows[row], left + sizeScale, rowTop, 27.2 * sizeScale,
			    1.65 * sizeScale, color, z + .05);
			if (row > 0 && row + 1 < rows.size())
			{
				rect(left + .7 * sizeScale, rowTop - 1.92 * sizeScale, left + 28.5 * sizeScale,
				    rowTop - 1.90 * sizeScale, z + .03, line);
			}
		}
		const auto analysis = BuildFieldPitchAnalysisLines(state);
		for (std::size_t row = 0; row < analysis.size(); ++row)
			text(analysis[row], left + sizeScale, top - (11.1 + row * 2.5) * sizeScale,
			    27.2 * sizeScale, 1.55 * sizeScale, row == 0 ? accent : white, z + .05);
		text(L"横＋：右 / 縦＋：上 / 真上から：上が投手側", left + sizeScale,
		    bottom + 1.15 * sizeScale, 27.2 * sizeScale, .65 * sizeScale, white, z + .05);

		const auto segment = [&](Vector3 a, Vector3 b, double thickness, const Color& color)
		{
			const double length = std::hypot(b.x - a.x, b.y - a.y);
			if (length == 0) return;
			const Vector3 n{-(b.y - a.y) * thickness / (2 * length),
			    (b.x - a.x) * thickness / (2 * length), 0};
			triangle(a + n, a - n, b - n, color.data());
			triangle(a + n, b - n, b + n, color.data());
		};
		const auto disc = [&](Vector3 center, double radius, const Color& color)
		{
			constexpr int slices = 32;
			constexpr double tau = 6.2831853071795864769;
			for (int i = 0; i < slices; ++i)
			{
				const double a = tau * i / slices, b = tau * (i + 1) / slices;
				triangle(center, Vector3{center.x + radius * std::cos(a),
				    center.y + radius * std::sin(a), center.z},
				    Vector3{center.x + radius * std::cos(b), center.y + radius * std::sin(b), center.z},
				    color.data());
			}
		};

		const auto catcherPlot = BuildFieldPitchPlotLayout(state, FieldPitchPlotView::Catcher);
		const auto overheadPlot = BuildFieldPitchPlotLayout(state, FieldPitchPlotView::Overhead);
		constexpr Color plateColor{.80F, .82F, .84F, 1}, zoneColor{.62F, .78F, .86F, 1};
		for (const auto* plot : {&catcherPlot, &overheadPlot})
		{
			const bool catcher = plot->view == FieldPitchPlotView::Catcher;
			text(catcher ? L"捕手目線" : L"真上から", plot->left,
			    top - .65 * sizeScale, plot->width, 1.05 * sizeScale, white, z + .05);
			rect(plot->left, plot->bottom, plot->left + plot->width, plot->bottom + plot->height, z + .03, header);
			const auto onPanel = [&](const Vector3& point, const double depth)
			{
				auto projected = plot->Project(point);
				projected.z = depth;
				return projected;
			};
			const auto plate = BuildFieldPitchPlotPlate(state.plateFrontDepthMeters);
			std::array<Vector3, 5> corners{};
			for (std::size_t i = 0; i < plate.size(); ++i)
			{
				corners[i] = plot->ProjectPlateCorner(plate[i]);
				corners[i].z = z + .04;
			}
			for (std::size_t i = 2; i < corners.size(); ++i)
				triangle(corners[0], corners[i - 1], corners[i], plateColor.data());
			if (catcher)
			{
				const auto zone = BuildFieldPitchPlotZone(state);
				for (std::size_t i = 0; i < zone.size(); ++i)
					segment(onPanel(zone[i], z + .05), onPanel(zone[(i + 1) % zone.size()], z + .05),
					    .05 * sizeScale, zoneColor);
			}
			if (state.pitchMetricsVisible && !state.pitchTrace.empty())
			{
				for (std::size_t i = 1; i < state.pitchTrace.size(); ++i)
				{
					// From behind the plate the ball's path thickens as it comes closer.
					const double thickness = catcher
					    ? std::clamp(.30 / plot->DistanceFromEye((state.pitchTrace[i - 1] + state.pitchTrace[i]) * .5),
					          .05, .16) * sizeScale
					    : .12 * sizeScale;
					segment(plot->Project(state.pitchTrace[i - 1]), plot->Project(state.pitchTrace[i]), thickness, accent);
				}
				auto endpoint = plot->Project(state.pitchTrace.back());
				endpoint.z += .02;
				disc(endpoint, plot->endpointRadius, amber);
			}
		}

		// The bat diagram spans both panels.
		const double areaLeft = catcherPlot.left;
		const double areaWidth = overheadPlot.left + overheadPlot.width - catcherPlot.left;
		const double batLeft = areaLeft + .4 * sizeScale;
		const double batWidth = areaWidth - .8 * sizeScale;
		const double batY = bottom + 2.0 * sizeScale;
		const auto batX = [&](double fraction) { return batLeft + batWidth * fraction; };
		const auto& sweet = Physics::GetBatMaterialProfile(
		    Physics::IsValidBatMaterial(state.batMaterial) ? state.batMaterial : Physics::BatMaterial::Wood);
		std::wstring contactLabel = L"打点  --   緑＝芯";
		if (state.hasBattedMetrics && state.hasBatContactPosition)
		{
			std::wostringstream label;
			label << L"打点  " << std::fixed << std::setprecision(1)
			      << state.batContactFraction * state.contactBatLengthMeters * 100 << L" cm   緑＝芯";
			contactLabel = label.str();
		}
		text(contactLabel, areaLeft, bottom + 4.0 * sizeScale, areaWidth,
		    1.0 * sizeScale, white, z + .05);
		for (std::size_t i = 1; i < Physics::BatBodyProfile.size(); ++i)
		{
			const auto& p0 = Physics::BatBodyProfile[i - 1];
			const auto& p1 = Physics::BatBodyProfile[i];
			const double x0 = batX(p0.fraction), x1 = batX(p1.fraction);
			for (const bool inner : {false, true})
			{
				const double radiusScale = (inner ? .355 : .40) * sizeScale;
				const double r0 = p0.radiusRatio * radiusScale, r1 = p1.radiusRatio * radiusScale;
				const Vector3 a{x0, batY - r0, z + (inner ? .07 : .06)},
				    b{x1, batY - r1, a.z}, c{x1, batY + r1, a.z},
				    d{x0, batY + r0, a.z};
				const auto& color = inner ? wood : white;
				if (r1 > 0) triangle(a, b, c, color.data());
				triangle(a, c, d, color.data());
			}
		}
		rect(batX(sweet.sweetSpotStartRatio), batY - .35 * sizeScale,
		    batX(sweet.sweetSpotEndRatio), batY + .35 * sizeScale, z + .09, green);
		if (state.hasBattedMetrics && state.hasBatContactPosition)
		{
			const double fraction = std::clamp(state.batContactFraction, 0.0, 1.0);
			const auto& color = fraction >= sweet.sweetSpotStartRatio &&
			    fraction <= sweet.sweetSpotEndRatio ? green : amber;
			segment({batX(fraction), batY - .65 * sizeScale, z + .12},
			    {batX(fraction), batY + .65 * sizeScale, z + .12}, .08 * sizeScale, color);
			disc({batX(fraction), batY + .80 * sizeScale, z + .12}, .17 * sizeScale, color);
		}
		text(L"グリップ", batLeft - .3 * sizeScale, bottom + 1.0 * sizeScale,
		    3.4 * sizeScale, .65 * sizeScale, white, z + .05);
		text(L"先端", batLeft + batWidth - 2.1 * sizeScale, bottom + 1.0 * sizeScale,
		    2.4 * sizeScale, .65 * sizeScale, white, z + .05);
	}

	// The sign card floating above the mitt in catcher mode, facing the catcher.
	template <typename TTriangle, typename TText>
	void EmitCatcherSignCardGeometry(const FieldPresentationState& state, TTriangle&& triangle, TText&& text)
	{
		if (!state.catcherMitt || state.catcherSignLine.empty())
		{
			return;
		}
		using Physics::Vector3;
		const Vector3 center = BuildCatcherMittFrame(*state.catcherMitt).pocket + Vector3{0.0, .27, 0.0};
		constexpr double Width = .36, Height = .115;
		constexpr std::array<float, 4> backing{.02F, .04F, .08F, .82F}, accent{.24F, .91F, .77F, 1.F},
		    white{.88F, .96F, 1.F, 1.F};
		const auto at = [center](const double x, const double y, const double z = 0.0)
		{ return center + Vector3{x, y, z}; };
		triangle(at(-Width * .5, -Height * .5), at(Width * .5, -Height * .5), at(Width * .5, Height * .5),
		    backing.data());
		triangle(at(-Width * .5, -Height * .5), at(Width * .5, Height * .5), at(-Width * .5, Height * .5),
		    backing.data());
		const auto onCard = [at](const double x, const double y) { return at(x, y, .002); };
		text(state.catcherSignLine, -Width * .5 + .012, Height * .5 - .01, Width - .024, .046, accent, onCard);
		text(state.catcherStatusLine, -Width * .5 + .012, Height * .5 - .064, Width - .024, .036, white, onCard);
	}

	template <typename TTriangle>
	void EmitFieldScoreboardMesh(const FieldStatisticsState& state, TTriangle&& triangle,
	    double projectedWidthPixels = 512, double projectedHeightPixels = 340)
	{
		EmitFieldScoreboardGeometry(state, triangle,
		    [&](std::wstring_view label, double left, double top, double maxWidth, double maxHeight,
		        const std::array<float, 4>& color, double z)
		    {
			    const auto raster = UiText::RasterizeToFit(
			        label, projectedWidthPixels * maxWidth / BuildFieldScoreboardLayout().width,
			        projectedHeightPixels * maxHeight / BuildFieldScoreboardLayout().height);
			    if (raster.width <= 0 || raster.height <= 0)
			    {
				    return;
			    }
			    const double scale = (std::min)(maxHeight / raster.height, maxWidth / raster.width);
			    UiText::EmitTextMesh(
			        raster, left + (maxWidth - raster.width * scale) * .5, top, scale, color,
			        [&](double x, double y) { return Physics::Vector3{x, y, z}; }, triangle);
		    });
	}

	template <typename TTriangle>
	void EmitProjectedFieldPresentation(const FieldPresentationState& state,
	    const ViewProjectionState& view, const LONG width, const LONG height, TTriangle&& triangle)
	{
		const auto project = [&](const Physics::Vector3& a, const Physics::Vector3& b,
		                         const Physics::Vector3& c, const float* color)
		{ EmitProjectedWorldTriangle(a, b, c, view, width, height, color, triangle); };
		EmitProjectedPitcherMesh(state.pitcher, view, width, height, triangle);
		EmitProjectedCatcherMitt(state.catcherMitt, view, width, height, triangle);
		EmitCatcherSignCardGeometry(state, project,
		    [&](std::wstring_view label, double left, double top, double maxWidth, double maxHeight,
		        const std::array<float, 4>& color, const auto& worldPoint)
		    {
			    const auto raster = UiText::RasterizeToFit(label, 1200.0 * maxWidth, 1200.0 * maxHeight);
			    if (raster.width <= 0 || raster.height <= 0)
			    {
				    return;
			    }
			    const double scale = (std::min)(maxHeight / raster.height, maxWidth / raster.width);
			    UiText::EmitTextMesh(raster, left, top, scale, color, worldPoint, project);
		    });
		const auto board = BuildFieldScoreboardLayout();
		ScreenPoint topLeft{}, topRight{}, bottomLeft{};
		const bool projected =
		    TryProjectWorldToScreen({-board.width * .5, board.bottom + board.height, board.centerZ},
		        view, width, height, &topLeft, false) &&
		    TryProjectWorldToScreen({board.width * .5, board.bottom + board.height, board.centerZ},
		        view, width, height, &topRight, false) &&
		    TryProjectWorldToScreen({-board.width * .5, board.bottom, board.centerZ}, view, width,
		        height, &bottomLeft, false);
		if (projected)
		{
			EmitFieldScoreboardMesh(state.statistics, project,
			    std::hypot(topRight.x - topLeft.x, topRight.y - topLeft.y),
			    std::hypot(bottomLeft.x - topLeft.x, bottomLeft.y - topLeft.y));
		}
	}
} // namespace BaseballSimulatorVR::Common
