#pragma once

#include "../Framework.hpp"
#include "../Physics/BallState.hpp"
#include "../Physics/BatGeometry.hpp"
#include "../Physics/Vector3.hpp"
#include "BatOverlayState.hpp"
#include "HomePlateGeometry.hpp"
#include "PreparedWorldProjection.hpp"
#include "IndexedWorldMesh.hpp"
#include "TrajectoryTubeMesh.hpp"
#include "ViewProjectionState.hpp"
#include "StrikeZoneCalibration.hpp"
#include "StrikeZoneSettings.hpp"
#include "StrikeZoneTrajectoryGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	namespace DebugWorldLayout
	{
		inline constexpr double NearClipMeters = 0.05;
		inline constexpr double FarClipMeters = 400.0;
		inline constexpr double FieldGridHalfWidthMeters = 8.0;
		inline constexpr double FieldGridNearDepthMeters = 0.5;
		inline constexpr double FieldGridFarDepthMeters = 28.0;
		inline constexpr double FieldGridSpacingMeters = 1.0;
		inline constexpr double FieldGridSampleStepMeters = 0.35;
		inline constexpr double PitchDistanceMeters = 18.44;
		inline constexpr double BatterToPlateDistanceMeters = 1.5;
		inline constexpr double StrikeZoneBottomMeters = 0.55;
		inline constexpr double StrikeZoneHeightMeters = 1.0;
		inline constexpr double StrikeZoneWidthMeters = 0.44;
		inline constexpr double StrikeZoneTopMeters =
		    StrikeZoneBottomMeters + StrikeZoneHeightMeters;
		inline constexpr double StrikeZoneHalfWidthMeters =
		    StrikeZoneWidthMeters * 0.5;
		inline constexpr double LegacyProjectionDepthMeters = 18.5;
		inline constexpr double LegacyProjectionMinHeightMeters = 0.2;
		inline constexpr double LegacyProjectionHeightMeters = 2.2;
		inline constexpr double ResultPanelCenterXMeters = 0.95;
		inline constexpr double ResultPanelCenterYMeters = 1.20;
		inline constexpr double ResultPanelDepthMeters = 1.2;
		inline constexpr double ResultPanelWidthMeters = 0.44;
		inline constexpr double ResultPanelHeightMeters = 0.62;
		inline constexpr double ResultPanelPaddingMeters = 0.04;
		inline constexpr double ResultPanelChipHeightMeters = 0.068;
		inline constexpr double ResultPanelChipGapMeters = 0.014;
		inline constexpr double ResultPanelScoreBarHeightMeters = 0.052;
		inline constexpr double ResultPanelScoreBarGapMeters = 0.022;
		inline constexpr double ResultPanelBorderThicknessMeters = 0.006;
		inline constexpr double ResultPanelCenterLineHalfWidthMeters = 0.003;
		inline constexpr double MainControlPanelCenterXMeters = -0.95;
		inline constexpr double MainControlPanelCenterYMeters = 1.20;
		inline constexpr double MainControlPanelDepthMeters = 1.2;
		inline constexpr double MainControlPanelWidthMeters = 0.46;
		inline constexpr double MainControlPanelHeightMeters = 0.52;
		inline constexpr double MainControlPanelPaddingMeters = 0.018;
		inline constexpr double MainControlPanelCellGapMeters = 0.008;
		inline constexpr int MainControlPanelColumns = 3;
		inline constexpr int MainControlPanelRows = 4;
		inline constexpr double UiLaserMaxDistanceMeters = 3.2;
		inline constexpr float UiLaserOverlayDepth = 0.001F;
	} // namespace DebugWorldLayout

	struct GroundRuntimeSettings final
	{
		double fanRadiusMeters{ 100.0 };
	};

	inline double g_pitchDistanceMeters{ DebugWorldLayout::PitchDistanceMeters };
	inline StrikeZoneRuntimeSettings g_strikeZoneRuntimeSettings{};
	inline GroundRuntimeSettings g_groundRuntimeSettings{};

	inline void SetPitchDistanceMeters(const double pitchDistanceMeters) noexcept
	{
		g_pitchDistanceMeters = std::clamp(pitchDistanceMeters, 5.0, 30.0);
	}

	[[nodiscard]] inline double GetPitchDistanceMeters() noexcept
	{
		return g_pitchDistanceMeters;
	}

	inline void SetStrikeZoneRuntimeSettings(
	    StrikeZoneRuntimeSettings settings) noexcept
	{
		settings.bottomMeters = std::clamp(settings.bottomMeters, 0.0, 2.5);
		settings.heightMeters = std::clamp(settings.heightMeters, 0.20, 2.0);
		settings.widthMeters = std::clamp(settings.widthMeters, 0.20, 1.2);
		g_strikeZoneRuntimeSettings = settings;
	}

	[[nodiscard]] inline StrikeZoneRuntimeSettings
	GetStrikeZoneRuntimeSettings() noexcept
	{
		return g_strikeZoneRuntimeSettings;
	}

	inline void SetGroundRuntimeSettings(
	    GroundRuntimeSettings settings) noexcept
	{
		settings.fanRadiusMeters = std::clamp(settings.fanRadiusMeters, 10.0, 300.0);
		g_groundRuntimeSettings = settings;
	}

	[[nodiscard]] inline GroundRuntimeSettings
	GetGroundRuntimeSettings() noexcept
	{
		return g_groundRuntimeSettings;
	}

	[[nodiscard]] inline double GetGroundFanRadiusMeters() noexcept
	{
		return g_groundRuntimeSettings.fanRadiusMeters;
	}

	[[nodiscard]] inline double GetStrikeZoneBottomMeters() noexcept
	{
		return g_strikeZoneRuntimeSettings.bottomMeters;
	}

	[[nodiscard]] inline double GetStrikeZoneTopMeters() noexcept
	{
		return g_strikeZoneRuntimeSettings.bottomMeters +
		       g_strikeZoneRuntimeSettings.heightMeters;
	}

	[[nodiscard]] inline double GetStrikeZoneHalfWidthMeters() noexcept
	{
		return g_strikeZoneRuntimeSettings.widthMeters * 0.5;
	}

	struct MainControlPanelLayout final
	{
		double centerX{ DebugWorldLayout::MainControlPanelCenterXMeters };
		double centerY{ DebugWorldLayout::MainControlPanelCenterYMeters };
		double centerZ{ -DebugWorldLayout::MainControlPanelDepthMeters };
		double widthMeters{ DebugWorldLayout::MainControlPanelWidthMeters };
		double heightMeters{ DebugWorldLayout::MainControlPanelHeightMeters };
		double paddingMeters{ DebugWorldLayout::MainControlPanelPaddingMeters };
		double cellGapMeters{ DebugWorldLayout::MainControlPanelCellGapMeters };
		int columns{ DebugWorldLayout::MainControlPanelColumns };
		int rows{ DebugWorldLayout::MainControlPanelRows };
	};

	[[nodiscard]] inline MainControlPanelLayout BuildDefaultMainControlPanelLayout()
	{
		return MainControlPanelLayout{};
	}

	namespace DebugWorldStyle
	{
		inline constexpr LONG BallHalfSizePixels = 8;
		inline constexpr LONG TrailHalfSizePixels = 4;
		inline constexpr LONG WorldLineHalfSizePixels = 2;
		inline constexpr LONG UiLaserHalfSizePixels = 1;
		inline constexpr LONG UiLaserHitHalfSizePixels = 4;
		inline constexpr int BallMeshLatitudeSegments = 24;
		inline constexpr int BallMeshLongitudeSegments = 40;
		inline constexpr double TrailMeshHalfWidthMeters = 0.03;
		inline constexpr double FloorMeshHalfWidthMeters = 5.0;
		inline constexpr double FloorMeshNearDepthMeters = 0.5;
		inline constexpr double FloorMeshFarDepthMeters = 24.5;
		inline constexpr double FloorMeshTileSizeMeters = 1.0;
		inline constexpr double FloorMeshHeightMeters = -0.01;
		inline constexpr double HomePlateHeightMeters = 0.005;
		inline constexpr std::array<float, 4> FloorMeshTileColorA{ 0.04F, 0.08F, 0.06F, 1.0F };
		inline constexpr std::array<float, 4> FloorMeshTileColorB{ 0.05F, 0.10F, 0.07F, 1.0F };
		inline constexpr std::array<float, 4> FloorGridColor{ 0.12F, 0.30F, 0.22F, 1.0F };
		inline constexpr std::array<float, 4> FloorGridMajorColor{ 0.20F, 0.42F, 0.32F, 1.0F };
		inline constexpr std::array<float, 4> PlateColor{ 0.95F, 0.95F, 0.98F, 1.0F };
		inline constexpr std::array<float, 4> BallColor{ 1.0F, 0.98F, 0.80F, 1.0F };
		inline constexpr std::array<float, 4> BatColor{ 0.79F, 0.56F, 0.26F, 1.0F };
		inline constexpr std::array<float, 4> MetalBatColor{ 0.78F, 0.81F, 0.85F, 1.0F };
		inline constexpr std::array<float, 4> UrethaneBatColor{ 0.22F, 0.24F, 0.27F, 1.0F };
		// Bright practice-bat orange so the forgiving bat is recognizable at a glance.
		inline constexpr std::array<float, 4> BeginnerBatColor{ 0.98F, 0.55F, 0.12F, 1.0F };
		inline constexpr std::array<float, 4> BatSweetSpotColor{ 0.08F, 0.95F, 0.20F, 0.35F };
		inline constexpr double BatSweetSpotShellOffsetMeters = 0.001;
		inline constexpr std::array<float, 4> TrailMeshColor{ 0.20F, 0.64F, 0.90F, 0.92F };
		inline constexpr std::array<float, 4> TrailColor{ 0.24F, 0.88F, 1.0F, 1.0F };
		inline constexpr std::array<float, 4> ZoneColor{ 1.0F, 0.55F, 0.18F, 1.0F };
		inline constexpr std::array<float, 4> ResultPanelBackgroundColor{ 0.05F, 0.07F, 0.15F, 1.0F };
		inline constexpr std::array<float, 4> ResultPanelBorderColor{ 0.31F, 0.39F, 0.58F, 1.0F };
		inline constexpr std::array<float, 4> ResultTagStrikeColor{ 0.24F, 0.86F, 0.40F, 1.0F };
		inline constexpr std::array<float, 4> ResultTagBallColor{ 0.94F, 0.34F, 0.30F, 1.0F };
		inline constexpr std::array<float, 4> ResultTagInPlayColor{ 0.24F, 0.74F, 0.96F, 1.0F };
		inline constexpr std::array<float, 4> ResultTagFoulColor{ 0.95F, 0.76F, 0.24F, 1.0F };
		inline constexpr std::array<float, 4> ResultTagPendingColor{ 0.70F, 0.70F, 0.74F, 1.0F };
		inline constexpr std::array<float, 4> ResultScorePositiveColor{ 0.24F, 0.88F, 0.42F, 1.0F };
		inline constexpr std::array<float, 4> ResultScoreNegativeColor{ 0.96F, 0.36F, 0.32F, 1.0F };
		inline constexpr std::array<float, 4> ResultScoreNeutralColor{ 0.22F, 0.24F, 0.28F, 1.0F };
		inline constexpr std::array<float, 4> ResultScoreCenterLineColor{ 0.90F, 0.92F, 0.96F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelBackgroundColor{ 0.05F, 0.07F, 0.15F, 0.96F };
		inline constexpr std::array<float, 4> MainControlPanelBorderColor{ 0.31F, 0.39F, 0.58F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelModeRowColor{ 0.14F, 0.33F, 0.50F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelPresetRowColor{ 0.42F, 0.29F, 0.11F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelPlaybackRowColor{ 0.49F, 0.39F, 0.14F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelOverlayRowColor{ 0.12F, 0.15F, 0.27F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelEnvironmentRowColor{ 0.17F, 0.28F, 0.47F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelPitchRowColor{ 0.13F, 0.27F, 0.42F, 1.0F };
		inline constexpr std::array<float, 4> MainControlPanelLabelColor{ 1.0F, 1.0F, 1.0F, 1.0F };
		inline constexpr std::array<float, 4> UiLaserColor{ 0.78F, 0.90F, 1.0F, 0.92F };
		inline constexpr std::array<float, 4> UiLaserHitColor{ 1.0F, 0.86F, 0.38F, 1.0F };
	} // namespace DebugWorldStyle

	[[nodiscard]] inline Physics::Vector3 ConvertSimulationPositionToWorld(
	    const Physics::Vector3& simulationPosition)
	{
		return Physics::Vector3{simulationPosition.x, simulationPosition.y,
		    simulationPosition.z -
		        (GetPitchDistanceMeters() + DebugWorldLayout::BatterToPlateDistanceMeters)};
	}

	[[nodiscard]] inline bool TryProjectWorldToScreen(const Physics::Vector3& worldPosition,
	    const ViewProjectionState& projectionState, const LONG width, const LONG height,
	    ScreenPoint* const projectedPoint, const bool requireVisibleBounds = true)
	{
		const PreparedWorldProjection projection(projectionState, width, height,
		    DebugWorldLayout::NearClipMeters, DebugWorldLayout::FarClipMeters);
		return projection.Project(worldPosition, projectedPoint, requireVisibleBounds);
	}

	[[nodiscard]] inline ScreenPoint ProjectLegacyWorldToScreen(
	    const Physics::Vector3& position, const LONG width, const LONG height)
	{
		const double normalizedDepth = std::clamp(
		    position.z / DebugWorldLayout::LegacyProjectionDepthMeters,
		    0.0,
		    1.0);
		const double normalizedHeight = std::clamp(
		    (position.y - DebugWorldLayout::LegacyProjectionMinHeightMeters) /
		        DebugWorldLayout::LegacyProjectionHeightMeters,
		    0.0,
		    1.0);

		const double x =
		    normalizedDepth * static_cast<double>((std::max)(1L, width - 1));
		const double y =
		    (1.0 - normalizedHeight) * static_cast<double>((std::max)(1L, height - 1));
		return ScreenPoint{ x, y, static_cast<float>(normalizedDepth) };
	}

	template <typename TProjectFn, typename TEmitSegmentFn>
	void ForEachProjectedLineSegment(
	    const Physics::Vector3& lineStart,
	    const Physics::Vector3& lineEnd,
	    const double sampleStepMeters,
	    TProjectFn&& projectPoint,
	    TEmitSegmentFn&& emitSegment)
	{
		const double dx = lineEnd.x - lineStart.x;
		const double dy = lineEnd.y - lineStart.y;
		const double dz = lineEnd.z - lineStart.z;
		const double lineLength = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
		const double effectiveStep =
		    (sampleStepMeters > 1e-6) ? sampleStepMeters : 0.1;
		const int sampleCount = (std::max)(1,
		                                   static_cast<int>(std::ceil(lineLength / effectiveStep)));

		bool hasVisibleChunkStart = false;
		ScreenPoint chunkStartPoint{};
		ScreenPoint previousProjectedPoint{};
		for (int sampleIndex = 0; sampleIndex <= sampleCount; ++sampleIndex)
		{
			const double blend = static_cast<double>(sampleIndex) /
			                     static_cast<double>(sampleCount);
			const Physics::Vector3 worldPoint{
				lineStart.x + (dx * blend),
				lineStart.y + (dy * blend),
				lineStart.z + (dz * blend)
			};

			ScreenPoint projectedPoint{};
			if (!projectPoint(worldPoint, &projectedPoint))
			{
				if (hasVisibleChunkStart)
				{
					emitSegment(chunkStartPoint, previousProjectedPoint);
					hasVisibleChunkStart = false;
				}
				continue;
			}

			if (!hasVisibleChunkStart)
			{
				chunkStartPoint = projectedPoint;
				hasVisibleChunkStart = true;
			}

			previousProjectedPoint = projectedPoint;
		}

		if (hasVisibleChunkStart)
		{
			emitSegment(chunkStartPoint, previousProjectedPoint);
		}
	}

	[[nodiscard]] inline bool TryProjectWorldTriangle(const Physics::Vector3& worldPoint0,
	    const Physics::Vector3& worldPoint1, const Physics::Vector3& worldPoint2,
	    const ViewProjectionState& projectionState, const LONG width, const LONG height,
	    ScreenPoint* const projectedPoint0, ScreenPoint* const projectedPoint1,
	    ScreenPoint* const projectedPoint2)
	{
		if ((projectedPoint0 == nullptr) || (projectedPoint1 == nullptr) ||
		    (projectedPoint2 == nullptr))
		{
			return false;
		}

		const PreparedWorldProjection projection(projectionState, width, height,
		    DebugWorldLayout::NearClipMeters, DebugWorldLayout::FarClipMeters);
		return projection.Project(worldPoint0, projectedPoint0, false) &&
		       projection.Project(worldPoint1, projectedPoint1, false) &&
		       projection.Project(worldPoint2, projectedPoint2, false);
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedWorldTriangle(
	    const Physics::Vector3& worldPoint0,
	    const Physics::Vector3& worldPoint1,
	    const Physics::Vector3& worldPoint2,
	    const ViewProjectionState& projectionState,
	    const LONG width,
	    const LONG height,
	    const float* const color,
	    TDrawTriangleFn&& drawTriangle)
	{
		const PreparedWorldProjection projection(projectionState, width, height,
		    DebugWorldLayout::NearClipMeters, DebugWorldLayout::FarClipMeters);
		projection.EmitNearClippedTriangle(worldPoint0, worldPoint1, worldPoint2,
		    [&](const ScreenPoint& p0, const ScreenPoint& p1, const ScreenPoint& p2)
		    { drawTriangle(p0, p1, p2, color); });
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedHomePlateOverlay(const ViewProjectionState& view, LONG width, LONG height,
	    TDrawTriangleFn&& drawTriangle)
	{
		if (!view.isValid || width <= 0 || height <= 0)
		{
			return;
		}
		const auto vertices = GetHomePlateVertices(-DebugWorldLayout::BatterToPlateDistanceMeters,
		    DebugWorldStyle::HomePlateHeightMeters);
		for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
		{
			EmitProjectedWorldTriangle(vertices[0], vertices[i + 1], vertices[i], view, width, height,
			    DebugWorldStyle::PlateColor.data(), drawTriangle);
		}
	}

	[[nodiscard]] inline LONG LegacyBallMarkerHalfSizePixels(double radiusMeters)
	{
		return (std::max)(1L,
		    static_cast<LONG>(std::lround(DebugWorldStyle::BallHalfSizePixels * radiusMeters /
		                                  Physics::BallProperties{}.radiusMeters)));
	}
	[[nodiscard]] inline LONG OrthographicBallRadiusPixels(
	    double radiusMeters, LONG pixelSpan, double worldSpanMeters)
	{
		return (std::max)(1L,
		    static_cast<LONG>(std::lround(radiusMeters * pixelSpan / worldSpanMeters)));
	}

	[[nodiscard]] inline bool TryProjectBallMarker(const Physics::BallState& ball,
	    const ViewProjectionState& view, LONG width, LONG height, ScreenPoint& center,
	    double& radiusPixels)
	{
		const PreparedWorldProjection projection(
		    view, width, height, DebugWorldLayout::NearClipMeters, DebugWorldLayout::FarClipMeters);
		const auto world = ConvertSimulationPositionToWorld(ball.position);
		if (!projection.Project(world, &center))
		{
			return false;
		}
		const auto q = view.orientation;
		const double w = view.orientationW;
		const Physics::Vector3 right{
		    1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.z * w), 2 * (q.x * q.z - q.y * w)};
		const Physics::Vector3 up{
		    2 * (q.x * q.y - q.z * w), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z + q.x * w)};
		ScreenPoint horizontal, vertical;
		if (!projection.Project(world + right * ball.properties.radiusMeters, &horizontal, false) ||
		    !projection.Project(world + up * ball.properties.radiusMeters, &vertical, false))
		{
			return false;
		}
		radiusPixels =
		    (std::max)(std::abs(horizontal.x - center.x), std::abs(vertical.y - center.y));
		return true;
	}

	template <typename TDrawPointFn, typename TDrawLineFn>
	void EmitProjectedDebugWorldOverlay(const ViewProjectionState& projectionState,
	    const LONG width, const LONG height, const std::vector<Physics::Vector3>& trajectory,
	    const Physics::BallState& latestBallState, TDrawPointFn&& drawPoint, TDrawLineFn&& drawLine,
	    const bool drawTrajectoryPolyline = true, const bool drawFloorGrid = true,
	    const StrikeZoneDisplayMode strikeZoneDisplayMode = StrikeZoneDisplayMode::Box,
	    const bool drawBallMarker = true)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0))
		{
			return;
		}

		auto drawProjectedWorldLine = [&](const Physics::Vector3& lineStart,
		                                  const Physics::Vector3& lineEnd,
		                                  const float* color,
		                                  const double sampleStepMeters = DebugWorldLayout::FieldGridSampleStepMeters) -> void
		{
			ForEachProjectedLineSegment(
			    lineStart,
			    lineEnd,
			    sampleStepMeters,
			    [&](const Physics::Vector3& worldPoint, ScreenPoint* const projectedPoint) -> bool
			    {
				    return TryProjectWorldToScreen(
				        worldPoint,
				        projectionState,
				        width,
				        height,
				        projectedPoint);
			    },
			    [&](const ScreenPoint& segmentStart, const ScreenPoint& segmentEnd) -> void
			    {
				    drawLine(
				        segmentStart,
				        segmentEnd,
				        DebugWorldStyle::WorldLineHalfSizePixels,
				        color);
			    });
		};

		if (drawFloorGrid)
		{
			const int lateralLineCount = static_cast<int>(
			    (DebugWorldLayout::FieldGridHalfWidthMeters * 2.0) /
			    DebugWorldLayout::FieldGridSpacingMeters);
			for (int lineIndex = 0; lineIndex <= lateralLineCount; ++lineIndex)
			{
				const double x = -DebugWorldLayout::FieldGridHalfWidthMeters +
				                 (static_cast<double>(lineIndex) * DebugWorldLayout::FieldGridSpacingMeters);
				const bool isCenterLine = std::abs(x) < 0.001;
				const bool isMajorLine = ((lineIndex % 5) == 0);
				const float* lineColor = isCenterLine
				                             ? DebugWorldStyle::PlateColor.data()
				                             : (isMajorLine
				                                    ? DebugWorldStyle::FloorGridMajorColor.data()
				                                    : DebugWorldStyle::FloorGridColor.data());
				drawProjectedWorldLine(
				    Physics::Vector3{ x, 0.0, -DebugWorldLayout::FieldGridNearDepthMeters },
				    Physics::Vector3{ x, 0.0, -DebugWorldLayout::FieldGridFarDepthMeters },
				    lineColor);
			}

			const int depthLineCount = static_cast<int>(
			    (DebugWorldLayout::FieldGridFarDepthMeters -
			     DebugWorldLayout::FieldGridNearDepthMeters) /
			    DebugWorldLayout::FieldGridSpacingMeters);
			for (int lineIndex = 0; lineIndex <= depthLineCount; ++lineIndex)
			{
				const double depth = DebugWorldLayout::FieldGridNearDepthMeters +
				                     (static_cast<double>(lineIndex) * DebugWorldLayout::FieldGridSpacingMeters);
				const bool isPlateLine =
				    std::abs(depth - DebugWorldLayout::BatterToPlateDistanceMeters) <
				    (DebugWorldLayout::FieldGridSpacingMeters * 0.5);
				const bool isMajorLine = ((lineIndex % 5) == 0);
				const float* lineColor = isPlateLine
				                             ? DebugWorldStyle::PlateColor.data()
				                             : (isMajorLine
				                                    ? DebugWorldStyle::FloorGridMajorColor.data()
				                                    : DebugWorldStyle::FloorGridColor.data());
				drawProjectedWorldLine(
				    Physics::Vector3{
				        -DebugWorldLayout::FieldGridHalfWidthMeters,
				        0.0,
				        -depth },
				    Physics::Vector3{
				        DebugWorldLayout::FieldGridHalfWidthMeters,
				        0.0,
				        -depth },
				    lineColor);
			}
		}

		ForEachStrikeZoneWireSegment(strikeZoneDisplayMode,
		    {-DebugWorldLayout::BatterToPlateDistanceMeters, GetStrikeZoneBottomMeters(),
		     GetStrikeZoneTopMeters()}, [&](const auto& from, const auto& to) {
			    drawProjectedWorldLine(from, to, DebugWorldStyle::ZoneColor.data(), 0.05);
		    });

		if (drawTrajectoryPolyline)
		{
			bool hasPreviousTrailPoint = false;
			ScreenPoint previousTrailPoint{};
			for (const Physics::Vector3& trailPoint : trajectory)
			{
				const Physics::Vector3 trailWorldPoint = ConvertSimulationPositionToWorld(trailPoint);
				ScreenPoint projectedTrailPoint{};
				if (!TryProjectWorldToScreen(
				        trailWorldPoint,
				        projectionState,
				        width,
				        height,
				        &projectedTrailPoint))
				{
					hasPreviousTrailPoint = false;
					continue;
				}

				if (hasPreviousTrailPoint)
				{
					drawLine(
					    previousTrailPoint,
					    projectedTrailPoint,
					    DebugWorldStyle::TrailHalfSizePixels,
					    DebugWorldStyle::TrailColor.data());
				}

				previousTrailPoint = projectedTrailPoint;
				hasPreviousTrailPoint = true;
			}
		}

		if (drawBallMarker)
		{
			ScreenPoint center;
			double radiusPixels{};
			if (TryProjectBallMarker(
			        latestBallState, projectionState, width, height, center, radiusPixels))
			{
				drawPoint(center, (std::max)(1L, static_cast<LONG>(std::ceil(radiusPixels))),
				    DebugWorldStyle::BallColor.data());
			}
		}
	}

	template <typename TDrawLineFn>
	void EmitProjectedZonePassCircleOverlay(const ViewProjectionState& projectionState,
	    const LONG width, const LONG height,
	    const std::optional<Physics::Vector3>& zonePassSimulationPosition,
	    const double circleRadiusMeters, TDrawLineFn&& drawLine)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0) ||
		    !zonePassSimulationPosition.has_value())
		{
			return;
		}

		const Physics::Vector3 centerWorldPoint =
		    ConvertSimulationPositionToWorld(zonePassSimulationPosition.value());
		constexpr int SegmentCount = 24;
		const double CircleRadiusMeters = circleRadiusMeters;
		for (int segmentIndex = 0; segmentIndex < SegmentCount; ++segmentIndex)
		{
			const double startAngle =
			    (2.0 * 3.14159265358979323846 * static_cast<double>(segmentIndex)) /
			    static_cast<double>(SegmentCount);
			const double endAngle =
			    (2.0 * 3.14159265358979323846 * static_cast<double>(segmentIndex + 1)) /
			    static_cast<double>(SegmentCount);
			const Physics::Vector3 startWorldPoint{
			    centerWorldPoint.x + (std::cos(startAngle) * CircleRadiusMeters),
			    centerWorldPoint.y + (std::sin(startAngle) * CircleRadiusMeters),
			    centerWorldPoint.z};
			const Physics::Vector3 endWorldPoint{
				centerWorldPoint.x + (std::cos(endAngle) * CircleRadiusMeters),
				centerWorldPoint.y + (std::sin(endAngle) * CircleRadiusMeters),
				centerWorldPoint.z
			};
			ScreenPoint startPoint{};
			ScreenPoint endPoint{};
			if (!TryProjectWorldToScreen(
			        startWorldPoint,
			        projectionState,
			        width,
			        height,
			        &startPoint,
			        false) ||
			    !TryProjectWorldToScreen(
			        endWorldPoint,
			        projectionState,
			        width,
			        height,
			        &endPoint,
			        false))
			{
				continue;
			}
			drawLine(
			    startPoint,
			    endPoint,
			    DebugWorldStyle::WorldLineHalfSizePixels,
			    DebugWorldStyle::ZoneColor.data());
		}
	}

	[[nodiscard]] inline const std::array<float, 4>& GetBatMaterialColor(
	    const Physics::BatMaterial material)
	{
		switch (material)
		{
		case Physics::BatMaterial::Wood: return DebugWorldStyle::BatColor;
		case Physics::BatMaterial::Metal: return DebugWorldStyle::MetalBatColor;
		case Physics::BatMaterial::Urethane: return DebugWorldStyle::UrethaneBatColor;
		case Physics::BatMaterial::Beginner: return DebugWorldStyle::BeginnerBatColor;
		}
		throw std::invalid_argument("Invalid bat material");
	}

	inline constexpr int BatCircumferenceSegments = 40;
	inline constexpr double BatTwoPi = 6.28318530717958647692;

	struct BatRenderGeometry final
	{
		Physics::Vector3 knob{};
		Physics::Vector3 direction{};
		Physics::Vector3 radialBasisX{};
		Physics::Vector3 radialBasisY{};
		std::array<Physics::Vector3, BatCircumferenceSegments> radialDirections{};
		std::vector<double> sectionFractions;
		double lengthMeters{};
		double radiusMeters{};
		double sweetStartFraction{};
		double sweetEndFraction{};
	};

	[[nodiscard]] inline std::optional<BatRenderGeometry> BuildBatRenderGeometry(
	    const BatOverlayState& batState)
	{
		const auto start = ConvertSimulationPositionToWorld(batState.startSimulationPosition);
		const auto end = ConvertSimulationPositionToWorld(batState.endSimulationPosition);
		const auto axis = end - start;
		const double length = Physics::Length(axis);
		const auto finite = [](const Physics::Vector3& p)
		{ return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
		if (!Physics::IsValidBatMaterial(batState.material) ||
		    !finite(start) || !finite(end) || !finite(batState.upSimulationDirection) ||
		    !std::isfinite(length) || length <= 1e-6 ||
		    !std::isfinite(batState.radiusMeters) || batState.radiusMeters <= 0.0)
			return std::nullopt;
		const auto direction = axis / length;
		auto radialX = batState.upSimulationDirection -
		    direction * Physics::Dot(batState.upSimulationDirection, direction);
		if (Physics::Length(radialX) <= 1e-6)
		{
			const Physics::Vector3 perpendicularSeed = std::abs(direction.y) < .9 ?
			    Physics::Vector3{0., 1., 0.} : Physics::Vector3{1., 0., 0.};
			radialX = perpendicularSeed - direction * Physics::Dot(perpendicularSeed, direction);
		}
		radialX = Physics::NormalizeOrZero(radialX);
		const auto& profile = Physics::GetBatMaterialProfile(batState.material);
		BatRenderGeometry geometry;
		geometry.knob = start;
		geometry.direction = direction;
		geometry.radialBasisX = radialX;
		geometry.radialBasisY = Physics::Cross(direction, radialX);
		geometry.lengthMeters = length;
		geometry.radiusMeters = batState.radiusMeters;
		geometry.sweetStartFraction = profile.sweetSpotStartRatio;
		geometry.sweetEndFraction = profile.sweetSpotEndRatio;
		geometry.sectionFractions.reserve(Physics::BatBodyProfile.size() + 20);
		for (const auto& point : Physics::BatBodyProfile)
			geometry.sectionFractions.push_back(point.fraction);
		geometry.sectionFractions.push_back(profile.sweetSpotStartRatio);
		geometry.sectionFractions.push_back(profile.sweetSpotEndRatio);
		// Extra grip rings resolve the diagonal tape seams without altering the shared profile.
		constexpr int GripSections = 16;
		for (int index = 1; index < GripSections; ++index)
			geometry.sectionFractions.push_back(std::lerp(Physics::BatGripStartRatio,
			    Physics::BatGripEndRatio, static_cast<double>(index) / GripSections));
		std::sort(geometry.sectionFractions.begin(), geometry.sectionFractions.end());
		geometry.sectionFractions.erase(std::unique(geometry.sectionFractions.begin(),
		    geometry.sectionFractions.end(), [](double a, double b) { return std::abs(a - b) < 1e-12; }),
		    geometry.sectionFractions.end());
		for (int segment = 0; segment < BatCircumferenceSegments; ++segment)
		{
			const double angle = BatTwoPi * segment / BatCircumferenceSegments;
			geometry.radialDirections[segment] = radialX * std::cos(angle) +
			    geometry.radialBasisY * std::sin(angle);
		}
		return geometry;
	}

	[[nodiscard]] inline Physics::Vector3 BatRenderSurfacePoint(
	    const BatRenderGeometry& geometry, double fraction, int segment, double shellOffset = 0.)
	{
		return geometry.knob + geometry.direction * (fraction * geometry.lengthMeters) +
		    geometry.radialDirections[segment % BatCircumferenceSegments] *
		    (Physics::BatRadiusAtFraction(fraction, geometry.radiusMeters) + shellOffset);
	}

	[[nodiscard]] inline std::array<float, 4> BatSurfaceColor(
	    Physics::BatMaterial material, double fraction, double angle)
	{
		const auto& base = GetBatMaterialColor(material);
		if (fraction >= Physics::BatGripStartRatio && fraction <= Physics::BatGripEndRatio)
		{
			const double wrap = .5 + .5 * std::cos(BatTwoPi * fraction * 24. - angle);
			const float seam = static_cast<float>(std::pow(wrap, 12.));
			if (material == Physics::BatMaterial::Wood)
				return {base[0] * (.76F + .07F * seam), base[1] * (.73F + .07F * seam),
				    base[2] * (.68F + .07F * seam), 1.F};
			return {.055F + .045F * seam, .060F + .045F * seam, .068F + .045F * seam, 1.F};
		}
		if (material == Physics::BatMaterial::Wood)
		{
			const double grain = std::sin(angle * 11. + .6 * std::sin(fraction * 13.)) *
			    std::sin(angle * 7. - fraction * 2.);
			const float shade = static_cast<float>(.95 + .07 * grain);
			return {base[0] * shade, base[1] * shade, base[2] * shade, 1.F};
		}
		if (material == Physics::BatMaterial::Metal)
		{
			const float brushed = static_cast<float>(.98 + .02 * std::sin(angle * 17.));
			return {base[0] * brushed, base[1] * brushed, base[2] * brushed, 1.F};
		}
		const float finish = static_cast<float>(.97 + .03 * std::sin(angle * 5. + fraction * 8.));
		return {base[0] * finish, base[1] * finish, base[2] * finish, 1.F};
	}

	template <typename TDrawLineFn>
	void EmitBatWorldWireframe(const BatOverlayState& batState, TDrawLineFn&& drawLine)
	{
		const auto geometry = BuildBatRenderGeometry(batState);
		if (!geometry) return;
		for (const bool shell : {false, true})
		{
			const double offset = shell ? DebugWorldStyle::BatSweetSpotShellOffsetMeters : 0.;
			for (std::size_t span = 0; span + 1 < geometry->sectionFractions.size(); ++span)
			{
				const double f0 = geometry->sectionFractions[span], f1 = geometry->sectionFractions[span + 1];
				if (shell && (f0 < geometry->sweetStartFraction || f1 > geometry->sweetEndFraction)) continue;
				for (int segment = 0; segment < BatCircumferenceSegments; ++segment)
				{
					const auto color = shell ? DebugWorldStyle::BatSweetSpotColor :
					    BatSurfaceColor(batState.material, (f0 + f1) * .5,
					        BatTwoPi * (segment + .5) / BatCircumferenceSegments);
					const auto a = BatRenderSurfacePoint(*geometry, f0, segment, offset);
					const auto b = BatRenderSurfacePoint(*geometry, f0, segment + 1, offset);
					const auto c = BatRenderSurfacePoint(*geometry, f1, segment, offset);
					drawLine(a, b, color);
					drawLine(a, c, color);
					if (shell && f1 == geometry->sweetEndFraction)
						drawLine(c, BatRenderSurfacePoint(*geometry, f1, segment + 1, offset), color);
				}
			}
		}
	}

	template <typename TDrawTriangleFn>
	void EmitBatWorldMesh(const BatOverlayState& batState, TDrawTriangleFn&& drawTriangle)
	{
		const auto geometry = BuildBatRenderGeometry(batState);
		if (!geometry) return;
		for (const bool shell : {false, true})
		{
			const double offset = shell ? DebugWorldStyle::BatSweetSpotShellOffsetMeters : 0.;
			for (std::size_t span = 0; span + 1 < geometry->sectionFractions.size(); ++span)
			{
				const double f0 = geometry->sectionFractions[span], f1 = geometry->sectionFractions[span + 1];
				if (shell && (f0 < geometry->sweetStartFraction || f1 > geometry->sweetEndFraction)) continue;
				for (int segment = 0; segment < BatCircumferenceSegments; ++segment)
				{
					const double angle = BatTwoPi * (segment + .5) / BatCircumferenceSegments;
					const auto color = shell ? DebugWorldStyle::BatSweetSpotColor :
					    BatSurfaceColor(batState.material, (f0 + f1) * .5, angle);
					const auto a = BatRenderSurfacePoint(*geometry, f0, segment, offset);
					const auto b = BatRenderSurfacePoint(*geometry, f0, segment + 1, offset);
					const auto c = BatRenderSurfacePoint(*geometry, f1, segment + 1, offset);
					const auto d = BatRenderSurfacePoint(*geometry, f1, segment, offset);
					drawTriangle(a, b, c, color, shell);
					if (f1 < 1.) drawTriangle(a, c, d, color, shell);
					if (!shell && span == 0)
						drawTriangle(geometry->knob, b, a, BatSurfaceColor(batState.material, 0., angle), false);
				}
			}
		}
	}

	template <typename TDrawLineFn>
	void EmitProjectedBatOverlay(
	    const ViewProjectionState& projectionState,
	    const LONG width, const LONG height,
	    const std::optional<BatOverlayState>& batOverlayState,
	    TDrawLineFn&& drawLine)
	{
		if (!projectionState.isValid || width <= 0 || height <= 0 || !batOverlayState) return;
		EmitBatWorldWireframe(*batOverlayState,
		    [&](const Physics::Vector3& start, const Physics::Vector3& end, const auto& color)
		    {
			    ScreenPoint projectedStart{}, projectedEnd{};
			    if (TryProjectWorldToScreen(start, projectionState, width, height, &projectedStart, false) &&
			        TryProjectWorldToScreen(end, projectionState, width, height, &projectedEnd, false))
				    drawLine(projectedStart, projectedEnd,
				        DebugWorldStyle::WorldLineHalfSizePixels, color.data());
		    });
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedSolidBatOverlay(
	    const ViewProjectionState& projectionState,
	    const LONG width, const LONG height,
	    const std::optional<BatOverlayState>& batOverlayState,
	    TDrawTriangleFn&& drawTriangle)
	{
		if (!projectionState.isValid || width <= 0 || height <= 0 || !batOverlayState) return;
		const auto lightDirection = Physics::NormalizeOrZero(Physics::Vector3{-0.34, 0.78, -0.52});
		const auto knob = ConvertSimulationPositionToWorld(batOverlayState->startSimulationPosition);
		const auto batAxis = batOverlayState->endSimulationPosition - batOverlayState->startSimulationPosition;
		EmitBatWorldMesh(*batOverlayState,
		    [&](const Physics::Vector3& point0, const Physics::Vector3& point1,
		        const Physics::Vector3& point2, const auto& color, const bool sweetSpot)
		    {
			    const auto normal = Physics::Cross(point1 - point0, point2 - point0);
			    if (Physics::Length(normal) <= 1e-10 ||
			        Physics::Dot(normal, projectionState.position - (point0 + point1 + point2) / 3.0) <= 0.0)
				    return;
			    const auto unitNormal = Physics::NormalizeOrZero(normal);
			    const double diffuse = std::clamp(Physics::Dot(unitNormal, lightDirection), 0.0, 1.0);
			    const float lightScale = sweetSpot ? 1.0F : static_cast<float>(.30 + .62 * diffuse);
			    const auto eyeDirection = Physics::NormalizeOrZero(
			        projectionState.position - (point0 + point1 + point2) / 3.);
			    const auto halfVector = Physics::NormalizeOrZero(lightDirection + eyeDirection);
			    const double fraction = Physics::Dot((point0 + point1 + point2) / 3. - knob, batAxis) /
			        Physics::LengthSquared(batAxis);
			    const bool grip = fraction >= Physics::BatGripStartRatio && fraction <= Physics::BatGripEndRatio;
			    const bool metal = !grip && batOverlayState->material == Physics::BatMaterial::Metal;
			    const double gloss = grip ? .02 : metal ? .38 : .07;
			    const float specular = sweetSpot ? 0.F : static_cast<float>(gloss *
			        std::pow((std::max)(0., Physics::Dot(unitNormal, halfVector)), metal ? 42. : 20.));
			    const std::array<float, 4> shadedColor{
				    (std::min)(1.F, color[0] * lightScale + specular),
				    (std::min)(1.F, color[1] * lightScale + specular),
				    (std::min)(1.F, color[2] * lightScale + specular), color[3]};
			    EmitProjectedWorldTriangle(point0, point1, point2, projectionState, width, height,
			        shadedColor.data(), drawTriangle);
		    });
	}

	template <typename TTriangle>
	void EmitBallWorldMesh(
	    const Physics::Vector3& ballCenter, double ballRadiusMeters, TTriangle&& emitBallTriangle)
	{
		constexpr double pi = 3.14159265358979323846;
		for (int latitudeIndex = 0; latitudeIndex < DebugWorldStyle::BallMeshLatitudeSegments;
		    ++latitudeIndex)
		{
			const double latitude0 =
			    (-pi * 0.5) + (static_cast<double>(latitudeIndex) * pi /
			                      static_cast<double>(DebugWorldStyle::BallMeshLatitudeSegments));
			const double latitude1 =
			    (-pi * 0.5) + (static_cast<double>(latitudeIndex + 1) * pi /
			                      static_cast<double>(DebugWorldStyle::BallMeshLatitudeSegments));
			const double sinLatitude0 = std::sin(latitude0);
			const double sinLatitude1 = std::sin(latitude1);
			const double cosLatitude0 = std::cos(latitude0);
			const double cosLatitude1 = std::cos(latitude1);

			for (int longitudeIndex = 0;
			    longitudeIndex < DebugWorldStyle::BallMeshLongitudeSegments; ++longitudeIndex)
			{
				const double longitude0 =
				    (static_cast<double>(longitudeIndex) * (2.0 * pi)) /
				    static_cast<double>(DebugWorldStyle::BallMeshLongitudeSegments);
				const double longitude1 =
				    (static_cast<double>(longitudeIndex + 1) * (2.0 * pi)) /
				    static_cast<double>(DebugWorldStyle::BallMeshLongitudeSegments);
				const double sinLongitude0 = std::sin(longitude0);
				const double sinLongitude1 = std::sin(longitude1);
				const double cosLongitude0 = std::cos(longitude0);
				const double cosLongitude1 = std::cos(longitude1);

				const Physics::Vector3 point00{
				    ballCenter.x + (ballRadiusMeters * cosLatitude0 * cosLongitude0),
				    ballCenter.y + (ballRadiusMeters * sinLatitude0),
				    ballCenter.z + (ballRadiusMeters * cosLatitude0 * sinLongitude0)};
				const Physics::Vector3 point01{
				    ballCenter.x + (ballRadiusMeters * cosLatitude0 * cosLongitude1),
				    ballCenter.y + (ballRadiusMeters * sinLatitude0),
				    ballCenter.z + (ballRadiusMeters * cosLatitude0 * sinLongitude1)};
				const Physics::Vector3 point10{
				    ballCenter.x + (ballRadiusMeters * cosLatitude1 * cosLongitude0),
				    ballCenter.y + (ballRadiusMeters * sinLatitude1),
				    ballCenter.z + (ballRadiusMeters * cosLatitude1 * sinLongitude0)};
				const Physics::Vector3 point11{
				    ballCenter.x + (ballRadiusMeters * cosLatitude1 * cosLongitude1),
				    ballCenter.y + (ballRadiusMeters * sinLatitude1),
				    ballCenter.z + (ballRadiusMeters * cosLatitude1 * sinLongitude1)};

				emitBallTriangle(point00, point01, point11);
				emitBallTriangle(point00, point11, point10);
			}
		}
	}

	enum class TrajectoryColorMode { Fading, Uniform, UniformOpaque };

	template <typename TDrawTriangleFn>
	void EmitProjectedBallAndTrajectoryMeshOverlay(const ViewProjectionState& projectionState,
	    const LONG width, const LONG height, const std::vector<Physics::Vector3>& trajectory,
	    const Physics::BallState& latestBallState, TDrawTriangleFn&& drawTriangle,
	    const float trailOpacityScale = 1.0F, const float staleFadeProgress = 0.0F,
	    const bool drawBall = true,
	    const TrajectoryColorMode colorMode = TrajectoryColorMode::Fading)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0))
		{
			return;
		}

		const float clampedTrailOpacityScale = std::clamp(trailOpacityScale, 0.0F, 1.50F);
		const float clampedStaleFadeProgress = std::clamp(staleFadeProgress, 0.0F, 1.0F);
		const std::size_t trajectoryCount = trajectory.size();
		if (trajectoryCount >= 2 && colorMode != TrajectoryColorMode::Fading)
		{
			static thread_local TrajectoryTubeMesh mesh;
			static thread_local TrajectoryTubeProjectionWorkspace projection;
			mesh.Build(trajectory, {0., 0., -(GetPitchDistanceMeters() +
			    DebugWorldLayout::BatterToPlateDistanceMeters)}, DebugWorldStyle::TrailMeshHalfWidthMeters);
			const std::array<float,4> color{DebugWorldStyle::TrailMeshColor[0],
			    DebugWorldStyle::TrailMeshColor[1], DebugWorldStyle::TrailMeshColor[2],
			    (colorMode == TrajectoryColorMode::UniformOpaque ? 1.F : DebugWorldStyle::TrailMeshColor[3]) *
			        std::clamp(clampedTrailOpacityScale, 0.F, 1.F)};
			if (color[3] > .001F)
				projection.Emit(mesh, projectionState, width, height, DebugWorldLayout::NearClipMeters,
				    DebugWorldLayout::FarClipMeters, color.data(), drawTriangle);
		}
		else if (trajectoryCount >= 2)
		{
			const Physics::Vector3 worldUpAxis{ 0.0, 1.0, 0.0 };
			const Physics::Vector3 worldRightAxis{ 1.0, 0.0, 0.0 };
			for (std::size_t index = 0; index < (trajectoryCount - 1); ++index)
			{
				const Physics::Vector3 startWorldPoint =
				    ConvertSimulationPositionToWorld(trajectory[index]);
				const Physics::Vector3 endWorldPoint =
				    ConvertSimulationPositionToWorld(trajectory[index + 1]);
				const Physics::Vector3 segmentDirection =
				    endWorldPoint - startWorldPoint;
				if (Physics::Length(segmentDirection) <= 1e-6)
				{
					continue;
				}

				const Physics::Vector3 segmentMidpoint =
				    0.5 * (startWorldPoint + endWorldPoint);
				const Physics::Vector3 segmentToCamera =
				    projectionState.position - segmentMidpoint;
				const double segmentDistanceMeters =
				    Physics::Length(segmentToCamera);
				Physics::Vector3 sideVector =
				    Physics::Cross(segmentDirection, segmentToCamera);
				if (Physics::Length(sideVector) <= 1e-6)
				{
					sideVector = Physics::Cross(segmentDirection, worldUpAxis);
				}
				if (Physics::Length(sideVector) <= 1e-6)
				{
					sideVector = Physics::Cross(segmentDirection, worldRightAxis);
				}

				const Physics::Vector3 normalizedSideVector =
				    Physics::NormalizeOrZero(sideVector);
				if (Physics::Length(normalizedSideVector) <= 1e-6)
				{
					continue;
				}

				const double indexBlend = static_cast<double>(index + 1) /
				                          static_cast<double>(trajectoryCount - 1);
				const double widthScale = std::clamp(
				    1.20 - (indexBlend * 0.55),
				    0.60,
				    1.20);
				const Physics::Vector3 ribbonOffset =
				    (DebugWorldStyle::TrailMeshHalfWidthMeters * widthScale) *
				    normalizedSideVector;
				const Physics::Vector3 startLeft = startWorldPoint + ribbonOffset;
				const Physics::Vector3 startRight = startWorldPoint - ribbonOffset;
				const Physics::Vector3 endLeft = endWorldPoint + ribbonOffset;
				const Physics::Vector3 endRight = endWorldPoint - ribbonOffset;

				const double agedBlend =
				    std::pow(std::clamp(indexBlend, 0.0, 1.0), 1.65);
				const float colorScale = static_cast<float>(std::clamp(
				    0.18 + (agedBlend * 0.82),
				    0.0,
				    1.0));
				const float alphaScale = static_cast<float>(std::clamp(
				    agedBlend,
				    0.0,
				    1.0));
				const float distanceFade = static_cast<float>(std::clamp(
				    1.00 - (segmentDistanceMeters / 36.0),
				    0.20,
				    1.0));
				float segmentTemporalFade = 1.0F;
				if (clampedStaleFadeProgress > 0.0F)
				{
					const double segmentOrder = std::clamp(indexBlend, 0.0, 1.0);
					const double visibilityThreshold =
					    static_cast<double>(clampedStaleFadeProgress);
					if (segmentOrder <= visibilityThreshold)
					{
						segmentTemporalFade = 0.0F;
					}
					else
					{
						const double remainingRange =
						    (std::max)(1e-6, 1.0 - visibilityThreshold);
						const double normalizedRemain = std::clamp(
						    (segmentOrder - visibilityThreshold) /
						        remainingRange,
						    0.0,
						    1.0);
						segmentTemporalFade = static_cast<float>(
						    std::pow(normalizedRemain, 0.60));
					}
				}
				const float finalAlpha = static_cast<float>(std::clamp(
				    alphaScale * distanceFade * clampedTrailOpacityScale *
				        segmentTemporalFade,
				    0.0F,
				    1.0F));
				if (finalAlpha <= 0.001F)
				{
					continue;
				}
				std::array<float, 4> ribbonColor{
					DebugWorldStyle::TrailMeshColor[0] * colorScale,
					DebugWorldStyle::TrailMeshColor[1] * colorScale,
					DebugWorldStyle::TrailMeshColor[2] * colorScale,
					DebugWorldStyle::TrailMeshColor[3] * finalAlpha
				};

				EmitProjectedWorldTriangle(
				    startLeft,
				    endLeft,
				    endRight,
				    projectionState,
				    width,
				    height,
				    ribbonColor.data(),
				    drawTriangle);
				EmitProjectedWorldTriangle(
				    startLeft,
				    endRight,
				    startRight,
				    projectionState,
				    width,
				    height,
				    ribbonColor.data(),
				    drawTriangle);
			}
		}

		if (!drawBall) return;
		const Physics::Vector3 ballCenter =
		    ConvertSimulationPositionToWorld(latestBallState.position);
		const Physics::Vector3 lightDirection =
		    Physics::NormalizeOrZero(Physics::Vector3{ -0.36, 0.82, -0.44 });
		const Physics::Vector3 fillLightDirection =
		    Physics::NormalizeOrZero(Physics::Vector3{ 0.42, 0.36, 0.84 });
		EmitBallWorldMesh(ballCenter, latestBallState.properties.radiusMeters,
		    [&](const Physics::Vector3& point0, const Physics::Vector3& point1,
		        const Physics::Vector3& point2)
		    {
			    const Physics::Vector3 triangleNormal =
			        Physics::Cross(point1 - point0, point2 - point0);
			    if (Physics::Length(triangleNormal) <= 1e-6)
			    {
				    return;
			    }

			    const Physics::Vector3 triangleCenter = (point0 + point1 + point2) / 3.0;
			    const Physics::Vector3 triangleToCamera = projectionState.position - triangleCenter;
			    const double facing = Physics::Dot(triangleNormal, triangleToCamera);
			    if (facing <= 0.0)
			    {
				    return;
			    }

			    const Physics::Vector3 normalizedNormal = Physics::NormalizeOrZero(triangleNormal);
			    const double diffuse =
			        std::clamp(Physics::Dot(normalizedNormal, lightDirection), 0.0, 1.0);
			    const double fillDiffuse =
			        std::clamp(Physics::Dot(normalizedNormal, fillLightDirection), 0.0, 1.0);
			    const float lightScale = static_cast<float>(
			        std::clamp(0.30 + (0.52 * diffuse) + (0.16 * fillDiffuse), 0.24, 0.96));
			    std::array<float, 4> ballColor{DebugWorldStyle::BallColor[0] * lightScale,
			        DebugWorldStyle::BallColor[1] * lightScale,
			        DebugWorldStyle::BallColor[2] * lightScale, DebugWorldStyle::BallColor[3]};
			    EmitProjectedWorldTriangle(point0, point1, point2, projectionState, width, height,
			        ballColor.data(), drawTriangle);
		    });
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedResultTagPanelOverlay(const ViewProjectionState& projectionState,
	    const LONG width, const LONG height, const std::wstring_view resultLabel,
	    const double resultScore, TDrawTriangleFn&& drawTriangle)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0))
		{
			return;
		}

		const double halfWidth = DebugWorldLayout::ResultPanelWidthMeters * 0.5;
		const double halfHeight = DebugWorldLayout::ResultPanelHeightMeters * 0.5;
		const double panelLeft = DebugWorldLayout::ResultPanelCenterXMeters - halfWidth;
		const double panelRight = DebugWorldLayout::ResultPanelCenterXMeters + halfWidth;
		const double panelTop = DebugWorldLayout::ResultPanelCenterYMeters + halfHeight;
		const double panelBottom = DebugWorldLayout::ResultPanelCenterYMeters - halfHeight;
		const double panelDepthBase = -DebugWorldLayout::ResultPanelDepthMeters;
		const double panelDepthForeground = panelDepthBase + 0.0025;
		const double panelDepthAccent = panelDepthForeground + 0.0025;

		auto drawPanelQuad = [&](const double left,
		                         const double top,
		                         const double right,
		                         const double bottom,
		                         const double depth,
		                         const float* const color) -> void
		{
			const Physics::Vector3 topLeft{ left, top, depth };
			const Physics::Vector3 topRight{ right, top, depth };
			const Physics::Vector3 bottomRight{ right, bottom, depth };
			const Physics::Vector3 bottomLeft{ left, bottom, depth };

			EmitProjectedWorldTriangle(
			    topLeft,
			    topRight,
			    bottomRight,
			    projectionState,
			    width,
			    height,
			    color,
			    drawTriangle);
			EmitProjectedWorldTriangle(
			    topLeft,
			    bottomRight,
			    bottomLeft,
			    projectionState,
			    width,
			    height,
			    color,
			    drawTriangle);
		};

		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelRight,
		    panelBottom,
		    panelDepthBase,
		    DebugWorldStyle::ResultPanelBackgroundColor.data());

		const double border = DebugWorldLayout::ResultPanelBorderThicknessMeters;
		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelRight,
		    panelTop - border,
		    panelDepthForeground,
		    DebugWorldStyle::ResultPanelBorderColor.data());
		drawPanelQuad(
		    panelLeft,
		    panelBottom + border,
		    panelRight,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::ResultPanelBorderColor.data());
		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelLeft + border,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::ResultPanelBorderColor.data());
		drawPanelQuad(
		    panelRight - border,
		    panelTop,
		    panelRight,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::ResultPanelBorderColor.data());

		auto resultLabelToIndex = [&](const std::wstring_view label) -> int
		{
			if (label == L"strike")
			{
				return 0;
			}
			if (label == L"ball")
			{
				return 1;
			}
			if (label == L"in_play")
			{
				return 2;
			}
			if (label == L"foul")
			{
				return 3;
			}

			return 4;
		};
		auto resultColorByIndex = [&](const int index) -> const std::array<float, 4>&
		{
			switch (index)
			{
				case 0:
					return DebugWorldStyle::ResultTagStrikeColor;
				case 1:
					return DebugWorldStyle::ResultTagBallColor;
				case 2:
					return DebugWorldStyle::ResultTagInPlayColor;
				case 3:
					return DebugWorldStyle::ResultTagFoulColor;
				default:
					return DebugWorldStyle::ResultTagPendingColor;
			}
		};

		const int activeResultIndex = resultLabelToIndex(resultLabel);
		const double panelInnerLeft = panelLeft + DebugWorldLayout::ResultPanelPaddingMeters;
		const double panelInnerRight = panelRight - DebugWorldLayout::ResultPanelPaddingMeters;
		const double chipHeight = DebugWorldLayout::ResultPanelChipHeightMeters;
		const double chipGap = DebugWorldLayout::ResultPanelChipGapMeters;
		const double chipTopStart = panelTop - DebugWorldLayout::ResultPanelPaddingMeters;

		for (int chipIndex = 0; chipIndex < 5; ++chipIndex)
		{
			const double chipTop =
			    chipTopStart - (static_cast<double>(chipIndex) * (chipHeight + chipGap));
			const double chipBottom = chipTop - chipHeight;
			const std::array<float, 4>& baseColor = resultColorByIndex(chipIndex);
			const bool isActiveChip = (chipIndex == activeResultIndex);
			const float colorScale = isActiveChip ? 1.0F : 0.46F;
			const std::array<float, 4> chipColor{
				baseColor[0] * colorScale,
				baseColor[1] * colorScale,
				baseColor[2] * colorScale,
				1.0F
			};

			drawPanelQuad(
			    panelInnerLeft,
			    chipTop,
			    panelInnerRight,
			    chipBottom,
			    panelDepthForeground,
			    chipColor.data());
		}

		const double scoreBarBottom = panelBottom + DebugWorldLayout::ResultPanelPaddingMeters;
		const double scoreBarTop =
		    scoreBarBottom + DebugWorldLayout::ResultPanelScoreBarHeightMeters;
		const double scoreBarLeft = panelInnerLeft;
		const double scoreBarRight = panelInnerRight;
		const double scoreCenterX = 0.5 * (scoreBarLeft + scoreBarRight);
		drawPanelQuad(
		    scoreBarLeft,
		    scoreBarTop,
		    scoreBarRight,
		    scoreBarBottom,
		    panelDepthForeground,
		    DebugWorldStyle::ResultScoreNeutralColor.data());
		drawPanelQuad(
		    scoreCenterX - DebugWorldLayout::ResultPanelCenterLineHalfWidthMeters,
		    scoreBarTop,
		    scoreCenterX + DebugWorldLayout::ResultPanelCenterLineHalfWidthMeters,
		    scoreBarBottom,
		    panelDepthAccent,
		    DebugWorldStyle::ResultScoreCenterLineColor.data());

		const double clampedScore = std::clamp(resultScore, -1.0, 1.0);
		if (clampedScore > 0.0)
		{
			const double fillRight =
			    scoreCenterX + (clampedScore * (scoreBarRight - scoreCenterX));
			drawPanelQuad(
			    scoreCenterX,
			    scoreBarTop,
			    fillRight,
			    scoreBarBottom,
			    panelDepthAccent,
			    DebugWorldStyle::ResultScorePositiveColor.data());
		}
		else if (clampedScore < 0.0)
		{
			const double fillLeft =
			    scoreCenterX - ((-clampedScore) * (scoreCenterX - scoreBarLeft));
			drawPanelQuad(
			    fillLeft,
			    scoreBarTop,
			    scoreCenterX,
			    scoreBarBottom,
			    panelDepthAccent,
			    DebugWorldStyle::ResultScoreNegativeColor.data());
		}
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedMainControlPanelOverlay(
	    const ViewProjectionState& projectionState,
	    const LONG width,
	    const LONG height,
	    const MainControlPanelLayout& panelLayout,
	    TDrawTriangleFn&& drawTriangle)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0))
		{
			return;
		}
		if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0) ||
		    (panelLayout.widthMeters <= 1e-6) ||
		    (panelLayout.heightMeters <= 1e-6))
		{
			return;
		}

		const double halfWidth = panelLayout.widthMeters * 0.5;
		const double halfHeight = panelLayout.heightMeters * 0.5;
		const double panelLeft = panelLayout.centerX - halfWidth;
		const double panelRight = panelLayout.centerX + halfWidth;
		const double panelTop = panelLayout.centerY + halfHeight;
		const double panelBottom = panelLayout.centerY - halfHeight;
		const double panelDepthBase = panelLayout.centerZ;
		const double panelDepthForeground = panelDepthBase + 0.0025;

		auto drawPanelQuad = [&](const double left,
		                         const double top,
		                         const double right,
		                         const double bottom,
		                         const double depth,
		                         const float* const color) -> void
		{
			const Physics::Vector3 topLeft{ left, top, depth };
			const Physics::Vector3 topRight{ right, top, depth };
			const Physics::Vector3 bottomRight{ right, bottom, depth };
			const Physics::Vector3 bottomLeft{ left, bottom, depth };

			EmitProjectedWorldTriangle(
			    topLeft,
			    topRight,
			    bottomRight,
			    projectionState,
			    width,
			    height,
			    color,
			    drawTriangle);
			EmitProjectedWorldTriangle(
			    topLeft,
			    bottomRight,
			    bottomLeft,
			    projectionState,
			    width,
			    height,
			    color,
			    drawTriangle);
		};

		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelRight,
		    panelBottom,
		    panelDepthBase,
		    DebugWorldStyle::MainControlPanelBackgroundColor.data());

		const double border = DebugWorldLayout::ResultPanelBorderThicknessMeters;
		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelRight,
		    panelTop - border,
		    panelDepthForeground,
		    DebugWorldStyle::MainControlPanelBorderColor.data());
		drawPanelQuad(
		    panelLeft,
		    panelBottom + border,
		    panelRight,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::MainControlPanelBorderColor.data());
		drawPanelQuad(
		    panelLeft,
		    panelTop,
		    panelLeft + border,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::MainControlPanelBorderColor.data());
		drawPanelQuad(
		    panelRight - border,
		    panelTop,
		    panelRight,
		    panelBottom,
		    panelDepthForeground,
		    DebugWorldStyle::MainControlPanelBorderColor.data());

		const double panelInnerLeft =
		    panelLeft + panelLayout.paddingMeters;
		const double panelInnerRight =
		    panelRight - panelLayout.paddingMeters;
		const double panelInnerTop =
		    panelTop - panelLayout.paddingMeters;
		const double panelInnerBottom =
		    panelBottom + panelLayout.paddingMeters;
		const int columnCount = panelLayout.columns;
		const int rowCount = panelLayout.rows;
		const double cellGap = panelLayout.cellGapMeters;
		if ((columnCount <= 0) || (rowCount <= 0))
		{
			return;
		}

		const double innerWidth = panelInnerRight - panelInnerLeft;
		const double innerHeight = panelInnerTop - panelInnerBottom;
		const double cellWidth =
		    (innerWidth - (cellGap * static_cast<double>(columnCount - 1))) /
		    static_cast<double>(columnCount);
		const double cellHeight =
		    (innerHeight - (cellGap * static_cast<double>(rowCount - 1))) /
		    static_cast<double>(rowCount);
		if ((cellWidth <= 0.0) || (cellHeight <= 0.0))
		{
			return;
		}

		const std::array<const std::array<float, 4>*, 4> rowColors{
			&DebugWorldStyle::MainControlPanelModeRowColor,
			&DebugWorldStyle::MainControlPanelPresetRowColor,
			&DebugWorldStyle::MainControlPanelOverlayRowColor,
			&DebugWorldStyle::MainControlPanelEnvironmentRowColor
		};

		for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
		{
			const double cellTop = panelInnerTop -
			                       (static_cast<double>(rowIndex) *
			                        (cellHeight + cellGap));
			const double cellBottom = cellTop - cellHeight;
			const std::array<float, 4>& baseColor =
			    *rowColors[std::min(rowIndex, static_cast<int>(rowColors.size()) - 1)];

			for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex)
			{
				const double cellLeft =
				    panelInnerLeft +
				    (static_cast<double>(columnIndex) * (cellWidth + cellGap));
				const double cellRight = cellLeft + cellWidth;
				const float brightnessScale = (columnIndex == 1) ? 1.0F : 0.82F;
				const std::array<float, 4> cellColor{
					baseColor[0] * brightnessScale,
					baseColor[1] * brightnessScale,
					baseColor[2] * brightnessScale,
					baseColor[3]
				};
				drawPanelQuad(
				    cellLeft,
				    cellTop,
				    cellRight,
				    cellBottom,
				    panelDepthForeground,
				    cellColor.data());
			}
		}
	}

	[[nodiscard]] inline bool TryResolveMainControlPanelCellIndex(
	    const MainControlPanelLayout& panelLayout,
	    const Physics::Vector3& panelHitPoint,
	    int* const cellIndex)
	{
		if (cellIndex == nullptr)
		{
			return false;
		}
		*cellIndex = -1;
		if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0))
		{
			return false;
		}

		const double halfWidth = panelLayout.widthMeters * 0.5;
		const double halfHeight = panelLayout.heightMeters * 0.5;
		const double panelLeft = panelLayout.centerX - halfWidth;
		const double panelRight = panelLayout.centerX + halfWidth;
		const double panelTop = panelLayout.centerY + halfHeight;
		const double panelBottom = panelLayout.centerY - halfHeight;
		const double panelInnerLeft = panelLeft + panelLayout.paddingMeters;
		const double panelInnerRight = panelRight - panelLayout.paddingMeters;
		const double panelInnerTop = panelTop - panelLayout.paddingMeters;
		const double panelInnerBottom = panelBottom + panelLayout.paddingMeters;
		if ((panelHitPoint.x < panelInnerLeft) ||
		    (panelHitPoint.x > panelInnerRight) ||
		    (panelHitPoint.y > panelInnerTop) ||
		    (panelHitPoint.y < panelInnerBottom))
		{
			return false;
		}

		const double cellWidth =
		    (panelInnerRight - panelInnerLeft -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.columns - 1))) /
		    static_cast<double>(panelLayout.columns);
		const double cellHeight =
		    (panelInnerTop - panelInnerBottom -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.rows - 1))) /
		    static_cast<double>(panelLayout.rows);
		if ((cellWidth <= 0.0) || (cellHeight <= 0.0))
		{
			return false;
		}

		for (int rowIndex = 0; rowIndex < panelLayout.rows; ++rowIndex)
		{
			const double cellTop = panelInnerTop -
			                       (static_cast<double>(rowIndex) *
			                        (cellHeight + panelLayout.cellGapMeters));
			const double cellBottom = cellTop - cellHeight;
			for (int columnIndex = 0; columnIndex < panelLayout.columns; ++columnIndex)
			{
				const double cellLeft =
				    panelInnerLeft +
				    (static_cast<double>(columnIndex) *
				     (cellWidth + panelLayout.cellGapMeters));
				const double cellRight = cellLeft + cellWidth;
				if ((panelHitPoint.x >= cellLeft) &&
				    (panelHitPoint.x <= cellRight) &&
				    (panelHitPoint.y <= cellTop) &&
				    (panelHitPoint.y >= cellBottom))
				{
					*cellIndex = (rowIndex * panelLayout.columns) + columnIndex;
					return true;
				}
			}
		}

		return false;
	}

	template <typename TDrawLineFn>
	void EmitProjectedMainControlPanelLabelOverlay(
	    const ViewProjectionState& projectionState,
	    const LONG width,
	    const LONG height,
	    const MainControlPanelLayout& panelLayout,
	    TDrawLineFn&& drawLine)
	{
		if (!projectionState.isValid || (width <= 0) || (height <= 0))
		{
			return;
		}
		if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0))
		{
			return;
		}

		const double halfWidth = panelLayout.widthMeters * 0.5;
		const double halfHeight = panelLayout.heightMeters * 0.5;
		const double panelLeft = panelLayout.centerX - halfWidth;
		const double panelRight = panelLayout.centerX + halfWidth;
		const double panelTop = panelLayout.centerY + halfHeight;
		const double panelBottom = panelLayout.centerY - halfHeight;
		const double panelInnerLeft = panelLeft + panelLayout.paddingMeters;
		const double panelInnerRight = panelRight - panelLayout.paddingMeters;
		const double panelInnerTop = panelTop - panelLayout.paddingMeters;
		const double panelInnerBottom = panelBottom + panelLayout.paddingMeters;
		const double cellWidth =
		    (panelInnerRight - panelInnerLeft -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.columns - 1))) /
		    static_cast<double>(panelLayout.columns);
		const double cellHeight =
		    (panelInnerTop - panelInnerBottom -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.rows - 1))) /
		    static_cast<double>(panelLayout.rows);
		if ((cellWidth <= 0.0) || (cellHeight <= 0.0))
		{
			return;
		}

		auto emitProjectedWorldLine = [&](const Physics::Vector3& worldStart,
		                                  const Physics::Vector3& worldEnd) -> void
		{
			ForEachProjectedLineSegment(
			    worldStart,
			    worldEnd,
			    DebugWorldLayout::FieldGridSampleStepMeters,
			    [&](const Physics::Vector3& worldPoint, ScreenPoint* const projectedPoint) -> bool
			    {
				    if (projectedPoint == nullptr)
				    {
					    return false;
				    }
				    return TryProjectWorldToScreen(
				        worldPoint,
				        projectionState,
				        width,
				        height,
				        projectedPoint,
				        false);
			    },
			    [&](const ScreenPoint& projectedStart,
			        const ScreenPoint& projectedEnd) -> void
			    {
				    drawLine(
				        projectedStart,
				        projectedEnd,
				        1,
				        DebugWorldStyle::MainControlPanelLabelColor.data());
			    });
		};

		auto emitDigit = [&](const int digit,
		                     const double centerX,
		                     const double centerY,
		                     const double depth) -> void
		{
			if ((digit < 0) || (digit > 9))
			{
				return;
			}

			static constexpr std::array<std::uint8_t, 10> SegmentMasks{
				0x3FU, // 0
				0x06U, // 1
				0x5BU, // 2
				0x4FU, // 3
				0x66U, // 4
				0x6DU, // 5
				0x7DU, // 6
				0x07U, // 7
				0x7FU, // 8
				0x6FU  // 9
			};

			const double digitWidth = cellWidth * 0.28;
			const double digitHeight = cellHeight * 0.52;
			const double halfDigitWidth = digitWidth * 0.5;
			const double halfDigitHeight = digitHeight * 0.5;
			const double midY = centerY;
			const double topY = centerY + halfDigitHeight;
			const double bottomY = centerY - halfDigitHeight;
			const double leftX = centerX - halfDigitWidth;
			const double rightX = centerX + halfDigitWidth;

			auto emitSegment = [&](const int segmentIndex,
			                       const Physics::Vector3& segmentStart,
			                       const Physics::Vector3& segmentEnd) -> void
			{
				const std::uint8_t mask = SegmentMasks[static_cast<std::size_t>(digit)];
				if ((mask & (1U << segmentIndex)) == 0U)
				{
					return;
				}
				emitProjectedWorldLine(segmentStart, segmentEnd);
			};

			emitSegment(
			    0,
			    Physics::Vector3{ leftX, topY, depth },
			    Physics::Vector3{ rightX, topY, depth });
			emitSegment(
			    1,
			    Physics::Vector3{ rightX, topY, depth },
			    Physics::Vector3{ rightX, midY, depth });
			emitSegment(
			    2,
			    Physics::Vector3{ rightX, midY, depth },
			    Physics::Vector3{ rightX, bottomY, depth });
			emitSegment(
			    3,
			    Physics::Vector3{ leftX, bottomY, depth },
			    Physics::Vector3{ rightX, bottomY, depth });
			emitSegment(
			    4,
			    Physics::Vector3{ leftX, midY, depth },
			    Physics::Vector3{ leftX, bottomY, depth });
			emitSegment(
			    5,
			    Physics::Vector3{ leftX, topY, depth },
			    Physics::Vector3{ leftX, midY, depth });
			emitSegment(
			    6,
			    Physics::Vector3{ leftX, midY, depth },
			    Physics::Vector3{ rightX, midY, depth });
		};

		int labelNumber = 1;
		for (int rowIndex = 0; rowIndex < panelLayout.rows; ++rowIndex)
		{
			const double cellTop = panelInnerTop -
			                       (static_cast<double>(rowIndex) *
			                        (cellHeight + panelLayout.cellGapMeters));
			const double cellBottom = cellTop - cellHeight;
			for (int columnIndex = 0; columnIndex < panelLayout.columns; ++columnIndex)
			{
				const double cellLeft =
				    panelInnerLeft +
				    (static_cast<double>(columnIndex) *
				     (cellWidth + panelLayout.cellGapMeters));
				const double cellRight = cellLeft + cellWidth;
				const double cellCenterX = (cellLeft + cellRight) * 0.5;
				const double cellCenterY = (cellTop + cellBottom) * 0.5;
				const double labelDepth = panelLayout.centerZ + 0.0035;
				if (labelNumber >= 10)
				{
					emitDigit(
					    labelNumber / 10,
					    cellCenterX - (cellWidth * 0.16),
					    cellCenterY,
					    labelDepth);
					emitDigit(
					    labelNumber % 10,
					    cellCenterX + (cellWidth * 0.16),
					    cellCenterY,
					    labelDepth);
				}
				else
				{
					emitDigit(labelNumber, cellCenterX, cellCenterY, labelDepth);
				}
				++labelNumber;
			}
		}
	}

	[[nodiscard]] inline IndexedWorldMesh BuildFloorWorldMesh(
	    double strikeZoneWidthMeters, double fanRadiusMeters)
	{
		IndexedWorldMesh mesh;
		strikeZoneWidthMeters = (std::max)(.20, strikeZoneWidthMeters);
		const double targetChordLengthMeters = strikeZoneWidthMeters + 2.0;
		const double plateDepthFromCenterMeters =
		    (std::max)(.10, std::abs(DebugWorldLayout::BatterToPlateDistanceMeters));
		const double halfChordLengthMeters = targetChordLengthMeters * .5;
		const double halfAngleRadians =
		    std::atan2(halfChordLengthMeters, plateDepthFromCenterMeters);
		const double innerRadiusMeters =
		    std::sqrt(plateDepthFromCenterMeters * plateDepthFromCenterMeters +
		              halfChordLengthMeters * halfChordLengthMeters);
		const double outerRadiusMeters = (std::max)(innerRadiusMeters + 1.0, fanRadiusMeters);
		if (halfAngleRadians <= 1e-6 || innerRadiusMeters <= 1e-6 ||
		    outerRadiusMeters <= innerRadiusMeters)
		{
			return mesh;
		}
		const double tileStepMeters =
		    (std::max)(DebugWorldStyle::FloorMeshTileSizeMeters * 2.0, .50);
		const int radialTileCount = std::clamp(
		    static_cast<int>(std::ceil((outerRadiusMeters - innerRadiusMeters) / tileStepMeters)),
		    1, 120);
		const int angularTileCount =
		    std::clamp(static_cast<int>(
		                   std::ceil(halfAngleRadians * 2.0 * outerRadiusMeters / tileStepMeters)),
		        16, 180);
		const double angularSpanRadians = halfAngleRadians * 2.0;
		std::vector<std::array<double, 2>> angles;
		angles.reserve(static_cast<std::size_t>(angularTileCount) + 1);
		for (int angleIndex = 0; angleIndex <= angularTileCount; ++angleIndex)
		{
			const double angle =
			    -halfAngleRadians +
			    angularSpanRadians * (static_cast<double>(angleIndex) / angularTileCount);
			angles.push_back({std::sin(angle), std::cos(angle)});
		}
		mesh.vertices.reserve(
		    static_cast<std::size_t>(radialTileCount + 1) * (angularTileCount + 1));
		mesh.triangles.reserve(static_cast<std::size_t>(radialTileCount) * angularTileCount * 2);
		for (int radialIndex = 0; radialIndex <= radialTileCount; ++radialIndex)
		{
			const double radius =
			    innerRadiusMeters + (outerRadiusMeters - innerRadiusMeters) *
			                            (static_cast<double>(radialIndex) / radialTileCount);
			for (const auto& angle : angles)
			{
				mesh.vertices.push_back({angle[0] * radius, DebugWorldStyle::FloorMeshHeightMeters,
				    -angle[1] * radius});
			}
		}
		for (int radialIndex = 0; radialIndex < radialTileCount; ++radialIndex)
		{
			for (int angleIndex = 0; angleIndex < angularTileCount; ++angleIndex)
			{
				const auto& tileColor = (radialIndex + angleIndex) % 2 == 0
				                            ? DebugWorldStyle::FloorMeshTileColorA
				                            : DebugWorldStyle::FloorMeshTileColorB;
				const double radialBlend =
				    radialTileCount > 1 ? static_cast<double>(radialIndex) / (radialTileCount - 1)
				                        : 0.0;
				const double angleBlend =
				    std::abs((static_cast<double>(angleIndex) + .5) / angularTileCount - .5) * 2.0;
				const float brightnessScale = static_cast<float>(
				    std::clamp(.94 - radialBlend * .26 - angleBlend * .08, .55, 1.0));
				const std::array<float, 4> color{tileColor[0] * brightnessScale,
				    tileColor[1] * brightnessScale, tileColor[2] * brightnessScale, tileColor[3]};
				const auto topLeft =
				    static_cast<std::uint32_t>(radialIndex * (angularTileCount + 1) + angleIndex);
				const auto topRight = topLeft + 1,
				           bottomLeft = topLeft + static_cast<std::uint32_t>(angularTileCount + 1),
				           bottomRight = bottomLeft + 1;
				mesh.triangles.push_back({{topLeft, topRight, bottomRight}, color});
				mesh.triangles.push_back({{topLeft, bottomRight, bottomLeft}, color});
			}
		}
		return mesh;
	}
	class FloorWorldMeshCache final
	{
	  public:
		[[nodiscard]] const IndexedWorldMesh& Get(
		    double strikeZoneWidthMeters, double fanRadiusMeters)
		{
			// One current configuration bounds memory even while settings are edited.
			if (!m_valid || m_width != strikeZoneWidthMeters || m_radius != fanRadiusMeters)
			{
				m_mesh = BuildFloorWorldMesh(strikeZoneWidthMeters, fanRadiusMeters);
				m_width = strikeZoneWidthMeters;
				m_radius = fanRadiusMeters;
				m_valid = true;
				++m_buildCount;
			}
			return m_mesh;
		}
		[[nodiscard]] std::uint64_t BuildCount() const noexcept
		{
			return m_buildCount;
		}

	  private:
		IndexedWorldMesh m_mesh;
		double m_width{}, m_radius{};
		bool m_valid{};
		std::uint64_t m_buildCount{};
	};
	[[nodiscard]] inline const IndexedWorldMesh& GetFloorWorldMesh(
	    double strikeZoneWidthMeters, double fanRadiusMeters)
	{
		static thread_local FloorWorldMeshCache cache;
		return cache.Get(strikeZoneWidthMeters, fanRadiusMeters);
	}
	template <typename TDrawTriangleFn>
	void EmitProjectedFloorMeshOverlay(
	    const ViewProjectionState& view, LONG width, LONG height, TDrawTriangleFn&& triangle)
	{
		if (!view.isValid || width <= 0 || height <= 0)
		{
			return;
		}
		static thread_local WorldMeshProjectionWorkspace workspace;
		const auto& mesh =
		    GetFloorWorldMesh(GetStrikeZoneHalfWidthMeters() * 2.0, GetGroundFanRadiusMeters());
		workspace.Emit(mesh,
		    PreparedWorldProjection(view, width, height, DebugWorldLayout::NearClipMeters,
		        DebugWorldLayout::FarClipMeters),
		    triangle);
	}
} // namespace BaseballSimulatorVR::Common
