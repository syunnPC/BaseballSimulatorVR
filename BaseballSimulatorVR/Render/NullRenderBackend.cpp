#include "NullRenderBackend.hpp"

#include "../Common/DebugWorldProjection.hpp"
#include "../Common/FieldPresentationOverlay.hpp"
#include "../Common/MathUtilities.hpp"
#include "../Common/PhaseTrajectoryOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Render
{
	void NullRenderBackend::SetFieldPresentationState(const Common::FieldPresentationState& state)
	{
		m_fieldPresentation = state;
	}

	namespace
	{
		constexpr COLORREF BackgroundColor = RGB(16, 18, 26);
		constexpr COLORREF PanelBorderColor = RGB(90, 100, 120);
		constexpr COLORREF StrikeZoneColor = RGB(250, 200, 60);
		constexpr COLORREF TrajectoryColor = RGB(90, 200, 255);
		constexpr COLORREF BallColor = RGB(255, 255, 255);
		constexpr COLORREF DuelMarkerColor = RGB(255, 120, 90);
		constexpr COLORREF BatOverlayColor = RGB(208, 150, 76);
		constexpr COLORREF LabelColor = RGB(220, 224, 236);

		constexpr LONG PanelMargin = 16;
		constexpr LONG PanelGap = 16;

		constexpr double SideViewMinZ = 0.0;
		constexpr double SideViewMaxZ = 20.0;
		constexpr double SideViewMinY = 0.0;
		constexpr double SideViewMaxY = 2.5;

		constexpr double TopViewMinX = -1.5;
		constexpr double TopViewMaxX = 1.5;
		constexpr double TopViewMinZ = 0.0;
		constexpr double TopViewMaxZ = 20.0;


		[[nodiscard]] LONG InterpolateToPixel(double normalizedValue, LONG pixelMin, LONG pixelMax)
		{
			const double pixelRange = static_cast<double>(pixelMax - pixelMin);
			return pixelMin + static_cast<LONG>(
			                      Common::Math::Clamp01(normalizedValue) *
			                      pixelRange);
		}

		void DrawTrajectory(HDC deviceContext, const std::vector<POINT>& points)
		{
			if (points.size() < 2)
			{
				return;
			}

			Polyline(deviceContext, points.data(), static_cast<int>(points.size()));
		}

		[[nodiscard]] bool IsValidSelectedObject(const HGDIOBJ object) noexcept
		{
			return (object != nullptr) && (object != HGDI_ERROR);
		}

		void DeleteGdiObject(const HGDIOBJ object) noexcept
		{
			if (object != nullptr)
			{
				(void)DeleteObject(object);
			}
		}
	} // namespace

	bool NullRenderBackend::Initialize([[maybe_unused]] HWND windowHandle)
	{
		m_windowHandle = windowHandle;
		m_trajectoryTrails.Clear();
		m_duelZonePassMarker.reset();
		m_batOverlayState.reset();
		m_isInitialized = true;
		return true;
	}

	void NullRenderBackend::BeginFrame()
	{
		if (!m_isInitialized)
		{
			return;
		}
	}

	void NullRenderBackend::RenderSimulation(const Physics::BallState& ballState)
	{
		if (!m_isInitialized || (m_windowHandle == nullptr))
		{
			return;
		}
		Common::UpdatePhaseTrajectoryTrails(m_trajectoryTrails, ballState,
		    m_fieldPresentation, std::chrono::steady_clock::now());

		RECT clientRect{};
		if (GetClientRect(m_windowHandle, &clientRect) == FALSE)
		{
			return;
		}

		const LONG clientWidth = clientRect.right - clientRect.left;
		const LONG clientHeight = clientRect.bottom - clientRect.top;
		if ((clientWidth <= 0) || (clientHeight <= 0))
		{
			return;
		}

		HDC windowDeviceContext = GetDC(m_windowHandle);
		if (windowDeviceContext == nullptr)
		{
			return;
		}

		HDC backBufferDeviceContext = nullptr;
		HBITMAP backBufferBitmap = nullptr;
		HBRUSH backgroundBrush = nullptr;
		HBRUSH ballBrush = nullptr;
		HPEN panelBorderPen = nullptr;
		HPEN strikeZonePen = nullptr;
		HPEN trajectoryPen = nullptr;
		HPEN duelMarkerPen = nullptr;
		HGDIOBJ previousBitmap = nullptr;
		HGDIOBJ previousPen = nullptr;
		HGDIOBJ previousBrush = nullptr;
		bool canPresent = false;
		const RECT sideViewRect = BuildSideViewRect(clientRect);
		const RECT topViewRect = BuildTopViewRect(clientRect);
		do
		{
			backBufferDeviceContext = CreateCompatibleDC(windowDeviceContext);
			if (backBufferDeviceContext == nullptr)
			{
				break;
			}

			backBufferBitmap = CreateCompatibleBitmap(
			    windowDeviceContext,
			    clientWidth,
			    clientHeight);
			if (backBufferBitmap == nullptr)
			{
				break;
			}

			previousBitmap = SelectObject(backBufferDeviceContext, backBufferBitmap);
			if (!IsValidSelectedObject(previousBitmap))
			{
				break;
			}

			backgroundBrush = CreateSolidBrush(BackgroundColor);
			if (backgroundBrush == nullptr)
			{
				break;
			}
			FillRect(backBufferDeviceContext, &clientRect, backgroundBrush);

			if (m_mirrorViewState.isValid)
			{
			    struct Triangle final { std::array<Common::ScreenPoint,3> points; float depth; std::array<float,4> color; bool blend; };
			    std::vector<Triangle> triangles;
			    const auto append = [&](const Common::ScreenPoint& a, const Common::ScreenPoint& b,
			                            const Common::ScreenPoint& c, const float* color, const bool blend = false) {
			        triangles.push_back({{a,b,c}, (a.depth+b.depth+c.depth)/3.0F,
			            {color[0],color[1],color[2],color[3]},blend});
			    };
			    if (m_overlayVisibility.showFloorMesh)
			        Common::EmitProjectedFloorMeshOverlay(m_mirrorViewState,clientWidth,clientHeight,append);
			    Common::EmitProjectedFieldPresentation(m_fieldPresentation,m_mirrorViewState,clientWidth,clientHeight,append);
			    Common::EmitProjectedSolidBatOverlay(m_mirrorViewState,clientWidth,clientHeight,m_batOverlayState,
			        [&](const auto& a, const auto& b, const auto& c, const float* color) {
			            append(a,b,c,color,color[3] < .999F);
			        });
			        Common::EmitProjectedPhaseBallAndTrajectoryMeshOverlay(m_mirrorViewState,clientWidth,clientHeight,
			            m_trajectoryTrails,m_overlayVisibility,ballState,append,m_renderQuality.trailOpacityScale,m_fieldPresentation.ballVisible);
			    std::stable_sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b) { return a.depth>b.depth; });
			    const auto oldPen = SelectObject(backBufferDeviceContext,GetStockObject(NULL_PEN));
			    const auto oldBrush = SelectObject(backBufferDeviceContext,GetStockObject(DC_BRUSH));
			    bool trianglesRendered = true;
			    for (const auto& tri : triangles) {
			        if (tri.blend) {
			            if (!m_alphaTriangleRasterizer.Draw(backBufferDeviceContext,
			                clientWidth, clientHeight, tri.points, tri.color)) {
			                trianglesRendered = false;
			                break;
			            }
			        } else {
			            std::array<POINT,3> points;
			            for (std::size_t i = 0; i < points.size(); ++i)
			                points[i] = {static_cast<LONG>(std::lround(tri.points[i].x)),
			                    static_cast<LONG>(std::lround(tri.points[i].y))};
			            SetDCBrushColor(backBufferDeviceContext,RGB(static_cast<BYTE>(tri.color[0]*255),
			                static_cast<BYTE>(tri.color[1]*255),static_cast<BYTE>(tri.color[2]*255)));
			            Polygon(backBufferDeviceContext,points.data(),3);
			        }
			    }
			    if (!trianglesRendered) {
			        SelectObject(backBufferDeviceContext,oldBrush);
			        SelectObject(backBufferDeviceContext,oldPen);
			        break;
			    }
			    const auto marker = [&](const Common::ScreenPoint& p, LONG radius, const float* color) {
			        SetDCBrushColor(backBufferDeviceContext,RGB(static_cast<BYTE>(color[0]*255),static_cast<BYTE>(color[1]*255),static_cast<BYTE>(color[2]*255)));
			        const LONG x=static_cast<LONG>(std::lround(p.x)), y=static_cast<LONG>(std::lround(p.y));
			        Rectangle(backBufferDeviceContext,x-radius,y-radius,x+radius+1,y+radius+1);
			    };
			    const auto line = [&](const Common::ScreenPoint& a,const Common::ScreenPoint& b,LONG radius,const float* color) {
			        const double length=std::hypot(b.x-a.x,b.y-a.y);
			        const int steps=(std::max)(1,static_cast<int>(std::ceil(length/2)));
			        for(int i=0;i<=steps;++i) {
			            const double t=static_cast<double>(i)/steps;
			            marker({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.depth},radius,color);
			        }
			    };
			    const std::vector<Physics::Vector3> noTrail{};
			    Common::EmitProjectedDebugWorldOverlay(m_mirrorViewState,clientWidth,clientHeight,noTrail,ballState,marker,line,
			        false,false,m_overlayVisibility.strikeZoneDisplayMode,false);
			    Common::EmitProjectedZonePassCircleOverlay(m_mirrorViewState,clientWidth,clientHeight,m_duelZonePassMarker,ballState.properties.radiusMeters,line);
			    SelectObject(backBufferDeviceContext,oldBrush);
			    SelectObject(backBufferDeviceContext,oldPen);
			    canPresent = true;
			    break;
			}

			panelBorderPen = CreatePen(PS_SOLID, 1, PanelBorderColor);
			strikeZonePen = CreatePen(PS_SOLID, 2, StrikeZoneColor);
			trajectoryPen = CreatePen(PS_SOLID, 2, TrajectoryColor);
			duelMarkerPen = CreatePen(PS_SOLID, 2, DuelMarkerColor);
			ballBrush = CreateSolidBrush(BallColor);
			if ((panelBorderPen == nullptr) || (strikeZonePen == nullptr) ||
			    (trajectoryPen == nullptr) || (duelMarkerPen == nullptr) ||
			    (ballBrush == nullptr))
			{
				break;
			}

			HBRUSH hollowBrush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
			previousPen = SelectObject(backBufferDeviceContext, panelBorderPen);
			previousBrush = SelectObject(backBufferDeviceContext, hollowBrush);
			if (!IsValidSelectedObject(previousPen) ||
			    !IsValidSelectedObject(previousBrush))
			{
				break;
			}

			Rectangle(backBufferDeviceContext, sideViewRect.left, sideViewRect.top, sideViewRect.right, sideViewRect.bottom);
			Rectangle(backBufferDeviceContext, topViewRect.left, topViewRect.top, topViewRect.right, topViewRect.bottom);

			const auto plate = Common::GetHomePlateVertices(Common::GetPitchDistanceMeters(),
			    Common::DebugWorldStyle::HomePlateHeightMeters);
			std::array<POINT, 5> topPlate{};
			for (std::size_t i = 0; i < plate.size(); ++i)
				topPlate[i] = ProjectTopViewPoint(plate[i], topViewRect);
			SelectObject(backBufferDeviceContext, GetStockObject(WHITE_PEN));
			SelectObject(backBufferDeviceContext, GetStockObject(WHITE_BRUSH));
			Polygon(backBufferDeviceContext, topPlate.data(), static_cast<int>(topPlate.size()));
			const auto plateFront = ProjectSideViewPoint(plate[0], sideViewRect);
			const auto plateBack = ProjectSideViewPoint(plate[3], sideViewRect);
			MoveToEx(backBufferDeviceContext, plateFront.x, plateFront.y, nullptr);
			LineTo(backBufferDeviceContext, plateBack.x, plateBack.y);
			SelectObject(backBufferDeviceContext, hollowBrush);

			SelectObject(backBufferDeviceContext, strikeZonePen);
			Common::ForEachStrikeZoneWireSegment(m_overlayVisibility.strikeZoneDisplayMode,
			    {Common::GetPitchDistanceMeters(), Common::GetStrikeZoneBottomMeters(),
			     Common::GetStrikeZoneTopMeters()}, [&](const auto& from, const auto& to) {
				    const auto sideFrom = ProjectSideViewPoint(from, sideViewRect);
				    const auto sideTo = ProjectSideViewPoint(to, sideViewRect);
				    MoveToEx(backBufferDeviceContext, sideFrom.x, sideFrom.y, nullptr);
				    LineTo(backBufferDeviceContext, sideTo.x, sideTo.y);
				    const auto topFrom = ProjectTopViewPoint(from, topViewRect);
				    const auto topTo = ProjectTopViewPoint(to, topViewRect);
				    MoveToEx(backBufferDeviceContext, topFrom.x, topFrom.y, nullptr);
				    LineTo(backBufferDeviceContext, topTo.x, topTo.y);
			    });

				SelectObject(backBufferDeviceContext, trajectoryPen);
				m_trajectoryTrails.ForEachVisible(m_overlayVisibility, [&](const Common::TrajectoryRenderSpan& trail) {
				std::vector<POINT> sideViewTrajectory{};
				std::vector<POINT> topViewTrajectory{};
				sideViewTrajectory.reserve(trail.Points().size());
				topViewTrajectory.reserve(trail.Points().size());

				for (const Physics::Vector3& sample : trail.Points())
				{
					sideViewTrajectory.push_back(ProjectSideViewPoint(sample, sideViewRect));
					topViewTrajectory.push_back(ProjectTopViewPoint(sample, topViewRect));
				}

				DrawTrajectory(backBufferDeviceContext, sideViewTrajectory);
				DrawTrajectory(backBufferDeviceContext, topViewTrajectory);
				});

			const POINT sideBall = ProjectSideViewPoint(ballState.position, sideViewRect);
			const POINT topBall = ProjectTopViewPoint(ballState.position, topViewRect);
			const LONG sideBallRadiusPixels = Common::OrthographicBallRadiusPixels(ballState.properties.radiusMeters,
			    sideViewRect.bottom - sideViewRect.top, SideViewMaxY - SideViewMinY);
			const LONG topBallRadiusPixels = Common::OrthographicBallRadiusPixels(ballState.properties.radiusMeters,
			    topViewRect.right - topViewRect.left, TopViewMaxX - TopViewMinX);
			if (m_fieldPresentation.ballVisible)
			{
			SelectObject(backBufferDeviceContext, ballBrush);
			Ellipse(
			    backBufferDeviceContext,
			    sideBall.x - sideBallRadiusPixels,
			    sideBall.y - sideBallRadiusPixels,
			    sideBall.x + sideBallRadiusPixels,
			    sideBall.y + sideBallRadiusPixels);
			Ellipse(
			    backBufferDeviceContext,
			    topBall.x - topBallRadiusPixels,
			    topBall.y - topBallRadiusPixels,
			    topBall.x + topBallRadiusPixels,
			    topBall.y + topBallRadiusPixels);
			}

			if (m_duelZonePassMarker.has_value())
			{
				SelectObject(backBufferDeviceContext, duelMarkerPen);
				const Physics::Vector3& zonePassMarker = *m_duelZonePassMarker;
				constexpr int markerSegmentCount = 24;
				const double markerRadiusMeters = ballState.properties.radiusMeters;
				for (int segmentIndex = 0; segmentIndex < markerSegmentCount;
				     ++segmentIndex)
				{
					const double startAngle =
					    (2.0 * 3.14159265358979323846 *
					     static_cast<double>(segmentIndex)) /
					    static_cast<double>(markerSegmentCount);
					const double endAngle =
					    (2.0 * 3.14159265358979323846 *
					     static_cast<double>(segmentIndex + 1)) /
					    static_cast<double>(markerSegmentCount);
					const Physics::Vector3 startPoint{
						zonePassMarker.x +
						    (std::cos(startAngle) * markerRadiusMeters),
						zonePassMarker.y +
						    (std::sin(startAngle) * markerRadiusMeters),
						zonePassMarker.z
					};
					const Physics::Vector3 endPoint{
						zonePassMarker.x +
						    (std::cos(endAngle) * markerRadiusMeters),
						zonePassMarker.y +
						    (std::sin(endAngle) * markerRadiusMeters),
						zonePassMarker.z
					};

					const POINT sideStart =
					    ProjectSideViewPoint(startPoint, sideViewRect);
					const POINT sideEnd =
					    ProjectSideViewPoint(endPoint, sideViewRect);
					MoveToEx(
					    backBufferDeviceContext,
					    sideStart.x,
					    sideStart.y,
					    nullptr);
					LineTo(
					    backBufferDeviceContext,
					    sideEnd.x,
					    sideEnd.y);

					const POINT topStart =
					    ProjectTopViewPoint(startPoint, topViewRect);
					const POINT topEnd =
					    ProjectTopViewPoint(endPoint, topViewRect);
					MoveToEx(
					    backBufferDeviceContext,
					    topStart.x,
					    topStart.y,
					    nullptr);
					LineTo(
					    backBufferDeviceContext,
					    topEnd.x,
					    topEnd.y);
				}
			}
			if (m_batOverlayState.has_value())
			{
				SelectObject(backBufferDeviceContext, GetStockObject(DC_PEN));
				const Physics::Vector3 simulationOffset{0.0, 0.0,
				    Common::GetPitchDistanceMeters() + Common::DebugWorldLayout::BatterToPlateDistanceMeters};
				Common::EmitBatWorldWireframe(*m_batOverlayState,
				    [&](const Physics::Vector3& worldStart, const Physics::Vector3& worldEnd,
				        const std::array<float, 4>& color)
				    {
					    const COLORREF penColor = color == Common::DebugWorldStyle::BatColor
					        ? BatOverlayColor
					        : RGB(static_cast<BYTE>(color[0] * 255), static_cast<BYTE>(color[1] * 255),
					            static_cast<BYTE>(color[2] * 255));
					    SetDCPenColor(backBufferDeviceContext, penColor);
					    const auto start = worldStart + simulationOffset;
					    const auto end = worldEnd + simulationOffset;
					    const auto sideStart = ProjectSideViewPoint(start, sideViewRect);
					    const auto sideEnd = ProjectSideViewPoint(end, sideViewRect);
					    MoveToEx(backBufferDeviceContext, sideStart.x, sideStart.y, nullptr);
					    LineTo(backBufferDeviceContext, sideEnd.x, sideEnd.y);
					    const auto topStart = ProjectTopViewPoint(start, topViewRect);
					    const auto topEnd = ProjectTopViewPoint(end, topViewRect);
					    MoveToEx(backBufferDeviceContext, topStart.x, topStart.y, nullptr);
					    LineTo(backBufferDeviceContext, topEnd.x, topEnd.y);
				    });
			}

			SetBkMode(backBufferDeviceContext, TRANSPARENT);
			SetTextColor(backBufferDeviceContext, LabelColor);
			{
				const std::wstring sideLabel = L"Side View (Z-Y)";
				const std::wstring topLabel = L"Top View (X-Z)";
				TextOutW(backBufferDeviceContext, sideViewRect.left + 8, sideViewRect.top + 8, sideLabel.c_str(), static_cast<int>(sideLabel.size()));
				TextOutW(backBufferDeviceContext, topViewRect.left + 8, topViewRect.top + 8, topLabel.c_str(), static_cast<int>(topLabel.size()));
			}
			canPresent = true;
		} while (false);

		if (canPresent && (backBufferDeviceContext != nullptr))
		{
			BitBlt(windowDeviceContext, 0, 0, clientWidth, clientHeight, backBufferDeviceContext, 0, 0, SRCCOPY);
		}

		if ((backBufferDeviceContext != nullptr) && IsValidSelectedObject(previousBrush))
		{
			SelectObject(backBufferDeviceContext, previousBrush);
		}
		if ((backBufferDeviceContext != nullptr) && IsValidSelectedObject(previousPen))
		{
			SelectObject(backBufferDeviceContext, previousPen);
		}
		if ((backBufferDeviceContext != nullptr) && IsValidSelectedObject(previousBitmap))
		{
			SelectObject(backBufferDeviceContext, previousBitmap);
		}

		DeleteGdiObject(ballBrush);
		DeleteGdiObject(backgroundBrush);
		DeleteGdiObject(trajectoryPen);
		DeleteGdiObject(strikeZonePen);
		DeleteGdiObject(duelMarkerPen);
		DeleteGdiObject(panelBorderPen);
		DeleteGdiObject(backBufferBitmap);
		if (backBufferDeviceContext != nullptr)
		{
			DeleteDC(backBufferDeviceContext);
		}
		ReleaseDC(m_windowHandle, windowDeviceContext);
	}

	void NullRenderBackend::EndFrame()
	{
		if (!m_isInitialized)
		{
			return;
		}
	}

	void NullRenderBackend::Shutdown()
	{
		m_isInitialized = false;
		m_windowHandle = nullptr;
		m_trajectoryTrails.Clear();
		m_duelZonePassMarker.reset();
		m_batOverlayState.reset();
	}

	std::wstring_view NullRenderBackend::GetBackendName() const noexcept
	{
		return L"GDI(debug)";
	}

	Common::GraphicsBinding NullRenderBackend::GetGraphicsBinding() const noexcept
	{
		return Common::GraphicsBinding{};
	}

	void NullRenderBackend::SetDesktopMirrorView(const Common::ViewProjectionState& mirrorViewState)
	{
		m_mirrorViewState = mirrorViewState;
	}

	void NullRenderBackend::SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility)
	{
		m_overlayVisibility = overlayVisibility;
	}

	void NullRenderBackend::SetRenderQuality(const Common::RenderQualitySettings& renderQuality)
	{
		m_renderQuality = Common::NormalizeRenderQualitySettings(renderQuality);
	}

	void NullRenderBackend::SetDuelZonePassMarker(
	    const std::optional<Physics::Vector3>& zonePassMarker)
	{
		m_duelZonePassMarker = zonePassMarker;
	}

	void NullRenderBackend::SetBatOverlayState(
	    const std::optional<Common::BatOverlayState>& batOverlayState)
	{
		m_batOverlayState = batOverlayState;
	}

	RECT NullRenderBackend::BuildSideViewRect(const RECT& clientRect)
	{
		const LONG width = clientRect.right - clientRect.left;
		const LONG height = clientRect.bottom - clientRect.top;
		const LONG panelWidth = std::max<LONG>((width - (PanelMargin * 2) - PanelGap) / 2, 50);

		RECT sideViewRect{};
		sideViewRect.left = PanelMargin;
		sideViewRect.top = PanelMargin;
		sideViewRect.right = sideViewRect.left + panelWidth;
		sideViewRect.bottom = height - PanelMargin;
		return sideViewRect;
	}

	RECT NullRenderBackend::BuildTopViewRect(const RECT& clientRect)
	{
		const RECT sideViewRect = BuildSideViewRect(clientRect);
		const LONG width = clientRect.right - clientRect.left;

		RECT topViewRect{};
		topViewRect.left = sideViewRect.right + PanelGap;
		topViewRect.top = sideViewRect.top;
		topViewRect.right = width - PanelMargin;
		topViewRect.bottom = sideViewRect.bottom;
		return topViewRect;
	}

	POINT NullRenderBackend::ProjectSideViewPoint(const Physics::Vector3& position, const RECT& sideViewRect)
	{
		const double normalizedZ = (position.z - SideViewMinZ) / (SideViewMaxZ - SideViewMinZ);
		const double normalizedY = (position.y - SideViewMinY) / (SideViewMaxY - SideViewMinY);

		POINT projectedPoint{};
		projectedPoint.x = InterpolateToPixel(normalizedZ, sideViewRect.left, sideViewRect.right);
		projectedPoint.y = InterpolateToPixel(1.0 - normalizedY, sideViewRect.top, sideViewRect.bottom);
		return projectedPoint;
	}

	POINT NullRenderBackend::ProjectTopViewPoint(const Physics::Vector3& position, const RECT& topViewRect)
	{
		const double normalizedX = (position.x - TopViewMinX) / (TopViewMaxX - TopViewMinX);
		const double normalizedZ = (position.z - TopViewMinZ) / (TopViewMaxZ - TopViewMinZ);

		POINT projectedPoint{};
		projectedPoint.x = InterpolateToPixel(normalizedX, topViewRect.left, topViewRect.right);
		projectedPoint.y = InterpolateToPixel(1.0 - normalizedZ, topViewRect.top, topViewRect.bottom);
		return projectedPoint;
	}

} // namespace BaseballSimulatorVR::Render
