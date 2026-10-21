// Per-eye scene rendering into the projection swapchain images.
#include "OpenXrSystemDetail.hpp"

#if defined(_WIN64)
namespace BaseballSimulatorVR::Xr
{
	using namespace Detail;

	namespace
	{
		[[nodiscard]] std::wstring_view PitchTrajectoryDisplayLabel(const Common::PitchTrajectoryDisplayMode mode) noexcept
		{
			switch (mode)
			{
			case Common::PitchTrajectoryDisplayMode::None: return L"投球跡 なし";
			case Common::PitchTrajectoryDisplayMode::Full: return L"投球跡 全体";
			case Common::PitchTrajectoryDisplayMode::ZoneOnly: return L"投球跡 ゾーン内";
			}
			return {};
		}

		[[nodiscard]] std::wstring_view StrikeZoneDisplayLabel(const Common::StrikeZoneDisplayMode mode) noexcept
		{
			switch (mode)
			{
			case Common::StrikeZoneDisplayMode::None: return L"ゾーン なし";
			case Common::StrikeZoneDisplayMode::Plane: return L"ゾーン 平面";
			case Common::StrikeZoneDisplayMode::Box: return L"ゾーン 立体";
			}
			return {};
		}

		constexpr std::array<float, 4> RightEyeClearColor = LeftEyeClearColor;

		[[nodiscard]] bool IsSrgbFormat(const DXGI_FORMAT format) noexcept
		{
			return (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) || (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
		}

		using Common::ScreenPoint;

		[[nodiscard]] Physics::Vector3 ConvertPanelLocalToWorld(
		    const OrientedUiPanelLayout& panelLayout,
		    const double localX,
		    const double localY,
		    const double localDepth)
		{
			return panelLayout.center +
			       (panelLayout.rightAxis * localX) +
			       (panelLayout.upAxis * localY) +
			       (panelLayout.forwardAxis * localDepth);
		}

		template <typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelRectOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    const UiPanelLocalRect& rect,
		    const std::array<float, 4>& color,
		    const double depthOffsetMeters,
		    TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
			Common::UiText::ScopedPanelGroup group(textContext,panelLayout,projectionState);
			if (!projectionState.isValid || (width <= 0) || (height <= 0))
			{
				return;
			}
			if (color[3] <= 0.001F)
			{
				return;
			}
			if ((rect.right <= rect.left) || (rect.top <= rect.bottom))
			{
				return;
			}

			const Physics::Vector3 topLeft = ConvertPanelLocalToWorld(
			    panelLayout,
			    rect.left,
			    rect.top,
			    depthOffsetMeters);
			const Physics::Vector3 topRight = ConvertPanelLocalToWorld(
			    panelLayout,
			    rect.right,
			    rect.top,
			    depthOffsetMeters);
			const Physics::Vector3 bottomRight = ConvertPanelLocalToWorld(
			    panelLayout,
			    rect.right,
			    rect.bottom,
			    depthOffsetMeters);
			const Physics::Vector3 bottomLeft = ConvertPanelLocalToWorld(
			    panelLayout,
			    rect.left,
			    rect.bottom,
			    depthOffsetMeters);
			Common::EmitProjectedWorldTriangle(
			    topLeft,
			    topRight,
			    bottomRight,
			    projectionState,
			    width,
			    height,
			    color.data(),
			    drawTriangle);
			Common::EmitProjectedWorldTriangle(
			    topLeft,
			    bottomRight,
			    bottomLeft,
			    projectionState,
			    width,
			    height,
			    color.data(),
			    drawTriangle);
		}

		template <typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelTextInRect(
		    const Common::ViewProjectionState& projectionState,const LONG width,const LONG height,
		    const OrientedUiPanelLayout& panelLayout,const UiPanelLocalRect& rect,const std::wstring_view label,
		    const int minimumFontPixelHeight,const double rectWidthUsage,const double rectHeightUsage,
		    const double alignX,const double alignY,const std::array<float,4>& textColor,
		    TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
		    (void)minimumFontPixelHeight;(void)drawTriangle;
		    Common::UiText::AppendProjectedPanelText(panelLayout,rect,label,rectWidthUsage,rectHeightUsage,
		        alignX,alignY,textColor,projectionState,width,height,textContext);
		}

		template <typename TResolveRowColorFn, typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    TResolveRowColorFn&& resolveRowColor,
		    TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
			Common::UiText::ScopedPanelGroup group(textContext,panelLayout,projectionState);
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

			auto drawPanelQuad = [&](const double left,
			                         const double top,
			                         const double right,
			                         const double bottom,
			                         const double depthOffset,
			                         const float* const color) -> void
			{
				const Physics::Vector3 topLeft =
				    ConvertPanelLocalToWorld(panelLayout, left, top, depthOffset);
				const Physics::Vector3 topRight =
				    ConvertPanelLocalToWorld(panelLayout, right, top, depthOffset);
				const Physics::Vector3 bottomRight =
				    ConvertPanelLocalToWorld(panelLayout, right, bottom, depthOffset);
				const Physics::Vector3 bottomLeft =
				    ConvertPanelLocalToWorld(panelLayout, left, bottom, depthOffset);
				Common::EmitProjectedWorldTriangle(
				    topLeft,
				    topRight,
				    bottomRight,
				    projectionState,
				    width,
				    height,
				    color,
				    drawTriangle);
				Common::EmitProjectedWorldTriangle(
				    topLeft,
				    bottomRight,
				    bottomLeft,
				    projectionState,
				    width,
				    height,
				    color,
				    drawTriangle);
			};

			const double halfWidth = panelLayout.widthMeters * 0.5;
			const double halfHeight = panelLayout.heightMeters * 0.5;
			const double panelLeft = -halfWidth;
			const double panelRight = halfWidth;
			const double panelTop = halfHeight;
			const double panelBottom = -halfHeight;
			const double cameraSide = Physics::Dot(
			    projectionState.position - panelLayout.center,
			    panelLayout.forwardAxis);
			const double towardCameraDepthSign = (cameraSide >= 0.0) ? 1.0 : -1.0;
			const double panelBackgroundDepth = -0.0015 * towardCameraDepthSign;
			const double panelForegroundDepth = 0.0065 * towardCameraDepthSign;
			drawPanelQuad(
			    panelLeft,
			    panelTop,
			    panelRight,
			    panelBottom,
			    panelBackgroundDepth,
			    Common::DebugWorldStyle::MainControlPanelBackgroundColor.data());

			const double border =
			    Common::DebugWorldLayout::ResultPanelBorderThicknessMeters;
			drawPanelQuad(
			    panelLeft,
			    panelTop,
			    panelRight,
			    panelTop - border,
			    panelForegroundDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
			drawPanelQuad(
			    panelLeft,
			    panelBottom + border,
			    panelRight,
			    panelBottom,
			    panelForegroundDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
			drawPanelQuad(
			    panelLeft,
			    panelTop,
			    panelLeft + border,
			    panelBottom,
			    panelForegroundDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
			drawPanelQuad(
			    panelRight - border,
			    panelTop,
			    panelRight,
			    panelBottom,
			    panelForegroundDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return;
			}

			for (int rowIndex = 0; rowIndex < panelLayout.rows; ++rowIndex)
			{
				const double cellTop = innerLayout.innerTop -
				                       (static_cast<double>(rowIndex) *
				                        (innerLayout.cellHeight + panelLayout.cellGapMeters));
				const double cellBottom = cellTop - innerLayout.cellHeight;
				const std::array<float, 4> baseColor = resolveRowColor(rowIndex);
				if (baseColor[3] <= 0.001F)
				{
					continue;
				}
				for (int columnIndex = 0; columnIndex < panelLayout.columns; ++columnIndex)
				{
					const double cellLeft =
					    innerLayout.innerLeft +
					    (static_cast<double>(columnIndex) *
					     (innerLayout.cellWidth + panelLayout.cellGapMeters));
					const double cellRight = cellLeft + innerLayout.cellWidth;
					const float brightnessScale = 1.0F;
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
					    panelForegroundDepth,
					    cellColor.data());
				}
			}
		}

		template <typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelTitleBarOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    const std::array<float, 4>& titleBarColor,
		    const std::array<float, 4>& minimizeButtonColor,
		    const std::array<float, 4>& closeButtonColor,
		    TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
			Common::UiText::ScopedPanelGroup group(textContext,panelLayout,projectionState);
			if (!projectionState.isValid || (width <= 0) || (height <= 0))
			{
				return;
			}
			if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0))
			{
				return;
			}

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return;
			}

			const double cameraSide = Physics::Dot(
			    projectionState.position - panelLayout.center,
			    panelLayout.forwardAxis);
			const double towardCameraDepthSign = (cameraSide >= 0.0) ? 1.0 : -1.0;
			const double titleBarDepth = 0.0140 * towardCameraDepthSign;
			const double buttonDepth = 0.0160 * towardCameraDepthSign;
			const double separatorDepth = 0.0180 * towardCameraDepthSign;

			auto drawPanelQuad = [&](const double left,
			                         const double top,
			                         const double right,
			                         const double bottom,
			                         const double depthOffset,
			                         const float* const color) -> void
			{
				const Physics::Vector3 topLeft =
				    ConvertPanelLocalToWorld(panelLayout, left, top, depthOffset);
				const Physics::Vector3 topRight =
				    ConvertPanelLocalToWorld(panelLayout, right, top, depthOffset);
				const Physics::Vector3 bottomRight =
				    ConvertPanelLocalToWorld(panelLayout, right, bottom, depthOffset);
				const Physics::Vector3 bottomLeft =
				    ConvertPanelLocalToWorld(panelLayout, left, bottom, depthOffset);
				Common::EmitProjectedWorldTriangle(
				    topLeft,
				    topRight,
				    bottomRight,
				    projectionState,
				    width,
				    height,
				    color,
				    drawTriangle);
				Common::EmitProjectedWorldTriangle(
				    topLeft,
				    bottomRight,
				    bottomLeft,
				    projectionState,
				    width,
				    height,
				    color,
				    drawTriangle);
			};

			const double titleBarTop = innerLayout.innerTop;
			const double titleBarBottom = titleBarTop - innerLayout.cellHeight;
			drawPanelQuad(
			    innerLayout.innerLeft,
			    titleBarTop,
			    innerLayout.innerRight,
			    titleBarBottom,
			    titleBarDepth,
			    titleBarColor.data());

			if (panelLayout.columns < 2)
			{
				return;
			}

			const double columnStep =
			    innerLayout.cellWidth + panelLayout.cellGapMeters;
			const double minimizeLeft =
			    innerLayout.innerLeft +
			    (static_cast<double>(panelLayout.columns - 2) * columnStep);
			const double minimizeRight = minimizeLeft + innerLayout.cellWidth;
			const double closeLeft =
			    innerLayout.innerLeft +
			    (static_cast<double>(panelLayout.columns - 1) * columnStep);
			const double closeRight = closeLeft + innerLayout.cellWidth;
			drawPanelQuad(
			    minimizeLeft,
			    titleBarTop,
			    minimizeRight,
			    titleBarBottom,
			    buttonDepth,
			    minimizeButtonColor.data());
			drawPanelQuad(
			    closeLeft,
			    titleBarTop,
			    closeRight,
			    titleBarBottom,
			    buttonDepth,
			    closeButtonColor.data());

			const double separatorWidth =
			    std::clamp(panelLayout.cellGapMeters * 0.45, 0.001, 0.0035);
			drawPanelQuad(
			    minimizeLeft - (separatorWidth * 0.5),
			    titleBarTop,
			    minimizeLeft + (separatorWidth * 0.5),
			    titleBarBottom,
			    separatorDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
			drawPanelQuad(
			    closeLeft - (separatorWidth * 0.5),
			    titleBarTop,
			    closeLeft + (separatorWidth * 0.5),
			    titleBarBottom,
			    separatorDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
			drawPanelQuad(
			    innerLayout.innerLeft,
			    titleBarBottom,
			    innerLayout.innerRight,
			    titleBarBottom - separatorWidth,
			    separatorDepth,
			    Common::DebugWorldStyle::MainControlPanelBorderColor.data());
		}

		[[nodiscard]] bool TryResolveOrientedPanelCellBounds(
		    const OrientedUiPanelLayout& panelLayout,
		    const int cellIndex,
		    double* const outCellLeft,
		    double* const outCellTop,
		    double* const outCellRight,
		    double* const outCellBottom)
		{
			if ((outCellLeft == nullptr) || (outCellTop == nullptr) ||
			    (outCellRight == nullptr) || (outCellBottom == nullptr))
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

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return false;
			}

			const int rowIndex = cellIndex / panelLayout.columns;
			const int columnIndex = cellIndex % panelLayout.columns;
			const double cellTop = innerLayout.innerTop -
			                       (static_cast<double>(rowIndex) *
			                        (innerLayout.cellHeight + panelLayout.cellGapMeters));
			const double cellBottom = cellTop - innerLayout.cellHeight;
			const double cellLeft =
			    innerLayout.innerLeft +
			    (static_cast<double>(columnIndex) *
			     (innerLayout.cellWidth + panelLayout.cellGapMeters));
			const double cellRight = cellLeft + innerLayout.cellWidth;
			*outCellLeft = cellLeft;
			*outCellTop = cellTop;
			*outCellRight = cellRight;
			*outCellBottom = cellBottom;
			return true;
		}

		template <typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelCellOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    const int cellIndex,
		    const std::array<float, 4>& color,
		    const double depthOffsetMeters,
		    TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
			Common::UiText::ScopedPanelGroup group(textContext,panelLayout,projectionState);
			if (!projectionState.isValid || (width <= 0) || (height <= 0))
			{
				return;
			}
			if (color[3] <= 0.001F)
			{
				return;
			}

			double cellLeft = 0.0;
			double cellTop = 0.0;
			double cellRight = 0.0;
			double cellBottom = 0.0;
			if (!TryResolveOrientedPanelCellBounds(
			        panelLayout,
			        cellIndex,
			        &cellLeft,
			        &cellTop,
			        &cellRight,
			        &cellBottom))
			{
				return;
			}

			const Physics::Vector3 topLeft = ConvertPanelLocalToWorld(
			    panelLayout,
			    cellLeft,
			    cellTop,
			    depthOffsetMeters);
			const Physics::Vector3 topRight = ConvertPanelLocalToWorld(
			    panelLayout,
			    cellRight,
			    cellTop,
			    depthOffsetMeters);
			const Physics::Vector3 bottomRight = ConvertPanelLocalToWorld(
			    panelLayout,
			    cellRight,
			    cellBottom,
			    depthOffsetMeters);
			const Physics::Vector3 bottomLeft = ConvertPanelLocalToWorld(
			    panelLayout,
			    cellLeft,
			    cellBottom,
			    depthOffsetMeters);
			Common::EmitProjectedWorldTriangle(
			    topLeft,
			    topRight,
			    bottomRight,
			    projectionState,
			    width,
			    height,
			    color.data(),
			    drawTriangle);
			Common::EmitProjectedWorldTriangle(
			    topLeft,
			    bottomRight,
			    bottomLeft,
			    projectionState,
			    width,
			    height,
			    color.data(),
			    drawTriangle);
		}

		template <typename TLabelResolverFn,typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelTextOverlay(
		    const Common::ViewProjectionState& projectionState,const LONG width,const LONG height,
		    const OrientedUiPanelLayout& panelLayout,const int labelCount,const int fontPixelHeight,
		    const double cellWidthUsage,const double cellHeightUsage,
		    TLabelResolverFn&& resolveLabel,TDrawTriangleFn&& drawTriangle,Common::UiText::AtlasDrawContext& textContext)
		{
		    UiPanelInnerLayout inner{};
		    if(!TryBuildPanelInnerLayout(panelLayout,&inner)) return;
		    const int count=std::clamp(labelCount,0,panelLayout.columns*panelLayout.rows);
		    for(int index=0;index<count;++index) {
				        const UiPanelLocalRect rect=Common::UiText::BuildPanelCellRect(inner.innerLeft,inner.innerTop,inner.cellWidth,inner.cellHeight,
		            panelLayout.cellGapMeters,panelLayout.columns,index);
		        EmitProjectedOrientedPanelTextInRect(projectionState,width,height,panelLayout,rect,resolveLabel(index),
		            fontPixelHeight,cellWidthUsage,cellHeightUsage,.5,.5,{1,1,1,1},drawTriangle,textContext);
		    }
		}

		constexpr double RadiansToDegrees = 57.29577951308232;

		struct DerivedFlightMetrics final
		{
			bool hasZonePassPoint{ false };
			Physics::Vector3 zonePassPoint{};
			Physics::Vector3 postPassDirection{};
			bool hasBattedContact{ false };
			double verticalBreakMeters{ 0.0 };
			double horizontalBreakMeters{ 0.0 };
			bool isZoneInStrike{ false };
			bool hasLandingPoint{ false };
			Physics::Vector3 landingPoint{};
			double directionAngleDegrees{ 0.0 };
			double launchAngleDegrees{ 0.0 };
			double carryDistanceMeters{ 0.0 };
			double apexHeightMeters{ 0.0 };
		};

		[[nodiscard]] DerivedFlightMetrics BuildDerivedFlightMetrics(
		    const std::vector<Physics::Vector3>& trajectory,
		    const double pitchDistanceMeters)
		{
			DerivedFlightMetrics metrics{};
			if (trajectory.size() < 2)
			{
				return metrics;
			}

			int passUpperIndex = -1;
			Physics::Vector3 passPoint{};
			for (std::size_t pointIndex = 1; pointIndex < trajectory.size(); ++pointIndex)
			{
				const Physics::Vector3& previousPoint = trajectory[pointIndex - 1];
				const Physics::Vector3& currentPoint = trajectory[pointIndex];
				const bool crossedDepth =
				    ((previousPoint.z <= pitchDistanceMeters) &&
				     (currentPoint.z >= pitchDistanceMeters)) ||
				    ((previousPoint.z >= pitchDistanceMeters) &&
				     (currentPoint.z <= pitchDistanceMeters));
				if (!crossedDepth)
				{
					continue;
				}

				double blend = 0.0;
				const double depthDelta = currentPoint.z - previousPoint.z;
				if (std::abs(depthDelta) > 1e-6)
				{
					blend = (pitchDistanceMeters - previousPoint.z) / depthDelta;
				}
				blend = std::clamp(blend, 0.0, 1.0);
				passPoint = previousPoint +
				            ((currentPoint - previousPoint) * blend);
				passUpperIndex = static_cast<int>(pointIndex);
				break;
			}

			if (passUpperIndex < 0)
			{
				return metrics;
			}

			metrics.hasZonePassPoint = true;
			metrics.zonePassPoint = passPoint;
			const Physics::Vector3 releasePoint = trajectory.front();
			metrics.verticalBreakMeters = passPoint.y - releasePoint.y;
			metrics.horizontalBreakMeters = passPoint.x - releasePoint.x;
			const double strikeZoneHalfWidth =
			    Common::GetStrikeZoneHalfWidthMeters();
			const double strikeZoneBottom =
			    Common::GetStrikeZoneBottomMeters();
			const double strikeZoneTop =
			    Common::GetStrikeZoneTopMeters();
			metrics.isZoneInStrike =
			    (std::abs(passPoint.x) <= strikeZoneHalfWidth) &&
			    (passPoint.y >= strikeZoneBottom) &&
			    (passPoint.y <= strikeZoneTop);

			metrics.apexHeightMeters = passPoint.y;
			for (std::size_t pointIndex = static_cast<std::size_t>(passUpperIndex);
			     pointIndex < trajectory.size();
			     ++pointIndex)
			{
				metrics.apexHeightMeters = (std::max)(metrics.apexHeightMeters, trajectory[pointIndex].y);
			}

			if (static_cast<std::size_t>(passUpperIndex) < trajectory.size())
			{
				const Physics::Vector3 initialDirection =
				    trajectory[static_cast<std::size_t>(passUpperIndex)] - passPoint;
				if (Physics::Length(initialDirection) > 1e-6)
				{
					metrics.postPassDirection = initialDirection;
				}
			}
			double minimumDepthAfterPass = passPoint.z;
			for (std::size_t pointIndex = static_cast<std::size_t>(passUpperIndex);
			     pointIndex < trajectory.size();
			     ++pointIndex)
			{
				minimumDepthAfterPass = (std::min)(minimumDepthAfterPass,
				                                   trajectory[pointIndex].z);
			}
			metrics.hasBattedContact =
			    (minimumDepthAfterPass <= (passPoint.z - 0.05));

			for (std::size_t pointIndex = static_cast<std::size_t>(passUpperIndex);
			     pointIndex < trajectory.size();
			     ++pointIndex)
			{
				const Physics::Vector3& currentPoint = trajectory[pointIndex];
				if (currentPoint.y <= 0.0)
				{
					metrics.hasLandingPoint = true;
					metrics.landingPoint = currentPoint;
					if (pointIndex > 0)
					{
						const Physics::Vector3& previousPoint =
						    trajectory[pointIndex - 1];
						const double yDelta =
						    currentPoint.y - previousPoint.y;
						if (std::abs(yDelta) > 1e-6)
						{
							double blend = (0.0 - previousPoint.y) / yDelta;
							blend = std::clamp(blend, 0.0, 1.0);
							metrics.landingPoint =
							    previousPoint +
							    ((currentPoint - previousPoint) * blend);
						}
					}
					break;
				}
			}

			const Physics::Vector3 endPoint = metrics.hasLandingPoint
			                                      ? metrics.landingPoint
			                                      : trajectory.back();
			const Physics::Vector3 travelVector = endPoint - passPoint;
			const double horizontalDistance = std::sqrt(
			    (travelVector.x * travelVector.x) +
			    (travelVector.z * travelVector.z));
			if (horizontalDistance > 1e-6)
			{
				metrics.directionAngleDegrees =
				    std::atan2(travelVector.x, -travelVector.z) * RadiansToDegrees;
			}
			metrics.carryDistanceMeters =
			    (travelVector.z <= 0.0)
			        ? horizontalDistance
			        : -horizontalDistance;

			const Physics::Vector3 launchDirection =
			    (Physics::Length(metrics.postPassDirection) > 1e-6)
			        ? metrics.postPassDirection
			        : travelVector;
			const double launchHorizontal = std::sqrt(
			    (launchDirection.x * launchDirection.x) +
			    (launchDirection.z * launchDirection.z));
			if ((launchHorizontal > 1e-6) ||
			    (std::abs(launchDirection.y) > 1e-6))
			{
				metrics.launchAngleDegrees =
				    std::atan2(launchDirection.y, launchHorizontal) *
				    RadiansToDegrees;
			}

			return metrics;
		}
	} // namespace

	bool OpenXrSystem::RenderLocatedViews()
	{
		const uint32_t viewCount = m_locatedViewCount;
		m_xrPostProcessStatus = m_isFxaaAvailable ? L"fxaa" : L"direct";

		m_uiTextAtlas.BeginFrame();
		for (uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
		{
			SwapchainContext& swapchainContext = m_projectionSwapchains[viewIndex];
			const XrView& adjustedView = m_views[viewIndex];
			uint32_t imageIndex = 0;

			XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			const XrResult acquireResult = xrAcquireSwapchainImage(
			    swapchainContext.swapchainHandle,
			    &acquireInfo,
			    &imageIndex);
			if (XR_FAILED(acquireResult))
			{
				m_lastProjectionLayerStatus = L"acquire-failed";
				m_sessionName = L"Session(unavailable): " +
				                BuildXrErrorMessage(L"xrAcquireSwapchainImage", acquireResult);
				return false;
			}

			XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			waitInfo.timeout = XR_INFINITE_DURATION;
			const auto releaseAcquiredImage = [&]() -> bool
			{
				XrSwapchainImageReleaseInfo releaseInfo{
					XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
				};
				const XrResult releaseResult = xrReleaseSwapchainImage(
				    swapchainContext.swapchainHandle,
				    &releaseInfo);
				if (XR_FAILED(releaseResult))
				{
					m_lastProjectionLayerStatus = L"release-failed";
					m_sessionName = L"Session(unavailable): " +
					                BuildXrErrorMessage(
					                    L"xrReleaseSwapchainImage",
					                    releaseResult);
					return false;
				}
				return true;
			};
			const XrResult waitResult = xrWaitSwapchainImage(
			    swapchainContext.swapchainHandle,
			    &waitInfo);
			if (XR_FAILED(waitResult))
			{
				if (!releaseAcquiredImage())
				{
					return false;
				}
				m_lastProjectionLayerStatus = L"wait-failed";
				m_sessionName = L"Session(unavailable): " +
				                BuildXrErrorMessage(L"xrWaitSwapchainImage", waitResult);
				return false;
			}

			const float* clearColor = (viewIndex == 0U)
			                              ? LeftEyeClearColor.data()
			                              : RightEyeClearColor.data();
			const bool renderResult = RenderSwapchainImage(
			    swapchainContext,
			    imageIndex,
			    clearColor,
			    adjustedView);
			if (!releaseAcquiredImage())
			{
				return false;
			}

			if (!renderResult)
			{
				return false;
			}

			XrCompositionLayerProjectionView& projectionView = m_projectionLayerViews[viewIndex];
			projectionView = XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			projectionView.pose = adjustedView.pose;
			projectionView.fov = adjustedView.fov;
			projectionView.subImage.swapchain = swapchainContext.swapchainHandle;
			projectionView.subImage.imageRect.offset = { 0, 0 };
			projectionView.subImage.imageRect.extent = { swapchainContext.width, swapchainContext.height };
			projectionView.subImage.imageArrayIndex = 0;
		}

		m_projectionLayer = XrCompositionLayerProjection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		m_projectionLayer.space = m_referenceSpaceHandle;
		m_projectionLayer.viewCount = viewCount;
		m_projectionLayer.views = m_projectionLayerViews.data();
		if (m_lastProjectionLayerStatus == L"locating")
		{
			m_lastProjectionLayerStatus = L"ok";
		}
		return true;
	}

	void OpenXrSystem::AppendTrajectoryPoint(const Physics::BallState& ballState)
	{
		Common::UpdatePhaseTrajectoryTrails(m_trajectoryTrails, ballState,
		    m_fieldPresentation, std::chrono::steady_clock::now());
	}

	bool OpenXrSystem::RenderSwapchainImage(
	    SwapchainContext& swapchainContext,
	    const uint32_t imageIndex,
	    const float* clearColor,
	    const XrView& view)
	{
		Common::D3d12RenderFrame& frame = swapchainContext.frames[swapchainContext.nextFrame];
		swapchainContext.nextFrame = (swapchainContext.nextFrame + 1) % swapchainContext.frames.size();
		if ((frame.allocator == nullptr) ||
		    (frame.commands == nullptr) ||
		    (m_d3d12Queue == nullptr))
		{
			m_lastProjectionLayerStatus = L"command-missing";
			m_sessionName = L"Session(unavailable): XR D3D12 command resources missing";
			return false;
		}

		if (imageIndex >= swapchainContext.images.size())
		{
			m_lastProjectionLayerStatus = L"image-index-range";
			m_sessionName = L"Session(unavailable): swapchain image index out of range";
			return false;
		}

		if ((imageIndex >= swapchainContext.depthBuffers.size()) ||
		    (swapchainContext.dsvHeap == nullptr))
		{
			m_lastProjectionLayerStatus = L"depth-missing";
			m_sessionName = L"Session(unavailable): swapchain depth resources missing";
			return false;
		}

		const bool hasSceneResources =
		    (swapchainContext.sceneRtvHeap != nullptr) &&
		    (swapchainContext.sceneSrvHeap != nullptr) &&
		    (imageIndex < swapchainContext.sceneColorTargets.size()) &&
		    (swapchainContext.sceneColorTargets[imageIndex] != nullptr) &&
		    (m_srvDescriptorSize != 0U);
		const bool useMsaaResolvePath =
		    hasSceneResources &&
		    m_isFxaaAvailable &&
		    ((std::max)(1U, m_projectionSampleCount) > 1U);
		const bool useAppMsaaPath =
		    hasSceneResources &&
		    ((std::max)(1U, m_projectionSampleCount) == 1U) &&
		    (swapchainContext.msaaColorTarget != nullptr) &&
		    (swapchainContext.msaaDepthTarget != nullptr);
		const bool useSceneDrawPath =
		    hasSceneResources &&
		    ((std::max)(1U, m_projectionSampleCount) == 1U) &&
		    !useAppMsaaPath;
		const bool canUseScenePostProcess =
		    useMsaaResolvePath || useAppMsaaPath || useSceneDrawPath;
		m_xrPostProcessStatus = canUseScenePostProcess
		                            ? (m_isFxaaAvailable ? L"fxaa" : L"copy")
		                            : L"direct";

		m_lastProjectionLayerStatus = L"clearing";

		ID3D12Resource* renderTargetTexture = swapchainContext.images[imageIndex].texture;
		if (renderTargetTexture == nullptr)
		{
			m_lastProjectionLayerStatus = L"texture-missing";
			m_sessionName = L"Session(unavailable): swapchain texture missing";
			return false;
		}

		const HRESULT beginResult = frame.Begin(m_xrFence.Get(), m_xrFenceEvent);
		if (FAILED(beginResult))
		{
			m_projectionRenderingFailed = true;
			m_lastProjectionLayerStatus = L"view-frame-reuse-failed";
			m_sessionName = L"Session(unavailable): " + BuildD3d12ErrorMessage(L"XR view Begin", beginResult);
			return false;
		}
		auto closeCommandListOnFailure = [&]() noexcept
		{
			(void)frame.commands->Close();
			m_projectionRenderingFailed = true;
		};

		D3D12_RESOURCE_BARRIER toDrawBarriers[2]{};
		UINT toDrawBarrierCount = 0;

		ID3D12Resource* sceneColorTexture = nullptr;
		if (hasSceneResources)
		{
			sceneColorTexture =
			    swapchainContext.sceneColorTargets[imageIndex].Get();
		}

		if (useMsaaResolvePath || useAppMsaaPath || useSceneDrawPath)
		{
			D3D12_RESOURCE_BARRIER& sceneToDrawBarrier =
			    toDrawBarriers[toDrawBarrierCount++];
			sceneToDrawBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneToDrawBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneToDrawBarrier.Transition.pResource = sceneColorTexture;
			sceneToDrawBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			sceneToDrawBarrier.Transition.StateAfter = (useMsaaResolvePath || useAppMsaaPath)
			                                               ? D3D12_RESOURCE_STATE_RESOLVE_DEST
			                                               : D3D12_RESOURCE_STATE_RENDER_TARGET;
			sceneToDrawBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}
		if (toDrawBarrierCount > 0) frame.commands->ResourceBarrier(toDrawBarrierCount, toDrawBarriers);

		D3D12_CPU_DESCRIPTOR_HANDLE swapchainRtvHandle =
		    swapchainContext.rtvHeap->GetCPUDescriptorHandleForHeapStart();
		swapchainRtvHandle.ptr += static_cast<SIZE_T>(imageIndex) *
		                          static_cast<SIZE_T>(m_rtvDescriptorSize);
		D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle{};
		if (swapchainContext.sceneRtvHeap != nullptr)
		{
			sceneRtvHandle =
			    swapchainContext.sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
			sceneRtvHandle.ptr += static_cast<SIZE_T>(imageIndex) *
			                      static_cast<SIZE_T>(m_rtvDescriptorSize);
		}
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = useAppMsaaPath
		    ? swapchainContext.msaaRtvHeap->GetCPUDescriptorHandleForHeapStart()
		    : (useSceneDrawPath ? sceneRtvHandle : swapchainRtvHandle);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = swapchainContext.dsvHeap->GetCPUDescriptorHandleForHeapStart();
		dsvHandle.ptr += static_cast<SIZE_T>(imageIndex) * static_cast<SIZE_T>(m_dsvDescriptorSize);
		if (useAppMsaaPath)
		{
			dsvHandle = swapchainContext.msaaDsvHeap->GetCPUDescriptorHandleForHeapStart();
		}
		frame.commands->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		frame.commands->ClearDepthStencilView(
		    dsvHandle,
		    D3D12_CLEAR_FLAG_DEPTH,
		    1.0F,
		    0,
		    0,
		    nullptr);
		frame.commands->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

#if defined(BASEBALLSIMVR_DISABLE_WORLD_DRAW)
		(void)view;
		m_lastProjectionLayerStatus = L"world-draw-disabled";
#else
		D3D12_VERTEX_BUFFER_VIEW activeVertexBufferView = frame.vertexView;
		UINT activeVertexCapacity = frame.vertexCapacity;
		ID3D12RootSignature* activeRootSignature = m_debugRootSignature.Get();
		ID3D12PipelineState* activeOpaquePipelineState = m_debugPipelineState.Get();
		ID3D12Resource* activeVertexBuffer = frame.vertices.Get();
		if (!activeRootSignature || !activeOpaquePipelineState || !activeVertexBuffer)
		{
			m_lastProjectionLayerStatus = L"view-resources-missing";
			closeCommandListOnFailure();
			return false;
		}
		const LONG width = swapchainContext.width;
		const LONG height = swapchainContext.height;
		if ((width <= 0) || (height <= 0))
		{
			m_lastProjectionLayerStatus = L"invalid-extent";
			m_sessionName = L"Session(unavailable): invalid swapchain extent";
			closeCommandListOnFailure();
			return false;
		}

		{
			const Common::ViewProjectionState projectionState = BuildProjectionStateFromView(view);
			std::vector<Common::UiText::AtlasVertex> atlasVertices;
			Common::UiText::AtlasDrawContext textContext{m_uiTextAtlas,m_uiTextLayouts,atlasVertices};
			std::vector<Common::DebugOverlay::Vertex> opaqueVertices{};
			std::vector<Common::DebugOverlay::Vertex> alphaVertices{};
			opaqueVertices.reserve(8192);
			alphaVertices.reserve(1024);
			bool hasVisibleCoverage = false;

			auto selectVertexBucket =
			    [&](const float* const color) -> std::vector<Common::DebugOverlay::Vertex>*
			{
				if ((color != nullptr) && (color[3] < 0.999F))
				{
					return &alphaVertices;
				}

				return &opaqueVertices;
			};

			auto appendMarker = [&](const ScreenPoint& centerPoint, const LONG halfSize, const float* color) -> void
			{
				auto* const vertices = selectVertexBucket(color);
				Common::DebugOverlay::AppendMarkerQuad(
				    vertices,
				    centerPoint.x,
				    centerPoint.y,
				    halfSize,
				    color,
				    width,
				    height,
				    centerPoint.depth);
				const double markerMinX = centerPoint.x - static_cast<double>(halfSize);
				const double markerMaxX = centerPoint.x + static_cast<double>(halfSize) + 1.0;
				const double markerMinY = centerPoint.y - static_cast<double>(halfSize);
				const double markerMaxY = centerPoint.y + static_cast<double>(halfSize) + 1.0;
				const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
				const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
				if ((markerMaxX >= 0.0) && (markerMinX <= screenMaxX) &&
				    (markerMaxY >= 0.0) && (markerMinY <= screenMaxY))
				{
					hasVisibleCoverage = true;
				}
			};

			auto appendLine = [&](const ScreenPoint& startPoint,
			                      const ScreenPoint& endPoint,
			                      const LONG halfSize,
			                      const float* color) -> void
			{
				auto* const vertices = selectVertexBucket(color);
				Common::DebugOverlay::AppendThickScreenLine(
				    vertices,
				    startPoint,
				    endPoint,
				    halfSize,
				    color,
				    width,
				    height);
				const double linePadding = static_cast<double>(halfSize) + 1.0;
				const double lineMinX = (std::min)(startPoint.x, endPoint.x) - linePadding;
				const double lineMaxX = (std::max)(startPoint.x, endPoint.x) + linePadding;
				const double lineMinY = (std::min)(startPoint.y, endPoint.y) - linePadding;
				const double lineMaxY = (std::max)(startPoint.y, endPoint.y) + linePadding;
				const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
				const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
				if ((lineMaxX >= 0.0) && (lineMinX <= screenMaxX) &&
				    (lineMaxY >= 0.0) && (lineMinY <= screenMaxY))
				{
					hasVisibleCoverage = true;
				}
			};

			auto appendTriangle = [&](const ScreenPoint& p0,
			                          const ScreenPoint& p1,
			                          const ScreenPoint& p2,
			                          const float* color) -> void
			{
				if(textContext.currentGroup!=0) {
				    Common::UiText::AppendUntexturedTriangle(textContext,p0,p1,p2,color,width,height);
				} else {
				auto* const vertices = selectVertexBucket(color);

				Common::DebugOverlay::AppendScreenTriangle(
				    vertices,
				    p0,
				    p1,
				    p2,
				    color,
				    width,
				    height);
				}
				const double triangleMinX = (std::min)(p0.x, (std::min)(p1.x, p2.x));
				const double triangleMaxX = (std::max)(p0.x, (std::max)(p1.x, p2.x));
				const double triangleMinY = (std::min)(p0.y, (std::min)(p1.y, p2.y));
				const double triangleMaxY = (std::max)(p0.y, (std::max)(p1.y, p2.y));
				const double screenMaxX = static_cast<double>((std::max)(1L, width - 1));
				const double screenMaxY = static_cast<double>((std::max)(1L, height - 1));
				if ((triangleMaxX >= 0.0) && (triangleMinX <= screenMaxX) &&
				    (triangleMaxY >= 0.0) && (triangleMinY <= screenMaxY))
				{
					hasVisibleCoverage = true;
				}
			};

			Common::UiText::EmitProjectedFieldPresentation(m_fieldPresentation,projectionState,width,height,textContext,appendTriangle);

			if (m_overlayVisibility.showFloorMesh)
			{
				Common::EmitProjectedFloorMeshOverlay(
				    projectionState,
				    width,
				    height,
				    [&](const ScreenPoint& p0,
				        const ScreenPoint& p1,
				        const ScreenPoint& p2,
				        const float* color) -> void
				    {
					    appendTriangle(p0, p1, p2, color);
				    });
			}

			Common::EmitProjectedPhaseDebugWorldOverlay(
			    projectionState,
			    width,
			    height,
			    m_trajectoryTrails, m_overlayVisibility,
			    m_latestBallState,
			    [&](const ScreenPoint& screenPoint, const LONG halfSize, const float* color) -> void
			    {
				    appendMarker(screenPoint, halfSize, color);
			    },
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    appendLine(
				        startPoint,
				        endPoint,
				        halfSize,
				        color);
			    },
			    false,
			    false,
			    true,
			    false);
			Common::EmitProjectedZonePassCircleOverlay(
			    projectionState,
			    width,
			    height,
			    m_duelZonePassMarker,
			    m_latestBallState.properties.radiusMeters,
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    appendLine(
				        startPoint,
				        endPoint,
				        halfSize,
				        color);
			    });
			Common::EmitProjectedSolidBatOverlay(
			    projectionState,
			    width,
			    height,
			    m_batOverlayState,
			    [&](const ScreenPoint& p0,
			        const ScreenPoint& p1,
			        const ScreenPoint& p2,
			        const float* color) -> void
			    {
				    appendTriangle(p0, p1, p2, color);
			    });

			Common::EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
			    projectionState,
			    width,
			    height,
			    m_trajectoryTrails, m_overlayVisibility,
			    m_latestBallState,
			    [&](const ScreenPoint& p0,
			        const ScreenPoint& p1,
			        const ScreenPoint& p2,
			        const float* color) -> void
			    {
				    appendTriangle(p0, p1, p2, color);
			    },
			    std::clamp(
			        m_renderQuality.trailOpacityScale,
			        0.0F,
			        1.5F), m_fieldPresentation.ballVisible);

			if (m_showMainControlPanel || m_showPitchControlPanel ||
			    m_showMetricsPanel || m_showBattingMetricsPanel)
			{
				const Physics::Vector3 panelArcForward =
				    ComputeHorizontalArcForwardFromHeadView(
				        m_leftEyeViewState,
				        m_nonMainPanelForward);
				Physics::Vector3 pitchPanelFacingForward = m_pitchPanelForward;
				Physics::Vector3 metricsPanelFacingForward = m_metricsPanelForward;
				Physics::Vector3 battingMetricsPanelFacingForward =
				    m_battingMetricsPanelForward;
				if (m_leftEyeViewState.isValid)
				{
					pitchPanelFacingForward = ResolvePanelFacingForward(
					    m_leftEyeViewState.position - m_pitchControlPanelCenter,
					    m_pitchPanelForward);
					metricsPanelFacingForward = ResolvePanelFacingForward(
					    m_leftEyeViewState.position - m_metricsPanelCenter,
					    m_metricsPanelForward);
					battingMetricsPanelFacingForward = ResolvePanelFacingForward(
					    m_leftEyeViewState.position - m_battingMetricsPanelCenter,
					    m_battingMetricsPanelForward);
				}
				else
				{
					pitchPanelFacingForward = ResolvePanelFacingForward(
					    Physics::Vector3{
					        -panelArcForward.x,
					        -panelArcForward.y,
					        -panelArcForward.z },
					    m_pitchPanelForward);
					metricsPanelFacingForward = ResolvePanelFacingForward(
					    Physics::Vector3{
					        -panelArcForward.x,
					        -panelArcForward.y,
					        -panelArcForward.z },
					    m_metricsPanelForward);
					battingMetricsPanelFacingForward = ResolvePanelFacingForward(
					    Physics::Vector3{
					        -panelArcForward.x,
					        -panelArcForward.y,
					        -panelArcForward.z },
					    m_battingMetricsPanelForward);
				}
				Physics::Vector3 pitchPanelRight{};
				Physics::Vector3 pitchPanelUp{};
				Physics::Vector3 pitchPanelForward{};
				BuildUiPanelAxesFromForward(
				    pitchPanelFacingForward,
				    &pitchPanelRight,
				    &pitchPanelUp,
				    &pitchPanelForward);
				Physics::Vector3 metricsPanelRight{};
				Physics::Vector3 metricsPanelUp{};
				Physics::Vector3 metricsPanelForward{};
				BuildUiPanelAxesFromForward(
				    metricsPanelFacingForward,
				    &metricsPanelRight,
				    &metricsPanelUp,
				    &metricsPanelForward);
				Physics::Vector3 battingMetricsPanelRight{};
				Physics::Vector3 battingMetricsPanelUp{};
				Physics::Vector3 battingMetricsPanelForward{};
				BuildUiPanelAxesFromForward(
				    battingMetricsPanelFacingForward,
				    &battingMetricsPanelRight,
				    &battingMetricsPanelUp,
				    &battingMetricsPanelForward);
				OrientedUiPanelLayout mainPanelLayout = BuildMainPanelLayout(
				    m_mainControlPanelCenter,
				    m_uiPanelRight,
				    m_uiPanelUp,
				    m_uiPanelForward);
				OrientedUiPanelLayout pitchPanelLayout = BuildPitchPanelLayout(
				    m_pitchControlPanelCenter,
				    pitchPanelRight,
				    pitchPanelUp,
				    pitchPanelForward,
				    m_pitchPanelMinimized);
				OrientedUiPanelLayout metricsPanelLayout = BuildMetricsPanelLayout(
				    m_metricsPanelCenter,
				    metricsPanelRight,
				    metricsPanelUp,
				    metricsPanelForward,
				    m_metricsPanelMinimized);
				OrientedUiPanelLayout battingMetricsPanelLayout =
				    BuildBattingMetricsPanelLayout(
				        m_battingMetricsPanelCenter,
				        battingMetricsPanelRight,
				        battingMetricsPanelUp,
				        battingMetricsPanelForward,
				        m_battingMetricsPanelMinimized);

				if (m_showMainControlPanel)
				{
					const auto calibrationUi = Common::BuildStrikeZoneCalibrationUi(m_uiTelemetry.strikeZoneCalibration);
					static constexpr std::array<std::array<float, 4>, 2> MainPanelRowColors{
						Common::DebugWorldStyle::MainControlPanelModeRowColor,
						Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor
					};
					EmitProjectedOrientedPanelOverlay(
					    projectionState,
					    width,
					    height,
					    mainPanelLayout,
					    [&](const int rowIndex) -> std::array<float, 4>
					    {
						    return MainPanelRowColors[static_cast<std::size_t>(
						        std::clamp(rowIndex, 0, static_cast<int>(MainPanelRowColors.size()) - 1))];
					    },
					    [&](const ScreenPoint& p0,
					        const ScreenPoint& p1,
					        const ScreenPoint& p2,
					        const float* color) -> void
					    {
						    appendTriangle(p0, p1, p2, color);
					    },textContext);
					const std::array<std::wstring_view, 9> mainPanelLabels{
						L"\u8ecc\u9053", // trajectory
						L"\u5bfe\u6226", // duel
						L"\u30c6\u30a3\u30fc",
						m_showPitchControlPanel
						    ? L"\u6295\u7403UI \u6551\u6e08"
						    : L"\u6295\u7403UI \u958b\u304f",
						m_showMetricsPanel
						    ? L"\u6295\u7403\u6307\u6a19 \u6551\u6e08"
						    : L"\u6295\u7403\u6307\u6a19 \u958b\u304f",
						m_showBattingMetricsPanel
						    ? L"\u6253\u7403\u6307\u6a19 \u6551\u6e08"
						    : L"\u6253\u7403\u6307\u6a19 \u958b\u304f",
						PitchTrajectoryDisplayLabel(m_overlayVisibility.pitchTrajectoryDisplayMode),
						m_overlayVisibility.showBattedTrajectory ? L"打球跡 隠" : L"打球跡 表",
						calibrationUi.buttonLabel
					};
					EmitProjectedOrientedPanelTextOverlay(
					    projectionState,
					    width,
					    height,
					    mainPanelLayout,
					    static_cast<int>(mainPanelLabels.size()),
					    20,
					    0.86,
					    0.62,
					    [&](const int labelIndex) -> std::wstring_view
					    {
						    return mainPanelLabels[static_cast<std::size_t>(labelIndex)];
					    },
					    [&](const ScreenPoint& p0,
					        const ScreenPoint& p1,
					        const ScreenPoint& p2,
					        const float* color) -> void
					    {
						    appendTriangle(p0, p1, p2, color);
					    },textContext);
					UiPanelInnerLayout calibrationInner{};
					if (TryBuildPanelInnerLayout(mainPanelLayout, &calibrationInner))
					{
						auto statusRect = Common::UiText::BuildPanelCellRect(calibrationInner.innerLeft,
						    calibrationInner.innerTop, calibrationInner.cellWidth, calibrationInner.cellHeight,
						    mainPanelLayout.cellGapMeters, mainPanelLayout.columns, 9);
						statusRect.right = calibrationInner.innerRight;
						const double middle = (statusRect.top + statusRect.bottom) * .5;
						auto instructionRect = statusRect;
						statusRect.bottom = middle;
						instructionRect.top = middle;
						const auto appendCalibrationLine = [&](const UiPanelLocalRect& rect, std::wstring_view text)
						{
							EmitProjectedOrientedPanelTextInRect(projectionState, width, height, mainPanelLayout,
							    rect, text, 16, .95, .75, 0.0, .5,
							    Common::DebugWorldStyle::MainControlPanelLabelColor,
							    [&](const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c, const float* color)
							    { appendTriangle(a, b, c, color); }, textContext);
						};
						appendCalibrationLine(statusRect, calibrationUi.statusLine);
						appendCalibrationLine(instructionRect, calibrationUi.instructionLine);
					}
				}

				if (m_showPitchControlPanel)
				{
					static constexpr std::array<float, 4> TransparentRowColor{
						0.0F,
						0.0F,
						0.0F,
						0.0F
					};
					static constexpr std::array<std::array<float, 4>, Common::PitchPanelRows> PitchPanelRowColors{
						TransparentRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelPresetRowColor,
						TransparentRowColor,
						Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor,
						TransparentRowColor,
						TransparentRowColor
					};
					EmitProjectedOrientedPanelOverlay(
					    projectionState,
					    width,
					    height,
					    pitchPanelLayout,
					    [&](const int rowIndex) -> std::array<float, 4>
					    {
						    return PitchPanelRowColors[static_cast<std::size_t>(
						        std::clamp(rowIndex, 0, static_cast<int>(PitchPanelRowColors.size()) - 1))];
					    },
					    [&](const ScreenPoint& p0,
					        const ScreenPoint& p1,
					        const ScreenPoint& p2,
					        const float* color) -> void
					    {
						    appendTriangle(p0, p1, p2, color);
					    },textContext);
					const std::array<float, 4> pitchTitleBarColor =
					    (m_subPanelDragTarget == SubPanelDragTarget::Pitch)
					        ? Common::DebugWorldStyle::MainControlPanelPresetRowColor
					        : Common::DebugWorldStyle::MainControlPanelPitchRowColor;
					EmitProjectedOrientedPanelTitleBarOverlay(
					    projectionState,
					    width,
					    height,
					    pitchPanelLayout,
					    pitchTitleBarColor,
					    Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor,
					    Common::DebugWorldStyle::MainControlPanelPresetRowColor,
					    [&](const ScreenPoint& p0,
					        const ScreenPoint& p1,
					        const ScreenPoint& p2,
					        const float* color) -> void
					    {
						    appendTriangle(p0, p1, p2, color);
					    },textContext);
					const double pitchCameraSide = Physics::Dot(
					    projectionState.position - pitchPanelLayout.center,
					    pitchPanelLayout.forwardAxis);
					const double pitchTowardCameraDepthSign =
					    (pitchCameraSide >= 0.0) ? 1.0 : -1.0;
					const std::array<float, 4> selectedZoneColor{
						0.78F,
						0.52F,
						0.18F,
						0.96F
					};
					for (int zoneIndex = 0; zoneIndex < PitchZoneCellCount; ++zoneIndex)
					{
						const std::uint32_t zoneBit =
						    static_cast<std::uint32_t>(1U) << zoneIndex;
						if ((m_uiTelemetry.duelTargetZoneMask & zoneBit) == 0U)
						{
							continue;
						}
						EmitProjectedOrientedPanelCellOverlay(
						    projectionState,
						    width,
						    height,
						    pitchPanelLayout,
						    PitchZoneCellStartIndex + zoneIndex,
						    selectedZoneColor,
						    0.0200 * pitchTowardCameraDepthSign,
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
					}

					if (m_pitchPanelMinimized)
					{
						const std::array<std::wstring, 5> pitchMinimizedLabels{
							L"\u6295\u7403UI",
							L"",
							L"",
							L"\u25a1",
							L"\u00d7"
						};
						EmitProjectedOrientedPanelTextOverlay(
						    projectionState,
						    width,
						    height,
						    pitchPanelLayout,
						    static_cast<int>(pitchMinimizedLabels.size()),
						    18,
						    0.86,
						    0.68,
						    [&](const int labelIndex) -> std::wstring_view
						    {
							    return pitchMinimizedLabels[static_cast<std::size_t>(labelIndex)];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
					}
					else
					{
						std::array<std::wstring, 45> pitchPanelLabels{
							L"\u6295\u7403UI",
							L"",
							L"",
							L"-",
							L"\u00d7",
							L"1",
							L"2",
							L"3",
							L"4",
							L"5",
							L"6",
							L"7",
							L"8",
							L"9",
							L"10",
							L"11",
							L"12",
							L"13",
							L"14",
							L"15",
							L"16",
							L"17",
							L"18",
							L"19",
							L"20",
							L"21",
							L"22",
							L"23",
							L"24",
							L"25",
							L"<",
							L"-",
							L">",
							m_uiTelemetry.playbackPaused ? L"\u518d\u751f"
							                             : L"\u4e00\u6642\u505c\u6b62",
							m_uiTelemetry.randomPitchProfileEnabled
							    ? L"Rnd On"
							    : L"Rnd Off",
							L"",
							L"",
							L"",
							L"",
							L"\u30be\u30fc\u30f3R",
							L"<",
							L"プリセット",
							L">",
							L"",
							L""
						};
						pitchPanelLabels[static_cast<std::size_t>(PitchTypeNameCellIndex)] =
						    m_uiTelemetry.pitchInformationVisible ? m_uiTelemetry.presetName : L"--";
						EmitProjectedOrientedPanelTextOverlay(
						    projectionState,
						    width,
						    height,
						    pitchPanelLayout,
						    static_cast<int>(pitchPanelLabels.size()),
						    16,
						    0.88,
						    0.70,
						    [&](const int labelIndex) -> std::wstring_view
						    {
							    return pitchPanelLabels[static_cast<std::size_t>(labelIndex)];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);

						UiPanelLocalRect presetNameRect{};
						if (Common::BuildPitchPresetNameRect(pitchPanelLayout,presetNameRect))
						{
							const std::wstring presetLabel = m_uiTelemetry.pitchSettingsFileCount > 0 &&
							    !m_uiTelemetry.pitchSettingsFileName.empty()
							    ? Common::BuildPitchPresetLabel(m_uiTelemetry.pitchSettingsFileName) : L"設定ファイルなし";
							double totalAdvance = 0.;
							for (const auto scalar : Common::UiText::DecodeUtf16(presetLabel))
								if (scalar != L'\n') totalAdvance += textContext.atlas.Metrics().Get(
								    scalar,Common::UiText::GlyphAtlas::FontPixelHeight).advance;
							const double lineHeight = textContext.atlas.Metrics().Get(
							    L' ',Common::UiText::GlyphAtlas::FontPixelHeight).lineHeight;
							const double aspect = (presetNameRect.right - presetNameRect.left) * .94 /
							    ((presetNameRect.top - presetNameRect.bottom) * .88);
							const double maximumAdvance = (std::max)(
							    14. * Common::UiText::GlyphAtlas::FontPixelHeight,
							    std::sqrt(totalAdvance * lineHeight * aspect));
							const auto wrappedLabel = Common::WrapPitchPresetLabel(presetLabel,maximumAdvance,
							    [&](std::uint32_t scalar) { return textContext.atlas.Metrics().Get(
							        scalar,Common::UiText::GlyphAtlas::FontPixelHeight).advance; });
							EmitProjectedOrientedPanelTextInRect(projectionState,width,height,pitchPanelLayout,
							    presetNameRect,wrappedLabel,16,.94,.88,.5,.5,
							    Common::DebugWorldStyle::MainControlPanelLabelColor,
							    [&](const ScreenPoint& p0, const ScreenPoint& p1, const ScreenPoint& p2, const float* color)
							    { appendTriangle(p0,p1,p2,color); },textContext);
						}

						UiPanelLocalRect sliderTrackRect{};
						UiPanelLocalRect sliderHitRect{};
						if (TryBuildPitchPlaybackSliderRects(
						        pitchPanelLayout,
						        PitchPlaybackSliderCellStartIndex,
						        PitchPlaybackSliderCellStartIndex +
						            PitchPlaybackSliderCellCount - 1,
						        &sliderTrackRect,
						        &sliderHitRect))
						{
							const double playbackNormalized = std::clamp(
							    m_uiTelemetry.playbackSpeedPercent / 200.0,
							    0.0,
							    1.0);
							const std::array<float, 4> sliderTrackColor{
								0.10F,
								0.14F,
								0.24F,
								0.96F
							};
							const std::array<float, 4> sliderFillColor{
								0.24F,
								0.68F,
								0.92F,
								0.98F
							};
							const std::array<float, 4> sliderThumbColor{
								0.95F,
								0.78F,
								0.28F,
								1.0F
							};
							const std::array<float, 4> sliderTextColor{
								1.0F,
								1.0F,
								1.0F,
								1.0F
							};
							EmitProjectedOrientedPanelRectOverlay(
							    projectionState,
							    width,
							    height,
							    pitchPanelLayout,
							    sliderTrackRect,
							    sliderTrackColor,
							    0.0185 * pitchTowardCameraDepthSign,
							    [&](const ScreenPoint& p0,
							        const ScreenPoint& p1,
							        const ScreenPoint& p2,
							        const float* color) -> void
							    {
								    appendTriangle(p0, p1, p2, color);
							    },textContext);

							UiPanelLocalRect sliderFillRect = sliderTrackRect;
							sliderFillRect.right = sliderTrackRect.left +
							                       ((sliderTrackRect.right -
							                         sliderTrackRect.left) *
							                        playbackNormalized);
							if (sliderFillRect.right > sliderFillRect.left)
							{
								EmitProjectedOrientedPanelRectOverlay(
								    projectionState,
								    width,
								    height,
								    pitchPanelLayout,
								    sliderFillRect,
								    sliderFillColor,
								    0.0195 * pitchTowardCameraDepthSign,
								    [&](const ScreenPoint& p0,
								        const ScreenPoint& p1,
								        const ScreenPoint& p2,
								        const float* color) -> void
								    {
									    appendTriangle(p0, p1, p2, color);
								    },textContext);
							}

							const double trackWidth =
							    sliderTrackRect.right - sliderTrackRect.left;
							const double thumbCenterX = sliderTrackRect.left +
							                            (trackWidth *
							                             playbackNormalized);
							const double thumbHalfWidth = std::clamp(
							    trackWidth * 0.015,
							    0.0022,
							    0.0065);
							const double thumbHalfHeight =
							    (sliderTrackRect.top - sliderTrackRect.bottom) * 1.8;
							UiPanelLocalRect sliderThumbRect{
								thumbCenterX - thumbHalfWidth,
								sliderTrackRect.top + thumbHalfHeight,
								thumbCenterX + thumbHalfWidth,
								sliderTrackRect.bottom - thumbHalfHeight
							};
							EmitProjectedOrientedPanelRectOverlay(
							    projectionState,
							    width,
							    height,
							    pitchPanelLayout,
							    sliderThumbRect,
							    sliderThumbColor,
							    0.0205 * pitchTowardCameraDepthSign,
							    [&](const ScreenPoint& p0,
							        const ScreenPoint& p1,
							        const ScreenPoint& p2,
							        const float* color) -> void
							    {
								    appendTriangle(p0, p1, p2, color);
							    },textContext);

							std::wostringstream sliderValueLabel{};
							sliderValueLabel << std::fixed << std::setprecision(0);
							sliderValueLabel << L"\u901f\u5ea6 "
							                 << m_uiTelemetry.playbackSpeedPercent
							                 << L"%";
							const double labelBandInsetY =
							    std::clamp((sliderHitRect.top - sliderHitRect.bottom) * 0.04,
							               0.0008,
							               0.0030);
							UiPanelLocalRect sliderValueRect{
								sliderHitRect.left,
								sliderHitRect.top,
								sliderHitRect.right,
								sliderTrackRect.top + labelBandInsetY
							};
							UiPanelLocalRect sliderRangeRect{
								sliderHitRect.left,
								sliderTrackRect.bottom - labelBandInsetY,
								sliderHitRect.right,
								sliderHitRect.bottom
							};
							UiPanelLocalRect sliderMinRect = sliderRangeRect;
							sliderMinRect.right =
							    sliderMinRect.left +
							    ((sliderMinRect.right - sliderMinRect.left) * 0.34);
							UiPanelLocalRect sliderMaxRect = sliderRangeRect;
							sliderMaxRect.left =
							    sliderMaxRect.left +
							    ((sliderMaxRect.right - sliderMaxRect.left) * 0.66);
							EmitProjectedOrientedPanelTextInRect(
							    projectionState,
							    width,
							    height,
							    pitchPanelLayout,
							    sliderValueRect,
							    sliderValueLabel.str(),
							    14,
							    0.94,
							    0.82,
							    0.5,
							    0.5,
							    sliderTextColor,
							    [&](const ScreenPoint& p0,
							        const ScreenPoint& p1,
							        const ScreenPoint& p2,
							        const float* color) -> void
							    {
								    appendTriangle(p0, p1, p2, color);
							    },textContext);
							EmitProjectedOrientedPanelTextInRect(
							    projectionState,
							    width,
							    height,
							    pitchPanelLayout,
							    sliderMinRect,
							    L"0%",
							    13,
							    0.90,
							    0.80,
							    0.0,
							    0.5,
							    sliderTextColor,
							    [&](const ScreenPoint& p0,
							        const ScreenPoint& p1,
							        const ScreenPoint& p2,
							        const float* color) -> void
							    {
								    appendTriangle(p0, p1, p2, color);
							    },textContext);
							EmitProjectedOrientedPanelTextInRect(
							    projectionState,
							    width,
							    height,
							    pitchPanelLayout,
							    sliderMaxRect,
							    L"200%",
							    13,
							    0.90,
							    0.80,
							    1.0,
							    0.5,
							    sliderTextColor,
							    [&](const ScreenPoint& p0,
							        const ScreenPoint& p1,
							        const ScreenPoint& p2,
							        const float* color) -> void
							    {
								    appendTriangle(p0, p1, p2, color);
							    },textContext);
						}
					}
				}

				if (m_showMetricsPanel || m_showBattingMetricsPanel)
				{
					const DerivedFlightMetrics derivedMetrics =
					    BuildDerivedFlightMetrics(
					        m_trajectoryTrails.Pitch().Points(),
					        m_uiTelemetry.pitchDistanceMeters);
					const double pitchMetricsSpeedMetersPerSecond =
					    m_uiTelemetry.pitchMetricsSpeedMetersPerSecond;
					const double pitchMetricsSpeedKilometersPerHour =
					    pitchMetricsSpeedMetersPerSecond * 3.6;
					const Physics::Vector3 horizontalSpinAxis{
						m_latestBallState.spinAxis.x,
						0.0,
						m_latestBallState.spinAxis.z
					};
					const double spinAxisHorizontalMagnitude =
					    Physics::Length(horizontalSpinAxis);
					const double spinAxisTiltDegrees = std::atan2(
					                                       m_latestBallState.spinAxis.y,
					                                       spinAxisHorizontalMagnitude) *
					                                   RadiansToDegrees;
					const double spinAxisDirectionDegrees = std::atan2(
					                                            m_latestBallState.spinAxis.x,
					                                            m_latestBallState.spinAxis.z) *
					                                        RadiansToDegrees;
					std::wstring verticalBreakLabel = L"\u7e26--";
					std::wstring horizontalBreakLabel = L"\u6a2a--";
					std::wstring zoneLabel = L"\u30be\u30fc\u30f3--";
					if (derivedMetrics.hasZonePassPoint)
					{
						std::wostringstream verticalBreak{};
						verticalBreak << std::fixed << std::setprecision(2);
						verticalBreak << L"\u7e26"
						              << derivedMetrics.verticalBreakMeters << L"m";
						verticalBreakLabel = verticalBreak.str();

						std::wostringstream horizontalBreak{};
						horizontalBreak << std::fixed << std::setprecision(2);
						horizontalBreak << L"\u6a2a"
						                << derivedMetrics.horizontalBreakMeters << L"m";
						horizontalBreakLabel = horizontalBreak.str();
						zoneLabel = derivedMetrics.isZoneInStrike
						                ? L"\u30be\u30fc\u30f3 IN"
						                : L"\u30be\u30fc\u30f3 OUT";
					}

					const std::wstring_view pitchTrajectoryModeLabel =
					    PitchTrajectoryDisplayLabel(m_overlayVisibility.pitchTrajectoryDisplayMode);
					const std::wstring battedTrajectoryToggleLabel =
					    m_overlayVisibility.showBattedTrajectory ? L"打球跡 隠" : L"打球跡 表";
					const std::wstring_view zoneModeLabel =
					    StrikeZoneDisplayLabel(m_overlayVisibility.strikeZoneDisplayMode);
					const std::wstring moundToggleLabel =
					    m_uiTelemetry.moundHeightOffsetEnabled
					        ? L"\u30de\u30a6\u30f3\u30c9 Off"
					        : L"\u30de\u30a6\u30f3\u30c9 On";
					const std::wstring duelPreviewToggleLabel =
					    m_uiTelemetry.duelPitchPreviewEnabled
					        ? L"\u5148\u8aad\u307f \u96a0"
					        : L"\u5148\u8aad\u307f \u8868";
					const std::wstring batHandToggleLabel =
					    m_uiTelemetry.batUseRightHand
					        ? L"\u6301\u3061\u624b \u53f3"
					        : L"\u6301\u3061\u624b \u5de6";
					const std::wstring batAngleDownLabel = L"\u89d2\u5ea6 -";
					const std::wstring batAngleUpLabel = L"\u89d2\u5ea6 +";
					const std::wstring batYawDownLabel = L"\u5411\u304d -";
					const std::wstring batYawUpLabel = L"\u5411\u304d +";
					const std::wstring batMaterialLabel = m_uiTelemetry.batMaterial == Physics::BatMaterial::Wood
					    ? L"材質 木製" : m_uiTelemetry.batMaterial == Physics::BatMaterial::Metal
					    ? L"材質 金属" : L"材質 ウレタン";
					std::wstring landingLabel = L"\u7740\u5f3e --";
					if (m_uiTelemetry.hasBattedMetrics &&
					    m_uiTelemetry.battedMetricsHasGroundContact)
					{
						std::wostringstream landingStream{};
						landingStream << std::fixed << std::setprecision(1);
						landingStream << L"\u7740\u5f3e "
						              << m_uiTelemetry.battedFirstGroundDistanceMeters
						              << L"m";
						landingLabel = landingStream.str();
					}
					std::wstring directionLaunchLine = L"\u65b9\u5411 -- / \u6253\u89d2 --";
					std::wstring carryApexLine = L"\u98db\u8ddd -- / \u6700\u9ad8 --";
					if (m_uiTelemetry.hasBattedMetrics)
					{
						const Physics::Vector3 launchVelocity =
						    m_uiTelemetry.battedLaunchVelocity;
						const double horizontalSpeed = std::sqrt(
						    (launchVelocity.x * launchVelocity.x) +
						    (launchVelocity.z * launchVelocity.z));
						const double directionDegreesFromPitcher =
						    (horizontalSpeed > 1e-6)
						        ? (std::atan2(
						               launchVelocity.x,
						               -launchVelocity.z) *
						           RadiansToDegrees)
						        : 0.0;
						const double launchAngleDegrees = std::atan2(
						                                      launchVelocity.y,
						                                      (std::max)(1e-6, horizontalSpeed)) *
						                                  RadiansToDegrees;
						std::wostringstream directionLaunchStream{};
						directionLaunchStream << std::fixed << std::setprecision(1);
						directionLaunchStream << L"\u65b9\u5411 ";
						const double directionAbsDegrees =
						    std::abs(directionDegreesFromPitcher);
						if (directionAbsDegrees <= 0.05)
						{
							directionLaunchStream << L"\u771f\u3063\u76f4\u3050 0.0\u00b0";
						}
						else
						{
							directionLaunchStream << ((directionDegreesFromPitcher > 0.0)
							                              ? L"\u53f3 "
							                              : L"\u5de6 ")
							                      << directionAbsDegrees << L"\u00b0";
						}
						directionLaunchStream << L" / \u6253\u89d2 "
						                      << launchAngleDegrees << L"\u00b0";
						directionLaunchLine = directionLaunchStream.str();

						std::wostringstream carryApexStream{};
						carryApexStream << std::fixed << std::setprecision(1);
						carryApexStream << L"\u98db\u8ddd ";
						if (m_uiTelemetry.battedMetricsHasGroundContact)
						{
							carryApexStream
							    << m_uiTelemetry.battedFirstGroundDistanceMeters << L"m";
						}
						else
						{
							carryApexStream << L"--";
						}
						carryApexStream << L" / \u6700\u9ad8 "
						                << m_uiTelemetry.battedMaxHeightMeters << L"m";
						carryApexLine = carryApexStream.str();
					}
					std::wostringstream pitchTypeSpeedLine{};
					pitchTypeSpeedLine << std::fixed << std::setprecision(0);
					pitchTypeSpeedLine << L"\u7403\u7a2e " << m_uiTelemetry.presetName
					                   << L" / \u7403\u901f "
					                   << pitchMetricsSpeedKilometersPerHour
					                   << L"km/h";
					std::wostringstream spinAxisLine{};
					spinAxisLine << std::fixed << std::setprecision(0);
					spinAxisLine << L"\u56de\u8ee2 " << m_latestBallState.spinRpm
					             << L"rpm / \u8ef8V " << spinAxisTiltDegrees
					             << L"\u00b0 H " << spinAxisDirectionDegrees << L"\u00b0";
					const std::wstring pitchBreakLine =
					    verticalBreakLabel + L" / " + horizontalBreakLabel;
					const std::wstring pitchZoneLine = zoneLabel;
					std::wstring battedSpeedLine = L"\u6253\u7403\u901f\u5ea6 --";
					if (m_uiTelemetry.hasBattedMetrics)
					{
						const Physics::Vector3 launchVelocity =
						    m_uiTelemetry.battedLaunchVelocity;
						const double battedSpeedKilometersPerHour =
						    std::sqrt(
						        (launchVelocity.x * launchVelocity.x) +
						        (launchVelocity.y * launchVelocity.y) +
						        (launchVelocity.z * launchVelocity.z)) *
						    3.6;
						std::wostringstream battedSpeedStream{};
						battedSpeedStream << std::fixed << std::setprecision(0);
						battedSpeedStream << L"\u6253\u7403\u901f\u5ea6 "
						                  << battedSpeedKilometersPerHour
						                  << L"km/h";
						battedSpeedLine = battedSpeedStream.str();
					}
					std::wostringstream strikeZoneBounds;
					strikeZoneBounds << std::fixed << std::setprecision(0)
					    << L"下限 " << m_uiTelemetry.strikeZoneBottomMeters * 100. << L"cm / 上限 "
					    << m_uiTelemetry.strikeZoneTopMeters * 100. << L"cm / 高さ "
					    << m_uiTelemetry.strikeZoneHeightMeters * 100. << L"cm";
					const std::array<std::wstring, 6> pitchingMetricRows{
						m_uiTelemetry.pitchInformationVisible ? pitchTypeSpeedLine.str() : L"球種 -- / 球速 --",
						m_uiTelemetry.pitchInformationVisible ? spinAxisLine.str() : L"回転 -- / 軸 --",
						m_uiTelemetry.pitchInformationVisible ? pitchBreakLine : L"縦 -- / 横 --",
						m_uiTelemetry.pitchInformationVisible ? pitchZoneLine : L"ゾーン --",
						Common::BuildStrikeZoneCalibrationUi(m_uiTelemetry.strikeZoneCalibration).statusLine,
						strikeZoneBounds.str()
					};
					const std::array<std::wstring, 4> battingMetricRows{
						battedSpeedLine,
						directionLaunchLine,
						carryApexLine,
						landingLabel
					};

					if (m_showMetricsPanel)
					{
						static constexpr std::array<float, 4> TransparentRowColor{
							0.0F,
							0.0F,
							0.0F,
							0.0F
						};
						static constexpr std::array<std::array<float, 4>, 8>
						    MetricsPanelRowColors{
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor
						    };
						EmitProjectedOrientedPanelOverlay(
						    projectionState,
						    width,
						    height,
						    metricsPanelLayout,
						    [&](const int rowIndex) -> std::array<float, 4>
						    {
							    return MetricsPanelRowColors[static_cast<std::size_t>(
							        std::clamp(
							            rowIndex,
							            0,
							            static_cast<int>(MetricsPanelRowColors.size()) - 1))];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
						const std::array<float, 4> metricsTitleBarColor =
						    (m_subPanelDragTarget == SubPanelDragTarget::Metrics)
						        ? Common::DebugWorldStyle::MainControlPanelPresetRowColor
						        : Common::DebugWorldStyle::MainControlPanelPitchRowColor;
						EmitProjectedOrientedPanelTitleBarOverlay(
						    projectionState,
						    width,
						    height,
						    metricsPanelLayout,
						    metricsTitleBarColor,
						    Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor,
						    Common::DebugWorldStyle::MainControlPanelPresetRowColor,
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
						const std::array<std::wstring, 5> metricsTitleLabels{
							L"\u6295\u7403\u6307\u6a19",
							L"",
							L"",
							m_metricsPanelMinimized ? L"\u25a1" : L"-",
							L"\u00d7"
						};
						EmitProjectedOrientedPanelTextOverlay(
						    projectionState,
						    width,
						    height,
						    metricsPanelLayout,
						    static_cast<int>(metricsTitleLabels.size()),
						    18,
						    0.86,
						    0.68,
						    [&](const int labelIndex) -> std::wstring_view
						    {
							    return metricsTitleLabels[static_cast<std::size_t>(labelIndex)];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);

						if (!m_metricsPanelMinimized)
						{
							UiPanelLocalRect metricsBodyRect{};
							std::array<UiPanelLocalRect, MetricsControlButtonCount> metricsControlButtonRects{};
							if (TryBuildMetricsPanelBodyLayout(
							        metricsPanelLayout,
							        &metricsBodyRect,
							        &metricsControlButtonRects))
							{
								const double metricsCameraSide = Physics::Dot(
								    projectionState.position - metricsPanelLayout.center,
								    metricsPanelLayout.forwardAxis);
								const double metricsTowardCameraDepthSign =
								    (metricsCameraSide >= 0.0) ? 1.0 : -1.0;
								const double rowGap = std::clamp(
								    metricsPanelLayout.cellGapMeters * 0.75,
								    0.0015,
								    0.0065);
								const double bodyHeight =
								    metricsBodyRect.top - metricsBodyRect.bottom;
								const double rowHeight =
								    (bodyHeight -
								     (rowGap *
								      static_cast<double>(pitchingMetricRows.size() - 1U))) /
								    static_cast<double>(pitchingMetricRows.size());
								if (rowHeight > 1e-6)
								{
									for (std::size_t rowIndex = 0;
									     rowIndex < pitchingMetricRows.size();
									     ++rowIndex)
									{
										const double rowTop =
										    metricsBodyRect.top -
										    (static_cast<double>(rowIndex) *
										     (rowHeight + rowGap));
										const double rowBottom = rowTop - rowHeight;
										UiPanelLocalRect rowRect{
											metricsBodyRect.left,
											rowTop,
											metricsBodyRect.right,
											rowBottom
										};
										const std::array<float, 4> rowBackgroundColor =
										    ((rowIndex % 2U) == 0U)
										        ? std::array<float, 4>{ 0.06F, 0.10F, 0.17F, 0.56F }
										        : std::array<float, 4>{ 0.05F, 0.08F, 0.14F, 0.38F };
										EmitProjectedOrientedPanelRectOverlay(
										    projectionState,
										    width,
										    height,
										    metricsPanelLayout,
										    rowRect,
										    rowBackgroundColor,
										    0.0175 * metricsTowardCameraDepthSign,
										    [&](const ScreenPoint& p0,
										        const ScreenPoint& p1,
										        const ScreenPoint& p2,
										        const float* color) -> void
										    {
											    appendTriangle(p0, p1, p2, color);
										    },textContext);
										UiPanelLocalRect rowTextRect = rowRect;
										rowTextRect.left += 0.0045;
										rowTextRect.right -= 0.0030;
										EmitProjectedOrientedPanelTextInRect(
										    projectionState,
										    width,
										    height,
										    metricsPanelLayout,
										    rowTextRect,
										    pitchingMetricRows[rowIndex],
										    11,
										    0.98,
										    0.80,
										    0.0,
										    0.5,
										    Common::DebugWorldStyle::MainControlPanelLabelColor,
										    [&](const ScreenPoint& p0,
										        const ScreenPoint& p1,
										        const ScreenPoint& p2,
										        const float* color) -> void
										    {
											    appendTriangle(p0, p1, p2, color);
										    },textContext);
									}
								}

								const std::array<std::wstring_view, MetricsControlButtonCount> metricsButtonLabels{
									pitchTrajectoryModeLabel,
									zoneModeLabel,
									moundToggleLabel,
									duelPreviewToggleLabel,
									batHandToggleLabel,
									batAngleDownLabel,
									batAngleUpLabel,
									batYawDownLabel,
									batYawUpLabel,
									L"\u4f4d\u7f6eR",
									L"校正",
									battedTrajectoryToggleLabel,
									batMaterialLabel,
									L"下限−",
									L"下限＋",
									L"上限−",
									L"上限＋"
								};
								for (std::size_t buttonIndex = 0;
								     buttonIndex < metricsControlButtonRects.size();
								     ++buttonIndex)
								{
									const bool isActiveButton =
									    ((buttonIndex == 0U) &&
									     m_overlayVisibility.pitchTrajectoryDisplayMode != Common::PitchTrajectoryDisplayMode::None) ||
									    ((buttonIndex == 11U) &&
									     m_overlayVisibility.showBattedTrajectory) ||
									    ((buttonIndex == 1U) &&
									     m_overlayVisibility.strikeZoneDisplayMode != Common::StrikeZoneDisplayMode::None) ||
									    ((buttonIndex == 2U) &&
									     m_uiTelemetry.moundHeightOffsetEnabled) ||
									    ((buttonIndex == 3U) &&
									     m_uiTelemetry.duelPitchPreviewEnabled) ||
									    ((buttonIndex == 4U) &&
									     m_uiTelemetry.batUseRightHand) ||
									    ((buttonIndex == 5U) &&
									     (m_uiTelemetry.batAngleOffsetDegrees < -0.5)) ||
									    ((buttonIndex == 6U) &&
									     (m_uiTelemetry.batAngleOffsetDegrees > 0.5)) ||
									    ((buttonIndex == 7U) &&
									     (m_uiTelemetry.batYawOffsetDegrees < -0.5)) ||
									    ((buttonIndex == 8U) &&
									     (m_uiTelemetry.batYawOffsetDegrees > 0.5));
									const std::array<float, 4> buttonColor =
									    isActiveButton
									        ? std::array<float, 4>{ 0.22F, 0.56F, 0.82F, 0.95F }
									        : std::array<float, 4>{ 0.14F, 0.19F, 0.30F, 0.90F };
									EmitProjectedOrientedPanelRectOverlay(
									    projectionState,
									    width,
									    height,
									    metricsPanelLayout,
									    metricsControlButtonRects[buttonIndex],
									    buttonColor,
									    0.0188 * metricsTowardCameraDepthSign,
									    [&](const ScreenPoint& p0,
									        const ScreenPoint& p1,
									        const ScreenPoint& p2,
									        const float* color) -> void
									    {
										    appendTriangle(p0, p1, p2, color);
									    },textContext);
									EmitProjectedOrientedPanelTextInRect(
									    projectionState,
									    width,
									    height,
									    metricsPanelLayout,
									    metricsControlButtonRects[buttonIndex],
									    metricsButtonLabels[buttonIndex],
									    12,
									    0.90,
									    0.82,
									    0.5,
									    0.5,
									    Common::DebugWorldStyle::MainControlPanelLabelColor,
									    [&](const ScreenPoint& p0,
									        const ScreenPoint& p1,
									        const ScreenPoint& p2,
									        const float* color) -> void
									    {
										    appendTriangle(p0, p1, p2, color);
									    },textContext);
								}
							}
						}
					}

					if (m_showBattingMetricsPanel)
					{
						static constexpr std::array<float, 4> TransparentRowColor{
							0.0F,
							0.0F,
							0.0F,
							0.0F
						};
						static constexpr std::array<std::array<float, 4>, 8>
						    BattingMetricsPanelRowColors{
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor,
							    TransparentRowColor
						    };
						EmitProjectedOrientedPanelOverlay(
						    projectionState,
						    width,
						    height,
						    battingMetricsPanelLayout,
						    [&](const int rowIndex) -> std::array<float, 4>
						    {
							    return BattingMetricsPanelRowColors[static_cast<std::size_t>(
							        std::clamp(
							            rowIndex,
							            0,
							            static_cast<int>(BattingMetricsPanelRowColors.size()) - 1))];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
						const std::array<float, 4> battingMetricsTitleBarColor =
						    (m_subPanelDragTarget == SubPanelDragTarget::BattingMetrics)
						        ? Common::DebugWorldStyle::MainControlPanelPresetRowColor
						        : Common::DebugWorldStyle::MainControlPanelPitchRowColor;
						EmitProjectedOrientedPanelTitleBarOverlay(
						    projectionState,
						    width,
						    height,
						    battingMetricsPanelLayout,
						    battingMetricsTitleBarColor,
						    Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor,
						    Common::DebugWorldStyle::MainControlPanelPresetRowColor,
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
						const std::array<std::wstring, 5> battingTitleLabels{
							L"\u6253\u7403\u6307\u6a19",
							L"",
							L"",
							m_battingMetricsPanelMinimized ? L"\u25a1" : L"-",
							L"\u00d7"
						};
						EmitProjectedOrientedPanelTextOverlay(
						    projectionState,
						    width,
						    height,
						    battingMetricsPanelLayout,
						    static_cast<int>(battingTitleLabels.size()),
						    18,
						    0.86,
						    0.68,
						    [&](const int labelIndex) -> std::wstring_view
						    {
							    return battingTitleLabels[static_cast<std::size_t>(labelIndex)];
						    },
						    [&](const ScreenPoint& p0,
						        const ScreenPoint& p1,
						        const ScreenPoint& p2,
						        const float* color) -> void
						    {
							    appendTriangle(p0, p1, p2, color);
						    },textContext);
						if (!m_battingMetricsPanelMinimized)
						{
							UiPanelInnerLayout innerLayout{};
							if (TryBuildPanelInnerLayout(
							        battingMetricsPanelLayout,
							        &innerLayout))
							{
								const double bodyGap = std::clamp(
								    battingMetricsPanelLayout.cellGapMeters * 0.75,
								    0.0020,
								    0.0080);
								UiPanelLocalRect battingBodyRect{
									innerLayout.innerLeft,
									innerLayout.innerTop - innerLayout.cellHeight - bodyGap,
									innerLayout.innerRight,
									innerLayout.innerBottom + bodyGap
								};
								const double battingCameraSide = Physics::Dot(
								    projectionState.position -
								        battingMetricsPanelLayout.center,
								    battingMetricsPanelLayout.forwardAxis);
								const double battingTowardCameraDepthSign =
								    (battingCameraSide >= 0.0) ? 1.0 : -1.0;
								const double rowGap = std::clamp(
								    battingMetricsPanelLayout.cellGapMeters * 0.75,
								    0.0015,
								    0.0065);
								const double bodyHeight =
								    battingBodyRect.top - battingBodyRect.bottom;
								const double rowHeight =
								    (bodyHeight -
								     (rowGap *
								      static_cast<double>(battingMetricRows.size() - 1U))) /
								    static_cast<double>(battingMetricRows.size());
								if (rowHeight > 1e-6)
								{
									for (std::size_t rowIndex = 0;
									     rowIndex < battingMetricRows.size();
									     ++rowIndex)
									{
										const double rowTop =
										    battingBodyRect.top -
										    (static_cast<double>(rowIndex) *
										     (rowHeight + rowGap));
										const double rowBottom = rowTop - rowHeight;
										UiPanelLocalRect rowRect{
											battingBodyRect.left,
											rowTop,
											battingBodyRect.right,
											rowBottom
										};
										const std::array<float, 4> rowBackgroundColor =
										    ((rowIndex % 2U) == 0U)
										        ? std::array<float, 4>{ 0.06F, 0.10F, 0.17F, 0.56F }
										        : std::array<float, 4>{ 0.05F, 0.08F, 0.14F, 0.38F };
										EmitProjectedOrientedPanelRectOverlay(
										    projectionState,
										    width,
										    height,
										    battingMetricsPanelLayout,
										    rowRect,
										    rowBackgroundColor,
										    0.0175 * battingTowardCameraDepthSign,
										    [&](const ScreenPoint& p0,
										        const ScreenPoint& p1,
										        const ScreenPoint& p2,
										        const float* color) -> void
										    {
											    appendTriangle(p0, p1, p2, color);
										    },textContext);
										UiPanelLocalRect rowTextRect = rowRect;
										rowTextRect.left += 0.0045;
										rowTextRect.right -= 0.0030;
										EmitProjectedOrientedPanelTextInRect(
										    projectionState,
										    width,
										    height,
										    battingMetricsPanelLayout,
										    rowTextRect,
										    battingMetricRows[rowIndex],
										    11,
										    0.98,
										    0.80,
										    0.0,
										    0.5,
										    Common::DebugWorldStyle::MainControlPanelLabelColor,
										    [&](const ScreenPoint& p0,
										        const ScreenPoint& p1,
										        const ScreenPoint& p2,
										        const float* color) -> void
										    {
											    appendTriangle(p0, p1, p2, color);
										    },textContext);
									}
								}
							}
						}
					}
				}
			}

			if (m_hasResultPanelLaser)
			{
				ScreenPoint laserStart{};
				ScreenPoint laserEnd{};
				if (Common::TryProjectWorldToScreen(
				        m_resultPanelLaserOrigin,
				        projectionState,
				        width,
				        height,
				        &laserStart,
				        false) &&
				    Common::TryProjectWorldToScreen(
				        m_resultPanelLaserTarget,
				        projectionState,
				        width,
				        height,
				        &laserEnd,
				        false))
				{
					laserStart.depth = Common::DebugWorldLayout::UiLaserOverlayDepth;
					laserEnd.depth = Common::DebugWorldLayout::UiLaserOverlayDepth;
					appendLine(
					    laserStart,
					    laserEnd,
					    Common::DebugWorldStyle::UiLaserHalfSizePixels,
					    Common::DebugWorldStyle::UiLaserColor.data());
					appendMarker(
					    laserEnd,
					    Common::DebugWorldStyle::UiLaserHitHalfSizePixels,
					    Common::DebugWorldStyle::UiLaserHitColor.data());
				}
			}

			if (!hasVisibleCoverage)
			{
				m_trajectoryTrails.ForEachVisible(m_overlayVisibility, [&](const Common::TrajectoryRenderSpan& span) {
					auto color = Common::DebugWorldStyle::TrailColor;
					color[3] = span.persistentZone ? 1.0F : color[3] * span.fadeScale;
					for (const Physics::Vector3& trailPoint : span.points)
					{
						const ScreenPoint screenPoint =
						    Common::ProjectLegacyWorldToScreen(trailPoint, width, height);
						appendMarker(screenPoint, Common::DebugWorldStyle::TrailHalfSizePixels, color.data());
					}
				});

				const ScreenPoint ballScreenPoint =
				    Common::ProjectLegacyWorldToScreen(
				        m_latestBallState.position,
				        width,
				        height);
				if (m_fieldPresentation.ballVisible) appendMarker(
				    ballScreenPoint,
				    Common::LegacyBallMarkerHalfSizePixels(m_latestBallState.properties.radiusMeters),
				    Common::DebugWorldStyle::BallColor.data());
				if (hasVisibleCoverage)
				{
					m_lastProjectionLayerStatus = L"legacy-fallback";
				}
			}

			if (!hasVisibleCoverage)
			{
				const double centerX =
				    0.5 * static_cast<double>((std::max)(1L, width - 1));
				const double centerY =
				    0.5 * static_cast<double>((std::max)(1L, height - 1));
				const double reticleHalfSpan =
				    (std::max)(4.0,
				               (std::min)(static_cast<double>(width),
				                          static_cast<double>(height)) *
				                   0.08);
				constexpr float kFallbackDepth = 0.05F;
				const ScreenPoint centerPoint{ centerX, centerY, kFallbackDepth };
				const ScreenPoint horizontalStart{
					centerX - reticleHalfSpan,
					centerY,
					kFallbackDepth
				};
				const ScreenPoint horizontalEnd{
					centerX + reticleHalfSpan,
					centerY,
					kFallbackDepth
				};
				const ScreenPoint verticalStart{
					centerX,
					centerY - reticleHalfSpan,
					kFallbackDepth
				};
				const ScreenPoint verticalEnd{
					centerX,
					centerY + reticleHalfSpan,
					kFallbackDepth
				};

				appendLine(
				    horizontalStart,
				    horizontalEnd,
				    Common::DebugWorldStyle::WorldLineHalfSizePixels,
				    Common::DebugWorldStyle::ZoneColor.data());
				appendLine(
				    verticalStart,
				    verticalEnd,
				    Common::DebugWorldStyle::WorldLineHalfSizePixels,
				    Common::DebugWorldStyle::ZoneColor.data());
				appendMarker(
				    centerPoint,
				    Common::DebugWorldStyle::BallHalfSizePixels,
				    Common::DebugWorldStyle::PlateColor.data());

				m_lastProjectionLayerStatus =
				    hasVisibleCoverage ? L"center-fallback" : L"overlay-empty";
			}

			const std::size_t requiredVertices=opaqueVertices.size();
			constexpr std::size_t maximumVertices=(std::numeric_limits<UINT>::max)()/sizeof(Common::DebugOverlay::Vertex);
			if(requiredVertices>maximumVertices) {
			    m_lastProjectionLayerStatus=L"vertex-count-exceeds-d3d12-buffer-limit";
			    closeCommandListOnFailure();
			    return false;
			}
			if(requiredVertices>activeVertexCapacity) {
			    std::size_t capacity=(std::max)(static_cast<std::size_t>(activeVertexCapacity),static_cast<std::size_t>(MaxDebugVertices));
			    while(capacity<requiredVertices) capacity=(std::min)(capacity*2,maximumVertices);
			    Microsoft::WRL::ComPtr<ID3D12Resource> grownBuffer;
			    D3D12_VERTEX_BUFFER_VIEW grownView{};
			    UINT grownCapacity{};
			    if(!Common::DebugOverlay::InitializeVertexBuffer(m_d3d12Device.Get(),static_cast<UINT>(capacity),
			        &grownBuffer,&grownView,&grownCapacity)) {
			        m_lastProjectionLayerStatus=L"ui-vertex-buffer-growth-failed";
			        closeCommandListOnFailure();
			        return false;
			    }
			    frame.vertices=std::move(grownBuffer);
			    frame.vertexView=grownView;
			    frame.vertexCapacity=grownCapacity;
			    activeVertexBuffer=frame.vertices.Get();
			    activeVertexBufferView=grownView;
			    activeVertexCapacity=grownCapacity;
			}
			for(const auto& vertex:alphaVertices) {
			    const Common::ScreenPoint point{(vertex.position[0]+1.0)*.5*(width-1),
			        (1.0-vertex.position[1])*.5*(height-1),vertex.position[2]};
			    constexpr float white=1.5F/Common::UiText::GlyphAtlas::Width;
			    atlasVertices.push_back(Common::UiText::BuildAtlasVertex(point,white,white,vertex.color,width,height));
			}
			Common::UiText::SortAtlasTrianglesBackToFront(atlasVertices);
			if(!opaqueVertices.empty() &&
			    (!Common::DebugOverlay::UploadVertices(activeVertexBuffer,opaqueVertices) ||
			     !Common::DebugOverlay::IssueDrawCommands(frame.commands.Get(),width,height,activeRootSignature,
			         activeOpaquePipelineState,activeVertexBufferView,static_cast<UINT>(opaqueVertices.size())))) {
			    m_lastProjectionLayerStatus=L"opaque-overlay-draw-failed";closeCommandListOnFailure();return false;
			}
			if(!frame.text.UploadAndDraw(frame.commands.Get(),m_uiTextAtlas,atlasVertices,width,height)) {
			    m_lastProjectionLayerStatus=L"ui-atlas-draw-failed";closeCommandListOnFailure();return false;
			}
		}

#endif
		if (useAppMsaaPath)
		{
			D3D12_RESOURCE_BARRIER msaaBarrier{};
			msaaBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			msaaBarrier.Transition.pResource = swapchainContext.msaaColorTarget.Get();
			msaaBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			msaaBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			msaaBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			frame.commands->ResourceBarrier(1, &msaaBarrier);
			frame.commands->ResolveSubresource(
			    sceneColorTexture,
			    0,
			    swapchainContext.msaaColorTarget.Get(),
			    0,
			    ResolveRtvFormat(m_colorSwapchainFormat));
			std::swap(msaaBarrier.Transition.StateBefore, msaaBarrier.Transition.StateAfter);
			frame.commands->ResourceBarrier(1, &msaaBarrier);
		}
		if (useMsaaResolvePath)
		{
			D3D12_RESOURCE_BARRIER swapchainToResolveSourceBarrier{};
			swapchainToResolveSourceBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			swapchainToResolveSourceBarrier.Flags =
			    D3D12_RESOURCE_BARRIER_FLAG_NONE;
			swapchainToResolveSourceBarrier.Transition.pResource =
			    renderTargetTexture;
			swapchainToResolveSourceBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			swapchainToResolveSourceBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			swapchainToResolveSourceBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			frame.commands->ResourceBarrier(
			    1,
			    &swapchainToResolveSourceBarrier);

			frame.commands->ResolveSubresource(
			    sceneColorTexture,
			    0,
			    renderTargetTexture,
			    0,
			    ResolveRtvFormat(m_colorSwapchainFormat));

			D3D12_RESOURCE_BARRIER swapchainBackToRenderBarrier{};
			swapchainBackToRenderBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			swapchainBackToRenderBarrier.Flags =
			    D3D12_RESOURCE_BARRIER_FLAG_NONE;
			swapchainBackToRenderBarrier.Transition.pResource =
			    renderTargetTexture;
			swapchainBackToRenderBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			swapchainBackToRenderBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_RENDER_TARGET;
			swapchainBackToRenderBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			frame.commands->ResourceBarrier(
			    1,
			    &swapchainBackToRenderBarrier);
		}

		if (useMsaaResolvePath || useAppMsaaPath || useSceneDrawPath)
		{
			D3D12_RESOURCE_BARRIER sceneToShaderBarrier{};
			sceneToShaderBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneToShaderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneToShaderBarrier.Transition.pResource = sceneColorTexture;
			sceneToShaderBarrier.Transition.StateBefore = (useMsaaResolvePath || useAppMsaaPath)
			                                                  ? D3D12_RESOURCE_STATE_RESOLVE_DEST
			                                                  : D3D12_RESOURCE_STATE_RENDER_TARGET;
			sceneToShaderBarrier.Transition.StateAfter =
			    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			sceneToShaderBarrier.Transition.Subresource =
			    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			frame.commands->ResourceBarrier(1, &sceneToShaderBarrier);
		}

		bool usedFxaaPass = false;
		if (canUseScenePostProcess &&
		    m_isFxaaAvailable &&
		    (m_fxaaRootSignature != nullptr) &&
		    (m_fxaaPipelineState != nullptr) &&
		    (swapchainContext.sceneSrvHeap != nullptr))
		{
			D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvGpuHandle =
			    swapchainContext.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart();
			sceneSrvGpuHandle.ptr += static_cast<UINT64>(imageIndex) *
			                         static_cast<UINT64>(m_srvDescriptorSize);

			D3D12_VIEWPORT viewport{};
			viewport.TopLeftX = 0.0F;
			viewport.TopLeftY = 0.0F;
			viewport.Width = static_cast<float>(swapchainContext.width);
			viewport.Height = static_cast<float>(swapchainContext.height);
			viewport.MinDepth = 0.0F;
			viewport.MaxDepth = 1.0F;
			D3D12_RECT scissorRect{};
			scissorRect.left = 0;
			scissorRect.top = 0;
			scissorRect.right = swapchainContext.width;
			scissorRect.bottom = swapchainContext.height;

			ID3D12DescriptorHeap* const descriptorHeaps[]{
				swapchainContext.sceneSrvHeap.Get()
			};
			frame.commands->SetDescriptorHeaps(
			    static_cast<UINT>(std::size(descriptorHeaps)),
			    descriptorHeaps);
			frame.commands->RSSetViewports(1, &viewport);
			frame.commands->RSSetScissorRects(1, &scissorRect);
			frame.commands->OMSetRenderTargets(
			    1,
			    &swapchainRtvHandle,
			    FALSE,
			    nullptr);
			frame.commands->SetGraphicsRootSignature(
			    m_fxaaRootSignature.Get());
			frame.commands->SetPipelineState(
			    m_fxaaPipelineState.Get());
			frame.commands->IASetPrimitiveTopology(
			    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			const float fxaaParameters[Common::FxaaParameterCount]{
				1.0F / static_cast<float>((std::max)(1, swapchainContext.width)),
				1.0F / static_cast<float>((std::max)(1, swapchainContext.height)),
				m_renderQuality.fxaaEdgeThreshold,
				m_renderQuality.fxaaEdgeThresholdMin,
				m_renderQuality.fxaaBlendFactor,
				IsSrgbFormat(m_colorSwapchainFormat) ? 1.0F : 0.0F
			};
			frame.commands->SetGraphicsRootDescriptorTable(
			    0,
			    sceneSrvGpuHandle);
			frame.commands->SetGraphicsRoot32BitConstants(
			    1,
			    Common::FxaaParameterCount,
			    fxaaParameters,
			    0);
			frame.commands->DrawInstanced(3, 1, 0, 0);
			usedFxaaPass = true;
			m_xrPostProcessStatus = L"fxaa";
		}

		if (canUseScenePostProcess && !usedFxaaPass)
		{
			if (useSceneDrawPath || useAppMsaaPath)
			{
				D3D12_RESOURCE_BARRIER toCopyBarriers[2]{};
				D3D12_RESOURCE_BARRIER& swapchainToCopyDestBarrier =
				    toCopyBarriers[0];
				swapchainToCopyDestBarrier.Type =
				    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				swapchainToCopyDestBarrier.Flags =
				    D3D12_RESOURCE_BARRIER_FLAG_NONE;
				swapchainToCopyDestBarrier.Transition.pResource =
				    renderTargetTexture;
				swapchainToCopyDestBarrier.Transition.StateBefore =
				    D3D12_RESOURCE_STATE_RENDER_TARGET;
				swapchainToCopyDestBarrier.Transition.StateAfter =
				    D3D12_RESOURCE_STATE_COPY_DEST;
				swapchainToCopyDestBarrier.Transition.Subresource =
				    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				D3D12_RESOURCE_BARRIER& sceneToCopySourceBarrier =
				    toCopyBarriers[1];
				sceneToCopySourceBarrier.Type =
				    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				sceneToCopySourceBarrier.Flags =
				    D3D12_RESOURCE_BARRIER_FLAG_NONE;
				sceneToCopySourceBarrier.Transition.pResource =
				    sceneColorTexture;
				sceneToCopySourceBarrier.Transition.StateBefore =
				    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				sceneToCopySourceBarrier.Transition.StateAfter =
				    D3D12_RESOURCE_STATE_COPY_SOURCE;
				sceneToCopySourceBarrier.Transition.Subresource =
				    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				frame.commands->ResourceBarrier(
				    static_cast<UINT>(std::size(toCopyBarriers)),
				    toCopyBarriers);

				frame.commands->CopyResource(
				    renderTargetTexture,
				    sceneColorTexture);

				D3D12_RESOURCE_BARRIER fromCopyBarriers[2]{};
				D3D12_RESOURCE_BARRIER& sceneFromCopySourceBarrier =
				    fromCopyBarriers[0];
				sceneFromCopySourceBarrier.Type =
				    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				sceneFromCopySourceBarrier.Flags =
				    D3D12_RESOURCE_BARRIER_FLAG_NONE;
				sceneFromCopySourceBarrier.Transition.pResource =
				    sceneColorTexture;
				sceneFromCopySourceBarrier.Transition.StateBefore =
				    D3D12_RESOURCE_STATE_COPY_SOURCE;
				sceneFromCopySourceBarrier.Transition.StateAfter =
				    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				sceneFromCopySourceBarrier.Transition.Subresource =
				    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				D3D12_RESOURCE_BARRIER& swapchainFromCopyDestBarrier =
				    fromCopyBarriers[1];
				swapchainFromCopyDestBarrier.Type =
				    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				swapchainFromCopyDestBarrier.Flags =
				    D3D12_RESOURCE_BARRIER_FLAG_NONE;
				swapchainFromCopyDestBarrier.Transition.pResource =
				    renderTargetTexture;
				swapchainFromCopyDestBarrier.Transition.StateBefore =
				    D3D12_RESOURCE_STATE_COPY_DEST;
				swapchainFromCopyDestBarrier.Transition.StateAfter =
				    D3D12_RESOURCE_STATE_RENDER_TARGET;
				swapchainFromCopyDestBarrier.Transition.Subresource =
				    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				frame.commands->ResourceBarrier(
				    static_cast<UINT>(std::size(fromCopyBarriers)),
				    fromCopyBarriers);

				m_xrPostProcessStatus = L"copy";
			}
			else
			{
				m_xrPostProcessStatus = L"direct";
			}
		}

		if (!canUseScenePostProcess)
		{
			m_xrPostProcessStatus = L"direct";
		}

		// xrReleaseSwapchainImage requires RENDER_TARGET availability on
		// the bound queue; all resolve/copy paths restore that state above.
		const HRESULT submit = frame.Submit(m_d3d12Queue.Get(),
		    m_xrFence.Get(), m_xrFenceValue++);
		if (FAILED(submit))
		{
			m_projectionRenderingFailed = true;
			m_lastProjectionLayerStatus = L"view-submit-failed";
			m_sessionName = L"Session(unavailable): " + BuildD3d12ErrorMessage(L"XR view Submit", submit);
			return false;
		}
		return true;
	}
} // namespace BaseballSimulatorVR::Xr
#endif
