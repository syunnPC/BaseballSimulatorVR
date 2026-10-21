#include "NullXrSystem.hpp"
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

#if defined(_WIN64)
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace BaseballSimulatorVR::Xr
{
	void NullXrSystem::SetFieldPresentationState(const Common::FieldPresentationState& state)
	{
		m_fieldPresentation = state;
	}

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

		constexpr wchar_t ActiveRuntimeValueName[] = L"ActiveRuntime";
		constexpr wchar_t OpenXrRegistryPath[] = L"SOFTWARE\\Khronos\\OpenXR\\1";
		constexpr XrViewConfigurationType PrimaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		// Both eyes clear to the same sky color; different colors per eye cause
		// binocular rivalry wherever the sky shows.
		constexpr std::array<float, 4> LeftEyeClearColor{ 0.02F, 0.03F, 0.08F, 1.0F };
		constexpr std::array<float, 4> RightEyeClearColor = LeftEyeClearColor;
		constexpr std::array<DXGI_FORMAT, 4> PreferredSwapchainFormats{
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM
		};
		constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
		constexpr UINT MaxDebugVertices = 262144;
#if defined(BASEBALLSIMVR_DISABLE_WORLD_DRAW)
		constexpr bool EnableDebugDraw = false;
#else
		constexpr bool EnableDebugDraw = true;
#endif

		[[nodiscard]] std::wstring ExtractFileName(const std::wstring& fullPath)
		{
			const std::size_t separator = fullPath.find_last_of(L"\\/");
			if (separator == std::wstring::npos)
			{
				return fullPath;
			}

			return fullPath.substr(separator + 1);
		}

#if defined(_WIN64)
		[[nodiscard]] std::wstring ConvertUtf8ToWide(const std::string_view utf8Text)
		{
			if (utf8Text.empty())
			{
				return std::wstring{};
			}

			const int wideSize = MultiByteToWideChar(
			    CP_UTF8,
			    0,
			    utf8Text.data(),
			    static_cast<int>(utf8Text.size()),
			    nullptr,
			    0);
			if (wideSize <= 0)
			{
				return std::wstring{};
			}

			std::wstring wide(wideSize, L'\0');
			const int converted = MultiByteToWideChar(
			    CP_UTF8,
			    0,
			    utf8Text.data(),
			    static_cast<int>(utf8Text.size()),
			    wide.data(),
			    wideSize);
			if (converted != wideSize)
			{
				return std::wstring{};
			}

			return wide;
		}

		[[nodiscard]] std::wstring BuildApiVersionString(const XrVersion runtimeVersion)
		{
			return std::to_wstring(XR_VERSION_MAJOR(runtimeVersion)) + L"." +
			       std::to_wstring(XR_VERSION_MINOR(runtimeVersion)) + L"." +
			       std::to_wstring(XR_VERSION_PATCH(runtimeVersion));
		}

		[[nodiscard]] std::wstring BuildSessionStateString(const XrSessionState state)
		{
			switch (state)
			{
				case XR_SESSION_STATE_UNKNOWN:
					return L"unknown";
				case XR_SESSION_STATE_IDLE:
					return L"idle";
				case XR_SESSION_STATE_READY:
					return L"ready";
				case XR_SESSION_STATE_SYNCHRONIZED:
					return L"synchronized";
				case XR_SESSION_STATE_VISIBLE:
					return L"visible";
				case XR_SESSION_STATE_FOCUSED:
					return L"focused";
				case XR_SESSION_STATE_STOPPING:
					return L"stopping";
				case XR_SESSION_STATE_LOSS_PENDING:
					return L"loss pending";
				case XR_SESSION_STATE_EXITING:
					return L"exiting";
				case XR_SESSION_STATE_MAX_ENUM:
					return L"max enum";
				default:
					return L"(unrecognized)";
			}
		}

		[[nodiscard]] std::wstring BuildXrErrorMessage(
		    const std::wstring_view phase,
		    const XrResult result)
		{
			return std::wstring{ phase } + L" failed(" +
			       std::to_wstring(static_cast<long long>(result)) + L")";
		}

		void LogXrCleanupFailure(
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

		void LogWin32CleanupFailure(
		    const std::wstring_view operationName,
		    const DWORD errorCode)
		{
			const std::wstring message =
			    L"[OpenXR] " + std::wstring{ operationName } + L" failed(win32=" +
			    std::to_wstring(static_cast<unsigned long long>(errorCode)) +
			    L")\n";
			OutputDebugStringW(message.c_str());
		}

		[[nodiscard]] std::wstring BuildD3d12ErrorMessage(
		    const std::wstring_view phase,
		    const HRESULT result)
		{
			return std::wstring{ phase } + L" failed(0x" +
			       std::to_wstring(static_cast<unsigned long long>(static_cast<unsigned long>(result))) + L")";
		}

		[[nodiscard]] std::wstring BuildDebugPipelineInitializationErrorMessage(
		    const Common::DebugOverlay::PipelineInitializationPhase failedPhase,
		    const HRESULT failedResult)
		{
			switch (failedPhase)
			{
				case Common::DebugOverlay::PipelineInitializationPhase::InvalidArguments:
					return L"debug pipeline initialization arguments invalid";
				case Common::DebugOverlay::PipelineInitializationPhase::SerializeRootSignature:
					return BuildD3d12ErrorMessage(L"D3D12SerializeRootSignature", failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::CreateRootSignature:
					return BuildD3d12ErrorMessage(
					    L"ID3D12Device::CreateRootSignature",
					    failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::CompileVertexShader:
				case Common::DebugOverlay::PipelineInitializationPhase::CompilePixelShader:
					return L"D3DCompile failed";
				case Common::DebugOverlay::PipelineInitializationPhase::CreatePipelineState:
					return BuildD3d12ErrorMessage(
					    L"ID3D12Device::CreateGraphicsPipelineState",
					    failedResult);
				case Common::DebugOverlay::PipelineInitializationPhase::None:
				default:
					return L"debug pipeline initialization failed";
			}
		}

		[[nodiscard]] bool IsSameLuid(const LUID& lhs, const LUID& rhs) noexcept
		{
			return (lhs.LowPart == rhs.LowPart) && (lhs.HighPart == rhs.HighPart);
		}

		[[nodiscard]] bool IsSrgbFormat(const DXGI_FORMAT format) noexcept
		{
			return (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) || (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
		}

		[[nodiscard]] DXGI_FORMAT ResolveRtvFormat(const DXGI_FORMAT format) noexcept
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

		[[nodiscard]] std::wstring BuildDxgiFormatName(const DXGI_FORMAT format)
		{
			switch (format)
			{
				case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
					return L"R8G8B8A8_UNORM_SRGB";
				case DXGI_FORMAT_R8G8B8A8_UNORM:
					return L"R8G8B8A8_UNORM";
				case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
					return L"B8G8R8A8_UNORM_SRGB";
				case DXGI_FORMAT_B8G8R8A8_UNORM:
					return L"B8G8R8A8_UNORM";
				default:
					return L"DXGI(" + std::to_wstring(static_cast<int>(format)) + L")";
			}
		}

		using Common::ScreenPoint;

		[[nodiscard]] std::wstring BuildReferenceSpaceName(const XrReferenceSpaceType referenceSpaceType)
		{
			switch (referenceSpaceType)
			{
				case XR_REFERENCE_SPACE_TYPE_LOCAL:
					return L"local";
				case XR_REFERENCE_SPACE_TYPE_STAGE:
					return L"stage";
				case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR:
					return L"local-floor";
				case XR_REFERENCE_SPACE_TYPE_VIEW:
					return L"view";
				default:
					return L"unknown";
			}
		}

		[[nodiscard]] Common::ViewProjectionState BuildProjectionStateFromView(const XrView& view)
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

		[[nodiscard]] std::wstring BuildFrameSummary(
		    const bool shouldRender,
		    const bool hasProjectionLayer,
		    const XrTime predictedDisplayTime,
		    const XrTime previousPredictedDisplayTime,
		    const std::wstring_view projectionLayerStatus,
		    const std::wstring_view laserStatus,
		    const std::wstring_view postProcessStatus)
		{
			std::wostringstream stream{};
			stream << L"frame(sr:" << (shouldRender ? L"1" : L"0")
			       << L",ly:" << (hasProjectionLayer ? L"1" : L"0");
			if (!projectionLayerStatus.empty())
			{
				stream << L",pl:" << projectionLayerStatus;
			}
			if (!laserStatus.empty())
			{
				stream << L",ui:" << laserStatus;
			}
			if (!postProcessStatus.empty())
			{
				stream << L",pp:" << postProcessStatus;
			}
			if ((previousPredictedDisplayTime != 0) &&
			    (predictedDisplayTime > previousPredictedDisplayTime))
			{
				const double deltaMilliseconds =
				    static_cast<double>(predictedDisplayTime - previousPredictedDisplayTime) /
				    1000000.0;
				stream << std::fixed << std::setprecision(2)
				       << L",dt:" << deltaMilliseconds << L"ms";
			}
			stream << L")";
			return stream.str();
		}

		[[nodiscard]] Physics::Vector3 RotateVectorByQuaternion(
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

		[[nodiscard]] XrQuaternionf NormalizeQuaternion(
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

		[[nodiscard]] XrQuaternionf BuildAxisAngleQuaternion(
		    const Physics::Vector3& axis,
		    const double angleRadians)
		{
			const Physics::Vector3 normalizedAxis =
			    Physics::NormalizeOrZero(axis);
			if (Physics::Length(normalizedAxis) <= 1e-6)
			{
				return XrQuaternionf{ 0.0F, 0.0F, 0.0F, 1.0F };
			}

			const double halfAngle = angleRadians * 0.5;
			const float sine = static_cast<float>(std::sin(halfAngle));
			const float cosine = static_cast<float>(std::cos(halfAngle));
			return NormalizeQuaternion(XrQuaternionf{
			    static_cast<float>(normalizedAxis.x) * sine,
			    static_cast<float>(normalizedAxis.y) * sine,
			    static_cast<float>(normalizedAxis.z) * sine,
			    cosine });
		}

		[[nodiscard]] XrQuaternionf BuildYawOffsetOrientation(
		    const double yawRadians)
		{
			return BuildAxisAngleQuaternion(
			    Physics::Vector3{ 0.0, 1.0, 0.0 },
			    yawRadians);
		}

		[[nodiscard]] XrVector2f ApplyStickDeadzone(
		    const XrVector2f& rawStickValue)
		{
			constexpr double deadzone = 0.18;
			const double x = static_cast<double>(rawStickValue.x);
			const double y = static_cast<double>(rawStickValue.y);
			const double magnitude = std::sqrt((x * x) + (y * y));
			if (magnitude <= deadzone)
			{
				return XrVector2f{ 0.0F, 0.0F };
			}

			const double normalizedMagnitude = std::clamp(
			    (magnitude - deadzone) / (1.0 - deadzone),
			    0.0,
			    1.0);
			if (normalizedMagnitude <= 1e-9)
			{
				return XrVector2f{ 0.0F, 0.0F };
			}

			const double scale = normalizedMagnitude / magnitude;
			return XrVector2f{
				static_cast<float>(x * scale),
				static_cast<float>(y * scale)
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

		struct UiPanelTitleBarHitInfo final
		{
			bool isInsideTitleBar{ false };
			bool isInDragRegion{ false };
			bool isInMinimizeButton{ false };
			bool isInCloseButton{ false };
		};

		using UiPanelLocalRect=Common::UiText::PanelLocalRect;

		[[nodiscard]] Physics::Vector3 ClampUiPanelCenter(
		    const Physics::Vector3& center)
		{
			return Physics::Vector3{
				std::clamp(center.x, -1.45, 1.45),
				std::clamp(center.y, 0.55, 2.05),
				std::clamp(center.z, -2.20, -0.02)
			};
		}

		void BuildUiPanelAxesFromForward(
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

		[[nodiscard]] bool TryBuildPanelInnerLayout(
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

		[[nodiscard]] bool TryResolveOrientedPanelLocalPoint(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& panelHitPoint,
		    UiPanelInnerLayout* const outInnerLayout,
		    double* const outLocalX,
		    double* const outLocalY)
		{
			if ((outInnerLayout == nullptr) || (outLocalX == nullptr) ||
			    (outLocalY == nullptr))
			{
				return false;
			}

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return false;
			}

			const Physics::Vector3 relative = panelHitPoint - panelLayout.center;
			const double localX = Physics::Dot(relative, panelLayout.rightAxis);
			const double localY = Physics::Dot(relative, panelLayout.upAxis);
			if ((localX < innerLayout.innerLeft) || (localX > innerLayout.innerRight) ||
			    (localY > innerLayout.innerTop) ||
			    (localY < innerLayout.innerBottom))
			{
				return false;
			}

			*outInnerLayout = innerLayout;
			*outLocalX = localX;
			*outLocalY = localY;
			return true;
		}

		[[nodiscard]] UiPanelTitleBarHitInfo ResolveOrientedPanelTitleBarHitInfo(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& panelHitPoint)
		{
			UiPanelTitleBarHitInfo hitInfo{};
			if ((panelLayout.columns <= 0) || (panelLayout.rows <= 0))
			{
				return hitInfo;
			}

			UiPanelInnerLayout innerLayout{};
			double localX = 0.0;
			double localY = 0.0;
			if (!TryResolveOrientedPanelLocalPoint(
			        panelLayout,
			        panelHitPoint,
			        &innerLayout,
			        &localX,
			        &localY))
			{
				return hitInfo;
			}

			const double titleBarTop = innerLayout.innerTop;
			const double titleBarBottom = titleBarTop - innerLayout.cellHeight;
			if ((localY > titleBarTop) || (localY < titleBarBottom))
			{
				return hitInfo;
			}

			hitInfo.isInsideTitleBar = true;
			if (panelLayout.columns < 2)
			{
				hitInfo.isInDragRegion = true;
				return hitInfo;
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

			hitInfo.isInMinimizeButton =
			    (localX >= minimizeLeft) && (localX <= minimizeRight);
			hitInfo.isInCloseButton = (localX >= closeLeft) && (localX <= closeRight);
			hitInfo.isInDragRegion =
			    !hitInfo.isInMinimizeButton && !hitInfo.isInCloseButton;
			return hitInfo;
		}

		[[nodiscard]] bool IsPointInsidePanelLocalRect(
		    const UiPanelLocalRect& rect,
		    const double localX,
		    const double localY)
		{
			if ((rect.right <= rect.left) || (rect.top <= rect.bottom))
			{
				return false;
			}
			return (localX >= rect.left) && (localX <= rect.right) &&
			       (localY <= rect.top) && (localY >= rect.bottom);
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

		[[nodiscard]] bool TryBuildPitchPlaybackSliderRects(
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

		[[nodiscard]] bool TryResolvePitchPlaybackSliderPercentFromHit(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& panelHitPoint,
		    const int firstSliderCellIndex,
		    const int lastSliderCellIndex,
		    int* const playbackPercent)
		{
			if (playbackPercent == nullptr)
			{
				return false;
			}
			*playbackPercent = 100;

			UiPanelInnerLayout innerLayout{};
			double localX = 0.0;
			double localY = 0.0;
			if (!TryResolveOrientedPanelLocalPoint(
			        panelLayout,
			        panelHitPoint,
			        &innerLayout,
			        &localX,
			        &localY))
			{
				return false;
			}

			UiPanelLocalRect trackRect{};
			UiPanelLocalRect hitRect{};
			if (!TryBuildPitchPlaybackSliderRects(
			        panelLayout,
			        firstSliderCellIndex,
			        lastSliderCellIndex,
			        &trackRect,
			        &hitRect))
			{
				return false;
			}
			if (!IsPointInsidePanelLocalRect(hitRect, localX, localY))
			{
				return false;
			}

			const double normalized = std::clamp(
			    (localX - trackRect.left) /
			        (std::max)(1e-6, trackRect.right - trackRect.left),
			    0.0,
			    1.0);
			const int snappedPercent = static_cast<int>(std::round(
			                               (normalized * 200.0) / 5.0)) *
			                           5;
			*playbackPercent = std::clamp(snappedPercent, 0, 200);
			return true;
		}

		constexpr std::size_t MetricsControlButtonCount = Common::MetricsControlButtonCount;
		constexpr int BatMaterialControlButtonIndex = 12;

		[[nodiscard]] bool TryBuildMetricsPanelBodyLayout(
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

		[[nodiscard]] bool TryResolveMetricsPanelControlButtonIndex(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& panelHitPoint,
		    int* const buttonIndex)
		{
			if (buttonIndex == nullptr)
			{
				return false;
			}
			*buttonIndex = -1;

			UiPanelInnerLayout innerLayout{};
			double localX = 0.0;
			double localY = 0.0;
			if (!TryResolveOrientedPanelLocalPoint(
			        panelLayout,
			        panelHitPoint,
			        &innerLayout,
			        &localX,
			        &localY))
			{
				return false;
			}

			UiPanelLocalRect bodyRect{};
			std::array<UiPanelLocalRect, MetricsControlButtonCount> controlButtonRects{};
			if (!TryBuildMetricsPanelBodyLayout(
			        panelLayout,
			        &bodyRect,
			        &controlButtonRects))
			{
				return false;
			}

			const auto resolved = Common::FindMetricsPanelControl(panelLayout, panelHitPoint, controlButtonRects);
			if (!resolved) return false;
			*buttonIndex = *resolved;
			return true;
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

		[[nodiscard]] bool TryResolveOrientedPanelCellIndex(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& panelHitPoint,
		    int* const cellIndex)
		{
			if (cellIndex == nullptr) return false;
			const auto hit = Common::FindOrientedUiPanelCellIndex(panelLayout, panelHitPoint);
			*cellIndex = hit.value_or(-1);
			return hit.has_value();
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

		[[nodiscard]] bool TryIntersectOrientedPanel(
		    const OrientedUiPanelLayout& panelLayout,
		    const Physics::Vector3& rayOrigin,
		    const Physics::Vector3& rayDirection,
		    const double maxDistanceMeters,
		    double* const hitDistanceMeters,
		    Physics::Vector3* const hitPoint)
		{
			if ((hitDistanceMeters == nullptr) || (hitPoint == nullptr))
			{
				return false;
			}

			const double planeAlignment = Physics::Dot(
			    rayDirection,
			    panelLayout.forwardAxis);
			if (std::abs(planeAlignment) <= 1e-6)
			{
				return false;
			}

			const double distanceToPanel = Physics::Dot(
			                                   panelLayout.center - rayOrigin,
			                                   panelLayout.forwardAxis) /
			                               planeAlignment;
			if ((distanceToPanel <= 0.0) ||
			    (distanceToPanel > maxDistanceMeters))
			{
				return false;
			}

			const Physics::Vector3 intersectionPoint =
			    rayOrigin + (rayDirection * distanceToPanel);
			const Physics::Vector3 relative = intersectionPoint - panelLayout.center;
			const double localX = Physics::Dot(relative, panelLayout.rightAxis);
			const double localY = Physics::Dot(relative, panelLayout.upAxis);
			const double halfWidth = panelLayout.widthMeters * 0.5;
			const double halfHeight = panelLayout.heightMeters * 0.5;
			if ((localX < -halfWidth) || (localX > halfWidth) ||
			    (localY < -halfHeight) || (localY > halfHeight))
			{
				return false;
			}

			*hitDistanceMeters = distanceToPanel;
			*hitPoint = intersectionPoint;
			return true;
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

		constexpr double UiMainPanelAnchorDistanceMeters = 0.008;
		constexpr double UiMainPanelAnchorDownOffsetMeters = -0.002;
		constexpr double UiMainPanelMinDistanceFromHeadMeters = 0.16;
		constexpr double UiPanelArcMinYawRadians = -1.35;
		constexpr double UiPanelArcMaxYawRadians = 1.35;
		constexpr double DefaultPitchPanelArcYawRadians = -0.30;
		constexpr double DefaultPitchPanelArcRadiusMeters = 0.72;
		constexpr double DefaultPitchPanelHeightOffsetMeters = -0.18;
		constexpr double DefaultPitchMetricsPanelArcYawRadians = 0.16;
		constexpr double DefaultPitchMetricsPanelArcRadiusMeters = 0.68;
		constexpr double DefaultPitchMetricsPanelHeightOffsetMeters = 0.00;
		constexpr double DefaultBattingMetricsPanelArcYawRadians = 0.50;
		constexpr double DefaultBattingMetricsPanelArcRadiusMeters = 0.72;
		constexpr double DefaultBattingMetricsPanelHeightOffsetMeters = -0.10;
		constexpr double DefaultMetricsPanelArcYawRadians =
		    DefaultPitchMetricsPanelArcYawRadians;
		constexpr double DefaultMetricsPanelArcRadiusMeters =
		    DefaultPitchMetricsPanelArcRadiusMeters;
		constexpr double DefaultMetricsPanelHeightOffsetMeters =
		    DefaultPitchMetricsPanelHeightOffsetMeters;
		constexpr double MoveSpeedMetersPerSecond = 2.00;
		constexpr double ViewYawSpeedRadiansPerSecond = 1.85;
		constexpr double MaxLocomotionDeltaSeconds = 0.10;
		constexpr int PitchZoneColumns = 5;
		constexpr int PitchZoneRows = 5;
		constexpr int PitchZoneCellCount = PitchZoneColumns * PitchZoneRows;
		constexpr int PitchTitleCellCount = 5;
		constexpr int PitchZoneCellStartIndex = PitchTitleCellCount;
		constexpr int PitchTypePreviousCellIndex =
		    PitchZoneCellStartIndex + PitchZoneCellCount;
		constexpr int PitchTypeNameCellIndex = PitchTypePreviousCellIndex + 1;
		constexpr int PitchTypeNextCellIndex = PitchTypePreviousCellIndex + 2;
		constexpr int PitchPlaybackToggleCellIndex =
		    PitchTypePreviousCellIndex + 3;
		constexpr int PitchRandomPitchModeCellIndex =
		    PitchTypePreviousCellIndex + 4;
		constexpr int PitchPlaybackSliderCellStartIndex =
		    PitchRandomPitchModeCellIndex + 1;
		constexpr int PitchPlaybackSliderCellCount = 4;
		constexpr int PitchZoneRandomCellIndex =
		    PitchPlaybackSliderCellStartIndex + PitchPlaybackSliderCellCount;
		constexpr int PitchSettingsPreviousCellIndex =
		    PitchZoneRandomCellIndex + 1;
		constexpr int PitchSettingsNextCellIndex =
		    PitchSettingsPreviousCellIndex + 2;
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

		[[nodiscard]] Physics::Vector3 ComputeArcPanelCenterFromHead(
		    const Physics::Vector3& headPosition,
		    const Physics::Vector3& baseForward,
		    const double yawRadians,
		    const double radiusMeters,
		    const double heightOffsetMeters)
		{
			Physics::Vector3 horizontalForward{
				baseForward.x,
				0.0,
				baseForward.z
			};
			horizontalForward = Physics::NormalizeOrZero(horizontalForward);
			if (Physics::Length(horizontalForward) <= 1e-6)
			{
				horizontalForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
			}

			Physics::Vector3 horizontalRight = Physics::NormalizeOrZero(
			    Physics::Cross(
			        Physics::Vector3{ 0.0, 1.0, 0.0 },
			        horizontalForward));
			if (Physics::Length(horizontalRight) <= 1e-6)
			{
				horizontalRight = Physics::Vector3{ 1.0, 0.0, 0.0 };
			}

			Physics::Vector3 arcDirection =
			    (horizontalForward * std::cos(yawRadians)) +
			    (horizontalRight * std::sin(yawRadians));
			arcDirection = Physics::NormalizeOrZero(arcDirection);
			if (Physics::Length(arcDirection) <= 1e-6)
			{
				arcDirection = horizontalForward;
			}

			return headPosition +
			       (arcDirection * std::clamp(radiusMeters, 0.40, 1.10)) +
			       Physics::Vector3{ 0.0, heightOffsetMeters, 0.0 };
		}

		[[nodiscard]] Physics::Vector3 ResolveHorizontalForward(
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

		[[nodiscard]] Physics::Vector3 ResolvePanelFacingForward(
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

		[[nodiscard]] Physics::Vector3 ComputeHorizontalArcForwardFromHeadView(
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

		[[nodiscard]] Physics::Vector3 EnsureUiDistanceFromHead(
		    const Physics::Vector3& center,
		    const Common::ViewProjectionState& viewState,
		    const Physics::Vector3& fallbackDirection,
		    const double minDistanceMeters)
		{
			if (!viewState.isValid)
			{
				return center;
			}

			const Physics::Vector3 toCenter = center - viewState.position;
			const double currentDistance = Physics::Length(toCenter);
			if (currentDistance >= minDistanceMeters)
			{
				return center;
			}

			Physics::Vector3 pushDirection = Physics::NormalizeOrZero(toCenter);
			if (Physics::Length(pushDirection) <= 1e-6)
			{
				pushDirection = Physics::NormalizeOrZero(fallbackDirection);
			}
			if (Physics::Length(pushDirection) <= 1e-6)
			{
				pushDirection = Physics::Vector3{ 0.0, 0.0, -1.0 };
			}
			return viewState.position +
			       (pushDirection * std::clamp(minDistanceMeters, 0.05, 1.0));
		}

		[[nodiscard]] OrientedUiPanelLayout BuildMainPanelLayout(
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

		[[nodiscard]] OrientedUiPanelLayout BuildPitchPanelLayout(
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

		[[nodiscard]] OrientedUiPanelLayout BuildPitchMetricsPanelLayout(
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

		[[nodiscard]] OrientedUiPanelLayout BuildBattingMetricsPanelLayout(
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

		[[nodiscard]] OrientedUiPanelLayout BuildMetricsPanelLayout(
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
	} // namespace

	bool NullXrSystem::Initialize([[maybe_unused]] const HWND windowHandle)
	{
		const std::wstring runtimePath = QueryActiveRuntimePath();
		m_runtimeName = BuildRuntimeDisplayName(runtimePath);
		m_sessionName = L"Session(unavailable)";

#if defined(_WIN64)
		TryInitializeOpenXrInstance();
		TryInitializeOpenXrSystem();
		UpdateSessionName();
#endif

		m_isInitialized = true;
		return true;
	}

	bool NullXrSystem::AttachGraphicsBinding(
	    const Common::GraphicsBinding& graphicsBinding)
	{
#if defined(_WIN64)
		if (!m_isInitialized)
		{
			return false;
		}

		m_graphicsBinding = graphicsBinding;
		m_hasGraphicsBinding = true;
		const bool created = TryInitializeOpenXrSession(graphicsBinding);
		UpdateSessionName();
		return created;
#else
		(void)graphicsBinding;
		return false;
#endif
	}

	void NullXrSystem::SetBallState(const Physics::BallState& ballState)
	{
#if defined(_WIN64)
		m_latestBallState = ballState;
		AppendTrajectoryPoint(ballState);
#else
		(void)ballState;
#endif
	}

	void NullXrSystem::SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility)
	{
		m_overlayVisibility = overlayVisibility;
	}

	void NullXrSystem::SetRenderQuality(
	    const Common::RenderQualitySettings& renderQuality)
	{
		m_renderQuality = Common::NormalizeRenderQualitySettings(renderQuality);
	}

	void NullXrSystem::SetDuelZonePassMarker(
	    const std::optional<Physics::Vector3>& zonePassMarker)
	{
#if defined(_WIN64)
		m_duelZonePassMarker = zonePassMarker;
#else
		(void)zonePassMarker;
#endif
	}

	void NullXrSystem::SetBatOverlayState(
	    const std::optional<Common::BatOverlayState>& batOverlayState)
	{
#if defined(_WIN64)
		m_batOverlayState = batOverlayState;
#else
		(void)batOverlayState;
#endif
	}

	void NullXrSystem::SetUiTelemetry(const UiTelemetry& telemetry)
	{
#if defined(_WIN64)
		m_uiTelemetry = telemetry;
		m_uiTelemetry.pitchDistanceMeters =
		    std::clamp(
		        m_uiTelemetry.pitchDistanceMeters,
		        Common::RuntimeLimits::MinimumPitchDistanceMeters,
		        Common::RuntimeLimits::MaximumPitchDistanceMeters);
		m_uiTelemetry.duelIntervalSeconds =
		    std::clamp(
		        m_uiTelemetry.duelIntervalSeconds,
		        Common::RuntimeLimits::MinimumDuelPitchIntervalSeconds,
		        Common::RuntimeLimits::MaximumDuelPitchIntervalSeconds);
		m_uiTelemetry.playbackSpeedPercent =
		    std::clamp(
		        m_uiTelemetry.playbackSpeedPercent,
		        Common::RuntimeLimits::MinimumPlaybackSpeedPercent,
		        Common::RuntimeLimits::MaximumPlaybackSpeedPercent);
		m_uiTelemetry.moundHeightOffsetMeters = std::clamp(
		    m_uiTelemetry.moundHeightOffsetMeters,
		    0.0,
		    0.50);
		m_uiTelemetry.batAngleOffsetDegrees = std::clamp(
		    m_uiTelemetry.batAngleOffsetDegrees,
		    -35.0,
		    35.0);
		m_uiTelemetry.batYawOffsetDegrees = std::clamp(
		    m_uiTelemetry.batYawOffsetDegrees,
		    -35.0,
		    35.0);
		m_uiTelemetry.pitchMetricsSpeedMetersPerSecond = (std::max)(0.0,
		                                                            m_uiTelemetry.pitchMetricsSpeedMetersPerSecond);
		m_uiTelemetry.battedFirstGroundDistanceMeters = (std::max)(0.0,
		                                                           m_uiTelemetry.battedFirstGroundDistanceMeters);
		m_uiTelemetry.battedMaxHeightMeters = (std::max)(0.0,
		                                                 m_uiTelemetry.battedMaxHeightMeters);
#else
		(void)telemetry;
#endif
	}

	void NullXrSystem::PollInput()
	{
		m_floorEyeHeightSample = {};
#if defined(_WIN64)
		m_floorEyeHeightSample.referenceSpaceRevision = m_floorReferenceRevision;
		m_floorEyeHeightSample.isFloorReferenced = m_sessionHandle != XR_NULL_HANDLE &&
		    (m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE || m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR);
		m_leftHandPose = {};
		m_rightHandPose = {};
#endif
		if (!m_isInitialized)
		{
			return;
		}

#if defined(_WIN64)
		if (m_isFrameBegun)
		{
			// The caller skipped SubmitFrame; every begun frame must still end.
			EndFrame(false);
		}
		m_frameTiming = {};
		TryRecover();
		if (m_instanceHandle == XR_NULL_HANDLE)
		{
			return;
		}

		while (true)
		{
			XrEventDataBuffer eventData{ XR_TYPE_EVENT_DATA_BUFFER };
			const XrResult pollResult = xrPollEvent(m_instanceHandle, &eventData);
			if (pollResult == XR_EVENT_UNAVAILABLE)
			{
				break;
			}

			if (XR_FAILED(pollResult))
			{
				m_sessionName = L"Session(unavailable): " +
				                BuildXrErrorMessage(L"xrPollEvent", pollResult);
				break;
			}

			if (eventData.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
			{
				const auto* changed = reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&eventData);
				m_pendingReferenceChangeTime = (std::max)(m_pendingReferenceChangeTime, changed->changeTime);
				++m_floorReferenceRevision;
				++m_handReferenceRevision;
				m_floorEyeHeightSample = {};
			}
			if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
			{
				const auto* sessionStateChanged =
				    reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
				HandleSessionStateChanged(sessionStateChanged->state);
			}
			else if (eventData.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
			{
				HandleInteractionProfileChanged();
			}
			else if (eventData.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
			{
				// The runtime is going away (e.g. an update or restart): release
				// everything and try to create a fresh instance later.
				OutputDebugStringW(L"[OpenXR] instance loss pending; tearing down and retrying.\n");
				DestroyOpenXrInstance();
				m_recoveryPending = true;
				m_nextRecoveryAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
				m_sessionName = L"Session(recovering): instance loss pending";
				return;
			}
			if (m_instanceHandle == XR_NULL_HANDLE)
			{
				return;
			}
		}

		TryBeginFrame();
#endif
	}

	bool NullXrSystem::UpdateReferenceSpace()
	{
#if defined(_WIN64)
		if ((m_sessionHandle == XR_NULL_HANDLE) ||
		    (m_referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE &&
		     m_referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR &&
		     m_referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL))
		{
			return false;
		}

		const double moundViewHeightOffsetMeters =
		    m_uiTelemetry.moundHeightOffsetEnabled
		        ? m_uiTelemetry.moundHeightOffsetMeters
		        : 0.0;
		Physics::Vector3 referenceSpacePositionOffset = m_locomotionPositionOffset;
		referenceSpacePositionOffset.y += moundViewHeightOffsetMeters;
		const Physics::Vector3 offsetDelta =
		    referenceSpacePositionOffset - m_appliedReferenceSpacePositionOffset;
		if ((std::abs(m_viewYawOffsetRadians - m_appliedReferenceSpaceYawOffsetRadians) <= 1e-9) &&
		    (Physics::Length(offsetDelta) <= 1e-9) &&
		    (m_referenceSpaceHandle != XR_NULL_HANDLE))
		{
			return true;
		}

		const XrQuaternionf yawOrientation = BuildYawOffsetOrientation(
		    m_viewYawOffsetRadians);
		const XrQuaternionf inverseYawOrientation = NormalizeQuaternion(XrQuaternionf{
		    -yawOrientation.x,
		    -yawOrientation.y,
		    -yawOrientation.z,
		    yawOrientation.w });
		const Physics::Vector3 inverseOffset = RotateVectorByQuaternion(
		    referenceSpacePositionOffset * -1.0,
		    inverseYawOrientation);

		XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{
			XR_TYPE_REFERENCE_SPACE_CREATE_INFO
		};
		referenceSpaceCreateInfo.referenceSpaceType = m_referenceSpaceType;
		referenceSpaceCreateInfo.poseInReferenceSpace.orientation =
		    inverseYawOrientation;
		referenceSpaceCreateInfo.poseInReferenceSpace.position = XrVector3f{
			static_cast<float>(inverseOffset.x),
			static_cast<float>(inverseOffset.y),
			static_cast<float>(inverseOffset.z)
		};

		XrSpace updatedReferenceSpaceHandle = XR_NULL_HANDLE;
		const XrResult referenceSpaceResult = xrCreateReferenceSpace(
		    m_sessionHandle,
		    &referenceSpaceCreateInfo,
		    &updatedReferenceSpaceHandle);
		if (XR_FAILED(referenceSpaceResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(
			                    L"xrCreateReferenceSpace",
			                    referenceSpaceResult);
			return false;
		}

		if (m_referenceSpaceHandle != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_referenceSpaceHandle));
		}

		// Poses are located in the recreated space, which keeps the world fixed, so
		// smooth stick locomotion stays continuous for bat sweeps. Only a jump
		// (mound toggle, recenter) would teleport the bat between two samples.
		constexpr double MaximumContinuousOffsetMeters = 0.05;
		constexpr double MaximumContinuousYawRadians = 5.0 * std::numbers::pi / 180.0;
		if ((Physics::Length(offsetDelta) > MaximumContinuousOffsetMeters) ||
		    (std::abs(m_viewYawOffsetRadians - m_appliedReferenceSpaceYawOffsetRadians) >
		     MaximumContinuousYawRadians))
		{
			++m_handReferenceRevision;
		}
		m_referenceSpaceHandle = updatedReferenceSpaceHandle;
		m_appliedReferenceSpaceYawOffsetRadians = m_viewYawOffsetRadians;
		m_appliedReferenceSpacePositionOffset = referenceSpacePositionOffset;
		return true;
#else
		return false;
#endif
	}

	std::vector<UiCommand> NullXrSystem::ConsumePendingUiCommands()
	{
#if defined(_WIN64)
		std::vector<UiCommand> consumedCommands{};
		consumedCommands.swap(m_pendingUiCommands);
		return consumedCommands;
#else
		return {};
#endif
	}

	void NullXrSystem::Shutdown()
	{
#if defined(_WIN64)
		if (m_isSessionRunning && (m_sessionHandle != XR_NULL_HANDLE) && !m_exitRequested)
		{
			// Ask the runtime to stop the session and keep the frame loop alive
			// until it reports STOPPING, instead of ending a running session.
			if (m_isFrameBegun)
			{
				EndFrame(false);
			}
			if (XR_SUCCEEDED(xrRequestExitSession(m_sessionHandle)))
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
				while (m_isSessionRunning && (m_sessionHandle != XR_NULL_HANDLE) &&
				       (std::chrono::steady_clock::now() < deadline))
				{
					PollInput();
					if (m_isFrameBegun)
					{
						EndFrame(false);
					}
				}
			}
		}
		DestroyOpenXrInstance();
		m_duelZonePassMarker.reset();
		m_batOverlayState.reset();
		m_leftHandPose = HandPoseState{};
		m_rightHandPose = HandPoseState{};
#endif
		m_isInitialized = false;
	}

	std::wstring_view NullXrSystem::GetRuntimeName() const noexcept
	{
		return m_runtimeName;
	}

	std::wstring_view NullXrSystem::GetSessionName() const noexcept
	{
		return m_sessionName;
	}

	Common::ViewProjectionState NullXrSystem::GetLeftEyeViewState() const noexcept
	{
		return m_leftEyeViewState;
	}

	Common::FloorEyeHeightSample NullXrSystem::GetFloorEyeHeightSample() const noexcept
	{
		return m_floorEyeHeightSample;
	}

	HandPoseState NullXrSystem::GetLeftHandPose() const noexcept
	{
		return m_leftHandPose;
	}

	HandPoseState NullXrSystem::GetRightHandPose() const noexcept
	{
		return m_rightHandPose;
	}

	FrameTiming NullXrSystem::GetFrameTiming() const noexcept
	{
#if defined(_WIN64)
		return m_frameTiming;
#else
		return {};
#endif
	}

	bool NullXrSystem::IsInputFocused() const noexcept
	{
#if defined(_WIN64)
		// Only a running, merely VISIBLE session has system UI over the app.
		return !m_isSessionRunning || (m_sessionState == XR_SESSION_STATE_FOCUSED);
#else
		return true;
#endif
	}

	bool NullXrSystem::IsExitRequested() const noexcept
	{
#if defined(_WIN64)
		return m_exitRequested;
#else
		return false;
#endif
	}

	void NullXrSystem::ApplyHapticPulse(const bool rightHand, const double amplitude, const double durationSeconds)
	{
#if defined(_WIN64)
		if ((m_sessionHandle == XR_NULL_HANDLE) || (m_hapticAction == XR_NULL_HANDLE) ||
		    !m_isSessionRunning || !std::isfinite(amplitude) || !std::isfinite(durationSeconds))
		{
			return;
		}
		XrHapticActionInfo hapticInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
		hapticInfo.action = m_hapticAction;
		hapticInfo.subactionPath = rightHand ? m_rightHandPath : m_leftHandPath;
		XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
		vibration.amplitude = static_cast<float>(std::clamp(amplitude, 0.0, 1.0));
		vibration.duration = static_cast<XrDuration>(std::clamp(durationSeconds, 0.0, 1.0) * 1'000'000'000.0);
		vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
		(void)xrApplyHapticFeedback(m_sessionHandle, &hapticInfo,
		    reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
#else
		(void)rightHand;
		(void)amplitude;
		(void)durationSeconds;
#endif
	}

	std::wstring NullXrSystem::QueryActiveRuntimePath()
	{
		DWORD valueSizeInBytes = 0;
		LONG queryResult = RegGetValueW(
		    HKEY_LOCAL_MACHINE,
		    OpenXrRegistryPath,
		    ActiveRuntimeValueName,
		    RRF_RT_REG_SZ,
		    nullptr,
		    nullptr,
		    &valueSizeInBytes);
		if ((queryResult != ERROR_SUCCESS) || (valueSizeInBytes == 0))
		{
			return std::wstring{};
		}

		std::wstring runtimePath(valueSizeInBytes / sizeof(wchar_t), L'\0');
		DWORD valueSizeForRead = valueSizeInBytes;
		queryResult = RegGetValueW(
		    HKEY_LOCAL_MACHINE,
		    OpenXrRegistryPath,
		    ActiveRuntimeValueName,
		    RRF_RT_REG_SZ,
		    nullptr,
		    runtimePath.data(),
		    &valueSizeForRead);
		if (queryResult != ERROR_SUCCESS)
		{
			return std::wstring{};
		}

		if (!runtimePath.empty() && (runtimePath.back() == L'\0'))
		{
			runtimePath.pop_back();
		}

		return runtimePath;
	}

	std::wstring NullXrSystem::BuildRuntimeDisplayName(const std::wstring& runtimePath)
	{
		if (runtimePath.empty())
		{
			return L"OpenXR runtime: not configured";
		}

		const DWORD attributes = GetFileAttributesW(runtimePath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			return L"OpenXR runtime: configured (file missing)";
		}

		const std::wstring runtimeFileName = ExtractFileName(runtimePath);
		return L"OpenXR runtime: " + runtimeFileName;
	}

#if defined(_WIN64)
	void NullXrSystem::TryInitializeOpenXrInstance()
	{
		if (m_instanceHandle != XR_NULL_HANDLE)
		{
			return;
		}

		bool hasD3d12Extension = false;
		m_hasLocalFloorExtension = false;
		uint32_t extensionCount = 0;
		const XrResult extensionCountResult = xrEnumerateInstanceExtensionProperties(
		    nullptr,
		    0,
		    &extensionCount,
		    nullptr);
		if (XR_SUCCEEDED(extensionCountResult) && (extensionCount > 0))
		{
			std::vector<XrExtensionProperties> extensionProperties(
			    extensionCount,
			    XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES });
			const XrResult extensionReadResult = xrEnumerateInstanceExtensionProperties(
			    nullptr,
			    extensionCount,
			    &extensionCount,
			    extensionProperties.data());
			if (XR_SUCCEEDED(extensionReadResult))
			{
				for (const auto& extensionProperty : extensionProperties)
				{
					if (std::strcmp(extensionProperty.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0)
					{
						hasD3d12Extension = true;
					}
					else if (std::strcmp(extensionProperty.extensionName, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME) == 0)
					{
						m_hasLocalFloorExtension = true;
					}
				}
			}
		}

		std::vector<const char*> enabledExtensions{};
		if (hasD3d12Extension)
		{
			enabledExtensions.push_back(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
		}
		if (m_hasLocalFloorExtension)
		{
			enabledExtensions.push_back(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
		}

		XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
		strncpy_s(createInfo.applicationInfo.applicationName, "BaseballSimulatorVR", _TRUNCATE);
		strncpy_s(createInfo.applicationInfo.engineName, "BaseballSimulatorVR", _TRUNCATE);
		createInfo.applicationInfo.applicationVersion = 1;
		createInfo.applicationInfo.engineVersion = 1;
		createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		createInfo.enabledExtensionNames = enabledExtensions.data();

		XrResult createResult = xrCreateInstance(&createInfo, &m_instanceHandle);
		if ((XR_FAILED(createResult) || (m_instanceHandle == XR_NULL_HANDLE)) &&
		    !enabledExtensions.empty())
		{
			enabledExtensions.clear();
			createInfo.enabledExtensionCount = 0;
			createInfo.enabledExtensionNames = nullptr;
			createResult = xrCreateInstance(&createInfo, &m_instanceHandle);
			hasD3d12Extension = false;
			m_hasLocalFloorExtension = false;
		}

		if (XR_FAILED(createResult) || (m_instanceHandle == XR_NULL_HANDLE))
		{
			m_runtimeName += L" | " + BuildXrErrorMessage(L"xrCreateInstance", createResult);
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrCreateInstance", createResult);
			m_instanceHandle = XR_NULL_HANDLE;
			return;
		}

		m_runtimeName = BuildRuntimeDisplayNameFromInstance(m_instanceHandle);

		if (hasD3d12Extension)
		{
			PFN_xrVoidFunction functionPointer = nullptr;
			const XrResult procAddressResult = xrGetInstanceProcAddr(
			    m_instanceHandle,
			    "xrGetD3D12GraphicsRequirementsKHR",
			    &functionPointer);
			if (XR_SUCCEEDED(procAddressResult))
			{
				m_xrGetD3d12GraphicsRequirementsKhr =
				    reinterpret_cast<PFN_xrGetD3D12GraphicsRequirementsKHR>(functionPointer);
			}
		}
		else
		{
			m_sessionName = L"Session(unavailable): XR_KHR_D3D12_enable missing";
		}
	}

	void NullXrSystem::TryInitializeOpenXrSystem()
	{
		if (m_instanceHandle == XR_NULL_HANDLE)
		{
			return;
		}

		if (m_systemId != XR_NULL_SYSTEM_ID)
		{
			return;
		}

		XrSystemGetInfo systemGetInfo{ XR_TYPE_SYSTEM_GET_INFO };
		systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

		const XrResult systemResult = xrGetSystem(m_instanceHandle, &systemGetInfo, &m_systemId);
		if (XR_FAILED(systemResult))
		{
			m_systemId = XR_NULL_SYSTEM_ID;
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrGetSystem", systemResult);
		}
	}

	bool NullXrSystem::TryInitializeOpenXrSession(
	    const Common::GraphicsBinding& graphicsBinding)
	{
		if (m_sessionHandle != XR_NULL_HANDLE)
		{
			return true;
		}

		if (m_instanceHandle == XR_NULL_HANDLE || m_systemId == XR_NULL_SYSTEM_ID)
		{
			m_sessionName = L"Session(unavailable): system not ready";
			return false;
		}

		if (m_xrGetD3d12GraphicsRequirementsKhr == nullptr)
		{
			m_sessionName = L"Session(unavailable): XR_KHR_D3D12_enable missing";
			return false;
		}

		if (!graphicsBinding.HasDevice())
		{
			m_sessionName = L"Session(unavailable): graphics device handle missing";
			return false;
		}

		XrGraphicsRequirementsD3D12KHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
		const XrResult requirementsResult = m_xrGetD3d12GraphicsRequirementsKhr(
		    m_instanceHandle,
		    m_systemId,
		    &graphicsRequirements);
		if (XR_FAILED(requirementsResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrGetD3D12GraphicsRequirementsKHR", requirementsResult);
			return false;
		}

		if (!TryInitializeGraphicsDevice(graphicsBinding, graphicsRequirements))
		{
			return false;
		}

		if ((m_d3d12Queue == nullptr) && !EnsureGraphicsQueueForCurrentDevice())
		{
			m_d3d12Device.Reset();
			m_d3d12Queue.Reset();
			m_usesDedicatedGraphicsDevice = false;
			return false;
		}

		XrGraphicsBindingD3D12KHR xrGraphicsBinding{
			XR_TYPE_GRAPHICS_BINDING_D3D12_KHR
		};
		xrGraphicsBinding.device = m_d3d12Device.Get();
		xrGraphicsBinding.queue = m_d3d12Queue.Get();

		XrSessionCreateInfo sessionCreateInfo{ XR_TYPE_SESSION_CREATE_INFO };
		sessionCreateInfo.next = &xrGraphicsBinding;
		sessionCreateInfo.systemId = m_systemId;

		const XrResult sessionResult = xrCreateSession(m_instanceHandle, &sessionCreateInfo, &m_sessionHandle);
		if (XR_FAILED(sessionResult))
		{
			m_sessionHandle = XR_NULL_HANDLE;
			m_d3d12Device.Reset();
			m_d3d12Queue.Reset();
			m_usesDedicatedGraphicsDevice = false;
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrCreateSession", sessionResult);
			return false;
		}

		++m_floorReferenceRevision;
		++m_handReferenceRevision;
		m_floorEyeHeightSample = {};
		m_pendingReferenceChangeTime = 0;
		XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		referenceSpaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.orientation.x = 0.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.orientation.y = 0.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.orientation.z = 0.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.position.x = 0.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.position.y = 0.0F;
		referenceSpaceCreateInfo.poseInReferenceSpace.position.z = 0.0F;

		auto tryCreateReferenceSpace = [&](const XrReferenceSpaceType referenceSpaceType) -> XrResult
		{
			referenceSpaceCreateInfo.referenceSpaceType = referenceSpaceType;
			return xrCreateReferenceSpace(m_sessionHandle, &referenceSpaceCreateInfo, &m_referenceSpaceHandle);
		};

		m_referenceSpaceHandle = XR_NULL_HANDLE;
		m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		// Prefer floor-anchored spaces so the strike zone and ground sit at the real floor.
		XrResult referenceSpaceResult = tryCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_STAGE);
		if (XR_SUCCEEDED(referenceSpaceResult))
		{
			m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		}
		else if (m_hasLocalFloorExtension &&
		         XR_SUCCEEDED(referenceSpaceResult = tryCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR)))
		{
			m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
		}
		else
		{
			referenceSpaceResult = tryCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL);
		}

		if (XR_FAILED(referenceSpaceResult))
		{
			LogXrCleanupFailure(
			    L"xrDestroySession",
			    xrDestroySession(m_sessionHandle));
			m_sessionHandle = XR_NULL_HANDLE;
			m_referenceSpaceHandle = XR_NULL_HANDLE;
			m_d3d12Device.Reset();
			m_d3d12Queue.Reset();
			m_usesDedicatedGraphicsDevice = false;
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrCreateReferenceSpace", referenceSpaceResult);
			return false;
		}

		if (!InitializeProjectionSwapchains())
		{
			if (m_referenceSpaceHandle != XR_NULL_HANDLE)
			{
				LogXrCleanupFailure(
				    L"xrDestroySpace",
				    xrDestroySpace(m_referenceSpaceHandle));
				m_referenceSpaceHandle = XR_NULL_HANDLE;
			}
			LogXrCleanupFailure(
			    L"xrDestroySession",
			    xrDestroySession(m_sessionHandle));
			m_sessionHandle = XR_NULL_HANDLE;
			m_d3d12Device.Reset();
			m_d3d12Queue.Reset();
			m_usesDedicatedGraphicsDevice = false;
			return false;
		}

		if (!InitializeInputActions())
		{
			OutputDebugStringW(
			    L"[OpenXR] input action initialization failed; continuing without VR panel input.\n");
		}

		m_isGraphicsBound = true;
		m_isSessionRunning = false;
		m_sessionState = XR_SESSION_STATE_UNKNOWN;
		return true;
	}

	bool NullXrSystem::TryInitializeGraphicsDevice(
	    const Common::GraphicsBinding& graphicsBinding,
	    const XrGraphicsRequirementsD3D12KHR& graphicsRequirements)
	{
		auto* providedDevice = graphicsBinding.device;
		if (providedDevice == nullptr)
		{
			m_sessionName = L"Session(unavailable): provided D3D12 device missing";
			return false;
		}

		const LUID providedAdapterLuid = providedDevice->GetAdapterLuid();
		if (IsSameLuid(providedAdapterLuid, graphicsRequirements.adapterLuid))
		{
			m_d3d12Device = providedDevice;
			m_d3d12Queue = graphicsBinding.queue;
			m_usesDedicatedGraphicsDevice = false;
			return true;
		}

		if (!TryCreateGraphicsDeviceFromAdapterLuid(
		        graphicsRequirements.adapterLuid,
		        graphicsRequirements.minFeatureLevel))
		{
			return false;
		}

		m_usesDedicatedGraphicsDevice = true;
		m_d3d12Queue.Reset();
		return true;
	}

	bool NullXrSystem::TryCreateGraphicsDeviceFromAdapterLuid(
	    const LUID adapterLuid,
	    const D3D_FEATURE_LEVEL minimumFeatureLevel)
	{
		Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory{};
		const HRESULT factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
		if (FAILED(factoryResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"CreateDXGIFactory2", factoryResult);
			return false;
		}

		Microsoft::WRL::ComPtr<IDXGIAdapter1> matchingAdapter{};
		for (UINT adapterIndex = 0;; ++adapterIndex)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapterCandidate{};
			const HRESULT enumResult = dxgiFactory->EnumAdapters1(adapterIndex, &adapterCandidate);
			if (enumResult == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			if (FAILED(enumResult))
			{
				m_sessionName = L"Session(unavailable): " +
				                BuildD3d12ErrorMessage(L"IDXGIFactory7::EnumAdapters1", enumResult);
				return false;
			}

			DXGI_ADAPTER_DESC1 adapterDescription{};
			if (FAILED(adapterCandidate->GetDesc1(&adapterDescription)))
			{
				continue;
			}

			if (IsSameLuid(adapterDescription.AdapterLuid, adapterLuid))
			{
				matchingAdapter = adapterCandidate;
				break;
			}
		}

		if (matchingAdapter == nullptr)
		{
			m_sessionName = L"Session(unavailable): required adapter not found";
			return false;
		}

		m_d3d12Device.Reset();
		const D3D_FEATURE_LEVEL featureLevel = (minimumFeatureLevel == 0)
		                                           ? D3D_FEATURE_LEVEL_11_0
		                                           : minimumFeatureLevel;
		const HRESULT deviceResult = D3D12CreateDevice(
		    matchingAdapter.Get(),
		    featureLevel,
		    IID_PPV_ARGS(&m_d3d12Device));
		if (FAILED(deviceResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"D3D12CreateDevice", deviceResult);
			return false;
		}

		return true;
	}

	bool NullXrSystem::EnsureGraphicsQueueForCurrentDevice()
	{
		if (m_d3d12Device == nullptr)
		{
			m_sessionName = L"Session(unavailable): D3D12 device unavailable";
			return false;
		}

		m_d3d12Queue.Reset();
		D3D12_COMMAND_QUEUE_DESC queueDescription{};
		queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

		const HRESULT queueResult = m_d3d12Device->CreateCommandQueue(
		    &queueDescription,
		    IID_PPV_ARGS(&m_d3d12Queue));
		if (FAILED(queueResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12Device::CreateCommandQueue", queueResult);
			return false;
		}

		return true;
	}

	std::optional<DXGI_FORMAT> NullXrSystem::SelectSwapchainFormat() const
	{
		uint32_t formatCount = 0;
		const XrResult formatCountResult = xrEnumerateSwapchainFormats(
		    m_sessionHandle,
		    0,
		    &formatCount,
		    nullptr);
		if (XR_FAILED(formatCountResult) || (formatCount == 0))
		{
			return std::nullopt;
		}

		std::vector<int64_t> runtimeFormats(formatCount);
		const XrResult formatReadResult = xrEnumerateSwapchainFormats(
		    m_sessionHandle,
		    formatCount,
		    &formatCount,
		    runtimeFormats.data());
		if (XR_FAILED(formatReadResult) || runtimeFormats.empty())
		{
			return std::nullopt;
		}

		for (const DXGI_FORMAT preferredFormat : PreferredSwapchainFormats)
		{
			const int64_t targetFormat = static_cast<int64_t>(preferredFormat);
			if (std::find(runtimeFormats.begin(), runtimeFormats.end(), targetFormat) != runtimeFormats.end())
			{
				return preferredFormat;
			}
		}

		return static_cast<DXGI_FORMAT>(runtimeFormats.front());
	}

	bool NullXrSystem::InitializeProjectionSwapchains()
	{
		if (m_sessionHandle == XR_NULL_HANDLE || m_d3d12Device == nullptr)
		{
			m_sessionName = L"Session(unavailable): session/device missing";
			return false;
		}

		const std::optional<DXGI_FORMAT> selectedSwapchainFormat =
		    SelectSwapchainFormat();
		if (!selectedSwapchainFormat.has_value())
		{
			m_sessionName = L"Session(unavailable): xrEnumerateSwapchainFormats failed";
			return false;
		}
		m_colorSwapchainFormat = *selectedSwapchainFormat;

		uint32_t viewConfigurationCount = 0;
		const XrResult viewCountResult = xrEnumerateViewConfigurationViews(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    0,
		    &viewConfigurationCount,
		    nullptr);
		if (XR_FAILED(viewCountResult) || (viewConfigurationCount == 0))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateViewConfigurationViews", viewCountResult);
			return false;
		}

		m_viewConfigurationViews.assign(
		    viewConfigurationCount,
		    XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
		const XrResult viewReadResult = xrEnumerateViewConfigurationViews(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    viewConfigurationCount,
		    &viewConfigurationCount,
		    m_viewConfigurationViews.data());
		if (XR_FAILED(viewReadResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateViewConfigurationViews", viewReadResult);
			return false;
		}

		uint32_t blendModeCount = 0;
		const XrResult blendModeCountResult = xrEnumerateEnvironmentBlendModes(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    0,
		    &blendModeCount,
		    nullptr);
		if (XR_FAILED(blendModeCountResult) || (blendModeCount == 0))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateEnvironmentBlendModes", blendModeCountResult);
			return false;
		}

		std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
		const XrResult blendModeReadResult = xrEnumerateEnvironmentBlendModes(
		    m_instanceHandle,
		    m_systemId,
		    PrimaryViewConfigurationType,
		    blendModeCount,
		    &blendModeCount,
		    blendModes.data());
		if (XR_FAILED(blendModeReadResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEnumerateEnvironmentBlendModes", blendModeReadResult);
			return false;
		}

		m_environmentBlendMode = blendModes.front();
		for (const XrEnvironmentBlendMode blendMode : blendModes)
		{
			if (blendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
			{
				m_environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				break;
			}
		}

		m_views.assign(viewConfigurationCount, XrView{ XR_TYPE_VIEW });
		m_projectionLayerViews.assign(
		    viewConfigurationCount,
		    XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
		m_projectionSwapchains.clear();
		m_projectionSwapchains.reserve(viewConfigurationCount);

		m_rtvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_srvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_projectionSampleCount = 1U;
		if (!m_viewConfigurationViews.empty())
		{
			m_projectionSampleCount = (std::max)(1U,
			                                     m_viewConfigurationViews.front().recommendedSwapchainSampleCount);
			for (const XrViewConfigurationView& viewConfiguration : m_viewConfigurationViews)
			{
				const uint32_t recommendedSampleCount = (std::max)(1U,
				                                                   viewConfiguration.recommendedSwapchainSampleCount);
				m_projectionSampleCount =
				    (std::min)(m_projectionSampleCount, recommendedSampleCount);
			}
		}

		for (uint32_t viewIndex = 0; viewIndex < viewConfigurationCount; ++viewIndex)
		{
			const XrViewConfigurationView& viewConfiguration = m_viewConfigurationViews[viewIndex];
			XrSwapchainCreateInfo swapchainCreateInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swapchainCreateInfo.createFlags = 0;
			swapchainCreateInfo.usageFlags =
			    XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			swapchainCreateInfo.format = static_cast<int64_t>(m_colorSwapchainFormat);
			swapchainCreateInfo.sampleCount = m_projectionSampleCount;
			if (viewConfiguration.recommendedImageRectWidth==0 || viewConfiguration.recommendedImageRectHeight==0 ||
			    viewConfiguration.recommendedImageRectWidth>static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
			    viewConfiguration.recommendedImageRectHeight>static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()))
			{
			    m_sessionName=L"Session(unavailable): OpenXR recommended eye resolution is invalid";
			    DestroyProjectionSwapchains();
			    return false;
			}
			swapchainCreateInfo.width=viewConfiguration.recommendedImageRectWidth;
			swapchainCreateInfo.height=viewConfiguration.recommendedImageRectHeight;
			std::wostringstream resolutionLog;
			resolutionLog << L"[OpenXR] eye " << viewIndex << L": " << swapchainCreateInfo.width << L"x"
			    << swapchainCreateInfo.height << L", MSAA " << m_projectionSampleCount
			    << L", FXAA blend " << m_renderQuality.fxaaBlendFactor << L", UI coverage AA with adaptive font density\n";
			OutputDebugStringW(resolutionLog.str().c_str());
			swapchainCreateInfo.faceCount = 1;
			swapchainCreateInfo.arraySize = 1;
			swapchainCreateInfo.mipCount = 1;

			SwapchainContext swapchainContext{};
			swapchainContext.width = static_cast<int32_t>(swapchainCreateInfo.width);
			swapchainContext.height = static_cast<int32_t>(swapchainCreateInfo.height);
			const auto cleanupCurrentSwapchainAndFail =
			    [&](const std::wstring& failureMessage) -> bool
			{
				m_sessionName = failureMessage;
				if (swapchainContext.swapchainHandle != XR_NULL_HANDLE)
				{
					LogXrCleanupFailure(
					    L"xrDestroySwapchain",
					    xrDestroySwapchain(swapchainContext.swapchainHandle));
					swapchainContext.swapchainHandle = XR_NULL_HANDLE;
				}
				DestroyProjectionSwapchains();
				return false;
			};

			const XrResult swapchainCreateResult = xrCreateSwapchain(
			    m_sessionHandle,
			    &swapchainCreateInfo,
			    &swapchainContext.swapchainHandle);
			if (XR_FAILED(swapchainCreateResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrCreateSwapchain",
				        swapchainCreateResult));
			}

			uint32_t swapchainImageCount = 0;
			const XrResult imageCountResult = xrEnumerateSwapchainImages(
			    swapchainContext.swapchainHandle,
			    0,
			    &swapchainImageCount,
			    nullptr);
			if (XR_FAILED(imageCountResult) || (swapchainImageCount == 0))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrEnumerateSwapchainImages",
				        imageCountResult));
			}

			swapchainContext.images.assign(
			    swapchainImageCount,
			    XrSwapchainImageD3D12KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
			const XrResult imageReadResult = xrEnumerateSwapchainImages(
			    swapchainContext.swapchainHandle,
			    swapchainImageCount,
			    &swapchainImageCount,
			    reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainContext.images.data()));
			if (XR_FAILED(imageReadResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildXrErrorMessage(
				        L"xrEnumerateSwapchainImages",
				        imageReadResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
			rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDescription.NumDescriptors = swapchainImageCount;
			rtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			const HRESULT descriptorHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &rtvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.rtvHeap));
			if (FAILED(descriptorHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        descriptorHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
			dsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDescription.NumDescriptors = swapchainImageCount;
			dsvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			const HRESULT dsvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &dsvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.dsvHeap));
			if (FAILED(dsvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        dsvHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC sceneRtvHeapDescription{};
			sceneRtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			sceneRtvHeapDescription.NumDescriptors = swapchainImageCount;
			sceneRtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			const HRESULT sceneRtvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &sceneRtvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.sceneRtvHeap));
			if (FAILED(sceneRtvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        sceneRtvHeapResult));
			}

			D3D12_DESCRIPTOR_HEAP_DESC sceneSrvHeapDescription{};
			sceneSrvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			sceneSrvHeapDescription.NumDescriptors = swapchainImageCount;
			sceneSrvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			const HRESULT sceneSrvHeapResult = m_d3d12Device->CreateDescriptorHeap(
			    &sceneSrvHeapDescription,
			    IID_PPV_ARGS(&swapchainContext.sceneSrvHeap));
			if (FAILED(sceneSrvHeapResult))
			{
				return cleanupCurrentSwapchainAndFail(
				    L"Session(unavailable): " +
				    BuildD3d12ErrorMessage(
				        L"ID3D12Device::CreateDescriptorHeap",
				        sceneSrvHeapResult));
			}

			if (m_dsvDescriptorSize == 0)
			{
				m_dsvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
			    swapchainContext.rtvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
			    swapchainContext.dsvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle =
			    swapchainContext.sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvHandle =
			    swapchainContext.sceneSrvHeap->GetCPUDescriptorHandleForHeapStart();
			swapchainContext.depthBuffers.resize(swapchainImageCount);
			swapchainContext.sceneColorTargets.resize(swapchainImageCount);
			for (uint32_t imageIndex = 0; imageIndex < swapchainImageCount; ++imageIndex)
			{
				ID3D12Resource* renderTargetTexture = swapchainContext.images[imageIndex].texture;
				if (renderTargetTexture == nullptr)
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): swapchain texture missing");
				}

				D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
				renderTargetViewDesc.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				renderTargetViewDesc.ViewDimension =
				    (m_projectionSampleCount > 1U)
				        ? D3D12_RTV_DIMENSION_TEXTURE2DMS
				        : D3D12_RTV_DIMENSION_TEXTURE2D;
				if (renderTargetViewDesc.ViewDimension ==
				    D3D12_RTV_DIMENSION_TEXTURE2D)
				{
					renderTargetViewDesc.Texture2D.MipSlice = 0;
					renderTargetViewDesc.Texture2D.PlaneSlice = 0;
				}
				m_d3d12Device->CreateRenderTargetView(renderTargetTexture, &renderTargetViewDesc, rtvHandle);
				rtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);

				D3D12_HEAP_PROPERTIES sceneHeapProperties{};
				sceneHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				sceneHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				sceneHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				sceneHeapProperties.CreationNodeMask = 1;
				sceneHeapProperties.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC sceneColorDescription{};
				sceneColorDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				sceneColorDescription.Alignment = 0;
				sceneColorDescription.Width = static_cast<UINT64>(swapchainContext.width);
				sceneColorDescription.Height = static_cast<UINT>(swapchainContext.height);
				sceneColorDescription.DepthOrArraySize = 1;
				sceneColorDescription.MipLevels = 1;
				sceneColorDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneColorDescription.SampleDesc.Count = 1;
				sceneColorDescription.SampleDesc.Quality = 0;
				sceneColorDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				sceneColorDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				D3D12_CLEAR_VALUE sceneColorClearValue{};
				sceneColorClearValue.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneColorClearValue.Color[0] = LeftEyeClearColor[0];
				sceneColorClearValue.Color[1] = LeftEyeClearColor[1];
				sceneColorClearValue.Color[2] = LeftEyeClearColor[2];
				sceneColorClearValue.Color[3] = LeftEyeClearColor[3];

				const HRESULT sceneResourceResult = m_d3d12Device->CreateCommittedResource(
				    &sceneHeapProperties,
				    D3D12_HEAP_FLAG_NONE,
				    &sceneColorDescription,
				    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				    &sceneColorClearValue,
				    IID_PPV_ARGS(&swapchainContext.sceneColorTargets[imageIndex]));
				if (FAILED(sceneResourceResult))
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): " +
					    BuildD3d12ErrorMessage(
					        L"ID3D12Device::CreateCommittedResource",
					        sceneResourceResult));
				}

				D3D12_RENDER_TARGET_VIEW_DESC sceneRtvDescription{};
				sceneRtvDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneRtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				sceneRtvDescription.Texture2D.MipSlice = 0;
				sceneRtvDescription.Texture2D.PlaneSlice = 0;
				m_d3d12Device->CreateRenderTargetView(
				    swapchainContext.sceneColorTargets[imageIndex].Get(),
				    &sceneRtvDescription,
				    sceneRtvHandle);
				sceneRtvHandle.ptr += static_cast<SIZE_T>(m_rtvDescriptorSize);

				D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrvDescription{};
				sceneSrvDescription.Shader4ComponentMapping =
				    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				sceneSrvDescription.Format = ResolveRtvFormat(m_colorSwapchainFormat);
				sceneSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				sceneSrvDescription.Texture2D.MostDetailedMip = 0;
				sceneSrvDescription.Texture2D.MipLevels = 1;
				sceneSrvDescription.Texture2D.PlaneSlice = 0;
				sceneSrvDescription.Texture2D.ResourceMinLODClamp = 0.0F;
				m_d3d12Device->CreateShaderResourceView(
				    swapchainContext.sceneColorTargets[imageIndex].Get(),
				    &sceneSrvDescription,
				    sceneSrvHandle);
				sceneSrvHandle.ptr += static_cast<SIZE_T>(m_srvDescriptorSize);

				D3D12_HEAP_PROPERTIES depthHeapProperties{};
				depthHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				depthHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				depthHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				depthHeapProperties.CreationNodeMask = 1;
				depthHeapProperties.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC depthDescription{};
				depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				depthDescription.Alignment = 0;
				depthDescription.Width = static_cast<UINT64>(swapchainContext.width);
				depthDescription.Height = static_cast<UINT>(swapchainContext.height);
				depthDescription.DepthOrArraySize = 1;
				depthDescription.MipLevels = 1;
				depthDescription.Format = DepthBufferFormat;
				depthDescription.SampleDesc.Count = m_projectionSampleCount;
				depthDescription.SampleDesc.Quality = 0;
				depthDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

				D3D12_CLEAR_VALUE depthClearValue{};
				depthClearValue.Format = DepthBufferFormat;
				depthClearValue.DepthStencil.Depth = 1.0F;
				depthClearValue.DepthStencil.Stencil = 0;

				const HRESULT depthResourceResult = m_d3d12Device->CreateCommittedResource(
				    &depthHeapProperties,
				    D3D12_HEAP_FLAG_NONE,
				    &depthDescription,
				    D3D12_RESOURCE_STATE_DEPTH_WRITE,
				    &depthClearValue,
				    IID_PPV_ARGS(&swapchainContext.depthBuffers[imageIndex]));
				if (FAILED(depthResourceResult))
				{
					return cleanupCurrentSwapchainAndFail(
					    L"Session(unavailable): " +
					    BuildD3d12ErrorMessage(
					        L"ID3D12Device::CreateCommittedResource",
					        depthResourceResult));
				}

				D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription{};
				depthStencilViewDescription.Format = DepthBufferFormat;
				depthStencilViewDescription.ViewDimension =
				    (m_projectionSampleCount > 1U)
				        ? D3D12_DSV_DIMENSION_TEXTURE2DMS
				        : D3D12_DSV_DIMENSION_TEXTURE2D;
				depthStencilViewDescription.Flags = D3D12_DSV_FLAG_NONE;
				if (depthStencilViewDescription.ViewDimension ==
				    D3D12_DSV_DIMENSION_TEXTURE2D)
				{
					depthStencilViewDescription.Texture2D.MipSlice = 0;
				}
				m_d3d12Device->CreateDepthStencilView(
				    swapchainContext.depthBuffers[imageIndex].Get(),
				    &depthStencilViewDescription,
				    dsvHandle);
				dsvHandle.ptr += static_cast<SIZE_T>(m_dsvDescriptorSize);
			}

			m_projectionSwapchains.push_back(std::move(swapchainContext));
		}

		if (EnableDebugDraw && !InitializeDebugPipeline())
		{
			DestroyProjectionSwapchains();
			return false;
		}
		for (auto& swapchain : m_projectionSwapchains)
		{
			for (auto& frame : swapchain.frames)
			{
				const HRESULT result = frame.Initialize(m_d3d12Device.Get(),
				    ResolveRtvFormat(m_colorSwapchainFormat), DepthBufferFormat,
				    m_projectionSampleCount, MaxDebugVertices);
				if (FAILED(result))
				{
					m_sessionName = L"Session(unavailable): " + BuildD3d12ErrorMessage(L"XR per-view resources", result);
					DestroyProjectionSwapchains();
					return false;
				}
			}
		}

		m_isFxaaAvailable = InitializeFxaaPipeline();
		if (!m_isFxaaAvailable)
		{
			OutputDebugStringW(
			    L"[OpenXR] FXAA pipeline init failed; falling back to direct/copy post-process.\n");
		}
		m_xrPostProcessStatus = m_isFxaaAvailable ? L"fxaa" : L"direct";

		const HRESULT fenceCreateResult = m_d3d12Device->CreateFence(
		    0,
		    D3D12_FENCE_FLAG_NONE,
		    IID_PPV_ARGS(&m_xrFence));
		if (FAILED(fenceCreateResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12Device::CreateFence", fenceCreateResult);
			DestroyProjectionSwapchains();
			return false;
		}

		m_xrFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_xrFenceEvent == nullptr)
		{
			m_sessionName = L"Session(unavailable): CreateEventW failed";
			DestroyProjectionSwapchains();
			return false;
		}

		m_xrFenceValue = 1;
		return true;
	}

	bool NullXrSystem::InitializeDebugPipeline()
	{
		if (m_d3d12Device == nullptr)
		{
			m_lastProjectionLayerStatus = L"pipeline-init-device-missing";
			m_sessionName = L"Session(unavailable): D3D12 device unavailable";
			return false;
		}

		Common::DebugOverlay::PipelineInitializationPhase failedPhase =
		    Common::DebugOverlay::PipelineInitializationPhase::None;
		HRESULT failedResult = S_OK;
		if (!Common::DebugOverlay::InitializePipeline(
		        m_d3d12Device.Get(),
		        ResolveRtvFormat(m_colorSwapchainFormat),
		        DepthBufferFormat,
		        &m_debugRootSignature,
		        &m_debugPipelineState,
		        &failedPhase,
		        &failedResult,
		        Common::DebugOverlay::PipelineBlendMode::Opaque,
		        Common::DebugOverlay::PipelineDepthWriteMode::Enabled,
		        false,
		        (std::max)(1U, m_projectionSampleCount)))
		{
			m_lastProjectionLayerStatus = L"pipeline-init-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildDebugPipelineInitializationErrorMessage(
			                    failedPhase,
			                    failedResult);
			return false;
		}
		if ((m_debugRootSignature.Get() == nullptr) ||
		    (m_debugPipelineState.Get() == nullptr))
		{
			m_lastProjectionLayerStatus = L"pipeline-init-empty";
			m_sessionName = L"Session(unavailable): XR debug pipeline resources missing";
			return false;
		}

		return true;
	}

	bool NullXrSystem::InitializeFxaaPipeline()
	{
		if (m_d3d12Device == nullptr)
		{
			return false;
		}

		Common::FxaaPipelineObjects fxaaPipelineObjects{};
		if (!Common::BuildFxaaPipeline(
		        m_d3d12Device.Get(),
		        ResolveRtvFormat(m_colorSwapchainFormat),
		        (std::max)(1U, m_projectionSampleCount),
		        &fxaaPipelineObjects))
		{
			m_fxaaRootSignature.Reset();
			m_fxaaPipelineState.Reset();
			return false;
		}

		m_fxaaRootSignature = std::move(fxaaPipelineObjects.rootSignature);
		m_fxaaPipelineState = std::move(fxaaPipelineObjects.pipelineState);
		return true;
	}

	bool NullXrSystem::InitializeInputActions()
	{
		DestroyInputActions();

		if ((m_instanceHandle == XR_NULL_HANDLE) ||
		    (m_sessionHandle == XR_NULL_HANDLE))
		{
			return false;
		}

		auto logInputFailure = [&](const std::wstring_view phase, const XrResult result) -> void
		{
			const std::wstring message =
			    L"[OpenXR] " + std::wstring{ phase } + L" failed(" +
			    std::to_wstring(static_cast<long long>(result)) + L")\n";
			OutputDebugStringW(message.c_str());
		};

		if (XR_FAILED(xrStringToPath(
		        m_instanceHandle,
		        "/user/hand/left",
		        &m_leftHandPath)) ||
		    XR_FAILED(xrStringToPath(
		        m_instanceHandle,
		        "/user/hand/right",
		        &m_rightHandPath)))
		{
			OutputDebugStringW(L"[OpenXR] failed to resolve hand subaction paths.\n");
			DestroyInputActions();
			return false;
		}

		std::array<XrPath, 2> handPaths{
			m_leftHandPath,
			m_rightHandPath
		};

		XrActionSetCreateInfo actionSetCreateInfo{
			XR_TYPE_ACTION_SET_CREATE_INFO
		};
		strncpy_s(actionSetCreateInfo.actionSetName, "ui_panel", _TRUNCATE);
		strncpy_s(actionSetCreateInfo.localizedActionSetName, "UI Panel", _TRUNCATE);
		actionSetCreateInfo.priority = 0;
		const XrResult actionSetResult = xrCreateActionSet(
		    m_instanceHandle,
		    &actionSetCreateInfo,
		    &m_inputActionSet);
		if (XR_FAILED(actionSetResult))
		{
			logInputFailure(L"xrCreateActionSet", actionSetResult);
			DestroyInputActions();
			return false;
		}

		auto createAction = [&](const char* actionName,
		                        const char* localizedActionName,
		                        const XrActionType actionType,
		                        XrAction* const actionHandle) -> bool
		{
			if (actionHandle == nullptr)
			{
				return false;
			}

			XrActionCreateInfo actionCreateInfo{ XR_TYPE_ACTION_CREATE_INFO };
			actionCreateInfo.actionType = actionType;
			strncpy_s(actionCreateInfo.actionName, actionName, _TRUNCATE);
			strncpy_s(actionCreateInfo.localizedActionName, localizedActionName, _TRUNCATE);
			actionCreateInfo.countSubactionPaths =
			    static_cast<uint32_t>(handPaths.size());
			actionCreateInfo.subactionPaths = handPaths.data();
			const XrResult actionResult = xrCreateAction(
			    m_inputActionSet,
			    &actionCreateInfo,
			    actionHandle);
			if (XR_FAILED(actionResult))
			{
				logInputFailure(
				    L"xrCreateAction",
				    actionResult);
				return false;
			}
			return true;
		};

		if (!createAction(
		        "aim_pose",
		        "Aim Pose",
		        XR_ACTION_TYPE_POSE_INPUT,
		        &m_aimPoseAction) ||
		    !createAction(
		        "grip_pose",
		        "Grip Pose",
		        XR_ACTION_TYPE_POSE_INPUT,
		        &m_gripPoseAction) ||
		    !createAction(
		        "select_click",
		        "Select Click",
		        XR_ACTION_TYPE_BOOLEAN_INPUT,
		        &m_selectClickAction) ||
		    !createAction(
		        "trigger_value",
		        "Trigger Value",
		        XR_ACTION_TYPE_FLOAT_INPUT,
		        &m_triggerValueAction) ||
		    !createAction(
		        "move_stick",
		        "Move Stick",
		        XR_ACTION_TYPE_VECTOR2F_INPUT,
		        &m_moveStickAction) ||
		    !createAction(
		        "view_stick",
		        "View Stick",
		        XR_ACTION_TYPE_VECTOR2F_INPUT,
		        &m_viewStickAction) ||
		    !createAction(
		        "recenter",
		        "Recenter At Batting Position",
		        XR_ACTION_TYPE_BOOLEAN_INPUT,
		        &m_recenterAction) ||
		    !createAction(
		        "bat_haptic",
		        "Bat Contact Vibration",
		        XR_ACTION_TYPE_VIBRATION_OUTPUT,
		        &m_hapticAction))
		{
			DestroyInputActions();
			return false;
		}

		auto appendBinding = [&](std::vector<XrActionSuggestedBinding>* const bindings,
		                         const XrAction actionHandle,
		                         const char* pathText) -> void
		{
			if ((bindings == nullptr) ||
			    (actionHandle == XR_NULL_HANDLE) ||
			    (pathText == nullptr))
			{
				return;
			}

			XrPath inputPath = XR_NULL_PATH;
			const XrResult pathResult = xrStringToPath(
			    m_instanceHandle,
			    pathText,
			    &inputPath);
			if (XR_FAILED(pathResult))
			{
				logInputFailure(L"xrStringToPath(input)", pathResult);
				return;
			}

			bindings->push_back(XrActionSuggestedBinding{
			    actionHandle,
			    inputPath });
		};

		auto suggestBindings = [&](const char* interactionProfilePath,
		                           const std::vector<XrActionSuggestedBinding>& bindings) -> void
		{
			if ((interactionProfilePath == nullptr) || bindings.empty())
			{
				return;
			}

			XrPath interactionProfile = XR_NULL_PATH;
			const XrResult pathResult = xrStringToPath(
			    m_instanceHandle,
			    interactionProfilePath,
			    &interactionProfile);
			if (XR_FAILED(pathResult))
			{
				logInputFailure(L"xrStringToPath(profile)", pathResult);
				return;
			}

			XrInteractionProfileSuggestedBinding suggestedBinding{
				XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
			};
			suggestedBinding.interactionProfile = interactionProfile;
			suggestedBinding.suggestedBindings = bindings.data();
			suggestedBinding.countSuggestedBindings =
			    static_cast<uint32_t>(bindings.size());
			const XrResult suggestResult = xrSuggestInteractionProfileBindings(
			    m_instanceHandle,
			    &suggestedBinding);
			if (XR_FAILED(suggestResult))
			{
				logInputFailure(
				    L"xrSuggestInteractionProfileBindings",
				    suggestResult);
			}
		};

		std::vector<XrActionSuggestedBinding> simpleControllerBindings{};
		appendBinding(
		    &simpleControllerBindings,
		    m_aimPoseAction,
		    "/user/hand/left/input/aim/pose");
		appendBinding(
		    &simpleControllerBindings,
		    m_aimPoseAction,
		    "/user/hand/right/input/aim/pose");
		appendBinding(
		    &simpleControllerBindings,
		    m_gripPoseAction,
		    "/user/hand/left/input/grip/pose");
		appendBinding(
		    &simpleControllerBindings,
		    m_gripPoseAction,
		    "/user/hand/right/input/grip/pose");
		appendBinding(
		    &simpleControllerBindings,
		    m_selectClickAction,
		    "/user/hand/left/input/select/click");
		appendBinding(
		    &simpleControllerBindings,
		    m_selectClickAction,
		    "/user/hand/right/input/select/click");
		appendBinding(
		    &simpleControllerBindings,
		    m_hapticAction,
		    "/user/hand/left/output/haptic");
		appendBinding(
		    &simpleControllerBindings,
		    m_hapticAction,
		    "/user/hand/right/output/haptic");
		suggestBindings(
		    "/interaction_profiles/khr/simple_controller",
		    simpleControllerBindings);

		// Every path must exist in the profile: one unsupported path makes the
		// runtime reject the whole profile (XR_ERROR_PATH_UNSUPPORTED).
		// oculus/touch_controller and microsoft/motion_controller have no
		// trigger/click; select then comes from trigger/value (see querySelectPressed).
		auto buildTriggerProfileBindings = [&](std::vector<XrActionSuggestedBinding>* const bindings,
		                                       const char* const leftStickPath,
		                                       const char* const rightStickPath,
		                                       const bool hasTriggerClick,
		                                       const char* const recenterPath) -> void
		{
			appendBinding(
			    bindings,
			    m_aimPoseAction,
			    "/user/hand/left/input/aim/pose");
			appendBinding(
			    bindings,
			    m_aimPoseAction,
			    "/user/hand/right/input/aim/pose");
			appendBinding(
			    bindings,
			    m_gripPoseAction,
			    "/user/hand/left/input/grip/pose");
			appendBinding(
			    bindings,
			    m_gripPoseAction,
			    "/user/hand/right/input/grip/pose");
			appendBinding(
			    bindings,
			    m_triggerValueAction,
			    "/user/hand/left/input/trigger/value");
			appendBinding(
			    bindings,
			    m_triggerValueAction,
			    "/user/hand/right/input/trigger/value");
			if (hasTriggerClick)
			{
				appendBinding(
				    bindings,
				    m_selectClickAction,
				    "/user/hand/left/input/trigger/click");
				appendBinding(
				    bindings,
				    m_selectClickAction,
				    "/user/hand/right/input/trigger/click");
			}
			appendBinding(
			    bindings,
			    m_hapticAction,
			    "/user/hand/left/output/haptic");
			appendBinding(
			    bindings,
			    m_hapticAction,
			    "/user/hand/right/output/haptic");
			if (recenterPath != nullptr)
			{
				appendBinding(
				    bindings,
				    m_recenterAction,
				    recenterPath);
			}
			if (leftStickPath != nullptr)
			{
				appendBinding(
				    bindings,
				    m_viewStickAction,
				    leftStickPath);
			}
			if (rightStickPath != nullptr)
			{
				appendBinding(
				    bindings,
				    m_moveStickAction,
				    rightStickPath);
			}
		};

		std::vector<XrActionSuggestedBinding> oculusTouchBindings{};
		buildTriggerProfileBindings(
		    &oculusTouchBindings,
		    "/user/hand/left/input/thumbstick",
		    "/user/hand/right/input/thumbstick",
		    false,
		    "/user/hand/left/input/thumbstick/click");
		suggestBindings(
		    "/interaction_profiles/oculus/touch_controller",
		    oculusTouchBindings);

		std::vector<XrActionSuggestedBinding> viveControllerBindings{};
		buildTriggerProfileBindings(
		    &viveControllerBindings,
		    "/user/hand/left/input/trackpad",
		    "/user/hand/right/input/trackpad",
		    true,
		    "/user/hand/left/input/menu/click");
		suggestBindings(
		    "/interaction_profiles/htc/vive_controller",
		    viveControllerBindings);

		std::vector<XrActionSuggestedBinding> valveIndexBindings{};
		buildTriggerProfileBindings(
		    &valveIndexBindings,
		    "/user/hand/left/input/thumbstick",
		    "/user/hand/right/input/thumbstick",
		    true,
		    "/user/hand/left/input/thumbstick/click");
		suggestBindings(
		    "/interaction_profiles/valve/index_controller",
		    valveIndexBindings);

		std::vector<XrActionSuggestedBinding> microsoftMotionBindings{};
		buildTriggerProfileBindings(
		    &microsoftMotionBindings,
		    "/user/hand/left/input/thumbstick",
		    "/user/hand/right/input/thumbstick",
		    false,
		    "/user/hand/left/input/thumbstick/click");
		suggestBindings(
		    "/interaction_profiles/microsoft/motion_controller",
		    microsoftMotionBindings);

		XrSessionActionSetsAttachInfo attachInfo{
			XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
		};
		attachInfo.countActionSets = 1;
		attachInfo.actionSets = &m_inputActionSet;
		const XrResult attachResult = xrAttachSessionActionSets(
		    m_sessionHandle,
		    &attachInfo);
		if (XR_FAILED(attachResult))
		{
			logInputFailure(L"xrAttachSessionActionSets", attachResult);
			DestroyInputActions();
			return false;
		}

		auto createPoseSpace = [&](const XrAction poseAction,
		                           const XrPath handPath,
		                           XrSpace* const actionSpace) -> bool
		{
			if (actionSpace == nullptr)
			{
				return false;
			}

			XrActionSpaceCreateInfo actionSpaceCreateInfo{
				XR_TYPE_ACTION_SPACE_CREATE_INFO
			};
			actionSpaceCreateInfo.action = poseAction;
			actionSpaceCreateInfo.subactionPath = handPath;
			actionSpaceCreateInfo.poseInActionSpace.orientation.w = 1.0F;
			const XrResult actionSpaceResult = xrCreateActionSpace(
			    m_sessionHandle,
			    &actionSpaceCreateInfo,
			    actionSpace);
			if (XR_FAILED(actionSpaceResult))
			{
				logInputFailure(L"xrCreateActionSpace", actionSpaceResult);
				return false;
			}
			return true;
		};

		if (!createPoseSpace(
		        m_aimPoseAction,
		        m_leftHandPath,
		        &m_leftAimActionSpace) ||
		    !createPoseSpace(
		        m_aimPoseAction,
		        m_rightHandPath,
		        &m_rightAimActionSpace) ||
		    !createPoseSpace(
		        m_gripPoseAction,
		        m_leftHandPath,
		        &m_leftGripActionSpace) ||
		    !createPoseSpace(
		        m_gripPoseAction,
		        m_rightHandPath,
		        &m_rightGripActionSpace))
		{
			DestroyInputActions();
			return false;
		}

		m_wasResultPanelSelectPressed = false;
		m_hasResultPanelLaser = false;
		m_locomotionPositionOffset = Physics::Vector3{};
		m_viewYawOffsetRadians = 0.0;
		m_appliedReferenceSpacePositionOffset = Physics::Vector3{};
		m_appliedReferenceSpaceYawOffsetRadians = 0.0;
		m_lastLocomotionSampleTime = 0;
		m_showPitchControlPanel = false;
		m_showMetricsPanel = false;
		m_showBattingMetricsPanel = false;
		m_pitchPanelMinimized = false;
		m_metricsPanelMinimized = false;
		m_battingMetricsPanelMinimized = false;
		m_isPitchPlaybackSliderDragging = false;
		m_subPanelDragTarget = SubPanelDragTarget::None;
		m_subPanelDragGrabLocalX = 0.0;
		m_subPanelDragGrabLocalY = 0.0;
		m_subPanelDragCenterDistanceMeters = 0.90;
		m_subPanelDragGrabDistanceMeters = 0.90;
		m_pitchPanelArcYawRadians = DefaultPitchPanelArcYawRadians;
		m_pitchPanelArcRadiusMeters = DefaultPitchPanelArcRadiusMeters;
		m_pitchPanelHeightOffsetMeters = DefaultPitchPanelHeightOffsetMeters;
		m_metricsPanelArcYawRadians = DefaultMetricsPanelArcYawRadians;
		m_metricsPanelArcRadiusMeters = DefaultMetricsPanelArcRadiusMeters;
		m_metricsPanelHeightOffsetMeters = DefaultMetricsPanelHeightOffsetMeters;
		m_battingMetricsPanelArcYawRadians = DefaultBattingMetricsPanelArcYawRadians;
		m_battingMetricsPanelArcRadiusMeters = DefaultBattingMetricsPanelArcRadiusMeters;
		m_battingMetricsPanelHeightOffsetMeters = DefaultBattingMetricsPanelHeightOffsetMeters;
		m_mainControlPanelCenter = Physics::Vector3{
			Common::DebugWorldLayout::MainControlPanelCenterXMeters,
			Common::DebugWorldLayout::MainControlPanelCenterYMeters,
			-Common::DebugWorldLayout::MainControlPanelDepthMeters
		};
		m_pitchControlPanelCenter = m_mainControlPanelCenter +
		                            Physics::Vector3{ 0.0, -0.30, 0.0 };
		m_metricsPanelCenter = m_mainControlPanelCenter +
		                       Physics::Vector3{ 0.26, 0.0, 0.0 };
		m_battingMetricsPanelCenter = m_mainControlPanelCenter +
		                              Physics::Vector3{ 0.42, -0.12, 0.0 };
		m_uiPanelForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
		m_nonMainPanelForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
		m_pitchPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_metricsPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_battingMetricsPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_uiPanelRight = Physics::Vector3{ 1.0, 0.0, 0.0 };
		m_uiPanelUp = Physics::Vector3{ 0.0, 1.0, 0.0 };
		m_resultPanelLaserStatus = L"none";
		return true;
	}

	void NullXrSystem::DestroyInputActions()
	{
		if (m_leftAimActionSpace != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_leftAimActionSpace));
			m_leftAimActionSpace = XR_NULL_HANDLE;
		}
		if (m_rightAimActionSpace != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_rightAimActionSpace));
			m_rightAimActionSpace = XR_NULL_HANDLE;
		}
		if (m_leftGripActionSpace != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_leftGripActionSpace));
			m_leftGripActionSpace = XR_NULL_HANDLE;
		}
		if (m_rightGripActionSpace != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_rightGripActionSpace));
			m_rightGripActionSpace = XR_NULL_HANDLE;
		}
		if (m_aimPoseAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_aimPoseAction));
			m_aimPoseAction = XR_NULL_HANDLE;
		}
		if (m_gripPoseAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_gripPoseAction));
			m_gripPoseAction = XR_NULL_HANDLE;
		}
		if (m_selectClickAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_selectClickAction));
			m_selectClickAction = XR_NULL_HANDLE;
		}
		if (m_triggerValueAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_triggerValueAction));
			m_triggerValueAction = XR_NULL_HANDLE;
		}
		if (m_moveStickAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_moveStickAction));
			m_moveStickAction = XR_NULL_HANDLE;
		}
		if (m_recenterAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_recenterAction));
			m_recenterAction = XR_NULL_HANDLE;
		}
		m_wasRecenterPressed = false;
		if (m_hapticAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_hapticAction));
			m_hapticAction = XR_NULL_HANDLE;
		}
		if (m_viewStickAction != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyAction",
			    xrDestroyAction(m_viewStickAction));
			m_viewStickAction = XR_NULL_HANDLE;
		}
		if (m_inputActionSet != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyActionSet",
			    xrDestroyActionSet(m_inputActionSet));
			m_inputActionSet = XR_NULL_HANDLE;
		}

		m_leftHandPath = XR_NULL_PATH;
		m_rightHandPath = XR_NULL_PATH;
		m_locomotionPositionOffset = Physics::Vector3{};
		m_viewYawOffsetRadians = 0.0;
		m_appliedReferenceSpacePositionOffset = Physics::Vector3{};
		m_appliedReferenceSpaceYawOffsetRadians = 0.0;
		m_lastLocomotionSampleTime = 0;
		m_wasResultPanelSelectPressed = false;
		m_hasResultPanelLaser = false;
		m_showMainControlPanel = false;
		m_showPitchControlPanel = false;
		m_showMetricsPanel = false;
		m_showBattingMetricsPanel = false;
		m_pitchPanelMinimized = false;
		m_metricsPanelMinimized = false;
		m_battingMetricsPanelMinimized = false;
		m_isPitchPlaybackSliderDragging = false;
		m_subPanelDragTarget = SubPanelDragTarget::None;
		m_subPanelDragGrabLocalX = 0.0;
		m_subPanelDragGrabLocalY = 0.0;
		m_subPanelDragCenterDistanceMeters = 0.90;
		m_subPanelDragGrabDistanceMeters = 0.90;
		m_pitchPanelArcYawRadians = DefaultPitchPanelArcYawRadians;
		m_pitchPanelArcRadiusMeters = DefaultPitchPanelArcRadiusMeters;
		m_pitchPanelHeightOffsetMeters = DefaultPitchPanelHeightOffsetMeters;
		m_metricsPanelArcYawRadians = DefaultMetricsPanelArcYawRadians;
		m_metricsPanelArcRadiusMeters = DefaultMetricsPanelArcRadiusMeters;
		m_metricsPanelHeightOffsetMeters = DefaultMetricsPanelHeightOffsetMeters;
		m_battingMetricsPanelArcYawRadians = DefaultBattingMetricsPanelArcYawRadians;
		m_battingMetricsPanelArcRadiusMeters = DefaultBattingMetricsPanelArcRadiusMeters;
		m_battingMetricsPanelHeightOffsetMeters = DefaultBattingMetricsPanelHeightOffsetMeters;
		m_mainControlPanelCenter = Physics::Vector3{
			Common::DebugWorldLayout::MainControlPanelCenterXMeters,
			Common::DebugWorldLayout::MainControlPanelCenterYMeters,
			-Common::DebugWorldLayout::MainControlPanelDepthMeters
		};
		m_pitchControlPanelCenter = m_mainControlPanelCenter +
		                            Physics::Vector3{ 0.0, -0.30, 0.0 };
		m_metricsPanelCenter = m_mainControlPanelCenter +
		                       Physics::Vector3{ 0.26, 0.0, 0.0 };
		m_battingMetricsPanelCenter = m_mainControlPanelCenter +
		                              Physics::Vector3{ 0.42, -0.12, 0.0 };
		m_uiPanelForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
		m_nonMainPanelForward = Physics::Vector3{ 0.0, 0.0, -1.0 };
		m_pitchPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_metricsPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_battingMetricsPanelForward = Physics::Vector3{ 0.0, 0.0, 1.0 };
		m_uiPanelRight = Physics::Vector3{ 1.0, 0.0, 0.0 };
		m_uiPanelUp = Physics::Vector3{ 0.0, 1.0, 0.0 };
		m_resultPanelLaserOrigin = Physics::Vector3{};
		m_resultPanelLaserTarget = Physics::Vector3{};
		m_resultPanelLaserStatus = L"none";
		m_leftHandPose = HandPoseState{};
		m_rightHandPose = HandPoseState{};
		m_batOverlayState.reset();
		m_pendingUiCommands.clear();
	}

	void NullXrSystem::RecenterToBattingPosition()
	{
		if (m_views.empty())
		{
			return;
		}
		const std::size_t viewCount = (std::min)(m_views.size(), static_cast<std::size_t>(2));
		Physics::Vector3 head{};
		for (std::size_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
		{
			head += Physics::Vector3{
				static_cast<double>(m_views[viewIndex].pose.position.x),
				static_cast<double>(m_views[viewIndex].pose.position.y),
				static_cast<double>(m_views[viewIndex].pose.position.z)
			};
		}
		head = head / static_cast<double>(viewCount);
		const Physics::Vector3 forward = RotateVectorByQuaternion(
		    Physics::Vector3{ 0.0, 0.0, -1.0 },
		    NormalizeQuaternion(m_views.front().pose.orientation));
		const Physics::Vector3 horizontalForward =
		    Physics::NormalizeOrZero(Physics::Vector3{ forward.x, 0.0, forward.z });
		if (Physics::Length(horizontalForward) < 0.5)
		{
			return;
		}
		// World = R(yaw) * tracking + offset. Rotating the world by the head
		// heading and removing the head's horizontal position puts the player at
		// the batting origin looking down the pitching line (-z).
		const double heading = std::atan2(horizontalForward.x, -horizontalForward.z);
		const double c = std::cos(heading);
		const double sn = std::sin(heading);
		const Physics::Vector3 shifted = m_locomotionPositionOffset - Physics::Vector3{ head.x, 0.0, head.z };
		m_locomotionPositionOffset = Physics::Vector3{
			(shifted.x * c) + (shifted.z * sn),
			shifted.y,
			(-shifted.x * sn) + (shifted.z * c)
		};
		m_viewYawOffsetRadians = std::remainder(m_viewYawOffsetRadians + heading, 2.0 * std::numbers::pi);
		++m_handReferenceRevision;
		OutputDebugStringW(L"[OpenXR] recentered at the batting position.\n");
	}

	void NullXrSystem::UpdateUiPanelInteraction(
	    const XrTime predictedDisplayTime)
	{
		m_hasResultPanelLaser = false;
		m_showMainControlPanel = false;
		m_resultPanelLaserOrigin = Physics::Vector3{};
		m_resultPanelLaserTarget = Physics::Vector3{};
		m_resultPanelLaserStatus = L"none";
		m_leftHandPose = HandPoseState{};
		m_rightHandPose = HandPoseState{};

		if ((m_sessionHandle == XR_NULL_HANDLE) ||
		    (m_referenceSpaceHandle == XR_NULL_HANDLE) ||
		    (m_inputActionSet == XR_NULL_HANDLE) ||
		    (m_aimPoseAction == XR_NULL_HANDLE))
		{
			m_wasResultPanelSelectPressed = false;
			return;
		}

		XrActiveActionSet activeActionSet{};
		activeActionSet.actionSet = m_inputActionSet;
		activeActionSet.subactionPath = XR_NULL_PATH;
		XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &activeActionSet;
		const XrResult syncResult = xrSyncActions(m_sessionHandle, &syncInfo);
		if (XR_FAILED(syncResult))
		{
			m_wasResultPanelSelectPressed = false;
			return;
		}

		auto queryStickValue = [&](const XrAction actionHandle,
		                           const XrPath handPath,
		                           XrVector2f* const outStickValue) -> bool
		{
			if (outStickValue == nullptr)
			{
				return false;
			}
			outStickValue->x = 0.0F;
			outStickValue->y = 0.0F;
			if (actionHandle == XR_NULL_HANDLE)
			{
				return false;
			}

			XrActionStateGetInfo stateInfo{
				XR_TYPE_ACTION_STATE_GET_INFO
			};
			stateInfo.action = actionHandle;
			stateInfo.subactionPath = handPath;
			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
			const XrResult stateResult = xrGetActionStateVector2f(
			    m_sessionHandle,
			    &stateInfo,
			    &state);
			if (XR_FAILED(stateResult) || (state.isActive != XR_TRUE))
			{
				return false;
			}

			*outStickValue = ApplyStickDeadzone(state.currentState);
			return true;
		};

		double frameDeltaSeconds = 0.0;
		if ((predictedDisplayTime > 0) && (m_lastLocomotionSampleTime > 0) &&
		    (predictedDisplayTime > m_lastLocomotionSampleTime))
		{
			frameDeltaSeconds = static_cast<double>(
			                        predictedDisplayTime - m_lastLocomotionSampleTime) /
			                    1'000'000'000.0;
			frameDeltaSeconds = std::clamp(
			    frameDeltaSeconds,
			    0.0,
			    MaxLocomotionDeltaSeconds);
		}
		m_lastLocomotionSampleTime = predictedDisplayTime;

		XrVector2f moveStickValue{ 0.0F, 0.0F };
		XrVector2f viewStickValue{ 0.0F, 0.0F };
		(void)queryStickValue(
		    m_moveStickAction,
		    m_rightHandPath,
		    &moveStickValue);
		(void)queryStickValue(
		    m_viewStickAction,
		    m_leftHandPath,
		    &viewStickValue);

		if (frameDeltaSeconds > 1e-6)
		{
			m_viewYawOffsetRadians -= static_cast<double>(viewStickValue.x) *
			                          ViewYawSpeedRadiansPerSecond *
			                          frameDeltaSeconds;

			double movementYawRadians = m_viewYawOffsetRadians;
			if (m_leftEyeViewState.isValid)
			{
				const XrQuaternionf leftEyeOrientation = NormalizeQuaternion(XrQuaternionf{
				    static_cast<float>(m_leftEyeViewState.orientation.x),
				    static_cast<float>(m_leftEyeViewState.orientation.y),
				    static_cast<float>(m_leftEyeViewState.orientation.z),
				    static_cast<float>(m_leftEyeViewState.orientationW) });
				const Physics::Vector3 leftEyeForward = RotateVectorByQuaternion(
				    Physics::Vector3{ 0.0, 0.0, -1.0 },
				    leftEyeOrientation);
				Physics::Vector3 horizontalForward{
					leftEyeForward.x,
					0.0,
					leftEyeForward.z
				};
				horizontalForward = Physics::NormalizeOrZero(horizontalForward);
				if (Physics::Length(horizontalForward) > 1e-6)
				{
					movementYawRadians = std::atan2(
					    horizontalForward.x,
					    -horizontalForward.z);
				}
			}

			const double forwardX = std::sin(movementYawRadians);
			const double forwardZ = -std::cos(movementYawRadians);
			const double rightX = std::cos(movementYawRadians);
			const double rightZ = std::sin(movementYawRadians);
			const Physics::Vector3 movementDirection{
				(forwardX * static_cast<double>(moveStickValue.y)) +
				    (rightX * static_cast<double>(moveStickValue.x)),
				0.0,
				(forwardZ * static_cast<double>(moveStickValue.y)) +
				    (rightZ * static_cast<double>(moveStickValue.x))
			};
			m_locomotionPositionOffset +=
			    movementDirection * (MoveSpeedMetersPerSecond * frameDeltaSeconds);
		}

		if (m_recenterAction != XR_NULL_HANDLE)
		{
			XrActionStateGetInfo recenterInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
			recenterInfo.action = m_recenterAction;
			recenterInfo.subactionPath = m_leftHandPath;
			XrActionStateBoolean recenterState{ XR_TYPE_ACTION_STATE_BOOLEAN };
			const bool recenterPressed =
			    XR_SUCCEEDED(xrGetActionStateBoolean(m_sessionHandle, &recenterInfo, &recenterState)) &&
			    (recenterState.isActive == XR_TRUE) && (recenterState.currentState == XR_TRUE);
			if (recenterPressed && !m_wasRecenterPressed)
			{
				RecenterToBattingPosition();
			}
			m_wasRecenterPressed = recenterPressed;
		}

		auto querySelectPressed = [&](const XrPath handPath, bool* const isPressed) -> bool
		{
			if (isPressed == nullptr)
			{
				return false;
			}

			bool pressed = false;
			bool hasActiveInput = false;
			if (m_selectClickAction != XR_NULL_HANDLE)
			{
				XrActionStateGetInfo stateInfo{
					XR_TYPE_ACTION_STATE_GET_INFO
				};
				stateInfo.action = m_selectClickAction;
				stateInfo.subactionPath = handPath;
				XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
				const XrResult stateResult = xrGetActionStateBoolean(
				    m_sessionHandle,
				    &stateInfo,
				    &state);
				if (XR_SUCCEEDED(stateResult) && (state.isActive == XR_TRUE))
				{
					hasActiveInput = true;
					pressed = pressed || (state.currentState == XR_TRUE);
				}
			}

			if (m_triggerValueAction != XR_NULL_HANDLE)
			{
				XrActionStateGetInfo stateInfo{
					XR_TYPE_ACTION_STATE_GET_INFO
				};
				stateInfo.action = m_triggerValueAction;
				stateInfo.subactionPath = handPath;
				XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
				const XrResult stateResult = xrGetActionStateFloat(
				    m_sessionHandle,
				    &stateInfo,
				    &state);
				if (XR_SUCCEEDED(stateResult) && (state.isActive == XR_TRUE))
				{
					hasActiveInput = true;
					pressed = pressed || (state.currentState >= 0.75F);
				}
			}

			*isPressed = pressed;
			return hasActiveInput;
		};

		struct HandRayState final
		{
			bool isValid{ false };
			bool isPressed{ false };
			Physics::Vector3 origin{};
			Physics::Vector3 direction{};
		};

		auto queryPose = [&](const XrAction poseAction,
		                     const XrPath handPath,
		                     const XrSpace actionSpace,
		                     XrPosef* const outPose,
		                     HandPoseState* const outMotion = nullptr) -> bool
		{
			if ((outPose == nullptr) ||
			    (poseAction == XR_NULL_HANDLE) ||
			    (actionSpace == XR_NULL_HANDLE))
			{
				return false;
			}

			XrActionStateGetInfo poseStateInfo{
				XR_TYPE_ACTION_STATE_GET_INFO
			};
			poseStateInfo.action = poseAction;
			poseStateInfo.subactionPath = handPath;
			XrActionStatePose poseState{ XR_TYPE_ACTION_STATE_POSE };
			const XrResult poseStateResult = xrGetActionStatePose(
			    m_sessionHandle,
			    &poseStateInfo,
			    &poseState);
			if (XR_FAILED(poseStateResult) || (poseState.isActive != XR_TRUE))
			{
				return false;
			}

			XrSpaceVelocity spaceVelocity{ XR_TYPE_SPACE_VELOCITY };
			XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
			if (outMotion != nullptr)
			{
				spaceLocation.next = &spaceVelocity;
			}
			const XrResult locateResult = xrLocateSpace(
			    actionSpace,
			    m_referenceSpaceHandle,
			    predictedDisplayTime,
			    &spaceLocation);
			if (XR_FAILED(locateResult))
			{
				return false;
			}

			// Inferred (VALID but not TRACKED) poses are kept: fast swings briefly
			// leave inside-out camera coverage, and dropping them removed the bat
			// and broke the sweep exactly at the contact point.
			const XrSpaceLocationFlags requiredFlags =
			    XR_SPACE_LOCATION_POSITION_VALID_BIT |
			    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
			if ((spaceLocation.locationFlags & requiredFlags) != requiredFlags)
			{
				return false;
			}

			*outPose = spaceLocation.pose;
			if (outMotion != nullptr)
			{
				constexpr XrSpaceLocationFlags trackedFlags =
				    XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
				outMotion->isTracked = (spaceLocation.locationFlags & trackedFlags) == trackedFlags;
				outMotion->hasLinearVelocity =
				    (spaceVelocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
				outMotion->hasAngularVelocity =
				    (spaceVelocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
				outMotion->linearVelocity = Physics::Vector3{
					static_cast<double>(spaceVelocity.linearVelocity.x),
					static_cast<double>(spaceVelocity.linearVelocity.y),
					static_cast<double>(spaceVelocity.linearVelocity.z)
				};
				outMotion->angularVelocity = Physics::Vector3{
					static_cast<double>(spaceVelocity.angularVelocity.x),
					static_cast<double>(spaceVelocity.angularVelocity.y),
					static_cast<double>(spaceVelocity.angularVelocity.z)
				};
			}
			return true;
		};

		auto queryHandRay = [&](const XrPath handPath,
		                        const XrSpace aimActionSpace,
		                        const XrSpace gripActionSpace) -> HandRayState
		{
			HandRayState handState{};
			(void)querySelectPressed(handPath, &handState.isPressed);

			XrPosef aimPose{};
			const bool hasAimPose = queryPose(
			    m_aimPoseAction,
			    handPath,
			    aimActionSpace,
			    &aimPose);
			XrPosef gripPose{};
			const bool hasGripPose = queryPose(
			    m_gripPoseAction,
			    handPath,
			    gripActionSpace,
			    &gripPose);
			if (!hasAimPose && !hasGripPose)
			{
				return handState;
			}

			const XrPosef sourceOriginPose = hasGripPose ? gripPose : aimPose;
			const XrPosef sourceDirectionPose = hasAimPose ? aimPose : sourceOriginPose;
			const Physics::Vector3 handRayOrigin{
				static_cast<double>(sourceOriginPose.position.x),
				static_cast<double>(sourceOriginPose.position.y),
				static_cast<double>(sourceOriginPose.position.z)
			};
			const Physics::Vector3 forward = RotateVectorByQuaternion(
			    Physics::Vector3{ 0.0, 0.0, -1.0 },
			    sourceDirectionPose.orientation);
			const Physics::Vector3 normalizedForward =
			    Physics::NormalizeOrZero(forward);
			if (Physics::Length(normalizedForward) <= 1e-6)
			{
				return handState;
			}

			handState.isValid = true;
			handState.origin = handRayOrigin;
			handState.direction = normalizedForward;
			return handState;
		};

		auto queryHandPoseState = [&](const XrPath handPath,
		                              const XrSpace gripActionSpace) -> HandPoseState
		{
			HandPoseState handPoseState{};
			XrPosef gripPose{};
			HandPoseState motion{};
			const bool hasGripPose = queryPose(
			    m_gripPoseAction,
			    handPath,
			    gripActionSpace,
			    &gripPose,
			    &motion);
			if (!hasGripPose)
			{
				return handPoseState;
			}

			const XrPosef sourceOriginPose = gripPose;
			const XrPosef sourceDirectionPose = gripPose;
			const Physics::Vector3 forward = RotateVectorByQuaternion(
			    Physics::Vector3{ 0.0, 0.0, -1.0 },
			    sourceDirectionPose.orientation);
			const Physics::Vector3 normalizedForward =
			    Physics::NormalizeOrZero(forward);
			if (Physics::Length(normalizedForward) <= 1e-6)
			{
				return handPoseState;
			}

			Physics::Vector3 up = RotateVectorByQuaternion(
			    Physics::Vector3{ 0.0, 1.0, 0.0 },
			    sourceDirectionPose.orientation);
			up = Physics::NormalizeOrZero(up);
			if (Physics::Length(up) <= 1e-6)
			{
				return handPoseState;
			}

			handPoseState.isValid = true;
			handPoseState.sampleTimeSeconds = static_cast<double>(predictedDisplayTime) / 1'000'000'000.0;
			handPoseState.referenceSpaceRevision = m_handReferenceRevision;
			handPoseState.position = Physics::Vector3{
				static_cast<double>(sourceOriginPose.position.x),
				static_cast<double>(sourceOriginPose.position.y),
				static_cast<double>(sourceOriginPose.position.z)
			};
			handPoseState.forward = normalizedForward;
			handPoseState.up = up;
			handPoseState.isTracked = motion.isTracked;
			handPoseState.hasLinearVelocity = motion.hasLinearVelocity;
			handPoseState.linearVelocity = motion.linearVelocity;
			handPoseState.hasAngularVelocity = motion.hasAngularVelocity;
			handPoseState.angularVelocity = motion.angularVelocity;
			return handPoseState;
		};

		const HandRayState rightHandRayState = queryHandRay(
		    m_rightHandPath,
		    m_rightAimActionSpace,
		    m_rightGripActionSpace);
		const HandRayState leftHandRayState = queryHandRay(
		    m_leftHandPath,
		    m_leftAimActionSpace,
		    m_leftGripActionSpace);
		const HandPoseState rightHandPoseState = queryHandPoseState(
		    m_rightHandPath,
		    m_rightGripActionSpace);
		const HandPoseState leftHandPoseState = queryHandPoseState(
		    m_leftHandPath,
		    m_leftGripActionSpace);
		if (rightHandPoseState.isValid)
		{
			m_rightHandPose = rightHandPoseState;
		}
		if (leftHandPoseState.isValid)
		{
			m_leftHandPose = leftHandPoseState;
		}
		auto isHandRequestingMainPanel = [&](const HandRayState& handState) -> bool
		{
			if (!handState.isValid || !m_leftEyeViewState.isValid)
			{
				return false;
			}

			const Physics::Vector3 toHead =
			    m_leftEyeViewState.position - handState.origin;
			if (Physics::Length(toHead) <= 1e-6)
			{
				return false;
			}

			const Physics::Vector3 toHeadDirection =
			    Physics::NormalizeOrZero(toHead);
			const double facing = Physics::Dot(
			    handState.direction,
			    toHeadDirection);
			return (facing >= 0.82);
		};
		const bool isBatMode =
		    (m_uiTelemetry.operationMode == UiOperationMode::BattingDuel) ||
		    (m_uiTelemetry.operationMode == UiOperationMode::TeeBatting);
		const HandRayState* mainPanelHandState = isBatMode && m_uiTelemetry.batUseRightHand
		    ? &leftHandRayState : &rightHandRayState;
		m_showMainControlPanel = isHandRequestingMainPanel(*mainPanelHandState);
		const Physics::Vector3 panelArcForward =
		    ComputeHorizontalArcForwardFromHeadView(
		        m_leftEyeViewState,
		        m_nonMainPanelForward);
		m_nonMainPanelForward = panelArcForward;
		if (m_showMainControlPanel && mainPanelHandState->isValid)
		{
			Physics::Vector3 mainPanelForward =
			    Physics::NormalizeOrZero(mainPanelHandState->direction);
			if (Physics::Length(mainPanelForward) <= 1e-6)
			{
				mainPanelForward = panelArcForward;
			}
			BuildUiPanelAxesFromForward(
			    mainPanelForward,
			    &m_uiPanelRight,
			    &m_uiPanelUp,
			    &m_uiPanelForward);
			const Physics::Vector3 anchoredPanelCenter =
			    mainPanelHandState->origin +
			    (mainPanelForward * UiMainPanelAnchorDistanceMeters) +
			    (m_uiPanelUp * UiMainPanelAnchorDownOffsetMeters);
			const Physics::Vector3 safeAnchoredCenter = EnsureUiDistanceFromHead(
			    anchoredPanelCenter,
			    m_leftEyeViewState,
			    panelArcForward,
			    UiMainPanelMinDistanceFromHeadMeters);
			m_mainControlPanelCenter = ClampUiPanelCenter(safeAnchoredCenter);
		}
		Physics::Vector3 arcPanelRight{};
		Physics::Vector3 arcPanelUp{};
		Physics::Vector3 arcPanelForward{};
		BuildUiPanelAxesFromForward(
		    panelArcForward,
		    &arcPanelRight,
		    &arcPanelUp,
		    &arcPanelForward);
		if (!m_showMainControlPanel)
		{
			m_uiPanelRight = arcPanelRight;
			m_uiPanelUp = arcPanelUp;
			m_uiPanelForward = arcPanelForward;
		}
		if (m_leftEyeViewState.isValid)
		{
			m_pitchControlPanelCenter = ClampUiPanelCenter(
			    ComputeArcPanelCenterFromHead(
			        m_leftEyeViewState.position,
			        panelArcForward,
			        m_pitchPanelArcYawRadians,
			        m_pitchPanelArcRadiusMeters,
			        m_pitchPanelHeightOffsetMeters));
			m_metricsPanelCenter = ClampUiPanelCenter(
			    ComputeArcPanelCenterFromHead(
			        m_leftEyeViewState.position,
			        panelArcForward,
			        m_metricsPanelArcYawRadians,
			        m_metricsPanelArcRadiusMeters,
			        m_metricsPanelHeightOffsetMeters));
			m_battingMetricsPanelCenter = ClampUiPanelCenter(
			    ComputeArcPanelCenterFromHead(
			        m_leftEyeViewState.position,
			        panelArcForward,
			        m_battingMetricsPanelArcYawRadians,
			        m_battingMetricsPanelArcRadiusMeters,
			        m_battingMetricsPanelHeightOffsetMeters));
		}

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
		m_pitchPanelForward = pitchPanelFacingForward;
		m_metricsPanelForward = metricsPanelFacingForward;
		m_battingMetricsPanelForward = battingMetricsPanelFacingForward;

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

		HandRayState handRayState{};
		if (isBatMode)
		{
			handRayState = m_uiTelemetry.batUseRightHand ? rightHandRayState : leftHandRayState;
		}
		else if (m_showMainControlPanel && leftHandRayState.isValid)
		{
			handRayState = leftHandRayState;
		}
		else if (m_showMainControlPanel && mainPanelHandState->isValid)
		{
			handRayState = *mainPanelHandState;
		}
		else if (rightHandRayState.isValid)
		{
			handRayState = rightHandRayState;
		}
		else
		{
			handRayState = leftHandRayState;
		}
		const auto pointerInput = Common::AdvanceUiPointerInput(isBatMode, m_showMainControlPanel,
		    handRayState.isValid, handRayState.isPressed, m_wasResultPanelSelectPressed);
		if (!pointerInput.visible)
		{
			m_subPanelDragTarget = SubPanelDragTarget::None;
			m_isPitchPlaybackSliderDragging = false;
			return;
		}

		const double maxLaserDistanceMeters =
		    Common::DebugWorldLayout::UiLaserMaxDistanceMeters;
		m_hasResultPanelLaser = true;
		m_resultPanelLaserOrigin = handRayState.origin;
		m_resultPanelLaserTarget =
		    handRayState.origin +
		    (handRayState.direction * maxLaserDistanceMeters);
		m_resultPanelLaserStatus = L"air";

		int hoveredMainPanelCellIndex = -1;
		int hoveredPitchPanelCellIndex = -1;
		int hoveredMetricsPanelCellIndex = -1;
		int hoveredBattingMetricsPanelCellIndex = -1;
		bool hoveringPitchPlaybackSlider = false;
		int hoveredPitchPlaybackPercent = 100;
		int hoveredMetricsControlButtonIndex = -1;
		bool hasMainPanelIntersection = false;
		double mainPanelHitDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 mainPanelHitPoint{};
		bool hasPitchPanelIntersection = false;
		double pitchPanelHitDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 pitchPanelHitPoint{};
		bool hasMetricsPanelIntersection = false;
		double metricsPanelHitDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 metricsPanelHitPoint{};
		bool hasBattingMetricsPanelIntersection = false;
		double battingMetricsPanelHitDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 battingMetricsPanelHitPoint{};
		bool hasFloorIntersection = false;
		double floorHitDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 floorHitPoint{};

		if (std::abs(handRayState.direction.y) > 1e-6)
		{
			const double floorHeightMeters =
			    Common::DebugWorldStyle::FloorMeshHeightMeters;
			const double distanceToFloor =
			    (floorHeightMeters - handRayState.origin.y) /
			    handRayState.direction.y;
			if ((distanceToFloor > 0.0) &&
			    (distanceToFloor <= maxLaserDistanceMeters))
			{
				hasFloorIntersection = true;
				floorHitDistanceMeters = distanceToFloor;
				floorHitPoint = handRayState.origin +
				                (handRayState.direction *
				                 distanceToFloor);
			}
		}

		if (m_showMainControlPanel)
		{
			hasMainPanelIntersection = TryIntersectOrientedPanel(
			    mainPanelLayout,
			    handRayState.origin,
			    handRayState.direction,
			    maxLaserDistanceMeters,
			    &mainPanelHitDistanceMeters,
			    &mainPanelHitPoint);
		}
		if (m_showPitchControlPanel)
		{
			hasPitchPanelIntersection = TryIntersectOrientedPanel(
			    pitchPanelLayout,
			    handRayState.origin,
			    handRayState.direction,
			    maxLaserDistanceMeters,
			    &pitchPanelHitDistanceMeters,
			    &pitchPanelHitPoint);
		}
		if (m_showMetricsPanel)
		{
			hasMetricsPanelIntersection = TryIntersectOrientedPanel(
			    metricsPanelLayout,
			    handRayState.origin,
			    handRayState.direction,
			    maxLaserDistanceMeters,
			    &metricsPanelHitDistanceMeters,
			    &metricsPanelHitPoint);
		}
		if (m_showBattingMetricsPanel)
		{
			hasBattingMetricsPanelIntersection = TryIntersectOrientedPanel(
			    battingMetricsPanelLayout,
			    handRayState.origin,
			    handRayState.direction,
			    maxLaserDistanceMeters,
			    &battingMetricsPanelHitDistanceMeters,
			    &battingMetricsPanelHitPoint);
		}

		enum class HitPanelTarget
		{
			None,
			MainPanel,
			PitchPanel,
			MetricsPanel,
			BattingMetricsPanel
		};
		HitPanelTarget selectedPanelTarget = HitPanelTarget::None;
		double selectedPanelDistanceMeters = maxLaserDistanceMeters + 1.0;
		Physics::Vector3 selectedPanelHitPoint{};
		auto selectNearestPanel = [&](const bool hasIntersection,
		                              const double hitDistanceMeters,
		                              const Physics::Vector3& hitPoint,
		                              const HitPanelTarget panelTarget) -> void
		{
			if (!hasIntersection || (hitDistanceMeters >= selectedPanelDistanceMeters))
			{
				return;
			}
			selectedPanelTarget = panelTarget;
			selectedPanelDistanceMeters = hitDistanceMeters;
			selectedPanelHitPoint = hitPoint;
		};
		selectNearestPanel(
		    hasMainPanelIntersection,
		    mainPanelHitDistanceMeters,
		    mainPanelHitPoint,
		    HitPanelTarget::MainPanel);
		selectNearestPanel(
		    hasPitchPanelIntersection,
		    pitchPanelHitDistanceMeters,
		    pitchPanelHitPoint,
		    HitPanelTarget::PitchPanel);
		selectNearestPanel(
		    hasMetricsPanelIntersection,
		    metricsPanelHitDistanceMeters,
		    metricsPanelHitPoint,
		    HitPanelTarget::MetricsPanel);
		selectNearestPanel(
		    hasBattingMetricsPanelIntersection,
		    battingMetricsPanelHitDistanceMeters,
		    battingMetricsPanelHitPoint,
		    HitPanelTarget::BattingMetricsPanel);

		const bool usePanelHit =
		    (selectedPanelTarget != HitPanelTarget::None) &&
		    (!hasFloorIntersection ||
		     (selectedPanelDistanceMeters <= floorHitDistanceMeters));
		if (usePanelHit)
		{
			m_resultPanelLaserTarget = selectedPanelHitPoint;
			if (selectedPanelTarget == HitPanelTarget::MainPanel)
			{
				(void)TryResolveOrientedPanelCellIndex(
				    mainPanelLayout,
				    selectedPanelHitPoint,
				    &hoveredMainPanelCellIndex);
				if (hoveredMainPanelCellIndex >= 0)
				{
					std::wostringstream statusStream{};
					statusStream << L"main-cell-" << (hoveredMainPanelCellIndex + 1);
					m_resultPanelLaserStatus = statusStream.str();
				}
				else
				{
					m_resultPanelLaserStatus = L"main-panel";
				}
			}
			else if (selectedPanelTarget == HitPanelTarget::PitchPanel)
			{
				(void)TryResolveOrientedPanelCellIndex(
				    pitchPanelLayout,
				    selectedPanelHitPoint,
				    &hoveredPitchPanelCellIndex);
				if (hoveredPitchPanelCellIndex >= 0)
				{
					std::wostringstream statusStream{};
					statusStream << L"pitch-cell-" << (hoveredPitchPanelCellIndex + 1);
					m_resultPanelLaserStatus = statusStream.str();
				}
				else
				{
					m_resultPanelLaserStatus = L"pitch-panel";
				}
				hoveringPitchPlaybackSlider =
				    TryResolvePitchPlaybackSliderPercentFromHit(
				        pitchPanelLayout,
				        selectedPanelHitPoint,
				        PitchPlaybackSliderCellStartIndex,
				        PitchPlaybackSliderCellStartIndex +
				            PitchPlaybackSliderCellCount - 1,
				        &hoveredPitchPlaybackPercent);
				if (hoveringPitchPlaybackSlider)
				{
					m_resultPanelLaserStatus = L"pitch-slider";
				}
			}
			else if (selectedPanelTarget == HitPanelTarget::MetricsPanel)
			{
				(void)TryResolveOrientedPanelCellIndex(
				    metricsPanelLayout,
				    selectedPanelHitPoint,
				    &hoveredMetricsPanelCellIndex);
				if (hoveredMetricsPanelCellIndex >= 0)
				{
					std::wostringstream statusStream{};
					statusStream << L"metrics-cell-" << (hoveredMetricsPanelCellIndex + 1);
					m_resultPanelLaserStatus = statusStream.str();
				}
				else
				{
					m_resultPanelLaserStatus = L"metrics-panel";
				}
				if (TryResolveMetricsPanelControlButtonIndex(
				        metricsPanelLayout,
				        selectedPanelHitPoint,
				        &hoveredMetricsControlButtonIndex))
				{
					std::wostringstream statusStream{};
					statusStream << L"metrics-button-"
					             << (hoveredMetricsControlButtonIndex + 1);
					m_resultPanelLaserStatus = statusStream.str();
				}
			}
			else if (selectedPanelTarget == HitPanelTarget::BattingMetricsPanel)
			{
				(void)TryResolveOrientedPanelCellIndex(
				    battingMetricsPanelLayout,
				    selectedPanelHitPoint,
				    &hoveredBattingMetricsPanelCellIndex);
				if (hoveredBattingMetricsPanelCellIndex >= 0)
				{
					std::wostringstream statusStream{};
					statusStream << L"batting-metrics-cell-"
					             << (hoveredBattingMetricsPanelCellIndex + 1);
					m_resultPanelLaserStatus = statusStream.str();
				}
				else
				{
					m_resultPanelLaserStatus = L"batting-metrics-panel";
				}
			}
		}
		else if (hasFloorIntersection)
		{
			m_resultPanelLaserTarget = floorHitPoint;
			m_resultPanelLaserStatus = L"floor";
		}

		UiPanelTitleBarHitInfo pitchTitleBarHitInfo{};
		UiPanelTitleBarHitInfo metricsTitleBarHitInfo{};
		UiPanelTitleBarHitInfo battingMetricsTitleBarHitInfo{};
		if (selectedPanelTarget == HitPanelTarget::PitchPanel)
		{
			pitchTitleBarHitInfo = ResolveOrientedPanelTitleBarHitInfo(
			    pitchPanelLayout,
			    selectedPanelHitPoint);
			if (pitchTitleBarHitInfo.isInCloseButton)
			{
				m_resultPanelLaserStatus = L"pitch-close";
			}
			else if (pitchTitleBarHitInfo.isInMinimizeButton)
			{
				m_resultPanelLaserStatus = L"pitch-minimize";
			}
			else if (pitchTitleBarHitInfo.isInDragRegion)
			{
				m_resultPanelLaserStatus = L"pitch-titlebar";
			}
		}
		else if (selectedPanelTarget == HitPanelTarget::MetricsPanel)
		{
			metricsTitleBarHitInfo = ResolveOrientedPanelTitleBarHitInfo(
			    metricsPanelLayout,
			    selectedPanelHitPoint);
			if (metricsTitleBarHitInfo.isInCloseButton)
			{
				m_resultPanelLaserStatus = L"metrics-close";
			}
			else if (metricsTitleBarHitInfo.isInMinimizeButton)
			{
				m_resultPanelLaserStatus = L"metrics-minimize";
			}
			else if (metricsTitleBarHitInfo.isInDragRegion)
			{
				m_resultPanelLaserStatus = L"metrics-titlebar";
			}
		}
		else if (selectedPanelTarget == HitPanelTarget::BattingMetricsPanel)
		{
			battingMetricsTitleBarHitInfo = ResolveOrientedPanelTitleBarHitInfo(
			    battingMetricsPanelLayout,
			    selectedPanelHitPoint);
			if (battingMetricsTitleBarHitInfo.isInCloseButton)
			{
				m_resultPanelLaserStatus = L"batting-metrics-close";
			}
			else if (battingMetricsTitleBarHitInfo.isInMinimizeButton)
			{
				m_resultPanelLaserStatus = L"batting-metrics-minimize";
			}
			else if (battingMetricsTitleBarHitInfo.isInDragRegion)
			{
				m_resultPanelLaserStatus = L"batting-metrics-titlebar";
			}
		}

		const bool hoveringPitchTitleBarDragRegion =
		    (selectedPanelTarget == HitPanelTarget::PitchPanel) &&
		    pitchTitleBarHitInfo.isInDragRegion;
		const bool hoveringPitchTitleBarMinimizeButton =
		    (selectedPanelTarget == HitPanelTarget::PitchPanel) &&
		    pitchTitleBarHitInfo.isInMinimizeButton;
		const bool hoveringPitchTitleBarCloseButton =
		    (selectedPanelTarget == HitPanelTarget::PitchPanel) &&
		    pitchTitleBarHitInfo.isInCloseButton;
		const bool hoveringMetricsTitleBarDragRegion =
		    (selectedPanelTarget == HitPanelTarget::MetricsPanel) &&
		    metricsTitleBarHitInfo.isInDragRegion;
		const bool hoveringMetricsTitleBarMinimizeButton =
		    (selectedPanelTarget == HitPanelTarget::MetricsPanel) &&
		    metricsTitleBarHitInfo.isInMinimizeButton;
		const bool hoveringMetricsTitleBarCloseButton =
		    (selectedPanelTarget == HitPanelTarget::MetricsPanel) &&
		    metricsTitleBarHitInfo.isInCloseButton;
		const bool hoveringBattingMetricsTitleBarDragRegion =
		    (selectedPanelTarget == HitPanelTarget::BattingMetricsPanel) &&
		    battingMetricsTitleBarHitInfo.isInDragRegion;
		const bool hoveringBattingMetricsTitleBarMinimizeButton =
		    (selectedPanelTarget == HitPanelTarget::BattingMetricsPanel) &&
		    battingMetricsTitleBarHitInfo.isInMinimizeButton;
		const bool hoveringBattingMetricsTitleBarCloseButton =
		    (selectedPanelTarget == HitPanelTarget::BattingMetricsPanel) &&
		    battingMetricsTitleBarHitInfo.isInCloseButton;

		auto applySubPanelDrag = [&](const SubPanelDragTarget dragTarget) -> bool
		{
			if (!m_leftEyeViewState.isValid || !handRayState.isValid)
			{
				return false;
			}

			const OrientedUiPanelLayout* dragPanelLayout = nullptr;
			double* yawRadians = nullptr;
			double* radiusMeters = nullptr;
			double* heightOffsetMeters = nullptr;
			const wchar_t* dragStatusLabel = L"panel-drag";
			switch (dragTarget)
			{
				case SubPanelDragTarget::Pitch:
					dragPanelLayout = &pitchPanelLayout;
					yawRadians = &m_pitchPanelArcYawRadians;
					radiusMeters = &m_pitchPanelArcRadiusMeters;
					heightOffsetMeters = &m_pitchPanelHeightOffsetMeters;
					dragStatusLabel = L"pitch-drag";
					break;
				case SubPanelDragTarget::Metrics:
					dragPanelLayout = &metricsPanelLayout;
					yawRadians = &m_metricsPanelArcYawRadians;
					radiusMeters = &m_metricsPanelArcRadiusMeters;
					heightOffsetMeters = &m_metricsPanelHeightOffsetMeters;
					dragStatusLabel = L"metrics-drag";
					break;
				case SubPanelDragTarget::BattingMetrics:
					dragPanelLayout = &battingMetricsPanelLayout;
					yawRadians = &m_battingMetricsPanelArcYawRadians;
					radiusMeters = &m_battingMetricsPanelArcRadiusMeters;
					heightOffsetMeters = &m_battingMetricsPanelHeightOffsetMeters;
					dragStatusLabel = L"batting-metrics-drag";
					break;
				case SubPanelDragTarget::None:
				default:
					return false;
			}

			if ((dragPanelLayout == nullptr) || (yawRadians == nullptr) ||
			    (radiusMeters == nullptr) || (heightOffsetMeters == nullptr))
			{
				return false;
			}

			const Physics::Vector3 headPosition = m_leftEyeViewState.position;
			const Physics::Vector3 rayDirection =
			    Physics::NormalizeOrZero(handRayState.direction);
			if (Physics::Length(rayDirection) <= 1e-6)
			{
				return false;
			}

			auto tryResolveRaySpherePoint = [&](const double sphereRadiusMeters,
			                                    Physics::Vector3* const outPoint) -> bool
			{
				if ((outPoint == nullptr) || (sphereRadiusMeters <= 1e-6))
				{
					return false;
				}
				const Physics::Vector3 rayOriginToHead =
				    handRayState.origin - headPosition;
				const double b = Physics::Dot(rayOriginToHead, rayDirection);
				const double c = Physics::Dot(rayOriginToHead, rayOriginToHead) -
				                 (sphereRadiusMeters * sphereRadiusMeters);
				const double discriminant = (b * b) - c;
				if (discriminant < 0.0)
				{
					return false;
				}

				const double root = std::sqrt(discriminant);
				double rayDistance = -b - root;
				if (rayDistance <= 0.0)
				{
					rayDistance = -b + root;
				}
				if (rayDistance <= 0.0)
				{
					return false;
				}

				*outPoint = handRayState.origin + (rayDirection * rayDistance);
				return true;
			};

			const double clampedGrabDistance = std::clamp(
			    m_subPanelDragGrabDistanceMeters,
			    0.35,
			    1.90);
			Physics::Vector3 dragGrabPoint{};
			if (!tryResolveRaySpherePoint(clampedGrabDistance, &dragGrabPoint))
			{
				const Physics::Vector3 rayOriginToHead =
				    handRayState.origin - headPosition;
				const double nearestRayDistance =
				    (std::max)(0.0, -Physics::Dot(rayOriginToHead, rayDirection));
				const Physics::Vector3 nearestRayPoint =
				    handRayState.origin + (rayDirection * nearestRayDistance);
				Physics::Vector3 radialDirection = Physics::NormalizeOrZero(
				    nearestRayPoint - headPosition);
				if (Physics::Length(radialDirection) <= 1e-6)
				{
					radialDirection = ResolveHorizontalForward(
					    panelArcForward,
					    m_nonMainPanelForward);
				}
				if (Physics::Length(radialDirection) <= 1e-6)
				{
					radialDirection = Physics::Vector3{ 0.0, 0.0, -1.0 };
				}
				dragGrabPoint =
				    headPosition + (radialDirection * clampedGrabDistance);
			}

			Physics::Vector3 targetPanelCenter =
			    dragGrabPoint -
			    (dragPanelLayout->rightAxis * m_subPanelDragGrabLocalX) -
			    (dragPanelLayout->upAxis * m_subPanelDragGrabLocalY);
			Physics::Vector3 centerDirection = Physics::NormalizeOrZero(
			    targetPanelCenter - headPosition);
			if (Physics::Length(centerDirection) <= 1e-6)
			{
				centerDirection = Physics::NormalizeOrZero(
				    dragPanelLayout->center - headPosition);
			}
			if (Physics::Length(centerDirection) <= 1e-6)
			{
				centerDirection = ResolveHorizontalForward(
				    panelArcForward,
				    m_nonMainPanelForward);
			}
			if (Physics::Length(centerDirection) <= 1e-6)
			{
				centerDirection = Physics::Vector3{ 0.0, 0.0, -1.0 };
			}

			const double clampedCenterDistance = std::clamp(
			    m_subPanelDragCenterDistanceMeters,
			    0.40,
			    1.60);
			targetPanelCenter =
			    headPosition + (centerDirection * clampedCenterDistance);

			Physics::Vector3 horizontalForward = ResolveHorizontalForward(
			    panelArcForward,
			    m_nonMainPanelForward);
			Physics::Vector3 horizontalRight = Physics::NormalizeOrZero(
			    Physics::Cross(
			        Physics::Vector3{ 0.0, 1.0, 0.0 },
			        horizontalForward));
			if (Physics::Length(horizontalRight) <= 1e-6)
			{
				horizontalRight = Physics::Vector3{ 1.0, 0.0, 0.0 };
			}

			const Physics::Vector3 relative = targetPanelCenter - headPosition;
			const double forwardComponent =
			    Physics::Dot(relative, horizontalForward);
			const double rightComponent =
			    Physics::Dot(relative, horizontalRight);
			const double horizontalDistance = std::sqrt(
			    (forwardComponent * forwardComponent) +
			    (rightComponent * rightComponent));
			*yawRadians = std::clamp(
			    std::atan2(rightComponent, forwardComponent),
			    UiPanelArcMinYawRadians,
			    UiPanelArcMaxYawRadians);
			*radiusMeters = std::clamp(horizontalDistance, 0.40, 1.10);
			*heightOffsetMeters = std::clamp(relative.y, -0.70, 0.70);
			m_resultPanelLaserStatus = dragStatusLabel;
			return true;
		};

		auto queueCommand = [&](const UiCommandType commandType,
		                        const int intValue = 0) -> void
		{
			UiCommand command{};
			command.type = commandType;
			command.intValue = intValue;
			m_pendingUiCommands.push_back(command);
		};

		const bool isPressed = pointerInput.pressed;
		const bool isRisingEdge = pointerInput.risingEdge;
		const bool isFallingEdge = pointerInput.fallingEdge;
		if (isFallingEdge)
		{
			m_subPanelDragTarget = SubPanelDragTarget::None;
			m_isPitchPlaybackSliderDragging = false;
		}
		if (m_subPanelDragTarget != SubPanelDragTarget::None)
		{
			if (!isPressed)
			{
				m_subPanelDragTarget = SubPanelDragTarget::None;
				return;
			}
			if (applySubPanelDrag(m_subPanelDragTarget))
			{
				return;
			}
			m_subPanelDragTarget = SubPanelDragTarget::None;
		}
		if (m_isPitchPlaybackSliderDragging)
		{
			if (!isPressed || (selectedPanelTarget != HitPanelTarget::PitchPanel))
			{
				m_isPitchPlaybackSliderDragging = false;
			}
			else
			{
				if (hoveringPitchPlaybackSlider)
				{
					queueCommand(
					    UiCommandType::SetPlaybackSpeedPercent,
					    hoveredPitchPlaybackPercent);
					m_resultPanelLaserStatus = L"pitch-slider-drag";
				}
				return;
			}
		}
		if (isRisingEdge)
		{
			auto beginSubPanelDrag =
			    [&](const SubPanelDragTarget dragTarget,
			        const OrientedUiPanelLayout& panelLayout,
			        const Physics::Vector3& panelHitPoint) -> void
			{
				const Physics::Vector3 relative =
				    panelHitPoint - panelLayout.center;
				m_subPanelDragGrabLocalX =
				    Physics::Dot(relative, panelLayout.rightAxis);
				m_subPanelDragGrabLocalY =
				    Physics::Dot(relative, panelLayout.upAxis);
				if (m_leftEyeViewState.isValid)
				{
					m_subPanelDragCenterDistanceMeters = std::clamp(
					    Physics::Length(panelLayout.center - m_leftEyeViewState.position),
					    0.40,
					    1.60);
					m_subPanelDragGrabDistanceMeters = std::clamp(
					    Physics::Length(panelHitPoint - m_leftEyeViewState.position),
					    0.35,
					    1.90);
				}
				else
				{
					m_subPanelDragCenterDistanceMeters = 0.90;
					m_subPanelDragGrabDistanceMeters = 0.90;
				}
				m_subPanelDragTarget = dragTarget;
			};

			if (hoveringPitchTitleBarDragRegion)
			{
				beginSubPanelDrag(
				    SubPanelDragTarget::Pitch,
				    pitchPanelLayout,
				    selectedPanelHitPoint);
				(void)applySubPanelDrag(m_subPanelDragTarget);
				return;
			}
			if (hoveringMetricsTitleBarDragRegion)
			{
				beginSubPanelDrag(
				    SubPanelDragTarget::Metrics,
				    metricsPanelLayout,
				    selectedPanelHitPoint);
				(void)applySubPanelDrag(m_subPanelDragTarget);
				return;
			}
			if (hoveringBattingMetricsTitleBarDragRegion)
			{
				beginSubPanelDrag(
				    SubPanelDragTarget::BattingMetrics,
				    battingMetricsPanelLayout,
				    selectedPanelHitPoint);
				(void)applySubPanelDrag(m_subPanelDragTarget);
				return;
			}
			if ((selectedPanelTarget == HitPanelTarget::PitchPanel) &&
			    hoveringPitchPlaybackSlider &&
			    !hoveringPitchTitleBarMinimizeButton &&
			    !hoveringPitchTitleBarCloseButton)
			{
				m_isPitchPlaybackSliderDragging = true;
				queueCommand(
				    UiCommandType::SetPlaybackSpeedPercent,
				    hoveredPitchPlaybackPercent);
				m_resultPanelLaserStatus = L"pitch-slider-drag";
				return;
			}
		}
		if (!isRisingEdge)
		{
			return;
		}

		if (hoveredMainPanelCellIndex >= 0)
		{
			switch (hoveredMainPanelCellIndex)
			{
				case 0:
					queueCommand(UiCommandType::SetModeTrajectory);
					return;
				case 1:
					queueCommand(UiCommandType::SetModeBattingDuel);
					return;
				case 2:
					queueCommand(UiCommandType::SetModeTeeBatting);
					return;
				case 3:
					if (!m_showPitchControlPanel)
					{
						m_showPitchControlPanel = true;
						m_pitchPanelMinimized = false;
						m_resultPanelLaserStatus = L"pitch-open";
						return;
					}
					m_showPitchControlPanel = true;
					m_pitchPanelMinimized = false;
					m_pitchPanelArcYawRadians = DefaultPitchPanelArcYawRadians;
					m_pitchPanelArcRadiusMeters = DefaultPitchPanelArcRadiusMeters;
					m_pitchPanelHeightOffsetMeters = DefaultPitchPanelHeightOffsetMeters;
					m_resultPanelLaserStatus = L"pitch-rescue";
					return;
				case 4:
					if (!m_showMetricsPanel)
					{
						m_showMetricsPanel = true;
						m_metricsPanelMinimized = false;
						m_resultPanelLaserStatus = L"pitch-metrics-open";
						return;
					}
					m_showMetricsPanel = true;
					m_metricsPanelMinimized = false;
					m_metricsPanelArcYawRadians = DefaultMetricsPanelArcYawRadians;
					m_metricsPanelArcRadiusMeters = DefaultMetricsPanelArcRadiusMeters;
					m_metricsPanelHeightOffsetMeters = DefaultMetricsPanelHeightOffsetMeters;
					m_resultPanelLaserStatus = L"pitch-metrics-rescue";
					return;
				case 5:
					if (!m_showBattingMetricsPanel)
					{
						m_showBattingMetricsPanel = true;
						m_battingMetricsPanelMinimized = false;
						m_resultPanelLaserStatus = L"batting-metrics-open";
						return;
					}
					m_showBattingMetricsPanel = true;
					m_battingMetricsPanelMinimized = false;
					m_battingMetricsPanelArcYawRadians =
					    DefaultBattingMetricsPanelArcYawRadians;
					m_battingMetricsPanelArcRadiusMeters =
					    DefaultBattingMetricsPanelArcRadiusMeters;
					m_battingMetricsPanelHeightOffsetMeters =
					    DefaultBattingMetricsPanelHeightOffsetMeters;
					m_resultPanelLaserStatus = L"batting-metrics-rescue";
					return;
				case 6:
					queueCommand(UiCommandType::CyclePitchTrajectoryDisplayMode);
					return;
				case 7:
					queueCommand(UiCommandType::ToggleBattedTrajectoryVisibility);
					return;
				case Common::CalibrationMainButtonCellIndex:
					queueCommand(UiCommandType::RecalibrateStrikeZone);
					return;
				default:
					break;
			}
		}

		if ((hoveredPitchPanelCellIndex >= 0) || hoveringPitchTitleBarMinimizeButton ||
		    hoveringPitchTitleBarCloseButton)
		{
			if (m_pitchPanelMinimized)
			{
				if (hoveringPitchTitleBarMinimizeButton)
				{
					m_pitchPanelMinimized = false;
				}
				else if (hoveringPitchTitleBarCloseButton)
				{
					m_showPitchControlPanel = false;
				}
				return;
			}

			if (hoveringPitchTitleBarMinimizeButton)
			{
				m_pitchPanelMinimized = true;
				return;
			}
			if (hoveringPitchTitleBarCloseButton)
			{
				m_showPitchControlPanel = false;
				return;
			}
			if ((hoveredPitchPanelCellIndex < 0) && !hoveringPitchPlaybackSlider)
			{
				return;
			}

			if ((hoveredPitchPanelCellIndex >= PitchZoneCellStartIndex) &&
			    (hoveredPitchPanelCellIndex <
			     (PitchZoneCellStartIndex + PitchZoneCellCount)))
			{
				queueCommand(
				    UiCommandType::SetDuelTargetZone,
				    hoveredPitchPanelCellIndex - PitchZoneCellStartIndex);
				return;
			}

			if (hoveringPitchPlaybackSlider)
			{
				queueCommand(
				    UiCommandType::SetPlaybackSpeedPercent,
				    hoveredPitchPlaybackPercent);
				return;
			}

			switch (hoveredPitchPanelCellIndex)
			{
				case PitchTypePreviousCellIndex:
					queueCommand(UiCommandType::SelectPreviousPitchProfile);
					return;
				case PitchTypeNextCellIndex:
					queueCommand(UiCommandType::SelectNextPitchProfile);
					return;
				case PitchPlaybackToggleCellIndex:
					queueCommand(UiCommandType::TogglePlaybackPause);
					return;
				case PitchRandomPitchModeCellIndex:
					queueCommand(UiCommandType::ToggleRandomPitchProfile);
					return;
				case PitchZoneRandomCellIndex:
					queueCommand(UiCommandType::ClearDuelTargetZoneBias);
					return;
				case PitchSettingsPreviousCellIndex:
					queueCommand(UiCommandType::SelectPreviousPitchSettingsFile);
					return;
				case PitchSettingsNextCellIndex:
					queueCommand(UiCommandType::SelectNextPitchSettingsFile);
					return;
				default:
					break;
			}
		}

		if ((hoveredMetricsPanelCellIndex >= 0) ||
		    (hoveredMetricsControlButtonIndex >= 0) ||
		    hoveringMetricsTitleBarMinimizeButton ||
		    hoveringMetricsTitleBarCloseButton)
		{
			if (m_metricsPanelMinimized)
			{
				if (hoveringMetricsTitleBarMinimizeButton)
				{
					m_metricsPanelMinimized = false;
				}
				else if (hoveringMetricsTitleBarCloseButton)
				{
					m_showMetricsPanel = false;
				}
				return;
			}

			if (hoveringMetricsTitleBarMinimizeButton)
			{
				m_metricsPanelMinimized = true;
				return;
			}
			if (hoveringMetricsTitleBarCloseButton)
			{
				m_showMetricsPanel = false;
				return;
			}
			if ((hoveredMetricsPanelCellIndex < 0) &&
			    (hoveredMetricsControlButtonIndex < 0))
			{
				return;
			}

			switch (hoveredMetricsControlButtonIndex)
			{
				case 0:
					queueCommand(UiCommandType::CyclePitchTrajectoryDisplayMode);
					return;
				case 1:
					queueCommand(UiCommandType::CycleStrikeZoneDisplayMode);
					return;
				case 2:
					queueCommand(UiCommandType::ToggleMoundHeightOffset);
					return;
				case 3:
					queueCommand(UiCommandType::ToggleDuelPitchPreview);
					return;
				case 4:
					queueCommand(UiCommandType::ToggleBatHandedness);
					return;
				case 5:
					queueCommand(UiCommandType::AdjustBatAngleDown);
					return;
				case 6:
					queueCommand(UiCommandType::AdjustBatAngleUp);
					return;
				case 7:
					queueCommand(UiCommandType::AdjustBatYawDown);
					return;
				case 8:
					queueCommand(UiCommandType::AdjustBatYawUp);
					return;
				case 9:
					m_metricsPanelArcYawRadians = DefaultMetricsPanelArcYawRadians;
					m_metricsPanelArcRadiusMeters = DefaultMetricsPanelArcRadiusMeters;
					m_metricsPanelHeightOffsetMeters = DefaultMetricsPanelHeightOffsetMeters;
					return;
				case Common::CalibrationMetricsButtonIndex:
					queueCommand(UiCommandType::RecalibrateStrikeZone);
					return;
				case 11:
					queueCommand(UiCommandType::ToggleBattedTrajectoryVisibility);
					return;
				case BatMaterialControlButtonIndex:
					queueCommand(UiCommandType::CycleBatMaterial);
					return;
				case Common::MetricsZoneBottomDownIndex:
					queueCommand(UiCommandType::AdjustStrikeZoneBottomDown);
					return;
				case Common::MetricsZoneBottomUpIndex:
					queueCommand(UiCommandType::AdjustStrikeZoneBottomUp);
					return;
				case Common::MetricsZoneTopDownIndex:
					queueCommand(UiCommandType::AdjustStrikeZoneTopDown);
					return;
				case Common::MetricsZoneTopUpIndex:
					queueCommand(UiCommandType::AdjustStrikeZoneTopUp);
					return;
				default:
					break;
			}
		}

		if ((hoveredBattingMetricsPanelCellIndex >= 0) ||
		    hoveringBattingMetricsTitleBarMinimizeButton ||
		    hoveringBattingMetricsTitleBarCloseButton)
		{
			if (m_battingMetricsPanelMinimized)
			{
				if (hoveringBattingMetricsTitleBarMinimizeButton)
				{
					m_battingMetricsPanelMinimized = false;
				}
				else if (hoveringBattingMetricsTitleBarCloseButton)
				{
					m_showBattingMetricsPanel = false;
				}
				return;
			}

			if (hoveringBattingMetricsTitleBarMinimizeButton)
			{
				m_battingMetricsPanelMinimized = true;
				return;
			}
			if (hoveringBattingMetricsTitleBarCloseButton)
			{
				m_showBattingMetricsPanel = false;
				return;
			}
		}
	}

	void NullXrSystem::DestroyProjectionSwapchains()
	{
		// The runtime consumes the same queue. Keep all view uploads, command
		// allocators, textures and pipelines alive until queued work completes.
		if (m_xrFence && m_d3d12Queue && m_xrFenceEvent)
		{
			const UINT64 value = m_xrFenceValue++;
			const bool completed = SUCCEEDED(m_d3d12Queue->Signal(m_xrFence.Get(), value)) &&
			    SUCCEEDED(Common::D3d12RenderFrame::WaitForFence(m_xrFence.Get(), value, m_xrFenceEvent));
			Common::D3d12RenderFrame::EnsureSafeResourceRelease(m_d3d12Device.Get(), completed);
		}
		m_projectionRenderingFailed = false;
		if (m_xrFenceEvent != nullptr)
		{
			if (CloseHandle(m_xrFenceEvent) == FALSE)
			{
				LogWin32CleanupFailure(
				    L"CloseHandle(xrFenceEvent)",
				    GetLastError());
			}
			m_xrFenceEvent = nullptr;
		}

		m_xrFence.Reset();
		m_debugPipelineState.Reset();
		m_debugRootSignature.Reset();
		m_fxaaPipelineState.Reset();
		m_fxaaRootSignature.Reset();
		m_xrFenceValue = 1;
		m_rtvDescriptorSize = 0;
		m_dsvDescriptorSize = 0;
		m_srvDescriptorSize = 0;
		m_projectionSampleCount = 1U;
		m_isFxaaAvailable = false;
		m_xrPostProcessStatus = L"none";

		for (auto& swapchainContext : m_projectionSwapchains)
		{
			swapchainContext.images.clear();
			swapchainContext.sceneColorTargets.clear();
			swapchainContext.depthBuffers.clear();
			swapchainContext.rtvHeap.Reset();
			swapchainContext.sceneRtvHeap.Reset();
			swapchainContext.sceneSrvHeap.Reset();
			swapchainContext.dsvHeap.Reset();
			if (swapchainContext.swapchainHandle != XR_NULL_HANDLE)
			{
				LogXrCleanupFailure(
				    L"xrDestroySwapchain",
				    xrDestroySwapchain(swapchainContext.swapchainHandle));
				swapchainContext.swapchainHandle = XR_NULL_HANDLE;
			}
		}
		m_projectionSwapchains.clear();
		m_viewConfigurationViews.clear();
		m_views.clear();
		m_projectionLayerViews.clear();
	}

	void NullXrSystem::HandleSessionStateChanged(const XrSessionState sessionState)
	{
		m_sessionState = sessionState;

		if (m_sessionHandle == XR_NULL_HANDLE)
		{
			UpdateSessionName();
			return;
		}

		if ((sessionState == XR_SESSION_STATE_READY) && !m_isSessionRunning)
		{
			XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
			beginInfo.primaryViewConfigurationType = PrimaryViewConfigurationType;
			const XrResult beginResult = xrBeginSession(m_sessionHandle, &beginInfo);
			if (XR_FAILED(beginResult))
			{
				m_sessionName = L"Session(unavailable): " +
				                BuildXrErrorMessage(L"xrBeginSession", beginResult);
				m_isSessionRunning = false;
				return;
			}

			m_isSessionRunning = true;
			m_lastPredictedDisplayTime = 0;
			m_lastLocomotionSampleTime = 0;
			m_lastFrameSummary = L"frame(pending)";
			m_xrPostProcessStatus = m_isFxaaAvailable ? L"fxaa" : L"direct";
		}
		else if ((sessionState == XR_SESSION_STATE_STOPPING) && m_isSessionRunning)
		{
			const XrResult endResult = xrEndSession(m_sessionHandle);
			if (XR_FAILED(endResult))
			{
				m_sessionName = L"Session(unavailable): " +
				                BuildXrErrorMessage(L"xrEndSession", endResult);
				return;
			}

			m_isSessionRunning = false;
			m_lastPredictedDisplayTime = 0;
			m_lastLocomotionSampleTime = 0;
			m_lastFrameSummary = L"frame(stopped)";
			m_xrPostProcessStatus = L"none";
		}
		else if (sessionState == XR_SESSION_STATE_EXITING)
		{
			// The user or runtime ended the experience: it must not restart itself.
			DestroySession();
			m_exitRequested = true;
			m_sessionName = L"Session(exited)";
			return;
		}
		else if (sessionState == XR_SESSION_STATE_LOSS_PENDING)
		{
			// The device was lost (e.g. headset disconnected); recreate the session
			// once xrGetSystem succeeds again.
			ScheduleRecovery(L"session loss pending");
			return;
		}

		UpdateSessionName();
	}

	void NullXrSystem::HandleInteractionProfileChanged()
	{
		m_interactionProfileName.clear();
		if ((m_sessionHandle == XR_NULL_HANDLE) || (m_rightHandPath == XR_NULL_PATH))
		{
			return;
		}
		XrInteractionProfileState profileState{ XR_TYPE_INTERACTION_PROFILE_STATE };
		const XrPath handPath = m_rightHandPath;
		if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(m_sessionHandle, handPath, &profileState)) &&
		    (profileState.interactionProfile != XR_NULL_PATH))
		{
			std::array<char, XR_MAX_PATH_LENGTH> buffer{};
			uint32_t length = 0;
			if (XR_SUCCEEDED(xrPathToString(m_instanceHandle, profileState.interactionProfile,
			        static_cast<uint32_t>(buffer.size()), &length, buffer.data())))
			{
				m_interactionProfileName = ConvertUtf8ToWide(buffer.data());
			}
		}
		// A controller swap can move the grip origin; do not sweep across it.
		++m_handReferenceRevision;
		const std::wstring message = L"[OpenXR] interaction profile: " +
		    (m_interactionProfileName.empty() ? std::wstring{ L"(none)" } : m_interactionProfileName) + L"\n";
		OutputDebugStringW(message.c_str());
	}

	void NullXrSystem::ScheduleRecovery(const std::wstring_view reason)
	{
		const std::wstring message = L"[OpenXR] " + std::wstring{ reason } + L"; recreating the session.\n";
		OutputDebugStringW(message.c_str());
		DestroySession();
		m_recoveryPending = true;
		m_nextRecoveryAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		m_sessionName = L"Session(recovering): " + std::wstring{ reason };
	}

	void NullXrSystem::TryRecover()
	{
		if (!m_recoveryPending || (std::chrono::steady_clock::now() < m_nextRecoveryAttempt))
		{
			return;
		}
		m_nextRecoveryAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		TryInitializeOpenXrInstance();
		TryInitializeOpenXrSystem();
		if ((m_instanceHandle == XR_NULL_HANDLE) || (m_systemId == XR_NULL_SYSTEM_ID) || !m_hasGraphicsBinding)
		{
			// xrGetSystem reports FORM_FACTOR_UNAVAILABLE until the headset is back.
			return;
		}
		if (TryInitializeOpenXrSession(m_graphicsBinding))
		{
			m_recoveryPending = false;
			OutputDebugStringW(L"[OpenXR] session recreated.\n");
		}
		UpdateSessionName();
	}

	void NullXrSystem::TryBeginFrame()
	{
		m_frameTiming = {};
		m_hasLocatedViews = false;
		if (!m_isSessionRunning || (m_sessionHandle == XR_NULL_HANDLE) ||
		    (m_referenceSpaceHandle == XR_NULL_HANDLE) ||
		    m_projectionSwapchains.empty() || (m_d3d12Queue == nullptr))
		{
			m_leftEyeViewState.isValid = false;
			return;
		}

		XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
		XrFrameState frameState{ XR_TYPE_FRAME_STATE };
		const XrResult frameWaitResult = xrWaitFrame(m_sessionHandle, &frameWaitInfo, &frameState);
		if (XR_FAILED(frameWaitResult))
		{
			m_leftEyeViewState.isValid = false;
			if (frameWaitResult == XR_ERROR_SESSION_LOST)
			{
				ScheduleRecovery(L"xrWaitFrame reported a lost session");
				return;
			}
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrWaitFrame", frameWaitResult);
			return;
		}

		XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
		const XrResult frameBeginResult = xrBeginFrame(m_sessionHandle, &frameBeginInfo);
		if (XR_FAILED(frameBeginResult))
		{
			m_leftEyeViewState.isValid = false;
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrBeginFrame", frameBeginResult);
			return;
		}

		m_isFrameBegun = true;
		m_frameState = frameState;
		m_frameTiming = FrameTiming{
			true,
			static_cast<double>(frameState.predictedDisplayTime) / 1'000'000'000.0,
			static_cast<double>(frameState.predictedDisplayPeriod) / 1'000'000'000.0
		};
		if (frameState.shouldRender != XR_TRUE)
		{
			m_leftEyeViewState.isValid = false;
			m_floorEyeHeightSample.sampleTimeSeconds = m_frameTiming.predictedDisplayTimeSeconds;
			return;
		}
		m_hasLocatedViews = LocateFrameViews(frameState.predictedDisplayTime);
	}

	void NullXrSystem::SubmitFrame()
	{
#if defined(_WIN64)
		if (!m_isFrameBegun)
		{
			return;
		}
		const bool hasProjectionLayer =
		    m_hasLocatedViews && !m_projectionRenderingFailed && RenderLocatedViews();
		EndFrame(hasProjectionLayer);
		if (m_projectionRenderingFailed)
		{
			// Keep presenting empty frames meanwhile; a failed device queue or
			// fence otherwise left the session without xrWaitFrame calls forever.
			ScheduleRecovery(L"projection rendering failed");
		}
#endif
	}

	void NullXrSystem::EndFrame(const bool hasProjectionLayer)
	{
		m_isFrameBegun = false;
		m_hasLocatedViews = false;
		XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
		frameEndInfo.displayTime = m_frameState.predictedDisplayTime;
		frameEndInfo.environmentBlendMode = m_environmentBlendMode;
		frameEndInfo.layerCount = 0;
		frameEndInfo.layers = nullptr;
		const XrTime previousPredictedDisplayTime = m_lastPredictedDisplayTime;
		const XrCompositionLayerBaseHeader* layers[] = {
			reinterpret_cast<const XrCompositionLayerBaseHeader*>(&m_projectionLayer)
		};
		if (hasProjectionLayer)
		{
			frameEndInfo.layerCount = 1;
			frameEndInfo.layers = layers;
		}

		const XrResult frameEndResult = xrEndFrame(m_sessionHandle, &frameEndInfo);
		if (XR_FAILED(frameEndResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrEndFrame", frameEndResult);
			return;
		}

		m_lastFrameSummary = BuildFrameSummary(
		    m_frameState.shouldRender,
		    hasProjectionLayer,
		    m_frameState.predictedDisplayTime,
		    previousPredictedDisplayTime,
		    m_lastProjectionLayerStatus,
		    m_resultPanelLaserStatus,
		    m_xrPostProcessStatus);
		m_lastPredictedDisplayTime = m_frameState.predictedDisplayTime;
		UpdateSessionName();
	}

	bool NullXrSystem::LocateFrameViews(const XrTime predictedDisplayTime)
	{
		m_lastProjectionLayerStatus = L"locating";
		if (m_pendingReferenceChangeTime > 0 && predictedDisplayTime >= m_pendingReferenceChangeTime) {
		    ++m_floorReferenceRevision;
		    ++m_handReferenceRevision;
		    m_pendingReferenceChangeTime = 0;
		}
		if (!UpdateReferenceSpace())
		{
			m_lastProjectionLayerStatus = L"reference-space-update-failed";
			return false;
		}

		XrViewLocateInfo viewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
		viewLocateInfo.viewConfigurationType = PrimaryViewConfigurationType;
		viewLocateInfo.displayTime = predictedDisplayTime;
		viewLocateInfo.space = m_referenceSpaceHandle;

		XrViewState viewState{ XR_TYPE_VIEW_STATE };
		uint32_t viewCountOutput = 0;
		const XrResult locateViewsResult = xrLocateViews(
		    m_sessionHandle,
		    &viewLocateInfo,
		    &viewState,
		    static_cast<uint32_t>(m_views.size()),
		    &viewCountOutput,
		    m_views.data());
		if (XR_FAILED(locateViewsResult))
		{
			m_lastProjectionLayerStatus = L"locate-failed";
			m_leftEyeViewState.isValid = false;
			m_sessionName = L"Session(unavailable): " +
			                BuildXrErrorMessage(L"xrLocateViews", locateViewsResult);
			return false;
		}

		if ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0)
		{
			m_lastProjectionLayerStatus = L"view-orientation-invalid";
			m_leftEyeViewState.isValid = false;
			return false;
		}
		if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0)
		{
			m_lastProjectionLayerStatus = L"view-position-invalid";
			m_leftEyeViewState.isValid = false;
			return false;
		}

		const uint32_t maxAvailableViewCount = static_cast<uint32_t>(
		    (std::min)(m_projectionSwapchains.size(),
		               (std::min)(m_projectionLayerViews.size(), m_views.size())));
		const uint32_t viewCount = (viewCountOutput < maxAvailableViewCount)
		                               ? viewCountOutput
		                               : maxAvailableViewCount;
		if (viewCount == 0)
		{
			m_lastProjectionLayerStatus = L"view-count-zero";
			m_leftEyeViewState.isValid = false;
			return false;
		}

		m_floorEyeHeightSample = {};
		m_floorEyeHeightSample.sampleTimeSeconds = static_cast<double>(predictedDisplayTime) / 1'000'000'000.0;
		m_floorEyeHeightSample.referenceSpaceRevision = m_floorReferenceRevision;
		m_floorEyeHeightSample.isFloorReferenced = m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ||
		    m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
		m_floorEyeHeightSample.isPositionTracked =
		    (viewState.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT)) ==
		    (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT);
		if (m_floorEyeHeightSample.isFloorReferenced && m_floorEyeHeightSample.isPositionTracked && viewCount >= 2 && m_pendingReferenceChangeTime == 0) {
		    m_floorEyeHeightSample.heightMeters =
		        (static_cast<double>(m_views[0].pose.position.y) + static_cast<double>(m_views[1].pose.position.y)) * .5 -
		        m_appliedReferenceSpacePositionOffset.y;
		} else {
		    m_floorEyeHeightSample.isPositionTracked = false;
		}

		m_locatedViewCount = viewCount;
		m_leftEyeViewState = BuildProjectionStateFromView(m_views.front());
		UpdateUiPanelInteraction(predictedDisplayTime);
		return true;
	}

	bool NullXrSystem::RenderLocatedViews()
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
			const bool clearResult = ClearSwapchainImage(
			    swapchainContext,
			    imageIndex,
			    clearColor,
			    adjustedView);
			if (!releaseAcquiredImage())
			{
				return false;
			}

			if (!clearResult)
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

	void NullXrSystem::AppendTrajectoryPoint(const Physics::BallState& ballState)
	{
		Common::UpdatePhaseTrajectoryTrails(m_trajectoryTrails, ballState,
		    m_fieldPresentation, std::chrono::steady_clock::now());
	}

	bool NullXrSystem::ClearSwapchainImage(
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
		const bool useSceneDrawPath =
		    hasSceneResources &&
		    ((std::max)(1U, m_projectionSampleCount) == 1U);
		const bool canUseScenePostProcess =
		    useMsaaResolvePath || useSceneDrawPath;
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

		if (useMsaaResolvePath || useSceneDrawPath)
		{
			D3D12_RESOURCE_BARRIER& sceneToDrawBarrier =
			    toDrawBarriers[toDrawBarrierCount++];
			sceneToDrawBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneToDrawBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneToDrawBarrier.Transition.pResource = sceneColorTexture;
			sceneToDrawBarrier.Transition.StateBefore =
			    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			sceneToDrawBarrier.Transition.StateAfter = useMsaaResolvePath
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
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
		    useSceneDrawPath ? sceneRtvHandle : swapchainRtvHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = swapchainContext.dsvHeap->GetCPUDescriptorHandleForHeapStart();
		dsvHandle.ptr += static_cast<SIZE_T>(imageIndex) * static_cast<SIZE_T>(m_dsvDescriptorSize);
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

		if (useMsaaResolvePath || useSceneDrawPath)
		{
			D3D12_RESOURCE_BARRIER sceneToShaderBarrier{};
			sceneToShaderBarrier.Type =
			    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			sceneToShaderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			sceneToShaderBarrier.Transition.pResource = sceneColorTexture;
			sceneToShaderBarrier.Transition.StateBefore = useMsaaResolvePath
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
			if (useSceneDrawPath)
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

	void NullXrSystem::DestroySession()
	{
		if (m_isFrameBegun && (m_sessionHandle != XR_NULL_HANDLE))
		{
			EndFrame(false);
		}
		m_isFrameBegun = false;
		m_hasLocatedViews = false;
		m_frameTiming = {};
		if (m_isSessionRunning && (m_sessionHandle != XR_NULL_HANDLE))
		{
			LogXrCleanupFailure(
			    L"xrEndSession",
			    xrEndSession(m_sessionHandle));
		}
		m_isSessionRunning = false;
		DestroyInputActions();

		DestroyProjectionSwapchains();

		if (m_referenceSpaceHandle != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySpace",
			    xrDestroySpace(m_referenceSpaceHandle));
			m_referenceSpaceHandle = XR_NULL_HANDLE;
		}

		if (m_sessionHandle != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroySession",
			    xrDestroySession(m_sessionHandle));
			m_sessionHandle = XR_NULL_HANDLE;
		}

		m_d3d12Device.Reset();
		m_d3d12Queue.Reset();
		m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		m_sessionState = XR_SESSION_STATE_UNKNOWN;
		m_isGraphicsBound = false;
		m_usesDedicatedGraphicsDevice = false;
		m_colorSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_projectionSampleCount = 1U;
		m_trajectoryTrails.Clear();
		m_leftEyeViewState = Common::ViewProjectionState{};
		m_leftHandPose = HandPoseState{};
		m_rightHandPose = HandPoseState{};
		m_lastPredictedDisplayTime = 0;
		m_lastFrameSummary = L"frame(pending)";
		m_xrPostProcessStatus = L"none";
		++m_handReferenceRevision;
		++m_floorReferenceRevision;
	}

	void NullXrSystem::DestroyOpenXrInstance()
	{
		DestroySession();
		if (m_instanceHandle != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyInstance",
			    xrDestroyInstance(m_instanceHandle));
			m_instanceHandle = XR_NULL_HANDLE;
		}

		m_systemId = XR_NULL_SYSTEM_ID;
		m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		m_sessionState = XR_SESSION_STATE_UNKNOWN;
		m_isGraphicsBound = false;
		m_usesDedicatedGraphicsDevice = false;
		m_xrGetD3d12GraphicsRequirementsKhr = nullptr;
		m_colorSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_projectionSampleCount = 1U;
		m_trajectoryTrails.Clear();
		m_leftEyeViewState = Common::ViewProjectionState{};
		m_lastPredictedDisplayTime = 0;
		m_lastFrameSummary = L"frame(pending)";
		m_xrPostProcessStatus = L"none";
		m_sessionName = L"Session(unavailable)";
		m_mainControlPanelCenter = Physics::Vector3{
			Common::DebugWorldLayout::MainControlPanelCenterXMeters,
			Common::DebugWorldLayout::MainControlPanelCenterYMeters,
			-Common::DebugWorldLayout::MainControlPanelDepthMeters
		};
		m_resultPanelLaserStatus = L"none";
	}

	std::wstring NullXrSystem::BuildRuntimeDisplayNameFromInstance(const XrInstance instanceHandle)
	{
		if (instanceHandle == XR_NULL_HANDLE)
		{
			return L"OpenXR runtime: instance unavailable";
		}

		XrInstanceProperties properties{ XR_TYPE_INSTANCE_PROPERTIES };
		const XrResult propertyResult = xrGetInstanceProperties(instanceHandle, &properties);
		if (XR_FAILED(propertyResult))
		{
			return L"OpenXR runtime: " +
			       BuildXrErrorMessage(L"xrGetInstanceProperties", propertyResult);
		}

		std::wstring runtimeName = ConvertUtf8ToWide(properties.runtimeName);
		if (runtimeName.empty())
		{
			runtimeName = L"(unknown)";
		}

		return L"OpenXR runtime: " + runtimeName + L" API " +
		       BuildApiVersionString(properties.runtimeVersion);
	}

	void NullXrSystem::UpdateSessionName()
	{
		if(m_projectionRenderingFailed)return;
		if (m_instanceHandle == XR_NULL_HANDLE)
		{
			if ((m_sessionName.find(L"failed(") == std::wstring::npos) &&
			    (m_sessionName.find(L"missing") == std::wstring::npos))
			{
				m_sessionName = L"Session(unavailable): instance unavailable";
			}
			return;
		}

		if (m_systemId == XR_NULL_SYSTEM_ID)
		{
			if ((m_sessionName.find(L"failed(") == std::wstring::npos) &&
			    (m_sessionName.find(L"missing") == std::wstring::npos))
			{
				m_sessionName = L"Session(unavailable): system unresolved";
			}
			return;
		}

		if (!m_isGraphicsBound || m_sessionHandle == XR_NULL_HANDLE)
		{
			if (m_xrGetD3d12GraphicsRequirementsKhr == nullptr)
			{
				m_sessionName = L"Session(unavailable): XR_KHR_D3D12_enable missing";
				return;
			}

			if ((m_sessionName.find(L"failed(") == std::wstring::npos) &&
			    (m_sessionName.find(L"mismatch") == std::wstring::npos) &&
			    (m_sessionName.find(L"missing") == std::wstring::npos))
			{
				m_sessionName = L"Session(ready for graphics binding)";
			}
			return;
		}

		const std::wstring referenceSpaceName = BuildReferenceSpaceName(m_referenceSpaceType);
		if (m_isSessionRunning)
		{
			m_sessionName = L"Session: " + BuildSessionStateString(m_sessionState) +
			                (m_usesDedicatedGraphicsDevice ? L" (presenting:dedi, space:" : L" (presenting:shared, space:") +
			                referenceSpaceName + L", UI:coverage-AA)";
		}
		else
		{
			m_sessionName = L"Session: " + BuildSessionStateString(m_sessionState) +
			                (m_usesDedicatedGraphicsDevice ? L" (bound:dedi, space:" : L" (bound:shared, space:") +
			                referenceSpaceName + L", UI:coverage-AA)";
		}

		if (!m_projectionSwapchains.empty())
		{
			const SwapchainContext& primarySwapchain = m_projectionSwapchains.front();
			m_sessionName += L", views:" +
			                 std::to_wstring(m_projectionSwapchains.size()) +
			                 L", swap:" +
			                 std::to_wstring(primarySwapchain.width) +
			                 L"x" +
			                 std::to_wstring(primarySwapchain.height) +
			                 L", samples:" +
			                 std::to_wstring((std::max)(1U, m_projectionSampleCount)) +
			                 L", fmt:" +
			                 BuildDxgiFormatName(m_colorSwapchainFormat);
		}

		if (m_isSessionRunning && !m_lastFrameSummary.empty())
		{
			m_sessionName += L", " + m_lastFrameSummary;
		}
	}
#endif
} // namespace BaseballSimulatorVR::Xr
