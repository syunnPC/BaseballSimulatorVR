#include "Application.hpp"

#include "../Resource.h"
#include "../Common/DebugWorldProjection.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/ZoneMaskUtilities.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <shlobj.h>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Core
{
	namespace
	{
		constexpr std::wstring_view WindowClassName = L"BaseballSimulatorVR.WindowClass";
		constexpr std::wstring_view DefaultWindowTitle = L"BaseballSimulatorVR";
		constexpr auto FixedSimulationStep = std::chrono::duration<double>(1.0 / 1000.0);
		constexpr auto WindowTitleUpdateInterval = std::chrono::duration<double>(1.0 / 30.0);
		constexpr auto MaxFrameTime = std::chrono::milliseconds(250);
		constexpr std::wstring_view PitchSettingsFilePrefix = L"PitchSettings.";
		constexpr std::wstring_view PitchSettingsJsonFileName = L"PitchSettings.user.json";
		constexpr std::wstring_view LegacyPitchSettingsJsonFileName = L"PitchPreset.user.json";
		constexpr std::wstring_view DefaultEnvironmentSettingsJsonFileName = L"EnvironmentSettings.user.json";
		constexpr std::wstring_view PlaySettingsJsonFileName = L"PlaySettings.user.json";
		constexpr std::wstring_view SessionHistoryDatabaseFileName = L"SessionHistory.sqlite3";

		constexpr double MinimumForwardSpeed = 20.0;
		constexpr double MaximumForwardSpeed = 50.0;
		constexpr double MinimumHorizontalSpeed = -8.0;
		constexpr double MaximumHorizontalSpeed = 8.0;
		constexpr double MinimumVerticalSpeed = -4.0;
		constexpr double MaximumVerticalSpeed = 4.0;
		constexpr double MinimumReleaseHeightMeters = 0.80;
		constexpr double MaximumReleaseHeightMeters = 2.50;
		constexpr double MinimumSpinRpm = 500.0;
		constexpr double MaximumSpinRpm = 3500.0;
		constexpr double MinimumWindForwardVelocity = -8.0;
		constexpr double MaximumWindForwardVelocity = 8.0;
		constexpr double MinimumWindCrossVelocity = -8.0;
		constexpr double MaximumWindCrossVelocity = 8.0;
		constexpr double MinimumWindVerticalVelocity = -4.0;
		constexpr double MaximumWindVerticalVelocity = 4.0;
		constexpr double MinimumWindShearFactor = 0.0;
		constexpr double MaximumWindShearFactor = 0.40;
		constexpr double MinimumSeamAmplitude = 0.0;
		constexpr double MaximumSeamAmplitude = 0.50;
		constexpr double MinimumSeamFrequency = 0.0;
		constexpr double MaximumSeamFrequency = 60.0;
		constexpr double MinimumResultScore = -1.0;
		constexpr double MaximumResultScore = 1.0;
		constexpr double MinimumPlaybackSpeedPercent =
		    Common::RuntimeLimits::MinimumPlaybackSpeedPercent;
		constexpr double MaximumPlaybackSpeedPercent =
		    Common::RuntimeLimits::MaximumPlaybackSpeedPercent;
		constexpr double PlaybackSpeedStepPercent = 5.0;
		constexpr double MinimumTrailOpacityScale = 0.50;
		constexpr double MaximumTrailOpacityScale = 1.50;
		constexpr double TrailOpacityScaleStep = 0.05;
		constexpr double DefaultPitchDistanceMeters =
		    Common::DebugWorldLayout::PitchDistanceMeters;
		constexpr double MinimumPitchDistanceMeters =
		    Common::RuntimeLimits::MinimumPitchDistanceMeters;
		constexpr double MaximumPitchDistanceMeters =
		    Common::RuntimeLimits::MaximumPitchDistanceMeters;
		constexpr double PitchDistanceStepMeters = 0.10;
		constexpr double DefaultStrikeZoneBottomMeters =
		    Common::DebugWorldLayout::StrikeZoneBottomMeters;
		constexpr double DefaultStrikeZoneHeightMeters =
		    Common::DebugWorldLayout::StrikeZoneHeightMeters;
		constexpr double DefaultStrikeZoneWidthMeters =
		    Common::DebugWorldLayout::StrikeZoneWidthMeters;
		constexpr double MinimumStrikeZoneBottomMeters = 0.20;
		constexpr double MaximumStrikeZoneBottomMeters = 1.50;
		constexpr double MinimumStrikeZoneHeightMeters = 0.40;
		constexpr double MaximumStrikeZoneHeightMeters = 1.60;
		constexpr double MinimumStrikeZoneWidthMeters = 0.20;
		constexpr double MaximumStrikeZoneWidthMeters = 0.80;
		constexpr double MinimumBatRadiusMeters = 0.010;
		constexpr double MaximumBatRadiusMeters = 0.080;
		constexpr double MinimumBatLengthMeters = 0.40;
		constexpr double MaximumBatLengthMeters = 1.20;
		constexpr double MinimumBatBallRestitution = 0.0;
		constexpr double MaximumBatBallRestitution = 1.0;
		constexpr double MinimumBatAngleOffsetDegrees = -35.0;
		constexpr double MaximumBatAngleOffsetDegrees = 35.0;
		constexpr double MinimumBatYawOffsetDegrees = -35.0;
		constexpr double MaximumBatYawOffsetDegrees = 35.0;
		constexpr double BatAngleOffsetStepDegrees = 2.5;
		constexpr double BatYawOffsetStepDegrees = 2.5;
		constexpr double BatStartForwardOffsetMeters = 0.08;
		constexpr double BatCollisionSuppressDurationSeconds = 0.04;
		constexpr double MaximumBattedBallSpeedMetersPerSecond = 120.0;
		constexpr double MinimumTeeLaunchSpeedMetersPerSecond = 15.0;
		constexpr double MaximumTeeLaunchSpeedMetersPerSecond = 62.0;
		constexpr double TeeLaunchSpeedFromBatSpeedMultiplier = 2.15;
		constexpr double TeeLaunchSpeedFromBatSpeedBias = 15.5;
		constexpr double DuelForwardAssistSwingThresholdMetersPerSecond = 0.35;
		constexpr double DuelForwardAssistBaseSpeedMetersPerSecond = 5.5;
		constexpr double DuelForwardAssistFromSwingScale = 0.80;
		constexpr double DuelForwardAssistMaximumSpeedMetersPerSecond = 30.0;
		constexpr double GroundPlaneHeightMeters = 0.0;
		constexpr double GroundContactHeightMeters =
		    GroundPlaneHeightMeters + Common::DebugWorldStyle::BallMeshRadiusMeters;
		constexpr double GroundBounceRestitution = 0.50;
		constexpr double GroundBounceTangentialVelocityScale = 0.88;
		constexpr double GroundRollingDampingPerSecond = 1.35;
		constexpr double GroundBounceVelocityThreshold = -0.05;
		constexpr double MinimumGroundFanRadiusMeters = 10.0;
		constexpr double MaximumGroundFanRadiusMeters = 300.0;
		constexpr double PostBounceSimulationSeconds = 5.0;
		constexpr double TeeBallHeightMeters = 1.00;
		constexpr double TeeBallMinX = -1.0;
		constexpr double TeeBallMaxX = 1.0;
		constexpr double TeeBallMinY = 0.40;
		constexpr double TeeBallMaxY = 1.60;
		constexpr double TeeBallMinZ = MinimumPitchDistanceMeters;
		constexpr double TeeBallMaxZ = MaximumPitchDistanceMeters;
		constexpr double TeeBallAdjustStepX = 0.05;
		constexpr double TeeBallAdjustStepY = 0.05;
		constexpr double TeeBallAdjustStepZ = 0.05;
		constexpr double MinimumDuelPitchIntervalSeconds =
		    Common::RuntimeLimits::MinimumDuelPitchIntervalSeconds;
		constexpr double MaximumDuelPitchIntervalSeconds =
		    Common::RuntimeLimits::MaximumDuelPitchIntervalSeconds;
		constexpr double DuelPitchIntervalStepSeconds = 0.25;
		constexpr double ForcedTrajectoryResetFlightTimeSeconds = -1.0;
		constexpr int DuelZoneColumns = Common::RuntimeLimits::DuelZoneColumns;
		constexpr int DuelZoneRows = Common::RuntimeLimits::DuelZoneRows;
		constexpr int DuelZoneCount = Common::RuntimeLimits::DuelZoneCount;
		constexpr std::array<Data::PitchPresetType, 4> DefaultPitchProfiles{
			Data::PitchPresetType::Fastball,
			Data::PitchPresetType::Slider,
			Data::PitchPresetType::Curveball,
			Data::PitchPresetType::Changeup
		};

		struct FxaaQualityPreset final
		{
			float edgeThreshold{ 0.125F };
			float edgeThresholdMin{ 0.0312F };
			float blendFactor{ 0.75F };
			std::wstring_view label{ L"balanced" };
		};

		constexpr std::array<FxaaQualityPreset, 5> FxaaQualityPresets{
			FxaaQualityPreset{ 0.38F, 0.12F, 0.00F, L"off-like" },
			FxaaQualityPreset{ 0.28F, 0.08F, 0.35F, L"sharp" },
			FxaaQualityPreset{ 0.18F, 0.05F, 0.65F, L"balanced" },
			FxaaQualityPreset{ 0.10F, 0.022F, 0.90F, L"smooth" },
			FxaaQualityPreset{ 0.05F, 0.008F, 1.00F, L"smooth+" }
		};

		struct TrajectoryTargetVelocity final
		{
			double x{ 0.0 };
			double y{ 0.0 };
		};

		struct DuelZoneGridLayout final
		{
			double leftEdge{ 0.0 };
			double rightEdge{ 0.0 };
			double topEdge{ 0.0 };
			double bottomEdge{ 0.0 };
			double cellWidth{ 0.0 };
			double cellHeight{ 0.0 };
		};

		struct DuelZoneCellBounds final
		{
			double left{ 0.0 };
			double right{ 0.0 };
			double top{ 0.0 };
			double bottom{ 0.0 };
		};

		[[nodiscard]] bool IsValidDuelZoneIndex(const int zoneIndex) noexcept
		{
			return Common::IsValidZoneIndex(zoneIndex, DuelZoneCount);
		}

		[[nodiscard]] std::uint32_t NormalizeDuelZoneMask(
		    const std::uint32_t zoneMask) noexcept
		{
			return Common::NormalizeZoneMask(zoneMask, DuelZoneCount);
		}

		[[nodiscard]] std::wstring BuildDuelZoneSelectionLabel(
		    const std::uint32_t zoneMask,
		    const std::wstring_view emptyLabel = L"random")
		{
			return Common::BuildZoneSelectionLabel(
			    NormalizeDuelZoneMask(zoneMask),
			    DuelZoneCount,
			    emptyLabel);
		}

		[[nodiscard]] std::vector<int> CollectSelectedDuelZoneIndices(
		    const std::uint32_t zoneMask)
		{
			return Common::CollectSelectedZoneIndices(zoneMask, DuelZoneCount);
		}

		[[nodiscard]] DuelZoneGridLayout BuildDuelZoneGridLayout() noexcept
		{
			const int strikeZoneColumnCount = (std::max)(1, DuelZoneColumns - 2);
			const int strikeZoneRowCount = (std::max)(1, DuelZoneRows - 2);
			const double strikeZoneHalfWidth =
			    Common::GetStrikeZoneHalfWidthMeters();
			const double strikeZoneBottom =
			    Common::GetStrikeZoneBottomMeters();
			const double strikeZoneTop =
			    Common::GetStrikeZoneTopMeters();
			const double strikeZoneWidth = strikeZoneHalfWidth * 2.0;
			const double strikeZoneHeight = strikeZoneTop - strikeZoneBottom;
			const double cellWidth =
			    strikeZoneWidth / static_cast<double>(strikeZoneColumnCount);
			const double cellHeight =
			    strikeZoneHeight / static_cast<double>(strikeZoneRowCount);
			const double horizontalRingCount =
			    static_cast<double>(DuelZoneColumns - strikeZoneColumnCount) * 0.5;
			const double verticalRingCount =
			    static_cast<double>(DuelZoneRows - strikeZoneRowCount) * 0.5;
			return DuelZoneGridLayout{
				-(strikeZoneHalfWidth +
				  (horizontalRingCount * cellWidth)),
				strikeZoneHalfWidth +
				    (horizontalRingCount * cellWidth),
				strikeZoneTop +
				    (verticalRingCount * cellHeight),
				strikeZoneBottom -
				    (verticalRingCount * cellHeight),
				cellWidth,
				cellHeight
			};
		}

		[[nodiscard]] std::optional<DuelZoneCellBounds> ResolveDuelZoneCellBounds(
		    const int zoneIndex)
		{
			if (!IsValidDuelZoneIndex(zoneIndex))
			{
				return std::nullopt;
			}

			const DuelZoneGridLayout gridLayout = BuildDuelZoneGridLayout();
			const int rowIndex = zoneIndex / DuelZoneColumns;
			const int columnIndex = zoneIndex % DuelZoneColumns;
			const double left =
			    gridLayout.leftEdge +
			    (static_cast<double>(columnIndex) * gridLayout.cellWidth);
			const double right = left + gridLayout.cellWidth;
			const double top =
			    gridLayout.topEdge -
			    (static_cast<double>(rowIndex) * gridLayout.cellHeight);
			const double bottom = top - gridLayout.cellHeight;
			return DuelZoneCellBounds{
				left,
				right,
				top,
				bottom
			};
		}

		[[nodiscard]] bool IsPointInsideDuelZoneCell(
		    const DuelZoneCellBounds& cellBounds,
		    const Physics::Vector3& zonePassPoint,
		    const double worldVerticalOffsetMeters = 0.0) noexcept
		{
			const double worldSpacePassY = zonePassPoint.y + worldVerticalOffsetMeters;
			return (zonePassPoint.x >= cellBounds.left) &&
			       (zonePassPoint.x <= cellBounds.right) &&
			       (worldSpacePassY <= cellBounds.top) &&
			       (worldSpacePassY >= cellBounds.bottom);
		}

		[[nodiscard]] std::optional<Physics::Vector3> SimulateZonePassPointAtDepth(
		    const Baseball::PitchParameters& pitchParameters,
		    const double pitchDistanceMeters)
		{
			if (pitchDistanceMeters <= 0.0)
			{
				return std::nullopt;
			}

			Physics::PitchPhysicsEngine pitchPhysicsEngine{};
			Physics::BallState previousState{};
			previousState.position.y = pitchParameters.releaseHeightMeters;
			previousState.velocity = pitchParameters.initialVelocity;
			previousState.spinAxis = Physics::NormalizeOrZero(
			    pitchParameters.spinAxis);
			previousState.spinRpm = pitchParameters.spinRpm;
			Physics::BallState currentState = previousState;
			const double estimatedTimeToPlateSeconds =
			    pitchDistanceMeters /
			    (std::max)(std::abs(pitchParameters.initialVelocity.z), 1.0);
			const double simulationHorizonSeconds = std::clamp(
			    estimatedTimeToPlateSeconds * 6.0,
			    1.20,
			    8.00);
			const int maxStepCount = static_cast<int>(
			                             std::ceil(simulationHorizonSeconds /
			                                       FixedSimulationStep.count())) +
			                         1;
			for (int stepIndex = 0; stepIndex < maxStepCount; ++stepIndex)
			{
				currentState = pitchPhysicsEngine.Step(
				    previousState,
				    pitchParameters,
				    FixedSimulationStep);
				const double previousDepth = previousState.position.z;
				const double currentDepth = currentState.position.z;
				const double crossingDenominator = currentDepth - previousDepth;
				const bool crossedDepth =
				    (std::abs(crossingDenominator) > 1e-9) &&
				    (((previousDepth - pitchDistanceMeters) *
				      (currentDepth - pitchDistanceMeters)) <= 0.0);
				if (crossedDepth)
				{
					double blend =
					    (pitchDistanceMeters - previousDepth) / crossingDenominator;
					blend = std::clamp(blend, 0.0, 1.0);
					return Physics::Vector3{
						previousState.position.x +
						    ((currentState.position.x - previousState.position.x) * blend),
						previousState.position.y +
						    ((currentState.position.y - previousState.position.y) * blend),
						pitchDistanceMeters
					};
				}

				previousState = currentState;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<Physics::Vector3> ResolveDuelZoneCellCenterPosition(
		    const int zoneIndex,
		    const double zoneDepthMeters)
		{
			const std::optional<DuelZoneCellBounds> zoneCellBounds =
			    ResolveDuelZoneCellBounds(zoneIndex);
			if (!zoneCellBounds.has_value())
			{
				return std::nullopt;
			}

			return Physics::Vector3{
				(zoneCellBounds->left + zoneCellBounds->right) * 0.5,
				(zoneCellBounds->top + zoneCellBounds->bottom) * 0.5,
				zoneDepthMeters
			};
		}

		[[nodiscard]] std::optional<Physics::Vector3> ResolveRandomDuelZoneCellPosition(
		    const int zoneIndex,
		    const double zoneDepthMeters,
		    std::mt19937& randomEngine)
		{
			const std::optional<DuelZoneCellBounds> zoneCellBounds =
			    ResolveDuelZoneCellBounds(zoneIndex);
			if (!zoneCellBounds.has_value())
			{
				return std::nullopt;
			}

			std::uniform_real_distribution<double> lateralDistribution(
			    zoneCellBounds->left,
			    zoneCellBounds->right);
			std::uniform_real_distribution<double> verticalDistribution(
			    zoneCellBounds->bottom,
			    zoneCellBounds->top);
			return Physics::Vector3{
				lateralDistribution(randomEngine),
				verticalDistribution(randomEngine),
				zoneDepthMeters
			};
		}

		[[nodiscard]] std::optional<TrajectoryTargetVelocity>
		ResolveTrajectoryZoneTargetVelocity(
		    const Baseball::PitchParameters& pitchParameters,
		    const double pitchDistanceMeters,
		    const int zoneIndex,
		    const DuelZoneCellBounds& targetZoneCellBounds,
		    const Physics::Vector3& targetZoneWorldPosition,
		    const double worldVerticalOffsetMeters = 0.0)
		{
			const double worldVerticalOffsetMetersForTarget =
			    std::isfinite(worldVerticalOffsetMeters)
			        ? worldVerticalOffsetMeters
			        : 0.0;
			const double simulationTargetY =
			    targetZoneWorldPosition.y - worldVerticalOffsetMetersForTarget;

			const auto evaluatePassPoint =
			    [&](const double lateralVelocityMetersPerSecond,
			        const double verticalVelocityMetersPerSecond,
			        Physics::Vector3* const outPassPoint,
			        double* const outSquaredError) -> bool
			{
				if ((outPassPoint == nullptr) || (outSquaredError == nullptr))
				{
					return false;
				}

				Baseball::PitchParameters candidateParameters = pitchParameters;
				candidateParameters.initialVelocity.x = std::clamp(
				    lateralVelocityMetersPerSecond,
				    MinimumHorizontalSpeed,
				    MaximumHorizontalSpeed);
				candidateParameters.initialVelocity.y = std::clamp(
				    verticalVelocityMetersPerSecond,
				    MinimumVerticalSpeed,
				    MaximumVerticalSpeed);
				const std::optional<Physics::Vector3> zonePassPoint =
				    SimulateZonePassPointAtDepth(
				        candidateParameters,
				        pitchDistanceMeters);
				if (!zonePassPoint.has_value())
				{
					return false;
				}

				*outPassPoint = zonePassPoint.value();
				const double errorX = targetZoneWorldPosition.x - outPassPoint->x;
				const double errorY = simulationTargetY - outPassPoint->y;
				*outSquaredError = (errorX * errorX) + (errorY * errorY);
				return std::isfinite(*outSquaredError);
			};

			double bestVelocityX = 0.0;
			double bestVelocityY = 0.0;
			Physics::Vector3 bestPassPoint{};
			double bestSquaredError = 0.0;
			bool hasBestCandidate = false;
			const auto considerCandidate =
			    [&](const double lateralVelocityMetersPerSecond,
			        const double verticalVelocityMetersPerSecond) -> void
			{
				Physics::Vector3 candidatePassPoint{};
				double candidateSquaredError = 0.0;
				if (!evaluatePassPoint(
				        lateralVelocityMetersPerSecond,
				        verticalVelocityMetersPerSecond,
				        &candidatePassPoint,
				        &candidateSquaredError))
				{
					return;
				}

				if (!hasBestCandidate || (candidateSquaredError < bestSquaredError))
				{
					hasBestCandidate = true;
					bestVelocityX = std::clamp(
					    lateralVelocityMetersPerSecond,
					    MinimumHorizontalSpeed,
					    MaximumHorizontalSpeed);
					bestVelocityY = std::clamp(
					    verticalVelocityMetersPerSecond,
					    MinimumVerticalSpeed,
					    MaximumVerticalSpeed);
					bestPassPoint = candidatePassPoint;
					bestSquaredError = candidateSquaredError;
				}
			};

			const double forwardSpeedMagnitude = std::clamp(
			    std::abs(pitchParameters.initialVelocity.z),
			    8.0,
			    80.0);
			const double solveTimeSeconds = std::clamp(
			    pitchDistanceMeters / forwardSpeedMagnitude,
			    0.20,
			    1.60);
			const double releaseHeightMeters = pitchParameters.releaseHeightMeters;
			constexpr double gravityAccelerationMetersPerSecondSquared = 9.80665;
			const double seededVelocityX = std::clamp(
			    targetZoneWorldPosition.x / solveTimeSeconds,
			    MinimumHorizontalSpeed,
			    MaximumHorizontalSpeed);
			const double seededVelocityY = std::clamp(
			    ((simulationTargetY - releaseHeightMeters) +
			     (0.5 * gravityAccelerationMetersPerSecondSquared *
			      solveTimeSeconds * solveTimeSeconds)) /
			        solveTimeSeconds,
			    MinimumVerticalSpeed,
			    MaximumVerticalSpeed);
			considerCandidate(seededVelocityX, seededVelocityY);
			considerCandidate(
			    pitchParameters.initialVelocity.x,
			    pitchParameters.initialVelocity.y);

			constexpr double CoarseSearchVelocityStep = 0.45;
			for (int lateralOffsetIndex = -4; lateralOffsetIndex <= 4;
			     lateralOffsetIndex += 2)
			{
				for (int verticalOffsetIndex = -4; verticalOffsetIndex <= 4;
				     verticalOffsetIndex += 2)
				{
					considerCandidate(
					    seededVelocityX +
					        (CoarseSearchVelocityStep *
					         static_cast<double>(lateralOffsetIndex)),
					    seededVelocityY +
					        (CoarseSearchVelocityStep *
					         static_cast<double>(verticalOffsetIndex)));
				}
			}

			if (!hasBestCandidate)
			{
				return std::nullopt;
			}

			double currentVelocityX = bestVelocityX;
			double currentVelocityY = bestVelocityY;
			Physics::Vector3 currentPassPoint = bestPassPoint;
			double currentSquaredError = bestSquaredError;
			double lambda = 1e-3;
			constexpr double ProbeVelocityStep = 0.08;
			constexpr int MaxSolveIterations = 26;
			for (int iteration = 0; iteration < MaxSolveIterations; ++iteration)
			{
				if (IsPointInsideDuelZoneCell(
				        targetZoneCellBounds,
				        currentPassPoint,
				        worldVerticalOffsetMetersForTarget))
				{
					break;
				}

				Physics::Vector3 probePassPointX{};
				double probeSquaredErrorX = 0.0;
				Physics::Vector3 probePassPointY{};
				double probeSquaredErrorY = 0.0;
				const bool hasProbeX = evaluatePassPoint(
				    currentVelocityX + ProbeVelocityStep,
				    currentVelocityY,
				    &probePassPointX,
				    &probeSquaredErrorX);
				const bool hasProbeY = evaluatePassPoint(
				    currentVelocityX,
				    currentVelocityY + ProbeVelocityStep,
				    &probePassPointY,
				    &probeSquaredErrorY);
				if (!hasProbeX || !hasProbeY)
				{
					lambda = std::min(1e6, lambda * 4.0);
					continue;
				}

				const double errorX =
				    currentPassPoint.x - targetZoneWorldPosition.x;
				const double errorY = currentPassPoint.y - simulationTargetY;
				const double j11 =
				    (probePassPointX.x - currentPassPoint.x) / ProbeVelocityStep;
				const double j12 =
				    (probePassPointY.x - currentPassPoint.x) / ProbeVelocityStep;
				const double j21 =
				    (probePassPointX.y - currentPassPoint.y) / ProbeVelocityStep;
				const double j22 =
				    (probePassPointY.y - currentPassPoint.y) / ProbeVelocityStep;

				const double a11 = (j11 * j11) + (j21 * j21) + lambda;
				const double a12 = (j11 * j12) + (j21 * j22);
				const double a22 = (j12 * j12) + (j22 * j22) + lambda;
				const double b1 = -((j11 * errorX) + (j21 * errorY));
				const double b2 = -((j12 * errorX) + (j22 * errorY));
				const double determinant = (a11 * a22) - (a12 * a12);
				if (std::abs(determinant) <= 1e-12)
				{
					break;
				}

				double deltaVelocityX = ((b1 * a22) - (b2 * a12)) / determinant;
				double deltaVelocityY = ((a11 * b2) - (a12 * b1)) / determinant;
				if (!std::isfinite(deltaVelocityX) || !std::isfinite(deltaVelocityY))
				{
					break;
				}

				const double stepNorm = std::sqrt(
				    (deltaVelocityX * deltaVelocityX) +
				    (deltaVelocityY * deltaVelocityY));
				if (stepNorm > 1.80)
				{
					const double stepScale = 1.80 / stepNorm;
					deltaVelocityX *= stepScale;
					deltaVelocityY *= stepScale;
				}

				const double candidateVelocityX = std::clamp(
				    currentVelocityX + deltaVelocityX,
				    MinimumHorizontalSpeed,
				    MaximumHorizontalSpeed);
				const double candidateVelocityY = std::clamp(
				    currentVelocityY + deltaVelocityY,
				    MinimumVerticalSpeed,
				    MaximumVerticalSpeed);
				Physics::Vector3 candidatePassPoint{};
				double candidateSquaredError = 0.0;
				if (!evaluatePassPoint(
				        candidateVelocityX,
				        candidateVelocityY,
				        &candidatePassPoint,
				        &candidateSquaredError))
				{
					lambda = std::min(1e6, lambda * 4.0);
					continue;
				}

				if (candidateSquaredError < currentSquaredError)
				{
					currentVelocityX = candidateVelocityX;
					currentVelocityY = candidateVelocityY;
					currentPassPoint = candidatePassPoint;
					currentSquaredError = candidateSquaredError;
					lambda = std::max(1e-6, lambda * 0.5);
				}
				else
				{
					lambda = std::min(1e6, lambda * 4.0);
				}

				if (currentSquaredError < bestSquaredError)
				{
					bestVelocityX = currentVelocityX;
					bestVelocityY = currentVelocityY;
					bestPassPoint = currentPassPoint;
					bestSquaredError = currentSquaredError;
				}
			}

			bool hasInZoneCandidate = false;
			double inZoneVelocityX = bestVelocityX;
			double inZoneVelocityY = bestVelocityY;
			Physics::Vector3 inZonePassPoint = bestPassPoint;
			double inZoneSquaredError = bestSquaredError;
			const auto considerInZoneCandidate =
			    [&](const double lateralVelocityMetersPerSecond,
			        const double verticalVelocityMetersPerSecond) -> void
			{
				Physics::Vector3 candidatePassPoint{};
				double candidateSquaredError = 0.0;
				if (!evaluatePassPoint(
				        lateralVelocityMetersPerSecond,
				        verticalVelocityMetersPerSecond,
				        &candidatePassPoint,
				        &candidateSquaredError))
				{
					return;
				}
				if (!IsPointInsideDuelZoneCell(
				        targetZoneCellBounds,
				        candidatePassPoint,
				        worldVerticalOffsetMetersForTarget))
				{
					return;
				}
				if (!hasInZoneCandidate || (candidateSquaredError < inZoneSquaredError))
				{
					hasInZoneCandidate = true;
					inZoneVelocityX = std::clamp(
					    lateralVelocityMetersPerSecond,
					    MinimumHorizontalSpeed,
					    MaximumHorizontalSpeed);
					inZoneVelocityY = std::clamp(
					    verticalVelocityMetersPerSecond,
					    MinimumVerticalSpeed,
					    MaximumVerticalSpeed);
					inZonePassPoint = candidatePassPoint;
					inZoneSquaredError = candidateSquaredError;
				}
			};
			if (!IsPointInsideDuelZoneCell(
			        targetZoneCellBounds,
			        bestPassPoint,
			        worldVerticalOffsetMetersForTarget))
			{
				const auto sampleVelocityBox =
				    [&](const double centerVelocityX,
				        const double centerVelocityY,
				        const double halfSpanVelocityX,
				        const double halfSpanVelocityY,
				        const double stepVelocityX,
				        const double stepVelocityY) -> void
				{
					const double minVelocityX = std::clamp(
					    centerVelocityX - halfSpanVelocityX,
					    MinimumHorizontalSpeed,
					    MaximumHorizontalSpeed);
					const double maxVelocityX = std::clamp(
					    centerVelocityX + halfSpanVelocityX,
					    MinimumHorizontalSpeed,
					    MaximumHorizontalSpeed);
					const double minVelocityY = std::clamp(
					    centerVelocityY - halfSpanVelocityY,
					    MinimumVerticalSpeed,
					    MaximumVerticalSpeed);
					const double maxVelocityY = std::clamp(
					    centerVelocityY + halfSpanVelocityY,
					    MinimumVerticalSpeed,
					    MaximumVerticalSpeed);
					for (double sampleVelocityX = minVelocityX;
					     sampleVelocityX <= (maxVelocityX + 1e-6);
					     sampleVelocityX += stepVelocityX)
					{
						for (double sampleVelocityY = minVelocityY;
						     sampleVelocityY <= (maxVelocityY + 1e-6);
						     sampleVelocityY += stepVelocityY)
						{
							considerInZoneCandidate(sampleVelocityX, sampleVelocityY);
						}
					}
				};

				sampleVelocityBox(
				    bestVelocityX,
				    bestVelocityY,
				    1.80,
				    1.20,
				    0.18,
				    0.16);
				sampleVelocityBox(
				    seededVelocityX,
				    seededVelocityY,
				    2.60,
				    1.80,
				    0.22,
				    0.20);
				if (!hasInZoneCandidate)
				{
					for (double sampleVelocityX = MinimumHorizontalSpeed;
					     sampleVelocityX <= (MaximumHorizontalSpeed + 1e-6);
					     sampleVelocityX += 0.50)
					{
						for (double sampleVelocityY = MinimumVerticalSpeed;
						     sampleVelocityY <= (MaximumVerticalSpeed + 1e-6);
						     sampleVelocityY += 0.40)
						{
							considerInZoneCandidate(sampleVelocityX, sampleVelocityY);
						}
					}
				}
			}
			if (hasInZoneCandidate)
			{
				bestVelocityX = inZoneVelocityX;
				bestVelocityY = inZoneVelocityY;
				bestPassPoint = inZonePassPoint;
				bestSquaredError = inZoneSquaredError;
			}

			if (!IsPointInsideDuelZoneCell(
			        targetZoneCellBounds,
			        bestPassPoint,
			        worldVerticalOffsetMetersForTarget))
			{
				std::wostringstream solveWarning{};
				solveWarning << std::fixed << std::setprecision(3);
				const double worldSpacePassY =
				    bestPassPoint.y + worldVerticalOffsetMetersForTarget;
				solveWarning << L"[DuelZone] inverse solve near miss: zone="
				             << (zoneIndex + 1) << L", pass=("
				             << bestPassPoint.x << L","
				             << worldSpacePassY << L"), target=("
				             << targetZoneWorldPosition.x << L","
				             << targetZoneWorldPosition.y << L")\n";
				OutputDebugStringW(solveWarning.str().c_str());
			}

			return TrajectoryTargetVelocity{ bestVelocityX, bestVelocityY };
		}

		[[nodiscard]] std::mt19937& GetThreadLocalRandomEngine()
		{
			static thread_local std::mt19937 randomEngine{
				std::random_device{}()
			};
			return randomEngine;
		}

		struct GroundFanShape final
		{
			double halfAngleRadians{ 0.0 };
			double innerRadiusMeters{ 0.0 };
			double outerRadiusMeters{ 0.0 };
		};

		[[nodiscard]] GroundFanShape BuildGroundFanShape(
		    const double strikeZoneWidthMeters,
		    const double configuredOuterRadiusMeters) noexcept
		{
			const double clampedOuterRadiusMeters = std::clamp(
			    configuredOuterRadiusMeters,
			    MinimumGroundFanRadiusMeters,
			    MaximumGroundFanRadiusMeters);
			const double targetChordLengthMeters =
			    (std::max)(0.20, strikeZoneWidthMeters) + 2.0;
			const double halfChordLengthMeters = targetChordLengthMeters * 0.5;
			const double plateDepthFromCenterMeters = (std::max)(0.10,
			                                                     std::abs(Common::DebugWorldLayout::BatterToPlateDistanceMeters));
			const double halfAngleRadians = std::atan2(
			    halfChordLengthMeters,
			    plateDepthFromCenterMeters);
			const double innerRadiusMeters = std::sqrt(
			    (plateDepthFromCenterMeters * plateDepthFromCenterMeters) +
			    (halfChordLengthMeters * halfChordLengthMeters));
			const double outerRadiusMeters = (std::max)(innerRadiusMeters + 1.0,
			                                            clampedOuterRadiusMeters);
			return GroundFanShape{
				halfAngleRadians,
				innerRadiusMeters,
				outerRadiusMeters
			};
		}

		[[nodiscard]] bool IsWorldPointInsideGroundFan(
		    const Physics::Vector3& worldPosition,
		    const GroundFanShape& fanShape) noexcept
		{
			if ((fanShape.halfAngleRadians <= 1e-6) ||
			    (fanShape.outerRadiusMeters <= fanShape.innerRadiusMeters))
			{
				return false;
			}

			const double horizontalRadiusMeters = std::sqrt(
			    (worldPosition.x * worldPosition.x) +
			    (worldPosition.z * worldPosition.z));
			if ((horizontalRadiusMeters < fanShape.innerRadiusMeters) ||
			    (horizontalRadiusMeters > fanShape.outerRadiusMeters))
			{
				return false;
			}

			const double angleRadians = std::atan2(
			    worldPosition.x,
			    -worldPosition.z);
			return std::abs(angleRadians) <= fanShape.halfAngleRadians;
		}

		[[nodiscard]] Physics::Vector3 RotateVectorAroundAxis(
		    const Physics::Vector3& vector,
		    const Physics::Vector3& axis,
		    const double angleRadians) noexcept
		{
			const Physics::Vector3 normalizedAxis =
			    Physics::NormalizeOrZero(axis);
			if (Physics::Length(normalizedAxis) <= 1e-6)
			{
				return vector;
			}

			const double cosine = std::cos(angleRadians);
			const double sine = std::sin(angleRadians);
			return (vector * cosine) +
			       (Physics::Cross(normalizedAxis, vector) * sine) +
			       (normalizedAxis * Physics::Dot(normalizedAxis, vector) *
			        (1.0 - cosine));
		}

		[[nodiscard]] bool DidBallCrossGroundFromAbove(
		    const Physics::BallState& previousState,
		    const Physics::BallState& currentState) noexcept
		{
			return (previousState.position.y > GroundContactHeightMeters) &&
			       (currentState.position.y <= GroundContactHeightMeters);
		}

		[[nodiscard]] Physics::Vector3 ResolveGroundCrossPoint(
		    const Physics::BallState& previousState,
		    const Physics::BallState& currentState) noexcept
		{
			const double denominator =
			    currentState.position.y - previousState.position.y;
			double blend = 0.0;
			if (std::abs(denominator) > 1e-6)
			{
				blend = (GroundContactHeightMeters - previousState.position.y) /
				        denominator;
			}
			blend = std::clamp(blend, 0.0, 1.0);
			return previousState.position +
			       ((currentState.position - previousState.position) * blend);
		}

		void ApplyGroundCollisionResponse(
		    Physics::BallState* const ballState,
		    const double frameDeltaSeconds) noexcept
		{
			if (ballState == nullptr)
			{
				return;
			}
			if (ballState->position.y > GroundContactHeightMeters)
			{
				return;
			}

			ballState->position.y = GroundContactHeightMeters;
			if (ballState->velocity.y < GroundBounceVelocityThreshold)
			{
				ballState->velocity.y =
				    -ballState->velocity.y * GroundBounceRestitution;
				ballState->velocity.x *= GroundBounceTangentialVelocityScale;
				ballState->velocity.z *= GroundBounceTangentialVelocityScale;
				return;
			}

			ballState->velocity.y = 0.0;
			const double rollingDamping = std::clamp(
			    1.0 - (frameDeltaSeconds * GroundRollingDampingPerSecond),
			    0.0,
			    1.0);
			ballState->velocity.x *= rollingDamping;
			ballState->velocity.z *= rollingDamping;
		}

		[[nodiscard]] bool ResolveClosestPointsOnSegments(
		    const Physics::Vector3& segmentStartA,
		    const Physics::Vector3& segmentEndA,
		    const Physics::Vector3& segmentStartB,
		    const Physics::Vector3& segmentEndB,
		    Physics::Vector3* const closestPointOnSegmentA,
		    Physics::Vector3* const closestPointOnSegmentB)
		{
			if ((closestPointOnSegmentA == nullptr) ||
			    (closestPointOnSegmentB == nullptr))
			{
				return false;
			}

			constexpr double epsilon = 1e-9;
			const Physics::Vector3 segmentDirectionA =
			    segmentEndA - segmentStartA;
			const Physics::Vector3 segmentDirectionB =
			    segmentEndB - segmentStartB;
			const Physics::Vector3 betweenStarts =
			    segmentStartA - segmentStartB;
			const double segmentALengthSquared =
			    Physics::Dot(segmentDirectionA, segmentDirectionA);
			const double segmentBLengthSquared =
			    Physics::Dot(segmentDirectionB, segmentDirectionB);
			const double segmentDot =
			    Physics::Dot(segmentDirectionA, segmentDirectionB);
			const double segmentAStartDot =
			    Physics::Dot(segmentDirectionA, betweenStarts);
			const double segmentBStartDot =
			    Physics::Dot(segmentDirectionB, betweenStarts);
			double segmentRatioA = 0.0;
			double segmentRatioB = 0.0;

			if ((segmentALengthSquared <= epsilon) &&
			    (segmentBLengthSquared <= epsilon))
			{
				*closestPointOnSegmentA = segmentStartA;
				*closestPointOnSegmentB = segmentStartB;
				return true;
			}

			if (segmentALengthSquared <= epsilon)
			{
				segmentRatioA = 0.0;
				segmentRatioB = std::clamp(
				    segmentBStartDot / segmentBLengthSquared,
				    0.0,
				    1.0);
			}
			else if (segmentBLengthSquared <= epsilon)
			{
				segmentRatioB = 0.0;
				segmentRatioA = std::clamp(
				    -segmentAStartDot / segmentALengthSquared,
				    0.0,
				    1.0);
			}
			else
			{
				const double denominator =
				    (segmentALengthSquared * segmentBLengthSquared) -
				    (segmentDot * segmentDot);
				if (std::abs(denominator) > epsilon)
				{
					segmentRatioA = std::clamp(
					    ((segmentDot * segmentBStartDot) -
					     (segmentAStartDot * segmentBLengthSquared)) /
					        denominator,
					    0.0,
					    1.0);
				}
				else
				{
					segmentRatioA = 0.0;
				}

				segmentRatioB =
				    ((segmentDot * segmentRatioA) + segmentBStartDot) /
				    segmentBLengthSquared;
				if (segmentRatioB < 0.0)
				{
					segmentRatioB = 0.0;
					segmentRatioA = std::clamp(
					    -segmentAStartDot / segmentALengthSquared,
					    0.0,
					    1.0);
				}
				else if (segmentRatioB > 1.0)
				{
					segmentRatioB = 1.0;
					segmentRatioA = std::clamp(
					    (segmentDot - segmentAStartDot) /
					        segmentALengthSquared,
					    0.0,
					    1.0);
				}
			}

			*closestPointOnSegmentA =
			    segmentStartA + (segmentDirectionA * segmentRatioA);
			*closestPointOnSegmentB =
			    segmentStartB + (segmentDirectionB * segmentRatioB);
			return true;
		}

		[[nodiscard]] double BuildWorldDepthOffsetMeters(
		    const double pitchDistanceMeters) noexcept
		{
			return pitchDistanceMeters +
			       Common::DebugWorldLayout::BatterToPlateDistanceMeters;
		}

		[[nodiscard]] Physics::Vector3 ConvertSimulationToCollisionWorldPosition(
		    const Physics::Vector3& simulationPosition,
		    const double pitchDistanceMeters,
		    const double moundHeightOffsetMeters) noexcept
		{
			const double depthOffsetMeters =
			    BuildWorldDepthOffsetMeters(pitchDistanceMeters);
			return Physics::Vector3{
				simulationPosition.x,
				simulationPosition.y + moundHeightOffsetMeters,
				simulationPosition.z - depthOffsetMeters
			};
		}

		[[nodiscard]] Physics::Vector3 ConvertCollisionWorldToSimulationPosition(
		    const Physics::Vector3& worldPosition,
		    const double pitchDistanceMeters,
		    const double moundHeightOffsetMeters) noexcept
		{
			const double depthOffsetMeters =
			    BuildWorldDepthOffsetMeters(pitchDistanceMeters);
			return Physics::Vector3{
				worldPosition.x,
				worldPosition.y - moundHeightOffsetMeters,
				worldPosition.z + depthOffsetMeters
			};
		}

		[[nodiscard]] Physics::Vector3 ConvertWorldToOverlaySimulationPosition(
		    const Physics::Vector3& worldPosition,
		    const double pitchDistanceMeters) noexcept
		{
			const double depthOffsetMeters =
			    BuildWorldDepthOffsetMeters(pitchDistanceMeters);
			return Physics::Vector3{
				worldPosition.x,
				worldPosition.y,
				worldPosition.z + depthOffsetMeters
			};
		}

		[[nodiscard]] std::filesystem::path NormalizePathForStorage(
		    const std::filesystem::path& filePath)
		{
			if (filePath.empty())
			{
				return {};
			}

			std::error_code error{};
			const std::filesystem::path absolutePath =
			    filePath.is_absolute()
			        ? filePath
			        : std::filesystem::absolute(filePath, error);
			if (error)
			{
				return filePath.lexically_normal();
			}
			return absolutePath.lexically_normal();
		}

		[[nodiscard]] std::wstring ResolvePathRelativeToOwningFile(
		    const std::wstring_view referencedPath,
		    const std::wstring_view owningFilePath)
		{
			const std::filesystem::path reference{ referencedPath };
			if (reference.empty())
			{
				return {};
			}
			if (reference.is_absolute())
			{
				return reference.lexically_normal().wstring();
			}

			const std::filesystem::path normalizedOwningPath =
			    NormalizePathForStorage(std::filesystem::path{ owningFilePath });
			if (!normalizedOwningPath.has_parent_path())
			{
				return reference.lexically_normal().wstring();
			}

			return (normalizedOwningPath.parent_path() / reference)
			    .lexically_normal()
			    .wstring();
		}

		[[nodiscard]] std::wstring BuildPitchSettingsPathReference(
		    const std::wstring_view targetPath,
		    const std::wstring_view owningFilePath)
		{
			const std::filesystem::path normalizedTargetPath =
			    NormalizePathForStorage(std::filesystem::path{ targetPath });
			const std::filesystem::path normalizedOwningPath =
			    NormalizePathForStorage(std::filesystem::path{ owningFilePath });
			if (normalizedTargetPath.empty() || normalizedOwningPath.empty() ||
			    !normalizedOwningPath.has_parent_path())
			{
				return std::wstring{ targetPath };
			}

			std::error_code error{};
			const std::filesystem::path relativePath = std::filesystem::proximate(
			    normalizedTargetPath,
			    normalizedOwningPath.parent_path(),
			    error);
			if (error || relativePath.empty())
			{
				return normalizedTargetPath.wstring();
			}

			return relativePath.wstring();
		}

		[[nodiscard]] std::filesystem::path BuildModuleFilePath()
		{
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD copiedCharacterCount = GetModuleFileNameW(
				    nullptr,
				    buffer.data(),
				    static_cast<DWORD>(buffer.size()));
				if (copiedCharacterCount == 0)
				{
					return {};
				}
				if (copiedCharacterCount < (buffer.size() - 1U))
				{
					buffer.resize(copiedCharacterCount);
					return NormalizePathForStorage(std::filesystem::path{
					    buffer });
				}
				buffer.resize(buffer.size() * 2U);
			}
		}

		[[nodiscard]] bool DirectoryContainsBundledSettings(
		    const std::filesystem::path& candidateDirectory)
		{
			if (candidateDirectory.empty())
			{
				return false;
			}

			std::error_code error{};
			return std::filesystem::exists(
			           candidateDirectory / PitchSettingsJsonFileName,
			           error) &&
			       !error &&
			       std::filesystem::exists(
			           candidateDirectory / DefaultEnvironmentSettingsJsonFileName,
			           error) &&
			       !error &&
			       std::filesystem::exists(
			           candidateDirectory / PlaySettingsJsonFileName,
			           error) &&
			       !error;
		}

		[[nodiscard]] std::filesystem::path FindBundledContentDirectory(
		    const std::filesystem::path& moduleFilePath)
		{
			std::vector<std::filesystem::path> candidates{};
			if (!moduleFilePath.empty())
			{
				std::filesystem::path candidate = moduleFilePath.parent_path();
				for (int depth = 0; depth < 5; ++depth)
				{
					if (candidate.empty())
					{
						break;
					}
					candidates.push_back(candidate);
					const std::filesystem::path parentPath =
					    candidate.parent_path();
					if (parentPath == candidate)
					{
						break;
					}
					candidate = parentPath;
				}
			}

			std::error_code currentPathError{};
			const std::filesystem::path currentPath =
			    std::filesystem::current_path(currentPathError);
			if (!currentPathError && !currentPath.empty())
			{
				candidates.push_back(currentPath);
			}

			for (const std::filesystem::path& candidate : candidates)
			{
				if (DirectoryContainsBundledSettings(candidate))
				{
					return NormalizePathForStorage(candidate);
				}
			}

			return {};
		}

		[[nodiscard]] std::filesystem::path GetLocalAppDataDirectory()
		{
			PWSTR knownFolderPath = nullptr;
			const HRESULT result = SHGetKnownFolderPath(
			    FOLDERID_LocalAppData,
			    KF_FLAG_DEFAULT,
			    nullptr,
			    &knownFolderPath);
			if (FAILED(result) || (knownFolderPath == nullptr))
			{
				if (knownFolderPath != nullptr)
				{
					CoTaskMemFree(knownFolderPath);
				}
				return {};
			}

			const std::filesystem::path localAppDataPath =
			    NormalizePathForStorage(std::filesystem::path{
			        knownFolderPath });
			CoTaskMemFree(knownFolderPath);
			return localAppDataPath;
		}

		[[nodiscard]] bool CopyFileIfMissing(
		    const std::filesystem::path& sourcePath,
		    const std::filesystem::path& destinationPath,
		    std::wstring& errorMessage)
		{
			errorMessage.clear();

			std::error_code existsError{};
			if (std::filesystem::exists(destinationPath, existsError))
			{
				return !existsError;
			}
			if (existsError)
			{
				errorMessage = L"Failed to inspect existing user settings file.";
				return false;
			}

			std::error_code sourceExistsError{};
			if (!std::filesystem::exists(sourcePath, sourceExistsError) ||
			    sourceExistsError)
			{
				errorMessage =
				    L"Bundled default settings file is missing: " +
				    sourcePath.wstring();
				return false;
			}

			std::error_code createDirectoryError{};
			std::filesystem::create_directories(
			    destinationPath.parent_path(),
			    createDirectoryError);
			if (createDirectoryError)
			{
				const std::string errorText = createDirectoryError.message();
				errorMessage =
				    L"Failed to create the user settings directory: " +
				    std::wstring{ errorText.begin(), errorText.end() };
				return false;
			}

			std::error_code copyError{};
			if (!std::filesystem::copy_file(
			        sourcePath,
			        destinationPath,
			        std::filesystem::copy_options::skip_existing,
			        copyError))
			{
				errorMessage = copyError
				                   ? L"Failed to copy the bundled default settings file."
				                   : L"Bundled default settings file was not copied.";
				return false;
			}

			return true;
		}

		[[nodiscard]] bool IsPitchSettingsJsonFileName(
		    const std::wstring_view fileName) noexcept
		{
			if (!fileName.starts_with(PitchSettingsFilePrefix))
			{
				return false;
			}
			if (fileName.size() < 5)
			{
				return false;
			}
			return fileName.substr(fileName.size() - 5) == L".json";
		}
	} // namespace

	Application::Application(std::unique_ptr<Xr::IXrSystem> xrSystem, std::unique_ptr<Render::IRenderBackend> renderBackend, Data::PresetRepository presetRepository)
	    : m_xrSystem(std::move(xrSystem))
	    , m_renderBackend(std::move(renderBackend))
	    , m_presetRepository(std::move(presetRepository))
	    , m_pitchPresetJsonStorage()
	    , m_runtimeSettingsJsonStorage()
	    , m_sessionLogRepository()
	    , m_pitchSimulationService()
	    , m_desktopHudViewModel()
	    , m_frameStatistics()
	{
		m_environmentSettings.pitchDistanceMeters = DefaultPitchDistanceMeters;
		m_environmentSettings.enableMoundHeightOffset = false;
		m_environmentSettings.moundHeightOffsetMeters = 0.18;
		m_environmentSettings.strikeZoneBottomMeters =
		    DefaultStrikeZoneBottomMeters;
		m_environmentSettings.strikeZoneHeightMeters =
		    DefaultStrikeZoneHeightMeters;
		m_environmentSettings.strikeZoneWidthMeters =
		    DefaultStrikeZoneWidthMeters;
		m_environmentSettings.windVelocity = Physics::Vector3{};
		m_environmentSettings.enableHeightDependentWind = false;
		m_environmentSettings.windShearFactor = 0.08;
		m_environmentSettings.batRadiusMeters = 0.035;
		m_environmentSettings.batLengthMeters = 0.84;
		m_environmentSettings.batBallRestitution = 0.50;
		m_environmentSettings.batAngleOffsetDegrees = 0.0;
		m_environmentSettings.batYawOffsetDegrees = 0.0;
		m_environmentSettings.groundFanRadiusMeters = 100.0;
		Common::SetPitchDistanceMeters(m_environmentSettings.pitchDistanceMeters);
		Common::SetStrikeZoneRuntimeSettings(Common::StrikeZoneRuntimeSettings{
		    m_environmentSettings.strikeZoneBottomMeters,
		    m_environmentSettings.strikeZoneHeightMeters,
		    m_environmentSettings.strikeZoneWidthMeters });
		Common::SetGroundRuntimeSettings(Common::GroundRuntimeSettings{
		    m_environmentSettings.groundFanRadiusMeters });
		m_teeBallPosition = Physics::Vector3{
			0.0,
			TeeBallHeightMeters,
			m_environmentSettings.pitchDistanceMeters
		};
		EnsureDefaultPitchProfiles();
		m_activePitchProfileIndex = 0;
		m_randomPitchProfileEnabled = true;
	}

	void Application::EnsureDefaultPitchProfiles()
	{
		if (!m_pitchProfiles.empty())
		{
			return;
		}

		m_pitchProfiles.reserve(DefaultPitchProfiles.size());
		for (const Data::PitchPresetType presetType : DefaultPitchProfiles)
		{
			Data::PitchProfileEntry entry{};
			entry.name = std::wstring{ m_presetRepository.GetPresetName(presetType) };
			entry.pitchParameters = m_presetRepository.LoadPitchParameters(presetType);
			m_pitchProfiles.push_back(entry);
		}
	}

	int Application::Run(HINSTANCE instanceHandle, int commandShow)
	{
		if ((m_xrSystem == nullptr) || (m_renderBackend == nullptr))
		{
			return -1;
		}

		if (!InitializeWindow(instanceHandle, commandShow))
		{
			OutputDebugStringW(L"[Startup] failed to initialize application window.\n");
			MessageBoxW(
			    nullptr,
			    L"Failed to initialize application window.",
			    L"BaseballSimulatorVR startup error",
			    MB_OK | MB_ICONERROR);
			return -2;
		}

		if (!m_xrSystem->Initialize(m_windowHandle))
		{
			OutputDebugStringW(
			    L"[Startup] failed to initialize OpenXR runtime/system resources.\n");
			MessageBoxW(
			    m_windowHandle,
			    L"OpenXR initialization failed.\n"
			    L"Please verify your OpenXR runtime/device setup and restart.",
			    L"BaseballSimulatorVR startup error",
			    MB_OK | MB_ICONERROR);
			Shutdown();
			return -3;
		}

		if (!m_renderBackend->Initialize(m_windowHandle))
		{
			OutputDebugStringW(
			    L"[Startup] failed to initialize desktop rendering backend.\n");
			MessageBoxW(
			    m_windowHandle,
			    L"DX12 render backend initialization failed.\n"
			    L"Please verify your graphics driver/device configuration and restart.",
			    L"BaseballSimulatorVR startup error",
			    MB_OK | MB_ICONERROR);
			Shutdown();
			return -4;
		}

		const Common::GraphicsBinding graphicsBinding =
		    m_renderBackend->GetGraphicsBinding();
		if (!m_xrSystem->AttachGraphicsBinding(graphicsBinding))
		{
			constexpr std::wstring_view xrErrorTitle = L"OpenXR initialization failed";
			const std::wstring xrErrorMessage =
			    L"[OpenXR] graphics binding unavailable; XR runtime is required.\n";
			OutputDebugStringW(xrErrorMessage.c_str());
			MessageBoxW(
			    m_windowHandle,
			    L"OpenXR graphics binding could not be attached.\n"
			    L"Please check your OpenXR runtime/device configuration and restart.",
			    xrErrorTitle.data(),
			    MB_OK | MB_ICONERROR);
			Shutdown();
			return -5;
		}

		const auto failStartup = [&](const int errorCode,
		                             const std::wstring& diagnosticsMessage,
		                             const std::wstring& userMessage) -> int
		{
			OutputDebugStringW(diagnosticsMessage.c_str());
			MessageBoxW(
			    m_windowHandle,
			    userMessage.c_str(),
			    L"BaseballSimulatorVR startup error",
			    MB_OK | MB_ICONERROR);
			Shutdown();
			return errorCode;
		};

		std::wstring storageErrorMessage{};
		if (!InitializeStoragePaths(storageErrorMessage))
		{
			return failStartup(
			    -6,
			    L"[Startup] failed to initialize application storage paths.\n",
			    L"Failed to initialize the application data directory.\n"
			    L"Please verify your user profile permissions and restart.");
		}
		if (!EnsureRequiredUserSettingsFiles(storageErrorMessage))
		{
			const std::wstring diagnostics =
			    L"[Startup] failed to provision required user settings: " +
			    storageErrorMessage + L"\n";
			return failStartup(
			    -7,
			    diagnostics,
			    L"Failed to prepare required user settings files.\n"
			    L"Please verify the packaged defaults and restart.");
		}

		const Data::OperationResult sessionLogInitializationResult =
		    m_sessionLogRepository.Initialize(
		        ResolveUserDataPath(SessionHistoryDatabaseFileName).wstring());
		m_isSessionLogAvailable = sessionLogInitializationResult.succeeded;
		if (!m_isSessionLogAvailable)
		{
			std::wstring logMessage = L"[SessionLog] initialization failed: " +
			                          sessionLogInitializationResult.errorMessage +
			                          L"\n";
			OutputDebugStringW(logMessage.c_str());
		}

		ApplyPreset(m_activePresetType);
		RefreshPitchSettingsFileList();
		const bool hasPitchSettingsFile =
		    !m_pitchSettingsFileNames.empty() ||
		    std::filesystem::exists(ResolveInputFilePath(
		        LegacyPitchSettingsJsonFileName));
		if (!hasPitchSettingsFile)
		{
			return failStartup(
			    -8,
			    L"[Startup] required pitch settings file is missing.\n",
			    L"Required setting file is missing:\n"
			    L"  - PitchSettings.*.json (or legacy PitchPreset.user.json)\n"
			    L"Restore the file and restart.");
		}
		if (!LoadUserPresetFromJson())
		{
			return failStartup(
			    -9,
			    L"[Startup] failed to load required pitch settings JSON.\n",
			    L"Failed to load required pitch settings.\n"
			    L"Please fix missing/invalid required fields in PitchSettings.*.json and restart.");
		}

		const std::filesystem::path requiredEnvironmentSettingsFilePath =
		    m_environmentSettingsFilePath.empty()
		        ? ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName)
		        : ResolveInputFilePath(m_environmentSettingsFilePath);
		if (!std::filesystem::exists(
		        requiredEnvironmentSettingsFilePath))
		{
			const std::wstring diagnostics =
			    L"[Startup] required environment settings file is missing: " +
			    requiredEnvironmentSettingsFilePath.wstring() + L"\n";
			const std::wstring userMessage =
			    L"Required setting file is missing:\n  - " +
			    requiredEnvironmentSettingsFilePath.wstring() +
			    L"\nRestore the file and restart.";
			return failStartup(-10, diagnostics, userMessage);
		}
		if (!LoadEnvironmentSettingsFromJson())
		{
			const std::wstring diagnostics =
			    L"[Startup] failed to load required environment settings JSON: " +
			    requiredEnvironmentSettingsFilePath.wstring() + L"\n";
			const std::wstring userMessage =
			    L"Failed to load required environment settings:\n  - " +
			    requiredEnvironmentSettingsFilePath.wstring() +
			    L"\nPlease fix missing/invalid required fields and restart.";
			return failStartup(-11, diagnostics, userMessage);
		}

		if (!std::filesystem::exists(ResolveUserDataPath(PlaySettingsJsonFileName)))
		{
			return failStartup(
			    -12,
			    L"[Startup] required play settings file is missing.\n",
			    L"Required setting file is missing:\n"
			    L"  - PlaySettings.user.json\n"
			    L"Restore the file and restart.");
		}
		if (!LoadPlaySettingsFromJson())
		{
			return failStartup(
			    -13,
			    L"[Startup] failed to load required play settings JSON.\n",
			    L"Failed to load required play settings.\n"
			    L"Please fix missing/invalid required fields in PlaySettings.user.json and restart.");
		}

		ApplyOverlayVisibilityChanges(L"initial");
		const std::size_t initialFxaaPresetIndex = static_cast<std::size_t>(std::clamp(
		    m_fxaaQualityPresetIndex,
		    0,
		    static_cast<int>(FxaaQualityPresets.size() - 1U)));
		m_renderQuality.fxaaEdgeThreshold =
		    FxaaQualityPresets[initialFxaaPresetIndex].edgeThreshold;
		m_renderQuality.fxaaEdgeThresholdMin =
		    FxaaQualityPresets[initialFxaaPresetIndex].edgeThresholdMin;
		m_renderQuality.fxaaBlendFactor =
		    FxaaQualityPresets[initialFxaaPresetIndex].blendFactor;
		m_fxaaQualityPresetLabel =
		    std::wstring{ FxaaQualityPresets[initialFxaaPresetIndex].label };
		ApplyRenderQualityChanges(L"initial");

		m_isRunning = true;
		MSG message{};
		auto previousTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> simulationAccumulator{ 0.0 };
		std::chrono::duration<double> titleAccumulator{ 0.0 };

		while (m_isRunning)
		{
			while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE)
			{
				if (message.message == WM_QUIT)
				{
					m_isRunning = false;
					break;
				}

				TranslateMessage(&message);
				DispatchMessageW(&message);
			}

			if (!m_isRunning)
			{
				break;
			}

			const auto currentTime = std::chrono::steady_clock::now();
			auto frameDuration = currentTime - previousTime;
			previousTime = currentTime;

			if (frameDuration > MaxFrameTime)
			{
				frameDuration = MaxFrameTime;
			}

			const std::chrono::duration<double> frameSeconds(frameDuration);
			m_lastFrameDeltaSeconds = frameSeconds.count();
			double playbackTimeScale = m_playbackPaused
			                               ? 0.0
			                               : (m_playbackSpeedPercent / 100.0);
			if (!m_playbackPaused &&
			    (m_operationMode == OperationMode::TeeBatting))
			{
				playbackTimeScale = 1.0;
			}
			simulationAccumulator += frameSeconds * playbackTimeScale;
			titleAccumulator += frameSeconds;
			m_frameStatistics.AddFrameSample(frameSeconds);

			while (simulationAccumulator >= FixedSimulationStep)
			{
				const bool isSimulationTickAllowedInMode =
				    (m_operationMode != OperationMode::TeeBatting) ||
				    m_isTeeBallInFlight;
				if (isSimulationTickAllowedInMode &&
				    !IsWaitingForNextDuelPitch())
				{
					m_pitchSimulationService.Tick(FixedSimulationStep);
				}
				simulationAccumulator -= FixedSimulationStep;
			}

			m_xrSystem->SetUiTelemetry(BuildXrUiTelemetry());
			m_xrSystem->PollInput();
			HandleXrUiCommands();

			if (!m_hasStrikeZoneAutoCalibrated)
			{
				const Common::ViewProjectionState eyeState =
				    m_xrSystem->GetLeftEyeViewState();
				const double estimatedPlayerHeight =
				    Common::EstimatePlayerHeightFromEyeHeight(
				        eyeState.position.y);
				if (eyeState.isValid &&
				    (estimatedPlayerHeight >= 1.00) &&
				    (estimatedPlayerHeight <= 2.50))
				{
					m_hasStrikeZoneAutoCalibrated =
					    CalibrateStrikeZoneFromPlayerHeight(
					        L"auto_calibrate_startup");
				}
			}

			RefreshBatPoseFromXrInput();

			Physics::BallState currentBallState = m_pitchSimulationService.GetBallState();
			UpdateModeSpecificBallState(&currentBallState);
			ApplyEnvironmentToBallState(&currentBallState);
			std::optional<Physics::Vector3> markerForRendering = m_duelZonePassMarker;
			const bool canShowDuelPitchPreview =
			    (m_operationMode == OperationMode::BattingDuel) &&
			    m_duelPitchPreviewEnabled &&
			    !m_hasCurrentDuelPitchPassedZone &&
			    !m_hasDuelBallBeenBatted &&
			    m_duelPitchPredictedZonePassPoint.has_value();
			if (canShowDuelPitchPreview)
			{
				const double previewStartThresholdSeconds =
				    (std::max)(0.0, m_duelPitchIntervalSeconds * 0.5);
				const bool isPreviewActiveWhileWaiting =
				    m_isWaitingForNextDuelPitch &&
				    (m_duelPitchCooldownRemainingSeconds <=
				     previewStartThresholdSeconds);
				const bool isPreviewActiveInFlight =
				    !m_isWaitingForNextDuelPitch;
				if (isPreviewActiveWhileWaiting || isPreviewActiveInFlight)
				{
					markerForRendering = m_duelPitchPredictedZonePassPoint;
				}
			}
			if ((m_operationMode == OperationMode::BattingDuel) &&
			    m_hasDuelBallBeenBatted)
			{
				markerForRendering.reset();
			}
			if (markerForRendering.has_value())
			{
				markerForRendering->y +=
				    m_environmentSettings.moundHeightOffsetMeters;
			}
			m_xrSystem->SetUiTelemetry(BuildXrUiTelemetry());
			m_xrSystem->SetBallState(currentBallState);
			m_xrSystem->SetDuelZonePassMarker(markerForRendering);
			m_xrSystem->SetBatOverlayState(m_batOverlayState);

			const Common::ViewProjectionState leftEyeViewState = m_xrSystem->GetLeftEyeViewState();
			m_renderBackend->SetDesktopMirrorView(leftEyeViewState);
			m_renderBackend->SetDuelZonePassMarker(markerForRendering);
			m_renderBackend->SetBatOverlayState(m_batOverlayState);

			m_renderBackend->BeginFrame();
			m_renderBackend->RenderSimulation(currentBallState);
			m_renderBackend->EndFrame();

			if (const auto frameSnapshot = m_frameStatistics.ConsumePendingSnapshot(); frameSnapshot.has_value())
			{
				std::wostringstream frameLog{};
				frameLog << std::fixed << std::setprecision(1);
				frameLog << L"[FrameStats] fps=" << frameSnapshot->framesPerSecond;
				frameLog << L", avg=" << std::setprecision(2) << frameSnapshot->averageFrameTimeMilliseconds << L"ms";
				frameLog << L", max=" << frameSnapshot->maxFrameTimeMilliseconds << L"ms\n";
				const std::wstring frameLogText = frameLog.str();
				OutputDebugStringW(frameLogText.c_str());
			}

			if (titleAccumulator >= WindowTitleUpdateInterval)
			{
				UpdateWindowTitle();
				titleAccumulator = std::chrono::duration<double>::zero();
			}
		}

		Shutdown();
		return static_cast<int>(message.wParam);
	}

	bool Application::InitializeWindow(HINSTANCE instanceHandle, int commandShow)
	{
		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = &Application::StaticWindowProc;
		windowClass.hInstance = instanceHandle;
		windowClass.hIcon = LoadIconW(instanceHandle, MAKEINTRESOURCEW(IDI_BASEBALLSIMULATORVR));
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		windowClass.lpszMenuName = MAKEINTRESOURCEW(IDC_BASEBALLSIMULATORVR);
		windowClass.lpszClassName = WindowClassName.data();
		windowClass.hIconSm = LoadIconW(instanceHandle, MAKEINTRESOURCEW(IDI_SMALL));

		const ATOM classAtom = RegisterClassExW(&windowClass);
		if ((classAtom == 0) && (GetLastError() != ERROR_CLASS_ALREADY_EXISTS))
		{
			return false;
		}

		m_windowHandle = CreateWindowExW(
		    0,
		    WindowClassName.data(),
		    DefaultWindowTitle.data(),
		    WS_OVERLAPPEDWINDOW,
		    CW_USEDEFAULT,
		    0,
		    1280,
		    720,
		    nullptr,
		    nullptr,
		    instanceHandle,
		    this);
		if (m_windowHandle == nullptr)
		{
			return false;
		}

		ShowWindow(m_windowHandle, commandShow);
		UpdateWindow(m_windowHandle);
		return true;
	}

	void Application::ApplyPreset(Data::PitchPresetType presetType)
	{
		m_activePresetType = presetType;
		m_activePitchParameters = m_presetRepository.LoadPitchParameters(presetType);
		m_randomPitchProfileEnabled = false;
		switch (presetType)
		{
			case Data::PitchPresetType::Fastball:
				m_activePitchProfileIndex = 0;
				break;
			case Data::PitchPresetType::Slider:
				m_activePitchProfileIndex = 1;
				break;
			case Data::PitchPresetType::Curveball:
				m_activePitchProfileIndex = 2;
				break;
			case Data::PitchPresetType::Changeup:
				m_activePitchProfileIndex = 3;
				break;
			case Data::PitchPresetType::Custom:
			default:
				break;
		}
		if ((m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)]
			    .pitchParameters = m_activePitchParameters;
		}
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"preset_apply");
	}

	void Application::ApplyPitchParameterChanges(
	    const bool preserveDirectionalVelocityForCurrentPitch)
	{
		m_activePitchParameters.releaseHeightMeters = std::clamp(
		    m_activePitchParameters.releaseHeightMeters,
		    MinimumReleaseHeightMeters,
		    MaximumReleaseHeightMeters);
		const double initialSpeed = std::clamp(
		    std::abs(m_activePitchParameters.initialVelocity.z),
		    MinimumForwardSpeed,
		    MaximumForwardSpeed);
		const double clampedLateralVelocity = std::clamp(
		    m_activePitchParameters.initialVelocity.x,
		    MinimumHorizontalSpeed,
		    MaximumHorizontalSpeed);
		const double clampedVerticalVelocity = std::clamp(
		    m_activePitchParameters.initialVelocity.y,
		    MinimumVerticalSpeed,
		    MaximumVerticalSpeed);
		m_activePitchParameters.initialVelocity =
		    preserveDirectionalVelocityForCurrentPitch
		        ? Physics::Vector3{
			          clampedLateralVelocity,
			          clampedVerticalVelocity,
			          initialSpeed
		          }
		        : Physics::Vector3{ 0.0, 0.0, initialSpeed };
		m_activePitchParameters.integratorType =
		    m_environmentSettings.integratorType;
		m_activePitchParameters.windVelocity =
		    m_environmentSettings.windVelocity;
		m_activePitchParameters.enableHeightDependentWind =
		    m_environmentSettings.enableHeightDependentWind;
		m_activePitchParameters.windShearFactor =
		    m_environmentSettings.windShearFactor;
		m_pitchMetricsSpeedMetersPerSecond =
		    Physics::Length(m_activePitchParameters.initialVelocity);
		if ((m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			Data::PitchProfileEntry& activePitchProfile =
			    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)];
			activePitchProfile.pitchParameters = m_activePitchParameters;
			// Stored profiles keep the single-speed format; zone-solve velocity biases
			// are only applied to the currently simulated pitch.
			activePitchProfile.pitchParameters.initialVelocity = Physics::Vector3{
				0.0,
				0.0,
				initialSpeed
			};
		}

		m_pitchSimulationService.Reset(m_activePitchParameters);

		std::wostringstream pitchLog{};
		pitchLog << std::fixed << std::setprecision(2);
		pitchLog << L"[PitchConfig] preset=" << m_presetRepository.GetPresetName(m_activePresetType);
		pitchLog << L", integrator=" << Physics::GetPitchIntegratorName(m_activePitchParameters.integratorType);
		pitchLog << L", table=" << (m_activePitchParameters.useCoefficientTable ? L"On" : L"Off");
		pitchLog << L", speed=" << m_activePitchParameters.initialVelocity.z;
		pitchLog << L", releaseH=" << m_activePitchParameters.releaseHeightMeters;
		pitchLog << L", spinRpm=" << std::setprecision(0) << m_activePitchParameters.spinRpm;
		pitchLog << std::setprecision(2);
		pitchLog << L", windVel=(" << m_environmentSettings.windVelocity.x << L", " << m_environmentSettings.windVelocity.y << L", " << m_environmentSettings.windVelocity.z << L")";
		pitchLog << L", heightWind=" << (m_environmentSettings.enableHeightDependentWind ? L"On" : L"Off");
		pitchLog << L", windShear=" << m_environmentSettings.windShearFactor;
		pitchLog << L", seam=" << (m_activePitchParameters.enableSeamPerturbation ? L"On" : L"Off");
		pitchLog << L", seamAmp=" << m_activePitchParameters.seamPerturbationAmplitude;
		pitchLog << L", seamFreq=" << m_activePitchParameters.seamPerturbationFrequencyHz << L"\n";
		OutputDebugStringW(pitchLog.str().c_str());
	}

	void Application::ApplyPitchParameterChangesAndLog(
	    const std::wstring_view eventType,
	    const std::wstring_view note,
	    const bool preserveDirectionalVelocityForCurrentPitch)
	{
		ApplyPitchParameterChanges(preserveDirectionalVelocityForCurrentPitch);
		(void)LogPitchEvent(eventType, note);
		UpdateWindowTitle();
	}

	void Application::HandleKeyDown(WPARAM keyCode)
	{
		switch (keyCode)
		{
			case VK_F1:
				ApplyPreset(Data::PitchPresetType::Fastball);
				break;
			case VK_F2:
				ApplyPreset(Data::PitchPresetType::Slider);
				break;
			case VK_F3:
				ApplyPreset(Data::PitchPresetType::Curveball);
				break;
			case VK_F4:
				ApplyPreset(Data::PitchPresetType::Changeup);
				break;
			case VK_F5:
			case '5':
				AdjustFxaaQualityPreset(-1);
				break;
			case VK_F6:
			case '6':
				AdjustFxaaQualityPreset(1);
				break;
			case VK_F7:
			case '7':
				AdjustTrailOpacityScale(-TrailOpacityScaleStep);
				break;
			case VK_F8:
			case '8':
				AdjustTrailOpacityScale(TrailOpacityScaleStep);
				break;
			case VK_F9:
				SetOperationMode(OperationMode::PitchTrajectory, L"key_mode_f9");
				break;
			case VK_F10:
				if (m_operationMode == OperationMode::BattingDuel)
				{
					const bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
					AdjustDuelPitchInterval(
					    isShiftPressed ? -DuelPitchIntervalStepSeconds
					                   : DuelPitchIntervalStepSeconds,
					    isShiftPressed ? L"key_shift_f10" : L"key_f10");
				}
				else
				{
					SetOperationMode(OperationMode::BattingDuel, L"key_mode_f10");
				}
				break;
			case VK_F11:
				SetOperationMode(OperationMode::TeeBatting, L"key_mode_f11");
				break;
			case VK_F12:
				ToggleMoundHeightOffset();
				break;
			case VK_OEM_4:
				AdjustPitchDistance(-PitchDistanceStepMeters);
				break;
			case VK_OEM_6:
				AdjustPitchDistance(PitchDistanceStepMeters);
				break;
			case '1':
				ToggleTrajectoryVisibility();
				break;
			case '2':
				ToggleStrikeZoneVisibility();
				break;
			case '3':
				ToggleFloorMeshVisibility();
				break;
			case 'Q':
				ToggleDuelPitchPreview(L"key_q");
				break;
			case 'E':
				ToggleBatHandedness(L"key_e");
				break;
			case VK_OEM_COMMA:
				AdjustBatAngleOffset(-BatAngleOffsetStepDegrees, L"key_comma");
				break;
			case VK_OEM_PERIOD:
				AdjustBatAngleOffset(BatAngleOffsetStepDegrees, L"key_period");
				break;
			case VK_OEM_1:
				AdjustBatYawOffset(-BatYawOffsetStepDegrees, L"key_semicolon");
				break;
			case VK_OEM_7:
				AdjustBatYawOffset(BatYawOffsetStepDegrees, L"key_quote");
				break;
			case 'R':
				ApplyPitchParameterChangesAndLog(
				    L"pitch_config",
				    L"key_r");
				break;
			case 'I':
				ToggleIntegrator();
				break;
			case 'Z':
				ToggleHeightDependentWind();
				break;
			case 'X':
				ToggleSeamPerturbation();
				break;
			case VK_UP:
				AdjustForwardSpeed(0.5);
				break;
			case VK_DOWN:
				AdjustForwardSpeed(-0.5);
				break;
			case VK_RIGHT:
				AdjustHorizontalSpeed(0.25);
				break;
			case VK_LEFT:
				AdjustHorizontalSpeed(-0.25);
				break;
			case VK_PRIOR:
				AdjustSpin(100.0);
				break;
			case VK_NEXT:
				AdjustSpin(-100.0);
				break;
			case 'W':
				AdjustWindForward(0.10);
				break;
			case 'S':
				AdjustWindForward(-0.10);
				break;
			case 'D':
				AdjustWindCross(0.10);
				break;
			case 'A':
				AdjustWindCross(-0.10);
				break;
			case 'T':
				AdjustWindShearFactor(0.01);
				break;
			case 'G':
				AdjustWindShearFactor(-0.01);
				break;
			case 'Y':
				AdjustSeamAmplitude(0.01);
				break;
			case 'H':
				AdjustSeamAmplitude(-0.01);
				break;
			case 'U':
				AdjustSeamFrequency(1.0);
				break;
			case 'J':
				AdjustSeamFrequency(-1.0);
				break;
			case VK_OEM_MINUS:
			case VK_SUBTRACT:
				AdjustPlaybackSpeedPercent(-PlaybackSpeedStepPercent);
				break;
			case VK_OEM_PLUS:
			case VK_ADD:
				AdjustPlaybackSpeedPercent(PlaybackSpeedStepPercent);
				break;
			case '0':
				ResetPlaybackSpeedPercent();
				break;
			case 'K':
				SaveUserPresetToJson();
				break;
			case 'L':
				if (!LoadUserPresetFromJson())
				{
					OutputDebugStringW(L"[PitchPreset] key load skipped: preset file invalid.\n");
				}
				break;
			case VK_NUMPAD4:
				AdjustTeeBallPosition(-TeeBallAdjustStepX, 0.0, 0.0);
				break;
			case VK_NUMPAD6:
				AdjustTeeBallPosition(TeeBallAdjustStepX, 0.0, 0.0);
				break;
			case VK_NUMPAD8:
				AdjustTeeBallPosition(0.0, 0.0, -TeeBallAdjustStepZ);
				break;
			case VK_NUMPAD2:
				AdjustTeeBallPosition(0.0, 0.0, TeeBallAdjustStepZ);
				break;
			case VK_NUMPAD9:
				AdjustTeeBallPosition(0.0, TeeBallAdjustStepY, 0.0);
				break;
			case VK_NUMPAD3:
				AdjustTeeBallPosition(0.0, -TeeBallAdjustStepY, 0.0);
				break;
			case VK_NUMPAD5:
				ResetTeeBallPosition();
				break;
			case 'C':
				TagLatestPitchEventResult(L"strike", 1.0, L"key_C");
				break;
			case 'V':
				TagLatestPitchEventResult(L"ball", -1.0, L"key_V");
				break;
			case 'B':
				TagLatestPitchEventResult(L"in_play", 0.5, L"key_B");
				break;
			case 'N':
				TagLatestPitchEventResult(L"foul", 0.0, L"key_N");
				break;
			case 'M':
				TagLatestPitchEventResult(L"pending", 0.0, L"key_M");
				break;
			case 'O':
				AdjustLatestPitchEventResultScore(-0.10);
				break;
			case 'P':
				AdjustLatestPitchEventResultScore(0.10);
				break;
			default:
				break;
		}
	}

	void Application::HandleXrUiCommands()
	{
		const std::vector<Xr::UiCommand> uiCommands =
		    m_xrSystem->ConsumePendingUiCommands();
		for (const Xr::UiCommand& uiCommand : uiCommands)
		{
			HandleXrUiCommand(uiCommand);
		}
	}

	void Application::HandleXrUiCommand(const Xr::UiCommand& uiCommand)
	{
		switch (uiCommand.type)
		{
			case Xr::UiCommandType::SetModeTrajectory:
				SetOperationMode(OperationMode::PitchTrajectory, L"xr_mode");
				break;
			case Xr::UiCommandType::SetModeBattingDuel:
				SetOperationMode(OperationMode::BattingDuel, L"xr_mode");
				break;
			case Xr::UiCommandType::SetModeTeeBatting:
				SetOperationMode(OperationMode::TeeBatting, L"xr_mode");
				break;
			case Xr::UiCommandType::SetPresetFastball:
				ApplyPreset(Data::PitchPresetType::Fastball);
				break;
			case Xr::UiCommandType::SetPresetSlider:
				ApplyPreset(Data::PitchPresetType::Slider);
				break;
			case Xr::UiCommandType::SetPresetCurveball:
				ApplyPreset(Data::PitchPresetType::Curveball);
				break;
			case Xr::UiCommandType::SetPresetChangeup:
				ApplyPreset(Data::PitchPresetType::Changeup);
				break;
			case Xr::UiCommandType::ToggleTrajectoryVisibility:
				ToggleTrajectoryVisibility();
				break;
			case Xr::UiCommandType::ToggleStrikeZoneVisibility:
				ToggleStrikeZoneVisibility();
				break;
			case Xr::UiCommandType::AdjustDuelIntervalDown:
				AdjustDuelPitchInterval(-DuelPitchIntervalStepSeconds, L"xr_main_panel");
				break;
			case Xr::UiCommandType::AdjustDuelIntervalUp:
				AdjustDuelPitchInterval(DuelPitchIntervalStepSeconds, L"xr_main_panel");
				break;
			case Xr::UiCommandType::AdjustPlaybackSpeedDown:
				AdjustPlaybackSpeedPercent(-PlaybackSpeedStepPercent);
				break;
			case Xr::UiCommandType::AdjustPlaybackSpeedUp:
				AdjustPlaybackSpeedPercent(PlaybackSpeedStepPercent);
				break;
			case Xr::UiCommandType::ResetPlaybackSpeed:
				ResetPlaybackSpeedPercent();
				break;
			case Xr::UiCommandType::ToggleMoundHeightOffset:
				ToggleMoundHeightOffset();
				break;
			case Xr::UiCommandType::AdjustPitchDistanceDown:
				AdjustPitchDistance(-PitchDistanceStepMeters);
				break;
			case Xr::UiCommandType::AdjustPitchDistanceUp:
				AdjustPitchDistance(PitchDistanceStepMeters);
				break;
			case Xr::UiCommandType::AdjustForwardSpeedDown:
				AdjustForwardSpeed(-0.5);
				break;
			case Xr::UiCommandType::AdjustForwardSpeedUp:
				AdjustForwardSpeed(0.5);
				break;
			case Xr::UiCommandType::AdjustSpinDown:
				AdjustSpin(-100.0);
				break;
			case Xr::UiCommandType::AdjustSpinUp:
				AdjustSpin(100.0);
				break;
			case Xr::UiCommandType::ToggleDuelPitchPreview:
				ToggleDuelPitchPreview(L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::ToggleBatHandedness:
				ToggleBatHandedness(L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::AdjustBatAngleDown:
				AdjustBatAngleOffset(-BatAngleOffsetStepDegrees, L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::AdjustBatAngleUp:
				AdjustBatAngleOffset(BatAngleOffsetStepDegrees, L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::AdjustBatYawDown:
				AdjustBatYawOffset(-BatYawOffsetStepDegrees, L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::AdjustBatYawUp:
				AdjustBatYawOffset(BatYawOffsetStepDegrees, L"xr_metrics_panel");
				break;
			case Xr::UiCommandType::SetDuelTargetZone:
				SetDuelTargetZoneBias(uiCommand.intValue);
				break;
			case Xr::UiCommandType::ClearDuelTargetZoneBias:
				ClearDuelTargetZoneBias();
				break;
			case Xr::UiCommandType::SelectPitchProfile:
				SelectPitchProfile(uiCommand.intValue, L"xr_pitch_panel");
				break;
			case Xr::UiCommandType::SelectPreviousPitchProfile:
				SelectPitchProfileByOffset(-1, L"xr_pitch_rotate");
				break;
			case Xr::UiCommandType::SelectNextPitchProfile:
				SelectPitchProfileByOffset(1, L"xr_pitch_rotate");
				break;
			case Xr::UiCommandType::SelectPreviousPitchSettingsFile:
				SelectPitchSettingsFileByOffset(-1, L"xr_pitch_file");
				break;
			case Xr::UiCommandType::SelectNextPitchSettingsFile:
				SelectPitchSettingsFileByOffset(1, L"xr_pitch_file");
				break;
			case Xr::UiCommandType::ToggleRandomPitchProfile:
				ToggleRandomPitchProfile(L"xr_pitch_panel");
				break;
			case Xr::UiCommandType::TogglePlaybackPause:
				TogglePlaybackPause(L"xr_pitch_panel");
				break;
			case Xr::UiCommandType::SetPlaybackSpeedPercent:
			{
				const double clampedPercent = std::clamp(
				    static_cast<double>(uiCommand.intValue),
				    MinimumPlaybackSpeedPercent,
				    MaximumPlaybackSpeedPercent);
				const double snappedPercent =
				    std::round(clampedPercent / PlaybackSpeedStepPercent) *
				    PlaybackSpeedStepPercent;
				AdjustPlaybackSpeedPercent(snappedPercent - m_playbackSpeedPercent);
				break;
			}
			case Xr::UiCommandType::RecalibrateStrikeZone:
				if (CalibrateStrikeZoneFromPlayerHeight(L"xr_recalibrate"))
				{
					m_hasStrikeZoneAutoCalibrated = true;
				}
				break;
			default:
				break;
		}
	}

	void Application::ToggleIntegrator()
	{
		if (m_environmentSettings.integratorType ==
		    Physics::PitchIntegratorType::Rk4)
		{
			m_environmentSettings.integratorType =
			    Physics::PitchIntegratorType::SemiImplicitEuler;
		}
		else
		{
			m_environmentSettings.integratorType =
			    Physics::PitchIntegratorType::Rk4;
		}

		std::wstring note = L"integrator=";
		note += std::wstring{
			Physics::GetPitchIntegratorName(
			    m_environmentSettings.integratorType)
		};
		ApplyEnvironmentSettingsChanges(note);
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    note);
	}

	void Application::ToggleHeightDependentWind()
	{
		m_environmentSettings.enableHeightDependentWind =
		    !m_environmentSettings.enableHeightDependentWind;
		ApplyEnvironmentSettingsChanges(L"toggle_height_wind");
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"toggle_height_wind");
	}

	void Application::ToggleSeamPerturbation()
	{
		m_activePitchParameters.enableSeamPerturbation = !m_activePitchParameters.enableSeamPerturbation;
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"toggle_seam");
	}

	void Application::AdjustForwardSpeed(double deltaMetersPerSecond)
	{
		const double nextSpeed = std::clamp(
		    std::abs(m_activePitchParameters.initialVelocity.z) +
		        deltaMetersPerSecond,
		    MinimumForwardSpeed,
		    MaximumForwardSpeed);
		m_activePitchParameters.initialVelocity.z = nextSpeed;
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_forward_speed");
	}

	void Application::AdjustHorizontalSpeed(double deltaMetersPerSecond)
	{
		(void)deltaMetersPerSecond;
		OutputDebugStringW(
		    L"[PitchConfig] horizontal initial direction is fixed toward catcher.\n");
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"initial_direction_fixed");
	}

	void Application::AdjustSpin(double deltaRpm)
	{
		m_activePitchParameters.spinRpm = std::clamp(m_activePitchParameters.spinRpm + deltaRpm, MinimumSpinRpm, MaximumSpinRpm);
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_spin");
	}

	void Application::AdjustWindForward(double deltaMetersPerSecond)
	{
		m_environmentSettings.windVelocity.z = std::clamp(
		    m_environmentSettings.windVelocity.z + deltaMetersPerSecond,
		    MinimumWindForwardVelocity,
		    MaximumWindForwardVelocity);
		ApplyEnvironmentSettingsChanges(L"adjust_wind_forward");
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_wind_forward");
	}

	void Application::AdjustWindCross(double deltaMetersPerSecond)
	{
		m_environmentSettings.windVelocity.x = std::clamp(
		    m_environmentSettings.windVelocity.x + deltaMetersPerSecond,
		    MinimumWindCrossVelocity,
		    MaximumWindCrossVelocity);
		ApplyEnvironmentSettingsChanges(L"adjust_wind_cross");
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_wind_cross");
	}

	void Application::AdjustWindShearFactor(double delta)
	{
		m_environmentSettings.windShearFactor = std::clamp(
		    m_environmentSettings.windShearFactor + delta,
		    MinimumWindShearFactor,
		    MaximumWindShearFactor);
		ApplyEnvironmentSettingsChanges(L"adjust_wind_shear");
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_wind_shear");
	}

	void Application::AdjustSeamAmplitude(double delta)
	{
		m_activePitchParameters.seamPerturbationAmplitude =
		    std::clamp(m_activePitchParameters.seamPerturbationAmplitude + delta, MinimumSeamAmplitude, MaximumSeamAmplitude);
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_seam_amp");
	}

	void Application::AdjustSeamFrequency(double deltaHertz)
	{
		m_activePitchParameters.seamPerturbationFrequencyHz =
		    std::clamp(m_activePitchParameters.seamPerturbationFrequencyHz + deltaHertz, MinimumSeamFrequency, MaximumSeamFrequency);
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"adjust_seam_freq");
	}

	void Application::AdjustPlaybackSpeedPercent(const double deltaPercent)
	{
		const double nextPlaybackSpeedPercent = std::clamp(
		    m_playbackSpeedPercent + deltaPercent,
		    MinimumPlaybackSpeedPercent,
		    MaximumPlaybackSpeedPercent);
		if (std::abs(nextPlaybackSpeedPercent - m_playbackSpeedPercent) <= 0.001)
		{
			return;
		}

		m_playbackSpeedPercent = nextPlaybackSpeedPercent;
		std::wostringstream noteStream{};
		noteStream << std::fixed << std::setprecision(0) << m_playbackSpeedPercent;
		const std::wstring noteText = L"playback=" + noteStream.str() + L"%";

		std::wstring logMessage = L"[Playback] speed set to " + noteStream.str() + L"%\n";
		OutputDebugStringW(logMessage.c_str());
		(void)LogPitchEvent(L"playback_speed", noteText);
		UpdateWindowTitle();
		SavePlaySettingsToJson();
	}

	void Application::ResetPlaybackSpeedPercent()
	{
		const double deltaToDefault = 100.0 - m_playbackSpeedPercent;
		AdjustPlaybackSpeedPercent(deltaToDefault);
	}

	void Application::TogglePlaybackPause(const std::wstring_view note)
	{
		if (m_playbackPaused &&
		    (m_operationMode == OperationMode::PitchTrajectory))
		{
			const Physics::BallState currentBallState =
			    m_pitchSimulationService.GetBallState();
			const double replayDepthThreshold =
			    m_environmentSettings.pitchDistanceMeters * 1.10;
			if (currentBallState.position.z >=
			    (replayDepthThreshold - 1e-3))
			{
				m_pitchSimulationService.Reset(m_activePitchParameters);
				m_forceTrajectoryResetOnNextFrame = true;
				m_duelZonePassMarker.reset();
				m_hasPreviousDuelBallState = false;
			}
		}
		m_playbackPaused = !m_playbackPaused;
		std::wstring playbackNote = L"playback_state=";
		playbackNote += m_playbackPaused ? L"paused" : L"running";
		if (!note.empty())
		{
			playbackNote += L"," + std::wstring{ note };
		}
		(void)LogPitchEvent(L"playback_state", playbackNote);
		UpdateWindowTitle();
	}

	void Application::SaveUserPresetToJson()
	{
		Data::PitchRuntimeSettings settings{};
		settings.pitchParameters = m_activePitchParameters;
		settings.environmentSettingsFile =
		    m_environmentSettingsFilePath.empty()
		        ? std::wstring{ DefaultEnvironmentSettingsJsonFileName }
		        : BuildPitchSettingsPathReference(
		              m_environmentSettingsFilePath,
		              m_activePitchSettingsFilePath);
		settings.pitchProfiles = m_pitchProfiles;
		settings.activePitchProfileIndex = m_activePitchProfileIndex;
		settings.randomPitchProfileEnabled = m_randomPitchProfileEnabled;
		const std::filesystem::path pitchSettingsFilePath =
		    !m_activePitchSettingsFilePath.empty()
		        ? ResolveInputFilePath(m_activePitchSettingsFilePath)
		        : ResolveUserDataPath(std::wstring{
		              IsPitchSettingsJsonFileName(m_activePitchSettingsFileName)
		                  ? m_activePitchSettingsFileName
		                  : std::wstring{ PitchSettingsJsonFileName } });

		std::wstring errorMessage{};
		if (!m_pitchPresetJsonStorage.SavePitchSettings(
		        pitchSettingsFilePath.wstring(),
		        settings,
		        errorMessage))
		{
			std::wstring logMessage = L"[PitchConfig] failed to save pitch settings: " +
			                          errorMessage + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return;
		}

		m_activePitchSettingsFilePath = pitchSettingsFilePath.wstring();
		m_activePitchSettingsFileName = pitchSettingsFilePath.filename().wstring();
		RefreshPitchSettingsFileList();
		std::wstring saveNote = L"Saved " + m_activePitchSettingsFileName;
		(void)LogPitchEvent(L"json_save", saveNote);
		std::wstring saveLog =
		    L"[PitchConfig] pitch settings saved to " + m_activePitchSettingsFilePath + L"\n";
		OutputDebugStringW(saveLog.c_str());
	}

	void Application::RefreshPitchSettingsFileList()
	{
		std::vector<std::wstring> discoveredFileNames{};
		const std::filesystem::path settingsDirectory =
		    ResolveUserDataPath(L".");
		if (settingsDirectory.empty())
		{
			std::wstring logMessage =
			    L"[PitchConfig] failed to enumerate pitch settings directory.\n";
			OutputDebugStringW(logMessage.c_str());
			m_pitchSettingsFileNames.clear();
			m_activePitchSettingsFileIndex = -1;
			return;
		}

		std::error_code iteratorError{};
		for (std::filesystem::directory_iterator iterator(
		         settingsDirectory,
		         std::filesystem::directory_options::skip_permission_denied,
		         iteratorError);
		     !iteratorError &&
		     (iterator != std::filesystem::directory_iterator{});
		     iterator.increment(iteratorError))
		{
			const std::filesystem::directory_entry& entry = *iterator;
			std::error_code entryError{};
			if (!entry.is_regular_file(entryError))
			{
				continue;
			}
			const std::wstring fileName = entry.path().filename().wstring();
			if (!IsPitchSettingsJsonFileName(fileName))
			{
				continue;
			}
			discoveredFileNames.push_back(fileName);
		}
		if (iteratorError)
		{
			const std::string errorText = iteratorError.message();
			std::wstring logMessage = L"[PitchConfig] pitch settings enumeration warning: " +
			                          std::wstring{ errorText.begin(), errorText.end() } +
			                          L"\n";
			OutputDebugStringW(logMessage.c_str());
		}

		std::sort(discoveredFileNames.begin(), discoveredFileNames.end());
		discoveredFileNames.erase(
		    std::unique(discoveredFileNames.begin(), discoveredFileNames.end()),
		    discoveredFileNames.end());
		m_pitchSettingsFileNames = std::move(discoveredFileNames);
		m_activePitchSettingsFileIndex = -1;

		if (!m_activePitchSettingsFileName.empty())
		{
			const auto currentFileIterator = std::find(
			    m_pitchSettingsFileNames.begin(),
			    m_pitchSettingsFileNames.end(),
			    m_activePitchSettingsFileName);
			if (currentFileIterator != m_pitchSettingsFileNames.end())
			{
				m_activePitchSettingsFileIndex = static_cast<int>(std::distance(
				    m_pitchSettingsFileNames.begin(),
				    currentFileIterator));
			}
		}
		if ((m_activePitchSettingsFileIndex < 0) &&
		    !m_pitchSettingsFileNames.empty())
		{
			const auto defaultFileIterator = std::find(
			    m_pitchSettingsFileNames.begin(),
			    m_pitchSettingsFileNames.end(),
			    std::wstring{ PitchSettingsJsonFileName });
			if (defaultFileIterator != m_pitchSettingsFileNames.end())
			{
				m_activePitchSettingsFileIndex = static_cast<int>(std::distance(
				    m_pitchSettingsFileNames.begin(),
				    defaultFileIterator));
			}
			else
			{
				m_activePitchSettingsFileIndex = 0;
			}
		}
		if ((m_activePitchSettingsFileIndex >= 0) &&
		    (m_activePitchSettingsFileIndex <
		     static_cast<int>(m_pitchSettingsFileNames.size())))
		{
			m_activePitchSettingsFileName =
			    m_pitchSettingsFileNames[static_cast<std::size_t>(
			        m_activePitchSettingsFileIndex)];
		}
	}

	bool Application::LoadUserPresetFromJsonFile(
	    const std::wstring_view filePath,
	    const std::wstring_view loadEventNote)
	{
		const std::filesystem::path normalizedPitchSettingsPath =
		    ResolveInputFilePath(filePath);
		std::wstring errorMessage{};
		const std::optional<Data::PitchRuntimeSettings> loadedSettings =
		    m_pitchPresetJsonStorage.LoadPitchSettings(
		        normalizedPitchSettingsPath.wstring(),
		        errorMessage);
		if (!loadedSettings.has_value())
		{
			std::wstring logMessage =
			    L"[PitchConfig] failed to load pitch settings (" +
			    normalizedPitchSettingsPath.wstring() + L"): " + errorMessage + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return false;
		}

		m_activePitchParameters = loadedSettings->pitchParameters;
		if (!loadedSettings->environmentSettingsFile.empty())
		{
			m_environmentSettingsFilePath =
			    ResolvePathRelativeToOwningFile(
			        loadedSettings->environmentSettingsFile,
			        normalizedPitchSettingsPath.wstring());
		}
		else
		{
			m_environmentSettingsFilePath =
			    ResolvePathRelativeToOwningFile(
			        DefaultEnvironmentSettingsJsonFileName,
			        normalizedPitchSettingsPath.wstring());
		}
		if (!loadedSettings->pitchProfiles.empty())
		{
			m_pitchProfiles = loadedSettings->pitchProfiles;
		}
		else if (m_pitchProfiles.empty())
		{
			Data::PitchProfileEntry fallbackProfile{};
			fallbackProfile.name = L"Custom";
			fallbackProfile.pitchParameters = loadedSettings->pitchParameters;
			m_pitchProfiles.push_back(fallbackProfile);
		}
		if (m_pitchProfiles.empty())
		{
			Data::PitchProfileEntry fallbackProfile{};
			fallbackProfile.name = L"Fastball";
			fallbackProfile.pitchParameters =
			    m_presetRepository.LoadPitchParameters(
			        Data::PitchPresetType::Fastball);
			m_pitchProfiles.push_back(fallbackProfile);
		}
		m_activePitchProfileIndex = std::clamp(
		    loadedSettings->activePitchProfileIndex,
		    0,
		    static_cast<int>(m_pitchProfiles.size()) - 1);
		m_randomPitchProfileEnabled =
		    loadedSettings->randomPitchProfileEnabled;
		if ((m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			m_activePitchParameters =
			    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)]
			        .pitchParameters;
		}
		m_activePresetType = Data::PitchPresetType::Custom;
		const std::wstring loadedFileName =
		    normalizedPitchSettingsPath.filename().wstring();
		m_activePitchSettingsFilePath = normalizedPitchSettingsPath.wstring();
		m_activePitchSettingsFileName = loadedFileName;
		RefreshPitchSettingsFileList();
		const std::wstring loadNote = std::wstring{ L"Loaded " } + loadedFileName;
		ApplyPitchParameterChangesAndLog(
		    L"json_load",
		    loadNote);
		if (!loadEventNote.empty())
		{
			(void)LogPitchEvent(L"pitch_settings_file", std::wstring{ loadEventNote });
		}
		std::wstring logMessage =
		    L"[PitchConfig] loaded from " + loadedFileName +
		    L" (environment=" + m_environmentSettingsFilePath + L")\n";
		OutputDebugStringW(logMessage.c_str());
		return true;
	}

	bool Application::LoadUserPresetFromJson()
	{
		RefreshPitchSettingsFileList();
		if (!m_pitchSettingsFileNames.empty())
		{
			if ((m_activePitchSettingsFileIndex >= 0) &&
			    (m_activePitchSettingsFileIndex <
			     static_cast<int>(m_pitchSettingsFileNames.size())))
			{
				return LoadUserPresetFromJsonFile(
				    m_pitchSettingsFileNames[static_cast<std::size_t>(
				        m_activePitchSettingsFileIndex)]);
			}
			return LoadUserPresetFromJsonFile(m_pitchSettingsFileNames.front());
		}

		if (std::filesystem::exists(ResolveInputFilePath(
		        LegacyPitchSettingsJsonFileName)))
		{
			return LoadUserPresetFromJsonFile(LegacyPitchSettingsJsonFileName);
		}

		return false;
	}

	void Application::SaveEnvironmentSettingsToJson()
	{
		Data::EnvironmentRuntimeSettings settings{};
		settings.pitchDistanceMeters = m_environmentSettings.pitchDistanceMeters;
		settings.enableMoundHeightOffset = m_environmentSettings.enableMoundHeightOffset;
		settings.moundHeightOffsetMeters = m_environmentSettings.moundHeightOffsetMeters;
		settings.strikeZoneBottomMeters =
		    m_environmentSettings.strikeZoneBottomMeters;
		settings.strikeZoneHeightMeters =
		    m_environmentSettings.strikeZoneHeightMeters;
		settings.strikeZoneWidthMeters =
		    m_environmentSettings.strikeZoneWidthMeters;
		settings.windVelocity = m_environmentSettings.windVelocity;
		settings.enableHeightDependentWind =
		    m_environmentSettings.enableHeightDependentWind;
		settings.windShearFactor = m_environmentSettings.windShearFactor;
		settings.batRadiusMeters = m_environmentSettings.batRadiusMeters;
		settings.batLengthMeters = m_environmentSettings.batLengthMeters;
		settings.batBallRestitution =
		    m_environmentSettings.batBallRestitution;
		settings.batAngleOffsetDegrees =
		    m_environmentSettings.batAngleOffsetDegrees;
		settings.batYawOffsetDegrees =
		    m_environmentSettings.batYawOffsetDegrees;
		settings.groundFanRadiusMeters =
		    m_environmentSettings.groundFanRadiusMeters;
		settings.integratorType = m_environmentSettings.integratorType;
		const std::wstring environmentSettingsFilePath =
		    m_environmentSettingsFilePath.empty()
		        ? ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName).wstring()
		        : ResolveInputFilePath(m_environmentSettingsFilePath).wstring();

		std::wstring errorMessage{};
		if (!m_runtimeSettingsJsonStorage.SaveEnvironment(
		        environmentSettingsFilePath,
		        settings,
		        errorMessage))
		{
			std::wstring logMessage =
			    L"[Environment] failed to save settings JSON: " + errorMessage + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return;
		}
	}

	bool Application::LoadEnvironmentSettingsFromJson()
	{
		std::wstring environmentSettingsFilePath =
		    m_environmentSettingsFilePath.empty()
		        ? ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName).wstring()
		        : ResolveInputFilePath(m_environmentSettingsFilePath).wstring();
		if (!std::filesystem::exists(
		        std::filesystem::path(environmentSettingsFilePath)))
		{
			const std::wstring logMessage =
			    L"[Environment] required settings JSON not found: " +
			    environmentSettingsFilePath + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return false;
		}

		std::wstring errorMessage{};
		const std::optional<Data::EnvironmentRuntimeSettings> loadedSettings =
		    m_runtimeSettingsJsonStorage.LoadEnvironment(
		        environmentSettingsFilePath,
		        errorMessage);
		if (!loadedSettings.has_value())
		{
			std::wstring logMessage =
			    L"[Environment] failed to load settings JSON: " + errorMessage +
			    L"\n";
			OutputDebugStringW(logMessage.c_str());
			return false;
		}

		m_environmentSettings.pitchDistanceMeters =
		    loadedSettings->pitchDistanceMeters;
		m_environmentSettings.enableMoundHeightOffset =
		    loadedSettings->enableMoundHeightOffset;
		m_environmentSettings.moundHeightOffsetMeters =
		    loadedSettings->moundHeightOffsetMeters;
		m_environmentSettings.strikeZoneBottomMeters =
		    loadedSettings->strikeZoneBottomMeters;
		m_environmentSettings.strikeZoneHeightMeters =
		    loadedSettings->strikeZoneHeightMeters;
		m_environmentSettings.strikeZoneWidthMeters =
		    loadedSettings->strikeZoneWidthMeters;
		m_environmentSettings.windVelocity = loadedSettings->windVelocity;
		m_environmentSettings.enableHeightDependentWind =
		    loadedSettings->enableHeightDependentWind;
		m_environmentSettings.windShearFactor =
		    loadedSettings->windShearFactor;
		m_environmentSettings.batRadiusMeters =
		    loadedSettings->batRadiusMeters;
		m_environmentSettings.batLengthMeters =
		    loadedSettings->batLengthMeters;
		m_environmentSettings.batBallRestitution =
		    loadedSettings->batBallRestitution;
		m_environmentSettings.batAngleOffsetDegrees =
		    loadedSettings->batAngleOffsetDegrees;
		m_environmentSettings.batYawOffsetDegrees =
		    loadedSettings->batYawOffsetDegrees;
		m_environmentSettings.groundFanRadiusMeters =
		    loadedSettings->groundFanRadiusMeters;
		m_environmentSettings.integratorType = loadedSettings->integratorType;
		m_environmentSettingsFilePath = environmentSettingsFilePath;
		ApplyEnvironmentSettingsChanges(L"json_environment_load", false);
		std::wstring logMessage =
		    L"[Environment] loaded from " + m_environmentSettingsFilePath + L"\n";
		OutputDebugStringW(logMessage.c_str());
		return true;
	}

	void Application::SavePlaySettingsToJson()
	{
		Data::PlayRuntimeSettings settings{};
		settings.duelPitchIntervalSeconds = m_duelPitchIntervalSeconds;
		settings.playbackSpeedPercent = m_playbackSpeedPercent;
		settings.duelPitchPreviewEnabled = m_duelPitchPreviewEnabled;
		settings.batUseRightHand = m_batUseRightHand;
		settings.pitchTrajectoryVisible = m_pitchTrajectoryVisible;
		settings.battedTrajectoryVisible = m_battedTrajectoryVisible;
		const std::uint32_t normalizedZoneMask =
		    NormalizeDuelZoneMask(m_duelTargetZoneMask);
		settings.duelTargetZoneMask = normalizedZoneMask;
		settings.duelTargetZoneIndex = Common::FindFirstSelectedZoneIndex(
		    normalizedZoneMask,
		    DuelZoneCount);

		std::wstring errorMessage{};
		if (!m_runtimeSettingsJsonStorage.SavePlay(
		        ResolveUserDataPath(PlaySettingsJsonFileName).wstring(),
		        settings,
		        errorMessage))
		{
			std::wstring logMessage =
			    L"[PlayConfig] failed to save settings JSON: " + errorMessage +
			    L"\n";
			OutputDebugStringW(logMessage.c_str());
			return;
		}
	}

	bool Application::LoadPlaySettingsFromJson()
	{
		const std::filesystem::path playSettingsFilePath =
		    ResolveUserDataPath(PlaySettingsJsonFileName);
		if (!std::filesystem::exists(playSettingsFilePath))
		{
			return false;
		}

		std::wstring errorMessage{};
		const std::optional<Data::PlayRuntimeSettings> loadedSettings =
		    m_runtimeSettingsJsonStorage.LoadPlay(
		        playSettingsFilePath.wstring(),
		        errorMessage);
		if (!loadedSettings.has_value())
		{
			std::wstring logMessage =
			    L"[PlayConfig] failed to load settings JSON: " + errorMessage +
			    L"\n";
			OutputDebugStringW(logMessage.c_str());
			return false;
		}

		m_duelPitchIntervalSeconds = std::clamp(
		    loadedSettings->duelPitchIntervalSeconds,
		    MinimumDuelPitchIntervalSeconds,
		    MaximumDuelPitchIntervalSeconds);
		m_playbackSpeedPercent = std::clamp(
		    loadedSettings->playbackSpeedPercent,
		    MinimumPlaybackSpeedPercent,
		    MaximumPlaybackSpeedPercent);
		m_duelPitchPreviewEnabled = loadedSettings->duelPitchPreviewEnabled;
		m_batUseRightHand = loadedSettings->batUseRightHand;
		m_pitchTrajectoryVisible = loadedSettings->pitchTrajectoryVisible;
		m_battedTrajectoryVisible = loadedSettings->battedTrajectoryVisible;
		SyncOverlayTrajectoryVisibilityForCurrentMode();
		m_batPoseState = BatPoseState{};
		m_previousBatPoseState = BatPoseState{};
		m_batOverlayState.reset();
		m_hasLastBatSampleOrigin = false;
		m_lastBatSampleUsedRightHand = m_batUseRightHand;
		m_duelTargetZoneMask = NormalizeDuelZoneMask(
		    loadedSettings->duelTargetZoneMask);
		if ((m_duelTargetZoneMask == 0U) &&
		    loadedSettings->duelTargetZoneIndex.has_value())
		{
			const int legacyZoneIndex = loadedSettings->duelTargetZoneIndex.value();
			if (IsValidDuelZoneIndex(legacyZoneIndex))
			{
				m_duelTargetZoneMask = Common::BuildZoneBit(legacyZoneIndex);
			}
		}
		if (m_isWaitingForNextDuelPitch)
		{
			m_duelPitchCooldownRemainingSeconds = std::clamp(
			    m_duelPitchCooldownRemainingSeconds,
			    0.0,
			    m_duelPitchIntervalSeconds);
		}

		std::wostringstream playLog{};
		playLog << std::fixed << std::setprecision(2);
		playLog << L"[PlayConfig] loaded interval=" << m_duelPitchIntervalSeconds;
		playLog << L", playback=" << m_playbackSpeedPercent;
		playLog << L", preview=" << (m_duelPitchPreviewEnabled ? L"on" : L"off");
		playLog << L", batHand=" << (m_batUseRightHand ? L"right" : L"left");
		playLog << L", pitchTraj=" << (m_pitchTrajectoryVisible ? L"on" : L"off");
		playLog << L", battedTraj=" << (m_battedTrajectoryVisible ? L"on" : L"off");
		playLog << L", zones=" << BuildDuelZoneSelectionLabel(m_duelTargetZoneMask);
		playLog << L"\n";
		OutputDebugStringW(playLog.str().c_str());
		(void)LogPitchEvent(L"play_config", L"json_play_load");
		UpdateWindowTitle();
		return true;
	}

	bool Application::InitializeStoragePaths(std::wstring& errorMessage)
	{
		errorMessage.clear();

		const std::filesystem::path moduleFilePath = BuildModuleFilePath();
		if (moduleFilePath.empty())
		{
			errorMessage = L"Failed to resolve the executable path.";
			return false;
		}

		const std::filesystem::path bundledContentDirectory =
		    FindBundledContentDirectory(moduleFilePath);
		if (bundledContentDirectory.empty())
		{
			errorMessage = L"Failed to locate the bundled default settings directory.";
			return false;
		}

		const std::filesystem::path localAppDataDirectory =
		    GetLocalAppDataDirectory();
		if (localAppDataDirectory.empty())
		{
			errorMessage = L"Failed to resolve the LocalAppData directory.";
			return false;
		}

		const std::filesystem::path userDataDirectory =
		    NormalizePathForStorage(localAppDataDirectory / L"BaseballSimulatorVR");
		std::error_code createDirectoryError{};
		std::filesystem::create_directories(
		    userDataDirectory,
		    createDirectoryError);
		if (createDirectoryError)
		{
			const std::string errorText = createDirectoryError.message();
			errorMessage =
			    L"Failed to create the user data directory: " +
			    std::wstring{ errorText.begin(), errorText.end() };
			return false;
		}

		m_bundledContentDirectoryPath = bundledContentDirectory.wstring();
		m_userDataDirectoryPath = userDataDirectory.wstring();
		return true;
	}

	bool Application::EnsureRequiredUserSettingsFiles(
	    std::wstring& errorMessage) const
	{
		static constexpr std::array<std::wstring_view, 3> RequiredSettingFileNames{
			PitchSettingsJsonFileName,
			DefaultEnvironmentSettingsJsonFileName,
			PlaySettingsJsonFileName
		};

		for (const std::wstring_view fileName : RequiredSettingFileNames)
		{
			if (!CopyFileIfMissing(
			        ResolveBundledContentPath(fileName),
			        ResolveUserDataPath(fileName),
			        errorMessage))
			{
				return false;
			}
		}

		return true;
	}

	std::filesystem::path Application::ResolveUserDataPath(
	    const std::wstring_view filePath) const
	{
		if (filePath.empty())
		{
			return {};
		}

		const std::filesystem::path inputPath{ filePath };
		if (inputPath.is_absolute())
		{
			return NormalizePathForStorage(inputPath);
		}
		if (m_userDataDirectoryPath.empty())
		{
			return NormalizePathForStorage(inputPath);
		}

		return NormalizePathForStorage(
		    std::filesystem::path{ m_userDataDirectoryPath } / inputPath);
	}

	std::filesystem::path Application::ResolveBundledContentPath(
	    const std::wstring_view filePath) const
	{
		if (filePath.empty())
		{
			return {};
		}

		const std::filesystem::path inputPath{ filePath };
		if (inputPath.is_absolute())
		{
			return NormalizePathForStorage(inputPath);
		}
		if (m_bundledContentDirectoryPath.empty())
		{
			return NormalizePathForStorage(inputPath);
		}

		return NormalizePathForStorage(
		    std::filesystem::path{ m_bundledContentDirectoryPath } / inputPath);
	}

	std::filesystem::path Application::ResolveInputFilePath(
	    const std::wstring_view filePath) const
	{
		const std::filesystem::path userDataPath = ResolveUserDataPath(filePath);
		if (userDataPath.empty())
		{
			return {};
		}
		std::error_code userExistsError{};
		if (std::filesystem::exists(userDataPath, userExistsError) &&
		    !userExistsError)
		{
			return userDataPath;
		}

		const std::filesystem::path bundledPath =
		    ResolveBundledContentPath(filePath);
		std::error_code bundledExistsError{};
		if (!bundledPath.empty() &&
		    std::filesystem::exists(bundledPath, bundledExistsError) &&
		    !bundledExistsError)
		{
			return bundledPath;
		}

		return userDataPath;
	}

	void Application::TagLatestPitchEventResult(
	    std::wstring_view resultLabel,
	    const double resultScore,
	    std::wstring_view note)
	{
		if (!m_sessionLogRepository.IsInitialized())
		{
			OutputDebugStringW(L"[SessionLog] result tag skipped: repository not initialized.\n");
			return;
		}

		if (m_lastLoggedPitchEventId <= 0)
		{
			OutputDebugStringW(L"[SessionLog] result tag skipped: no logged pitch event.\n");
			return;
		}

		const double clampedScore = std::clamp(resultScore, MinimumResultScore, MaximumResultScore);
		const Data::OperationResult upsertResult =
		    m_sessionLogRepository.UpsertPitchEventResult(
		        m_lastLoggedPitchEventId,
		        resultLabel,
		        clampedScore,
		        note);
		if (!upsertResult)
		{
			std::wstring logMessage = L"[SessionLog] result upsert failed: " +
			                          upsertResult.errorMessage + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return;
		}

		m_latestPitchResultLabel = std::wstring{ resultLabel };
		m_latestPitchResultScore = clampedScore;

		std::wostringstream resultLog{};
		resultLog << std::fixed << std::setprecision(2);
		resultLog << L"[SessionLog] result tagged id=" << m_lastLoggedPitchEventId;
		resultLog << L", label=" << m_latestPitchResultLabel;
		resultLog << L", score=" << m_latestPitchResultScore << L"\n";
		OutputDebugStringW(resultLog.str().c_str());
		UpdateWindowTitle();
	}

	void Application::AdjustLatestPitchEventResultScore(const double deltaScore)
	{
		if (m_latestPitchResultLabel.empty())
		{
			m_latestPitchResultLabel = L"manual";
		}

		TagLatestPitchEventResult(
		    m_latestPitchResultLabel,
		    m_latestPitchResultScore + deltaScore,
		    L"key_score_adjust");
	}

	bool Application::GetTrajectoryVisibilityForCurrentMode() const noexcept
	{
		return (m_operationMode == OperationMode::PitchTrajectory)
		           ? m_pitchTrajectoryVisible
		           : m_battedTrajectoryVisible;
	}

	void Application::SetTrajectoryVisibilityForCurrentMode(const bool visible)
	{
		if (m_operationMode == OperationMode::PitchTrajectory)
		{
			m_pitchTrajectoryVisible = visible;
		}
		else
		{
			m_battedTrajectoryVisible = visible;
		}
	}

	void Application::SyncOverlayTrajectoryVisibilityForCurrentMode()
	{
		m_overlayVisibility.showTrajectory =
		    GetTrajectoryVisibilityForCurrentMode();
		m_xrSystem->SetOverlayVisibility(m_overlayVisibility);
		m_renderBackend->SetOverlayVisibility(m_overlayVisibility);
	}

	void Application::ApplyOverlayVisibilityChanges(std::wstring_view note)
	{
		SyncOverlayTrajectoryVisibilityForCurrentMode();

		std::wstring noteText = std::wstring{ note };
		if (noteText.empty())
		{
			noteText = L"updated";
		}

		std::wostringstream overlayLog{};
		overlayLog << L"[OverlayConfig] trajectory=" << (m_overlayVisibility.showTrajectory ? L"On" : L"Off");
		overlayLog << L", zone=" << (m_overlayVisibility.showStrikeZone ? L"On" : L"Off");
		overlayLog << L", floor=" << (m_overlayVisibility.showFloorMesh ? L"On" : L"Off");
		overlayLog << L", note=" << noteText << L"\n";
		OutputDebugStringW(overlayLog.str().c_str());

		(void)LogPitchEvent(L"overlay_config", noteText);
		UpdateWindowTitle();
	}

	void Application::ToggleTrajectoryVisibility()
	{
		const bool nextVisibility = !GetTrajectoryVisibilityForCurrentMode();
		SetTrajectoryVisibilityForCurrentMode(nextVisibility);
		ApplyOverlayVisibilityChanges(
		    (m_operationMode == OperationMode::PitchTrajectory)
		        ? std::wstring_view{ L"toggle_pitch_trajectory" }
		        : std::wstring_view{ L"toggle_batted_trajectory" });
		SavePlaySettingsToJson();
	}

	void Application::ToggleStrikeZoneVisibility()
	{
		m_overlayVisibility.showStrikeZone = !m_overlayVisibility.showStrikeZone;
		ApplyOverlayVisibilityChanges(L"toggle_zone");
	}

	void Application::ToggleFloorMeshVisibility()
	{
		m_overlayVisibility.showFloorMesh = !m_overlayVisibility.showFloorMesh;
		ApplyOverlayVisibilityChanges(L"toggle_floor");
	}

	void Application::ApplyRenderQualityChanges(std::wstring_view note)
	{
		m_renderQuality = Common::NormalizeRenderQualitySettings(m_renderQuality);
		m_xrSystem->SetRenderQuality(m_renderQuality);
		m_renderBackend->SetRenderQuality(m_renderQuality);

		std::wstring noteText = std::wstring{ note };
		if (noteText.empty())
		{
			noteText = L"updated";
		}

		std::wostringstream qualityLog{};
		qualityLog << std::fixed << std::setprecision(4);
		qualityLog << L"[RenderQuality] fxaaEdge=" << m_renderQuality.fxaaEdgeThreshold;
		qualityLog << L", fxaaMin=" << m_renderQuality.fxaaEdgeThresholdMin;
		qualityLog << L", fxaaBlend=" << m_renderQuality.fxaaBlendFactor;
		qualityLog << std::setprecision(2);
		qualityLog << L", trailOpacity=" << m_renderQuality.trailOpacityScale;
		qualityLog << L", note=" << noteText << L"\n";
		OutputDebugStringW(qualityLog.str().c_str());

		std::wostringstream qualityNote{};
		qualityNote << std::fixed << std::setprecision(3);
		qualityNote << L"fxaa=" << m_renderQuality.fxaaEdgeThreshold;
		qualityNote << L"/" << m_renderQuality.fxaaEdgeThresholdMin;
		qualityNote << L"/" << m_renderQuality.fxaaBlendFactor;
		qualityNote << std::setprecision(2);
		qualityNote << L",trail=" << m_renderQuality.trailOpacityScale;
		qualityNote << L"," << noteText;
		(void)LogPitchEvent(L"render_quality", qualityNote.str());
		UpdateWindowTitle();
	}

	void Application::AdjustFxaaQualityPreset(const int delta)
	{
		const int maxPresetIndex = static_cast<int>(FxaaQualityPresets.size() - 1U);
		if ((delta == 0) || (maxPresetIndex < 0))
		{
			return;
		}

		int nextPresetIndex = m_fxaaQualityPresetIndex + delta;
		if (nextPresetIndex < 0)
		{
			nextPresetIndex = maxPresetIndex;
		}
		else if (nextPresetIndex > maxPresetIndex)
		{
			nextPresetIndex = 0;
		}

		m_fxaaQualityPresetIndex = nextPresetIndex;
		const FxaaQualityPreset& selectedPreset =
		    FxaaQualityPresets[static_cast<std::size_t>(m_fxaaQualityPresetIndex)];
		m_renderQuality.fxaaEdgeThreshold = selectedPreset.edgeThreshold;
		m_renderQuality.fxaaEdgeThresholdMin = selectedPreset.edgeThresholdMin;
		m_renderQuality.fxaaBlendFactor = selectedPreset.blendFactor;
		m_fxaaQualityPresetLabel = std::wstring{ selectedPreset.label };
		ApplyRenderQualityChanges(
		    std::wstring{ L"fxaa_preset:" } + std::wstring{ selectedPreset.label });
	}

	void Application::AdjustTrailOpacityScale(const double delta)
	{
		const double nextScale = std::clamp(
		    static_cast<double>(m_renderQuality.trailOpacityScale) + delta,
		    MinimumTrailOpacityScale,
		    MaximumTrailOpacityScale);
		if (std::abs(nextScale - static_cast<double>(m_renderQuality.trailOpacityScale)) <= 0.0005)
		{
			return;
		}

		m_renderQuality.trailOpacityScale = static_cast<float>(nextScale);
		std::wostringstream note{};
		note << std::fixed << std::setprecision(2);
		note << L"trail_opacity=" << nextScale;
		ApplyRenderQualityChanges(note.str());
	}

	void Application::ApplyEnvironmentSettingsChanges(
	    const std::wstring_view note,
	    const bool persistToJson)
	{
		m_environmentSettings.pitchDistanceMeters = std::clamp(
		    m_environmentSettings.pitchDistanceMeters,
		    MinimumPitchDistanceMeters,
		    MaximumPitchDistanceMeters);
		Common::SetPitchDistanceMeters(m_environmentSettings.pitchDistanceMeters);
		m_environmentSettings.moundHeightOffsetMeters = std::clamp(
		    m_environmentSettings.moundHeightOffsetMeters,
		    0.0,
		    0.50);
		m_environmentSettings.strikeZoneBottomMeters = std::clamp(
		    m_environmentSettings.strikeZoneBottomMeters,
		    MinimumStrikeZoneBottomMeters,
		    MaximumStrikeZoneBottomMeters);
		m_environmentSettings.strikeZoneHeightMeters = std::clamp(
		    m_environmentSettings.strikeZoneHeightMeters,
		    MinimumStrikeZoneHeightMeters,
		    MaximumStrikeZoneHeightMeters);
		m_environmentSettings.strikeZoneWidthMeters = std::clamp(
		    m_environmentSettings.strikeZoneWidthMeters,
		    MinimumStrikeZoneWidthMeters,
		    MaximumStrikeZoneWidthMeters);
		m_environmentSettings.windVelocity.x = std::clamp(
		    m_environmentSettings.windVelocity.x,
		    MinimumWindCrossVelocity,
		    MaximumWindCrossVelocity);
		m_environmentSettings.windVelocity.y = std::clamp(
		    m_environmentSettings.windVelocity.y,
		    MinimumWindVerticalVelocity,
		    MaximumWindVerticalVelocity);
		m_environmentSettings.windVelocity.z = std::clamp(
		    m_environmentSettings.windVelocity.z,
		    MinimumWindForwardVelocity,
		    MaximumWindForwardVelocity);
		m_environmentSettings.windShearFactor = std::clamp(
		    m_environmentSettings.windShearFactor,
		    MinimumWindShearFactor,
		    MaximumWindShearFactor);
		m_environmentSettings.batRadiusMeters = std::clamp(
		    m_environmentSettings.batRadiusMeters,
		    MinimumBatRadiusMeters,
		    MaximumBatRadiusMeters);
		m_environmentSettings.batLengthMeters = std::clamp(
		    m_environmentSettings.batLengthMeters,
		    MinimumBatLengthMeters,
		    MaximumBatLengthMeters);
		m_environmentSettings.batBallRestitution = std::clamp(
		    m_environmentSettings.batBallRestitution,
		    MinimumBatBallRestitution,
		    MaximumBatBallRestitution);
		m_environmentSettings.batAngleOffsetDegrees = std::clamp(
		    m_environmentSettings.batAngleOffsetDegrees,
		    MinimumBatAngleOffsetDegrees,
		    MaximumBatAngleOffsetDegrees);
		m_environmentSettings.batYawOffsetDegrees = std::clamp(
		    m_environmentSettings.batYawOffsetDegrees,
		    MinimumBatYawOffsetDegrees,
		    MaximumBatYawOffsetDegrees);
		m_environmentSettings.groundFanRadiusMeters = std::clamp(
		    m_environmentSettings.groundFanRadiusMeters,
		    MinimumGroundFanRadiusMeters,
		    MaximumGroundFanRadiusMeters);
		Common::SetStrikeZoneRuntimeSettings(Common::StrikeZoneRuntimeSettings{
		    m_environmentSettings.strikeZoneBottomMeters,
		    m_environmentSettings.strikeZoneHeightMeters,
		    m_environmentSettings.strikeZoneWidthMeters });
		Common::SetGroundRuntimeSettings(Common::GroundRuntimeSettings{
		    m_environmentSettings.groundFanRadiusMeters });
		m_teeBallPosition.x = std::clamp(
		    m_teeBallPosition.x,
		    TeeBallMinX,
		    TeeBallMaxX);
		m_teeBallPosition.z = std::clamp(
		    m_teeBallPosition.z,
		    TeeBallMinZ,
		    TeeBallMaxZ);
		m_teeBallPosition.y = std::clamp(
		    m_teeBallPosition.y,
		    TeeBallMinY,
		    TeeBallMaxY);
		for (Data::PitchProfileEntry& profile : m_pitchProfiles)
		{
			profile.pitchParameters.integratorType =
			    m_environmentSettings.integratorType;
			profile.pitchParameters.windVelocity =
			    m_environmentSettings.windVelocity;
			profile.pitchParameters.enableHeightDependentWind =
			    m_environmentSettings.enableHeightDependentWind;
			profile.pitchParameters.windShearFactor =
			    m_environmentSettings.windShearFactor;
		}
		m_activePitchParameters.integratorType =
		    m_environmentSettings.integratorType;
		m_activePitchParameters.windVelocity =
		    m_environmentSettings.windVelocity;
		m_activePitchParameters.enableHeightDependentWind =
		    m_environmentSettings.enableHeightDependentWind;
		m_activePitchParameters.windShearFactor =
		    m_environmentSettings.windShearFactor;
		m_pitchSimulationService.Reset(m_activePitchParameters);
		if (m_batOverlayState.has_value())
		{
			m_batOverlayState->radiusMeters =
			    m_environmentSettings.batRadiusMeters;
		}

		std::wstring noteText = std::wstring{ note };
		if (noteText.empty())
		{
			noteText = L"updated";
		}

		std::wostringstream envLog{};
		envLog << std::fixed << std::setprecision(2);
		envLog << L"[Environment] pitchDistance=" << m_environmentSettings.pitchDistanceMeters;
		envLog << L", moundOffsetEnabled="
		       << (m_environmentSettings.enableMoundHeightOffset ? L"On" : L"Off");
		envLog << L", moundOffset=" << m_environmentSettings.moundHeightOffsetMeters;
		envLog << L", strikeZone=(bottom="
		       << m_environmentSettings.strikeZoneBottomMeters
		       << L",height=" << m_environmentSettings.strikeZoneHeightMeters
		       << L",width=" << m_environmentSettings.strikeZoneWidthMeters
		       << L")";
		envLog << L", windVel=(" << m_environmentSettings.windVelocity.x << L", "
		       << m_environmentSettings.windVelocity.y << L", "
		       << m_environmentSettings.windVelocity.z << L")";
		envLog << L", heightWind="
		       << (m_environmentSettings.enableHeightDependentWind ? L"On" : L"Off");
		envLog << L", windShear=" << m_environmentSettings.windShearFactor;
		envLog << L", bat=(r=" << m_environmentSettings.batRadiusMeters
		       << L",l=" << m_environmentSettings.batLengthMeters
		       << L",e=" << m_environmentSettings.batBallRestitution
		       << L",angle=" << m_environmentSettings.batAngleOffsetDegrees
		       << L",yaw=" << m_environmentSettings.batYawOffsetDegrees
		       << L")";
		envLog << L", groundFanR=" << m_environmentSettings.groundFanRadiusMeters;
		envLog << L", integrator="
		       << Physics::GetPitchIntegratorName(
		              m_environmentSettings.integratorType);
		envLog << L", note=" << noteText << L"\n";
		OutputDebugStringW(envLog.str().c_str());

		std::wostringstream envNote{};
		envNote << std::fixed << std::setprecision(2);
		envNote << L"distance=" << m_environmentSettings.pitchDistanceMeters;
		envNote << L",mound="
		        << (m_environmentSettings.enableMoundHeightOffset ? L"on" : L"off");
		envNote << L",offset=" << m_environmentSettings.moundHeightOffsetMeters;
		envNote << L",zone=("
		        << m_environmentSettings.strikeZoneBottomMeters << L","
		        << m_environmentSettings.strikeZoneHeightMeters << L","
		        << m_environmentSettings.strikeZoneWidthMeters << L")";
		envNote << L",wind=(" << m_environmentSettings.windVelocity.x << L","
		        << m_environmentSettings.windVelocity.y << L","
		        << m_environmentSettings.windVelocity.z << L")";
		envNote << L",heightWind="
		        << (m_environmentSettings.enableHeightDependentWind ? L"on" : L"off");
		envNote << L",shear=" << m_environmentSettings.windShearFactor;
		envNote << L",bat=(" << m_environmentSettings.batRadiusMeters
		        << L"," << m_environmentSettings.batLengthMeters
		        << L"," << m_environmentSettings.batBallRestitution
		        << L"," << m_environmentSettings.batAngleOffsetDegrees
		        << L"," << m_environmentSettings.batYawOffsetDegrees << L")";
		envNote << L",groundFanR=" << m_environmentSettings.groundFanRadiusMeters;
		envNote << L",integrator="
		        << Physics::GetPitchIntegratorName(
		               m_environmentSettings.integratorType);
		envNote << L"," << noteText;
		(void)LogPitchEvent(L"environment_config", envNote.str());
		UpdateWindowTitle();
		if (persistToJson)
		{
			SaveEnvironmentSettingsToJson();
		}
	}

	void Application::ToggleMoundHeightOffset()
	{
		m_environmentSettings.enableMoundHeightOffset =
		    !m_environmentSettings.enableMoundHeightOffset;
		ApplyEnvironmentSettingsChanges(L"toggle_mound_offset");
	}

	bool Application::CalibrateStrikeZoneFromPlayerHeight(
	    const std::wstring_view note)
	{
		const Common::ViewProjectionState eyeViewState =
		    m_xrSystem->GetLeftEyeViewState();
		double playerHeightMeters = 0.0;
		if (eyeViewState.isValid && (eyeViewState.position.y > 0.10))
		{
			playerHeightMeters =
			    Common::EstimatePlayerHeightFromEyeHeight(
			        eyeViewState.position.y);
		}

		if (playerHeightMeters < 1.00 || playerHeightMeters > 2.50)
		{
			std::wostringstream logMessage{};
			logMessage << std::fixed << std::setprecision(3);
			logMessage << L"[StrikeZone] calibration skipped: estimated height="
			           << playerHeightMeters
			           << L"m (out of plausible range), note="
			           << note << L"\n";
			OutputDebugStringW(logMessage.str().c_str());
			return false;
		}

		const Common::StrikeZoneFromHeight strikeZone =
		    Common::ComputeStrikeZoneFromPlayerHeight(playerHeightMeters);
		m_environmentSettings.strikeZoneBottomMeters =
		    strikeZone.bottomMeters;
		m_environmentSettings.strikeZoneHeightMeters =
		    strikeZone.heightMeters;
		ApplyEnvironmentSettingsChanges(
		    note.empty() ? L"strike_zone_calibrate" : note);

		std::wostringstream logMessage{};
		logMessage << std::fixed << std::setprecision(3);
		logMessage << L"[StrikeZone] calibrated: eyeY="
		           << eyeViewState.position.y
		           << L"m, playerHeight=" << playerHeightMeters
		           << L"m, bottom=" << strikeZone.bottomMeters
		           << L"m, height=" << strikeZone.heightMeters
		           << L"m, note=" << note << L"\n";
		OutputDebugStringW(logMessage.str().c_str());
		return true;
	}

	void Application::AdjustPitchDistance(const double deltaMeters)
	{
		const double nextDistance = std::clamp(
		    m_environmentSettings.pitchDistanceMeters + deltaMeters,
		    MinimumPitchDistanceMeters,
		    MaximumPitchDistanceMeters);
		if (std::abs(nextDistance - m_environmentSettings.pitchDistanceMeters) <=
		    0.0005)
		{
			return;
		}

		m_environmentSettings.pitchDistanceMeters = nextDistance;
		if (m_operationMode == OperationMode::TeeBatting)
		{
			m_teeBallPosition.z = nextDistance;
		}

		std::wostringstream note{};
		note << std::fixed << std::setprecision(2);
		note << L"pitch_distance=" << nextDistance;
		ApplyEnvironmentSettingsChanges(note.str());
	}

	void Application::AdjustDuelPitchInterval(
	    const double deltaSeconds,
	    const std::wstring_view note)
	{
		const double nextInterval = std::clamp(
		    m_duelPitchIntervalSeconds + deltaSeconds,
		    MinimumDuelPitchIntervalSeconds,
		    MaximumDuelPitchIntervalSeconds);
		if (std::abs(nextInterval - m_duelPitchIntervalSeconds) <= 1e-6)
		{
			return;
		}

		m_duelPitchIntervalSeconds = nextInterval;
		if (m_isWaitingForNextDuelPitch)
		{
			m_duelPitchCooldownRemainingSeconds = std::clamp(
			    m_duelPitchCooldownRemainingSeconds,
			    0.0,
			    m_duelPitchIntervalSeconds);
		}

		std::wostringstream noteStream{};
		noteStream << std::fixed << std::setprecision(2);
		noteStream << L"duel_interval=" << m_duelPitchIntervalSeconds;
		if (!note.empty())
		{
			noteStream << L"," << note;
		}
		(void)LogPitchEvent(L"duel_interval", noteStream.str());
		UpdateWindowTitle();
		SavePlaySettingsToJson();
	}

	void Application::ToggleDuelPitchPreview(const std::wstring_view note)
	{
		m_duelPitchPreviewEnabled = !m_duelPitchPreviewEnabled;

		std::wstring previewNote = L"duel_preview=";
		previewNote += m_duelPitchPreviewEnabled ? L"on" : L"off";
		if (!note.empty())
		{
			previewNote += L"," + std::wstring{ note };
		}
		(void)LogPitchEvent(L"duel_preview", previewNote);
		UpdateWindowTitle();
		SavePlaySettingsToJson();
	}

	void Application::ToggleBatHandedness(const std::wstring_view note)
	{
		m_batUseRightHand = !m_batUseRightHand;
		m_batPoseState = BatPoseState{};
		m_previousBatPoseState = BatPoseState{};
		m_batOverlayState.reset();
		m_hasLastBatSampleOrigin = false;
		m_lastBatSampleUsedRightHand = m_batUseRightHand;

		std::wstring batHandNote = L"bat_hand=";
		batHandNote += m_batUseRightHand ? L"right" : L"left";
		if (!note.empty())
		{
			batHandNote += L"," + std::wstring{ note };
		}
		(void)LogPitchEvent(L"play_config", batHandNote);
		UpdateWindowTitle();
		SavePlaySettingsToJson();
	}

	void Application::AdjustBatAngleOffset(
	    const double deltaDegrees,
	    const std::wstring_view note)
	{
		const double nextAngle = std::clamp(
		    m_environmentSettings.batAngleOffsetDegrees + deltaDegrees,
		    MinimumBatAngleOffsetDegrees,
		    MaximumBatAngleOffsetDegrees);
		if (std::abs(nextAngle - m_environmentSettings.batAngleOffsetDegrees) <=
		    1e-6)
		{
			return;
		}

		m_environmentSettings.batAngleOffsetDegrees = nextAngle;
		std::wostringstream angleLog{};
		angleLog << std::fixed << std::setprecision(1);
		angleLog << L"bat_angle_deg="
		         << m_environmentSettings.batAngleOffsetDegrees;
		if (!note.empty())
		{
			angleLog << L"," << note;
		}
		(void)LogPitchEvent(L"environment_config", angleLog.str());
		UpdateWindowTitle();
		SaveEnvironmentSettingsToJson();
	}

	void Application::AdjustBatYawOffset(
	    const double deltaDegrees,
	    const std::wstring_view note)
	{
		const double nextYaw = std::clamp(
		    m_environmentSettings.batYawOffsetDegrees + deltaDegrees,
		    MinimumBatYawOffsetDegrees,
		    MaximumBatYawOffsetDegrees);
		if (std::abs(nextYaw - m_environmentSettings.batYawOffsetDegrees) <=
		    1e-6)
		{
			return;
		}

		m_environmentSettings.batYawOffsetDegrees = nextYaw;
		std::wostringstream yawLog{};
		yawLog << std::fixed << std::setprecision(1);
		yawLog << L"bat_yaw_deg="
		       << m_environmentSettings.batYawOffsetDegrees;
		if (!note.empty())
		{
			yawLog << L"," << note;
		}
		(void)LogPitchEvent(L"environment_config", yawLog.str());
		UpdateWindowTitle();
		SaveEnvironmentSettingsToJson();
	}

	void Application::SetDuelTargetZoneBias(const int zoneIndex)
	{
		if (!IsValidDuelZoneIndex(zoneIndex))
		{
			std::wostringstream errorLog{};
			errorLog << L"[DuelZone] invalid zone index: " << zoneIndex << L"\n";
			OutputDebugStringW(errorLog.str().c_str());
			return;
		}

		const std::uint32_t zoneBit = Common::BuildZoneBit(zoneIndex);
		if ((m_duelTargetZoneMask & zoneBit) != 0U)
		{
			m_duelTargetZoneMask &= ~zoneBit;
		}
		else
		{
			m_duelTargetZoneMask |= zoneBit;
		}
		m_duelTargetZoneMask = NormalizeDuelZoneMask(m_duelTargetZoneMask);

		std::wostringstream note{};
		note << L"duel_zone_toggle=" << (zoneIndex + 1);
		note << L",zones=" << BuildDuelZoneSelectionLabel(m_duelTargetZoneMask);
		(void)LogPitchEvent(L"duel_bias", note.str());
		UpdateWindowTitle();
		SavePlaySettingsToJson();
		if (m_operationMode == OperationMode::TeeBatting)
		{
			RefreshTeeBallSpawnFromZoneSelection(L"zone_toggle");
		}
		else if (m_operationMode == OperationMode::PitchTrajectory)
		{
			RefreshPitchTrajectoryFromZoneSelection(L"zone_toggle");
		}
	}

	void Application::ClearDuelTargetZoneBias()
	{
		if (m_duelTargetZoneMask == 0U)
		{
			return;
		}

		m_duelTargetZoneMask = 0U;
		(void)LogPitchEvent(L"duel_bias", L"duel_zone=random");
		UpdateWindowTitle();
		SavePlaySettingsToJson();
		if (m_operationMode == OperationMode::TeeBatting)
		{
			RefreshTeeBallSpawnFromZoneSelection(L"zone_random");
		}
		else if (m_operationMode == OperationMode::PitchTrajectory)
		{
			RefreshPitchTrajectoryFromZoneSelection(L"zone_random");
		}
	}

	void Application::SelectPitchProfile(
	    const int profileIndex,
	    const std::wstring_view note)
	{
		if ((profileIndex < 0) ||
		    (profileIndex >= static_cast<int>(m_pitchProfiles.size())))
		{
			return;
		}

		m_activePitchProfileIndex = profileIndex;
		m_randomPitchProfileEnabled = false;
		m_activePitchParameters =
		    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)]
		        .pitchParameters;
		m_trajectoryZoneBiasLateral = 0.0;
		m_trajectoryZoneBiasVertical = 0.0;
		m_activePresetType = Data::PitchPresetType::Custom;
		std::wstring profileNote = L"profile=" +
		                           m_pitchProfiles[static_cast<std::size_t>(
		                                               m_activePitchProfileIndex)]
		                               .name;
		if (!note.empty())
		{
			profileNote += L"," + std::wstring{ note };
		}
		ApplyPitchParameterChangesAndLog(
		    L"pitch_profile",
		    profileNote);
		if ((m_operationMode == OperationMode::PitchTrajectory) &&
		    HasDuelTargetZoneSelection())
		{
			RefreshPitchTrajectoryFromZoneSelection(L"profile_select");
		}
		SaveUserPresetToJson();
	}

	void Application::SelectPitchProfileByOffset(
	    const int profileOffset,
	    const std::wstring_view note)
	{
		if ((profileOffset == 0) || m_pitchProfiles.empty())
		{
			return;
		}

		const int profileCount = static_cast<int>(m_pitchProfiles.size());
		int baseIndex = m_activePitchProfileIndex;
		if ((baseIndex < 0) || (baseIndex >= profileCount))
		{
			baseIndex = 0;
		}
		const int normalizedOffset = profileOffset % profileCount;
		int nextIndex = (baseIndex + normalizedOffset) % profileCount;
		if (nextIndex < 0)
		{
			nextIndex += profileCount;
		}
		SelectPitchProfile(nextIndex, note);
	}

	void Application::SelectPitchSettingsFileByOffset(
	    const int fileOffset,
	    const std::wstring_view note)
	{
		if (fileOffset == 0)
		{
			return;
		}

		RefreshPitchSettingsFileList();
		if (m_pitchSettingsFileNames.empty())
		{
			return;
		}

		const int fileCount = static_cast<int>(m_pitchSettingsFileNames.size());
		int baseIndex = m_activePitchSettingsFileIndex;
		if ((baseIndex < 0) || (baseIndex >= fileCount))
		{
			baseIndex = 0;
		}
		const int normalizedOffset = fileOffset % fileCount;
		int nextIndex = (baseIndex + normalizedOffset) % fileCount;
		if (nextIndex < 0)
		{
			nextIndex += fileCount;
		}
		const std::wstring nextFileName =
		    m_pitchSettingsFileNames[static_cast<std::size_t>(nextIndex)];
		if (nextFileName == m_activePitchSettingsFileName)
		{
			return;
		}

		std::wstring loadNote = L"switch_file=" + nextFileName;
		if (!note.empty())
		{
			loadNote += L"," + std::wstring{ note };
		}
		if (!LoadUserPresetFromJsonFile(nextFileName, loadNote))
		{
			return;
		}
	}

	void Application::ToggleRandomPitchProfile(const std::wstring_view note)
	{
		m_randomPitchProfileEnabled = !m_randomPitchProfileEnabled;
		std::wstring pitchNote = L"random_pitch=";
		pitchNote += m_randomPitchProfileEnabled ? L"on" : L"off";
		if (!note.empty())
		{
			pitchNote += L"," + std::wstring{ note };
		}
		(void)LogPitchEvent(L"pitch_profile", pitchNote);
		UpdateWindowTitle();
		SaveUserPresetToJson();
	}

	void Application::ApplyAutoZoneResultTag(
	    const Physics::Vector3& zonePassPoint,
	    const std::wstring_view note)
	{
		const double strikeZoneHalfWidth = Common::GetStrikeZoneHalfWidthMeters();
		const double strikeZoneBottom = Common::GetStrikeZoneBottomMeters();
		const double strikeZoneTop = Common::GetStrikeZoneTopMeters();
		const bool isInStrikeZone =
		    (std::abs(zonePassPoint.x) <= strikeZoneHalfWidth) &&
		    (zonePassPoint.y >= strikeZoneBottom) &&
		    (zonePassPoint.y <= strikeZoneTop);
		TagLatestPitchEventResult(
		    isInStrikeZone ? std::wstring_view{ L"strike" }
		                   : std::wstring_view{ L"ball" },
		    isInStrikeZone ? 1.0 : -1.0,
		    note);
	}

	bool Application::IsWaitingForNextDuelPitch() const noexcept
	{
		return (m_operationMode == OperationMode::BattingDuel) &&
		       m_isWaitingForNextDuelPitch;
	}

	Xr::UiTelemetry Application::BuildXrUiTelemetry() const
	{
		Xr::UiTelemetry telemetry{};
		switch (m_operationMode)
		{
			case OperationMode::PitchTrajectory:
				telemetry.operationMode = Xr::UiOperationMode::PitchTrajectory;
				break;
			case OperationMode::BattingDuel:
				telemetry.operationMode = Xr::UiOperationMode::BattingDuel;
				break;
			case OperationMode::TeeBatting:
				telemetry.operationMode = Xr::UiOperationMode::TeeBatting;
				break;
			default:
				telemetry.operationMode = Xr::UiOperationMode::PitchTrajectory;
				break;
		}

		if ((m_activePresetType == Data::PitchPresetType::Custom) &&
		    (m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			telemetry.presetName =
			    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)].name;
		}
		else
		{
			telemetry.presetName = std::wstring{
				m_presetRepository.GetPresetName(m_activePresetType)
			};
		}
		telemetry.pitchDistanceMeters = m_environmentSettings.pitchDistanceMeters;
		telemetry.duelIntervalSeconds = m_duelPitchIntervalSeconds;
		telemetry.playbackSpeedPercent = m_playbackSpeedPercent;
		telemetry.playbackPaused = m_playbackPaused;
		telemetry.moundHeightOffsetEnabled =
		    m_environmentSettings.enableMoundHeightOffset;
		telemetry.moundHeightOffsetMeters =
		    m_environmentSettings.moundHeightOffsetMeters;
		telemetry.pitchProfileCount = static_cast<int>(m_pitchProfiles.size());
		telemetry.pitchProfileNames.resize(
		    static_cast<std::size_t>(telemetry.pitchProfileCount));
		for (int profileIndex = 0;
		     profileIndex < telemetry.pitchProfileCount;
		     ++profileIndex)
		{
			telemetry.pitchProfileNames[static_cast<std::size_t>(profileIndex)] =
			    m_pitchProfiles[static_cast<std::size_t>(profileIndex)].name;
		}
		telemetry.activePitchProfileIndex = m_activePitchProfileIndex;
		telemetry.pitchSettingsFileName = m_activePitchSettingsFileName;
		telemetry.pitchSettingsFileCount = static_cast<int>(
		    m_pitchSettingsFileNames.size());
		telemetry.randomPitchProfileEnabled = m_randomPitchProfileEnabled;
		telemetry.duelTargetZoneMask = NormalizeDuelZoneMask(m_duelTargetZoneMask);
		telemetry.duelPitchPreviewEnabled = m_duelPitchPreviewEnabled;
		telemetry.batUseRightHand = m_batUseRightHand;
		telemetry.batAngleOffsetDegrees =
		    m_environmentSettings.batAngleOffsetDegrees;
		telemetry.batYawOffsetDegrees =
		    m_environmentSettings.batYawOffsetDegrees;
		telemetry.pitchMetricsSpeedMetersPerSecond =
		    (std::max)(0.0, m_pitchMetricsSpeedMetersPerSecond);
		const BattedBallMetrics* metricsForTelemetry = nullptr;
		bool hasLiveBattedMetrics = false;
		if ((m_operationMode == OperationMode::BattingDuel) &&
		    m_duelBattedMetrics.hasContact)
		{
			metricsForTelemetry = &m_duelBattedMetrics;
			hasLiveBattedMetrics = true;
		}
		else if ((m_operationMode == OperationMode::TeeBatting) &&
		         m_teeBattedMetrics.hasContact)
		{
			metricsForTelemetry = &m_teeBattedMetrics;
			hasLiveBattedMetrics = true;
		}
		else if ((m_operationMode == OperationMode::TeeBatting) &&
		         m_hasTeeLastBattedMetrics)
		{
			metricsForTelemetry = &m_teeLastBattedMetrics;
		}
		if (metricsForTelemetry != nullptr)
		{
			telemetry.hasBattedMetrics = true;
			telemetry.battedMetricsLive = hasLiveBattedMetrics;
			telemetry.battedMetricsHasGroundContact =
			    metricsForTelemetry->hasFirstGroundContact;
			telemetry.battedFirstGroundDistanceMeters =
			    metricsForTelemetry->firstGroundDistanceMeters;
			telemetry.battedMaxHeightMeters =
			    metricsForTelemetry->maxHeightMeters;
			telemetry.battedLaunchVelocity =
			    metricsForTelemetry->launchVelocity;
		}
		return telemetry;
	}

	void Application::RefreshBatPoseFromXrInput()
	{
		const bool isBattingMode =
		    (m_operationMode == OperationMode::BattingDuel) ||
		    (m_operationMode == OperationMode::TeeBatting);
		if (!isBattingMode)
		{
			m_batPoseState = BatPoseState{};
			m_previousBatPoseState = BatPoseState{};
			m_batOverlayState.reset();
			m_hasLastBatSampleOrigin = false;
			return;
		}

		const Xr::HandPoseState selectedHandPose =
		    m_batUseRightHand ? m_xrSystem->GetRightHandPose()
		                      : m_xrSystem->GetLeftHandPose();
		Physics::Vector3 baseForward = Physics::NormalizeOrZero(
		    selectedHandPose.forward);
		if (!selectedHandPose.isValid ||
		    (Physics::Length(baseForward) <= 1e-6))
		{
			m_batPoseState = BatPoseState{};
			m_previousBatPoseState = BatPoseState{};
			m_batOverlayState.reset();
			m_hasLastBatSampleOrigin = false;
			return;
		}

		Physics::Vector3 controllerUp = Physics::NormalizeOrZero(
		    selectedHandPose.up);
		if (Physics::Length(controllerUp) <= 1e-6)
		{
			controllerUp = Physics::Vector3{ 0.0, 1.0, 0.0 };
		}
		if (std::abs(Physics::Dot(baseForward, controllerUp)) >= 0.995)
		{
			Physics::Vector3 fallbackUp = Physics::NormalizeOrZero(
			    Physics::Cross(
			        baseForward,
			        Physics::Vector3{ 0.0, 0.0, 1.0 }));
			if (Physics::Length(fallbackUp) <= 1e-6)
			{
				fallbackUp = Physics::NormalizeOrZero(
				    Physics::Cross(
				        baseForward,
				        Physics::Vector3{ 1.0, 0.0, 0.0 }));
			}
			if (Physics::Length(fallbackUp) > 1e-6)
			{
				controllerUp = Physics::NormalizeOrZero(
				    Physics::Cross(fallbackUp, baseForward));
			}
		}

		Physics::Vector3 axisDirectionWorld = baseForward;
		constexpr double DegreesToRadians =
		    3.14159265358979323846 / 180.0;
		if (std::abs(m_environmentSettings.batYawOffsetDegrees) > 1e-6)
		{
			axisDirectionWorld = Physics::NormalizeOrZero(
			    RotateVectorAroundAxis(
			        axisDirectionWorld,
			        controllerUp,
			        m_environmentSettings.batYawOffsetDegrees *
			            DegreesToRadians));
			if (Physics::Length(axisDirectionWorld) <= 1e-6)
			{
				axisDirectionWorld = baseForward;
			}
		}

		Physics::Vector3 localRightAxis = Physics::NormalizeOrZero(
		    Physics::Cross(axisDirectionWorld, controllerUp));
		if (Physics::Length(localRightAxis) <= 1e-6)
		{
			localRightAxis = Physics::NormalizeOrZero(
			    Physics::Cross(
			        axisDirectionWorld,
			        Physics::Vector3{ 0.0, 1.0, 0.0 }));
		}
		if (Physics::Length(localRightAxis) <= 1e-6)
		{
			localRightAxis = Physics::NormalizeOrZero(
			    Physics::Cross(
			        axisDirectionWorld,
			        Physics::Vector3{ 1.0, 0.0, 0.0 }));
		}
		if (std::abs(m_environmentSettings.batAngleOffsetDegrees) > 1e-6)
		{
			if (Physics::Length(localRightAxis) > 1e-6)
			{
				axisDirectionWorld = Physics::NormalizeOrZero(
				    RotateVectorAroundAxis(
				        axisDirectionWorld,
				        localRightAxis,
				        m_environmentSettings.batAngleOffsetDegrees *
				            DegreesToRadians));
			}
		}
		if (Physics::Length(axisDirectionWorld) <= 1e-6)
		{
			axisDirectionWorld = baseForward;
		}

		Physics::Vector3 linearVelocityWorld{};
		const bool canEstimateVelocity =
		    m_hasLastBatSampleOrigin &&
		    (m_lastBatSampleUsedRightHand == m_batUseRightHand) &&
		    (m_lastFrameDeltaSeconds > 1e-6);
		if (canEstimateVelocity)
		{
			linearVelocityWorld =
			    (selectedHandPose.position - m_lastBatSampleOriginWorld) /
			    m_lastFrameDeltaSeconds;
		}

		m_previousBatPoseState =
		    (m_batPoseState.isValid &&
		     (m_lastBatSampleUsedRightHand == m_batUseRightHand))
		        ? m_batPoseState
		        : BatPoseState{};
		m_batPoseState.isValid = true;
		m_batPoseState.originWorld = selectedHandPose.position;
		m_batPoseState.axisDirectionWorld = axisDirectionWorld;
		m_batPoseState.upDirectionWorld = controllerUp;
		m_batPoseState.linearVelocityWorld = linearVelocityWorld;
		m_lastBatSampleOriginWorld = selectedHandPose.position;
		m_hasLastBatSampleOrigin = true;
		m_lastBatSampleUsedRightHand = m_batUseRightHand;

		const double batLengthMeters = std::clamp(
		    m_environmentSettings.batLengthMeters,
		    MinimumBatLengthMeters,
		    MaximumBatLengthMeters);
		const double batRadiusMeters = std::clamp(
		    m_environmentSettings.batRadiusMeters,
		    MinimumBatRadiusMeters,
		    MaximumBatRadiusMeters);
		const Physics::Vector3 batStartWorld =
		    selectedHandPose.position +
		    (axisDirectionWorld * BatStartForwardOffsetMeters);
		const Physics::Vector3 batEndWorld =
		    selectedHandPose.position + (axisDirectionWorld * batLengthMeters);
		m_batOverlayState = Common::BatOverlayState{
			ConvertWorldToOverlaySimulationPosition(
			    batStartWorld,
			    m_environmentSettings.pitchDistanceMeters),
			ConvertWorldToOverlaySimulationPosition(
			    batEndWorld,
			    m_environmentSettings.pitchDistanceMeters),
			batRadiusMeters
		};
	}

	bool Application::TryApplyBatCollision(Physics::BallState* const ballState)
	{
		if ((ballState == nullptr) || !m_batPoseState.isValid)
		{
			return false;
		}

		if (m_batCollisionSuppressRemainingSeconds > 1e-6)
		{
			return false;
		}

		const double batRadiusMeters = std::clamp(
		    m_environmentSettings.batRadiusMeters,
		    MinimumBatRadiusMeters,
		    MaximumBatRadiusMeters);
		const double batLengthMeters = std::clamp(
		    m_environmentSettings.batLengthMeters,
		    MinimumBatLengthMeters,
		    MaximumBatLengthMeters);
		const double restitution = std::clamp(
		    m_environmentSettings.batBallRestitution,
		    MinimumBatBallRestitution,
		    MaximumBatBallRestitution);
		if ((batRadiusMeters <= 1e-6) || (batLengthMeters <= 1e-6))
		{
			return false;
		}

		const Physics::Vector3 currentBatAxisDirection = Physics::NormalizeOrZero(
		    m_batPoseState.axisDirectionWorld);
		if (Physics::Length(currentBatAxisDirection) <= 1e-6)
		{
			return false;
		}

		const Physics::Vector3 ballWorldPosition =
		    ConvertSimulationToCollisionWorldPosition(
		        ballState->position,
		        m_environmentSettings.pitchDistanceMeters,
		        m_environmentSettings.moundHeightOffsetMeters);
		const double collisionSampleDeltaSeconds = (std::max)(m_lastFrameDeltaSeconds,
		                                                      FixedSimulationStep.count());
		Physics::Vector3 previousBallWorldPosition{};
		bool hasPreviousBallWorldPosition = false;
		if ((m_operationMode == OperationMode::BattingDuel) &&
		    m_hasPreviousDuelBallState)
		{
			previousBallWorldPosition = ConvertSimulationToCollisionWorldPosition(
			    m_previousDuelBallState.position,
			    m_environmentSettings.pitchDistanceMeters,
			    m_environmentSettings.moundHeightOffsetMeters);
			hasPreviousBallWorldPosition = true;
		}
		else if ((m_operationMode == OperationMode::TeeBatting) &&
		         m_hasPreviousTeeBallState)
		{
			previousBallWorldPosition = ConvertSimulationToCollisionWorldPosition(
			    m_previousTeeBallState.position,
			    m_environmentSettings.pitchDistanceMeters,
			    m_environmentSettings.moundHeightOffsetMeters);
			hasPreviousBallWorldPosition = true;
		}
		if (!hasPreviousBallWorldPosition)
		{
			previousBallWorldPosition =
			    ballWorldPosition - (ballState->velocity * collisionSampleDeltaSeconds);
		}
		Physics::Vector3 nearestPointOnBallPath{};
		Physics::Vector3 nearestPointOnBat{};
		Physics::Vector3 closestBatAxisDirection = currentBatAxisDirection;
		Physics::Vector3 closestBatUpDirection = Physics::NormalizeOrZero(
		    m_batPoseState.upDirectionWorld);
		Physics::Vector3 closestBatLinearVelocity =
		    m_batPoseState.linearVelocityWorld;
		double distanceSquared = 0.0;
		bool hasClosestPoints = false;
		auto considerBatSegment = [&](const BatPoseState& batPoseState) -> void
		{
			if (!batPoseState.isValid)
			{
				return;
			}
			const Physics::Vector3 candidateBatAxisDirection =
			    Physics::NormalizeOrZero(batPoseState.axisDirectionWorld);
			if (Physics::Length(candidateBatAxisDirection) <= 1e-6)
			{
				return;
			}

			const Physics::Vector3 candidateBatStartWorld =
			    batPoseState.originWorld +
			    (candidateBatAxisDirection * BatStartForwardOffsetMeters);
			const Physics::Vector3 candidateBatEndWorld =
			    batPoseState.originWorld +
			    (candidateBatAxisDirection * batLengthMeters);
			Physics::Vector3 candidateNearestPointOnBallPath{};
			Physics::Vector3 candidateNearestPointOnBat{};
			if (!ResolveClosestPointsOnSegments(
			        previousBallWorldPosition,
			        ballWorldPosition,
			        candidateBatStartWorld,
			        candidateBatEndWorld,
			        &candidateNearestPointOnBallPath,
			        &candidateNearestPointOnBat))
			{
				return;
			}

			const double candidateDistanceSquared = Physics::LengthSquared(
			    candidateNearestPointOnBallPath - candidateNearestPointOnBat);
			if (!hasClosestPoints || (candidateDistanceSquared < distanceSquared))
			{
				hasClosestPoints = true;
				distanceSquared = candidateDistanceSquared;
				nearestPointOnBallPath = candidateNearestPointOnBallPath;
				nearestPointOnBat = candidateNearestPointOnBat;
				closestBatAxisDirection = candidateBatAxisDirection;
				closestBatUpDirection = Physics::NormalizeOrZero(
				    batPoseState.upDirectionWorld);
				closestBatLinearVelocity = batPoseState.linearVelocityWorld;
			}
		};
		considerBatSegment(m_batPoseState);
		considerBatSegment(m_previousBatPoseState);
		if (!hasClosestPoints)
		{
			return false;
		}

		const Physics::Vector3 fromBatToBall =
		    nearestPointOnBallPath - nearestPointOnBat;
		const double collisionRadiusMeters =
		    batRadiusMeters + Common::DebugWorldStyle::BallMeshRadiusMeters;
		const double collisionRadiusSquared =
		    collisionRadiusMeters * collisionRadiusMeters;
		if (distanceSquared > collisionRadiusSquared)
		{
			return false;
		}

		Physics::Vector3 collisionNormal = Physics::NormalizeOrZero(fromBatToBall);
		Physics::Vector3 batUpDirection = closestBatUpDirection;
		if (Physics::Length(batUpDirection) <= 1e-6)
		{
			batUpDirection = Physics::Vector3{ 0.0, 1.0, 0.0 };
		}
		const Physics::Vector3 ballPathVelocity =
		    (ballWorldPosition - previousBallWorldPosition) /
		    collisionSampleDeltaSeconds;
		Physics::Vector3 relativeVelocity =
		    ballPathVelocity - closestBatLinearVelocity;
		if (Physics::Length(collisionNormal) <= 1e-6)
		{
			collisionNormal = Physics::NormalizeOrZero(
			    -relativeVelocity);
		}
		if (Physics::Length(collisionNormal) <= 1e-6)
		{
			collisionNormal = Physics::NormalizeOrZero(
			    Physics::Cross(
			        closestBatAxisDirection,
			        batUpDirection));
		}
		if (Physics::Length(collisionNormal) <= 1e-6)
		{
			collisionNormal = Physics::Vector3{ 1.0, 0.0, 0.0 };
		}
		if (Physics::Dot(relativeVelocity, collisionNormal) > 0.0)
		{
			collisionNormal = -collisionNormal;
		}

		const double distanceMeters = std::sqrt((std::max)(distanceSquared, 0.0));
		const double penetrationMeters = collisionRadiusMeters - distanceMeters;
		Physics::Vector3 correctedBallWorldPosition = ballWorldPosition;
		if (penetrationMeters > 0.0)
		{
			correctedBallWorldPosition =
			    nearestPointOnBat +
			    (collisionNormal * (collisionRadiusMeters + 1e-4));
		}

		double relativeNormalSpeed = Physics::Dot(
		    relativeVelocity,
		    collisionNormal);
		const double batSpeedMetersPerSecond = Physics::Length(
		    closestBatLinearVelocity);
		if ((relativeNormalSpeed >= -0.05) &&
		    (penetrationMeters <= 1e-4) &&
		    (batSpeedMetersPerSecond <= 0.25))
		{
			return false;
		}

		if (relativeNormalSpeed < 0.0)
		{
			ballState->velocity -=
			    ((1.0 + restitution) * relativeNormalSpeed) *
			    collisionNormal;
		}
		Physics::Vector3 carryAxisDirection = closestBatAxisDirection;
		if (Physics::Dot(carryAxisDirection, closestBatLinearVelocity) < 0.0)
		{
			carryAxisDirection = -carryAxisDirection;
		}
		const Physics::Vector3 batCarryVelocity =
		    (closestBatLinearVelocity * 0.85) +
		    (carryAxisDirection * 4.5);
		ballState->velocity += batCarryVelocity;
		if (m_operationMode == OperationMode::BattingDuel)
		{
			const Physics::Vector3 fairTerritoryForwardDirection{ 0.0, 0.0, -1.0 };
			const double forwardSwingSpeed = (std::max)(0.0,
			                                            Physics::Dot(
			                                                closestBatLinearVelocity,
			                                                fairTerritoryForwardDirection));
			if (forwardSwingSpeed >= DuelForwardAssistSwingThresholdMetersPerSecond)
			{
				const double minimumForwardSpeed = std::clamp(
				    DuelForwardAssistBaseSpeedMetersPerSecond +
				        (forwardSwingSpeed * DuelForwardAssistFromSwingScale),
				    DuelForwardAssistBaseSpeedMetersPerSecond,
				    DuelForwardAssistMaximumSpeedMetersPerSecond);
				const double currentForwardSpeed = Physics::Dot(
				    ballState->velocity,
				    fairTerritoryForwardDirection);
				if (currentForwardSpeed < minimumForwardSpeed)
				{
					ballState->velocity +=
					    fairTerritoryForwardDirection *
					    (minimumForwardSpeed - currentForwardSpeed);
				}
			}
		}
		if (m_operationMode == OperationMode::TeeBatting)
		{
			const double teeLaunchSpeed =
			    Physics::Length(ballState->velocity);
			const double teeLaunchSpeedFloor = std::clamp(
			    TeeLaunchSpeedFromBatSpeedBias +
			        (batSpeedMetersPerSecond * TeeLaunchSpeedFromBatSpeedMultiplier),
			    MinimumTeeLaunchSpeedMetersPerSecond,
			    MaximumTeeLaunchSpeedMetersPerSecond);
			if (teeLaunchSpeed < teeLaunchSpeedFloor)
			{
				Physics::Vector3 teeLaunchDirection =
				    Physics::NormalizeOrZero(ballState->velocity);
				if (Physics::Length(teeLaunchDirection) <= 1e-6)
				{
					teeLaunchDirection = Physics::NormalizeOrZero(
					    (collisionNormal * 0.65) +
					    (closestBatAxisDirection * 0.85));
				}
				if (Physics::Length(teeLaunchDirection) > 1e-6)
				{
					ballState->velocity =
					    teeLaunchDirection *
					    teeLaunchSpeedFloor;
				}
			}
		}
		const double nextBallSpeed = Physics::Length(ballState->velocity);
		if (nextBallSpeed > MaximumBattedBallSpeedMetersPerSecond)
		{
			ballState->velocity =
			    (ballState->velocity / nextBallSpeed) *
			    MaximumBattedBallSpeedMetersPerSecond;
		}
		ballState->position = ConvertCollisionWorldToSimulationPosition(
		    correctedBallWorldPosition,
		    m_environmentSettings.pitchDistanceMeters,
		    m_environmentSettings.moundHeightOffsetMeters);
		m_pitchSimulationService.SetBallState(*ballState);
		m_batCollisionSuppressRemainingSeconds =
		    BatCollisionSuppressDurationSeconds;
		BattedBallMetrics& activeMetrics =
		    (m_operationMode == OperationMode::TeeBatting)
		        ? m_teeBattedMetrics
		        : m_duelBattedMetrics;
		activeMetrics = BattedBallMetrics{};
		activeMetrics.hasContact = true;
		activeMetrics.contactPosition = ballState->position;
		activeMetrics.launchVelocity = ballState->velocity;
		activeMetrics.maxHeightMeters =
		    ballState->position.y + m_environmentSettings.moundHeightOffsetMeters;
		if (m_operationMode == OperationMode::BattingDuel)
		{
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_hasCurrentDuelPitchPassedZone = true;
		}
		else if (m_operationMode == OperationMode::TeeBatting)
		{
			m_hasTeeLastBattedMetrics = false;
		}

		std::wostringstream collisionNote{};
		collisionNote << std::fixed << std::setprecision(2);
		collisionNote << L"mode="
		              << ((m_operationMode == OperationMode::TeeBatting)
		                      ? L"tee"
		                      : L"duel");
		collisionNote << L",hand="
		              << (m_batUseRightHand ? L"right" : L"left");
		collisionNote << L",speed=" << Physics::Length(ballState->velocity);
		(void)LogPitchEvent(L"bat_contact", collisionNote.str());
		return true;
	}

	void Application::ApplyEnvironmentToBallState(
	    Physics::BallState* const ballState) const
	{
		if (ballState == nullptr)
		{
			return;
		}

		ballState->position.y += m_environmentSettings.moundHeightOffsetMeters;
	}

	bool Application::HasDuelTargetZoneSelection() const noexcept
	{
		return NormalizeDuelZoneMask(m_duelTargetZoneMask) != 0U;
	}

	void Application::RefreshPitchTrajectoryFromZoneSelection(
	    const std::wstring_view note)
	{
		if (m_operationMode != OperationMode::PitchTrajectory)
		{
			return;
		}

		m_activePitchParameters.initialVelocity.x = std::clamp(
		    m_activePitchParameters.initialVelocity.x - m_trajectoryZoneBiasLateral,
		    MinimumHorizontalSpeed,
		    MaximumHorizontalSpeed);
		m_activePitchParameters.initialVelocity.y = std::clamp(
		    m_activePitchParameters.initialVelocity.y - m_trajectoryZoneBiasVertical,
		    MinimumVerticalSpeed,
		    MaximumVerticalSpeed);
		m_trajectoryZoneBiasLateral = 0.0;
		m_trajectoryZoneBiasVertical = 0.0;

		std::wstring zoneNote = L"zone=random";
		if (HasDuelTargetZoneSelection())
		{
			const double zoneWorldVerticalOffsetMeters =
			    m_environmentSettings.moundHeightOffsetMeters;
			std::vector<int> selectedZoneIndices =
			    CollectSelectedDuelZoneIndices(m_duelTargetZoneMask);
			if (!selectedZoneIndices.empty())
			{
				std::mt19937& randomEngine = GetThreadLocalRandomEngine();
				std::uniform_int_distribution<std::size_t> selectedZoneDistribution(
				    0U,
				    selectedZoneIndices.size() - 1U);
				const int selectedZone = selectedZoneIndices[selectedZoneDistribution(
				    randomEngine)];
				const std::optional<DuelZoneCellBounds> targetZoneCellBounds =
				    ResolveDuelZoneCellBounds(selectedZone);
				const std::optional<Physics::Vector3> targetZoneWorldPosition =
				    ResolveRandomDuelZoneCellPosition(
				        selectedZone,
				        m_environmentSettings.pitchDistanceMeters,
				        randomEngine);
				const std::optional<TrajectoryTargetVelocity> targetVelocity =
				    (targetZoneCellBounds.has_value() &&
				     targetZoneWorldPosition.has_value())
				        ? ResolveTrajectoryZoneTargetVelocity(
				              m_activePitchParameters,
				              m_environmentSettings.pitchDistanceMeters,
				              selectedZone,
				              targetZoneCellBounds.value(),
				              targetZoneWorldPosition.value(),
				              zoneWorldVerticalOffsetMeters)
				        : std::nullopt;
				zoneNote = L"zone=" + std::to_wstring(selectedZone + 1);
				if (targetZoneWorldPosition.has_value())
				{
					std::wostringstream targetPointLabel{};
					targetPointLabel << std::fixed << std::setprecision(2);
					targetPointLabel << L",target="
					                 << targetZoneWorldPosition->x
					                 << L"," << targetZoneWorldPosition->y;
					zoneNote += targetPointLabel.str();
				}
				if (targetVelocity.has_value())
				{
					m_trajectoryZoneBiasLateral =
					    targetVelocity->x -
					    m_activePitchParameters.initialVelocity.x;
					m_trajectoryZoneBiasVertical =
					    targetVelocity->y -
					    m_activePitchParameters.initialVelocity.y;
					m_activePitchParameters.initialVelocity.x = std::clamp(
					    m_activePitchParameters.initialVelocity.x +
					        m_trajectoryZoneBiasLateral,
					    MinimumHorizontalSpeed,
					    MaximumHorizontalSpeed);
					m_activePitchParameters.initialVelocity.y = std::clamp(
					    m_activePitchParameters.initialVelocity.y +
					        m_trajectoryZoneBiasVertical,
					    MinimumVerticalSpeed,
					    MaximumVerticalSpeed);
					zoneNote += L",zoneSolve=ok";
				}
				else
				{
					zoneNote += L",zoneSolve=failed";
				}
			}
		}
		if (!note.empty())
		{
			zoneNote += L"," + std::wstring{ note };
		}
		ApplyPitchParameterChangesAndLog(
		    L"pitch_zone",
		    zoneNote,
		    true);
	}

	void Application::RefreshTeeBallSpawnFromZoneSelection(
	    const std::wstring_view note)
	{
		std::mt19937& randomEngine = GetThreadLocalRandomEngine();
		std::vector<int> candidateZoneIndices =
		    HasDuelTargetZoneSelection()
		        ? CollectSelectedDuelZoneIndices(m_duelTargetZoneMask)
		        : std::vector<int>{};
		if (candidateZoneIndices.empty())
		{
			for (int zoneIndex = 0; zoneIndex < DuelZoneCount; ++zoneIndex)
			{
				candidateZoneIndices.push_back(zoneIndex);
			}
		}

		std::uniform_int_distribution<std::size_t> zoneDistribution(
		    0U,
		    candidateZoneIndices.size() - 1U);
		const int selectedZoneIndex =
		    candidateZoneIndices[zoneDistribution(randomEngine)];
		const std::optional<Physics::Vector3> zoneCenterPosition =
		    ResolveDuelZoneCellCenterPosition(
		        selectedZoneIndex,
		        m_environmentSettings.pitchDistanceMeters);
		if (!zoneCenterPosition.has_value())
		{
			m_teeBallPosition = Physics::Vector3{
				0.0,
				TeeBallHeightMeters,
				m_environmentSettings.pitchDistanceMeters
			};
		}
		else
		{
			const double simulationY = zoneCenterPosition->y -
			                           m_environmentSettings.moundHeightOffsetMeters;
			m_teeBallPosition = Physics::Vector3{
				std::clamp(zoneCenterPosition->x, TeeBallMinX, TeeBallMaxX),
				std::clamp(simulationY, TeeBallMinY, TeeBallMaxY),
				std::clamp(zoneCenterPosition->z, TeeBallMinZ, TeeBallMaxZ)
			};
		}
		m_isTeeBallInFlight = false;
		m_hasTeeBattedBallBounced = false;
		m_teePostBounceElapsedSeconds = 0.0;
		m_hasPreviousTeeBallState = false;
		m_teeBattedMetrics = BattedBallMetrics{};
		m_pitchSimulationService.SetBallState(BuildTeeBallState());
		m_pitchMetricsSpeedMetersPerSecond = 0.0;
		m_forceTrajectoryResetOnNextFrame = true;

		std::wostringstream teeSpawnLog{};
		teeSpawnLog << std::fixed << std::setprecision(2);
		teeSpawnLog << L"tee_spawn_zone=" << (selectedZoneIndex + 1);
		teeSpawnLog << L",tee_pos=" << m_teeBallPosition.x << L","
		            << m_teeBallPosition.y << L"," << m_teeBallPosition.z;
		if (!note.empty())
		{
			teeSpawnLog << L"," << note;
		}
		(void)LogPitchEvent(L"tee_config", teeSpawnLog.str());
		UpdateWindowTitle();
	}

	void Application::AdjustTeeBallPosition(
	    const double deltaX,
	    const double deltaY,
	    const double deltaZ)
	{
		const Physics::Vector3 previousPosition = m_teeBallPosition;
		m_teeBallPosition.x = std::clamp(
		    m_teeBallPosition.x + deltaX,
		    TeeBallMinX,
		    TeeBallMaxX);
		m_teeBallPosition.y = std::clamp(
		    m_teeBallPosition.y + deltaY,
		    TeeBallMinY,
		    TeeBallMaxY);
		m_teeBallPosition.z = std::clamp(
		    m_teeBallPosition.z + deltaZ,
		    TeeBallMinZ,
		    TeeBallMaxZ);
		if ((std::abs(m_teeBallPosition.x - previousPosition.x) <= 1e-6) &&
		    (std::abs(m_teeBallPosition.y - previousPosition.y) <= 1e-6) &&
		    (std::abs(m_teeBallPosition.z - previousPosition.z) <= 1e-6))
		{
			return;
		}
		m_isTeeBallInFlight = false;
		m_hasTeeBattedBallBounced = false;
		m_teePostBounceElapsedSeconds = 0.0;
		m_teeBattedMetrics = BattedBallMetrics{};
		m_hasPreviousTeeBallState = false;
		m_pitchSimulationService.SetBallState(BuildTeeBallState());
		m_pitchMetricsSpeedMetersPerSecond = 0.0;
		m_forceTrajectoryResetOnNextFrame = true;

		std::wostringstream note{};
		note << std::fixed << std::setprecision(2);
		note << L"tee_pos=" << m_teeBallPosition.x << L","
		     << m_teeBallPosition.y << L"," << m_teeBallPosition.z;
		(void)LogPitchEvent(L"tee_config", note.str());
		UpdateWindowTitle();
	}

	void Application::ResetTeeBallPosition()
	{
		RefreshTeeBallSpawnFromZoneSelection(L"tee_pos_reset");
	}

	void Application::SetOperationMode(
	    const OperationMode mode,
	    const std::wstring_view note)
	{
		if (m_operationMode == mode)
		{
			if (mode == OperationMode::TeeBatting)
			{
				RefreshTeeBallSpawnFromZoneSelection(L"mode_repeat");
			}
			return;
		}

		m_operationMode = mode;
		SyncOverlayTrajectoryVisibilityForCurrentMode();
		m_forceTrajectoryResetOnNextFrame = true;
		m_playbackPaused = false;
		m_isWaitingForNextDuelPitch = false;
		m_duelPitchCooldownRemainingSeconds = 0.0;
		m_trajectoryZoneBiasLateral = 0.0;
		m_trajectoryZoneBiasVertical = 0.0;
		m_batCollisionSuppressRemainingSeconds = 0.0;
		m_previousBatPoseState = BatPoseState{};
		m_hasDuelBallBeenBatted = false;
		m_hasDuelBattedBallBounced = false;
		m_duelPostBounceElapsedSeconds = 0.0;
		m_duelBattedMetrics = BattedBallMetrics{};
		m_isTeeBallInFlight = false;
		m_hasTeeBattedBallBounced = false;
		m_teePostBounceElapsedSeconds = 0.0;
		m_teeBattedMetrics = BattedBallMetrics{};
		m_teeLastBattedMetrics = BattedBallMetrics{};
		m_hasTeeLastBattedMetrics = false;
		m_hasPreviousTeeBallState = false;
		switch (m_operationMode)
		{
			case OperationMode::PitchTrajectory:
				m_duelZonePassMarker.reset();
				m_duelPitchPredictedZonePassPoint.reset();
				m_duelPitchElapsedSeconds = 0.0;
				m_hasCurrentDuelPitchPassedZone = false;
				m_hasPreviousDuelBallState = false;
				m_batPoseState = BatPoseState{};
				m_previousBatPoseState = BatPoseState{};
				m_batOverlayState.reset();
				m_hasLastBatSampleOrigin = false;
				RefreshPitchTrajectoryFromZoneSelection(L"mode_enter");
				break;
			case OperationMode::BattingDuel:
				StartRandomBattingDuelPitch(L"mode_enter");
				break;
			case OperationMode::TeeBatting:
				m_duelZonePassMarker.reset();
				m_duelPitchPredictedZonePassPoint.reset();
				m_duelPitchElapsedSeconds = 0.0;
				m_hasCurrentDuelPitchPassedZone = false;
				m_hasPreviousDuelBallState = false;
				RefreshTeeBallSpawnFromZoneSelection(L"mode_enter");
				break;
			default:
				break;
		}

		const std::wstring_view modeLabel = [&]() -> std::wstring_view
		{
			switch (m_operationMode)
			{
				case OperationMode::PitchTrajectory:
					return L"trajectory";
				case OperationMode::BattingDuel:
					return L"batting-duel";
				case OperationMode::TeeBatting:
					return L"tee-batting";
				default:
					return L"unknown";
			}
		}();

		std::wstring modeNote = L"mode=" + std::wstring{ modeLabel };
		if (!note.empty())
		{
			modeNote += L"," + std::wstring{ note };
		}

		std::wstring logMessage =
		    L"[Mode] switched to " + std::wstring{ modeLabel } + L"\n";
		OutputDebugStringW(logMessage.c_str());
		(void)LogPitchEvent(L"operation_mode", modeNote);
		UpdateWindowTitle();
	}

	void Application::StartRandomBattingDuelPitch(const std::wstring_view note)
	{
		std::mt19937& randomEngine = GetThreadLocalRandomEngine();

		EnsureDefaultPitchProfiles();
		if (m_pitchProfiles.empty())
		{
			return;
		}

		int selectedProfileIndex = m_activePitchProfileIndex;
		if (m_randomPitchProfileEnabled)
		{
			std::uniform_int_distribution<int> profileDistribution(
			    0,
			    static_cast<int>(m_pitchProfiles.size()) - 1);
			selectedProfileIndex = profileDistribution(randomEngine);
		}
		selectedProfileIndex = std::clamp(
		    selectedProfileIndex,
		    0,
		    static_cast<int>(m_pitchProfiles.size()) - 1);
		m_activePitchProfileIndex = selectedProfileIndex;
		m_activePresetType = Data::PitchPresetType::Custom;
		m_activePitchParameters =
		    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)]
		        .pitchParameters;

		std::vector<int> candidateZoneIndices =
		    HasDuelTargetZoneSelection()
		        ? CollectSelectedDuelZoneIndices(m_duelTargetZoneMask)
		        : std::vector<int>{};
		if (candidateZoneIndices.empty())
		{
			candidateZoneIndices.reserve(DuelZoneCount);
			for (int zoneIndex = 0; zoneIndex < DuelZoneCount; ++zoneIndex)
			{
				candidateZoneIndices.push_back(zoneIndex);
			}
		}

		int selectedZoneIndex = -1;
		bool zoneSolveSucceeded = false;
		std::optional<Physics::Vector3> selectedZoneTargetWorldPosition{};
		if (!candidateZoneIndices.empty())
		{
			const double zoneWorldVerticalOffsetMeters =
			    m_environmentSettings.moundHeightOffsetMeters;
			std::uniform_int_distribution<std::size_t> selectedZoneDistribution(
			    0U,
			    candidateZoneIndices.size() - 1U);
			selectedZoneIndex = candidateZoneIndices[selectedZoneDistribution(
			    randomEngine)];
			const std::optional<DuelZoneCellBounds> targetZoneCellBounds =
			    ResolveDuelZoneCellBounds(selectedZoneIndex);
			selectedZoneTargetWorldPosition = ResolveRandomDuelZoneCellPosition(
			    selectedZoneIndex,
			    m_environmentSettings.pitchDistanceMeters,
			    randomEngine);
			const std::optional<TrajectoryTargetVelocity> targetVelocity =
			    (targetZoneCellBounds.has_value() &&
			     selectedZoneTargetWorldPosition.has_value())
			        ? ResolveTrajectoryZoneTargetVelocity(
			              m_activePitchParameters,
			              m_environmentSettings.pitchDistanceMeters,
			              selectedZoneIndex,
			              targetZoneCellBounds.value(),
			              selectedZoneTargetWorldPosition.value(),
			              zoneWorldVerticalOffsetMeters)
			        : std::nullopt;
			if (targetVelocity.has_value())
			{
				m_activePitchParameters.initialVelocity.x = std::clamp(
				    targetVelocity->x,
				    MinimumHorizontalSpeed,
				    MaximumHorizontalSpeed);
				m_activePitchParameters.initialVelocity.y = std::clamp(
				    targetVelocity->y,
				    MinimumVerticalSpeed,
				    MaximumVerticalSpeed);
				zoneSolveSucceeded = true;
			}
		}
		if (!zoneSolveSucceeded)
		{
			std::uniform_real_distribution<double> lateralFallbackDistribution(
			    -1.2,
			    1.2);
			std::uniform_real_distribution<double> verticalFallbackDistribution(
			    -0.35,
			    0.35);
			m_activePitchParameters.initialVelocity.x = std::clamp(
			    m_activePitchParameters.initialVelocity.x +
			        lateralFallbackDistribution(randomEngine),
			    MinimumHorizontalSpeed,
			    MaximumHorizontalSpeed);
			m_activePitchParameters.initialVelocity.y = std::clamp(
			    m_activePitchParameters.initialVelocity.y +
			        verticalFallbackDistribution(randomEngine),
			    MinimumVerticalSpeed,
			    MaximumVerticalSpeed);
		}
		m_duelZonePassMarker.reset();
		m_duelPitchPredictedZonePassPoint = SimulateZonePassPointAtDepth(
		    m_activePitchParameters,
		    m_environmentSettings.pitchDistanceMeters);
		m_duelPitchElapsedSeconds = 0.0;
		m_hasCurrentDuelPitchPassedZone = false;
		m_isWaitingForNextDuelPitch = false;
		m_duelPitchCooldownRemainingSeconds = 0.0;
		m_hasPreviousDuelBallState = false;
		m_hasDuelBallBeenBatted = false;
		m_hasDuelBattedBallBounced = false;
		m_duelPostBounceElapsedSeconds = 0.0;
		m_duelBattedMetrics = BattedBallMetrics{};
		m_pitchMetricsSpeedMetersPerSecond =
		    Physics::Length(m_activePitchParameters.initialVelocity);
		m_batCollisionSuppressRemainingSeconds = 0.0;
		m_isTeeBallInFlight = false;
		m_hasTeeBattedBallBounced = false;
		m_teePostBounceElapsedSeconds = 0.0;
		m_hasPreviousTeeBallState = false;

		std::wstring pitchNote = L"duel_random";
		if ((m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			pitchNote += L",profile=" +
			             m_pitchProfiles[static_cast<std::size_t>(
			                                 m_activePitchProfileIndex)]
			                 .name;
		}
		if (selectedZoneIndex >= 0)
		{
			pitchNote += L",zonePick=" + std::to_wstring(selectedZoneIndex + 1);
			if (selectedZoneTargetWorldPosition.has_value())
			{
				std::wostringstream targetPointStream{};
				targetPointStream << std::fixed << std::setprecision(2);
				targetPointStream << L",target="
				                  << selectedZoneTargetWorldPosition->x
				                  << L","
				                  << selectedZoneTargetWorldPosition->y;
				pitchNote += targetPointStream.str();
			}
		}
		pitchNote += L",zoneSolve=";
		pitchNote += zoneSolveSucceeded ? L"ok" : L"fallback";
		pitchNote += L",pitchRnd=";
		pitchNote += m_randomPitchProfileEnabled ? L"on" : L"off";
		if (HasDuelTargetZoneSelection())
		{
			pitchNote += L",zones=" + BuildDuelZoneSelectionLabel(m_duelTargetZoneMask);
		}
		else
		{
			pitchNote += L",zones=random";
		}
		if (!note.empty())
		{
			pitchNote += L":" + std::wstring{ note };
		}
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    pitchNote,
		    true);
	}

	void Application::UpdateModeSpecificBallState(Physics::BallState* const ballState)
	{
		if (ballState == nullptr)
		{
			return;
		}
		m_batCollisionSuppressRemainingSeconds = (std::max)(0.0,
		                                                    m_batCollisionSuppressRemainingSeconds - m_lastFrameDeltaSeconds);
		const GroundFanShape groundFanShape = BuildGroundFanShape(
		    m_environmentSettings.strikeZoneWidthMeters,
		    m_environmentSettings.groundFanRadiusMeters);

		auto applyPendingTrajectoryReset = [&](Physics::BallState* const mutableBallState) -> void
		{
			if ((mutableBallState == nullptr) || !m_forceTrajectoryResetOnNextFrame)
			{
				return;
			}

			mutableBallState->flightTimeSeconds =
			    ForcedTrajectoryResetFlightTimeSeconds;
			m_forceTrajectoryResetOnNextFrame = false;
		};
		auto updateBattedMetricsLiveState = [&](BattedBallMetrics* const metrics) -> void
		{
			if ((metrics == nullptr) || !metrics->hasContact)
			{
				return;
			}
			metrics->maxHeightMeters = (std::max)(metrics->maxHeightMeters,
			                                      ballState->position.y + m_environmentSettings.moundHeightOffsetMeters);
		};
		auto registerGroundContactForMetrics =
		    [&](BattedBallMetrics* const metrics,
		        const Physics::BallState& previousState,
		        const bool hasPreviousState) -> bool
		{
			if ((metrics == nullptr) || !metrics->hasContact ||
			    metrics->hasFirstGroundContact || !hasPreviousState ||
			    !DidBallCrossGroundFromAbove(previousState, *ballState))
			{
				return false;
			}

			const Physics::Vector3 groundCrossPosition =
			    ResolveGroundCrossPoint(previousState, *ballState);
			const Physics::Vector3 groundCrossWorldPosition =
			    ConvertSimulationToCollisionWorldPosition(
			        groundCrossPosition,
			        m_environmentSettings.pitchDistanceMeters,
			        m_environmentSettings.moundHeightOffsetMeters);
			if (!IsWorldPointInsideGroundFan(groundCrossWorldPosition, groundFanShape))
			{
				return false;
			}

			metrics->hasFirstGroundContact = true;
			const Physics::Vector3 contactDelta =
			    groundCrossPosition - metrics->contactPosition;
			metrics->firstGroundDistanceMeters = std::sqrt(
			    (contactDelta.x * contactDelta.x) +
			    (contactDelta.z * contactDelta.z));
			return true;
		};
		auto isBallBelowGroundOutsideFan = [&](const Physics::BallState& state) -> bool
		{
			if (state.position.y > GroundContactHeightMeters)
			{
				return false;
			}
			const Physics::Vector3 worldPosition =
			    ConvertSimulationToCollisionWorldPosition(
			        state.position,
			        m_environmentSettings.pitchDistanceMeters,
			        m_environmentSettings.moundHeightOffsetMeters);
			return !IsWorldPointInsideGroundFan(worldPosition, groundFanShape);
		};
		auto applyGroundCollisionIfNeeded =
		    [&](const Physics::BallState& previousState,
		        const bool hasPreviousState) -> void
		{
			Physics::Vector3 collisionCheckPosition = ballState->position;
			if (hasPreviousState &&
			    DidBallCrossGroundFromAbove(previousState, *ballState))
			{
				collisionCheckPosition =
				    ResolveGroundCrossPoint(previousState, *ballState);
			}
			const Physics::Vector3 collisionCheckWorldPosition =
			    ConvertSimulationToCollisionWorldPosition(
			        collisionCheckPosition,
			        m_environmentSettings.pitchDistanceMeters,
			        m_environmentSettings.moundHeightOffsetMeters);
			if (!IsWorldPointInsideGroundFan(
			        collisionCheckWorldPosition,
			        groundFanShape))
			{
				return;
			}
			ApplyGroundCollisionResponse(ballState, m_lastFrameDeltaSeconds);
			m_pitchSimulationService.SetBallState(*ballState);
		};

		if (m_operationMode == OperationMode::TeeBatting)
		{
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_duelPitchElapsedSeconds = 0.0;
			m_hasCurrentDuelPitchPassedZone = false;
			m_hasPreviousDuelBallState = false;
			m_isWaitingForNextDuelPitch = false;
			m_duelPitchCooldownRemainingSeconds = 0.0;
			m_hasDuelBallBeenBatted = false;
			m_hasDuelBattedBallBounced = false;
			m_duelPostBounceElapsedSeconds = 0.0;
			m_duelBattedMetrics = BattedBallMetrics{};
			if (!m_isTeeBallInFlight)
			{
				*ballState = BuildTeeBallState();
				m_pitchSimulationService.SetBallState(*ballState);
				if (TryApplyBatCollision(ballState))
				{
					m_isTeeBallInFlight = true;
					m_hasTeeBattedBallBounced = false;
					m_teePostBounceElapsedSeconds = 0.0;
					updateBattedMetricsLiveState(&m_teeBattedMetrics);
					applyGroundCollisionIfNeeded(
					    m_previousTeeBallState,
					    m_hasPreviousTeeBallState);
					m_hasPreviousTeeBallState = true;
					m_previousTeeBallState = *ballState;
				}
				else
				{
					m_hasPreviousTeeBallState = false;
				}
				applyPendingTrajectoryReset(ballState);
				return;
			}
			if (TryApplyBatCollision(ballState))
			{
				m_isTeeBallInFlight = true;
				m_hasTeeBattedBallBounced = false;
				m_teePostBounceElapsedSeconds = 0.0;
			}
			updateBattedMetricsLiveState(&m_teeBattedMetrics);
			if (!m_hasTeeBattedBallBounced &&
			    registerGroundContactForMetrics(
			        &m_teeBattedMetrics,
			        m_previousTeeBallState,
			        m_hasPreviousTeeBallState))
			{
				m_hasTeeBattedBallBounced = true;
				m_teePostBounceElapsedSeconds = 0.0;
			}
			if (m_teeBattedMetrics.hasContact &&
			    isBallBelowGroundOutsideFan(*ballState))
			{
				if (!m_teeBattedMetrics.hasFirstGroundContact)
				{
					m_teeBattedMetrics.hasFirstGroundContact = true;
					const Physics::Vector3 contactDelta =
					    ballState->position -
					    m_teeBattedMetrics.contactPosition;
					m_teeBattedMetrics.firstGroundDistanceMeters = std::sqrt(
					    (contactDelta.x * contactDelta.x) +
					    (contactDelta.z * contactDelta.z));
				}
				m_teeLastBattedMetrics = m_teeBattedMetrics;
				m_hasTeeLastBattedMetrics = true;
				m_isTeeBallInFlight = false;
				m_hasTeeBattedBallBounced = false;
				m_teePostBounceElapsedSeconds = 0.0;
				m_hasPreviousTeeBallState = false;
				m_teeBattedMetrics = BattedBallMetrics{};
				if (HasDuelTargetZoneSelection())
				{
					RefreshTeeBallSpawnFromZoneSelection(
					    L"tee_respawn_no_ground");
					*ballState = m_pitchSimulationService.GetBallState();
					applyPendingTrajectoryReset(ballState);
					return;
				}
				*ballState = BuildTeeBallState();
				m_pitchSimulationService.SetBallState(*ballState);
				m_forceTrajectoryResetOnNextFrame = true;
				applyPendingTrajectoryReset(ballState);
				return;
			}
			applyGroundCollisionIfNeeded(
			    m_previousTeeBallState,
			    m_hasPreviousTeeBallState);
			if (m_hasTeeBattedBallBounced)
			{
				m_teePostBounceElapsedSeconds += m_lastFrameDeltaSeconds;
			}
			m_previousTeeBallState = *ballState;
			m_hasPreviousTeeBallState = true;
			const bool shouldRespawnTeeBall =
			    m_hasTeeBattedBallBounced &&
			    (m_teePostBounceElapsedSeconds >=
			     PostBounceSimulationSeconds);
			if (shouldRespawnTeeBall)
			{
				if (m_teeBattedMetrics.hasContact)
				{
					m_teeLastBattedMetrics = m_teeBattedMetrics;
					m_hasTeeLastBattedMetrics = true;
				}
				m_isTeeBallInFlight = false;
				m_hasTeeBattedBallBounced = false;
				m_teePostBounceElapsedSeconds = 0.0;
				m_hasPreviousTeeBallState = false;
				m_teeBattedMetrics = BattedBallMetrics{};
				if (HasDuelTargetZoneSelection())
				{
					RefreshTeeBallSpawnFromZoneSelection(
					    L"tee_respawn_post_bounce");
					*ballState = m_pitchSimulationService.GetBallState();
					m_forceTrajectoryResetOnNextFrame = true;
				}
				else
				{
					*ballState = BuildTeeBallState();
					m_pitchSimulationService.SetBallState(*ballState);
					m_forceTrajectoryResetOnNextFrame = true;
				}
			}
			applyPendingTrajectoryReset(ballState);
			return;
		}

		if (m_operationMode != OperationMode::BattingDuel)
		{
			if (m_operationMode == OperationMode::PitchTrajectory)
			{
				const double autoStopDepthMeters =
				    m_environmentSettings.pitchDistanceMeters * 1.10;
				if (!m_playbackPaused &&
				    (ballState->position.z >= autoStopDepthMeters))
				{
					m_playbackPaused = true;
					(void)LogPitchEvent(
					    L"playback_state",
					    L"auto_pause_zone10");
					UpdateWindowTitle();
				}
			}
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_duelPitchElapsedSeconds = 0.0;
			m_hasCurrentDuelPitchPassedZone = false;
			m_hasDuelBallBeenBatted = false;
			m_hasDuelBattedBallBounced = false;
			m_duelPostBounceElapsedSeconds = 0.0;
			m_duelBattedMetrics = BattedBallMetrics{};
			m_hasPreviousDuelBallState = false;
			m_isWaitingForNextDuelPitch = false;
			m_duelPitchCooldownRemainingSeconds = 0.0;
			m_isTeeBallInFlight = false;
			m_hasTeeBattedBallBounced = false;
			m_teePostBounceElapsedSeconds = 0.0;
			m_teeBattedMetrics = BattedBallMetrics{};
			m_hasPreviousTeeBallState = false;
			applyPendingTrajectoryReset(ballState);
			return;
		}
		m_isTeeBallInFlight = false;

		if (m_isWaitingForNextDuelPitch)
		{
			m_duelPitchCooldownRemainingSeconds = (std::max)(0.0,
			                                                 m_duelPitchCooldownRemainingSeconds - m_lastFrameDeltaSeconds);
			if (m_duelPitchCooldownRemainingSeconds <= 1e-6)
			{
				StartRandomBattingDuelPitch(L"duel_cycle");
				*ballState = m_pitchSimulationService.GetBallState();
				m_previousDuelBallState = *ballState;
				m_hasPreviousDuelBallState = true;
			}
			else
			{
				*ballState = m_previousDuelBallState;
				applyPendingTrajectoryReset(ballState);
				return;
			}
		}

		const double duelZoneDepthMeters =
		    m_environmentSettings.pitchDistanceMeters;
		const double duelPitchEndDepthMeters = duelZoneDepthMeters * 1.10;
		const Physics::BallState currentState = *ballState;
		m_duelPitchElapsedSeconds += m_lastFrameDeltaSeconds;
		if (!m_hasDuelBallBeenBatted && m_hasPreviousDuelBallState)
		{
			const double previousDepth = m_previousDuelBallState.position.z;
			const double currentDepth = currentState.position.z;
			if ((previousDepth < duelZoneDepthMeters) &&
			    (currentDepth >= duelZoneDepthMeters))
			{
				double blend = 0.0;
				const double depthDelta = currentDepth - previousDepth;
				if (std::abs(depthDelta) > 1e-6)
				{
					blend = (duelZoneDepthMeters - previousDepth) / depthDelta;
				}
				blend = std::clamp(blend, 0.0, 1.0);
				m_duelZonePassMarker = Physics::Vector3{
					m_previousDuelBallState.position.x +
					    ((currentState.position.x - m_previousDuelBallState.position.x) * blend),
					m_previousDuelBallState.position.y +
					    ((currentState.position.y - m_previousDuelBallState.position.y) * blend),
					duelZoneDepthMeters
				};
				m_duelPitchPredictedZonePassPoint.reset();
				m_hasCurrentDuelPitchPassedZone = true;
				Physics::Vector3 zonePassPointForTag = m_duelZonePassMarker.value();
				zonePassPointForTag.y += m_environmentSettings.moundHeightOffsetMeters;
				ApplyAutoZoneResultTag(
				    zonePassPointForTag,
				    L"auto_zone_duel");
			}
		}

		const bool collidedWithBat = TryApplyBatCollision(ballState);
		if (collidedWithBat)
		{
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_hasCurrentDuelPitchPassedZone = true;
			m_hasDuelBallBeenBatted = true;
			m_hasDuelBattedBallBounced = false;
			m_duelPostBounceElapsedSeconds = 0.0;
		}
		if (m_hasDuelBallBeenBatted &&
		    !m_hasDuelBattedBallBounced &&
		    registerGroundContactForMetrics(
		        &m_duelBattedMetrics,
		        m_previousDuelBallState,
		        m_hasPreviousDuelBallState))
		{
			m_hasDuelBattedBallBounced = true;
			m_duelPostBounceElapsedSeconds = 0.0;
		}
		if (m_hasDuelBallBeenBatted &&
		    m_duelBattedMetrics.hasContact &&
		    isBallBelowGroundOutsideFan(*ballState))
		{
			if (!m_duelBattedMetrics.hasFirstGroundContact)
			{
				m_duelBattedMetrics.hasFirstGroundContact = true;
				const Physics::Vector3 contactDelta =
				    ballState->position - m_duelBattedMetrics.contactPosition;
				m_duelBattedMetrics.firstGroundDistanceMeters = std::sqrt(
				    (contactDelta.x * contactDelta.x) +
				    (contactDelta.z * contactDelta.z));
			}
			m_isWaitingForNextDuelPitch = true;
			m_duelPitchCooldownRemainingSeconds = m_duelPitchIntervalSeconds;
			m_duelPitchElapsedSeconds = 0.0;
			if (m_hasPreviousDuelBallState)
			{
				*ballState = m_previousDuelBallState;
			}
			applyPendingTrajectoryReset(ballState);
			return;
		}
		if (m_hasDuelBallBeenBatted)
		{
			updateBattedMetricsLiveState(&m_duelBattedMetrics);
			applyGroundCollisionIfNeeded(
			    m_previousDuelBallState,
			    m_hasPreviousDuelBallState);
		}
		if (m_hasDuelBallBeenBatted &&
		    m_hasDuelBattedBallBounced)
		{
			m_duelPostBounceElapsedSeconds += m_lastFrameDeltaSeconds;
		}
		const Physics::BallState stateForTracking = *ballState;
		m_previousDuelBallState = stateForTracking;
		m_hasPreviousDuelBallState = true;
		const bool shouldStartIntervalForUnbattedPitch =
		    !m_hasDuelBallBeenBatted &&
		    (stateForTracking.position.z >= duelPitchEndDepthMeters);
		const bool shouldStartIntervalAfterBattedBounce =
		    m_hasDuelBallBeenBatted &&
		    m_hasDuelBattedBallBounced &&
		    (m_duelPostBounceElapsedSeconds >=
		     PostBounceSimulationSeconds);
		if (shouldStartIntervalForUnbattedPitch ||
		    shouldStartIntervalAfterBattedBounce)
		{
			m_isWaitingForNextDuelPitch = true;
			m_duelPitchCooldownRemainingSeconds = m_duelPitchIntervalSeconds;
			m_duelPitchElapsedSeconds = 0.0;
		}
		applyPendingTrajectoryReset(ballState);
	}

	Physics::BallState Application::BuildTeeBallState() const
	{
		Physics::BallState teeBallState{};
		teeBallState.position = Physics::Vector3{
			std::clamp(m_teeBallPosition.x, TeeBallMinX, TeeBallMaxX),
			std::clamp(m_teeBallPosition.y, TeeBallMinY, TeeBallMaxY),
			std::clamp(m_teeBallPosition.z, TeeBallMinZ, TeeBallMaxZ)
		};
		teeBallState.velocity = Physics::Vector3{ 0.0, 0.0, 0.0 };
		teeBallState.flightTimeSeconds = 0.0;
		return teeBallState;
	}

	bool Application::LogPitchEvent(std::wstring_view eventType, std::wstring_view note)
	{
		if (!m_isSessionLogAvailable || !m_sessionLogRepository.IsInitialized())
		{
			return false;
		}

		const Data::PitchEventLogResult insertedEventResult =
		    m_sessionLogRepository.LogPitchEvent(
		        eventType,
		        m_presetRepository.GetPresetName(m_activePresetType),
		        Physics::GetPitchIntegratorName(m_activePitchParameters.integratorType),
		        m_activePitchParameters,
		        note);
		if (!insertedEventResult)
		{
			std::wstring logMessage = L"[SessionLog] insert failed: " +
			                          insertedEventResult.errorMessage + L"\n";
			OutputDebugStringW(logMessage.c_str());
			return false;
		}

		m_lastLoggedPitchEventId = *insertedEventResult.eventId;
		m_latestPitchResultLabel = L"pending";
		m_latestPitchResultScore = 0.0;
		return true;
	}

	void Application::UpdateWindowTitle()
	{
		if (m_windowHandle == nullptr)
		{
			return;
		}

		const Core::FrameStatisticsSnapshot frameSnapshot = m_frameStatistics.GetLatestSnapshot();
		const double speedMetersPerSecond = Physics::Length(
		    m_activePitchParameters.initialVelocity);
		const std::wstring_view operationModeLabel = [&]() -> std::wstring_view
		{
			switch (m_operationMode)
			{
				case OperationMode::PitchTrajectory:
					return L"Trajectory";
				case OperationMode::BattingDuel:
					return L"BattingDuel";
				case OperationMode::TeeBatting:
					return L"TeeBatting";
				default:
					return L"Unknown";
			}
		}();
		Physics::BallState displayBallState = m_pitchSimulationService.GetBallState();
		if ((m_operationMode == OperationMode::TeeBatting) && !m_isTeeBallInFlight)
		{
			displayBallState = BuildTeeBallState();
		}

		UI::DesktopHudViewModel::WindowTitleInput titleInput{};
		titleInput.ballState = displayBallState;
		titleInput.xrRuntimeName = m_xrSystem->GetRuntimeName();
		titleInput.xrSessionName = m_xrSystem->GetSessionName();
		titleInput.renderBackendName = m_renderBackend->GetBackendName();
		titleInput.operationModeName = operationModeLabel;
		titleInput.fxaaPresetName = m_fxaaQualityPresetLabel;
		titleInput.presetName = m_presetRepository.GetPresetName(m_activePresetType);
		titleInput.integratorName = Physics::GetPitchIntegratorName(
		    m_activePitchParameters.integratorType);
		titleInput.pitchSpeedMetersPerSecond = speedMetersPerSecond;
		titleInput.spinRpm = m_activePitchParameters.spinRpm;
		titleInput.windForwardVelocity =
		    m_environmentSettings.windVelocity.z;
		titleInput.windCrossVelocity =
		    m_environmentSettings.windVelocity.x;
		titleInput.isHeightWindEnabled =
		    m_environmentSettings.enableHeightDependentWind;
		titleInput.isSeamPerturbationEnabled =
		    m_activePitchParameters.enableSeamPerturbation;
		titleInput.windShearFactor = m_environmentSettings.windShearFactor;
		titleInput.seamPerturbationAmplitude =
		    m_activePitchParameters.seamPerturbationAmplitude;
		titleInput.playbackSpeedPercent = m_playbackSpeedPercent;
		titleInput.fxaaEdgeThreshold = m_renderQuality.fxaaEdgeThreshold;
		titleInput.fxaaEdgeThresholdMin = m_renderQuality.fxaaEdgeThresholdMin;
		titleInput.fxaaBlendFactor = m_renderQuality.fxaaBlendFactor;
		titleInput.trailOpacityScale = m_renderQuality.trailOpacityScale;
		titleInput.latestResultLabel = m_latestPitchResultLabel;
		titleInput.latestResultScore = m_latestPitchResultScore;
		titleInput.isSessionLogAvailable =
		    m_isSessionLogAvailable && m_sessionLogRepository.IsInitialized();
		titleInput.framesPerSecond = frameSnapshot.framesPerSecond;
		titleInput.averageFrameTimeMilliseconds =
		    frameSnapshot.averageFrameTimeMilliseconds;
		const std::wstring title = m_desktopHudViewModel.BuildWindowTitle(titleInput);
		std::wostringstream envTitle{};
		envTitle << std::fixed << std::setprecision(2);
		envTitle << title;
		envTitle << L" | Env:D=" << m_environmentSettings.pitchDistanceMeters;
		envTitle << L",Mound="
		         << (m_environmentSettings.enableMoundHeightOffset ? L"On" : L"Off");
		envTitle << L"(" << m_environmentSettings.moundHeightOffsetMeters << L")";
		envTitle << L",Wind=(" << m_environmentSettings.windVelocity.x << L","
		         << m_environmentSettings.windVelocity.y << L","
		         << m_environmentSettings.windVelocity.z << L")";
		envTitle << L",HeightWind="
		         << (m_environmentSettings.enableHeightDependentWind ? L"On" : L"Off");
		envTitle << L",Shear=" << m_environmentSettings.windShearFactor;
		envTitle << L",DuelInt=" << m_duelPitchIntervalSeconds << L"s";
		envTitle << L",Zone="
		         << BuildDuelZoneSelectionLabel(m_duelTargetZoneMask, L"Rnd");
		envTitle << L",Preview=" << (m_duelPitchPreviewEnabled ? L"On" : L"Off");
		envTitle << L",Bat="
		         << (m_batUseRightHand ? L"R" : L"L")
		         << L"(" << m_environmentSettings.batRadiusMeters << L","
		         << m_environmentSettings.batLengthMeters << L","
		         << m_environmentSettings.batBallRestitution << L","
		         << m_environmentSettings.batAngleOffsetDegrees << L"deg,"
		         << m_environmentSettings.batYawOffsetDegrees << L"deg)";
		envTitle << L",GroundFan=" << m_environmentSettings.groundFanRadiusMeters
		         << L"m";
		envTitle << L",PitchRnd=" << (m_randomPitchProfileEnabled ? L"On" : L"Off");
		envTitle << L",Playback=" << (m_playbackPaused ? L"Paused" : L"Run");
		if (m_isWaitingForNextDuelPitch)
		{
			envTitle << L",Next=" << m_duelPitchCooldownRemainingSeconds << L"s";
		}
		SetWindowTextW(m_windowHandle, envTitle.str().c_str());
	}

	void Application::Shutdown()
	{
		m_isRunning = false;

		m_sessionLogRepository.Shutdown();

		if (m_xrSystem != nullptr)
		{
			m_xrSystem->Shutdown();
		}

		if (m_renderBackend != nullptr)
		{
			m_renderBackend->Shutdown();
		}

		if (m_windowHandle != nullptr)
		{
			DestroyWindow(m_windowHandle);
			m_windowHandle = nullptr;
		}
	}

	LRESULT CALLBACK Application::StaticWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_NCCREATE)
		{
			const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			auto* application = static_cast<Application*>(createStruct->lpCreateParams);
			SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
			return TRUE;
		}

		auto* application = reinterpret_cast<Application*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
		if (application != nullptr)
		{
			return application->WindowProc(windowHandle, message, wParam, lParam);
		}

		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}

	LRESULT Application::WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				HandleKeyDown(wParam);
				return 0;
			case WM_COMMAND:
			{
				const int commandId = LOWORD(wParam);
				switch (commandId)
				{
					case IDM_ABOUT:
						MessageBoxW(
						    windowHandle,
						    L"BaseballSimulatorVR phase6 build.\n\n"
						    L"Controls:\n"
						    L"  F1/F2/F3/F4 : Fastball / Slider / Curveball / Changeup\n"
						    L"  F5/F6       : FXAA preset (sharper / smoother)\n"
						    L"  5/6         : FXAA preset (fallback keys)\n"
						    L"  F7/F8       : Trail opacity (down / up)\n"
						    L"  7/8         : Trail opacity (fallback keys)\n"
						    L"  F9/F10/F11  : Mode (Trajectory / BattingDuel / Tee)\n"
						    L"  F10 (duel)  : Increase duel interval (+0.25s)\n"
						    L"  Shift+F10   : Decrease duel interval (-0.25s)\n"
						    L"  F12         : Toggle mound height offset\n"
						    L"  [ / ]       : Adjust pitch distance (-/+)\n"
						    L"  Numpad 4/6  : Tee ball X offset (-/+)\n"
						    L"  Numpad 8/2  : Tee ball Z offset (-/+)\n"
						    L"  Numpad 9/3  : Tee ball Y offset (+/-)\n"
						    L"  Numpad 5    : Reset tee ball position\n"
						    L"  1/2/3       : Toggle trajectory(current mode) / zone / floor\n"
						    L"  Q           : Toggle duel pitch preview marker\n"
						    L"  E           : Toggle bat hand (right / left)\n"
						    L"  , / .       : Bat angle down / up\n"
						    L"  ; / '       : Bat yaw left / right\n"
						    L"  R           : Reset pitch\n"
						    L"  I           : Toggle integrator (Rk4 / SemiImplicitEuler)\n"
						    L"  Up/Down     : Adjust forward speed\n"
						    L"  Left/Right  : Adjust horizontal speed\n"
						    L"  PgUp/PgDn   : Adjust spin rpm\n"
						    L"  W/S         : Adjust forward wind\n"
						    L"  A/D         : Adjust cross wind\n"
						    L"  Z           : Toggle height-dependent wind\n"
						    L"  X           : Toggle seam perturbation\n"
						    L"  T/G         : Adjust wind shear factor\n"
						    L"  Y/H         : Adjust seam amplitude\n"
						    L"  U/J         : Adjust seam frequency\n"
						    L"  +/-         : Adjust playback speed (0-200%, 5% step)\n"
						    L"  0           : Reset playback speed to 100%\n"
						    L"  K           : Save current preset to JSON\n"
						    L"  L           : Load preset from JSON\n"
						    L"  C/V/B/N/M   : Tag result (strike/ball/in_play/foul/pending)\n"
						    L"  O/P         : Adjust result score (-/+)\n\n"
						    L"  (BattingDuel auto-tags strike/ball at zone pass)\n\n"
						    L"XR UI:\n"
						    L"  Point UI-hand controller toward HMD to show split UI near\n"
						    L"  that hand ray root. Panel orientation follows UI-hand ray.\n"
						    L"  Batting/Tee: UI hand = non-bat hand, laser = bat hand.\n"
						    L"  Other modes: UI hand = right, laser = left.\n"
						    L"  Left thumbstick  : Rotate view (yaw/pitch).\n"
						    L"  Right thumbstick : Move (forward/strafe).\n"
						    L"  Use laser hand + trigger to select.\n"
						    L"  Main panel : mode / panel open (open-or-rescue)\n"
						    L"  Pitch panel: 25-zone multi-select / <- pitch -> / <- settings file -> / play-pause / playback slider\n"
						    L"  Metrics UI : pitching+batted sections + trajectory/zone/mound/preview/hand toggles",
						    L"About BaseballSimulatorVR",
						    MB_OK | MB_ICONINFORMATION);
						return 0;
					case IDM_EXIT:
						DestroyWindow(windowHandle);
						return 0;
					default:
						break;
				}
				break;
			}
			case WM_CLOSE:
				DestroyWindow(windowHandle);
				return 0;
			case WM_DESTROY:
				m_isRunning = false;
				m_windowHandle = nullptr;
				PostQuitMessage(0);
				return 0;
			default:
				break;
		}

		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
} // namespace BaseballSimulatorVR::Core
