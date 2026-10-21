#pragma once

#include "DebugWorldProjection.hpp"
#include "PitcherPresentation.hpp"
#include "ProjectedSceneMeshes.hpp"
#include "UiTextRasterization.hpp"
#include "../Physics/BatMaterial.hpp"
#include "../Physics/BatGeometry.hpp"

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
		// Fixed to center field; never follows the viewer or a hand panel.
		const double distance =
		    (std::max)(GetGroundFanRadiusMeters(), GetPitchDistanceMeters() + 10.0);
		const double scale = distance / 100.0;
		return {-(distance + DebugWorldLayout::BatterToPlateDistanceMeters) + .5, 44.0 * scale,
		    4.0 * scale, 17.0 * scale, scale};
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
		if (state.hasBattedMetrics)
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
			// negative displacement reads +0.0; a `cm == 0 ? 0.0 : cm` select can
			// be folded back to the negative zero by the MSVC optimizer.
			const double cm = std::round(meters * 1000.0) / 10.0;
			stream << (cm < 0.0 ? L'-' : L'+') << std::fixed << std::setprecision(1) << std::abs(cm);
			return stream.str();
		};
		const bool reveal = state.pitchMetricsVisible && state.hasPitchBreak;
		return {L"判定  " + call,
		    L"変化  横 " + (reveal ? signedCentimeters(state.pitchHorizontalBreakMeters) : L"--") +
		    L" cm    縦 " + (reveal ? signedCentimeters(state.pitchVerticalBreakMeters) : L"--") + L" cm"};
	}

	struct FieldPitchPlotLayout final
	{
		double left{}, bottom{}, width{}, height{};
		double centerXMeters{}, centerYMeters{}, unitsPerMeter{};
		double z{};
		double endpointRadius{};
		bool hasVisibleMovement{};

		[[nodiscard]] Physics::Vector3 Project(const Physics::Vector3& point) const
		{
			// Catcher faces -z, so simulation x points right. Depth stays orthographic.
			return {left + width * .5 + (point.x - centerXMeters) * unitsPerMeter,
			    bottom + height * .5 + (point.y - centerYMeters) * unitsPerMeter, z};
		}
	};

	[[nodiscard]] inline FieldPitchPlotLayout BuildFieldPitchPlotLayout(
	    const FieldStatisticsState& state)
	{
		const auto board = BuildFieldScoreboardLayout();
		const double s = board.unitsPerLayoutUnit;
		double minX = 0, maxX = 0, minY = 0, maxY = 0;
		if (state.pitchMetricsVisible && !state.pitchTrace.empty())
		{
			minX = maxX = state.pitchTrace.front().x;
			minY = maxY = state.pitchTrace.front().y;
			for (const auto& point : state.pitchTrace)
			{
				minX = (std::min)(minX, point.x);
				maxX = (std::max)(maxX, point.x);
				minY = (std::min)(minY, point.y);
				maxY = (std::max)(maxY, point.y);
			}
		}
		const double width = 11.8 * s, height = 9.6 * s;
		const double availableWidth = width - .9 * s, availableHeight = height - .9 * s;
		const double extentX = maxX - minX, extentY = maxY - minY;
		// Sub-micrometer residuals are numerical noise, not a movement to magnify.
		const bool hasMovement = (std::max)(extentX, extentY) > 1e-7;
		double unitsPerMeter = (std::min)(availableWidth, availableHeight) /
		    (2 * state.pitchBallRadiusMeters);
		if (hasMovement)
		{
			unitsPerMeter = extentX * availableHeight > extentY * availableWidth
			    ? availableWidth / extentX : availableHeight / extentY;
		}
		return {-board.width * .5 + 30.6 * s, board.bottom + 4.8 * s, width, height,
		    .5 * (minX + maxX), .5 * (minY + maxY),
		    unitsPerMeter, board.centerZ + .08, .18 * s, hasMovement};
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
		text(L"横＋：右 / 縦＋：上", left + sizeScale,
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

		const auto plot = BuildFieldPitchPlotLayout(state);
		text(L"軌道（捕手目線）", plot.left, top - .65 * sizeScale,
		    plot.width, 1.05 * sizeScale, white, z + .05);
		rect(plot.left, plot.bottom, plot.left + plot.width, plot.bottom + plot.height, z + .03, header);
		if (state.pitchMetricsVisible && !state.pitchTrace.empty())
		{
			if (plot.hasVisibleMovement)
				for (std::size_t i = 1; i < state.pitchTrace.size(); ++i)
					segment(plot.Project(state.pitchTrace[i - 1]), plot.Project(state.pitchTrace[i]),
					    .14 * sizeScale, accent);
			auto endpoint = plot.Project(state.pitchTrace.back());
			endpoint.z += .02;
			disc(endpoint, plot.endpointRadius, amber);
		}

		const double batLeft = plot.left + .4 * sizeScale;
		const double batWidth = plot.width - .8 * sizeScale;
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
		text(contactLabel, plot.left, bottom + 4.0 * sizeScale, plot.width,
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
