// OpenXR action sets, interaction-profile bindings and recentering.
#include "OpenXrSystemDetail.hpp"

#if defined(_WIN64)
namespace BaseballSimulatorVR::Xr
{
	using namespace Detail;

	bool OpenXrSystem::InitializeInputActions()
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

	void OpenXrSystem::DestroyInputActions()
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

	void OpenXrSystem::RecenterToBattingPosition()
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
} // namespace BaseballSimulatorVR::Xr
#endif
