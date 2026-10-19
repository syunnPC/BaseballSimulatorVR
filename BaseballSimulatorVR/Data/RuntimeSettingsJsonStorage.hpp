#pragma once

#include "../Common/OverlayVisibilitySettings.hpp"

#include "../Physics/PitchIntegratorType.hpp"
#include "../Physics/Vector3.hpp"
#include "../Physics/BallProperties.hpp"
#include "../Physics/BatMaterial.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Data
{
	struct EnvironmentRuntimeSettings final
	{
		double pitchDistanceMeters{ 18.44 };
		bool enableMoundHeightOffset{ false };
		double moundHeightOffsetMeters{ 0.18 };
		Physics::Vector3 windVelocity{};
		Physics::BallProperties ballProperties{};
		bool enableHeightDependentWind{ false };
		double windShearFactor{ 0.08 };
		double batRadiusMeters{ 0.035 };
		double batLengthMeters{ 0.84 };
		double batAngleOffsetDegrees{ 0.0 };
		double batYawOffsetDegrees{ 0.0 };
		double groundFanRadiusMeters{ 100.0 };
		Physics::PitchIntegratorType integratorType{
			Physics::PitchIntegratorType::Rk4
		};
	};

	struct PlayRuntimeSettings final
	{
		double strikeZoneBottomMeters{ 0.55 };
		double strikeZoneHeightMeters{ 1.0 };
		double strikeZoneWidthMeters{ 0.44 };
		bool strikeZoneConfigured{ false };
		double duelPitchIntervalSeconds{ 5.0 };
		double playbackSpeedPercent{ 100.0 };
		std::uint32_t duelTargetZoneMask{ 0U };
		std::optional<int> duelTargetZoneIndex{};
		bool duelPitchPreviewEnabled{ true };
		bool batUseRightHand{ true };
		Physics::BatMaterial batMaterial{ Physics::BatMaterial::Wood };
		Common::PitchTrajectoryDisplayMode pitchTrajectoryDisplayMode{ Common::PitchTrajectoryDisplayMode::Full };
		Common::StrikeZoneDisplayMode strikeZoneDisplayMode{ Common::StrikeZoneDisplayMode::Box };
		bool battedTrajectoryVisible{ true };
	};

	class RuntimeSettingsJsonStorage final
	{
	  public:
		[[nodiscard]] bool SaveEnvironment(
		    std::wstring_view filePath,
		    const EnvironmentRuntimeSettings& settings,
		    std::wstring& errorMessage) const;
		[[nodiscard]] std::optional<EnvironmentRuntimeSettings> LoadEnvironment(
		    std::wstring_view filePath,
		    std::wstring& errorMessage) const;
		[[nodiscard]] bool SavePlay(
		    std::wstring_view filePath,
		    const PlayRuntimeSettings& settings,
		    std::wstring& errorMessage) const;
		[[nodiscard]] std::optional<PlayRuntimeSettings> LoadPlay(
		    std::wstring_view filePath,
		    std::wstring& errorMessage) const;
	};
} // namespace BaseballSimulatorVR::Data
