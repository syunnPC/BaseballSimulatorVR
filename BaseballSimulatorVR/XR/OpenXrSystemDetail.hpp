#pragma once

// Helpers shared by the OpenXrSystem*.cpp translation units. Everything here is
// internal to the OpenXR backend; helpers used by a single unit stay in that
// unit's anonymous namespace instead.

#include "OpenXrSystem.hpp"
#include "../Common/D3d12DebugOverlay.hpp"
#include "../Common/DebugWorldProjection.hpp"
#include "../Common/FieldPresentationOverlay.hpp"
#include "../Common/UiTextRasterization.hpp"
#include "../Common/UiTextAtlas.hpp"
#include "../Common/TransparentGeometry.hpp"
#include "../Common/BatPoseUtilities.hpp"
#include "../Common/FxaaPipelineUtilities.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/PhaseTrajectoryOverlay.hpp"
#include "../Common/StrikeZoneCalibrationUi.hpp"
#include "../Common/MetricsPanelControls.hpp"
#include "../Common/PitchPresetUi.hpp"
#include "../Common/UiPointerInput.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BaseballSimulatorVR::Xr::Detail
{
	inline constexpr XrViewConfigurationType PrimaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	// Both eyes clear to the same sky color; different colors per eye cause
	// binocular rivalry wherever the sky shows.
	inline constexpr std::array<float, 4> LeftEyeClearColor{ 0.02F, 0.03F, 0.08F, 1.0F };

	inline constexpr UINT MaxDebugVertices = 262144;

#if defined(_WIN64)
	[[nodiscard]] inline std::wstring BuildXrErrorMessage(
	    const std::wstring_view phase,
	    const XrResult result)
	{
		return std::wstring{ phase } + L" failed(" +
		       std::to_wstring(static_cast<long long>(result)) + L")";
	}

	inline void LogXrCleanupFailure(
	    const std::wstring_view operationName,
	    const XrResult result)
	{
		if (XR_SUCCEEDED(result))
		{
			return;
		}

		const std::wstring message =
		    L"[OpenXR] " + std::wstring{ operationName } + L" failed(" +
		    std::to_wstring(static_cast<long long>(result)) + L")\n";
		OutputDebugStringW(message.c_str());
	}

	[[nodiscard]] inline std::wstring BuildD3d12ErrorMessage(
	    const std::wstring_view phase,
	    const HRESULT result)
	{
		return std::wstring{ phase } + L" failed(0x" +
		       std::to_wstring(static_cast<unsigned long long>(static_cast<unsigned long>(result))) + L")";
	}

	[[nodiscard]] inline DXGI_FORMAT ResolveRtvFormat(const DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			default:
				return format;
		}
	}

	[[nodiscard]] inline Common::ViewProjectionState BuildProjectionStateFromView(const XrView& view)
	{
		Common::ViewProjectionState projectionState{};
		projectionState.isValid = true;
		projectionState.position = Physics::Vector3{
			static_cast<double>(view.pose.position.x),
			static_cast<double>(view.pose.position.y),
			static_cast<double>(view.pose.position.z)
		};
		projectionState.orientation = Physics::Vector3{
			static_cast<double>(view.pose.orientation.x),
			static_cast<double>(view.pose.orientation.y),
			static_cast<double>(view.pose.orientation.z)
		};
		projectionState.orientationW = static_cast<double>(view.pose.orientation.w);
		projectionState.fovLeftRadians = static_cast<double>(view.fov.angleLeft);
		projectionState.fovRightRadians = static_cast<double>(view.fov.angleRight);
		projectionState.fovUpRadians = static_cast<double>(view.fov.angleUp);
		projectionState.fovDownRadians = static_cast<double>(view.fov.angleDown);
		return projectionState;
	}

	[[nodiscard]] inline Physics::Vector3 RotateVectorByQuaternion(
	    const Physics::Vector3& vector,
	    const XrQuaternionf& quaternion)
	{
		const Physics::Vector3 qVector{
			static_cast<double>(quaternion.x),
			static_cast<double>(quaternion.y),
			static_cast<double>(quaternion.z)
		};
		const double qw = static_cast<double>(quaternion.w);
		const Physics::Vector3 uv = Physics::Cross(qVector, vector);
		const Physics::Vector3 uuv = Physics::Cross(qVector, uv);
		return vector + (2.0 * qw * uv) + (2.0 * uuv);
	}

	[[nodiscard]] inline XrQuaternionf NormalizeQuaternion(
	    const XrQuaternionf& quaternion)
	{
		const double lengthSquared =
		    (static_cast<double>(quaternion.x) *
		     static_cast<double>(quaternion.x)) +
		    (static_cast<double>(quaternion.y) *
		     static_cast<double>(quaternion.y)) +
		    (static_cast<double>(quaternion.z) *
		     static_cast<double>(quaternion.z)) +
		    (static_cast<double>(quaternion.w) *
		     static_cast<double>(quaternion.w));
		if (lengthSquared <= 1e-12)
		{
			return XrQuaternionf{ 0.0F, 0.0F, 0.0F, 1.0F };
		}

		const float inverseLength = static_cast<float>(
		    1.0 / std::sqrt(lengthSquared));
		return XrQuaternionf{
			quaternion.x * inverseLength,
			quaternion.y * inverseLength,
			quaternion.z * inverseLength,
			quaternion.w * inverseLength
		};
	}

	using OrientedUiPanelLayout=Common::UiText::PanelLayout;

	struct UiPanelInnerLayout final
	{
		double innerLeft{ 0.0 };
		double innerRight{ 0.0 };
		double innerTop{ 0.0 };
		double innerBottom{ 0.0 };
		double cellWidth{ 0.0 };
		double cellHeight{ 0.0 };
	};

	using UiPanelLocalRect=Common::UiText::PanelLocalRect;

	inline void BuildUiPanelAxesFromForward(
	    const Physics::Vector3& forwardDirection,
	    Physics::Vector3* const rightAxis,
	    Physics::Vector3* const upAxis,
	    Physics::Vector3* const forwardAxis)
	{
		if ((rightAxis == nullptr) || (upAxis == nullptr) || (forwardAxis == nullptr))
		{
			return;
		}

		Physics::Vector3 resolvedForward = Physics::NormalizeOrZero(forwardDirection);
		if (Physics::Length(resolvedForward) <= 1e-6)
		{
			resolvedForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
		}

		const Physics::Vector3 worldUp{ 0.0, 1.0, 0.0 };
		Physics::Vector3 resolvedRight =
		    Physics::NormalizeOrZero(Physics::Cross(worldUp, resolvedForward));
		if (Physics::Length(resolvedRight) <= 1e-6)
		{
			resolvedRight = Physics::Vector3{ 1.0, 0.0, 0.0 };
		}

		Physics::Vector3 resolvedUp = Physics::NormalizeOrZero(
		    Physics::Cross(resolvedForward, resolvedRight));
		if (Physics::Length(resolvedUp) <= 1e-6)
		{
			resolvedUp = worldUp;
		}

		*rightAxis = resolvedRight;
		*upAxis = resolvedUp;
		*forwardAxis = resolvedForward;
	}

	[[nodiscard]] inline bool TryBuildPanelInnerLayout(
	    const OrientedUiPanelLayout& panelLayout,
	    UiPanelInnerLayout* const innerLayout)
	{
		if (innerLayout == nullptr)
		{
			return false;
		}
		if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0))
		{
			return false;
		}

		const double halfWidth = panelLayout.widthMeters * 0.5;
		const double halfHeight = panelLayout.heightMeters * 0.5;
		const double panelLeft = -halfWidth;
		const double panelRight = halfWidth;
		const double panelTop = halfHeight;
		const double panelBottom = -halfHeight;
		innerLayout->innerLeft = panelLeft + panelLayout.paddingMeters;
		innerLayout->innerRight = panelRight - panelLayout.paddingMeters;
		innerLayout->innerTop = panelTop - panelLayout.paddingMeters;
		innerLayout->innerBottom = panelBottom + panelLayout.paddingMeters;
		innerLayout->cellWidth =
		    (innerLayout->innerRight - innerLayout->innerLeft -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.columns - 1))) /
		    static_cast<double>(panelLayout.columns);
		innerLayout->cellHeight =
		    (innerLayout->innerTop - innerLayout->innerBottom -
		     (panelLayout.cellGapMeters *
		      static_cast<double>(panelLayout.rows - 1))) /
		    static_cast<double>(panelLayout.rows);
		if ((innerLayout->cellWidth <= 0.0) || (innerLayout->cellHeight <= 0.0))
		{
			return false;
		}

		return true;
	}

	[[nodiscard]] inline bool TryBuildPitchPlaybackSliderRects(
	    const OrientedUiPanelLayout& panelLayout,
	    const int firstSliderCellIndex,
	    const int lastSliderCellIndex,
	    UiPanelLocalRect* const trackRect,
	    UiPanelLocalRect* const hitRect)
	{
		if ((trackRect == nullptr) || (hitRect == nullptr))
		{
			return false;
		}

		double firstLeft = 0.0;
		double firstTop = 0.0;
		double firstRight = 0.0;
		double firstBottom = 0.0;
		double lastLeft = 0.0;
		double lastTop = 0.0;
		double lastRight = 0.0;
		double lastBottom = 0.0;
		UiPanelInnerLayout innerLayout{};
		if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
		{
			return false;
		}
		const auto resolveCellBounds = [&](const int cellIndex,
		                                   double* const outLeft,
		                                   double* const outTop,
		                                   double* const outRight,
		                                   double* const outBottom) -> bool
		{
			if ((outLeft == nullptr) || (outTop == nullptr) ||
			    (outRight == nullptr) || (outBottom == nullptr))
			{
				return false;
			}
			if ((cellIndex < 0) || (panelLayout.columns <= 0) ||
			    (panelLayout.rows <= 0))
			{
				return false;
			}
			const int maxCellCount = panelLayout.columns * panelLayout.rows;
			if (cellIndex >= maxCellCount)
			{
				return false;
			}

			const int rowIndex = cellIndex / panelLayout.columns;
			const int columnIndex = cellIndex % panelLayout.columns;
			const double cellTop =
			    innerLayout.innerTop -
			    (static_cast<double>(rowIndex) *
			     (innerLayout.cellHeight + panelLayout.cellGapMeters));
			const double cellBottom = cellTop - innerLayout.cellHeight;
			const double cellLeft =
			    innerLayout.innerLeft +
			    (static_cast<double>(columnIndex) *
			     (innerLayout.cellWidth + panelLayout.cellGapMeters));
			const double cellRight = cellLeft + innerLayout.cellWidth;
			*outLeft = cellLeft;
			*outTop = cellTop;
			*outRight = cellRight;
			*outBottom = cellBottom;
			return true;
		};
		if (!resolveCellBounds(
		        firstSliderCellIndex,
		        &firstLeft,
		        &firstTop,
		        &firstRight,
		        &firstBottom) ||
		    !resolveCellBounds(
		        lastSliderCellIndex,
		        &lastLeft,
		        &lastTop,
		        &lastRight,
		        &lastBottom))
		{
			return false;
		}

		hitRect->left = firstLeft;
		hitRect->top = (std::max)(firstTop, lastTop);
		hitRect->right = lastRight;
		hitRect->bottom = (std::min)(firstBottom, lastBottom);
		const double sliderCellWidth = firstRight - firstLeft;
		const double sliderCellHeight = firstTop - firstBottom;
		const double trackInsetX =
		    std::clamp(sliderCellWidth * 0.16, 0.002, sliderCellWidth * 0.36);
		const double trackHalfHeight = std::clamp(
		    sliderCellHeight * 0.12,
		    0.0015,
		    sliderCellHeight * 0.24);
		const double trackCenterY = (hitRect->top + hitRect->bottom) * 0.5;
		trackRect->left = hitRect->left + trackInsetX;
		trackRect->right = hitRect->right - trackInsetX;
		trackRect->top = trackCenterY + trackHalfHeight;
		trackRect->bottom = trackCenterY - trackHalfHeight;
		return (trackRect->right > trackRect->left) &&
		       (trackRect->top > trackRect->bottom);
	}

	inline constexpr std::size_t MetricsControlButtonCount = Common::MetricsControlButtonCount;
	inline constexpr int BatMaterialControlButtonIndex = 12;

	[[nodiscard]] inline bool TryBuildMetricsPanelBodyLayout(
	    const OrientedUiPanelLayout& panelLayout,
	    UiPanelLocalRect* const bodyRect,
	    std::array<UiPanelLocalRect, MetricsControlButtonCount>* const controlButtonRects)
	{
		if ((bodyRect == nullptr) || (controlButtonRects == nullptr))
		{
			return false;
		}

		return Common::BuildMetricsPanelControlLayout(panelLayout, *bodyRect, *controlButtonRects);
	}

	inline constexpr double UiMainPanelAnchorDistanceMeters = 0.008;
	inline constexpr double UiMainPanelAnchorDownOffsetMeters = -0.002;
	inline constexpr double UiMainPanelMinDistanceFromHeadMeters = 0.16;
	inline constexpr double UiPanelArcMinYawRadians = -1.35;
	inline constexpr double UiPanelArcMaxYawRadians = 1.35;
	inline constexpr double DefaultPitchPanelArcYawRadians = -0.30;
	inline constexpr double DefaultPitchPanelArcRadiusMeters = 0.72;
	inline constexpr double DefaultPitchPanelHeightOffsetMeters = -0.18;
	inline constexpr double DefaultPitchMetricsPanelArcYawRadians = 0.16;
	inline constexpr double DefaultPitchMetricsPanelArcRadiusMeters = 0.68;
	inline constexpr double DefaultPitchMetricsPanelHeightOffsetMeters = 0.00;
	inline constexpr double DefaultBattingMetricsPanelArcYawRadians = 0.50;
	inline constexpr double DefaultBattingMetricsPanelArcRadiusMeters = 0.72;
	inline constexpr double DefaultBattingMetricsPanelHeightOffsetMeters = -0.10;
	inline constexpr double DefaultMetricsPanelArcYawRadians =
	    DefaultPitchMetricsPanelArcYawRadians;
	inline constexpr double DefaultMetricsPanelArcRadiusMeters =
	    DefaultPitchMetricsPanelArcRadiusMeters;
	inline constexpr double DefaultMetricsPanelHeightOffsetMeters =
	    DefaultPitchMetricsPanelHeightOffsetMeters;
	inline constexpr double MoveSpeedMetersPerSecond = 2.00;
	inline constexpr double ViewYawSpeedRadiansPerSecond = 1.85;
	inline constexpr double MaxLocomotionDeltaSeconds = 0.10;
	inline constexpr int PitchZoneColumns = 5;
	inline constexpr int PitchZoneRows = 5;
	inline constexpr int PitchZoneCellCount = PitchZoneColumns * PitchZoneRows;
	inline constexpr int PitchTitleCellCount = 5;
	inline constexpr int PitchZoneCellStartIndex = PitchTitleCellCount;
	inline constexpr int PitchTypePreviousCellIndex =
	    PitchZoneCellStartIndex + PitchZoneCellCount;
	inline constexpr int PitchTypeNameCellIndex = PitchTypePreviousCellIndex + 1;
	inline constexpr int PitchTypeNextCellIndex = PitchTypePreviousCellIndex + 2;
	inline constexpr int PitchPlaybackToggleCellIndex =
	    PitchTypePreviousCellIndex + 3;
	inline constexpr int PitchRandomPitchModeCellIndex =
	    PitchTypePreviousCellIndex + 4;
	inline constexpr int PitchPlaybackSliderCellStartIndex =
	    PitchRandomPitchModeCellIndex + 1;
	inline constexpr int PitchPlaybackSliderCellCount = 4;
	inline constexpr int PitchZoneRandomCellIndex =
	    PitchPlaybackSliderCellStartIndex + PitchPlaybackSliderCellCount;
	inline constexpr int PitchSettingsPreviousCellIndex =
	    PitchZoneRandomCellIndex + 1;
	inline constexpr int PitchSettingsNextCellIndex =
	    PitchSettingsPreviousCellIndex + 2;

	[[nodiscard]] inline Physics::Vector3 ResolveHorizontalForward(
	    const Physics::Vector3& forwardDirection,
	    const Physics::Vector3& fallbackDirection)
	{
		Physics::Vector3 horizontalForward{
			forwardDirection.x,
			0.0,
			forwardDirection.z
		};
		horizontalForward = Physics::NormalizeOrZero(horizontalForward);
		if (Physics::Length(horizontalForward) > 1e-6)
		{
			return horizontalForward;
		}

		Physics::Vector3 fallbackHorizontal{
			fallbackDirection.x,
			0.0,
			fallbackDirection.z
		};
		fallbackHorizontal = Physics::NormalizeOrZero(fallbackHorizontal);
		if (Physics::Length(fallbackHorizontal) <= 1e-6)
		{
			fallbackHorizontal = Physics::Vector3{ 0.0, 0.0, -1.0 };
		}
		return fallbackHorizontal;
	}

	[[nodiscard]] inline Physics::Vector3 ResolvePanelFacingForward(
	    const Physics::Vector3& forwardDirection,
	    const Physics::Vector3& fallbackDirection)
	{
		Physics::Vector3 resolvedForward = Physics::NormalizeOrZero(forwardDirection);
		if (Physics::Length(resolvedForward) > 1e-6)
		{
			return resolvedForward;
		}

		resolvedForward = Physics::NormalizeOrZero(fallbackDirection);
		if (Physics::Length(resolvedForward) <= 1e-6)
		{
			resolvedForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		}
		return resolvedForward;
	}

	[[nodiscard]] inline Physics::Vector3 ComputeHorizontalArcForwardFromHeadView(
	    const Common::ViewProjectionState& viewState,
	    const Physics::Vector3& fallbackForward)
	{
		Physics::Vector3 panelArcForward = fallbackForward;
		if (viewState.isValid)
		{
			const XrQuaternionf headOrientation{
				static_cast<float>(viewState.orientation.x),
				static_cast<float>(viewState.orientation.y),
				static_cast<float>(viewState.orientation.z),
				static_cast<float>(viewState.orientationW)
			};
			panelArcForward = RotateVectorByQuaternion(
			    Physics::Vector3{ 0.0, 0.0, -1.0 },
			    headOrientation);
		}
		return ResolveHorizontalForward(panelArcForward, fallbackForward);
	}

	[[nodiscard]] inline OrientedUiPanelLayout BuildMainPanelLayout(
	    const Physics::Vector3& center,
	    const Physics::Vector3& rightAxis,
	    const Physics::Vector3& upAxis,
	    const Physics::Vector3& forwardAxis)
	{
		OrientedUiPanelLayout layout{};
		layout.center = center;
		layout.rightAxis = rightAxis;
		layout.upAxis = upAxis;
		layout.forwardAxis = forwardAxis;
		layout.widthMeters = 0.36;
		layout.heightMeters = 0.236;
		layout.paddingMeters = 0.010;
		layout.cellGapMeters = 0.004;
		layout.columns = Common::CalibrationMainPanelColumns;
		layout.rows = Common::CalibrationMainPanelRows;
		return layout;
	}

	[[nodiscard]] inline OrientedUiPanelLayout BuildPitchPanelLayout(
	    const Physics::Vector3& center,
	    const Physics::Vector3& rightAxis,
	    const Physics::Vector3& upAxis,
	    const Physics::Vector3& forwardAxis,
	    const bool minimized)
	{
		OrientedUiPanelLayout layout{};
		layout.center = center;
		layout.rightAxis = rightAxis;
		layout.upAxis = upAxis;
		layout.forwardAxis = forwardAxis;
		if (minimized)
		{
			layout.widthMeters = 0.24;
			layout.heightMeters = 0.048;
			layout.paddingMeters = 0.008;
			layout.cellGapMeters = 0.003;
			layout.columns = 5;
			layout.rows = 1;
			return layout;
		}

		Common::ConfigureExpandedPitchPanel(layout);
		return layout;
	}

	[[nodiscard]] inline OrientedUiPanelLayout BuildPitchMetricsPanelLayout(
	    const Physics::Vector3& center,
	    const Physics::Vector3& rightAxis,
	    const Physics::Vector3& upAxis,
	    const Physics::Vector3& forwardAxis,
	    const bool minimized)
	{
		OrientedUiPanelLayout layout{};
		layout.center = center;
		layout.rightAxis = rightAxis;
		layout.upAxis = upAxis;
		layout.forwardAxis = forwardAxis;
		if (minimized)
		{
			layout.widthMeters = 0.24;
			layout.heightMeters = 0.048;
			layout.paddingMeters = 0.008;
			layout.cellGapMeters = 0.003;
			layout.columns = 5;
			layout.rows = 1;
			return layout;
		}

		layout.widthMeters = 0.42;
		layout.heightMeters = Common::MetricsPanelHeightMeters;
		layout.paddingMeters = 0.009;
		layout.cellGapMeters = 0.003;
		layout.columns = 5;
		layout.rows = 8;
		return layout;
	}

	[[nodiscard]] inline OrientedUiPanelLayout BuildBattingMetricsPanelLayout(
	    const Physics::Vector3& center,
	    const Physics::Vector3& rightAxis,
	    const Physics::Vector3& upAxis,
	    const Physics::Vector3& forwardAxis,
	    const bool minimized)
	{
		OrientedUiPanelLayout layout{};
		layout.center = center;
		layout.rightAxis = rightAxis;
		layout.upAxis = upAxis;
		layout.forwardAxis = forwardAxis;
		if (minimized)
		{
			layout.widthMeters = 0.24;
			layout.heightMeters = 0.048;
			layout.paddingMeters = 0.008;
			layout.cellGapMeters = 0.003;
			layout.columns = 5;
			layout.rows = 1;
			return layout;
		}

		layout.widthMeters = 0.38;
		layout.heightMeters = 0.23;
		layout.paddingMeters = 0.009;
		layout.cellGapMeters = 0.003;
		layout.columns = 5;
		layout.rows = 8;
		return layout;
	}

	[[nodiscard]] inline OrientedUiPanelLayout BuildMetricsPanelLayout(
	    const Physics::Vector3& center,
	    const Physics::Vector3& rightAxis,
	    const Physics::Vector3& upAxis,
	    const Physics::Vector3& forwardAxis,
	    const bool minimized)
	{
		return BuildPitchMetricsPanelLayout(
		    center,
		    rightAxis,
		    upAxis,
		    forwardAxis,
		    minimized);
	}
#endif
} // namespace BaseballSimulatorVR::Xr::Detail
