#include "NullXrSystem.hpp"
#include "../Common/D3d12DebugOverlay.hpp"
#include "../Common/DebugWorldProjection.hpp"
#include "../Common/FxaaPipelineUtilities.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/TrajectoryTrail.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
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
	namespace
	{
		constexpr wchar_t ActiveRuntimeValueName[] = L"ActiveRuntime";
		constexpr wchar_t OpenXrRegistryPath[] = L"SOFTWARE\\Khronos\\OpenXR\\1";
		constexpr XrViewConfigurationType PrimaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		constexpr std::array<float, 4> LeftEyeClearColor{ 0.02F, 0.03F, 0.08F, 1.0F };
		constexpr std::array<float, 4> RightEyeClearColor{ 0.03F, 0.02F, 0.07F, 1.0F };
		constexpr std::array<DXGI_FORMAT, 4> PreferredSwapchainFormats{
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM
		};
		constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
		constexpr UINT MaxDebugVertices = 65536;
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

		[[nodiscard]] std::wstring ExtractPitchSettingsDisplayName(
		    const std::wstring_view fileName)
		{
			constexpr std::wstring_view Prefix = L"PitchSettings.";
			constexpr std::wstring_view Suffix = L".json";
			if (fileName.starts_with(Prefix) &&
			    fileName.ends_with(Suffix) &&
			    (fileName.size() > (Prefix.size() + Suffix.size())))
			{
				return std::wstring{
					fileName.substr(
					    Prefix.size(),
					    fileName.size() - Prefix.size() - Suffix.size())
				};
			}
			return std::wstring{ fileName };
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

		[[nodiscard]] std::wstring BuildHresultHex(const HRESULT result)
		{
			std::wstringstream stream{};
			stream << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
			       << static_cast<unsigned long>(result);
			return stream.str();
		}

		[[nodiscard]] std::wstring BuildPipelineInitPhaseToken(
		    const Common::DebugOverlay::PipelineInitializationPhase phase)
		{
			switch (phase)
			{
				case Common::DebugOverlay::PipelineInitializationPhase::InvalidArguments:
					return L"invalidargs";
				case Common::DebugOverlay::PipelineInitializationPhase::SerializeRootSignature:
					return L"serializeroot";
				case Common::DebugOverlay::PipelineInitializationPhase::CreateRootSignature:
					return L"createroot";
				case Common::DebugOverlay::PipelineInitializationPhase::CompileVertexShader:
					return L"compilevs";
				case Common::DebugOverlay::PipelineInitializationPhase::CompilePixelShader:
					return L"compileps";
				case Common::DebugOverlay::PipelineInitializationPhase::CreatePipelineState:
					return L"createpso";
				case Common::DebugOverlay::PipelineInitializationPhase::None:
				default:
					return L"none";
			}
		}

		[[nodiscard]] std::wstring BuildVertexInitPhaseToken(
		    const Common::DebugOverlay::VertexBufferInitializationPhase phase)
		{
			switch (phase)
			{
				case Common::DebugOverlay::VertexBufferInitializationPhase::InvalidArguments:
					return L"invalidargs";
				case Common::DebugOverlay::VertexBufferInitializationPhase::CreateCommittedResource:
					return L"createresource";
				case Common::DebugOverlay::VertexBufferInitializationPhase::None:
				default:
					return L"none";
			}
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

		[[nodiscard]] std::wstring BuildDebugVertexBufferInitializationErrorMessage(
		    const Common::DebugOverlay::VertexBufferInitializationPhase failedPhase,
		    const HRESULT failedResult)
		{
			switch (failedPhase)
			{
				case Common::DebugOverlay::VertexBufferInitializationPhase::InvalidArguments:
					return L"debug vertex buffer initialization arguments invalid";
				case Common::DebugOverlay::VertexBufferInitializationPhase::CreateCommittedResource:
					return BuildD3d12ErrorMessage(
					    L"ID3D12Device::CreateCommittedResource",
					    failedResult);
				case Common::DebugOverlay::VertexBufferInitializationPhase::None:
				default:
					return L"debug vertex buffer initialization failed";
			}
		}

		[[nodiscard]] std::wstring BuildDebugVertexUploadErrorMessage(
		    const Common::DebugOverlay::VertexUploadPhase failedPhase,
		    const HRESULT failedResult)
		{
			switch (failedPhase)
			{
				case Common::DebugOverlay::VertexUploadPhase::InvalidArguments:
					return L"debug vertex upload arguments invalid";
				case Common::DebugOverlay::VertexUploadPhase::Map:
					return BuildD3d12ErrorMessage(L"ID3D12Resource::Map", failedResult);
				case Common::DebugOverlay::VertexUploadPhase::None:
				default:
					return L"debug vertex upload failed";
			}
		}

		[[nodiscard]] bool IsSameLuid(const LUID& lhs, const LUID& rhs) noexcept
		{
			return (lhs.LowPart == rhs.LowPart) && (lhs.HighPart == rhs.HighPart);
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

		[[nodiscard]] XrQuaternionf MultiplyQuaternions(
		    const XrQuaternionf& left,
		    const XrQuaternionf& right)
		{
			return NormalizeQuaternion(XrQuaternionf{
			    static_cast<float>(
			        (static_cast<double>(left.w) * static_cast<double>(right.x)) +
			        (static_cast<double>(left.x) * static_cast<double>(right.w)) +
			        (static_cast<double>(left.y) * static_cast<double>(right.z)) -
			        (static_cast<double>(left.z) * static_cast<double>(right.y))),
			    static_cast<float>(
			        (static_cast<double>(left.w) * static_cast<double>(right.y)) -
			        (static_cast<double>(left.x) * static_cast<double>(right.z)) +
			        (static_cast<double>(left.y) * static_cast<double>(right.w)) +
			        (static_cast<double>(left.z) * static_cast<double>(right.x))),
			    static_cast<float>(
			        (static_cast<double>(left.w) * static_cast<double>(right.z)) +
			        (static_cast<double>(left.x) * static_cast<double>(right.y)) -
			        (static_cast<double>(left.y) * static_cast<double>(right.x)) +
			        (static_cast<double>(left.z) * static_cast<double>(right.w))),
			    static_cast<float>(
			        (static_cast<double>(left.w) * static_cast<double>(right.w)) -
			        (static_cast<double>(left.x) * static_cast<double>(right.x)) -
			        (static_cast<double>(left.y) * static_cast<double>(right.y)) -
			        (static_cast<double>(left.z) * static_cast<double>(right.z))) });
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

		[[nodiscard]] XrQuaternionf BuildPitchOffsetOrientation(
		    const XrQuaternionf& referenceOrientation,
		    const double pitchRadians)
		{
			if (std::abs(pitchRadians) <= 1e-9)
			{
				return XrQuaternionf{ 0.0F, 0.0F, 0.0F, 1.0F };
			}

			const Physics::Vector3 pitchAxis = RotateVectorByQuaternion(
			    Physics::Vector3{ 1.0, 0.0, 0.0 },
			    NormalizeQuaternion(referenceOrientation));
			return BuildAxisAngleQuaternion(pitchAxis, pitchRadians);
		}

		[[nodiscard]] XrQuaternionf BuildViewOrientationWithLookOffsets(
		    const XrQuaternionf& sourceOrientation,
		    const double yawRadians,
		    const double pitchRadians)
		{
			const XrQuaternionf yawOffsetOrientation =
			    BuildYawOffsetOrientation(yawRadians);
			const XrQuaternionf yawAdjustedOrientation = MultiplyQuaternions(
			    yawOffsetOrientation,
			    sourceOrientation);
			const XrQuaternionf pitchOffsetOrientation =
			    BuildPitchOffsetOrientation(
			        yawAdjustedOrientation,
			        pitchRadians);
			return MultiplyQuaternions(
			    pitchOffsetOrientation,
			    yawAdjustedOrientation);
		}

		[[nodiscard]] Physics::Vector3 RotatePointAroundPivot(
		    const Physics::Vector3& point,
		    const Physics::Vector3& pivot,
		    const XrQuaternionf& rotation)
		{
			return pivot + RotateVectorByQuaternion(point - pivot, rotation);
		}

		[[nodiscard]] Physics::Vector3 ApplyYawAndPositionOffset(
		    const Physics::Vector3& sourcePosition,
		    const double yawRadians,
		    const Physics::Vector3& positionOffset)
		{
			const double c = std::cos(yawRadians);
			const double s = std::sin(yawRadians);
			const double rotatedX = (sourcePosition.x * c) - (sourcePosition.z * s);
			const double rotatedZ = (sourcePosition.x * s) + (sourcePosition.z * c);
			return Physics::Vector3{
				rotatedX + positionOffset.x,
				sourcePosition.y + positionOffset.y,
				rotatedZ + positionOffset.z
			};
		}

		[[nodiscard]] bool TryBuildLookPitchPivot(
		    const std::span<const XrView> sourceViews,
		    const double yawRadians,
		    const Physics::Vector3& positionOffset,
		    const Common::ViewProjectionState* const fallbackViewState,
		    Physics::Vector3* const outPivot)
		{
			if (outPivot == nullptr)
			{
				return false;
			}

			if (!sourceViews.empty())
			{
				const std::size_t sampleCount = (std::min)(sourceViews.size(),
				                                           static_cast<std::size_t>(2));
				double sumX = 0.0;
				double sumY = 0.0;
				double sumZ = 0.0;
				for (std::size_t viewIndex = 0; viewIndex < sampleCount; ++viewIndex)
				{
					sumX += static_cast<double>(sourceViews[viewIndex].pose.position.x);
					sumY += static_cast<double>(sourceViews[viewIndex].pose.position.y);
					sumZ += static_cast<double>(sourceViews[viewIndex].pose.position.z);
				}

				const double inverseCount =
				    1.0 / static_cast<double>(sampleCount);
				*outPivot = ApplyYawAndPositionOffset(
				    Physics::Vector3{
				        sumX * inverseCount,
				        sumY * inverseCount,
				        sumZ * inverseCount },
				    yawRadians,
				    positionOffset);
				return true;
			}

			if ((fallbackViewState != nullptr) && fallbackViewState->isValid)
			{
				*outPivot = fallbackViewState->position;
				return true;
			}

			return false;
		}

		[[nodiscard]] bool TryBuildLookPitchOrientation(
		    const std::span<const XrView> sourceViews,
		    const double yawRadians,
		    const Common::ViewProjectionState* const fallbackViewState,
		    const double pitchRadians,
		    XrQuaternionf* const outOrientation)
		{
			if (outOrientation == nullptr)
			{
				return false;
			}

			*outOrientation = XrQuaternionf{ 0.0F, 0.0F, 0.0F, 1.0F };
			if (std::abs(pitchRadians) <= 1e-9)
			{
				return false;
			}

			XrQuaternionf referenceOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
			bool hasReferenceOrientation = false;
			if (!sourceViews.empty())
			{
				referenceOrientation = NormalizeQuaternion(
				    sourceViews.front().pose.orientation);
				hasReferenceOrientation = true;
			}
			else if ((fallbackViewState != nullptr) && fallbackViewState->isValid)
			{
				referenceOrientation = NormalizeQuaternion(XrQuaternionf{
				    static_cast<float>(fallbackViewState->orientation.x),
				    static_cast<float>(fallbackViewState->orientation.y),
				    static_cast<float>(fallbackViewState->orientation.z),
				    static_cast<float>(fallbackViewState->orientationW) });
				hasReferenceOrientation = true;
			}

			if (!hasReferenceOrientation)
			{
				return false;
			}

			const XrQuaternionf yawAdjustedOrientation = MultiplyQuaternions(
			    BuildYawOffsetOrientation(yawRadians),
			    referenceOrientation);
			*outOrientation = BuildPitchOffsetOrientation(
			    yawAdjustedOrientation,
			    pitchRadians);
			return true;
		}

		[[nodiscard]] XrPosef ApplyLocomotionToPose(
		    const XrPosef& sourcePose,
		    const double yawRadians,
		    const double pitchRadians,
		    const Physics::Vector3& positionOffset,
		    const Physics::Vector3* const lookPitchPivot = nullptr,
		    const XrQuaternionf* const lookPitchOrientation = nullptr)
		{
			XrPosef adjustedPose = sourcePose;
			const XrQuaternionf yawAdjustedOrientation = MultiplyQuaternions(
			    BuildYawOffsetOrientation(yawRadians),
			    sourcePose.orientation);
			const bool hasPitchOffset = std::abs(pitchRadians) > 1e-9;
			XrQuaternionf pitchOffsetOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
			if (hasPitchOffset)
			{
				pitchOffsetOrientation =
				    (lookPitchOrientation != nullptr)
				        ? *lookPitchOrientation
				        : BuildPitchOffsetOrientation(
				              yawAdjustedOrientation,
				              pitchRadians);
			}

			adjustedPose.orientation = hasPitchOffset
			                               ? MultiplyQuaternions(
			                                     pitchOffsetOrientation,
			                                     yawAdjustedOrientation)
			                               : yawAdjustedOrientation;
			Physics::Vector3 adjustedPosition = ApplyYawAndPositionOffset(
			    Physics::Vector3{
			        static_cast<double>(sourcePose.position.x),
			        static_cast<double>(sourcePose.position.y),
			        static_cast<double>(sourcePose.position.z) },
			    yawRadians,
			    positionOffset);
			if ((lookPitchPivot != nullptr) && hasPitchOffset)
			{
				adjustedPosition = RotatePointAroundPivot(
				    adjustedPosition,
				    *lookPitchPivot,
				    pitchOffsetOrientation);
			}

			adjustedPose.position.x = static_cast<float>(adjustedPosition.x);
			adjustedPose.position.y = static_cast<float>(adjustedPosition.y);
			adjustedPose.position.z = static_cast<float>(adjustedPosition.z);
			return adjustedPose;
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

		struct RasterizedUiText final
		{
			int width{ 1 };
			int height{ 1 };
			std::vector<std::uint8_t> coverage{ 0U };
		};

		[[nodiscard]] RasterizedUiText BuildRasterizedUiText(
		    const std::wstring_view text,
		    const int requestedPixelHeight)
		{
			RasterizedUiText raster{};
			if (text.empty())
			{
				return raster;
			}

			const int pixelHeight = (std::max)(12, requestedPixelHeight);
			HDC memoryDeviceContext = CreateCompatibleDC(nullptr);
			if (memoryDeviceContext == nullptr)
			{
				return raster;
			}

			static constexpr std::array<const wchar_t*, 3> FontFamilies{
				L"Meiryo UI",
				L"Meiryo",
				L"Segoe UI"
			};
			HFONT selectedFont = nullptr;
			HFONT previousFont = nullptr;
			SIZE textSize{};
			for (const wchar_t* const fontFamily : FontFamilies)
			{
				HFONT candidateFont = CreateFontW(
				    -pixelHeight,
				    0,
				    0,
				    0,
				    FW_SEMIBOLD,
				    FALSE,
				    FALSE,
				    FALSE,
				    DEFAULT_CHARSET,
				    OUT_OUTLINE_PRECIS,
				    CLIP_DEFAULT_PRECIS,
				    CLEARTYPE_QUALITY,
				    DEFAULT_PITCH | FF_DONTCARE,
				    fontFamily);
				if (candidateFont == nullptr)
				{
					continue;
				}

				previousFont =
				    static_cast<HFONT>(SelectObject(memoryDeviceContext, candidateFont));
				SIZE measuredSize{};
				const BOOL measured = GetTextExtentPoint32W(
				    memoryDeviceContext,
				    text.data(),
				    static_cast<int>(text.size()),
				    &measuredSize);
				if ((measured != FALSE) && (measuredSize.cx > 0) &&
				    (measuredSize.cy > 0))
				{
					selectedFont = candidateFont;
					textSize = measuredSize;
					break;
				}

				SelectObject(memoryDeviceContext, previousFont);
				DeleteObject(candidateFont);
				previousFont = nullptr;
			}

			if (selectedFont == nullptr)
			{
				DeleteDC(memoryDeviceContext);
				return raster;
			}

			raster.width = (std::max)(1, static_cast<int>(textSize.cx) + 4);
			raster.height = (std::max)(1, static_cast<int>(textSize.cy) + 4);
			BITMAPINFO bitmapInfo{};
			bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmapInfo.bmiHeader.biWidth = raster.width;
			bitmapInfo.bmiHeader.biHeight = -raster.height;
			bitmapInfo.bmiHeader.biPlanes = 1;
			bitmapInfo.bmiHeader.biBitCount = 32;
			bitmapInfo.bmiHeader.biCompression = BI_RGB;

			void* bitmapBits = nullptr;
			HBITMAP textBitmap = CreateDIBSection(
			    memoryDeviceContext,
			    &bitmapInfo,
			    DIB_RGB_COLORS,
			    &bitmapBits,
			    nullptr,
			    0);
			if ((textBitmap == nullptr) || (bitmapBits == nullptr))
			{
				SelectObject(memoryDeviceContext, previousFont);
				DeleteObject(selectedFont);
				DeleteDC(memoryDeviceContext);
				return raster;
			}

			HBITMAP previousBitmap =
			    static_cast<HBITMAP>(SelectObject(memoryDeviceContext, textBitmap));
			RECT canvasRect{
				0,
				0,
				raster.width,
				raster.height
			};
			HBRUSH backgroundBrush = CreateSolidBrush(RGB(0, 0, 0));
			if (backgroundBrush != nullptr)
			{
				FillRect(memoryDeviceContext, &canvasRect, backgroundBrush);
				DeleteObject(backgroundBrush);
			}
			SetBkMode(memoryDeviceContext, TRANSPARENT);
			SetTextColor(memoryDeviceContext, RGB(255, 255, 255));
			(void)TextOutW(
			    memoryDeviceContext,
			    2,
			    2,
			    text.data(),
			    static_cast<int>(text.size()));

			raster.coverage.assign(
			    static_cast<std::size_t>(raster.width) *
			        static_cast<std::size_t>(raster.height),
			    0U);
			const auto* const pixels =
			    static_cast<const std::uint32_t*>(bitmapBits);
			for (int y = 0; y < raster.height; ++y)
			{
				for (int x = 0; x < raster.width; ++x)
				{
					const std::size_t pixelIndex =
					    (static_cast<std::size_t>(y) *
					     static_cast<std::size_t>(raster.width)) +
					    static_cast<std::size_t>(x);
					const std::uint32_t pixel = pixels[pixelIndex];
					const std::uint8_t blue = static_cast<std::uint8_t>(pixel & 0xFFU);
					const std::uint8_t green =
					    static_cast<std::uint8_t>((pixel >> 8) & 0xFFU);
					const std::uint8_t red =
					    static_cast<std::uint8_t>((pixel >> 16) & 0xFFU);
					raster.coverage[pixelIndex] = (std::max)(red, (std::max)(green, blue));
				}
			}

			SelectObject(memoryDeviceContext, previousBitmap);
			SelectObject(memoryDeviceContext, previousFont);
			DeleteObject(textBitmap);
			DeleteObject(selectedFont);
			DeleteDC(memoryDeviceContext);
			return raster;
		}

		struct OrientedUiPanelLayout final
		{
			Physics::Vector3 center{};
			Physics::Vector3 rightAxis{ 1.0, 0.0, 0.0 };
			Physics::Vector3 upAxis{ 0.0, 1.0, 0.0 };
			Physics::Vector3 forwardAxis{ 0.0, 0.0, -1.0 };
			double widthMeters{ 0.30 };
			double heightMeters{ 0.24 };
			double paddingMeters{ 0.012 };
			double cellGapMeters{ 0.006 };
			int columns{ 1 };
			int rows{ 1 };
		};

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

		struct UiPanelLocalRect final
		{
			double left{ 0.0 };
			double top{ 0.0 };
			double right{ 0.0 };
			double bottom{ 0.0 };
		};

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

		[[nodiscard]] const RasterizedUiText& GetCachedUiLabelBitmap(
		    const std::wstring_view label,
		    const int requestedPixelHeight)
		{
			static std::unordered_map<std::wstring, RasterizedUiText> cache{};
			const int pixelHeight = (std::max)(12, requestedPixelHeight);
			std::wstring key{ label };
			key += L"#";
			key += std::to_wstring(pixelHeight);
			const auto existing = cache.find(key);
			if (existing != cache.end())
			{
				return existing->second;
			}

			RasterizedUiText generated = BuildRasterizedUiText(label, pixelHeight);
			const auto insertResult = cache.emplace(key, std::move(generated));
			return insertResult.first->second;
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
		    TDrawTriangleFn&& drawTriangle)
		{
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
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    const UiPanelLocalRect& rect,
		    const std::wstring_view label,
		    const int fontPixelHeight,
		    const double rectWidthUsage,
		    const double rectHeightUsage,
		    const double alignX,
		    const double alignY,
		    const std::array<float, 4>& textColor,
		    TDrawTriangleFn&& drawTriangle)
		{
			if (!projectionState.isValid || (width <= 0) || (height <= 0))
			{
				return;
			}
			if (label.empty())
			{
				return;
			}
			if ((rect.right <= rect.left) || (rect.top <= rect.bottom))
			{
				return;
			}

			const RasterizedUiText& bitmap =
			    GetCachedUiLabelBitmap(label, fontPixelHeight);
			if ((bitmap.width <= 0) || (bitmap.height <= 0))
			{
				return;
			}

			const double rectWidth = rect.right - rect.left;
			const double rectHeight = rect.top - rect.bottom;
			const double widthUsage = std::clamp(rectWidthUsage, 0.1, 1.0);
			const double heightUsage = std::clamp(rectHeightUsage, 0.1, 1.0);
			const double availableWidth = rectWidth * widthUsage;
			const double availableHeight = rectHeight * heightUsage;
			if ((availableWidth <= 1e-6) || (availableHeight <= 1e-6))
			{
				return;
			}
			const double availableLeft =
			    rect.left + ((rectWidth - availableWidth) * 0.5);
			const double availableTop =
			    rect.top - ((rectHeight - availableHeight) * 0.5);
			const double pixelScale = (std::min)(availableWidth / static_cast<double>(bitmap.width),
			                                     availableHeight / static_cast<double>(bitmap.height));
			if (pixelScale <= 1e-6)
			{
				return;
			}

			const double pixelWorldWidth = pixelScale;
			const double pixelWorldHeight = pixelScale;
			const double textWorldWidth =
			    static_cast<double>(bitmap.width) * pixelWorldWidth;
			const double textWorldHeight =
			    static_cast<double>(bitmap.height) * pixelWorldHeight;
			const double clampedAlignX = std::clamp(alignX, 0.0, 1.0);
			const double clampedAlignY = std::clamp(alignY, 0.0, 1.0);
			const double textLeft = availableLeft +
			                        ((availableWidth - textWorldWidth) * clampedAlignX);
			const double textTop = availableTop -
			                       ((availableHeight - textWorldHeight) * clampedAlignY);
			const double cameraSide = Physics::Dot(
			    projectionState.position - panelLayout.center,
			    panelLayout.forwardAxis);
			const double towardCameraDepthSign = (cameraSide >= 0.0) ? 1.0 : -1.0;
			const double textDepthOffset = 0.0240 * towardCameraDepthSign;
			for (int y = 0; y < bitmap.height; ++y)
			{
				int runStartPixel = -1;
				for (int x = 0; x <= bitmap.width; ++x)
				{
					bool isTextPixel = false;
					if (x < bitmap.width)
					{
						const std::size_t pixelIndex =
						    (static_cast<std::size_t>(y) *
						     static_cast<std::size_t>(bitmap.width)) +
						    static_cast<std::size_t>(x);
						isTextPixel = (bitmap.coverage[pixelIndex] >= 96U);
					}

					if (isTextPixel && (runStartPixel < 0))
					{
						runStartPixel = x;
					}
					else if (!isTextPixel && (runStartPixel >= 0))
					{
						const double runLeft =
						    textLeft +
						    (static_cast<double>(runStartPixel) * pixelWorldWidth);
						const double runRight =
						    textLeft + (static_cast<double>(x) * pixelWorldWidth);
						const double runTop =
						    textTop - (static_cast<double>(y) * pixelWorldHeight);
						const double runBottom = runTop - pixelWorldHeight;
						const Physics::Vector3 topLeft = ConvertPanelLocalToWorld(
						    panelLayout,
						    runLeft,
						    runTop,
						    textDepthOffset);
						const Physics::Vector3 topRight = ConvertPanelLocalToWorld(
						    panelLayout,
						    runRight,
						    runTop,
						    textDepthOffset);
						const Physics::Vector3 bottomRight = ConvertPanelLocalToWorld(
						    panelLayout,
						    runRight,
						    runBottom,
						    textDepthOffset);
						const Physics::Vector3 bottomLeft = ConvertPanelLocalToWorld(
						    panelLayout,
						    runLeft,
						    runBottom,
						    textDepthOffset);
						Common::EmitProjectedWorldTriangle(
						    topLeft,
						    topRight,
						    bottomRight,
						    projectionState,
						    width,
						    height,
						    textColor.data(),
						    drawTriangle);
						Common::EmitProjectedWorldTriangle(
						    topLeft,
						    bottomRight,
						    bottomLeft,
						    projectionState,
						    width,
						    height,
						    textColor.data(),
						    drawTriangle);
						runStartPixel = -1;
					}
				}
			}
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

		constexpr std::size_t MetricsControlButtonCount = 11U;

		[[nodiscard]] bool TryBuildMetricsPanelBodyLayout(
		    const OrientedUiPanelLayout& panelLayout,
		    UiPanelLocalRect* const bodyRect,
		    std::array<UiPanelLocalRect, MetricsControlButtonCount>* const controlButtonRects)
		{
			if ((bodyRect == nullptr) || (controlButtonRects == nullptr))
			{
				return false;
			}

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return false;
			}

			const double titleBarBottom = innerLayout.innerTop - innerLayout.cellHeight;
			const double controlGap =
			    std::clamp(panelLayout.cellGapMeters * 0.8, 0.0025, 0.0100);
			const double buttonHeight = std::clamp(
			    innerLayout.cellHeight * 0.80,
			    0.018,
			    0.045);
			const double buttonTop = innerLayout.innerBottom + buttonHeight;
			const double buttonBottom = innerLayout.innerBottom;
			const double availableButtonWidth =
			    innerLayout.innerRight - innerLayout.innerLeft -
			    (controlGap * static_cast<double>(controlButtonRects->size() - 1U));
			if (availableButtonWidth <= 1e-6)
			{
				return false;
			}
			const double buttonWidth = availableButtonWidth /
			                           static_cast<double>(controlButtonRects->size());
			for (std::size_t buttonIndex = 0;
			     buttonIndex < controlButtonRects->size();
			     ++buttonIndex)
			{
				const double left =
				    innerLayout.innerLeft +
				    (static_cast<double>(buttonIndex) * (buttonWidth + controlGap));
				(*controlButtonRects)[buttonIndex] = UiPanelLocalRect{
					left,
					buttonTop,
					left + buttonWidth,
					buttonBottom
				};
			}

			bodyRect->left = innerLayout.innerLeft;
			bodyRect->top = titleBarBottom - controlGap;
			bodyRect->right = innerLayout.innerRight;
			bodyRect->bottom = buttonTop + controlGap;
			return (bodyRect->right > bodyRect->left) &&
			       (bodyRect->top > bodyRect->bottom);
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

			for (std::size_t index = 0; index < controlButtonRects.size(); ++index)
			{
				if (!IsPointInsidePanelLocalRect(
				        controlButtonRects[index],
				        localX,
				        localY))
				{
					continue;
				}
				*buttonIndex = static_cast<int>(index);
				return true;
			}

			return false;
		}

		template <typename TResolveRowColorFn, typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    TResolveRowColorFn&& resolveRowColor,
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
		    TDrawTriangleFn&& drawTriangle)
		{
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
			if (cellIndex == nullptr)
			{
				return false;
			}
			*cellIndex = -1;

			UiPanelInnerLayout innerLayout{};
			if (!TryBuildPanelInnerLayout(panelLayout, &innerLayout))
			{
				return false;
			}

			const Physics::Vector3 relative = panelHitPoint - panelLayout.center;
			const double localX = Physics::Dot(relative, panelLayout.rightAxis);
			const double localY = Physics::Dot(relative, panelLayout.upAxis);
			if ((localX < innerLayout.innerLeft) ||
			    (localX > innerLayout.innerRight) ||
			    (localY > innerLayout.innerTop) ||
			    (localY < innerLayout.innerBottom))
			{
				return false;
			}

			for (int rowIndex = 0; rowIndex < panelLayout.rows; ++rowIndex)
			{
				const double cellTop = innerLayout.innerTop -
				                       (static_cast<double>(rowIndex) *
				                        (innerLayout.cellHeight + panelLayout.cellGapMeters));
				const double cellBottom = cellTop - innerLayout.cellHeight;
				for (int columnIndex = 0; columnIndex < panelLayout.columns; ++columnIndex)
				{
					const double cellLeft =
					    innerLayout.innerLeft +
					    (static_cast<double>(columnIndex) *
					     (innerLayout.cellWidth + panelLayout.cellGapMeters));
					const double cellRight = cellLeft + innerLayout.cellWidth;
					if ((localX >= cellLeft) && (localX <= cellRight) &&
					    (localY <= cellTop) && (localY >= cellBottom))
					{
						*cellIndex = (rowIndex * panelLayout.columns) + columnIndex;
						return true;
					}
				}
			}

			return false;
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
		    TDrawTriangleFn&& drawTriangle)
		{
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

		template <typename TLabelResolverFn, typename TDrawTriangleFn>
		void EmitProjectedOrientedPanelTextOverlay(
		    const Common::ViewProjectionState& projectionState,
		    const LONG width,
		    const LONG height,
		    const OrientedUiPanelLayout& panelLayout,
		    const int labelCount,
		    const int fontPixelHeight,
		    const double cellWidthUsage,
		    const double cellHeightUsage,
		    TLabelResolverFn&& resolveLabel,
		    TDrawTriangleFn&& drawTriangle)
		{
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

			const std::array<float, 4> textColor{ 1.0F, 1.0F, 1.0F, 1.0F };
			const double cameraSide = Physics::Dot(
			    projectionState.position - panelLayout.center,
			    panelLayout.forwardAxis);
			const double towardCameraDepthSign = (cameraSide >= 0.0) ? 1.0 : -1.0;
			const double textDepthOffset = 0.0240 * towardCameraDepthSign;
			const int clampedLabelCount = (std::min)((std::max)(0, labelCount),
			                                         panelLayout.columns * panelLayout.rows);
			for (int labelIndex = 0; labelIndex < clampedLabelCount; ++labelIndex)
			{
				const std::wstring_view label = resolveLabel(labelIndex);
				if (label.empty())
				{
					continue;
				}

				const int rowIndex = labelIndex / panelLayout.columns;
				const int columnIndex = labelIndex % panelLayout.columns;
				const double cellTop = innerLayout.innerTop -
				                       (static_cast<double>(rowIndex) *
				                        (innerLayout.cellHeight + panelLayout.cellGapMeters));
				const double cellBottom = cellTop - innerLayout.cellHeight;
				const double cellLeft =
				    innerLayout.innerLeft +
				    (static_cast<double>(columnIndex) *
				     (innerLayout.cellWidth + panelLayout.cellGapMeters));
				const double cellRight = cellLeft + innerLayout.cellWidth;

				const RasterizedUiText& bitmap =
				    GetCachedUiLabelBitmap(label, fontPixelHeight);
				if ((bitmap.width <= 0) || (bitmap.height <= 0))
				{
					continue;
				}

				const double availableTextWidth =
				    innerLayout.cellWidth * std::clamp(cellWidthUsage, 0.1, 1.0);
				const double availableTextHeight =
				    innerLayout.cellHeight * std::clamp(cellHeightUsage, 0.1, 1.0);
				const double pixelScale = (std::min)(availableTextWidth / static_cast<double>(bitmap.width),
				                                     availableTextHeight / static_cast<double>(bitmap.height));
				if (pixelScale <= 1e-6)
				{
					continue;
				}

				const double pixelWorldWidth = pixelScale;
				const double pixelWorldHeight = pixelScale;
				const double textWorldWidth =
				    static_cast<double>(bitmap.width) * pixelWorldWidth;
				const double textWorldHeight =
				    static_cast<double>(bitmap.height) * pixelWorldHeight;
				const double textLeft =
				    ((cellLeft + cellRight) - textWorldWidth) * 0.5;
				const double textTop =
				    ((cellTop + cellBottom) + textWorldHeight) * 0.5;

				for (int y = 0; y < bitmap.height; ++y)
				{
					int runStartPixel = -1;
					for (int x = 0; x <= bitmap.width; ++x)
					{
						bool isTextPixel = false;
						if (x < bitmap.width)
						{
							const std::size_t pixelIndex =
							    (static_cast<std::size_t>(y) *
							     static_cast<std::size_t>(bitmap.width)) +
							    static_cast<std::size_t>(x);
							isTextPixel = (bitmap.coverage[pixelIndex] >= 96U);
						}

						if (isTextPixel && (runStartPixel < 0))
						{
							runStartPixel = x;
						}

						if ((!isTextPixel || (x == bitmap.width)) &&
						    (runStartPixel >= 0))
						{
							const int runEndPixel = x;
							const double left =
							    textLeft +
							    (static_cast<double>(runStartPixel) *
							     pixelWorldWidth);
							const double right =
							    textLeft +
							    (static_cast<double>(runEndPixel) *
							     pixelWorldWidth);
							const double top =
							    textTop -
							    (static_cast<double>(y) * pixelWorldHeight);
							const double bottom = top - pixelWorldHeight;
							const Physics::Vector3 topLeft = ConvertPanelLocalToWorld(
							    panelLayout,
							    left,
							    top,
							    textDepthOffset);
							const Physics::Vector3 topRight = ConvertPanelLocalToWorld(
							    panelLayout,
							    right,
							    top,
							    textDepthOffset);
							const Physics::Vector3 bottomRight = ConvertPanelLocalToWorld(
							    panelLayout,
							    right,
							    bottom,
							    textDepthOffset);
							const Physics::Vector3 bottomLeft = ConvertPanelLocalToWorld(
							    panelLayout,
							    left,
							    bottom,
							    textDepthOffset);

							Common::EmitProjectedWorldTriangle(
							    topLeft,
							    topRight,
							    bottomRight,
							    projectionState,
							    width,
							    height,
							    textColor.data(),
							    drawTriangle);
							Common::EmitProjectedWorldTriangle(
							    topLeft,
							    bottomRight,
							    bottomLeft,
							    projectionState,
							    width,
							    height,
							    textColor.data(),
							    drawTriangle);
							runStartPixel = -1;
						}
					}
				}
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
		constexpr int PitchSettingsNameCellIndex =
		    PitchSettingsPreviousCellIndex + 1;
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
			layout.widthMeters = 0.27;
			layout.heightMeters = 0.16;
			layout.paddingMeters = 0.010;
			layout.cellGapMeters = 0.004;
			layout.columns = 3;
			layout.rows = 2;
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

			layout.widthMeters = 0.34;
			layout.heightMeters = 0.45;
			layout.paddingMeters = 0.009;
			layout.cellGapMeters = 0.004;
			layout.columns = 5;
			layout.rows = 9;
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
			layout.heightMeters = 0.23;
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
		if (!m_isInitialized)
		{
			return;
		}

#if defined(_WIN64)
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

			if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
			{
				const auto* sessionStateChanged =
				    reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
				HandleSessionStateChanged(sessionStateChanged->state);
			}
		}

		TryRenderFrame();
#endif
	}

	bool NullXrSystem::UpdateReferenceSpace()
	{
#if defined(_WIN64)
		if ((m_sessionHandle == XR_NULL_HANDLE) ||
		    (m_referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE &&
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

	HandPoseState NullXrSystem::GetLeftHandPose() const noexcept
	{
		return m_leftHandPose;
	}

	HandPoseState NullXrSystem::GetRightHandPose() const noexcept
	{
		return m_rightHandPose;
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
						break;
					}
				}
			}
		}

		std::vector<const char*> enabledExtensions{};
		if (hasD3d12Extension)
		{
			enabledExtensions.push_back(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
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
		XrResult referenceSpaceResult = tryCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_STAGE);
		if (XR_SUCCEEDED(referenceSpaceResult))
		{
			m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
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
			swapchainCreateInfo.width =
			    (viewConfiguration.recommendedImageRectWidth == 0)
			        ? 1U
			        : viewConfiguration.recommendedImageRectWidth;
			swapchainCreateInfo.height =
			    (viewConfiguration.recommendedImageRectHeight == 0)
			        ? 1U
			        : viewConfiguration.recommendedImageRectHeight;
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

		const HRESULT allocatorCreateResult = m_d3d12Device->CreateCommandAllocator(
		    D3D12_COMMAND_LIST_TYPE_DIRECT,
		    IID_PPV_ARGS(&m_xrCommandAllocator));
		if (FAILED(allocatorCreateResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12Device::CreateCommandAllocator", allocatorCreateResult);
			DestroyProjectionSwapchains();
			return false;
		}

		const HRESULT commandListCreateResult = m_d3d12Device->CreateCommandList(
		    0,
		    D3D12_COMMAND_LIST_TYPE_DIRECT,
		    m_xrCommandAllocator.Get(),
		    nullptr,
		    IID_PPV_ARGS(&m_xrCommandList));
		if (FAILED(commandListCreateResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12Device::CreateCommandList", commandListCreateResult);
			DestroyProjectionSwapchains();
			return false;
		}

		if (FAILED(m_xrCommandList->Close()))
		{
			m_sessionName = L"Session(unavailable): ID3D12GraphicsCommandList::Close failed";
			DestroyProjectionSwapchains();
			return false;
		}

		if (EnableDebugDraw &&
		    (!InitializeDebugPipeline() || !InitializeDebugVertexBuffer()))
		{
			DestroyProjectionSwapchains();
			return false;
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

		failedPhase = Common::DebugOverlay::PipelineInitializationPhase::None;
		failedResult = S_OK;
		if (!Common::DebugOverlay::InitializePipeline(
		        m_d3d12Device.Get(),
		        ResolveRtvFormat(m_colorSwapchainFormat),
		        DepthBufferFormat,
		        &m_debugRootSignature,
		        &m_debugAlphaPipelineState,
		        &failedPhase,
		        &failedResult,
		        Common::DebugOverlay::PipelineBlendMode::AlphaBlend,
		        Common::DebugOverlay::PipelineDepthWriteMode::Disabled,
		        true,
		        (std::max)(1U, m_projectionSampleCount)))
		{
			OutputDebugStringW(
			    L"[OpenXR] alpha debug pipeline init failed; using opaque pipeline.\n");
			m_debugAlphaPipelineState = m_debugPipelineState;
		}
		if (m_debugAlphaPipelineState.Get() == nullptr)
		{
			m_lastProjectionLayerStatus = L"alpha-pso-missing";
			m_sessionName = L"Session(unavailable): XR alpha debug pipeline missing";
			return false;
		}

		return true;
	}

	bool NullXrSystem::InitializeDebugVertexBuffer()
	{
		if (m_d3d12Device == nullptr)
		{
			m_lastProjectionLayerStatus = L"vertex-init-device-missing";
			m_sessionName = L"Session(unavailable): D3D12 device unavailable";
			return false;
		}

		Common::DebugOverlay::VertexBufferInitializationPhase failedPhase =
		    Common::DebugOverlay::VertexBufferInitializationPhase::None;
		HRESULT failedResult = S_OK;
		if (!Common::DebugOverlay::InitializeVertexBuffer(
		        m_d3d12Device.Get(),
		        MaxDebugVertices,
		        &m_debugVertexBuffer,
		        &m_debugVertexBufferView,
		        &m_debugVertexCapacity,
		        &failedPhase,
		        &failedResult))
		{
			m_lastProjectionLayerStatus = L"vertex-init-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildDebugVertexBufferInitializationErrorMessage(
			                    failedPhase,
			                    failedResult);
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
		        &m_viewStickAction))
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
		suggestBindings(
		    "/interaction_profiles/khr/simple_controller",
		    simpleControllerBindings);

		auto buildTriggerProfileBindings = [&](std::vector<XrActionSuggestedBinding>* const bindings,
		                                       const char* const leftStickPath,
		                                       const char* const rightStickPath) -> void
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
			appendBinding(
			    bindings,
			    m_selectClickAction,
			    "/user/hand/left/input/trigger/click");
			appendBinding(
			    bindings,
			    m_selectClickAction,
			    "/user/hand/right/input/trigger/click");
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
		    "/user/hand/right/input/thumbstick");
		suggestBindings(
		    "/interaction_profiles/oculus/touch_controller",
		    oculusTouchBindings);

		std::vector<XrActionSuggestedBinding> viveControllerBindings{};
		buildTriggerProfileBindings(
		    &viveControllerBindings,
		    "/user/hand/left/input/trackpad",
		    "/user/hand/right/input/trackpad");
		suggestBindings(
		    "/interaction_profiles/htc/vive_controller",
		    viveControllerBindings);

		std::vector<XrActionSuggestedBinding> valveIndexBindings{};
		buildTriggerProfileBindings(
		    &valveIndexBindings,
		    "/user/hand/left/input/thumbstick",
		    "/user/hand/right/input/thumbstick");
		suggestBindings(
		    "/interaction_profiles/valve/index_controller",
		    valveIndexBindings);

		std::vector<XrActionSuggestedBinding> microsoftMotionBindings{};
		buildTriggerProfileBindings(
		    &microsoftMotionBindings,
		    "/user/hand/left/input/thumbstick",
		    "/user/hand/right/input/thumbstick");
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
		m_viewPitchOffsetRadians = 0.0;
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
		m_viewPitchOffsetRadians = 0.0;
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
		                     XrPosef* const outPose) -> bool
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

			XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
			const XrResult locateResult = xrLocateSpace(
			    actionSpace,
			    m_referenceSpaceHandle,
			    predictedDisplayTime,
			    &spaceLocation);
			if (XR_FAILED(locateResult))
			{
				return false;
			}

			const XrSpaceLocationFlags requiredFlags =
			    XR_SPACE_LOCATION_POSITION_VALID_BIT |
			    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
			if ((spaceLocation.locationFlags & requiredFlags) != requiredFlags)
			{
				return false;
			}

			*outPose = spaceLocation.pose;
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
		                              const XrSpace aimActionSpace,
		                              const XrSpace gripActionSpace) -> HandPoseState
		{
			HandPoseState handPoseState{};
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
				return handPoseState;
			}

			const XrPosef sourceOriginPose = hasGripPose ? gripPose : aimPose;
			const XrPosef sourceDirectionPose =
			    hasAimPose ? aimPose : sourceOriginPose;
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
				up = Physics::Vector3{ 0.0, 1.0, 0.0 };
			}

			handPoseState.isValid = true;
			handPoseState.position = Physics::Vector3{
				static_cast<double>(sourceOriginPose.position.x),
				static_cast<double>(sourceOriginPose.position.y),
				static_cast<double>(sourceOriginPose.position.z)
			};
			handPoseState.forward = normalizedForward;
			handPoseState.up = up;
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
		    m_rightAimActionSpace,
		    m_rightGripActionSpace);
		const HandPoseState leftHandPoseState = queryHandPoseState(
		    m_leftHandPath,
		    m_leftAimActionSpace,
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
		const HandRayState* mainPanelHandState = &rightHandRayState;
		const HandRayState* laserHandStateWhenMainPanelVisible = &leftHandRayState;
		if (isBatMode)
		{
			if (m_uiTelemetry.batUseRightHand)
			{
				mainPanelHandState = &leftHandRayState;
				laserHandStateWhenMainPanelVisible = &rightHandRayState;
			}
			else
			{
				mainPanelHandState = &rightHandRayState;
				laserHandStateWhenMainPanelVisible = &leftHandRayState;
			}
		}
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
		if (m_showMainControlPanel &&
		    laserHandStateWhenMainPanelVisible->isValid)
		{
			handRayState = *laserHandStateWhenMainPanelVisible;
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
		if (!handRayState.isValid)
		{
			m_wasResultPanelSelectPressed = false;
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

		const bool isPressed = handRayState.isPressed;
		const bool isRisingEdge = isPressed && !m_wasResultPanelSelectPressed;
		const bool isFallingEdge = !isPressed && m_wasResultPanelSelectPressed;
		m_wasResultPanelSelectPressed = isPressed;
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
					queueCommand(UiCommandType::ToggleTrajectoryVisibility);
					return;
				case 1:
					queueCommand(UiCommandType::ToggleStrikeZoneVisibility);
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
				case 10:
					queueCommand(UiCommandType::RecalibrateStrikeZone);
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
		m_xrCommandList.Reset();
		m_xrCommandAllocator.Reset();
		m_debugVertexBuffer.Reset();
		m_debugPipelineState.Reset();
		m_debugAlphaPipelineState.Reset();
		m_debugRootSignature.Reset();
		m_fxaaPipelineState.Reset();
		m_fxaaRootSignature.Reset();
		m_xrFenceValue = 1;
		m_rtvDescriptorSize = 0;
		m_dsvDescriptorSize = 0;
		m_srvDescriptorSize = 0;
		m_debugVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
		m_debugVertexCapacity = 0;
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
		else if ((sessionState == XR_SESSION_STATE_EXITING) ||
		         (sessionState == XR_SESSION_STATE_LOSS_PENDING))
		{
			m_isSessionRunning = false;
			m_lastPredictedDisplayTime = 0;
			m_lastLocomotionSampleTime = 0;
			m_lastFrameSummary = L"frame(stopped)";
			m_xrPostProcessStatus = L"none";
		}

		UpdateSessionName();
	}

	void NullXrSystem::TryRenderFrame()
	{
		if (!m_isSessionRunning || (m_sessionHandle == XR_NULL_HANDLE) ||
		    (m_referenceSpaceHandle == XR_NULL_HANDLE))
		{
			m_leftEyeViewState.isValid = false;
			return;
		}

		if (m_projectionSwapchains.empty() || (m_d3d12Queue == nullptr))
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

		XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
		frameEndInfo.displayTime = frameState.predictedDisplayTime;
		frameEndInfo.environmentBlendMode = m_environmentBlendMode;
		frameEndInfo.layerCount = 0;
		frameEndInfo.layers = nullptr;
		const XrTime previousPredictedDisplayTime = m_lastPredictedDisplayTime;

		const bool hasProjectionLayer =
		    RenderProjectionLayer(frameState.predictedDisplayTime);
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
		    frameState.shouldRender,
		    hasProjectionLayer,
		    frameState.predictedDisplayTime,
		    previousPredictedDisplayTime,
		    m_lastProjectionLayerStatus,
		    m_resultPanelLaserStatus,
		    m_xrPostProcessStatus);
		m_lastPredictedDisplayTime = frameState.predictedDisplayTime;
		UpdateSessionName();
	}

	bool NullXrSystem::RenderProjectionLayer(const XrTime predictedDisplayTime)
	{
		m_lastProjectionLayerStatus = L"locating";
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

		Physics::Vector3 lookPitchPivot{};
		const Physics::Vector3* lookPitchPivotPtr = nullptr;
		XrQuaternionf lookPitchOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
		const XrQuaternionf* lookPitchOrientationPtr = nullptr;
		if ((std::abs(m_viewPitchOffsetRadians) > 1e-9) &&
		    TryBuildLookPitchPivot(
		        std::span<const XrView>{ m_views },
		        0.0,
		        Physics::Vector3{},
		        nullptr,
		        &lookPitchPivot))
		{
			lookPitchPivotPtr = &lookPitchPivot;
		}
		if (TryBuildLookPitchOrientation(
		        std::span<const XrView>{ m_views },
		        0.0,
		        nullptr,
		        m_viewPitchOffsetRadians,
		        &lookPitchOrientation))
		{
			lookPitchOrientationPtr = &lookPitchOrientation;
		}

		XrView preUiLeftEyeView = m_views.front();
		preUiLeftEyeView.pose = ApplyLocomotionToPose(
		    preUiLeftEyeView.pose,
		    0.0,
		    m_viewPitchOffsetRadians,
		    Physics::Vector3{},
		    lookPitchPivotPtr,
		    lookPitchOrientationPtr);
		m_leftEyeViewState = BuildProjectionStateFromView(preUiLeftEyeView);
		UpdateUiPanelInteraction(predictedDisplayTime);
		XrView leftEyeView = m_views.front();
		leftEyeView.pose = ApplyLocomotionToPose(
		    leftEyeView.pose,
		    0.0,
		    m_viewPitchOffsetRadians,
		    Physics::Vector3{},
		    lookPitchPivotPtr,
		    lookPitchOrientationPtr);
		m_leftEyeViewState = BuildProjectionStateFromView(leftEyeView);
		m_xrPostProcessStatus = m_isFxaaAvailable ? L"fxaa" : L"direct";

		for (uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
		{
			SwapchainContext& swapchainContext = m_projectionSwapchains[viewIndex];
			XrView adjustedView = m_views[viewIndex];
			adjustedView.pose = ApplyLocomotionToPose(
			    adjustedView.pose,
			    0.0,
			    m_viewPitchOffsetRadians,
			    Physics::Vector3{},
			    lookPitchPivotPtr,
			    lookPitchOrientationPtr);
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
		const auto now = std::chrono::steady_clock::now();
		Common::TrajectoryTrailState trailState{
			m_lastBallFlightTimeSeconds,
			m_lastTrajectoryAdvanceTime,
			m_trajectoryFadeScale
		};
		Common::UpdateTrajectoryTrail(
		    ballState,
		    &m_ballTrajectory,
		    &trailState,
		    now);
		m_lastBallFlightTimeSeconds = trailState.lastFlightTimeSeconds;
		m_lastTrajectoryAdvanceTime = trailState.lastAdvanceTime;
		m_trajectoryFadeScale = trailState.fadeScale;
	}

	bool NullXrSystem::ClearSwapchainImage(
	    const SwapchainContext& swapchainContext,
	    const uint32_t imageIndex,
	    const float* clearColor,
	    const XrView& view)
	{
		if ((m_xrCommandAllocator == nullptr) ||
		    (m_xrCommandList == nullptr) ||
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

		const HRESULT allocatorResetResult = m_xrCommandAllocator->Reset();
		if (FAILED(allocatorResetResult))
		{
			m_lastProjectionLayerStatus = L"allocator-reset-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12CommandAllocator::Reset", allocatorResetResult);
			return false;
		}

		const HRESULT commandListResetResult = m_xrCommandList->Reset(m_xrCommandAllocator.Get(), nullptr);
		if (FAILED(commandListResetResult))
		{
			m_lastProjectionLayerStatus = L"command-reset-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12GraphicsCommandList::Reset", commandListResetResult);
			return false;
		}
		auto closeCommandListOnFailure = [&]() noexcept
		{
			const HRESULT ignored = m_xrCommandList->Close();
			(void)ignored;
		};

		D3D12_RESOURCE_BARRIER toDrawBarriers[2]{};
		UINT toDrawBarrierCount = 0;

		D3D12_RESOURCE_BARRIER& renderTargetToDrawBarrier =
		    toDrawBarriers[toDrawBarrierCount++];
		renderTargetToDrawBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		renderTargetToDrawBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		renderTargetToDrawBarrier.Transition.pResource = renderTargetTexture;
		renderTargetToDrawBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		renderTargetToDrawBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		renderTargetToDrawBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

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
		m_xrCommandList->ResourceBarrier(toDrawBarrierCount, toDrawBarriers);

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
		m_xrCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		m_xrCommandList->ClearDepthStencilView(
		    dsvHandle,
		    D3D12_CLEAR_FLAG_DEPTH,
		    1.0F,
		    0,
		    0,
		    nullptr);
		m_xrCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

#if defined(BASEBALLSIMVR_DISABLE_WORLD_DRAW)
		(void)view;
		m_lastProjectionLayerStatus = L"world-draw-disabled";
#else
		Microsoft::WRL::ComPtr<ID3D12RootSignature> fallbackRootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> fallbackOpaquePipelineState{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> fallbackAlphaPipelineState{};
		Microsoft::WRL::ComPtr<ID3D12Resource> fallbackVertexBuffer{};
		D3D12_VERTEX_BUFFER_VIEW activeVertexBufferView = m_debugVertexBufferView;
		UINT activeVertexCapacity = m_debugVertexCapacity;
		ID3D12RootSignature* activeRootSignature = m_debugRootSignature.Get();
		ID3D12PipelineState* activeOpaquePipelineState = m_debugPipelineState.Get();
		ID3D12PipelineState* activeAlphaPipelineState = m_debugAlphaPipelineState.Get();
		ID3D12Resource* activeVertexBuffer = m_debugVertexBuffer.Get();
		auto refreshMemberDebugResources = [&]() -> void
		{
			const bool pipelineMissing =
			    (m_debugRootSignature.Get() == nullptr) ||
			    (m_debugPipelineState.Get() == nullptr) ||
			    (m_debugAlphaPipelineState.Get() == nullptr);
			if (pipelineMissing)
			{
				if (!InitializeDebugPipeline())
				{
					m_lastProjectionLayerStatus = L"debug-pipeline-reinit-failed";
				}
			}

			const bool vertexMissing =
			    (m_debugVertexBuffer.Get() == nullptr) ||
			    (m_debugVertexCapacity == 0U) ||
			    (m_debugVertexBufferView.SizeInBytes == 0U) ||
			    (m_debugVertexBufferView.StrideInBytes == 0U);
			if (vertexMissing)
			{
				if (!InitializeDebugVertexBuffer())
				{
					m_lastProjectionLayerStatus = L"debug-vertex-reinit-failed";
				}
			}

			activeVertexBufferView = m_debugVertexBufferView;
			activeVertexCapacity = m_debugVertexCapacity;
			activeRootSignature = m_debugRootSignature.Get();
			activeOpaquePipelineState = m_debugPipelineState.Get();
			activeAlphaPipelineState = m_debugAlphaPipelineState.Get();
			activeVertexBuffer = m_debugVertexBuffer.Get();
		};
		auto repairActiveDebugResources = [&]() -> void
		{
			if ((activeAlphaPipelineState == nullptr) &&
			    (activeOpaquePipelineState != nullptr))
			{
				activeAlphaPipelineState = activeOpaquePipelineState;
			}

			if ((activeVertexBuffer != nullptr) && (activeVertexCapacity == 0U))
			{
				if ((activeVertexBufferView.SizeInBytes != 0U) &&
				    (activeVertexBufferView.StrideInBytes ==
				     sizeof(Common::DebugOverlay::Vertex)))
				{
					activeVertexCapacity =
					    activeVertexBufferView.SizeInBytes /
					    activeVertexBufferView.StrideInBytes;
				}
				else
				{
					activeVertexCapacity = MaxDebugVertices;
				}
			}

			if ((activeVertexBuffer != nullptr) &&
			    (activeVertexCapacity != 0U) &&
			    ((activeVertexBufferView.SizeInBytes == 0U) ||
			     (activeVertexBufferView.StrideInBytes == 0U)))
			{
				activeVertexBufferView.BufferLocation =
				    activeVertexBuffer->GetGPUVirtualAddress();
				activeVertexBufferView.StrideInBytes =
				    sizeof(Common::DebugOverlay::Vertex);
				activeVertexBufferView.SizeInBytes =
				    static_cast<UINT>(activeVertexCapacity *
				                      sizeof(Common::DebugOverlay::Vertex));
			}
		};
		auto hasActiveDebugResources = [&]() -> bool
		{
			return (activeRootSignature != nullptr) &&
			       (activeOpaquePipelineState != nullptr) &&
			       (activeAlphaPipelineState != nullptr) &&
			       (activeVertexBuffer != nullptr) &&
			       (activeVertexCapacity != 0U) &&
			       (activeVertexBufferView.SizeInBytes != 0U) &&
			       (activeVertexBufferView.StrideInBytes != 0U);
		};
		auto buildDebugResourceMissingReason = [&]() -> std::wstring
		{
			if (activeRootSignature == nullptr)
			{
				return L"root-null";
			}
			if (activeOpaquePipelineState == nullptr)
			{
				return L"opaque-null";
			}
			if (activeAlphaPipelineState == nullptr)
			{
				return L"alpha-null";
			}
			if (activeVertexBuffer == nullptr)
			{
				return L"vb-null";
			}
			if (activeVertexCapacity == 0U)
			{
				return L"capacity-zero";
			}
			if (activeVertexBufferView.SizeInBytes == 0U)
			{
				return L"vb-size-zero";
			}
			if (activeVertexBufferView.StrideInBytes == 0U)
			{
				return L"vb-stride-zero";
			}
			return L"unknown";
		};
		refreshMemberDebugResources();
		repairActiveDebugResources();
		if (!hasActiveDebugResources())
		{
			bool fallbackUsable = true;
			Common::DebugOverlay::PipelineInitializationPhase failedPhase =
			    Common::DebugOverlay::PipelineInitializationPhase::None;
			HRESULT failedResult = S_OK;
			if (!Common::DebugOverlay::InitializePipeline(
			        m_d3d12Device.Get(),
			        ResolveRtvFormat(m_colorSwapchainFormat),
			        DepthBufferFormat,
			        &fallbackRootSignature,
			        &fallbackOpaquePipelineState,
			        &failedPhase,
			        &failedResult,
			        Common::DebugOverlay::PipelineBlendMode::Opaque,
			        Common::DebugOverlay::PipelineDepthWriteMode::Enabled,
			        false,
			        (std::max)(1U, m_projectionSampleCount)))
			{
				m_lastProjectionLayerStatus =
				    L"pipeline-fallback-failed-" +
				    BuildPipelineInitPhaseToken(failedPhase) +
				    L"-" +
				    BuildHresultHex(failedResult);
				m_sessionName = L"Session(unavailable): " +
				                BuildDebugPipelineInitializationErrorMessage(
				                    failedPhase,
				                    failedResult);
				activeRootSignature = nullptr;
				activeOpaquePipelineState = nullptr;
				activeAlphaPipelineState = nullptr;
				activeVertexBuffer = nullptr;
				activeVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
				activeVertexCapacity = 0U;
				fallbackUsable = false;
			}
			if (fallbackUsable &&
			    ((fallbackRootSignature.Get() == nullptr) ||
			     (fallbackOpaquePipelineState.Get() == nullptr)))
			{
				m_lastProjectionLayerStatus = L"pipeline-fallback-empty";
				m_sessionName = L"Session(unavailable): XR fallback pipeline resources missing";
				activeRootSignature = nullptr;
				activeOpaquePipelineState = nullptr;
				activeAlphaPipelineState = nullptr;
				activeVertexBuffer = nullptr;
				activeVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
				activeVertexCapacity = 0U;
				fallbackUsable = false;
			}

			if (fallbackUsable)
			{
				m_debugRootSignature = fallbackRootSignature;
				m_debugPipelineState = fallbackOpaquePipelineState;
				if (m_debugAlphaPipelineState.Get() == nullptr)
				{
					m_debugAlphaPipelineState = m_debugPipelineState;
				}

				failedPhase = Common::DebugOverlay::PipelineInitializationPhase::None;
				failedResult = S_OK;
				if (!Common::DebugOverlay::InitializePipeline(
				        m_d3d12Device.Get(),
				        ResolveRtvFormat(m_colorSwapchainFormat),
				        DepthBufferFormat,
				        &fallbackRootSignature,
				        &fallbackAlphaPipelineState,
				        &failedPhase,
				        &failedResult,
				        Common::DebugOverlay::PipelineBlendMode::AlphaBlend,
				        Common::DebugOverlay::PipelineDepthWriteMode::Disabled,
				        true,
				        (std::max)(1U, m_projectionSampleCount)))
				{
					OutputDebugStringW(
					    L"[OpenXR] alpha fallback pipeline init failed; using opaque pipeline.\n");
					fallbackAlphaPipelineState = fallbackOpaquePipelineState;
				}

				if (fallbackAlphaPipelineState.Get() == nullptr)
				{
					fallbackAlphaPipelineState = fallbackOpaquePipelineState;
				}
				m_debugAlphaPipelineState = fallbackAlphaPipelineState;
			}

			if (fallbackUsable)
			{
				Common::DebugOverlay::VertexBufferInitializationPhase vertexFailedPhase =
				    Common::DebugOverlay::VertexBufferInitializationPhase::None;
				HRESULT vertexFailedResult = S_OK;
				if (!Common::DebugOverlay::InitializeVertexBuffer(
				        m_d3d12Device.Get(),
				        MaxDebugVertices,
				        &fallbackVertexBuffer,
				        &activeVertexBufferView,
				        &activeVertexCapacity,
				        &vertexFailedPhase,
				        &vertexFailedResult))
				{
					m_lastProjectionLayerStatus =
					    L"vertex-fallback-failed-" +
					    BuildVertexInitPhaseToken(vertexFailedPhase) +
					    L"-" +
					    BuildHresultHex(vertexFailedResult);
					m_sessionName = L"Session(unavailable): " +
					                BuildDebugVertexBufferInitializationErrorMessage(
					                    vertexFailedPhase,
					                    vertexFailedResult);
					activeRootSignature = nullptr;
					activeOpaquePipelineState = nullptr;
					activeAlphaPipelineState = nullptr;
					activeVertexBuffer = nullptr;
					activeVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
					activeVertexCapacity = 0U;
					fallbackUsable = false;
				}
				else
				{
					m_debugVertexBuffer = fallbackVertexBuffer;
					m_debugVertexBufferView = activeVertexBufferView;
					m_debugVertexCapacity = activeVertexCapacity;
				}
			}

			if (fallbackUsable)
			{
				refreshMemberDebugResources();
				repairActiveDebugResources();
				activeRootSignature = fallbackRootSignature.Get();
				activeOpaquePipelineState = fallbackOpaquePipelineState.Get();
				activeAlphaPipelineState = fallbackAlphaPipelineState.Get();
				activeVertexBuffer = fallbackVertexBuffer.Get();
				if ((activeAlphaPipelineState == nullptr) &&
				    (activeOpaquePipelineState != nullptr))
				{
					activeAlphaPipelineState = activeOpaquePipelineState;
				}
				if ((activeRootSignature == nullptr) ||
				    (activeOpaquePipelineState == nullptr) ||
				    (activeAlphaPipelineState == nullptr))
				{
					m_lastProjectionLayerStatus = L"fallback-assign-null";
					m_sessionName = L"Session(unavailable): XR fallback pipeline assignment failed";
					activeRootSignature = nullptr;
					activeOpaquePipelineState = nullptr;
					activeAlphaPipelineState = nullptr;
					activeVertexBuffer = nullptr;
					activeVertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
					activeVertexCapacity = 0U;
					fallbackUsable = false;
				}
			}

			if (fallbackUsable)
			{
				m_lastProjectionLayerStatus = L"debug-fallback-ok";
			}
			else
			{
				const bool hasDetailedFailure =
				    (m_lastProjectionLayerStatus.rfind(L"pipeline-fallback-failed", 0) == 0) ||
				    (m_lastProjectionLayerStatus.rfind(L"vertex-fallback-failed", 0) == 0) ||
				    (m_lastProjectionLayerStatus.rfind(L"fallback-assign-null", 0) == 0);
				if (!hasDetailedFailure)
				{
					m_lastProjectionLayerStatus =
					    L"debug-fallback-missing-" + buildDebugResourceMissingReason();
				}
			}
		}
		else
		{
			m_lastProjectionLayerStatus = L"debug-ready";
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
		static const std::vector<Physics::Vector3> EmptyTrajectory{};
		const std::vector<Physics::Vector3>& trajectoryForOverlay =
		    m_overlayVisibility.showTrajectory
		        ? m_ballTrajectory
		        : EmptyTrajectory;

		const bool debugDrawUsable = hasActiveDebugResources();
		if (!debugDrawUsable)
		{
			const std::wstring fallbackStatus = m_lastProjectionLayerStatus;
			const std::wstring missingReason = buildDebugResourceMissingReason();
			bool drewDynamicOverlay = false;
			auto drawRect = [&](LONG left,
			                    LONG top,
			                    LONG right,
			                    LONG bottom,
			                    const float* const color) -> void
			{
				const LONG clampedLeft = (std::max)(0L, left);
				const LONG clampedTop = (std::max)(0L, top);
				const LONG clampedRight = (std::min)(width, right);
				const LONG clampedBottom = (std::min)(height, bottom);
				if ((clampedLeft >= clampedRight) || (clampedTop >= clampedBottom))
				{
					return;
				}

				const D3D12_RECT rect{
					clampedLeft,
					clampedTop,
					clampedRight,
					clampedBottom
				};
				m_xrCommandList->ClearRenderTargetView(rtvHandle, color, 1, &rect);
			};

			auto drawFallbackMarker = [&](const ScreenPoint& point,
			                              const LONG halfSize,
			                              const float* const color) -> void
			{
				const LONG centerX = static_cast<LONG>(std::llround(point.x));
				const LONG centerY = static_cast<LONG>(std::llround(point.y));
				drawRect(
				    centerX - halfSize,
				    centerY - halfSize,
				    centerX + halfSize + 1,
				    centerY + halfSize + 1,
				    color);
				drewDynamicOverlay = true;
			};
			auto drawFallbackLine = [&](const ScreenPoint& startPoint,
			                            const ScreenPoint& endPoint,
			                            const LONG halfSize,
			                            const float* const color) -> void
			{
				const double dx = endPoint.x - startPoint.x;
				const double dy = endPoint.y - startPoint.y;
				const double length = std::sqrt((dx * dx) + (dy * dy));
				const int stepCount = (std::max)(1,
				                                 static_cast<int>(std::ceil(length / 8.0)));
				for (int stepIndex = 0; stepIndex <= stepCount; ++stepIndex)
				{
					const double blend = static_cast<double>(stepIndex) /
					                     static_cast<double>(stepCount);
					const ScreenPoint point{
						startPoint.x + (dx * blend),
						startPoint.y + (dy * blend),
						startPoint.depth + static_cast<float>((endPoint.depth - startPoint.depth) * blend)
					};
					drawFallbackMarker(point, halfSize, color);
				}
			};

			const Common::ViewProjectionState projectionState =
			    BuildProjectionStateFromView(view);
			Common::EmitProjectedDebugWorldOverlay(
			    projectionState,
			    width,
			    height,
			    trajectoryForOverlay,
			    m_latestBallState,
			    [&](const ScreenPoint& screenPoint, const LONG halfSize, const float* color) -> void
			    {
				    drawFallbackMarker(screenPoint, halfSize, color);
			    },
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    drawFallbackLine(startPoint, endPoint, halfSize, color);
			    },
			    m_overlayVisibility.showTrajectory,
			    m_overlayVisibility.showFloorMesh,
			    m_overlayVisibility.showStrikeZone);
			Common::EmitProjectedZonePassCircleOverlay(
			    projectionState,
			    width,
			    height,
			    m_duelZonePassMarker,
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    drawFallbackLine(startPoint, endPoint, halfSize, color);
			    });
			Common::EmitProjectedBatCylinderOverlay(
			    projectionState,
			    width,
			    height,
			    m_batOverlayState,
			    [&](const ScreenPoint& startPoint,
			        const ScreenPoint& endPoint,
			        const LONG halfSize,
			        const float* color) -> void
			    {
				    drawFallbackLine(startPoint, endPoint, halfSize, color);
			    });

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
					drawFallbackLine(
					    laserStart,
					    laserEnd,
					    Common::DebugWorldStyle::UiLaserHalfSizePixels,
					    Common::DebugWorldStyle::UiLaserColor.data());
					drawFallbackMarker(
					    laserEnd,
					    Common::DebugWorldStyle::PlateMarkerHalfSizePixels + 1,
					    Common::DebugWorldStyle::UiLaserHitColor.data());
				}
			}

			if (!drewDynamicOverlay)
			{
				const LONG centerX = width / 2;
				const LONG centerY = height / 2;
				const LONG armLength = (std::max)(8L, (std::min)(width, height) / 10L);
				const LONG lineHalfThickness = 2L;
				const LONG markerHalfSize = 8L;
				drawRect(
				    centerX - armLength,
				    centerY - lineHalfThickness,
				    centerX + armLength + 1,
				    centerY + lineHalfThickness + 1,
				    Common::DebugWorldStyle::ZoneColor.data());
				drawRect(
				    centerX - lineHalfThickness,
				    centerY - armLength,
				    centerX + lineHalfThickness + 1,
				    centerY + armLength + 1,
				    Common::DebugWorldStyle::ZoneColor.data());
				drawRect(
				    centerX - markerHalfSize,
				    centerY - markerHalfSize,
				    centerX + markerHalfSize + 1,
				    centerY + markerHalfSize + 1,
				    Common::DebugWorldStyle::PlateColor.data());
			}

			if (drewDynamicOverlay)
			{
				m_lastProjectionLayerStatus = L"rect-world-fallback";
			}
			else if ((fallbackStatus.rfind(L"pipeline-fallback", 0) == 0) ||
			         (fallbackStatus.rfind(L"vertex-fallback", 0) == 0) ||
			         (fallbackStatus.rfind(L"debug-fallback-missing", 0) == 0) ||
			         (fallbackStatus.rfind(L"fallback-assign", 0) == 0))
			{
				m_lastProjectionLayerStatus = L"rect-" + fallbackStatus;
			}
			else
			{
				m_lastProjectionLayerStatus = L"rect-fallback-" + missingReason;
			}
		}

		if (debugDrawUsable)
		{
			const Common::ViewProjectionState projectionState = BuildProjectionStateFromView(view);
			std::vector<Common::DebugOverlay::Vertex> opaqueVertices{};
			std::vector<Common::DebugOverlay::Vertex> alphaVertices{};
			opaqueVertices.reserve(8192);
			alphaVertices.reserve(1024);
			const std::size_t vertexCapacity = static_cast<std::size_t>(activeVertexCapacity);
			bool hasVisibleCoverage = false;

			auto hasCapacity = [&](const std::size_t additionalVertices) -> bool
			{
				return ((opaqueVertices.size() + alphaVertices.size() + additionalVertices) <=
				        vertexCapacity);
			};

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
				if (!hasCapacity(6))
				{
					return;
				}
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
				if (!hasCapacity(6))
				{
					return;
				}
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
				if (!hasCapacity(3))
				{
					return;
				}
				auto* const vertices = selectVertexBucket(color);

				Common::DebugOverlay::AppendScreenTriangle(
				    vertices,
				    p0,
				    p1,
				    p2,
				    color,
				    width,
				    height);
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

			Common::EmitProjectedDebugWorldOverlay(
			    projectionState,
			    width,
			    height,
			    trajectoryForOverlay,
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
			    m_overlayVisibility.showStrikeZone,
			    false);
			Common::EmitProjectedZonePassCircleOverlay(
			    projectionState,
			    width,
			    height,
			    m_duelZonePassMarker,
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
			Common::EmitProjectedSolidBatCylinderOverlay(
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

			Common::EmitProjectedBallAndTrajectoryMeshOverlay(
			    projectionState,
			    width,
			    height,
			    trajectoryForOverlay,
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
			        1.5F),
			    std::clamp(
			        1.0F - m_trajectoryFadeScale,
			        0.0F,
			        1.0F));

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
					    });
					const std::array<std::wstring, 6> mainPanelLabels{
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
						    : L"\u6253\u7403\u6307\u6a19 \u958b\u304f"
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
					    });
				}

				if (m_showPitchControlPanel)
				{
					static constexpr std::array<float, 4> TransparentRowColor{
						0.0F,
						0.0F,
						0.0F,
						0.0F
					};
					static constexpr std::array<std::array<float, 4>, 9> PitchPanelRowColors{
						TransparentRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelOverlayRowColor,
						Common::DebugWorldStyle::MainControlPanelPresetRowColor,
						TransparentRowColor,
						Common::DebugWorldStyle::MainControlPanelEnvironmentRowColor
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
					    });
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
					    });
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
						    });
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
						    });
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
							L"-",
							L">",
							L"\u8a2d\u5b9a",
							L""
						};
						const int pitchProfileCount = std::clamp(
						    m_uiTelemetry.pitchProfileCount,
						    0,
						    static_cast<int>(m_uiTelemetry.pitchProfileNames.size()));
						if (pitchProfileCount > 0)
						{
							int activeProfileIndex = m_uiTelemetry.activePitchProfileIndex;
							if ((activeProfileIndex < 0) ||
							    (activeProfileIndex >= pitchProfileCount))
							{
								activeProfileIndex = 0;
							}
							pitchPanelLabels[static_cast<std::size_t>(
							    PitchTypeNameCellIndex)] =
							    m_uiTelemetry.pitchProfileNames[static_cast<std::size_t>(
							        activeProfileIndex)];
						}
						else if (!m_uiTelemetry.presetName.empty())
						{
							pitchPanelLabels[static_cast<std::size_t>(
							    PitchTypeNameCellIndex)] = m_uiTelemetry.presetName;
						}
						std::wstring settingsFileLabel =
						    ExtractPitchSettingsDisplayName(
						        m_uiTelemetry.pitchSettingsFileName);
						if (settingsFileLabel.empty() ||
						    (m_uiTelemetry.pitchSettingsFileCount <= 0))
						{
							settingsFileLabel = L"(none)";
						}
						if (settingsFileLabel.size() > 20)
						{
							settingsFileLabel =
							    L"..." +
							    settingsFileLabel.substr(settingsFileLabel.size() - 17);
						}
						pitchPanelLabels[static_cast<std::size_t>(
						    PitchSettingsNameCellIndex)] = settingsFileLabel;
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
						    });

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
							    });

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
								    });
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
							    });

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
							    });
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
							    });
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
							    });
						}
					}
				}

				if (m_showMetricsPanel || m_showBattingMetricsPanel)
				{
					const DerivedFlightMetrics derivedMetrics =
					    BuildDerivedFlightMetrics(
					        m_ballTrajectory,
					        m_uiTelemetry.pitchDistanceMeters);
					const double liveBallSpeedMetersPerSecond =
					    Physics::Length(m_latestBallState.velocity);
					const double pitchMetricsSpeedMetersPerSecond =
					    (m_uiTelemetry.pitchMetricsSpeedMetersPerSecond > 1e-6)
					        ? m_uiTelemetry.pitchMetricsSpeedMetersPerSecond
					        : liveBallSpeedMetersPerSecond;
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

					const std::wstring trajectoryToggleLabel =
					    m_overlayVisibility.showTrajectory
					        ? L"\u8ecc\u8de1 \u96a0"
					        : L"\u8ecc\u8de1 \u8868";
					const std::wstring zoneToggleLabel =
					    m_overlayVisibility.showStrikeZone
					        ? L"\u30be\u30fc\u30f3 \u96a0"
					        : L"\u30be\u30fc\u30f3 \u8868";
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
					const std::array<std::wstring, 4> pitchingMetricRows{
						pitchTypeSpeedLine.str(),
						spinAxisLine.str(),
						pitchBreakLine,
						pitchZoneLine
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
						    });
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
						    });
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
						    });

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
										    });
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
										    });
									}
								}

								const std::array<std::wstring, MetricsControlButtonCount> metricsButtonLabels{
									trajectoryToggleLabel,
									zoneToggleLabel,
									moundToggleLabel,
									duelPreviewToggleLabel,
									batHandToggleLabel,
									batAngleDownLabel,
									batAngleUpLabel,
									batYawDownLabel,
									batYawUpLabel,
									L"\u4f4d\u7f6eR",
									L"ZoneR"
								};
								for (std::size_t buttonIndex = 0;
								     buttonIndex < metricsControlButtonRects.size();
								     ++buttonIndex)
								{
									const bool isActiveButton =
									    ((buttonIndex == 0U) &&
									     m_overlayVisibility.showTrajectory) ||
									    ((buttonIndex == 1U) &&
									     m_overlayVisibility.showStrikeZone) ||
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
									    });
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
									    });
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
						    });
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
						    });
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
						    });
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
										    });
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
										    });
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
					    Common::DebugWorldStyle::PlateMarkerHalfSizePixels + 1,
					    Common::DebugWorldStyle::UiLaserHitColor.data());
				}
			}

			if (!hasVisibleCoverage)
			{
				for (const Physics::Vector3& trailPoint : trajectoryForOverlay)
				{
					const ScreenPoint screenPoint =
					    Common::ProjectLegacyWorldToScreen(trailPoint, width, height);
					appendMarker(
					    screenPoint,
					    Common::DebugWorldStyle::TrailHalfSizePixels,
					    Common::DebugWorldStyle::TrailColor.data());
				}

				const ScreenPoint ballScreenPoint =
				    Common::ProjectLegacyWorldToScreen(
				        m_latestBallState.position,
				        width,
				        height);
				appendMarker(
				    ballScreenPoint,
				    Common::DebugWorldStyle::BallHalfSizePixels,
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

			const UINT opaqueVertexCount = static_cast<UINT>(opaqueVertices.size());
			const UINT alphaVertexCount = static_cast<UINT>(alphaVertices.size());
			if ((opaqueVertexCount > 0U) || (alphaVertexCount > 0U))
			{
				bool overlayDrawReady = true;
				std::vector<Common::DebugOverlay::Vertex> combinedVertices{};
				combinedVertices.reserve(
				    static_cast<std::size_t>(opaqueVertexCount) +
				    static_cast<std::size_t>(alphaVertexCount));
				combinedVertices.insert(
				    combinedVertices.end(),
				    opaqueVertices.begin(),
				    opaqueVertices.end());
				combinedVertices.insert(
				    combinedVertices.end(),
				    alphaVertices.begin(),
				    alphaVertices.end());

				Common::DebugOverlay::VertexUploadPhase failedPhase =
				    Common::DebugOverlay::VertexUploadPhase::None;
				HRESULT failedResult = S_OK;
				if (!Common::DebugOverlay::UploadVertices(
				        activeVertexBuffer,
				        combinedVertices,
				        &failedPhase,
				        &failedResult))
				{
					overlayDrawReady = false;
					m_lastProjectionLayerStatus = L"upload-failed";
					m_sessionName = L"Session(unavailable): " +
					                BuildDebugVertexUploadErrorMessage(
					                    failedPhase,
					                    failedResult);
				}

				Common::DebugOverlay::DrawCommandValidationPhase failedDrawPhase =
				    Common::DebugOverlay::DrawCommandValidationPhase::None;
				const bool opaqueDrawArgsValid =
				    (m_xrCommandList.Get() != nullptr) &&
				    (width > 0) &&
				    (height > 0) &&
				    (activeRootSignature != nullptr) &&
				    (activeOpaquePipelineState != nullptr) &&
				    (activeVertexBufferView.SizeInBytes != 0U) &&
				    (activeVertexBufferView.StrideInBytes != 0U) &&
				    (opaqueVertexCount > 0U);
				if (overlayDrawReady &&
				    (opaqueVertexCount > 0U) &&
				    (!opaqueDrawArgsValid ||
				     !Common::DebugOverlay::IssueDrawCommands(
				         m_xrCommandList.Get(),
				         width,
				         height,
				         activeRootSignature,
				         activeOpaquePipelineState,
				         activeVertexBufferView,
				         opaqueVertexCount,
				         0U,
				         &failedDrawPhase)))
				{
					if (!opaqueDrawArgsValid)
					{
						if (activeRootSignature == nullptr)
						{
							m_lastProjectionLayerStatus = L"opaque-root-null";
						}
						else if (activeOpaquePipelineState == nullptr)
						{
							m_lastProjectionLayerStatus = L"opaque-pso-null";
						}
						else if ((activeVertexBufferView.SizeInBytes == 0U) ||
						         (activeVertexBufferView.StrideInBytes == 0U))
						{
							m_lastProjectionLayerStatus = L"opaque-vbview-null";
						}
						else
						{
							m_lastProjectionLayerStatus = L"opaque-args-invalid";
						}
					}
					else if (failedDrawPhase == Common::DebugOverlay::DrawCommandValidationPhase::VertexRange)
					{
						m_lastProjectionLayerStatus = L"opaque-draw-range";
					}
					else
					{
						m_lastProjectionLayerStatus = L"opaque-draw-args";
					}
					m_sessionName = L"Session(unavailable): XR debug draw arguments invalid";
					overlayDrawReady = false;
				}

				const bool alphaDrawArgsValid =
				    (m_xrCommandList.Get() != nullptr) &&
				    (width > 0) &&
				    (height > 0) &&
				    (activeRootSignature != nullptr) &&
				    (activeAlphaPipelineState != nullptr) &&
				    (activeVertexBufferView.SizeInBytes != 0U) &&
				    (activeVertexBufferView.StrideInBytes != 0U) &&
				    (alphaVertexCount > 0U);
				if (overlayDrawReady &&
				    (alphaVertexCount > 0U) &&
				    (!alphaDrawArgsValid ||
				     !Common::DebugOverlay::IssueDrawCommands(
				         m_xrCommandList.Get(),
				         width,
				         height,
				         activeRootSignature,
				         activeAlphaPipelineState,
				         activeVertexBufferView,
				         alphaVertexCount,
				         opaqueVertexCount,
				         &failedDrawPhase)))
				{
					if (!alphaDrawArgsValid)
					{
						if (activeRootSignature == nullptr)
						{
							m_lastProjectionLayerStatus = L"alpha-root-null";
						}
						else if (activeAlphaPipelineState == nullptr)
						{
							m_lastProjectionLayerStatus = L"alpha-pso-null";
						}
						else if ((activeVertexBufferView.SizeInBytes == 0U) ||
						         (activeVertexBufferView.StrideInBytes == 0U))
						{
							m_lastProjectionLayerStatus = L"alpha-vbview-null";
						}
						else
						{
							m_lastProjectionLayerStatus = L"alpha-args-invalid";
						}
					}
					else if (failedDrawPhase == Common::DebugOverlay::DrawCommandValidationPhase::VertexRange)
					{
						m_lastProjectionLayerStatus = L"alpha-draw-range";
					}
					else
					{
						m_lastProjectionLayerStatus = L"alpha-draw-args";
					}
					m_sessionName = L"Session(unavailable): XR debug draw arguments invalid";
					overlayDrawReady = false;
				}
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
			m_xrCommandList->ResourceBarrier(
			    1,
			    &swapchainToResolveSourceBarrier);

			m_xrCommandList->ResolveSubresource(
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
			m_xrCommandList->ResourceBarrier(
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
			m_xrCommandList->ResourceBarrier(1, &sceneToShaderBarrier);
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
			m_xrCommandList->SetDescriptorHeaps(
			    static_cast<UINT>(std::size(descriptorHeaps)),
			    descriptorHeaps);
			m_xrCommandList->RSSetViewports(1, &viewport);
			m_xrCommandList->RSSetScissorRects(1, &scissorRect);
			m_xrCommandList->OMSetRenderTargets(
			    1,
			    &swapchainRtvHandle,
			    FALSE,
			    nullptr);
			m_xrCommandList->SetGraphicsRootSignature(
			    m_fxaaRootSignature.Get());
			m_xrCommandList->SetPipelineState(
			    m_fxaaPipelineState.Get());
			m_xrCommandList->IASetPrimitiveTopology(
			    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			const float fxaaParameters[Common::FxaaParameterCount]{
				1.0F / static_cast<float>((std::max)(1, swapchainContext.width)),
				1.0F / static_cast<float>((std::max)(1, swapchainContext.height)),
				m_renderQuality.fxaaEdgeThreshold,
				m_renderQuality.fxaaEdgeThresholdMin,
				m_renderQuality.fxaaBlendFactor
			};
			m_xrCommandList->SetGraphicsRootDescriptorTable(
			    0,
			    sceneSrvGpuHandle);
			m_xrCommandList->SetGraphicsRoot32BitConstants(
			    1,
			    Common::FxaaParameterCount,
			    fxaaParameters,
			    0);
			m_xrCommandList->DrawInstanced(3, 1, 0, 0);
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
				m_xrCommandList->ResourceBarrier(
				    static_cast<UINT>(std::size(toCopyBarriers)),
				    toCopyBarriers);

				m_xrCommandList->CopyResource(
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
				m_xrCommandList->ResourceBarrier(
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

		D3D12_RESOURCE_BARRIER toCommonBarrier{};
		toCommonBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toCommonBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		toCommonBarrier.Transition.pResource = renderTargetTexture;
		toCommonBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		toCommonBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		toCommonBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_xrCommandList->ResourceBarrier(1, &toCommonBarrier);

		const HRESULT closeResult = m_xrCommandList->Close();
		if (FAILED(closeResult))
		{
			m_lastProjectionLayerStatus = L"command-close-failed";
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12GraphicsCommandList::Close", closeResult);
			return false;
		}

		if (!ExecuteAndWaitXrCommandList())
		{
			m_lastProjectionLayerStatus = L"execute-failed";
			return false;
		}

		return true;
	}

	bool NullXrSystem::ExecuteAndWaitXrCommandList()
	{
		if ((m_xrFence == nullptr) || (m_xrFenceEvent == nullptr) || (m_d3d12Queue == nullptr))
		{
			m_sessionName = L"Session(unavailable): XR fence resources missing";
			return false;
		}

		ID3D12CommandList* commandLists[] = { m_xrCommandList.Get() };
		m_d3d12Queue->ExecuteCommandLists(static_cast<UINT>(std::size(commandLists)), commandLists);

		const HRESULT signalResult = m_d3d12Queue->Signal(m_xrFence.Get(), m_xrFenceValue);
		if (FAILED(signalResult))
		{
			m_sessionName = L"Session(unavailable): " +
			                BuildD3d12ErrorMessage(L"ID3D12CommandQueue::Signal", signalResult);
			return false;
		}

		if (m_xrFence->GetCompletedValue() < m_xrFenceValue)
		{
			const HRESULT eventResult = m_xrFence->SetEventOnCompletion(m_xrFenceValue, m_xrFenceEvent);
			if (FAILED(eventResult))
			{
				m_sessionName = L"Session(unavailable): " +
				                BuildD3d12ErrorMessage(L"ID3D12Fence::SetEventOnCompletion", eventResult);
				return false;
			}

			const DWORD waitResult = WaitForSingleObject(m_xrFenceEvent, INFINITE);
			if (waitResult != WAIT_OBJECT_0)
			{
				m_sessionName = L"Session(unavailable): WaitForSingleObject failed";
				return false;
			}
		}

		++m_xrFenceValue;
		return true;
	}

	void NullXrSystem::DestroyOpenXrInstance()
	{
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

		if (m_instanceHandle != XR_NULL_HANDLE)
		{
			LogXrCleanupFailure(
			    L"xrDestroyInstance",
			    xrDestroyInstance(m_instanceHandle));
			m_instanceHandle = XR_NULL_HANDLE;
		}

		m_d3d12Device.Reset();
		m_d3d12Queue.Reset();
		m_systemId = XR_NULL_SYSTEM_ID;
		m_referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		m_sessionState = XR_SESSION_STATE_UNKNOWN;
		m_isGraphicsBound = false;
		m_usesDedicatedGraphicsDevice = false;
		m_xrGetD3d12GraphicsRequirementsKhr = nullptr;
		m_colorSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_projectionSampleCount = 1U;
		m_lastBallFlightTimeSeconds = 0.0;
		m_ballTrajectory.clear();
		m_lastTrajectoryAdvanceTime = std::chrono::steady_clock::time_point{};
		m_trajectoryFadeScale = 1.0F;
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
			                referenceSpaceName + L")";
		}
		else
		{
			m_sessionName = L"Session: " + BuildSessionStateString(m_sessionState) +
			                (m_usesDedicatedGraphicsDevice ? L" (bound:dedi, space:" : L" (bound:shared, space:") +
			                referenceSpaceName + L")";
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
