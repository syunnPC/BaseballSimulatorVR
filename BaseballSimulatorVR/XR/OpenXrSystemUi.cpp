// Controller-ray interaction with the world-space UI panels.
#include "OpenXrSystemDetail.hpp"

#if defined(_WIN64)
namespace BaseballSimulatorVR::Xr
{
	using namespace Detail;

	namespace
	{

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

		struct UiPanelTitleBarHitInfo final
		{
			bool isInsideTitleBar{ false };
			bool isInDragRegion{ false };
			bool isInMinimizeButton{ false };
			bool isInCloseButton{ false };
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
	} // namespace

	void OpenXrSystem::UpdateUiPanelInteraction(
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
		auto queryButtonPressed = [&](const XrAction action, const XrPath handPath) -> bool
		{
			if (action == XR_NULL_HANDLE)
			{
				return false;
			}
			XrActionStateGetInfo stateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
			stateInfo.action = action;
			stateInfo.subactionPath = handPath;
			XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
			return XR_SUCCEEDED(xrGetActionStateBoolean(m_sessionHandle, &stateInfo, &state)) &&
			       (state.isActive == XR_TRUE) && (state.currentState == XR_TRUE);
		};
		auto queryControllerButtons = [&](const XrPath handPath) -> ControllerButtonState
		{
			ControllerButtonState buttons{};
			buttons.isActive = querySelectPressed(handPath, &buttons.triggerPressed);
			buttons.primaryPressed = queryButtonPressed(m_primaryButtonAction, handPath);
			buttons.secondaryPressed = queryButtonPressed(m_secondaryButtonAction, handPath);
			return buttons;
		};
		m_controllerInput.left = queryControllerButtons(m_leftHandPath);
		m_controllerInput.right = queryControllerButtons(m_rightHandPath);
		m_controllerInput.uiPointerActive = false;
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
		// In the batting modes and behind the plate the bat or mitt hand holds the laser and
		// the other hand calls up the panel.
		const bool isBatMode =
		    (m_uiTelemetry.operationMode == UiOperationMode::BattingDuel) ||
		    (m_uiTelemetry.operationMode == UiOperationMode::TeeBatting) ||
		    (m_uiTelemetry.operationMode == UiOperationMode::Catcher);
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
		m_controllerInput.uiPointerActive = pointerInput.visible;
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
					queueCommand(UiCommandType::SetModeCatcher);
					return;
				case 4:
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
				case 5:
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
				case 6:
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
				case 7:
					queueCommand(UiCommandType::CyclePitchTrajectoryDisplayMode);
					return;
				case 8:
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
} // namespace BaseballSimulatorVR::Xr
#endif
