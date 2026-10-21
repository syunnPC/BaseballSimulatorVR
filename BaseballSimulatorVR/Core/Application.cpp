#include "Application.hpp"

#include "../Resource.h"
#include "../Common/AppLog.hpp"
#include "../Common/AtomicFileWriter.hpp"
#include "../Common/DebugWorldProjection.hpp"
#include "../Common/TextConversion.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/ZoneMaskUtilities.hpp"
#include "../Common/BatPoseUtilities.hpp"
#include "../Common/PitcherPresentation.hpp"
#include "../Physics/BatBallCollision.hpp"
#include "../Physics/BattedBallAerodynamics.hpp"
#include "../Physics/BatGeometry.hpp"
#include "../Physics/SweptBatCollision.hpp"
#include "../Simulation/StadiumCollision.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <future>
#include <shlobj.h>
#include <iomanip>
#include <iterator>
#include <numbers>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace BaseballSimulatorVR::Core
{
	namespace
	{
		[[nodiscard]] std::wstring OverlayModeLabel(const std::string_view token)
		{
			return std::wstring(token.begin(), token.end());
		}

		[[nodiscard]] std::wstring BatMaterialLabel(const Physics::BatMaterial material)
		{
			const auto name = Physics::BatMaterialName(material);
			return std::wstring(name.begin(), name.end());
		}

		[[nodiscard]] double ComputeFastestPresetArmSpeedScale(
		    const std::vector<Data::PitchProfileEntry>& profiles,
		    const Baseball::PitchParameters& rootParameters)
		{
			double fastestSpeed = profiles.empty() ? std::abs(rootParameters.initialVelocity.z) : 0.0;
			for (const auto& profile : profiles)
				fastestSpeed = (std::max)(fastestSpeed, std::abs(profile.pitchParameters.initialVelocity.z));
			return Common::PitcherArmSpeedScale(fastestSpeed);
		}

		constexpr std::wstring_view WindowClassName = L"BaseballSimulatorVR.WindowClass";
		constexpr std::wstring_view DefaultWindowTitle = L"BaseballSimulatorVR";
		constexpr auto FixedSimulationStep = std::chrono::duration<double>(1.0 / 1000.0);
		constexpr auto WindowTitleUpdateInterval = std::chrono::duration<double>(1.0 / 30.0);
		constexpr auto MaxFrameTime = std::chrono::milliseconds(250);
		constexpr std::wstring_view PitchSettingsJsonFileName = Data::PresetLayout::UserPitchSettingsFileName;
		constexpr std::wstring_view LegacyPitchSettingsJsonFileName = L"PitchPreset.user.json";
		constexpr std::wstring_view DefaultEnvironmentSettingsJsonFileName =
		    Data::PresetLayout::UserEnvironmentSettingsFileName;
		constexpr std::wstring_view PlaySettingsJsonFileName = Data::PresetLayout::UserPlaySettingsFileName;
		constexpr std::wstring_view SessionHistoryDatabaseFileName = L"SessionHistory.sqlite3";
		constexpr std::wstring_view SettingsLogCategory = L"Settings";
		// Settings writes are coalesced until no change happened for this long.
		constexpr auto SettingsSaveDebounceInterval = std::chrono::milliseconds(500);

		constexpr double MinimumForwardSpeed = 5.0;
		constexpr double MaximumForwardSpeed = 50.0;
		constexpr double MinimumHorizontalSpeed = -8.0;
		constexpr double MaximumHorizontalSpeed = 8.0;
		constexpr double MinimumVerticalSpeed = -4.0;
		constexpr double MaximumVerticalSpeed = 12.0;
		constexpr double MinimumReleaseHeightMeters = 0.10;
		constexpr double MaximumReleaseHeightMeters = 2.50;
		// Bundled presets include 0 rpm forkballs and 3900 rpm softball curves.
		constexpr double MinimumSpinRpm = 0.0;
		constexpr double MaximumSpinRpm = 4000.0;
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
		constexpr double MinimumBatRadiusMeters = 0.010;
		constexpr double MaximumBatRadiusMeters = 0.080;
		constexpr double MinimumBatLengthMeters = 0.40;
		constexpr double MaximumBatLengthMeters = 1.20;
		constexpr double MinimumBallRestitution = 0.0;
		constexpr double MaximumBallRestitution = 1.0;
		constexpr double MinimumBatAngleOffsetDegrees = -35.0;
		constexpr double MaximumBatAngleOffsetDegrees = 35.0;
		constexpr double MinimumBatYawOffsetDegrees = -35.0;
		constexpr double MaximumBatYawOffsetDegrees = 35.0;
		constexpr double BatAngleOffsetStepDegrees = 2.5;
		constexpr double BatYawOffsetStepDegrees = 2.5;
		constexpr double BatCollisionSuppressDurationSeconds = 0.04;
		constexpr double GroundPlaneHeightMeters = 0.0;
		constexpr double GroundBounceRestitution = 0.50;
		constexpr double GroundBounceTangentialVelocityScale = 0.88;
		constexpr double GroundRollingDampingPerSecond = 1.35;
		constexpr double GroundBounceVelocityThreshold = -0.05;
		constexpr double MinimumGroundFanRadiusMeters = 10.0;
		constexpr double MaximumGroundFanRadiusMeters = 300.0;
		constexpr double PostBounceSimulationSeconds = 5.0;
		// Catcher mode. A pitcher misses his target by about 9 cm each way, and now and then
		// (6%) badly; the mitt closes on a squeeze from a little before the ball arrives until
		// just after it hits the pocket; a missed ball rolls on this long before the next sign.
		constexpr double CatcherCommandErrorMeters = .09;
		constexpr double CatcherWildPitchChance = .06;
		constexpr double CatcherWildErrorMeters = .30;
		constexpr double CatcherEarlySqueezeSeconds = .45;
		constexpr double CatcherLateSqueezeSeconds = .25;
		constexpr double CatcherCleanSqueezeSeconds = .12;
		constexpr double CatcherMissedPlaySeconds = 2.5;
		// The pitcher walks back to the rubber and comes set before taking the next sign.
		constexpr double CatcherResetSeconds = Common::PitcherRecoverySeconds + Common::PitcherSetHoldSeconds;
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
			previousState.properties = pitchParameters.ballProperties;
			previousState.aerodynamicsModel = pitchParameters.usePhysicalAerodynamics
			    ? Physics::BallAerodynamicsModel::Physical : Physics::BallAerodynamicsModel::PitchProfile;
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

		// BASEBALLSIM_RANDOM_SEED (a decimal 32-bit unsigned integer) makes the
		// random zone and pitch selection reproducible; otherwise seed randomly.
		[[nodiscard]] std::uint32_t InitialRandomSeed()
		{
			std::array<wchar_t, 16> text{};
			const DWORD length = GetEnvironmentVariableW(
			    L"BASEBALLSIM_RANDOM_SEED",
			    text.data(),
			    static_cast<DWORD>(text.size()));
			std::uint64_t seed = 0;
			bool valid = (length > 0) && (length < text.size());
			for (DWORD index = 0; valid && (index < length); ++index)
			{
				const wchar_t digit = text[index];
				valid = (digit >= L'0') && (digit <= L'9');
				if (valid)
				{
					seed = (seed * 10) + static_cast<std::uint64_t>(digit - L'0');
					valid = seed <= UINT32_MAX;
				}
			}
			return valid ? static_cast<std::uint32_t>(seed) : std::random_device{}();
		}

		[[nodiscard]] std::mt19937& GetThreadLocalRandomEngine()
		{
			static thread_local std::mt19937 randomEngine{
				InitialRandomSeed()
			};
			return randomEngine;
		}
	} // namespace

	void SeedRandomEngine(const std::uint32_t seed)
	{
		GetThreadLocalRandomEngine().seed(seed);
	}

	namespace
	{
		// A modal MessageBox on the frame thread stops xrWaitFrame/xrEndFrame and
		// freezes the headset image; show runtime notices from a helper thread.
		void ShowNonBlockingMessage(std::wstring title, std::wstring text, const UINT iconFlags)
		{
			std::thread([title = std::move(title), text = std::move(text), iconFlags]()
			{
				(void)MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_SETFOREGROUND | iconFlags);
			}).detach();
		}

		[[nodiscard]] bool DidBallCrossGroundFromAbove(
		    const Physics::BallState& previousState,
		    const Physics::BallState& currentState,
		    const double groundContactHeight) noexcept
		{
			return (previousState.position.y > groundContactHeight) &&
			       (currentState.position.y <= groundContactHeight);
		}

		[[nodiscard]] Physics::Vector3 ResolveGroundCrossPoint(
		    const Physics::BallState& previousState,
		    const Physics::BallState& currentState,
		    const double groundContactHeight) noexcept
		{
			const double denominator =
			    currentState.position.y - previousState.position.y;
			double blend = 0.0;
			if (std::abs(denominator) > 1e-6)
			{
				blend = (groundContactHeight - previousState.position.y) /
				        denominator;
			}
			blend = std::clamp(blend, 0.0, 1.0);
			return previousState.position +
			       ((currentState.position - previousState.position) * blend);
		}

		void ApplyGroundCollisionResponse(
		    Physics::BallState* const ballState,
		    const double frameDeltaSeconds,
		    const double groundContactHeight) noexcept
		{
			if (ballState == nullptr)
			{
				return;
			}
			if (ballState->position.y > groundContactHeight)
			{
				return;
			}

			const double penetrationMeters = groundContactHeight - ballState->position.y;
			ballState->position.y = groundContactHeight;
			if (ballState->velocity.y > 0.0)
			{
				return;
			}
			if (ballState->velocity.y < GroundBounceVelocityThreshold)
			{
				// Reflect the part of the frame spent below ground so the bounce height
				// does not depend on where the frame boundary fell.
				ballState->position.y += penetrationMeters * GroundBounceRestitution;
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

		[[nodiscard]] std::filesystem::path FindBundledPresetsDirectory(
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
				const std::filesystem::path presets = candidate / Data::PresetLayout::PresetsDirectoryName;
				if (Data::PresetCatalog::IsBundledPresetsDirectory(presets))
				{
					return NormalizePathForStorage(presets);
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

		[[nodiscard]] bool FileExists(const std::filesystem::path& path)
		{
			std::error_code error{};
			return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
		}

		// "PlaySettings.user.json" -> "PlaySettings.user.broken-20261021-153000.json"
		[[nodiscard]] std::filesystem::path BuildBrokenFilePath(const std::filesystem::path& filePath)
		{
			SYSTEMTIME time{};
			GetLocalTime(&time);
			wchar_t stamp[32]{};
			(void)swprintf_s(stamp, std::size(stamp), L"%04u%02u%02u-%02u%02u%02u",
			    static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
			    static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute),
			    static_cast<unsigned>(time.wSecond));
			const std::wstring base = filePath.stem().wstring() + L".broken-" + stamp;
			std::filesystem::path candidate = filePath.parent_path() / (base + L".json");
			for (int suffix = 2; FileExists(candidate) && suffix < 100; ++suffix)
			{
				candidate = filePath.parent_path() / (base + L"-" + std::to_wstring(suffix) + L".json");
			}
			return candidate;
		}

		[[nodiscard]] std::wstring_view ZoneCallToken(const Common::PitchZoneCall call) noexcept
		{
			switch (call)
			{
			case Common::PitchZoneCall::Strike:
				return L"strike";
			case Common::PitchZoneCall::Ball:
				return L"ball";
			case Common::PitchZoneCall::Contact:
				return L"contact";
			case Common::PitchZoneCall::Pending:
			default:
				return L"";
			}
		}

		// Keys whose auto-repeat drives a continuous adjustment; every other key (toggles, mode
		// switches, file and profile selection, saves) only reacts to the initial press.
		[[nodiscard]] bool IsRepeatableAdjustmentKey(const WPARAM keyCode, const bool controlPressed) noexcept
		{
			if (controlPressed)
			{
				return false;
			}
			switch (keyCode)
			{
			case VK_UP:
			case VK_DOWN:
			case VK_LEFT:
			case VK_RIGHT:
			case VK_PRIOR:
			case VK_NEXT:
			case 'W':
			case 'S':
			case 'A':
			case 'D':
			case 'T':
			case 'G':
			case 'Y':
			case 'H':
			case 'U':
			case 'J':
			case 'O':
			case 'P':
			case VK_OEM_MINUS:
			case VK_SUBTRACT:
			case VK_OEM_PLUS:
			case VK_ADD:
			case VK_OEM_4:
			case VK_OEM_6:
			case VK_OEM_COMMA:
			case VK_OEM_PERIOD:
			case VK_OEM_1:
			case VK_OEM_7:
			case VK_F7:
			case VK_F8:
			case '7':
			case '8':
			case VK_NUMPAD2:
			case VK_NUMPAD3:
			case VK_NUMPAD4:
			case VK_NUMPAD6:
			case VK_NUMPAD8:
			case VK_NUMPAD9:
				return true;
			default:
				return false;
			}
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
		m_strikeZoneSettings.bottomMeters =
		    DefaultStrikeZoneBottomMeters;
		m_strikeZoneSettings.heightMeters =
		    DefaultStrikeZoneHeightMeters;
		m_strikeZoneSettings.widthMeters =
		    DefaultStrikeZoneWidthMeters;
		m_environmentSettings.windVelocity = Physics::Vector3{};
		m_environmentSettings.enableHeightDependentWind = false;
		m_environmentSettings.windShearFactor = 0.08;
		m_environmentSettings.batRadiusMeters = 0.035;
		m_environmentSettings.batLengthMeters = 0.84;
		m_environmentSettings.ballProperties.restitutionAtReferenceSpeed = 0.45;
		m_environmentSettings.batAngleOffsetDegrees = 0.0;
		m_environmentSettings.batYawOffsetDegrees = 0.0;
		m_environmentSettings.groundFanRadiusMeters = 100.0;
		Common::SetPitchDistanceMeters(m_environmentSettings.pitchDistanceMeters);
		Common::SetStrikeZoneRuntimeSettings(Common::StrikeZoneRuntimeSettings{
		    m_strikeZoneSettings.bottomMeters,
		    m_strikeZoneSettings.heightMeters,
		    m_strikeZoneSettings.widthMeters });
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
		m_pitcherArmSpeedScale = ComputeFastestPresetArmSpeedScale(m_pitchProfiles, m_activePitchParameters);
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
			Common::AppLog::Error(L"Startup", diagnosticsMessage);
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
			Common::AppLog::Error(L"SessionLog",
			    L"initialization failed: " + sessionLogInitializationResult.errorMessage);
		}

		ApplyPreset(m_activePresetType);
		std::wstring settingsErrorMessage{};
		if (!LoadStartupSettings(settingsErrorMessage))
		{
			return failStartup(
			    -9,
			    L"[Startup] failed to load required settings: " + settingsErrorMessage + L"\n",
			    L"Failed to load the required settings:\n" + settingsErrorMessage +
			        L"\n\nRestore the application's Presets folder and restart.");
		}
		if (!m_startupNotices.empty())
		{
			// Which file and key were bad and what was restored. The XR session is already
			// running here, so a modal dialog would hold the headset on a frozen frame until
			// the player took it off to dismiss the box on the desktop.
			std::wstring notice{};
			for (const std::wstring& line : m_startupNotices)
			{
				notice += line + L"\n\n";
			}
			ShowNonBlockingMessage(L"BaseballSimulatorVR settings", std::move(notice), MB_ICONWARNING);
			m_startupNotices.clear();
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
		double previousDisplayTimeSeconds = 0.0;
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
			if (m_xrSystem->IsExitRequested())
			{
				OutputDebugStringW(L"[OpenXR] the runtime ended the session; exiting.\n");
				m_isRunning = false;
				break;
			}

			// Blocks in xrWaitFrame (pacing the loop) and samples views, hand poses
			// and panel input at the predicted display time of the frame that
			// SubmitFrame presents below, after the simulation has caught up.
			m_xrSystem->PollInput();
			const Xr::FrameTiming frameTiming = m_xrSystem->GetFrameTiming();

			const auto currentTime = std::chrono::steady_clock::now();
			std::chrono::duration<double> frameSeconds = currentTime - previousTime;
			previousTime = currentTime;
			// Advance by the display-time step that the two bat samples span, so the
			// swept collision compares ball and bat over the same interval.
			if (frameTiming.isValid && (previousDisplayTimeSeconds > 0.0) &&
			    (frameTiming.predictedDisplayTimeSeconds > previousDisplayTimeSeconds))
			{
				frameSeconds = std::chrono::duration<double>(
				    frameTiming.predictedDisplayTimeSeconds - previousDisplayTimeSeconds);
			}
			previousDisplayTimeSeconds = frameTiming.isValid ? frameTiming.predictedDisplayTimeSeconds : 0.0;
			if (frameSeconds > MaxFrameTime)
			{
				frameSeconds = MaxFrameTime;
			}

			m_lastFrameDeltaSeconds = frameSeconds.count();
			double playbackTimeScale = m_playbackPaused
			                               ? 0.0
			                               : (m_playbackSpeedPercent / 100.0);
			if (!m_playbackPaused &&
			    (m_operationMode == OperationMode::TeeBatting))
			{
				playbackTimeScale = 1.0;
			}
			if (!m_xrSystem->IsInputFocused())
			{
				// The runtime's own UI covers the app: hold the pitch instead of
				// throwing it at a player who cannot see or swing.
				playbackTimeScale = 0.0;
			}
			const std::chrono::duration<double> simulationDelta = frameSeconds * playbackTimeScale;
			m_lastSimulationDeltaSeconds = simulationDelta.count();
			titleAccumulator += frameSeconds;
			m_frameStatistics.AddFrameSample(frameSeconds);

			m_pendingPitchSamples.clear();
			// Fixed 1 ms steps plus the remainder: the ball reaches the display time
			// exactly instead of trailing it by an accumulator residue.
			for (double remainingSeconds = simulationDelta.count(); remainingSeconds > 1e-12;)
			{
				const auto step = std::chrono::duration<double>(
				    (std::min)(remainingSeconds, FixedSimulationStep.count()));
				const bool isSimulationTickAllowedInMode =
				    (m_operationMode != OperationMode::TeeBatting) ||
				    m_teeFlight.isBallInFlight;
				if (isSimulationTickAllowedInMode &&
				    !IsWaitingForNextDuelPitch())
				{
					const auto flightStep = UsesPitcher()
					    ? m_pitchDeliveryController.Advance(step)
					    : step;
					AdvancePitchSimulation(flightStep);
				}
				remainingSeconds -= step.count();
			}

			HandleXrUiCommands();

			UpdateStrikeZoneCalibration();

			RefreshBatPoseFromXrInput();
			UpdateCatcherInput();

			Physics::BallState currentBallState = m_pitchSimulationService.GetBallState();
			UpdateModeSpecificBallState(&currentBallState);
			ApplyEnvironmentToBallState(&currentBallState);
			std::optional<Physics::Vector3> markerForRendering = m_duelZonePassMarker;
			const bool canShowDuelPitchPreview =
			    (m_operationMode == OperationMode::BattingDuel) &&
			    m_duelPitchPreviewEnabled &&
			    !m_duelRound.hasPitchPassedZone &&
			    !m_duelRound.hasBallBeenBatted &&
			    m_duelPitchPredictedZonePassPoint.has_value();
			if (canShowDuelPitchPreview)
			{
				const double previewStartThresholdSeconds =
				    (std::max)(0.0, m_duelPitchIntervalSeconds * 0.5);
				const bool isPreviewActiveWhileWaiting =
				    m_duelRound.isWaitingForNextPitch &&
				    (m_duelRound.cooldownRemainingSeconds <=
				     previewStartThresholdSeconds);
				const bool isPreviewActiveInFlight =
				    !m_duelRound.isWaitingForNextPitch;
				if (isPreviewActiveWhileWaiting || isPreviewActiveInFlight)
				{
					markerForRendering = m_duelPitchPredictedZonePassPoint;
				}
			}
			if ((m_operationMode == OperationMode::BattingDuel) &&
			    m_duelRound.hasBallBeenBatted)
			{
				markerForRendering.reset();
			}
			if (markerForRendering.has_value())
			{
				markerForRendering->y +=
				    m_environmentSettings.moundHeightOffsetMeters;
			}
			m_xrSystem->SetUiTelemetry(BuildXrUiTelemetry());
			const Common::FieldPresentationState fieldPresentation = BuildFieldPresentationState();
			m_xrSystem->SetFieldPresentationState(fieldPresentation);
			m_renderBackend->SetFieldPresentationState(fieldPresentation);
			m_xrSystem->SetBallState(currentBallState);
			m_xrSystem->SetDuelZonePassMarker(markerForRendering);
			m_xrSystem->SetBatOverlayState(m_batOverlayState);
			m_xrSystem->SubmitFrame();

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
			FlushPendingSettingsSaves(false);
			if (!frameTiming.isValid)
			{
				// No XR frame paces the loop (session idle or recovering): wait for
				// window messages briefly instead of spinning a core.
				(void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
			}
		}

		Shutdown();
		// wParam is only meaningful for WM_QUIT; an XR-requested exit returns 0.
		return message.message == WM_QUIT ? static_cast<int>(message.wParam) : 0;
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
		// F1-F4 / XR preset buttons throw one of the built-in pitches transiently: the loaded
		// pitch profiles, the profile selection and the settings files stay untouched, and
		// selecting a profile (or the next random duel pitch) returns to the loaded repertoire.
		m_activePresetType = presetType;
		m_activePitchParameters = m_presetRepository.LoadPitchParameters(presetType);
		m_trajectoryZoneBiasLateral = 0.0;
		m_trajectoryZoneBiasVertical = 0.0;
		ApplyPitchParameterChangesAndLog(
		    L"pitch_config",
		    L"preset_apply");
	}

	void Application::ResetPitchSimulation()
	{
		RecordPitchOutcome();
		m_pendingPitchSamples.clear();
		// Reset the ball and controller collision history together.
		m_batPoseState = {};
		m_previousBatPoseState = {};
		m_batCollisionSuppressRemainingSeconds = 0.0;
		m_currentPitchPresentationName = BuildXrUiTelemetry().presetName;
		m_activePitchParameters.ballProperties = m_environmentSettings.ballProperties;
		m_pitchSimulationService.Reset(m_activePitchParameters);
		if (m_operationMode != OperationMode::TeeBatting)
			m_pitchFlightAnalysis.Reset(m_pitchSimulationService.GetPitchParameters(),
			    m_environmentSettings.pitchDistanceMeters, m_environmentSettings.moundHeightOffsetMeters,
			    m_strikeZoneSettings);
		if (m_operationMode == OperationMode::TeeBatting)
		{
			m_teeFlight = {};
			m_pitchSimulationService.SetBallState(BuildTeeBallState());
			m_forceTrajectoryResetOnNextFrame = true;
			return;
		}
		if (!UsesPitcher()) return;
		m_pitchDeliveryController.Reset();
		m_duelZonePassMarker.reset();
		m_duelPitchPredictedZonePassPoint = m_pitchFlightAnalysis.Preview().frontCrossing;
		if (m_duelPitchPredictedZonePassPoint)
			m_duelPitchPredictedZonePassPoint->y -= m_environmentSettings.moundHeightOffsetMeters;
		m_duelRound = {};
	}

	void Application::AdvancePitchSimulation(const std::chrono::duration<double> delta)
	{
		m_pitchSimulationService.Tick(delta);
		if (delta.count() > 0. && m_operationMode != OperationMode::TeeBatting && !m_duelRound.hasBallBeenBatted &&
		    !m_pitchFlightAnalysis.IsResolved())
			m_pendingPitchSamples.push_back(m_pitchSimulationService.GetBallState());
	}

	void Application::CommitPitchAnalysis(const Physics::Vector3& endpoint, const double cutoffTime)
	{
		for (const auto& sample : m_pendingPitchSamples)
			if (sample.flightTimeSeconds < cutoffTime)
				m_pitchFlightAnalysis.ObservePosition(sample.position);
		m_pendingPitchSamples.clear();
		m_pitchFlightAnalysis.ObservePosition(endpoint);
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
		        : Physics::Vector3{ 0.0,
		              initialSpeed * std::tan(m_activePitchParameters.launchElevationDegrees * std::numbers::pi / 180.0),
		              initialSpeed };
		if (!preserveDirectionalVelocityForCurrentPitch)
		{
			m_trajectoryZoneBiasLateral = 0.0;
			m_trajectoryZoneBiasVertical = 0.0;
		}
		m_activePitchParameters.ballProperties = m_environmentSettings.ballProperties;
		m_activePitchParameters.integratorType =
		    m_environmentSettings.integratorType;
		m_activePitchParameters.windVelocity =
		    m_environmentSettings.windVelocity;
		m_activePitchParameters.enableHeightDependentWind =
		    m_environmentSettings.enableHeightDependentWind;
		m_activePitchParameters.windShearFactor =
		    m_environmentSettings.windShearFactor;
		m_activePitchParameters.airDensityKilogramsPerCubicMeter = EnvironmentAirDensity();
		m_pitchMetricsSpeedMetersPerSecond =
		    Physics::Length(m_activePitchParameters.initialVelocity);
		if ((m_activePresetType == Data::PitchPresetType::Custom) &&
		    (m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			Data::PitchProfileEntry& activePitchProfile =
			    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)];
			activePitchProfile.pitchParameters = m_activePitchParameters;
			// Stored profiles keep the single-speed format; zone-solve velocity biases
			// are only applied to the currently simulated pitch.
			activePitchProfile.pitchParameters.initialVelocity = Physics::Vector3{
				0.0,
				initialSpeed * std::tan(m_activePitchParameters.launchElevationDegrees * std::numbers::pi / 180.0),
				initialSpeed
			};
		}
		m_pitcherArmSpeedScale = ComputeFastestPresetArmSpeedScale(
		    m_pitchProfiles,
		    m_activePitchParameters);

		if (!preserveDirectionalVelocityForCurrentPitch &&
		    m_operationMode == OperationMode::PitchTrajectory &&
		    HasDuelTargetZoneSelection())
		{
			// Refresh applies the solved velocity with preservation enabled and
			// performs the reset once, after updating the selected target.
			RefreshPitchTrajectoryFromZoneSelection(L"pitch_config");
			return;
		}
		ResetPitchSimulation();

		std::wostringstream pitchLog{};
		pitchLog << std::fixed << std::setprecision(2);
		pitchLog << L"[PitchConfig] preset=" << BuildActivePitchName();
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
		(void)LogPitchEvent(eventType, note, Data::SessionEventCategory::Pitch);
		UpdateWindowTitle();
	}

	void Application::HandleKeyDown(WPARAM keyCode, const bool isAutoRepeat)
	{
		const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (isAutoRepeat && !IsRepeatableAdjustmentKey(keyCode, controlPressed))
		{
			// Holding a toggle, mode, file or save key must not flip or rewrite it repeatedly.
			return;
		}
		if (controlPressed)
		{
			switch (keyCode)
			{
				case 'E':
					ExportCurrentSessionCsv();
					return;
				case 'H':
					BeginStrikeZoneCalibration(L"key_ctrl_h");
					return;
				case VK_LEFT:
					SelectPitchSettingsFileByOffset(-1, L"key_ctrl_left");
					return;
				case VK_RIGHT:
					SelectPitchSettingsFileByOffset(1, L"key_ctrl_right");
					return;
				case VK_UP:
					SelectPitchProfileByOffset(-1, L"key_ctrl_up");
					return;
				case VK_DOWN:
					SelectPitchProfileByOffset(1, L"key_ctrl_down");
					return;
				default:
					break;
			}
		}
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
			case 'F':
				SetOperationMode(OperationMode::Catcher, L"key_mode_f");
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
				CyclePitchTrajectoryDisplayMode();
				break;
			case '4':
				ToggleBattedTrajectoryVisibility();
				break;
			case '2':
				CycleStrikeZoneDisplayMode();
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
			case '9':
				CycleBatMaterial(L"key_9");
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
					Common::AppLog::Warning(SettingsLogCategory, L"key load skipped: no loadable pitch settings.");
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
			case Xr::UiCommandType::SetModeCatcher:
				SetOperationMode(OperationMode::Catcher, L"xr_mode");
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
			case Xr::UiCommandType::CyclePitchTrajectoryDisplayMode:
				CyclePitchTrajectoryDisplayMode();
				break;
			case Xr::UiCommandType::ToggleBattedTrajectoryVisibility:
				ToggleBattedTrajectoryVisibility();
				break;
			case Xr::UiCommandType::CycleStrikeZoneDisplayMode:
				CycleStrikeZoneDisplayMode();
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
			case Xr::UiCommandType::CycleBatMaterial:
				CycleBatMaterial(L"xr_metrics_panel");
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
				BeginStrikeZoneCalibration(L"xr_recalibrate");
				break;
			case Xr::UiCommandType::AdjustStrikeZoneBottomDown:
				AdjustStrikeZoneBoundary(false, -.01);
				break;
			case Xr::UiCommandType::AdjustStrikeZoneBottomUp:
				AdjustStrikeZoneBoundary(false, .01);
				break;
			case Xr::UiCommandType::AdjustStrikeZoneTopDown:
				AdjustStrikeZoneBoundary(true, -.01);
				break;
			case Xr::UiCommandType::AdjustStrikeZoneTopUp:
				AdjustStrikeZoneBoundary(true, .01);
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
		MarkPlaySettingsDirty();
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
				ResetPitchSimulation();
				m_forceTrajectoryResetOnNextFrame = true;
				m_duelZonePassMarker.reset();
				m_duelRound.hasPreviousBallState = false;
			}
		}
		m_playbackPaused = !m_playbackPaused;
		m_batPoseState = {};
		m_previousBatPoseState = {};
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
		// Explicit save (K key). Presets are never rewritten implicitly; saving a read-only bundled
		// preset (or a generated opposite-hand variant) writes a same-named customized copy into the
		// user directory, which from then on overrides that entry.
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		const Data::PitchSettingsEntry entry = m_activePitchSettingsEntry.value_or(
		    catalog.DescribeFile(ResolveUserDataPath(PitchSettingsJsonFileName)));
		const std::filesystem::path pitchSettingsFilePath = catalog.ResolvePitchSaveTarget(entry);
		const bool keepsBothHands = (pitchSettingsFilePath == entry.sourcePath) && !entry.mirrored &&
		                            entry.pitcherThrowsLeftHanded.has_value();

		Data::PitchRuntimeSettings settings{};
		settings.pitchParameters = m_activePitchParameters;
		if (m_environmentSettingsFilePath.empty())
		{
			settings.environmentSettingsFile = std::wstring{ DefaultEnvironmentSettingsJsonFileName };
		}
		else
		{
			const std::filesystem::path environmentPath = ResolveInputFilePath(m_environmentSettingsFilePath);
			// Bundled environments are referenced by name and resolved through the catalog.
			settings.environmentSettingsFile = catalog.IsBundledPath(environmentPath)
			    ? (environmentPath.filename() == Data::PresetLayout::DefaultEnvironmentSettingsFileName
			              ? std::wstring{ DefaultEnvironmentSettingsJsonFileName }
			              : environmentPath.filename().wstring())
			    : BuildPitchSettingsPathReference(environmentPath.wstring(), pitchSettingsFilePath.wstring());
		}
		settings.pitchProfiles = m_pitchProfiles;
		settings.activePitchProfileIndex = m_activePitchProfileIndex;
		settings.randomPitchProfileEnabled = m_randomPitchProfileEnabled;
		settings.pitcherThrowsLeftHanded = m_pitcherThrowsLeftHanded;
		settings.mirrorForLeftHandedPitcher = keepsBothHands;
		settings.displayName = keepsBothHands ? entry.displayName : entry.BuildLabel();

		std::wstring errorMessage{};
		if (!m_pitchPresetJsonStorage.SavePitchSettings(
		        pitchSettingsFilePath.wstring(),
		        settings,
		        errorMessage))
		{
			Common::AppLog::Error(SettingsLogCategory, L"failed to save pitch settings: " + errorMessage);
			return;
		}

		m_activePitchSettingsFilePath = pitchSettingsFilePath.wstring();
		m_activePitchSettingsFileName = keepsBothHands ? entry.id : pitchSettingsFilePath.filename().wstring();
		RefreshPitchSettingsFileList();
		m_activePitchSettingsEntry.reset();
		for (const Data::PitchSettingsEntry& candidate : m_pitchSettingsEntries)
		{
			if (candidate.id == m_activePitchSettingsFileName)
			{
				m_activePitchSettingsEntry = candidate;
			}
		}
		std::wstring saveNote = L"Saved " + m_activePitchSettingsFileName;
		(void)LogPitchEvent(L"json_save", saveNote);
		Common::AppLog::Info(SettingsLogCategory, L"pitch settings saved to " + m_activePitchSettingsFilePath);
	}

	void Application::ExportCurrentSessionCsv()
	{
		const std::filesystem::path filePath = ResolveUserDataPath(L"SessionHistory.current.csv");
		const Data::OperationResult result = m_sessionLogRepository.ExportCurrentSessionCsv(filePath);
		const std::wstring message = result
		    ? L"Current session exported to:\n" + filePath.wstring()
		    : L"Failed to export current session:\n" + result.errorMessage;
		ShowNonBlockingMessage(L"Session CSV export", message,
		    static_cast<UINT>(result ? MB_ICONINFORMATION : MB_ICONERROR));
	}

	Data::PresetCatalog Application::BuildPresetCatalog() const
	{
		return Data::PresetCatalog{ m_bundledContentDirectoryPath, m_userDataDirectoryPath };
	}

	std::wstring Application::BuildActivePitchName() const
	{
		if ((m_activePresetType == Data::PitchPresetType::Custom) &&
		    (m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			return m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)].name;
		}
		return std::wstring{ m_presetRepository.GetPresetName(m_activePresetType) };
	}

	std::wstring Application::BuildActivePitchSettingsLabel() const
	{
		for (const Data::PitchSettingsEntry& entry : m_pitchSettingsEntries)
		{
			if (entry.id == m_activePitchSettingsFileName)
			{
				return entry.BuildLabel();
			}
		}
		return m_activePitchSettingsFileName;
	}

	void Application::MarkPlaySettingsDirty()
	{
		m_playSettingsDirty = true;
		m_lastSettingsChangeTime = std::chrono::steady_clock::now();
	}

	void Application::MarkEnvironmentSettingsDirty()
	{
		m_environmentSettingsDirty = true;
		m_lastSettingsChangeTime = std::chrono::steady_clock::now();
	}

	void Application::FlushPendingSettingsSaves(const bool force)
	{
		if (!m_playSettingsDirty && !m_environmentSettingsDirty)
		{
			return;
		}
		if (!force &&
		    (std::chrono::steady_clock::now() - m_lastSettingsChangeTime < SettingsSaveDebounceInterval))
		{
			return;
		}
		if (m_environmentSettingsDirty)
		{
			SaveEnvironmentSettingsToJson();
		}
		if (m_playSettingsDirty)
		{
			SavePlaySettingsToJson();
		}
	}

	void Application::RefreshPitchSettingsFileList()
	{
		std::vector<std::wstring> warnings{};
		m_pitchSettingsEntries = BuildPresetCatalog().EnumeratePitchSettings(&warnings);
		for (const std::wstring& warning : warnings)
		{
			Common::AppLog::Warning(SettingsLogCategory, L"skipping pitch settings " + warning);
		}
		m_pitchSettingsFileNames.clear();
		m_pitchSettingsFileNames.reserve(m_pitchSettingsEntries.size());
		for (const Data::PitchSettingsEntry& entry : m_pitchSettingsEntries)
		{
			m_pitchSettingsFileNames.push_back(entry.id);
		}
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
		// Pending environment edits belong to the environment of the current file.
		FlushPendingSettingsSaves(true);
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		// A bare entry name selects a catalog entry (which may be a generated mirror variant);
		// anything else is an explicit file.
		std::optional<Data::PitchSettingsEntry> entry{};
		if (!std::filesystem::path{ filePath }.has_parent_path())
		{
			entry = catalog.FindPitchSettings(filePath);
		}
		if (!entry.has_value())
		{
			entry = catalog.DescribeFile(ResolveInputFilePath(filePath));
		}
		std::wstring errorMessage{};
		const std::optional<Data::PitchRuntimeSettings> loadedSettings =
		    catalog.LoadPitchSettings(*entry, errorMessage);
		if (!loadedSettings.has_value())
		{
			Common::AppLog::Error(SettingsLogCategory,
			    L"failed to load pitch settings (" + entry->sourcePath.wstring() + L"): " + errorMessage);
			return false;
		}

		m_activePitchParameters = loadedSettings->pitchParameters;
		m_pitcherThrowsLeftHanded = loadedSettings->pitcherThrowsLeftHanded;
		m_environmentSettingsFilePath =
		    catalog.ResolveEnvironmentReference(loadedSettings->environmentSettingsFile, entry->sourcePath).wstring();
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
		m_pitcherArmSpeedScale = ComputeFastestPresetArmSpeedScale(m_pitchProfiles, loadedSettings->pitchParameters);
		m_activePitchParameters =
		    m_pitchProfiles[static_cast<std::size_t>(m_activePitchProfileIndex)].pitchParameters;
		m_activePresetType = Data::PitchPresetType::Custom;
		m_activePitchSettingsFilePath = entry->sourcePath.wstring();
		m_activePitchSettingsFileName = entry->id;
		m_activePitchSettingsEntry = entry;
		RefreshPitchSettingsFileList();
		const std::wstring loadNote = L"Loaded " + entry->id;
		ApplyPitchParameterChangesAndLog(
		    L"json_load",
		    loadNote);
		if (!loadEventNote.empty())
		{
			(void)LogPitchEvent(L"pitch_settings_file", std::wstring{ loadEventNote });
		}
		Common::AppLog::Info(SettingsLogCategory,
		    L"pitch settings loaded from " + entry->sourcePath.wstring() + L" as " + entry->id +
		        L" (environment=" + m_environmentSettingsFilePath + L")");
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

		const std::filesystem::path legacyPath = ResolveInputFilePath(LegacyPitchSettingsJsonFileName);
		if (FileExists(legacyPath))
		{
			return LoadUserPresetFromJsonFile(legacyPath.wstring());
		}

		return false;
	}

	void Application::SaveEnvironmentSettingsToJson()
	{
		m_environmentSettingsDirty = false;
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		const std::filesystem::path currentPath =
		    m_environmentSettingsFilePath.empty()
		        ? ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName)
		        : ResolveInputFilePath(m_environmentSettingsFilePath);
		// Bundled environments are read-only: edits go to a same-named copy in the user directory.
		const std::filesystem::path targetPath = catalog.ResolveEnvironmentSaveTarget(currentPath);

		std::wstring errorMessage{};
		if (!m_runtimeSettingsJsonStorage.SaveEnvironment(
		        targetPath.wstring(),
		        m_environmentSettings,
		        errorMessage))
		{
			Common::AppLog::Error(SettingsLogCategory, L"failed to save environment settings: " + errorMessage);
			return;
		}
		if (targetPath != currentPath)
		{
			Common::AppLog::Info(SettingsLogCategory,
			    L"saved a customized copy of " + currentPath.wstring() + L" as " + targetPath.wstring());
			m_environmentSettingsFilePath = targetPath.wstring();
		}
	}

	bool Application::LoadEnvironmentSettingsFromJson()
	{
		FlushPendingSettingsSaves(true);
		const std::filesystem::path environmentSettingsFilePath =
		    m_environmentSettingsFilePath.empty()
		        ? ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName)
		        : ResolveInputFilePath(m_environmentSettingsFilePath);
		if (!FileExists(environmentSettingsFilePath))
		{
			Common::AppLog::Error(SettingsLogCategory,
			    L"required environment settings JSON not found: " + environmentSettingsFilePath.wstring());
			return false;
		}

		std::wstring errorMessage{};
		const std::optional<Data::EnvironmentRuntimeSettings> loadedSettings =
		    m_runtimeSettingsJsonStorage.LoadEnvironment(
		        environmentSettingsFilePath.wstring(),
		        errorMessage);
		if (!loadedSettings.has_value())
		{
			Common::AppLog::Error(SettingsLogCategory, L"failed to load environment settings: " + errorMessage);
			return false;
		}

		m_environmentSettings = *loadedSettings;
		m_environmentSettingsFilePath = environmentSettingsFilePath.wstring();
		ApplyEnvironmentSettingsChanges(L"json_environment_load", false);
		Common::AppLog::Info(SettingsLogCategory, L"environment settings loaded from " + m_environmentSettingsFilePath);
		return true;
	}

	void Application::SavePlaySettingsToJson()
	{
		m_playSettingsDirty = false;
		Data::PlayRuntimeSettings settings{};
		settings.duelPitchIntervalSeconds = m_duelPitchIntervalSeconds;
		settings.playbackSpeedPercent = m_playbackSpeedPercent;
		settings.duelPitchPreviewEnabled = m_duelPitchPreviewEnabled;
		settings.batUseRightHand = m_batUseRightHand;
		settings.batMaterial = m_batMaterial;
		settings.strikeZoneBottomMeters = m_strikeZoneSettings.bottomMeters;
		settings.strikeZoneHeightMeters = m_strikeZoneSettings.heightMeters;
		settings.strikeZoneWidthMeters = m_strikeZoneSettings.widthMeters;
		settings.strikeZoneConfigured = m_strikeZoneConfigured;
		settings.pitchTrajectoryDisplayMode = m_overlayVisibility.pitchTrajectoryDisplayMode;
		settings.strikeZoneDisplayMode = m_overlayVisibility.strikeZoneDisplayMode;
		settings.battedTrajectoryVisible = m_overlayVisibility.showBattedTrajectory;
		const std::uint32_t normalizedZoneMask =
		    NormalizeDuelZoneMask(m_duelTargetZoneMask);
		settings.duelTargetZoneMask = normalizedZoneMask;
		settings.duelTargetZoneIndex = Common::FindFirstSelectedZoneIndex(
		    normalizedZoneMask,
		    DuelZoneCount);
		// Presets are read-only, so the pitch selection state is remembered here.
		settings.selectedPitchSettingsFile = m_activePitchSettingsFileName;
		if ((m_activePitchProfileIndex >= 0) &&
		    (m_activePitchProfileIndex < static_cast<int>(m_pitchProfiles.size())))
		{
			settings.activePitchProfileIndex = m_activePitchProfileIndex;
		}
		settings.randomPitchProfileEnabled = m_randomPitchProfileEnabled;
		settings.settingsLayoutVersion = Data::CurrentSettingsLayoutVersion;

		std::wstring errorMessage{};
		if (!m_runtimeSettingsJsonStorage.SavePlay(
		        ResolveUserDataPath(PlaySettingsJsonFileName).wstring(),
		        settings,
		        errorMessage))
		{
			Common::AppLog::Error(SettingsLogCategory, L"failed to save play settings: " + errorMessage);
			return;
		}
		m_settingsLayoutVersion = Data::CurrentSettingsLayoutVersion;
	}

	bool Application::LoadPlaySettingsFromJson()
	{
		const std::filesystem::path playSettingsFilePath =
		    ResolveUserDataPath(PlaySettingsJsonFileName);
		if (!FileExists(playSettingsFilePath))
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
			Common::AppLog::Error(SettingsLogCategory, L"failed to load play settings: " + errorMessage);
			return false;
		}

		m_settingsLayoutVersion = loadedSettings->settingsLayoutVersion;
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
		m_batMaterial = loadedSettings->batMaterial;
		m_strikeZoneSettings = {loadedSettings->strikeZoneBottomMeters,
		    loadedSettings->strikeZoneHeightMeters, loadedSettings->strikeZoneWidthMeters};
		m_strikeZoneConfigured = loadedSettings->strikeZoneConfigured;
		if (m_strikeZoneConfigured) m_strikeZoneCalibrator.UseSavedZone();
		else m_strikeZoneCalibrator.Reset();
		m_overlayVisibility.pitchTrajectoryDisplayMode = loadedSettings->pitchTrajectoryDisplayMode;
		m_overlayVisibility.strikeZoneDisplayMode = loadedSettings->strikeZoneDisplayMode;
		m_overlayVisibility.showBattedTrajectory = loadedSettings->battedTrajectoryVisible;
		m_xrSystem->SetOverlayVisibility(m_overlayVisibility);
		m_renderBackend->SetOverlayVisibility(m_overlayVisibility);
		m_batPoseState = BatPoseState{};
		m_previousBatPoseState = BatPoseState{};
		m_batOverlayState.reset();
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
		if (m_duelRound.isWaitingForNextPitch)
		{
			m_duelRound.cooldownRemainingSeconds = std::clamp(
			    m_duelRound.cooldownRemainingSeconds,
			    0.0,
			    m_duelPitchIntervalSeconds);
		}

		std::wostringstream playLog{};
		playLog << std::fixed << std::setprecision(2);
		playLog << L"[PlayConfig] loaded interval=" << m_duelPitchIntervalSeconds;
		playLog << L", playback=" << m_playbackSpeedPercent;
		playLog << L", preview=" << (m_duelPitchPreviewEnabled ? L"on" : L"off");
		playLog << L", batHand=" << (m_batUseRightHand ? L"right" : L"left");
		playLog << L", batMaterial=" << BatMaterialLabel(m_batMaterial);
		playLog << L", pitchTraj=" << OverlayModeLabel(Common::GetPitchTrajectoryDisplayModeToken(m_overlayVisibility.pitchTrajectoryDisplayMode));
		playLog << L", zoneDisplay=" << OverlayModeLabel(Common::GetStrikeZoneDisplayModeToken(m_overlayVisibility.strikeZoneDisplayMode));
		playLog << L", battedTraj=" << (m_overlayVisibility.showBattedTrajectory ? L"on" : L"off");
		playLog << L", zones=" << BuildDuelZoneSelectionLabel(m_duelTargetZoneMask);
		playLog << L"\n";
		OutputDebugStringW(playLog.str().c_str());
		ApplyStrikeZoneSettingsChanges(false);
		(void)LogPitchEvent(L"play_config", L"json_play_load");
		UpdateWindowTitle();
		return true;
	}

	bool Application::RecoverBrokenUserSettingsFile(
	    const std::filesystem::path& filePath,
	    const std::wstring_view loadError,
	    const std::function<bool(const std::filesystem::path&)>& isLoadable)
	{
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		if (catalog.IsBundledPath(filePath))
		{
			const std::wstring notice = L"The bundled settings file\n  " + filePath.wstring() +
			    L"\ncould not be loaded:\n  " + std::wstring{ loadError } +
			    L"\nThe installation may be damaged; reinstall the application.";
			Common::AppLog::Error(SettingsLogCategory, notice);
			m_startupNotices.push_back(notice);
			return false;
		}

		const std::filesystem::path brokenPath = BuildBrokenFilePath(filePath);
		if (!MoveFileExW(filePath.c_str(), brokenPath.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			const std::wstring notice = L"The settings file\n  " + filePath.wstring() + L"\ncould not be loaded:\n  " +
			    std::wstring{ loadError } + L"\nand could not be renamed: " +
			    Common::TextConversion::FormatSystemErrorMessage(GetLastError());
			Common::AppLog::Error(SettingsLogCategory, notice);
			m_startupNotices.push_back(notice);
			return false;
		}

		std::wstring restoredFrom{};
		const std::filesystem::path backupPath = Common::BuildBackupFilePath(filePath);
		if (FileExists(backupPath) && isLoadable(backupPath) &&
		    CopyFileW(backupPath.c_str(), filePath.c_str(), TRUE))
		{
			restoredFrom = L"restored from its last good backup " + backupPath.filename().wstring();
		}
		else if (const std::filesystem::path defaultPath = catalog.FindBundledDefaultFor(filePath);
		         !defaultPath.empty() && CopyFileW(defaultPath.c_str(), filePath.c_str(), TRUE))
		{
			restoredFrom = L"restored from the bundled default " + defaultPath.filename().wstring();
		}
		else
		{
			restoredFrom = L"the bundled preset of the same name is used instead";
		}
		const std::wstring notice = L"The settings file\n  " + filePath.wstring() + L"\ncould not be loaded:\n  " +
		    std::wstring{ loadError } + L"\nIt was renamed to " + brokenPath.filename().wstring() + L" and " +
		    restoredFrom + L".";
		Common::AppLog::Error(SettingsLogCategory, notice);
		m_startupNotices.push_back(notice);
		return true;
	}

	bool Application::LoadStartupSettings(std::wstring& fatalErrorMessage)
	{
		fatalErrorMessage.clear();
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		std::wstring errorMessage{};

		// 1. Play settings first: they remember the selected pitch settings entry.
		const std::filesystem::path playPath = ResolveUserDataPath(PlaySettingsJsonFileName);
		const auto isLoadablePlay = [this](const std::filesystem::path& path)
		{
			std::wstring ignored{};
			return m_runtimeSettingsJsonStorage.LoadPlay(path.wstring(), ignored).has_value();
		};
		std::optional<Data::PlayRuntimeSettings> play =
		    m_runtimeSettingsJsonStorage.LoadPlay(playPath.wstring(), errorMessage);
		if (!play.has_value())
		{
			if (RecoverBrokenUserSettingsFile(playPath, errorMessage, isLoadablePlay))
			{
				play = m_runtimeSettingsJsonStorage.LoadPlay(playPath.wstring(), errorMessage);
			}
			if (!play.has_value())
			{
				fatalErrorMessage = playPath.wstring() + L": " + errorMessage;
				return false;
			}
		}

		// 2. User pitch settings files that no longer load are renamed and restored.
		const auto isLoadablePitch = [this](const std::filesystem::path& path)
		{
			std::wstring ignored{};
			return m_pitchPresetJsonStorage.LoadPitchSettings(path.wstring(), ignored).has_value();
		};
		std::error_code iteratorError{};
		std::vector<std::filesystem::path> userPitchFiles{};
		for (std::filesystem::directory_iterator iterator(catalog.GetUserDirectory(),
		         std::filesystem::directory_options::skip_permission_denied, iteratorError);
		     !iteratorError && iterator != std::filesystem::directory_iterator{}; iterator.increment(iteratorError))
		{
			std::error_code entryError{};
			if (iterator->is_regular_file(entryError) && Data::IsPitchSettingsFileName(iterator->path().filename().wstring()))
			{
				userPitchFiles.push_back(iterator->path());
			}
		}
		for (const std::filesystem::path& file : userPitchFiles)
		{
			if (!m_pitchPresetJsonStorage.LoadPitchSettings(file.wstring(), errorMessage).has_value())
			{
				(void)RecoverBrokenUserSettingsFile(file, errorMessage, isLoadablePitch);
			}
		}

		// 3. The remembered pitch settings entry, else the user's file, else the first entry.
		RefreshPitchSettingsFileList();
		std::vector<std::wstring> candidates{};
		if (!play->selectedPitchSettingsFile.empty())
		{
			candidates.push_back(play->selectedPitchSettingsFile);
		}
		candidates.push_back(std::wstring{ PitchSettingsJsonFileName });
		candidates.insert(candidates.end(), m_pitchSettingsFileNames.begin(), m_pitchSettingsFileNames.end());
		bool pitchSettingsLoaded = false;
		for (const std::wstring& candidate : candidates)
		{
			if (std::find(m_pitchSettingsFileNames.begin(), m_pitchSettingsFileNames.end(), candidate) !=
			        m_pitchSettingsFileNames.end() &&
			    LoadUserPresetFromJsonFile(candidate, L"startup"))
			{
				pitchSettingsLoaded = true;
				break;
			}
		}
		if (!pitchSettingsLoaded)
		{
			const std::filesystem::path legacyPath = ResolveInputFilePath(LegacyPitchSettingsJsonFileName);
			pitchSettingsLoaded = FileExists(legacyPath) && LoadUserPresetFromJsonFile(legacyPath.wstring(), L"startup");
		}
		if (!pitchSettingsLoaded)
		{
			fatalErrorMessage = L"No loadable pitch settings (Presets\\Pitch\\PitchSettings.*.json) were found.";
			return false;
		}
		if (play->selectedPitchSettingsFile == m_activePitchSettingsFileName)
		{
			if (play->randomPitchProfileEnabled.has_value())
			{
				m_randomPitchProfileEnabled = *play->randomPitchProfileEnabled;
			}
			const int profileIndex = play->activePitchProfileIndex.value_or(-1);
			if ((profileIndex >= 0) && (profileIndex < static_cast<int>(m_pitchProfiles.size())) &&
			    (profileIndex != m_activePitchProfileIndex))
			{
				m_activePitchProfileIndex = profileIndex;
				m_activePitchParameters = m_pitchProfiles[static_cast<std::size_t>(profileIndex)].pitchParameters;
				ApplyPitchParameterChangesAndLog(L"pitch_profile",
				    L"profile=" + m_pitchProfiles[static_cast<std::size_t>(profileIndex)].name + L",startup_restore");
			}
		}

		// 4. The environment referenced by the pitch settings.
		std::filesystem::path environmentPath = ResolveInputFilePath(m_environmentSettingsFilePath);
		if (!FileExists(environmentPath))
		{
			const std::wstring notice = L"The environment settings file\n  " + environmentPath.wstring() +
			    L"\nreferenced by " + m_activePitchSettingsFileName + L" does not exist; " +
			    std::wstring{ DefaultEnvironmentSettingsJsonFileName } + L" is used instead.";
			Common::AppLog::Warning(SettingsLogCategory, notice);
			m_startupNotices.push_back(notice);
			environmentPath = ResolveUserDataPath(DefaultEnvironmentSettingsJsonFileName);
			m_environmentSettingsFilePath = environmentPath.wstring();
		}
		if (!m_runtimeSettingsJsonStorage.LoadEnvironment(environmentPath.wstring(), errorMessage).has_value())
		{
			const auto isLoadableEnvironment = [this](const std::filesystem::path& path)
			{
				std::wstring ignored{};
				return m_runtimeSettingsJsonStorage.LoadEnvironment(path.wstring(), ignored).has_value();
			};
			if (!RecoverBrokenUserSettingsFile(environmentPath, errorMessage, isLoadableEnvironment))
			{
				fatalErrorMessage = environmentPath.wstring() + L": " + errorMessage;
				return false;
			}
			// A removed user override falls back to the bundled environment of the same name.
			const std::filesystem::path resolved = catalog.ResolveEnvironmentReference(
			    environmentPath.filename().wstring(), m_activePitchSettingsFilePath);
			m_environmentSettingsFilePath = resolved.wstring();
		}
		if (!LoadEnvironmentSettingsFromJson())
		{
			fatalErrorMessage = L"The environment settings could not be loaded: " + m_environmentSettingsFilePath;
			return false;
		}

		// 5. Apply the play settings last; they retarget the zone-dependent pitch state.
		if (!LoadPlaySettingsFromJson())
		{
			fatalErrorMessage = playPath.wstring() + L": the play settings could not be applied.";
			return false;
		}
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

		const std::filesystem::path bundledPresetsDirectory =
		    FindBundledPresetsDirectory(moduleFilePath);
		if (bundledPresetsDirectory.empty())
		{
			errorMessage = L"Failed to locate the bundled Presets folder next to the executable.";
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
			errorMessage =
			    L"Failed to create the user data directory: " +
			    Common::TextConversion::DescribeErrorCode(createDirectoryError);
			return false;
		}

		m_bundledContentDirectoryPath = bundledPresetsDirectory.wstring();
		m_userDataDirectoryPath = userDataDirectory.wstring();
		return true;
	}

	bool Application::EnsureRequiredUserSettingsFiles(
	    std::wstring& errorMessage)
	{
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		if (!catalog.EnsureUserFiles(errorMessage))
		{
			return false;
		}

		// One-time migration to the read-only Presets layout. Earlier versions copied every bundled
		// preset into the user directory, where the copies hid all later preset updates.
		const std::filesystem::path playPath = catalog.GetUserPlaySettingsPath();
		std::wstring playError{};
		std::optional<Data::PlayRuntimeSettings> play =
		    m_runtimeSettingsJsonStorage.LoadPlay(playPath.wstring(), playError);
		m_settingsLayoutVersion = play.has_value() ? play->settingsLayoutVersion : 0;
		if (m_settingsLayoutVersion >= Data::CurrentSettingsLayoutVersion)
		{
			return true;
		}
		const std::vector<std::filesystem::path> legacyCopies = catalog.FindLegacyProvisionedCopies();
		if (!legacyCopies.empty())
		{
			std::wstring moveError{};
			const std::optional<std::filesystem::path> backupDirectory =
			    catalog.MoveToLegacyBackup(legacyCopies, moveError);
			if (!moveError.empty())
			{
				Common::AppLog::Error(SettingsLogCategory, moveError);
			}
			if (backupDirectory.has_value())
			{
				const std::wstring notice =
				    L"Bundled presets are now read directly from the application's Presets folder, so preset "
				    L"updates take effect automatically. " + std::to_wstring(legacyCopies.size()) +
				    L" preset copies made by an earlier version were moved to:\n  " + backupDirectory->wstring() +
				    L"\nTo keep a customized preset, copy it back into the settings folder under a new name.";
				Common::AppLog::Info(SettingsLogCategory, notice);
				m_startupNotices.push_back(notice);
			}
		}
		if (play.has_value())
		{
			play->settingsLayoutVersion = Data::CurrentSettingsLayoutVersion;
			std::wstring saveError{};
			if (!m_runtimeSettingsJsonStorage.SavePlay(playPath.wstring(), *play, saveError))
			{
				Common::AppLog::Error(SettingsLogCategory, L"failed to record the settings layout version: " + saveError);
			}
		}
		m_settingsLayoutVersion = Data::CurrentSettingsLayoutVersion;
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
			Common::AppLog::Error(L"SessionLog", L"result upsert failed: " + upsertResult.errorMessage);
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

	void Application::ApplyOverlayVisibilityChanges(std::wstring_view note)
	{
		m_xrSystem->SetOverlayVisibility(m_overlayVisibility);
		m_renderBackend->SetOverlayVisibility(m_overlayVisibility);

		std::wstring noteText = std::wstring{ note };
		if (noteText.empty())
		{
			noteText = L"updated";
		}

		std::wostringstream overlayLog{};
		overlayLog << L"[OverlayConfig] pitchTrajectory=" << OverlayModeLabel(Common::GetPitchTrajectoryDisplayModeToken(m_overlayVisibility.pitchTrajectoryDisplayMode));
		overlayLog << L", battedTrajectory=" << (m_overlayVisibility.showBattedTrajectory ? L"On" : L"Off");
		overlayLog << L", zone=" << OverlayModeLabel(Common::GetStrikeZoneDisplayModeToken(m_overlayVisibility.strikeZoneDisplayMode));
		overlayLog << L", floor=" << (m_overlayVisibility.showFloorMesh ? L"On" : L"Off");
		overlayLog << L", note=" << noteText << L"\n";
		OutputDebugStringW(overlayLog.str().c_str());

		(void)LogPitchEvent(L"overlay_config", noteText);
		UpdateWindowTitle();
	}

	void Application::CyclePitchTrajectoryDisplayMode()
	{
		m_overlayVisibility.pitchTrajectoryDisplayMode =
		    Common::NextPitchTrajectoryDisplayMode(m_overlayVisibility.pitchTrajectoryDisplayMode);
		ApplyOverlayVisibilityChanges(L"cycle_pitch_trajectory");
		MarkPlaySettingsDirty();
	}

	void Application::ToggleBattedTrajectoryVisibility()
	{
		m_overlayVisibility.showBattedTrajectory = !m_overlayVisibility.showBattedTrajectory;
		ApplyOverlayVisibilityChanges(L"toggle_batted_trajectory");
		MarkPlaySettingsDirty();
	}

	void Application::CycleStrikeZoneDisplayMode()
	{
		m_overlayVisibility.strikeZoneDisplayMode =
		    Common::NextStrikeZoneDisplayMode(m_overlayVisibility.strikeZoneDisplayMode);
		ApplyOverlayVisibilityChanges(L"cycle_zone");
		MarkPlaySettingsDirty();
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
		// Changing bat geometry or its world origin is not physical swing motion.
		m_batPoseState = {};
		m_previousBatPoseState = {};
		m_environmentSettings.pitchDistanceMeters = std::clamp(
		    m_environmentSettings.pitchDistanceMeters,
		    MinimumPitchDistanceMeters,
		    MaximumPitchDistanceMeters);
		Common::SetPitchDistanceMeters(m_environmentSettings.pitchDistanceMeters);
		m_environmentSettings.moundHeightOffsetMeters = std::clamp(
		    m_environmentSettings.moundHeightOffsetMeters,
		    0.0,
		    0.50);
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
		m_environmentSettings.ballProperties.restitutionAtReferenceSpeed = std::clamp(
		    m_environmentSettings.ballProperties.restitutionAtReferenceSpeed,
		    MinimumBallRestitution,
		    MaximumBallRestitution);
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
			profile.pitchParameters.ballProperties = m_environmentSettings.ballProperties;
			profile.pitchParameters.integratorType =
			    m_environmentSettings.integratorType;
			profile.pitchParameters.windVelocity =
			    m_environmentSettings.windVelocity;
			profile.pitchParameters.enableHeightDependentWind =
			    m_environmentSettings.enableHeightDependentWind;
			profile.pitchParameters.windShearFactor =
			    m_environmentSettings.windShearFactor;
			profile.pitchParameters.airDensityKilogramsPerCubicMeter = EnvironmentAirDensity();
		}
		m_activePitchParameters.integratorType =
		    m_environmentSettings.integratorType;
		m_activePitchParameters.windVelocity =
		    m_environmentSettings.windVelocity;
		m_activePitchParameters.enableHeightDependentWind =
		    m_environmentSettings.enableHeightDependentWind;
		m_activePitchParameters.windShearFactor =
		    m_environmentSettings.windShearFactor;
		m_activePitchParameters.airDensityKilogramsPerCubicMeter = EnvironmentAirDensity();
		ResetPitchSimulation();
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
		envLog << L", windVel=(" << m_environmentSettings.windVelocity.x << L", "
		       << m_environmentSettings.windVelocity.y << L", "
		       << m_environmentSettings.windVelocity.z << L")";
		envLog << L", heightWind="
		       << (m_environmentSettings.enableHeightDependentWind ? L"On" : L"Off");
		envLog << L", windShear=" << m_environmentSettings.windShearFactor;
		envLog << L", bat=(r=" << m_environmentSettings.batRadiusMeters
		       << L",l=" << m_environmentSettings.batLengthMeters
		       << L",angle=" << m_environmentSettings.batAngleOffsetDegrees
		       << L",yaw=" << m_environmentSettings.batYawOffsetDegrees
		       << L")";
		envLog << L", ballCOR=" << m_environmentSettings.ballProperties.restitutionAtReferenceSpeed;
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
		envNote << L",wind=(" << m_environmentSettings.windVelocity.x << L","
		        << m_environmentSettings.windVelocity.y << L","
		        << m_environmentSettings.windVelocity.z << L")";
		envNote << L",heightWind="
		        << (m_environmentSettings.enableHeightDependentWind ? L"on" : L"off");
		envNote << L",shear=" << m_environmentSettings.windShearFactor;
		envNote << L",bat=(" << m_environmentSettings.batRadiusMeters
		        << L"," << m_environmentSettings.batLengthMeters
		        << L"," << m_environmentSettings.batAngleOffsetDegrees
		        << L"," << m_environmentSettings.batYawOffsetDegrees << L")";
		envNote << L",ballCOR=" << m_environmentSettings.ballProperties.restitutionAtReferenceSpeed;
		envNote << L",groundFanR=" << m_environmentSettings.groundFanRadiusMeters;
		envNote << L",integrator="
		        << Physics::GetPitchIntegratorName(
		               m_environmentSettings.integratorType);
		envNote << L"," << noteText;
		(void)LogPitchEvent(L"environment_config", envNote.str());
		UpdateWindowTitle();
		if (persistToJson)
		{
			MarkEnvironmentSettingsDirty();
		}
	}

	void Application::ToggleMoundHeightOffset()
	{
		m_environmentSettings.enableMoundHeightOffset =
		    !m_environmentSettings.enableMoundHeightOffset;
		ApplyEnvironmentSettingsChanges(L"toggle_mound_offset");
	}

	void Application::BeginStrikeZoneCalibration(const std::wstring_view note)
	{
		m_strikeZoneCalibrator.Reset();
		m_strikeZoneConfigured = false;
		m_strikeZoneCalibrationNote = note;
		MarkPlaySettingsDirty();
		OutputDebugStringW(L"[StrikeZone] calibration started: stand upright for three seconds.\n");
	}

	void Application::ApplyStrikeZoneSettingsChanges(const bool persist)
	{
		Common::SetStrikeZoneRuntimeSettings(m_strikeZoneSettings);
		if (m_operationMode != OperationMode::TeeBatting)
			m_pitchFlightAnalysis.SetPendingZone(m_strikeZoneSettings);
		if (m_operationMode == OperationMode::PitchTrajectory && HasDuelTargetZoneSelection())
			RefreshPitchTrajectoryFromZoneSelection(L"strike_zone_change");
		else if (m_operationMode == OperationMode::BattingDuel && !m_pitchDeliveryController.HasReleased())
			StartRandomBattingDuelPitch(L"strike_zone_change");
		else if (m_operationMode == OperationMode::TeeBatting && !m_teeFlight.isBallInFlight && HasDuelTargetZoneSelection())
			RefreshTeeBallSpawnFromZoneSelection(L"strike_zone_change");
		UpdateWindowTitle();
		if (persist) MarkPlaySettingsDirty();
	}

	void Application::AdjustStrikeZoneBoundary(const bool upper, const double deltaMeters)
	{
		m_strikeZoneCalibrator.UseManualZone();
		m_strikeZoneConfigured = true;
		(void)Common::AdjustStrikeZoneBoundary(m_strikeZoneSettings, upper, deltaMeters);
		ApplyStrikeZoneSettingsChanges(true);
		std::wostringstream note;
		note << L"strike_zone_manual,bottom=" << m_strikeZoneSettings.bottomMeters
		     << L",top=" << m_strikeZoneSettings.bottomMeters + m_strikeZoneSettings.heightMeters;
		(void)LogPitchEvent(L"play_config", note.str());
	}

	void Application::UpdateStrikeZoneCalibration()
	{
		if (m_strikeZoneCalibrator.IsComplete()) return;
		const auto result = m_strikeZoneCalibrator.AddSample(m_xrSystem->GetFloorEyeHeightSample());
		if (!result) return;
		m_strikeZoneSettings.bottomMeters = result->strikeZone.bottomMeters;
		m_strikeZoneSettings.heightMeters = result->strikeZone.heightMeters;
		m_strikeZoneConfigured = true;
		ApplyStrikeZoneSettingsChanges(true);
		(void)LogPitchEvent(L"play_config", m_strikeZoneCalibrationNote);
		std::wostringstream log;
		log << std::fixed << std::setprecision(3)
		    << L"[StrikeZone] calibrated from tracked floor-relative eyes: eyeY=" << result->eyeHeightMeters
		    << L", playerHeight=" << result->playerHeightMeters
		    << L", bottom=" << result->strikeZone.bottomMeters
		    << L", height=" << result->strikeZone.heightMeters << L"\n";
		OutputDebugStringW(log.str().c_str());
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
		if (m_duelRound.isWaitingForNextPitch)
		{
			m_duelRound.cooldownRemainingSeconds = std::clamp(
			    m_duelRound.cooldownRemainingSeconds,
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
		MarkPlaySettingsDirty();
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
		MarkPlaySettingsDirty();
	}

	void Application::ToggleBatHandedness(const std::wstring_view note)
	{
		m_batUseRightHand = !m_batUseRightHand;
		m_batPoseState = BatPoseState{};
		m_previousBatPoseState = BatPoseState{};
		m_batOverlayState.reset();
		m_lastBatSampleUsedRightHand = m_batUseRightHand;

		std::wstring batHandNote = L"bat_hand=";
		batHandNote += m_batUseRightHand ? L"right" : L"left";
		if (!note.empty())
		{
			batHandNote += L"," + std::wstring{ note };
		}
		(void)LogPitchEvent(L"play_config", batHandNote);
		UpdateWindowTitle();
		MarkPlaySettingsDirty();
	}

	void Application::CycleBatMaterial(const std::wstring_view note)
	{
		switch (m_batMaterial)
		{
			case Physics::BatMaterial::Wood: m_batMaterial = Physics::BatMaterial::Metal; break;
			case Physics::BatMaterial::Metal: m_batMaterial = Physics::BatMaterial::Urethane; break;
			case Physics::BatMaterial::Urethane: m_batMaterial = Physics::BatMaterial::Beginner; break;
			case Physics::BatMaterial::Beginner: m_batMaterial = Physics::BatMaterial::Wood; break;
		}
		if (m_batOverlayState) m_batOverlayState->material = m_batMaterial;
		std::wstring materialNote = L"bat_material=" + BatMaterialLabel(m_batMaterial);
		if (!note.empty()) materialNote += L"," + std::wstring{ note };
		(void)LogPitchEvent(L"play_config", materialNote);
		UpdateWindowTitle();
		MarkPlaySettingsDirty();
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
		MarkEnvironmentSettingsDirty();
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
		MarkEnvironmentSettingsDirty();
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
		MarkPlaySettingsDirty();
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
		MarkPlaySettingsDirty();
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
		MarkPlaySettingsDirty();
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
		std::wstring error;
		// Validate the complete selection before replacing anything that is active.
		const Data::PresetCatalog catalog = BuildPresetCatalog();
		const Data::PitchSettingsEntry selectedEntry =
		    m_pitchSettingsEntries[static_cast<std::size_t>(nextIndex)];
		const auto selectedSettings = catalog.LoadPitchSettings(selectedEntry, error);
		if (!selectedSettings)
		{
			Common::AppLog::Warning(SettingsLogCategory, L"cannot switch to " + nextFileName + L": " + error);
			return;
		}
		const std::filesystem::path selectedEnvironment = catalog.ResolveEnvironmentReference(
		    selectedSettings->environmentSettingsFile, selectedEntry.sourcePath);
		if (!m_runtimeSettingsJsonStorage.LoadEnvironment(selectedEnvironment.wstring(), error))
		{
			Common::AppLog::Warning(SettingsLogCategory, L"cannot switch to " + nextFileName +
			    L": invalid environment " + selectedEnvironment.wstring() + L": " + error);
			return;
		}
		if (!note.empty())
		{
			loadNote += L"," + std::wstring{ note };
		}
		if (!LoadUserPresetFromJsonFile(nextFileName, loadNote))
		{
			return;
		}
		if (!LoadEnvironmentSettingsFromJson()) return;
		MarkPlaySettingsDirty();
		if (m_operationMode == OperationMode::BattingDuel)
			StartRandomBattingDuelPitch(L"switch_file");
		else if (m_operationMode == OperationMode::Catcher)
			EnterCatcherMode();
		else if (m_operationMode == OperationMode::PitchTrajectory && HasDuelTargetZoneSelection())
			RefreshPitchTrajectoryFromZoneSelection(L"switch_file");
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
		MarkPlaySettingsDirty();
	}

	void Application::ApplyAutoZoneResultTag(
	    const Common::PitchZoneCall call,
	    const std::wstring_view note)
	{
		if (call != Common::PitchZoneCall::Strike && call != Common::PitchZoneCall::Ball) return;
		const bool isInStrikeZone = call == Common::PitchZoneCall::Strike;
		TagLatestPitchEventResult(
		    isInStrikeZone ? std::wstring_view{ L"strike" }
		                   : std::wstring_view{ L"ball" },
		    isInStrikeZone ? 1.0 : -1.0,
		    note);
		RecordPitchOutcome();
	}

	double Application::EnvironmentAirDensity() const noexcept
	{
		const double density = Physics::ComputeAirDensity(m_environmentSettings.altitudeMeters,
		    m_environmentSettings.temperatureCelsius, m_environmentSettings.relativeHumidityPercent);
		return Physics::IsPlausibleAirDensity(density) ? density : Physics::StandardAirDensityKilogramsPerCubicMeter;
	}

	bool Application::IsWaitingForNextDuelPitch() const noexcept
	{
		return UsesPitcher() && m_duelRound.isWaitingForNextPitch;
	}

	bool Application::UsesPitcher() const noexcept
	{
		return (m_operationMode == OperationMode::BattingDuel) || (m_operationMode == OperationMode::Catcher);
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
			case OperationMode::Catcher:
				telemetry.operationMode = Xr::UiOperationMode::Catcher;
				break;
			default:
				telemetry.operationMode = Xr::UiOperationMode::PitchTrajectory;
				break;
		}

		telemetry.presetName = BuildActivePitchName();
		telemetry.pitchDistanceMeters = m_environmentSettings.pitchDistanceMeters;
		telemetry.duelIntervalSeconds = m_duelPitchIntervalSeconds;
		telemetry.playbackSpeedPercent = m_playbackSpeedPercent;
		telemetry.playbackPaused = m_playbackPaused;
		telemetry.strikeZoneCalibration = m_strikeZoneCalibrator.GetProgress();
		telemetry.strikeZoneBottomMeters = m_strikeZoneSettings.bottomMeters;
		telemetry.strikeZoneHeightMeters = m_strikeZoneSettings.heightMeters;
		telemetry.strikeZoneTopMeters = m_strikeZoneSettings.bottomMeters + m_strikeZoneSettings.heightMeters;
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
		// The XR panel shows this text as the preset label (displayName + throwing hand).
		telemetry.pitchSettingsFileName = BuildActivePitchSettingsLabel();
		telemetry.pitchSettingsFileCount = static_cast<int>(
		    m_pitchSettingsFileNames.size());
		telemetry.randomPitchProfileEnabled = m_randomPitchProfileEnabled;
		telemetry.duelTargetZoneMask = NormalizeDuelZoneMask(m_duelTargetZoneMask);
		telemetry.duelPitchPreviewEnabled = m_duelPitchPreviewEnabled;
		telemetry.pitchInformationVisible = m_operationMode != OperationMode::BattingDuel ||
		    !m_randomPitchProfileEnabled || m_duelPitchPreviewEnabled || m_pitchDeliveryController.HasReleased();
		telemetry.batUseRightHand = m_batUseRightHand;
		telemetry.batMaterial = m_batMaterial;
		telemetry.batAngleOffsetDegrees =
		    m_environmentSettings.batAngleOffsetDegrees;
		telemetry.batYawOffsetDegrees =
		    m_environmentSettings.batYawOffsetDegrees;
		telemetry.batLengthMeters = m_environmentSettings.batLengthMeters;
		telemetry.batRadiusMeters = m_environmentSettings.batRadiusMeters;
		telemetry.pitchMetricsSpeedMetersPerSecond =
		    (std::max)(0.0, m_pitchMetricsSpeedMetersPerSecond);
		const BattedBallMetrics* metricsForTelemetry = nullptr;
		bool hasLiveBattedMetrics = false;
		if ((m_operationMode == OperationMode::BattingDuel) &&
		    m_duelRound.battedMetrics.hasContact)
		{
			metricsForTelemetry = &m_duelRound.battedMetrics;
			hasLiveBattedMetrics = !m_duelRound.isWaitingForNextPitch;
		}
		else if ((m_operationMode == OperationMode::TeeBatting) &&
		         m_teeBattedMetrics.hasContact)
		{
			metricsForTelemetry = &m_teeBattedMetrics;
			hasLiveBattedMetrics = m_teeFlight.isBallInFlight;
		}
		if (metricsForTelemetry != nullptr)
		{
			telemetry.hasBattedMetrics = true;
			telemetry.battedMetricsLive = hasLiveBattedMetrics;
			telemetry.battedMetricsHasGroundContact =
			    metricsForTelemetry->hasFirstGroundContact;
			telemetry.battedFirstGroundDistanceMeters =
			    metricsForTelemetry->firstGroundDistanceMeters;
			telemetry.battedResultLabel = metricsForTelemetry->hasFirstGroundContact
			    ? metricsForTelemetry->resultLabel : std::wstring{};
			telemetry.battedMaxHeightMeters =
			    metricsForTelemetry->maxHeightMeters;
			telemetry.battedLaunchVelocity =
			    metricsForTelemetry->launchVelocity;
		}
		return telemetry;
	}

	Common::FieldPresentationState Application::BuildFieldPresentationState() const
	{
		Common::FieldPresentationState state;
		const bool duel = m_operationMode == OperationMode::BattingDuel;
		const bool tee = m_operationMode == OperationMode::TeeBatting;
		const bool catcher = m_operationMode == OperationMode::Catcher;
		const bool pitcher = UsesPitcher();
		const auto& deliveredPitch = m_pitchSimulationService.GetPitchParameters();
		state.pitcher.visible = pitcher;
		state.pitcher.throwsLeftHanded = m_pitcherThrowsLeftHanded;
		state.pitcher.armSpeedScale = m_pitcherArmSpeedScale;
		state.pitcher.heldBallRadiusMeters = deliveredPitch.ballProperties.radiusMeters;
		state.pitcher.underhandToss = deliveredPitch.underhandToss;
		state.pitcher.secondsFromRelease = m_pitchDeliveryController.GetSecondsFromRelease();
		if (m_duelRound.isWaitingForNextPitch)
		{
			// The delivery clock stops during the interval: let the follow-through finish, then
			// walk the pitcher back to the rubber so the next windup starts from the set position.
			const double interval = catcher ? CatcherResetSeconds : m_duelPitchIntervalSeconds;
			state.pitcher.secondsFromRelease = m_pitchDeliveryController.HasReleased()
			    ? state.pitcher.secondsFromRelease +
			          (std::max)(0.0, interval - m_duelRound.cooldownRemainingSeconds)
			    : -Simulation::PitchDeliveryController::WindupSeconds;
			state.pitcher.recoveryProgress = m_pitchDeliveryController.HasReleased()
			    ? Common::PitcherRecoveryProgress(m_duelRound.cooldownRemainingSeconds)
			    : 1.0;
		}
		state.pitcher.moundHeightMeters = m_environmentSettings.moundHeightOffsetMeters;
		state.pitcher.releasePositionWorld = ConvertSimulationToCollisionWorldPosition(
		    { 0.0, deliveredPitch.releaseHeightMeters, 0.0 },
		    m_environmentSettings.pitchDistanceMeters, m_environmentSettings.moundHeightOffsetMeters);
		state.ballVisible = !pitcher || (!m_duelRound.isWaitingForNextPitch && m_pitchDeliveryController.HasReleased());
		const BattedBallMetrics* battedMetrics = tee ? &m_teeBattedMetrics
		    : (duel ? &m_duelRound.battedMetrics : nullptr);
		state.ballFlightPhase = tee
		    ? (m_teeFlight.isBallInFlight ? Common::BallFlightPhase::Batted : Common::BallFlightPhase::Stationary)
		    : (duel && m_duelRound.hasBallBeenBatted ? Common::BallFlightPhase::Batted
		        : (!pitcher || m_pitchDeliveryController.HasReleased()
		            ? Common::BallFlightPhase::Pitch : Common::BallFlightPhase::Stationary));
		state.trajectoryLifecycle.retainBattedOnBallReset = tee;
		state.trajectoryLifecycle.clearBatted = pitcher && !m_duelRound.isWaitingForNextPitch &&
		    !m_pitchDeliveryController.HasReleased();
		state.trajectoryLifecycle.battedContactSequence = m_battedContactSequence;
		if (state.ballFlightPhase == Common::BallFlightPhase::Batted &&
		    battedMetrics != nullptr && battedMetrics->hasContact)
			state.battedContactWorld = ConvertSimulationToCollisionWorldPosition(
			    battedMetrics->contactPosition, m_environmentSettings.pitchDistanceMeters,
			    m_environmentSettings.moundHeightOffsetMeters);
		auto& statistics = state.statistics;
		statistics.visible = true;
		statistics.pitchMetricsVisible = !duel || m_duelPitchPreviewEnabled ||
		    m_pitchFlightAnalysis.IsResolved() || m_duelRound.hasBallBeenBatted;
		const auto& plotZone = tee ? m_strikeZoneSettings : m_pitchFlightAnalysis.Zone();
		statistics.zoneBottomMeters = plotZone.bottomMeters;
		statistics.zoneTopMeters = plotZone.bottomMeters + plotZone.heightMeters;
		statistics.pitchBallRadiusMeters = deliveredPitch.ballProperties.radiusMeters;
		if (statistics.pitchMetricsVisible)
		{
			statistics.pitchName = tee ? L"Tee"
			    : catcher && m_catcherRound.phase == CatcherPhase::WaitingForSign ? CatcherSignName()
			                                                                     : m_currentPitchPresentationName;
			statistics.pitchSpeedMetersPerSecond = tee ? 0.0 : Physics::Length(deliveredPitch.initialVelocity);
			statistics.pitchSpinRpm = tee ? 0.0 : deliveredPitch.spinRpm;
			if (!tee)
			{
				const auto& report = duel && m_duelPitchPreviewEnabled && !m_pitchFlightAnalysis.IsResolved()
				    ? m_pitchFlightAnalysis.Preview() : m_pitchFlightAnalysis.Actual();
				statistics.zoneCall = m_pitchFlightAnalysis.Actual().call;
				statistics.pitchTrace = m_pitchFlightAnalysis.BuildMovementTrace(report);
				statistics.hasPitchBreak = report.breakMeters.has_value();
				if (report.breakMeters)
				{
					statistics.pitchHorizontalBreakMeters = report.breakMeters->x;
					statistics.pitchVerticalBreakMeters = report.breakMeters->y;
				}
			}
		}
		statistics.hasBattedMetrics = battedMetrics != nullptr && battedMetrics->hasContact &&
		    (!duel || m_pitchDeliveryController.HasReleased());
		if (statistics.hasBattedMetrics)
		{
			statistics.battedMetricsLive = tee ? m_teeFlight.isBallInFlight : !m_duelRound.isWaitingForNextPitch;
			statistics.battedHasGroundContact = battedMetrics->hasFirstGroundContact;
			statistics.battedLaunchSpeedMetersPerSecond = Physics::Length(battedMetrics->launchVelocity);
			statistics.battedLaunchAngleDegrees = std::atan2(battedMetrics->launchVelocity.y,
			    std::hypot(battedMetrics->launchVelocity.x, battedMetrics->launchVelocity.z)) *
			    180.0 / 3.14159265358979323846;
			statistics.battedDistanceMeters = battedMetrics->firstGroundDistanceMeters;
			statistics.battedMaxHeightMeters = battedMetrics->maxHeightMeters;
			statistics.hasBatContactPosition = battedMetrics->hasBatContactPosition;
			statistics.batContactFraction = battedMetrics->batContactFraction;
			statistics.batMaterial = m_batMaterial;
			if (battedMetrics->hasFirstGroundContact) statistics.battedResult = battedMetrics->resultLabel;
			statistics.contactBatLengthMeters = battedMetrics->contactBatLengthMeters;
		}
		if (catcher)
		{
			statistics.catcherMode = true;
			statistics.catcherCaught = m_catcherRound.caught;
			statistics.catcherPitches = m_catcherRound.pitches;
			statistics.catcherResult = m_catcherRound.result;
			state.catcherMitt = BuildCatcherMittState();
			state.catcherSignLine = L"サイン  " + CatcherSignName();
			switch (m_catcherRound.phase)
			{
			case CatcherPhase::WaitingForSign:
				state.catcherStatusLine = m_duelRound.cooldownRemainingSeconds > 0.0
				    ? L"投手準備中"
				    : L"A/X 次  B/Y 前  トリガーで決定";
				break;
			case CatcherPhase::Pitching:
				state.catcherStatusLine = L"ミットを合わせてトリガーで捕球";
				break;
			case CatcherPhase::Caught:
				state.catcherStatusLine = m_catcherRound.result + L"  離すと次の球";
				break;
			case CatcherPhase::Missed:
				state.catcherStatusLine = m_catcherRound.result;
				break;
			}
		}
		return state;
	}

	Simulation::BattedBallOutcome Application::JudgeBattedBallLanding(const BattedBallMetrics& metrics,
	    const Physics::Vector3& landingPosition, const double landingFlightTimeSeconds,
	    const std::optional<double> wallHeightMeters) const
	{
		// Simulation z grows toward the plate, so the field lies toward -z from home plate.
		const double homeZ = m_environmentSettings.pitchDistanceMeters + Common::HomePlateDepthMeters;
		const Physics::Vector3& launch = metrics.launchVelocity;
		const double horizontalLaunch = std::hypot(launch.x, launch.z);
		const bool groundBall = Simulation::ClassifyBattedBallType(
		    std::atan2(launch.y, horizontalLaunch) * 180.0 / std::numbers::pi) == Simulation::BattedBallType::GroundBall;
		// Ground balls are judged along their launch direction; air balls where they came down.
		const double sprayX = groundBall ? launch.x : landingPosition.x;
		const double sprayDepth = groundBall ? -launch.z : homeZ - landingPosition.z;
		Simulation::BattedBallFlight flight;
		flight.exitSpeedMetersPerSecond = Physics::Length(launch);
		flight.launchAngleDegrees = std::atan2(launch.y, horizontalLaunch) * 180.0 / std::numbers::pi;
		flight.sprayAngleDegrees = std::atan2(sprayX, sprayDepth) * 180.0 / std::numbers::pi;
		flight.landingDistanceMeters = metrics.firstGroundDistanceMeters;
		flight.hangTimeSeconds = (std::max)(0.0, landingFlightTimeSeconds - metrics.contactFlightTimeSeconds);
		flight.apexHeightMeters = metrics.maxHeightMeters;
		flight.rightHandedBatter = m_batUseRightHand;
		flight.wallHeightMeters = wallHeightMeters;
		return Simulation::JudgeBattedBall(flight);
	}

	std::optional<Physics::Vector3> Application::ProjectBattedBallLanding(
	    Physics::BallState state, const double groundContactHeight) const
	{
		// Fly a copy on with the same aerodynamics and no stadium in the way.
		Physics::PitchPhysicsEngine engine;
		const Baseball::PitchParameters& parameters = m_pitchSimulationService.GetPitchParameters();
		for (int step = 0; step < 20000; ++step)
		{
			const Physics::BallState previous = state;
			state = engine.Step(previous, parameters, FixedSimulationStep);
			if (state.position.y <= groundContactHeight)
			{
				return ResolveGroundCrossPoint(previous, state, groundContactHeight);
			}
		}
		return std::nullopt;
	}

	void Application::RefreshBatPoseFromXrInput()
	{
		const bool isBattingMode = m_operationMode == OperationMode::BattingDuel ||
		    m_operationMode == OperationMode::TeeBatting;
		const Xr::HandPoseState hand = m_batUseRightHand
		    ? m_xrSystem->GetRightHandPose() : m_xrSystem->GetLeftHandPose();
		const auto orientation = Common::BuildBatOrientation(hand.forward, hand.up,
		    m_environmentSettings.batAngleOffsetDegrees, m_environmentSettings.batYawOffsetDegrees);
		const auto axis = orientation ? std::optional<Physics::Vector3>(orientation->axis) : std::nullopt;
		const bool finitePosition = std::isfinite(hand.position.x) &&
		    std::isfinite(hand.position.y) && std::isfinite(hand.position.z);
		if (!isBattingMode || !hand.isValid || !axis || !finitePosition ||
		    !std::isfinite(hand.sampleTimeSeconds))
		{
			m_batPoseState = {};
			m_previousBatPoseState = {};
			m_batOverlayState.reset();
			return;
		}
		const bool sameSource = m_batPoseState.isValid &&
		    m_lastBatSampleUsedRightHand == m_batUseRightHand &&
		    m_batPoseState.referenceSpaceRevision == hand.referenceSpaceRevision;
		// Reject duplicate or reversed sample times and clear the sweep interval.
		if (sameSource && hand.sampleTimeSeconds <= m_batPoseState.sampleTimeSeconds)
		{
			m_previousBatPoseState = {};
			return;
		}
		const bool sameHandAndSpace = sameSource &&
		    hand.sampleTimeSeconds - m_batPoseState.sampleTimeSeconds <= 0.10 &&
		    Physics::Dot(m_batPoseState.upDirectionWorld, orientation->up) > 0.5 &&
		    Physics::Dot(m_batPoseState.axisDirectionWorld, *axis) > 0.5;
		m_previousBatPoseState = sameHandAndSpace ? m_batPoseState : BatPoseState{};
		m_batPoseState = { true, hand.position, *axis, hand.sampleTimeSeconds,
		    hand.referenceSpaceRevision, orientation->up,
		    hand.hasLinearVelocity, hand.linearVelocity,
		    hand.hasAngularVelocity, hand.angularVelocity };
		m_lastBatSampleUsedRightHand = m_batUseRightHand;
		const double length = m_environmentSettings.batLengthMeters;
		const auto endpoints = Physics::BatEndpointsFromGrip(hand.position, *axis, length);
		m_batOverlayState = Common::BatOverlayState{
			ConvertWorldToOverlaySimulationPosition(endpoints.knob,
			    m_environmentSettings.pitchDistanceMeters),
			ConvertWorldToOverlaySimulationPosition(endpoints.tip,
			    m_environmentSettings.pitchDistanceMeters),
			m_environmentSettings.batRadiusMeters,
			m_batMaterial,
			orientation->up
		};
	}
	bool Application::TryApplyBatCollision(Physics::BallState* const ballState)
	{
		if (ballState == nullptr || m_playbackPaused || !m_batPoseState.isValid ||
		    !m_previousBatPoseState.isValid || m_batCollisionSuppressRemainingSeconds > 1e-6)
			return false;
		const double sampleSeconds = m_batPoseState.sampleTimeSeconds - m_previousBatPoseState.sampleTimeSeconds;
		if (!(sampleSeconds > 0.0) || sampleSeconds > 0.10 ||
		    m_batPoseState.referenceSpaceRevision != m_previousBatPoseState.referenceSpaceRevision)
			return false;
		const bool hasPreviousBall = m_operationMode == OperationMode::BattingDuel
		    ? m_duelRound.hasPreviousBallState : m_teeFlight.hasPreviousBallState;
		if (!hasPreviousBall) return false;
		const auto& previousBall = m_operationMode == OperationMode::BattingDuel
		    ? m_duelRound.previousBallState : m_teeFlight.previousBallState;
		const auto worldPosition = [&](const Physics::Vector3& position)
		{
			return ConvertSimulationToCollisionWorldPosition(position,
			    m_environmentSettings.pitchDistanceMeters, m_environmentSettings.moundHeightOffsetMeters);
		};
		const auto segmentAt = [&](const Physics::Vector3& grip, const Physics::Vector3& axis)
		{
			const auto endpoints = Physics::BatEndpointsFromGrip(grip, axis, m_environmentSettings.batLengthMeters);
			return Physics::BatSegment{endpoints.knob, endpoints.tip};
		};
		const Common::BatOrientation previousOrientation{
			m_previousBatPoseState.axisDirectionWorld, m_previousBatPoseState.upDirectionWorld };
		const Common::BatOrientation currentOrientation{
			m_batPoseState.axisDirectionWorld, m_batPoseState.upDirectionWorld };
		const auto frameAngularVelocity = Common::ComputeBatAngularVelocity(
		    previousOrientation, currentOrientation, sampleSeconds);
		if (!frameAngularVelocity) return false;
		// The swept test interpolates bat endpoints linearly. A 40-50 rad/s swing
		// turns ~30 degrees per 90 Hz frame, where that chord shortens the bat and
		// skews its velocity, so split the frame into rigid-rotation sub-steps.
		constexpr double MaximumSubStepRadians = 8.0 * std::numbers::pi / 180.0;
		const int subSteps = std::clamp(static_cast<int>(std::ceil(
		    Physics::Length(*frameAngularVelocity) * sampleSeconds / MaximumSubStepRadians)), 1, 8);
		const Physics::Vector3 previousBallWorld = worldPosition(previousBall.position);
		const Physics::Vector3 currentBallWorld = worldPosition(ballState->position);
		std::optional<Physics::SweptBatContact> contact{};
		for (int subStep = 0; subStep < subSteps && !contact; ++subStep)
		{
			const double start = static_cast<double>(subStep) / subSteps;
			const double end = static_cast<double>(subStep + 1) / subSteps;
			const auto poseAt = [&](const double fraction)
			{
				const auto orientation = fraction >= 1.0 ? currentOrientation
				    : Common::RotateBatOrientation(previousOrientation, *frameAngularVelocity, fraction * sampleSeconds);
				return segmentAt(m_previousBatPoseState.originWorld +
				    (m_batPoseState.originWorld - m_previousBatPoseState.originWorld) * fraction, orientation.axis);
			};
			contact = Physics::FindSweptBatProfileContact(
			    previousBallWorld + (currentBallWorld - previousBallWorld) * start,
			    previousBallWorld + (currentBallWorld - previousBallWorld) * end,
			    poseAt(start), poseAt(end),
			    m_environmentSettings.batRadiusMeters, ballState->properties.radiusMeters,
			    sampleSeconds / subSteps);
			if (contact) contact->frameFraction = (subStep + contact->frameFraction) / subSteps;
		}
		if (!contact) return false;
		// Rigid-body surface velocity v_grip + w x r at the contact instant. Prefer
		// the runtime's IMU-based grip velocities, which are free of the finite
		// difference error between two predicted poses; reject implausible ones.
		const double fraction = contact->frameFraction;
		const Physics::Vector3 differencedGripVelocity =
		    (m_batPoseState.originWorld - m_previousBatPoseState.originWorld) / sampleSeconds;
		Physics::Vector3 gripVelocity = differencedGripVelocity;
		if (m_previousBatPoseState.hasLinearVelocity && m_batPoseState.hasLinearVelocity)
		{
			const Physics::Vector3 reported = m_previousBatPoseState.linearVelocityWorld +
			    (m_batPoseState.linearVelocityWorld - m_previousBatPoseState.linearVelocityWorld) * fraction;
			if (Physics::Length(reported - differencedGripVelocity) < 10.0) gripVelocity = reported;
		}
		Physics::Vector3 angularVelocity = *frameAngularVelocity;
		if (m_previousBatPoseState.hasAngularVelocity && m_batPoseState.hasAngularVelocity)
		{
			const Physics::Vector3 reported = m_previousBatPoseState.angularVelocityWorld +
			    (m_batPoseState.angularVelocityWorld - m_previousBatPoseState.angularVelocityWorld) * fraction;
			if (Physics::Length(reported - *frameAngularVelocity) < 25.0) angularVelocity = reported;
		}
		const Physics::Vector3 gripAtContact = m_previousBatPoseState.originWorld +
		    (m_batPoseState.originWorld - m_previousBatPoseState.originWorld) * fraction;
		Physics::BatBallCollisionInput input;
		input.ballProperties = ballState->properties;
		input.ballVelocityWorld = ballState->velocity;
		input.ballAngularVelocityWorld = ballState->spinAxis *
		    (ballState->spinRpm * 2.0 * std::numbers::pi / 60.0);
		// Ordinary bats move at the contact point; the beginner bat responds with the speed of
		// a regular sweet spot wherever the barrel meets the ball.
		const double responseShift = (Physics::BatResponseRatio(m_batMaterial, contact->batFraction) -
		    contact->batFraction) * m_environmentSettings.batLengthMeters;
		input.batContactVelocityWorld = gripVelocity + Physics::Cross(angularVelocity,
		    contact->batSurfacePosition + contact->batAxis * responseShift - gripAtContact);
		input.collisionNormal = contact->normal;
		input.batAxisDirectionWorld = contact->batAxis;
		input.batSurfaceOffsetWorld = contact->batSurfaceOffset;
		input.contactDistanceFromHandleMeters = contact->batFraction * m_environmentSettings.batLengthMeters;
		input.batLengthMeters = m_environmentSettings.batLengthMeters;
		input.batRadiusMeters = m_environmentSettings.batRadiusMeters;
		input.batMaterial = m_batMaterial;
		input.batMassKilograms = m_environmentSettings.batMassKilograms;
		const auto response = Physics::ResolveBatBallCollision(input);
		if (!response.hasImpulse) return false;
		const Physics::Vector3 contactPosition = ConvertCollisionWorldToSimulationPosition(
		    contact->batSurfacePosition + contact->normal * (ballState->properties.radiusMeters + 1e-4),
		    m_environmentSettings.pitchDistanceMeters, m_environmentSettings.moundHeightOffsetMeters);
		const double flightDeltaSeconds = (std::max)(0.0, ballState->flightTimeSeconds - previousBall.flightTimeSeconds);
		Physics::BallState contactState = *ballState;
		contactState.position = contactPosition;
		contactState.velocity = response.ballVelocityWorld;
		contactState.aerodynamicsModel = Physics::BallAerodynamicsModel::Physical;
		contactState.spinAxis = Physics::NormalizeOrZero(response.ballAngularVelocityWorld);
		contactState.spinRpm = Physics::Length(response.ballAngularVelocityWorld) *
		    60.0 / (2.0 * std::numbers::pi);
		contactState.flightTimeSeconds = previousBall.flightTimeSeconds + flightDeltaSeconds * contact->frameFraction;
		m_pitchSimulationService.SetBallState(contactState);
		const double remainingSeconds = (m_operationMode == OperationMode::TeeBatting && !m_teeFlight.isBallInFlight)
		    ? m_lastSimulationDeltaSeconds * (1.0 - contact->frameFraction)
		    : flightDeltaSeconds * (1.0 - contact->frameFraction);
		// Integrate the rest of the contact frame with the same fixed step as normal flight.
		for (double remaining = remainingSeconds; remaining > 1e-12;)
		{
			const double step = (std::min)(remaining, FixedSimulationStep.count());
			m_pitchSimulationService.Tick(std::chrono::duration<double>(step));
			remaining -= step;
		}
		*ballState = m_pitchSimulationService.GetBallState();
		m_batCollisionSuppressRemainingSeconds = BatCollisionSuppressDurationSeconds;
		BattedBallMetrics& activeMetrics =
		    (m_operationMode == OperationMode::TeeBatting)
		        ? m_teeBattedMetrics
		        : m_duelRound.battedMetrics;
		activeMetrics = BattedBallMetrics{};
		++m_battedContactSequence;
		activeMetrics.hasContact = true;
		activeMetrics.contactPosition = contactPosition;
		activeMetrics.launchVelocity = response.ballVelocityWorld;
		activeMetrics.hasBatContactPosition = true;
		activeMetrics.batContactFraction = std::clamp(input.contactDistanceFromHandleMeters / input.batLengthMeters, 0., 1.);
		activeMetrics.contactBatLengthMeters = input.batLengthMeters;
		activeMetrics.contactFlightTimeSeconds = contactState.flightTimeSeconds;
		activeMetrics.maxHeightMeters =
		    (std::max)(contactPosition.y, ballState->position.y) +
		    m_environmentSettings.moundHeightOffsetMeters;
		if (m_operationMode == OperationMode::BattingDuel)
		{
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_duelRound.hasPitchPassedZone = true;
		}

		// Contact feedback: a clean barrel hit is a short light tap, a miss-hit on
		// the handle or end stings longer and harder; both scale with impact.
		const double contactQuality = Physics::BatContactRestitutionScale(m_batMaterial,
		    activeMetrics.batContactFraction) / Physics::GetBatMaterialProfile(m_batMaterial).restitutionMultiplier;
		const double sting = std::clamp((1.0 - contactQuality) / (1.0 - Physics::MinimumBatContactResponse), 0.0, 1.0);
		const double impact = std::clamp(-Physics::Dot(input.ballVelocityWorld - input.batContactVelocityWorld,
		    input.collisionNormal) / 40.0, 0.35, 1.0);
		m_xrSystem->ApplyHapticPulse(m_batUseRightHand, (0.35 + 0.65 * sting) * impact, 0.015 + 0.06 * sting);

		std::wostringstream collisionNote{};
		collisionNote << std::fixed << std::setprecision(2);
		collisionNote << L"mode="
		              << ((m_operationMode == OperationMode::TeeBatting)
		                      ? L"tee"
		                      : L"duel");
		collisionNote << L",hand="
		              << (m_batUseRightHand ? L"right" : L"left");
		collisionNote << L",material=" << BatMaterialLabel(m_batMaterial);
		collisionNote << L",speed=" << Physics::Length(ballState->velocity);
		collisionNote << L",launch_speed=" << Physics::Length(response.ballVelocityWorld)
		              << L",bat_contact_speed=" << Physics::Length(input.batContactVelocityWorld)
		              << L",closing_normal_speed=" << -Physics::Dot(
		                  input.ballVelocityWorld - input.batContactVelocityWorld, input.collisionNormal)
		              << L",spin_rpm=" << contactState.spinRpm
		              << L",normal_cor=" << response.restitution
		              << L",friction_limited=" << response.tangentialRestitutionLimitedByFriction
		              << L",energy_limited=" << response.tangentialRestitutionLimitedByEnergy
		              << L",contact_from_handle=" << input.contactDistanceFromHandleMeters;
		(void)LogBattedBall(collisionNote.str(), response.ballVelocityWorld, activeMetrics.batContactFraction);
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
		m_teeFlight = {};
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
		m_teeFlight = {};
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
		// A mode switch is a natural point to persist pending settings changes.
		FlushPendingSettingsSaves(true);

		m_operationMode = mode;
		m_forceTrajectoryResetOnNextFrame = true;
		m_playbackPaused = false;
		m_trajectoryZoneBiasLateral = 0.0;
		m_trajectoryZoneBiasVertical = 0.0;
		m_batCollisionSuppressRemainingSeconds = 0.0;
		m_previousBatPoseState = BatPoseState{};
		m_duelRound = {};
		m_teeFlight = {};
		switch (m_operationMode)
		{
			case OperationMode::PitchTrajectory:
				m_duelZonePassMarker.reset();
				m_duelPitchPredictedZonePassPoint.reset();
				m_batPoseState = BatPoseState{};
				m_previousBatPoseState = BatPoseState{};
				m_batOverlayState.reset();
				RefreshPitchTrajectoryFromZoneSelection(L"mode_enter");
				break;
			case OperationMode::BattingDuel:
				StartRandomBattingDuelPitch(L"mode_enter");
				break;
			case OperationMode::TeeBatting:
				m_duelZonePassMarker.reset();
				m_duelPitchPredictedZonePassPoint.reset();
				RefreshTeeBallSpawnFromZoneSelection(L"mode_enter");
				break;
			case OperationMode::Catcher:
				m_duelZonePassMarker.reset();
				m_duelPitchPredictedZonePassPoint.reset();
				m_batPoseState = BatPoseState{};
				m_previousBatPoseState = BatPoseState{};
				m_batOverlayState.reset();
				EnterCatcherMode();
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
				case OperationMode::Catcher:
					return L"catcher";
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

	std::wstring Application::CatcherSignName() const
	{
		if (m_pitchProfiles.empty())
		{
			return m_currentPitchPresentationName;
		}
		const int index = std::clamp(m_catcherRound.signIndex, 0, static_cast<int>(m_pitchProfiles.size()) - 1);
		return m_pitchProfiles[static_cast<std::size_t>(index)].name;
	}

	std::optional<Common::CatcherMittState> Application::BuildCatcherMittState() const
	{
		if (m_operationMode != OperationMode::Catcher)
		{
			return std::nullopt;
		}
		// The mitt goes on the hand that does not throw.
		const bool mittOnRightHand = !m_batUseRightHand;
		const Xr::HandPoseState hand = mittOnRightHand ? m_xrSystem->GetRightHandPose() : m_xrSystem->GetLeftHandPose();
		if (!hand.isValid || !std::isfinite(hand.position.x) || !std::isfinite(hand.position.y) ||
		    !std::isfinite(hand.position.z))
		{
			return std::nullopt;
		}
		Common::CatcherMittState mitt;
		mitt.gripWorld = hand.position;
		mitt.forwardWorld = hand.forward;
		mitt.upWorld = hand.up;
		mitt.leftHand = !mittOnRightHand;
		const Xr::ControllerButtonState& buttons =
		    mittOnRightHand ? m_previousControllerInput.right : m_previousControllerInput.left;
		mitt.closedAmount = m_catcherRound.phase == CatcherPhase::Caught ? .75 : (buttons.triggerPressed ? 1.0 : 0.0);
		return mitt;
	}

	void Application::EnterCatcherMode()
	{
		EnsureDefaultPitchProfiles();
		m_catcherRound = {};
		if (!m_pitchProfiles.empty())
		{
			m_catcherRound.signIndex =
			    std::clamp(m_activePitchProfileIndex, 0, static_cast<int>(m_pitchProfiles.size()) - 1);
			m_activePitchProfileIndex = m_catcherRound.signIndex;
			m_activePresetType = Data::PitchPresetType::Custom;
			m_activePitchParameters =
			    m_pitchProfiles[static_cast<std::size_t>(m_catcherRound.signIndex)].pitchParameters;
		}
		ApplyPitchParameterChanges();
		m_duelRound.isWaitingForNextPitch = true;
		m_duelRound.cooldownRemainingSeconds = 0.0;
	}

	void Application::UpdateCatcherInput()
	{
		const Xr::ControllerInputState input = m_xrSystem->GetControllerInputState();
		const Xr::ControllerInputState previous = m_previousControllerInput;
		m_previousControllerInput = input;
		if (m_operationMode != OperationMode::Catcher)
		{
			return;
		}
		m_catcherRound.clockSeconds += m_lastFrameDeltaSeconds;
		const auto rising = [](const bool now, const bool before) { return now && !before; };
		const bool next = rising(input.left.primaryPressed, previous.left.primaryPressed) ||
		    rising(input.right.primaryPressed, previous.right.primaryPressed);
		const bool back = rising(input.left.secondaryPressed, previous.left.secondaryPressed) ||
		    rising(input.right.secondaryPressed, previous.right.secondaryPressed);
		const bool mittOnRightHand = !m_batUseRightHand;
		const Xr::ControllerButtonState& mitt = mittOnRightHand ? input.right : input.left;
		const Xr::ControllerButtonState& mittBefore = mittOnRightHand ? previous.right : previous.left;
		const bool squeezed = rising(mitt.triggerPressed, mittBefore.triggerPressed);
		const bool released = !mitt.triggerPressed && mittBefore.triggerPressed;
		// The laser hand's trigger belongs to the panels while the laser is out.
		const bool confirm = !input.uiPointerActive &&
		    (rising(input.left.triggerPressed, previous.left.triggerPressed) ||
		     rising(input.right.triggerPressed, previous.right.triggerPressed));
		if (squeezed)
		{
			m_catcherRound.squeezeSeconds = m_catcherRound.clockSeconds;
		}
		switch (m_catcherRound.phase)
		{
		case CatcherPhase::WaitingForSign:
		{
			const int count = static_cast<int>(m_pitchProfiles.size());
			if (count > 0 && (next || back))
			{
				m_catcherRound.signIndex = ((m_catcherRound.signIndex + (next ? 1 : -1)) % count + count) % count;
			}
			if (confirm && m_duelRound.cooldownRemainingSeconds <= 0.0)
			{
				StartCatcherPitch();
			}
			break;
		}
		case CatcherPhase::Caught:
			if (released)
			{
				ReturnToCatcherSign();
			}
			break;
		case CatcherPhase::Missed:
			if (released && m_catcherRound.missedSeconds > .3)
			{
				ReturnToCatcherSign();
			}
			break;
		case CatcherPhase::Pitching:
			break;
		}
	}

	void Application::StartCatcherPitch()
	{
		EnsureDefaultPitchProfiles();
		if (m_pitchProfiles.empty())
		{
			return;
		}
		const int index = std::clamp(m_catcherRound.signIndex, 0, static_cast<int>(m_pitchProfiles.size()) - 1);
		// Target: the pocket of the mitt as it is set now, or the middle of the zone without one.
		const double plateApexWorldZ =
		    -Common::DebugWorldLayout::BatterToPlateDistanceMeters + Common::HomePlateDepthMeters;
		Physics::Vector3 target{0.0, m_strikeZoneSettings.bottomMeters + m_strikeZoneSettings.heightMeters * .5,
		    plateApexWorldZ + .35};
		if (const auto mitt = BuildCatcherMittState())
		{
			target = Common::BuildCatcherMittFrame(*mitt).pocket;
		}
		target.x = std::clamp(target.x, -1.2, 1.2);
		target.y = std::clamp(target.y, .15, 2.4);
		target.z = std::clamp(target.z, plateApexWorldZ - .45, plateApexWorldZ + 1.5);
		// The pitcher misses his spot a little, and once in a while a lot.
		std::mt19937& random = GetThreadLocalRandomEngine();
		const bool wild = std::uniform_real_distribution<double>(0.0, 1.0)(random) < CatcherWildPitchChance;
		std::normal_distribution<double> miss(0.0, wild ? CatcherWildErrorMeters : CatcherCommandErrorMeters);
		Physics::Vector3 aim = target;
		aim.x += miss(random);
		aim.y = (std::max)(.05, aim.y + miss(random));

		Baseball::PitchParameters parameters = m_pitchProfiles[static_cast<std::size_t>(index)].pitchParameters;
		// Solve with the air the pitch will actually fly through.
		parameters.ballProperties = m_environmentSettings.ballProperties;
		parameters.integratorType = m_environmentSettings.integratorType;
		parameters.windVelocity = m_environmentSettings.windVelocity;
		parameters.enableHeightDependentWind = m_environmentSettings.enableHeightDependentWind;
		parameters.windShearFactor = m_environmentSettings.windShearFactor;
		parameters.airDensityKilogramsPerCubicMeter = EnvironmentAirDensity();
		const double depth = ConvertCollisionWorldToSimulationPosition(aim, m_environmentSettings.pitchDistanceMeters,
		    m_environmentSettings.moundHeightOffsetMeters).z;
		constexpr double Tolerance = .015;
		const DuelZoneCellBounds bounds{aim.x - Tolerance, aim.x + Tolerance, aim.y + Tolerance, aim.y - Tolerance};
		const auto velocity = ResolveTrajectoryZoneTargetVelocity(parameters, depth, -1, bounds,
		    Physics::Vector3{aim.x, aim.y, depth}, m_environmentSettings.moundHeightOffsetMeters);

		m_activePitchProfileIndex = index;
		m_activePresetType = Data::PitchPresetType::Custom;
		m_activePitchParameters = m_pitchProfiles[static_cast<std::size_t>(index)].pitchParameters;
		if (velocity)
		{
			m_activePitchParameters.initialVelocity.x = std::clamp(velocity->x, MinimumHorizontalSpeed, MaximumHorizontalSpeed);
			m_activePitchParameters.initialVelocity.y = std::clamp(velocity->y, MinimumVerticalSpeed, MaximumVerticalSpeed);
		}
		const int caught = m_catcherRound.caught, pitches = m_catcherRound.pitches;
		const double clock = m_catcherRound.clockSeconds, squeeze = m_catcherRound.squeezeSeconds;
		m_catcherRound = {};
		m_catcherRound.signIndex = index;
		m_catcherRound.caught = caught;
		m_catcherRound.pitches = pitches + 1;
		m_catcherRound.clockSeconds = clock;
		m_catcherRound.squeezeSeconds = squeeze;
		m_catcherRound.phase = CatcherPhase::Pitching;
		m_catcherRound.targetWorld = target;
		m_catcherRound.aimWorld = aim;
		std::wostringstream note{};
		note << std::fixed << std::setprecision(2) << L"catcher_sign,profile="
		     << m_pitchProfiles[static_cast<std::size_t>(index)].name << L",target=" << target.x << L"," << target.y
		     << L",aim=" << aim.x << L"," << aim.y << (wild ? L",wild" : L"") << (velocity ? L"" : L",solve=fallback");
		ApplyPitchParameterChangesAndLog(L"pitch_config", note.str(), true);
		m_duelRound.isWaitingForNextPitch = false;
	}

	void Application::CatchPitchIfReached(const Physics::BallState& previousState, Physics::BallState* const ballState)
	{
		const double pitchDistance = m_environmentSettings.pitchDistanceMeters;
		const double mound = m_environmentSettings.moundHeightOffsetMeters;
		const auto toWorld = [&](const Physics::Vector3& position)
		{ return ConvertSimulationToCollisionWorldPosition(position, pitchDistance, mound); };
		const auto toSimulation = [&](const Physics::Vector3& position)
		{ return ConvertCollisionWorldToSimulationPosition(position, pitchDistance, mound); };
		const std::optional<Common::CatcherMittState> mitt = BuildCatcherMittState();
		const std::optional<Common::CatcherMittFrame> frame =
		    mitt ? std::optional<Common::CatcherMittFrame>(Common::BuildCatcherMittFrame(*mitt)) : std::nullopt;
		const double now = m_catcherRound.clockSeconds;
		const auto holdInPocket = [&]()
		{
			if (frame)
			{
				ballState->position = toSimulation(frame->pocket);
			}
			ballState->velocity = {};
			m_pitchSimulationService.SetBallState(*ballState);
		};
		if (m_catcherRound.phase == CatcherPhase::Caught)
		{
			holdInPocket();
			return;
		}
		if (m_catcherRound.ballInPocket)
		{
			// The ball sits in the pocket for a moment waiting for the squeeze.
			if (m_catcherRound.squeezeSeconds >= m_catcherRound.arrivalSeconds - 1e-9)
			{
				holdInPocket();
				FinishCatcherPitch(L"捕球", true);
				return;
			}
			if (now - m_catcherRound.arrivalSeconds > CatcherLateSqueezeSeconds)
			{
				m_catcherRound.ballInPocket = false;
				// Pops out and drops in front of the plate.
				ballState->velocity = (frame ? frame->palm * 1.2 : Physics::Vector3{}) + Physics::Vector3{0.0, .5, 0.0};
				m_pitchSimulationService.SetBallState(*ballState);
				FinishCatcherPitch(L"落球", false);
				return;
			}
			holdInPocket();
			return;
		}
		const Physics::Vector3 from = toWorld(previousState.position);
		const Physics::Vector3 to = toWorld(ballState->position);
		if (frame)
		{
			// Closest approach of this frame's stretch of flight to the pocket.
			const Physics::Vector3 path = to - from;
			const double lengthSquared = Physics::LengthSquared(path);
			const double t = lengthSquared > 1e-12
			    ? std::clamp(Physics::Dot(frame->pocket - from, path) / lengthSquared, 0.0, 1.0) : 1.0;
			const Physics::Vector3 closest = from + path * t;
			const double distance = Physics::Length(frame->pocket - closest);
			m_catcherRound.closestApproachMeters = (std::min)(m_catcherRound.closestApproachMeters, distance);
			const Xr::ControllerButtonState& mittButtons =
			    m_batUseRightHand ? m_previousControllerInput.left : m_previousControllerInput.right;
			if (distance <= Common::CatcherMittPocketRadiusMeters)
			{
				m_catcherRound.arrivalSeconds = now;
				const double sinceSqueeze = now - m_catcherRound.squeezeSeconds;
				if (mittButtons.triggerPressed && sinceSqueeze <= CatcherEarlySqueezeSeconds)
				{
					holdInPocket();
					FinishCatcherPitch(sinceSqueeze <= CatcherCleanSqueezeSeconds ? L"ナイスキャッチ" : L"捕球", true);
					return;
				}
				if (mittButtons.triggerPressed)
				{
					// The mitt was already shut: the ball glances off it.
					ballState->position = toSimulation(closest + frame->palm * .05);
					ballState->velocity = (ballState->velocity - frame->palm * (1.3 * Physics::Dot(ballState->velocity, frame->palm))) * .3;
					m_pitchSimulationService.SetBallState(*ballState);
					FinishCatcherPitch(L"はじいた", false);
					return;
				}
				m_catcherRound.ballInPocket = true;
				holdInPocket();
				return;
			}
			if (distance <= Common::CatcherMittRimRadiusMeters && t < 1.0)
			{
				// Off the edge of the mitt.
				const Physics::Vector3 away = Physics::NormalizeOrZero(closest - frame->pocket);
				ballState->position = toSimulation(closest + away * .03);
				ballState->velocity = (ballState->velocity - away * (1.4 * Physics::Dot(ballState->velocity, away))) * .45;
				m_pitchSimulationService.SetBallState(*ballState);
				FinishCatcherPitch(L"はじいた", false);
				return;
			}
		}
		// Past the catcher without reaching the mitt: his fault if it was within reach.
		const double behind = frame ? frame->pocket.z : -Common::DebugWorldLayout::BatterToPlateDistanceMeters + 1.0;
		if (to.z > behind + .5 && ballState->velocity.z > 0.0)
		{
			FinishCatcherPitch(m_catcherRound.closestApproachMeters <= .45 ? L"パスボール" : L"ワイルドピッチ", false);
		}
	}

	void Application::FinishCatcherPitch(const std::wstring_view result, const bool caught)
	{
		m_catcherRound.result = std::wstring{ result };
		m_catcherRound.ballInPocket = false;
		m_catcherRound.missedSeconds = 0.0;
		if (caught)
		{
			m_catcherRound.phase = CatcherPhase::Caught;
			++m_catcherRound.caught;
			// A pop in the mitt hand, harder for a harder pitch.
			const double speed = Physics::Length(m_activePitchParameters.initialVelocity);
			m_xrSystem->ApplyHapticPulse(!m_batUseRightHand, std::clamp(speed / 45.0, .3, 1.0), .07);
		}
		else
		{
			m_catcherRound.phase = CatcherPhase::Missed;
		}
		(void)LogPitchEvent(L"catcher_result", L"result=" + m_catcherRound.result);
		UpdateWindowTitle();
	}

	void Application::ReturnToCatcherSign()
	{
		m_catcherRound.phase = CatcherPhase::WaitingForSign;
		m_catcherRound.ballInPocket = false;
		m_catcherRound.missedSeconds = 0.0;
		m_duelRound.isWaitingForNextPitch = true;
		m_duelRound.cooldownRemainingSeconds = CatcherResetSeconds;
	}

	// Everything a duel pitch depends on, chosen on the main thread. The zone
	// solve is a pure function of these inputs and may run on a worker thread.
	struct Application::DuelPitchPlan final
	{
		int profileIndex{ -1 };
		Baseball::PitchParameters parameters{};
		int zoneIndex{ -1 };
		std::optional<DuelZoneCellBounds> zoneBounds{};
		std::optional<Physics::Vector3> zoneTarget{};
		double pitchDistanceMeters{ 0.0 };
		double verticalOffsetMeters{ 0.0 };
		std::uint32_t zoneMask{ 0U };
		bool randomProfile{ false };
		Physics::Vector3 fallbackOffset{};
		std::shared_future<std::optional<TrajectoryTargetVelocity>> solution{};
	};

	namespace
	{
		[[nodiscard]] bool SamePitchParameters(const Baseball::PitchParameters& a, const Baseball::PitchParameters& b)
		{
			const auto sameVector = [](const Physics::Vector3& u, const Physics::Vector3& v)
			{ return u.x == v.x && u.y == v.y && u.z == v.z; };
			return sameVector(a.initialVelocity, b.initialVelocity) && a.launchElevationDegrees == b.launchElevationDegrees &&
			    a.usePhysicalAerodynamics == b.usePhysicalAerodynamics && a.underhandToss == b.underhandToss &&
			    a.ballProperties.radiusMeters == b.ballProperties.radiusMeters &&
			    a.ballProperties.massKilograms == b.ballProperties.massKilograms &&
			    a.releaseHeightMeters == b.releaseHeightMeters && sameVector(a.spinAxis, b.spinAxis) &&
			    sameVector(a.windVelocity, b.windVelocity) && a.spinRpm == b.spinRpm &&
			    a.dragCoefficient == b.dragCoefficient && a.liftCoefficient == b.liftCoefficient &&
			    a.integratorType == b.integratorType && a.useCoefficientTable == b.useCoefficientTable &&
			    a.enableHeightDependentWind == b.enableHeightDependentWind && a.windShearFactor == b.windShearFactor &&
			    a.enableSeamPerturbation == b.enableSeamPerturbation &&
			    a.seamPerturbationAmplitude == b.seamPerturbationAmplitude &&
			    a.seamPerturbationFrequencyHz == b.seamPerturbationFrequencyHz &&
			    a.seamPerturbationPhase == b.seamPerturbationPhase &&
			    a.airDensityKilogramsPerCubicMeter == b.airDensityKilogramsPerCubicMeter;
		}

		[[nodiscard]] std::optional<TrajectoryTargetVelocity> SolveDuelPitchPlan(
		    const Baseball::PitchParameters& parameters, const double pitchDistanceMeters, const int zoneIndex,
		    const std::optional<DuelZoneCellBounds>& zoneBounds, const std::optional<Physics::Vector3>& zoneTarget,
		    const double verticalOffsetMeters)
		{
			if (!zoneBounds.has_value() || !zoneTarget.has_value())
			{
				return std::nullopt;
			}
			return ResolveTrajectoryZoneTargetVelocity(parameters, pitchDistanceMeters, zoneIndex,
			    *zoneBounds, *zoneTarget, verticalOffsetMeters);
		}
	} // namespace

	void Application::PlanNextDuelPitch()
	{
		m_nextDuelPitchPlan.reset();
		EnsureDefaultPitchProfiles();
		if (m_pitchProfiles.empty())
		{
			return;
		}
		std::mt19937& randomEngine = GetThreadLocalRandomEngine();
		auto plan = std::make_shared<DuelPitchPlan>();
		plan->randomProfile = m_randomPitchProfileEnabled;
		plan->profileIndex = m_activePitchProfileIndex;
		if (m_randomPitchProfileEnabled)
		{
			std::uniform_int_distribution<int> profileDistribution(
			    0,
			    static_cast<int>(m_pitchProfiles.size()) - 1);
			plan->profileIndex = profileDistribution(randomEngine);
		}
		plan->profileIndex = std::clamp(
		    plan->profileIndex,
		    0,
		    static_cast<int>(m_pitchProfiles.size()) - 1);
		plan->parameters = m_pitchProfiles[static_cast<std::size_t>(plan->profileIndex)].pitchParameters;
		plan->zoneMask = m_duelTargetZoneMask;
		plan->pitchDistanceMeters = m_environmentSettings.pitchDistanceMeters;
		plan->verticalOffsetMeters = m_environmentSettings.moundHeightOffsetMeters;

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
		std::uniform_int_distribution<std::size_t> selectedZoneDistribution(
		    0U,
		    candidateZoneIndices.size() - 1U);
		plan->zoneIndex = candidateZoneIndices[selectedZoneDistribution(randomEngine)];
		plan->zoneBounds = ResolveDuelZoneCellBounds(plan->zoneIndex);
		plan->zoneTarget = ResolveRandomDuelZoneCellPosition(
		    plan->zoneIndex,
		    plan->pitchDistanceMeters,
		    randomEngine);
		std::uniform_real_distribution<double> lateralFallbackDistribution(-1.2, 1.2);
		std::uniform_real_distribution<double> verticalFallbackDistribution(-0.35, 0.35);
		plan->fallbackOffset = Physics::Vector3{
			lateralFallbackDistribution(randomEngine),
			verticalFallbackDistribution(randomEngine),
			0.0
		};
		plan->solution = std::async(std::launch::async, SolveDuelPitchPlan, plan->parameters,
		    plan->pitchDistanceMeters, plan->zoneIndex, plan->zoneBounds, plan->zoneTarget,
		    plan->verticalOffsetMeters).share();
		m_nextDuelPitchPlan = std::move(plan);
	}

	void Application::StartRandomBattingDuelPitch(const std::wstring_view note)
	{
		EnsureDefaultPitchProfiles();
		if (m_pitchProfiles.empty())
		{
			return;
		}

		// Use the pitch planned during the interval unless something it depends
		// on changed since; otherwise plan now and solve synchronously.
		std::shared_ptr<DuelPitchPlan> plan = std::move(m_nextDuelPitchPlan);
		m_nextDuelPitchPlan.reset();
		const bool planIsCurrent = plan &&
		    plan->profileIndex < static_cast<int>(m_pitchProfiles.size()) &&
		    SamePitchParameters(plan->parameters,
		        m_pitchProfiles[static_cast<std::size_t>(plan->profileIndex)].pitchParameters) &&
		    plan->randomProfile == m_randomPitchProfileEnabled &&
		    (plan->randomProfile || plan->profileIndex == std::clamp(m_activePitchProfileIndex, 0,
		        static_cast<int>(m_pitchProfiles.size()) - 1)) &&
		    plan->zoneMask == m_duelTargetZoneMask &&
		    plan->pitchDistanceMeters == m_environmentSettings.pitchDistanceMeters &&
		    plan->verticalOffsetMeters == m_environmentSettings.moundHeightOffsetMeters;
		const auto currentBounds = plan ? ResolveDuelZoneCellBounds(plan->zoneIndex) : std::nullopt;
		const bool boundsUnchanged = planIsCurrent && currentBounds.has_value() == plan->zoneBounds.has_value() &&
		    (!currentBounds.has_value() ||
		        (currentBounds->left == plan->zoneBounds->left && currentBounds->right == plan->zoneBounds->right &&
		         currentBounds->top == plan->zoneBounds->top && currentBounds->bottom == plan->zoneBounds->bottom));
		if (!boundsUnchanged)
		{
			PlanNextDuelPitch();
			plan = std::move(m_nextDuelPitchPlan);
			m_nextDuelPitchPlan.reset();
			if (!plan)
			{
				return;
			}
		}

		m_activePitchProfileIndex = plan->profileIndex;
		m_activePresetType = Data::PitchPresetType::Custom;
		m_activePitchParameters = plan->parameters;
		const int selectedZoneIndex = plan->zoneIndex;
		const std::optional<Physics::Vector3> selectedZoneTargetWorldPosition = plan->zoneTarget;
		const std::optional<TrajectoryTargetVelocity> targetVelocity = plan->solution.get();
		bool zoneSolveSucceeded = false;
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
		else
		{
			m_activePitchParameters.initialVelocity.x = std::clamp(
			    m_activePitchParameters.initialVelocity.x + plan->fallbackOffset.x,
			    MinimumHorizontalSpeed,
			    MaximumHorizontalSpeed);
			m_activePitchParameters.initialVelocity.y = std::clamp(
			    m_activePitchParameters.initialVelocity.y + plan->fallbackOffset.y,
			    MinimumVerticalSpeed,
			    MaximumVerticalSpeed);
		}
		m_duelZonePassMarker.reset();
		m_duelRound = {};
		m_pitchMetricsSpeedMetersPerSecond =
		    Physics::Length(m_activePitchParameters.initialVelocity);
		m_batCollisionSuppressRemainingSeconds = 0.0;
		m_teeFlight = {};

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
		const double groundContactHeight = GroundPlaneHeightMeters + ballState->properties.radiusMeters -
		    m_environmentSettings.moundHeightOffsetMeters;
		// The stadium bounds the playing field: walls, netting and seats stop the ball.
		const Simulation::StadiumPlacement stadium{
			m_environmentSettings.pitchDistanceMeters + Common::HomePlateDepthMeters,
			GroundPlaneHeightMeters - m_environmentSettings.moundHeightOffsetMeters };

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
			    !DidBallCrossGroundFromAbove(previousState, *ballState, groundContactHeight))
			{
				return false;
			}

			const Physics::Vector3 groundCrossPosition =
			    ResolveGroundCrossPoint(previousState, *ballState, groundContactHeight);
			// Carry ends at the first physical ground-plane crossing, including
			// beyond the rendered fan. Field bounds only govern bounce response.
			metrics->hasFirstGroundContact = true;
			const Physics::Vector3 contactDelta =
			    groundCrossPosition - metrics->contactPosition;
			metrics->firstGroundDistanceMeters = std::sqrt(
			    (contactDelta.x * contactDelta.x) +
			    (contactDelta.z * contactDelta.z));
			const double heightChange = ballState->position.y - previousState.position.y;
			const double crossBlend = std::abs(heightChange) > 1e-9
			    ? std::clamp((groundContactHeight - previousState.position.y) / heightChange, 0.0, 1.0) : 1.0;
			metrics->resultLabel = JudgeBattedBallLanding(*metrics, groundCrossPosition,
			    previousState.flightTimeSeconds +
			        (ballState->flightTimeSeconds - previousState.flightTimeSeconds) * crossBlend).label;
			if (m_isSessionLogAvailable && (m_lastBattedBallEventId > 0))
			{
				(void)m_sessionLogRepository.UpdateBattedBallLanding(
				    m_lastBattedBallEventId,
				    metrics->firstGroundDistanceMeters,
				    metrics->maxHeightMeters,
				    metrics->resultLabel);
			}
			return true;
		};
		auto applyStadiumCollision = [&](const Physics::BallState& previousState,
		                                 const bool hasPreviousState) -> Simulation::StadiumCollisionResult
		{
			if (!hasPreviousState)
			{
				return {};
			}
			const Simulation::StadiumCollisionResult result =
			    Simulation::ApplyStadiumCollision(previousState, ballState, stadium);
			if ((result.contact == Simulation::StadiumContact::Wall) ||
			    (result.contact == Simulation::StadiumContact::Stands))
			{
				m_pitchSimulationService.SetBallState(*ballState);
			}
			return result;
		};
		// Out over the top of the stands, or somehow below the ground: the play is over.
		auto hasBallLeftStadium = [&](const Physics::BallState& state,
		                              const Simulation::StadiumCollisionResult& result) -> bool
		{
			return (result.contact == Simulation::StadiumContact::LeftStadium) ||
			       (state.position.y < groundContactHeight - 1.0);
		};
		// The first time a batted ball reaches the wall or the seats before coming down in
		// the field, its carry ends there; one that clears the outfield wall is measured to
		// where it would have come down, as Statcast projects home-run distance.
		auto registerStadiumContactForMetrics =
		    [&](BattedBallMetrics* const metrics,
		        const Physics::BallState& previousState,
		        const Physics::BallState& unobstructedState,
		        const Simulation::StadiumCollisionResult& result) -> bool
		{
			if ((metrics == nullptr) || !metrics->hasContact || metrics->hasFirstGroundContact ||
			    (!result.reachedWall && (result.contact != Simulation::StadiumContact::Stands)))
			{
				return false;
			}
			const double fraction = result.reachedWall ? result.wallFrameFraction : 1.0;
			const Physics::Vector3 reached = previousState.position +
			    ((unobstructedState.position - previousState.position) * fraction);
			const double reachedTime = previousState.flightTimeSeconds +
			    ((unobstructedState.flightTimeSeconds - previousState.flightTimeSeconds) * fraction);
			const auto carry = [&](const Physics::Vector3& point)
			{
				return std::hypot(point.x - metrics->contactPosition.x, point.z - metrics->contactPosition.z);
			};
			metrics->hasFirstGroundContact = true;
			metrics->firstGroundDistanceMeters = carry(reached);
			if (result.clearedWall)
			{
				if (const auto landing = ProjectBattedBallLanding(unobstructedState, groundContactHeight))
				{
					metrics->firstGroundDistanceMeters = carry(*landing);
				}
			}
			// The judge compares the bottom of the ball with the top of the wall, as the
			// collision does.
			const std::optional<double> wallHeight = result.reachedWall
			    ? std::optional<double>(result.wallHeightMeters - unobstructedState.properties.radiusMeters)
			    : std::nullopt;
			metrics->resultLabel = JudgeBattedBallLanding(*metrics, reached, reachedTime, wallHeight).label;
			if (m_isSessionLogAvailable && (m_lastBattedBallEventId > 0))
			{
				(void)m_sessionLogRepository.UpdateBattedBallLanding(
				    m_lastBattedBallEventId,
				    metrics->firstGroundDistanceMeters,
				    metrics->maxHeightMeters,
				    metrics->resultLabel);
			}
			return true;
		};
		auto applyGroundCollisionIfNeeded =
		    [&](const Physics::BallState& previousState,
		        const bool hasPreviousState) -> void
		{
			Physics::Vector3 collisionCheckPosition = ballState->position;
			if (hasPreviousState &&
			    DidBallCrossGroundFromAbove(previousState, *ballState, groundContactHeight))
			{
				collisionCheckPosition =
				    ResolveGroundCrossPoint(previousState, *ballState, groundContactHeight);
			}
			const Simulation::FieldPosition field = Simulation::ToField(collisionCheckPosition, stadium);
			if (!Common::Stadium::IsInsidePlayingField(field.u, field.v))
			{
				return;
			}
			ApplyGroundCollisionResponse(ballState, m_lastSimulationDeltaSeconds, groundContactHeight);
			m_pitchSimulationService.SetBallState(*ballState);
		};

		if (m_operationMode == OperationMode::TeeBatting)
		{
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_duelRound = {};
			if (!m_teeFlight.isBallInFlight)
			{
				*ballState = BuildTeeBallState();
				m_pitchSimulationService.SetBallState(*ballState);
				if (TryApplyBatCollision(ballState))
				{
					m_teeFlight.isBallInFlight = true;
					m_teeFlight.hasBattedBallBounced = false;
					m_teeFlight.postBounceElapsedSeconds = 0.0;
					updateBattedMetricsLiveState(&m_teeBattedMetrics);
					Physics::BallState contactState = *ballState;
					contactState.position = m_teeBattedMetrics.contactPosition;
					m_teeFlight.hasBattedBallBounced = registerGroundContactForMetrics(
					    &m_teeBattedMetrics, contactState, true);
					applyGroundCollisionIfNeeded(
					    contactState, true);
					m_teeFlight.hasPreviousBallState = true;
					m_teeFlight.previousBallState = *ballState;
				}
				else
				{
					m_teeFlight.previousBallState = *ballState;
					m_teeFlight.hasPreviousBallState = true;
				}
				applyPendingTrajectoryReset(ballState);
				return;
			}
			const bool collidedWithBat = TryApplyBatCollision(ballState);
			if (collidedWithBat)
			{
				m_teeFlight.isBallInFlight = true;
				m_teeFlight.hasBattedBallBounced = false;
				m_teeFlight.postBounceElapsedSeconds = 0.0;
			}
			Physics::BallState groundPreviousState = m_teeFlight.previousBallState;
			if (collidedWithBat)
			{
				groundPreviousState.position = m_teeBattedMetrics.contactPosition;
			}
			updateBattedMetricsLiveState(&m_teeBattedMetrics);
			const Physics::BallState unobstructedState = *ballState;
			const Simulation::StadiumCollisionResult stadiumResult = applyStadiumCollision(
			    groundPreviousState, m_teeFlight.hasPreviousBallState || collidedWithBat);
			if (!m_teeFlight.hasBattedBallBounced &&
			    (registerStadiumContactForMetrics(
			         &m_teeBattedMetrics, groundPreviousState, unobstructedState, stadiumResult) ||
			     registerGroundContactForMetrics(
			         &m_teeBattedMetrics,
			         groundPreviousState,
			         m_teeFlight.hasPreviousBallState || collidedWithBat)))
			{
				m_teeFlight.hasBattedBallBounced = true;
				m_teeFlight.postBounceElapsedSeconds = 0.0;
			}
			if (m_teeBattedMetrics.hasContact &&
			    hasBallLeftStadium(*ballState, stadiumResult))
			{
				m_teeFlight = {};
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
			    groundPreviousState,
			    m_teeFlight.hasPreviousBallState || collidedWithBat);
			if (m_teeFlight.hasBattedBallBounced)
			{
				m_teeFlight.postBounceElapsedSeconds += m_lastSimulationDeltaSeconds;
			}
			m_teeFlight.previousBallState = *ballState;
			m_teeFlight.hasPreviousBallState = true;
			const bool shouldRespawnTeeBall =
			    m_teeFlight.hasBattedBallBounced &&
			    (m_teeFlight.postBounceElapsedSeconds >=
			     PostBounceSimulationSeconds);
			if (shouldRespawnTeeBall)
			{
				m_teeFlight = {};
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

		if (m_operationMode == OperationMode::Catcher)
		{
			m_teeFlight = {};
			if (m_catcherRound.phase == CatcherPhase::WaitingForSign)
			{
				// The pitcher walks back and comes set; the ball waits in his glove.
				m_duelRound.cooldownRemainingSeconds =
				    (std::max)(0.0, m_duelRound.cooldownRemainingSeconds - m_lastFrameDeltaSeconds);
				applyPendingTrajectoryReset(ballState);
				return;
			}
			if (!m_pitchDeliveryController.HasReleased())
			{
				m_duelRound.previousBallState = *ballState;
				m_duelRound.hasPreviousBallState = true;
				applyPendingTrajectoryReset(ballState);
				return;
			}
			const Physics::BallState previousState = m_duelRound.previousBallState;
			const bool hasPreviousState = m_duelRound.hasPreviousBallState;
			switch (m_catcherRound.phase)
			{
			case CatcherPhase::Pitching:
			{
				const bool wasResolved = m_pitchFlightAnalysis.IsResolved();
				CommitPitchAnalysis(ballState->position, ballState->flightTimeSeconds);
				if (!wasResolved && m_pitchFlightAnalysis.IsResolved())
				{
					ApplyAutoZoneResultTag(m_pitchFlightAnalysis.Actual().call, L"auto_zone_catcher");
				}
				if (hasPreviousState)
				{
					CatchPitchIfReached(previousState, ballState);
				}
				break;
			}
			case CatcherPhase::Caught:
				CatchPitchIfReached(previousState, ballState);
				break;
			case CatcherPhase::Missed:
			{
				m_catcherRound.missedSeconds += m_lastFrameDeltaSeconds;
				const Simulation::StadiumCollisionResult stadiumResult =
				    applyStadiumCollision(previousState, hasPreviousState);
				applyGroundCollisionIfNeeded(previousState, hasPreviousState);
				if (hasBallLeftStadium(*ballState, stadiumResult) ||
				    (m_catcherRound.missedSeconds >= CatcherMissedPlaySeconds))
				{
					ReturnToCatcherSign();
				}
				break;
			}
			case CatcherPhase::WaitingForSign:
				break;
			}
			m_duelRound.previousBallState = *ballState;
			m_duelRound.hasPreviousBallState = true;
			applyPendingTrajectoryReset(ballState);
			return;
		}

		if (m_operationMode != OperationMode::BattingDuel)
		{
			if (m_operationMode == OperationMode::PitchTrajectory)
			{
				CommitPitchAnalysis(ballState->position, ballState->flightTimeSeconds);
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
			m_duelRound = {};
			m_teeFlight = {};
			applyPendingTrajectoryReset(ballState);
			return;
		}
		m_teeFlight.isBallInFlight = false;

		if (m_duelRound.isWaitingForNextPitch)
		{
			m_duelRound.cooldownRemainingSeconds = (std::max)(0.0,
			                                                 m_duelRound.cooldownRemainingSeconds - m_lastSimulationDeltaSeconds);
			if (m_duelRound.cooldownRemainingSeconds <= 1e-6)
			{
				StartRandomBattingDuelPitch(L"duel_cycle");
				*ballState = m_pitchSimulationService.GetBallState();
				m_duelRound.previousBallState = *ballState;
				m_duelRound.hasPreviousBallState = true;
			}
			else
			{
				*ballState = m_duelRound.previousBallState;
				applyPendingTrajectoryReset(ballState);
				return;
			}
		}

		if (!m_pitchDeliveryController.HasReleased())
		{
			m_duelRound.previousBallState = *ballState;
			m_duelRound.hasPreviousBallState = true;
			applyPendingTrajectoryReset(ballState);
			return;
		}

		const double duelZoneDepthMeters =
		    m_environmentSettings.pitchDistanceMeters;
		const double duelPitchEndDepthMeters = duelZoneDepthMeters * 1.10;
		const Physics::BallState currentState = *ballState;
		m_duelRound.pitchElapsedSeconds += m_lastSimulationDeltaSeconds;
		const bool collidedWithBat = TryApplyBatCollision(ballState);
		if (!m_duelRound.hasBallBeenBatted)
		{
			const bool wasResolved = m_pitchFlightAnalysis.IsResolved();
			CommitPitchAnalysis(collidedWithBat ? m_duelRound.battedMetrics.contactPosition : currentState.position,
			    collidedWithBat ? m_duelRound.battedMetrics.contactFlightTimeSeconds : currentState.flightTimeSeconds);
			if (collidedWithBat) m_pitchFlightAnalysis.FinishContact(m_duelRound.battedMetrics.contactPosition);
			if (!wasResolved && m_pitchFlightAnalysis.IsResolved())
			{
				m_duelRound.hasPitchPassedZone = true;
				m_duelPitchPredictedZonePassPoint.reset();
				m_duelZonePassMarker = m_pitchFlightAnalysis.Actual().frontCrossing;
				if (m_duelZonePassMarker) m_duelZonePassMarker->y -= m_environmentSettings.moundHeightOffsetMeters;
				if (!collidedWithBat) ApplyAutoZoneResultTag(m_pitchFlightAnalysis.Actual().call, L"auto_zone_duel");
			}
		}
		Physics::BallState groundPreviousState = m_duelRound.previousBallState;
		if (collidedWithBat)
		{
			groundPreviousState.position = m_duelRound.battedMetrics.contactPosition;
			m_duelZonePassMarker.reset();
			m_duelPitchPredictedZonePassPoint.reset();
			m_duelRound.hasPitchPassedZone = true;
			m_duelRound.hasBallBeenBatted = true;
			m_duelRound.hasBattedBallBounced = false;
			m_duelRound.postBounceElapsedSeconds = 0.0;
		}
		const Physics::BallState unobstructedState = *ballState;
		const Simulation::StadiumCollisionResult stadiumResult = m_duelRound.hasBallBeenBatted
		    ? applyStadiumCollision(groundPreviousState, m_duelRound.hasPreviousBallState || collidedWithBat)
		    : Simulation::StadiumCollisionResult{};
		if (m_duelRound.hasBallBeenBatted &&
		    !m_duelRound.hasBattedBallBounced &&
		    (registerStadiumContactForMetrics(
		         &m_duelRound.battedMetrics, groundPreviousState, unobstructedState, stadiumResult) ||
		     registerGroundContactForMetrics(
		         &m_duelRound.battedMetrics,
		         groundPreviousState,
		         m_duelRound.hasPreviousBallState || collidedWithBat)))
		{
			m_duelRound.hasBattedBallBounced = true;
			m_duelRound.postBounceElapsedSeconds = 0.0;
		}
		if (m_duelRound.hasBallBeenBatted &&
		    m_duelRound.battedMetrics.hasContact &&
		    hasBallLeftStadium(*ballState, stadiumResult))
		{
			m_duelRound.isWaitingForNextPitch = true;
			m_duelRound.cooldownRemainingSeconds = m_duelPitchIntervalSeconds;
			m_duelRound.pitchElapsedSeconds = 0.0;
			PlanNextDuelPitch();
			if (m_duelRound.hasPreviousBallState)
			{
				*ballState = m_duelRound.previousBallState;
			}
			applyPendingTrajectoryReset(ballState);
			return;
		}
		if (m_duelRound.hasBallBeenBatted)
		{
			updateBattedMetricsLiveState(&m_duelRound.battedMetrics);
			applyGroundCollisionIfNeeded(
			    groundPreviousState,
			    m_duelRound.hasPreviousBallState || collidedWithBat);
		}
		if (m_duelRound.hasBallBeenBatted &&
		    m_duelRound.hasBattedBallBounced)
		{
			m_duelRound.postBounceElapsedSeconds += m_lastSimulationDeltaSeconds;
		}
		const Physics::BallState stateForTracking = *ballState;
		m_duelRound.previousBallState = stateForTracking;
		m_duelRound.hasPreviousBallState = true;
		const bool shouldStartIntervalForUnbattedPitch =
		    !m_duelRound.hasBallBeenBatted &&
		    (stateForTracking.position.z >= duelPitchEndDepthMeters);
		const bool shouldStartIntervalAfterBattedBounce =
		    m_duelRound.hasBallBeenBatted &&
		    m_duelRound.hasBattedBallBounced &&
		    (m_duelRound.postBounceElapsedSeconds >=
		     PostBounceSimulationSeconds);
		if (shouldStartIntervalForUnbattedPitch ||
		    shouldStartIntervalAfterBattedBounce)
		{
			m_duelRound.isWaitingForNextPitch = true;
			m_duelRound.cooldownRemainingSeconds = m_duelPitchIntervalSeconds;
			m_duelRound.pitchElapsedSeconds = 0.0;
			PlanNextDuelPitch();
		}
		applyPendingTrajectoryReset(ballState);
	}

	Physics::BallState Application::BuildTeeBallState() const
	{
		Physics::BallState teeBallState{};
		teeBallState.properties = m_environmentSettings.ballProperties;
		teeBallState.position = Physics::Vector3{
			std::clamp(m_teeBallPosition.x, TeeBallMinX, TeeBallMaxX),
			std::clamp(m_teeBallPosition.y, TeeBallMinY, TeeBallMaxY),
			std::clamp(m_teeBallPosition.z, TeeBallMinZ, TeeBallMaxZ)
		};
		teeBallState.velocity = Physics::Vector3{ 0.0, 0.0, 0.0 };
		teeBallState.flightTimeSeconds = 0.0;
		return teeBallState;
	}

	bool Application::LogPitchEvent(
	    const std::wstring_view eventType,
	    const std::wstring_view note,
	    const Data::SessionEventCategory category)
	{
		if (!m_isSessionLogAvailable || !m_sessionLogRepository.IsInitialized())
		{
			return false;
		}

		const std::wstring pitchName = BuildActivePitchName();
		const std::wstring_view integratorName = Physics::GetPitchIntegratorName(m_activePitchParameters.integratorType);
		Data::PitchEventLogResult insertedEventResult{};
		if (category == Data::SessionEventCategory::Pitch)
		{
			Data::PitchDetails details{};
			details.mode = m_operationMode == OperationMode::BattingDuel ? L"batting-duel"
			    : m_operationMode == OperationMode::TeeBatting           ? L"tee-batting"
			    : m_operationMode == OperationMode::Catcher              ? L"catcher"
			                                                             : L"trajectory";
			details.profileName = pitchName;
			details.pitchSettingsFile = m_activePitchSettingsFileName;
			details.pitcherThrowsLeftHanded = m_pitcherThrowsLeftHanded;
			insertedEventResult = m_sessionLogRepository.LogPitch(
			    eventType, pitchName, integratorName, m_activePitchParameters, note, details);
		}
		else
		{
			insertedEventResult = m_sessionLogRepository.LogEvent(
			    category, eventType, pitchName, integratorName, m_activePitchParameters, note);
		}
		if (!insertedEventResult)
		{
			Common::AppLog::Error(L"SessionLog", L"insert failed: " + insertedEventResult.errorMessage);
			return false;
		}

		// Only pitch deliveries receive results and outcomes; configuration and UI events must not
		// redirect a later automatic strike/ball tag.
		if (category == Data::SessionEventCategory::Pitch)
		{
			m_lastLoggedPitchEventId = *insertedEventResult.eventId;
			m_pitchOutcomePendingEventId =
			    m_operationMode == OperationMode::TeeBatting ? 0 : *insertedEventResult.eventId;
			m_latestPitchResultLabel = L"pending";
			m_latestPitchResultScore = 0.0;
		}
		return true;
	}

	void Application::RecordPitchOutcome()
	{
		// The first resolved flight after a pitch event is its outcome; resets before the plate
		// (replays, environment reloads) keep the pitch pending.
		if ((m_pitchOutcomePendingEventId <= 0) || !m_isSessionLogAvailable ||
		    !m_sessionLogRepository.IsInitialized() || !m_pitchFlightAnalysis.IsResolved())
		{
			return;
		}
		const std::int64_t eventId = std::exchange(m_pitchOutcomePendingEventId, 0);
		const Simulation::PitchFlightReport& actual = m_pitchFlightAnalysis.Actual();
		Data::PitchOutcome outcome{};
		if (actual.frontCrossing.has_value())
		{
			outcome.plateCrossingX = actual.frontCrossing->x;
			outcome.plateCrossingY = actual.frontCrossing->y;
		}
		outcome.zoneCall = std::wstring{ ZoneCallToken(actual.call) };
		if (actual.breakMeters.has_value())
		{
			outcome.horizontalBreakMeters = actual.breakMeters->x;
			outcome.verticalBreakMeters = actual.breakMeters->y;
		}
		const Data::OperationResult result = m_sessionLogRepository.RecordPitchOutcome(eventId, outcome);
		if (!result)
		{
			Common::AppLog::Warning(L"SessionLog", L"pitch outcome not recorded: " + result.errorMessage);
		}
	}

	bool Application::LogBattedBall(
	    const std::wstring_view note,
	    const Physics::Vector3& launchVelocity,
	    const double contactFractionAlongBat)
	{
		if (!m_isSessionLogAvailable || !m_sessionLogRepository.IsInitialized())
		{
			return false;
		}
		Data::BattedBallDetails details{};
		const bool tee = m_operationMode == OperationMode::TeeBatting;
		details.mode = tee ? L"tee-batting" : L"batting-duel";
		if (!tee && (m_lastLoggedPitchEventId > 0))
		{
			details.pitchEventId = m_lastLoggedPitchEventId;
		}
		const double horizontalSpeed = std::hypot(launchVelocity.x, launchVelocity.z);
		details.exitSpeedMetersPerSecond = Physics::Length(launchVelocity);
		details.launchAngleDegrees = std::atan2(launchVelocity.y, horizontalSpeed) * 180.0 / std::numbers::pi;
		// Batted balls travel toward -z (back over the pitcher); +x is the batter's right.
		details.sprayAngleDegrees = horizontalSpeed > 1e-9
		    ? std::atan2(launchVelocity.x, -launchVelocity.z) * 180.0 / std::numbers::pi
		    : 0.0;
		details.contactFractionAlongBat = std::clamp(contactFractionAlongBat, 0.0, 1.0);
		details.batMaterial = BatMaterialLabel(m_batMaterial);
		details.batRightHanded = m_batUseRightHand;
		const Data::PitchEventLogResult result = m_sessionLogRepository.LogBattedBall(BuildActivePitchName(),
		    Physics::GetPitchIntegratorName(m_activePitchParameters.integratorType), m_activePitchParameters, note,
		    details);
		if (!result)
		{
			Common::AppLog::Error(L"SessionLog", L"batted-ball insert failed: " + result.errorMessage);
			return false;
		}
		m_lastBattedBallEventId = *result.eventId;
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
				case OperationMode::Catcher:
					return L"Catcher";
				default:
					return L"Unknown";
			}
		}();
		Physics::BallState displayBallState = m_pitchSimulationService.GetBallState();
		if ((m_operationMode == OperationMode::TeeBatting) && !m_teeFlight.isBallInFlight)
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
		const std::wstring activePitchName = BuildActivePitchName();
		titleInput.presetName = activePitchName;
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
		         << BatMaterialLabel(m_batMaterial) << L":"
		         << (m_batUseRightHand ? L"R" : L"L")
		         << L"(" << m_environmentSettings.batRadiusMeters << L","
		         << m_environmentSettings.batLengthMeters << L","
		         << m_environmentSettings.batAngleOffsetDegrees << L"deg,"
		         << m_environmentSettings.batYawOffsetDegrees << L"deg)";
		envTitle << L",BallCOR=" << m_environmentSettings.ballProperties.restitutionAtReferenceSpeed;
		envTitle << L",GroundFan=" << m_environmentSettings.groundFanRadiusMeters
		         << L"m";
		envTitle << L",PitchRnd=" << (m_randomPitchProfileEnabled ? L"On" : L"Off");
		envTitle << L",Playback=" << (m_playbackPaused ? L"Paused" : L"Run");
		if (m_duelRound.isWaitingForNextPitch)
		{
			envTitle << L",Next=" << m_duelRound.cooldownRemainingSeconds << L"s";
		}
		SetWindowTextW(m_windowHandle, envTitle.str().c_str());
	}

	void Application::Shutdown()
	{
		m_isRunning = false;

		FlushPendingSettingsSaves(true);
		// Flushes queued session-log writes before closing the database.
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
			case WM_DPICHANGED:
			{
				const auto& suggested = *reinterpret_cast<const RECT*>(lParam);
				SetWindowPos(windowHandle, nullptr, suggested.left, suggested.top,
				    suggested.right - suggested.left, suggested.bottom - suggested.top,
				    SWP_NOZORDER | SWP_NOACTIVATE);
				return 0;
			}
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				// lParam bit 30 is set when the key was already down (keyboard auto-repeat).
				HandleKeyDown(wParam, (static_cast<std::uint64_t>(lParam) & (1ULL << 30U)) != 0U);
				return 0;
			case WM_COMMAND:
			{
				const int commandId = LOWORD(wParam);
				switch (commandId)
				{
					case IDM_ABOUT:
						ShowNonBlockingMessage(
						    L"About BaseballSimulatorVR",
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
						    L"  1           : Pitch trail (None / Full / ZoneOnly)\n"
						    L"  4           : Toggle batted-ball trajectory\n"
						    L"  2           : Strike zone (None / Plane / Box)\n"
						    L"  3           : Toggle floor\n"
						    L"  Q           : Toggle duel pitch preview marker\n"
						    L"  E           : Toggle bat hand (right / left)\n"
						    L"  9           : Bat material (Wood / Metal / Urethane)\n"
						    L"  Ctrl+E      : Export current session to CSV\n"
						    L"  Ctrl+H      : Recalibrate strike zone (stand still for 3s)\n"
						    L"  Ctrl+Left/Right : Previous / next settings file\n"
						    L"  Ctrl+Up/Down    : Previous / next pitch profile\n"
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
						    L"  Metrics UI : trajectory / zone / mound / preview / bat hand / bat material",
						    MB_ICONINFORMATION);
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
