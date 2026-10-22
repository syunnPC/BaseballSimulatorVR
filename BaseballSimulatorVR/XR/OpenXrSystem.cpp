// Instance, session and frame lifecycle of the OpenXR backend.
#include "OpenXrSystemDetail.hpp"

namespace BaseballSimulatorVR::Xr
{
	using namespace Detail;

	namespace
	{
		constexpr wchar_t ActiveRuntimeValueName[] = L"ActiveRuntime";
		constexpr wchar_t OpenXrRegistryPath[] = L"SOFTWARE\\Khronos\\OpenXR\\1";

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

		[[nodiscard]] bool IsSameLuid(const LUID& lhs, const LUID& rhs) noexcept
		{
			return (lhs.LowPart == rhs.LowPart) && (lhs.HighPart == rhs.HighPart);
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
#endif
	} // namespace

	void OpenXrSystem::SetFieldPresentationState(const Common::FieldPresentationState& state)
	{
		m_fieldPresentation = state;
	}

	bool OpenXrSystem::Initialize([[maybe_unused]] const HWND windowHandle)
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

	bool OpenXrSystem::AttachGraphicsBinding(
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

	void OpenXrSystem::SetBallState(const Physics::BallState& ballState)
	{
#if defined(_WIN64)
		m_latestBallState = ballState;
		AppendTrajectoryPoint(ballState);
#else
		(void)ballState;
#endif
	}

	void OpenXrSystem::SetOverlayVisibility(const Common::OverlayVisibilitySettings& overlayVisibility)
	{
		m_overlayVisibility = overlayVisibility;
	}

	void OpenXrSystem::SetRenderQuality(
	    const Common::RenderQualitySettings& renderQuality)
	{
		m_renderQuality = Common::NormalizeRenderQualitySettings(renderQuality);
	}

	void OpenXrSystem::SetDuelZonePassMarker(
	    const std::optional<Physics::Vector3>& zonePassMarker)
	{
#if defined(_WIN64)
		m_duelZonePassMarker = zonePassMarker;
#else
		(void)zonePassMarker;
#endif
	}

	void OpenXrSystem::SetBatOverlayState(
	    const std::optional<Common::BatOverlayState>& batOverlayState)
	{
#if defined(_WIN64)
		m_batOverlayState = batOverlayState;
#else
		(void)batOverlayState;
#endif
	}

	void OpenXrSystem::SetUiTelemetry(const UiTelemetry& telemetry)
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

	void OpenXrSystem::PollInput()
	{
		m_floorEyeHeightSample = {};
#if defined(_WIN64)
		m_floorEyeHeightSample.referenceSpaceRevision = m_floorReferenceRevision;
		m_floorEyeHeightSample.isFloorReferenced = m_sessionHandle != XR_NULL_HANDLE &&
		    (m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE || m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR);
		m_leftHandPose = {};
		m_rightHandPose = {};
		m_controllerInput = {};
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

	bool OpenXrSystem::UpdateReferenceSpace()
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

	std::vector<UiCommand> OpenXrSystem::ConsumePendingUiCommands()
	{
#if defined(_WIN64)
		std::vector<UiCommand> consumedCommands{};
		consumedCommands.swap(m_pendingUiCommands);
		return consumedCommands;
#else
		return {};
#endif
	}

	void OpenXrSystem::Shutdown()
	{
#if defined(_WIN64)
		if (m_isSessionRunning && (m_sessionHandle != XR_NULL_HANDLE) && !m_exitRequested)
		{
			// xrEndSession is valid only once the runtime reports STOPPING, so
			// request an exit and keep the frame loop alive until it does.
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

	std::wstring_view OpenXrSystem::GetRuntimeName() const noexcept
	{
		return m_runtimeName;
	}

	std::wstring_view OpenXrSystem::GetSessionName() const noexcept
	{
		return m_sessionName;
	}

	Common::ViewProjectionState OpenXrSystem::GetLeftEyeViewState() const noexcept
	{
		return m_leftEyeViewState;
	}

	Common::FloorEyeHeightSample OpenXrSystem::GetFloorEyeHeightSample() const noexcept
	{
		return m_floorEyeHeightSample;
	}

	HandPoseState OpenXrSystem::GetLeftHandPose() const noexcept
	{
		return m_leftHandPose;
	}

	HandPoseState OpenXrSystem::GetRightHandPose() const noexcept
	{
		return m_rightHandPose;
	}

	ControllerInputState OpenXrSystem::GetControllerInputState() const noexcept
	{
		return m_controllerInput;
	}

	FrameTiming OpenXrSystem::GetFrameTiming() const noexcept
	{
#if defined(_WIN64)
		return m_frameTiming;
#else
		return {};
#endif
	}

	bool OpenXrSystem::IsInputFocused() const noexcept
	{
#if defined(_WIN64)
		// Only a running, merely VISIBLE session has system UI over the app.
		return !m_isSessionRunning || (m_sessionState == XR_SESSION_STATE_FOCUSED);
#else
		return true;
#endif
	}

	bool OpenXrSystem::IsExitRequested() const noexcept
	{
#if defined(_WIN64)
		return m_exitRequested;
#else
		return false;
#endif
	}

	void OpenXrSystem::ApplyHapticPulse(const bool rightHand, const double amplitude, const double durationSeconds)
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

	std::wstring OpenXrSystem::QueryActiveRuntimePath()
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

	std::wstring OpenXrSystem::BuildRuntimeDisplayName(const std::wstring& runtimePath)
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
	void OpenXrSystem::TryInitializeOpenXrInstance()
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

	void OpenXrSystem::TryInitializeOpenXrSystem()
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

	bool OpenXrSystem::TryInitializeOpenXrSession(
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

	bool OpenXrSystem::TryInitializeGraphicsDevice(
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

	bool OpenXrSystem::TryCreateGraphicsDeviceFromAdapterLuid(
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

	bool OpenXrSystem::EnsureGraphicsQueueForCurrentDevice()
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

	void OpenXrSystem::HandleSessionStateChanged(const XrSessionState sessionState)
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

	void OpenXrSystem::HandleInteractionProfileChanged()
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

	void OpenXrSystem::ScheduleRecovery(const std::wstring_view reason)
	{
		const std::wstring message = L"[OpenXR] " + std::wstring{ reason } + L"; recreating the session.\n";
		OutputDebugStringW(message.c_str());
		DestroySession();
		// Query the system again: xrGetSystem reports FORM_FACTOR_UNAVAILABLE
		// until the device is back, and its id may change.
		m_systemId = XR_NULL_SYSTEM_ID;
		m_recoveryPending = true;
		m_nextRecoveryAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		m_sessionName = L"Session(recovering): " + std::wstring{ reason };
	}

	void OpenXrSystem::TryRecover()
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

	void OpenXrSystem::TryBeginFrame()
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

	void OpenXrSystem::SubmitFrame()
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
			// The per-view command resources or the queue are unusable after a
			// rendering failure. The frame has already ended without a layer;
			// recreate the session to rebuild them.
			ScheduleRecovery(L"projection rendering failed");
		}
#endif
	}

	void OpenXrSystem::EndFrame(const bool hasProjectionLayer)
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

	bool OpenXrSystem::LocateFrameViews(const XrTime predictedDisplayTime)
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

	void OpenXrSystem::DestroySession()
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
		m_renderSampleCount = 1U;
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

	void OpenXrSystem::DestroyOpenXrInstance()
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
		m_renderSampleCount = 1U;
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

	std::wstring OpenXrSystem::BuildRuntimeDisplayNameFromInstance(const XrInstance instanceHandle)
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

	void OpenXrSystem::UpdateSessionName()
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
			                 std::to_wstring((std::max)(1U, m_renderSampleCount)) +
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
