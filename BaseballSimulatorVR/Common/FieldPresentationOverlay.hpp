#pragma once

#include "DebugWorldProjection.hpp"
#include "PitcherPresentation.hpp"
#include "ProjectedSceneMeshes.hpp"
#include "UiTextRasterization.hpp"

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
		double width{30.0};
		double bottom{4.0};
		double height{12.0};
	};

	[[nodiscard]] inline FieldScoreboardLayout BuildFieldScoreboardLayout()
	{
		// Fixed to center field; never follows the viewer or a hand panel.
		const double distance =
		    (std::max)(GetGroundFanRadiusMeters(), GetPitchDistanceMeters() + 10.0);
		const double scale = distance / 100.0;
		return {-(distance + DebugWorldLayout::BatterToPlateDistanceMeters) + .5, 30.0 * scale,
		    4.0 * scale, 12.0 * scale};
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
		if (state.pitchMetricsVisible && state.hasBattedMetrics)
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
		    line{.08F, .21F, .27F, 1};
		const double left = -layout.width * .5, right = layout.width * .5;
		const double bottom = layout.bottom, top = bottom + layout.height;
		const double z = layout.centerZ;
		const double sizeScale = layout.width / 30.0;
		rect(left - .5 * sizeScale, bottom - .5 * sizeScale, right + .5 * sizeScale,
		    top + .5 * sizeScale, z - .05, frame);
		rect(left, bottom, right, top, z, screen);
		rect(left, top - 2.8 * sizeScale, right, top, z + .02, header);
		for (const double x : {-layout.width * .32, layout.width * .32})
		{
			rect(x - .30 * sizeScale, 0, x + .30 * sizeScale, bottom, z - .1, frame);
		}
		const auto rows = BuildFieldStatisticsLines(state);
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			const double rowTop = top - (.5 + static_cast<double>(row) * 2.65) * sizeScale;
			const Color& color = row == 0 ? accent : white;
			text(rows[row], left + sizeScale, rowTop, layout.width - 2 * sizeScale,
			    1.65 * sizeScale, color, z + .05);
			if (row > 0 && row + 1 < rows.size())
			{
				rect(left + .7 * sizeScale, rowTop - 1.92 * sizeScale, right - .7 * sizeScale,
				    rowTop - 1.90 * sizeScale, z + .03, line);
			}
		}
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
			        label, projectedWidthPixels * .93,
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
