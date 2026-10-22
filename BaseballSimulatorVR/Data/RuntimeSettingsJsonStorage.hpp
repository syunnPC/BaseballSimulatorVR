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
	// Validated ranges of the optional atmosphere and bat-mass environment fields.
	namespace EnvironmentLimits
	{
		inline constexpr double MinimumAltitudeMeters = -500.0;
		inline constexpr double MaximumAltitudeMeters = 5000.0;
		inline constexpr double MinimumTemperatureCelsius = -40.0;
		inline constexpr double MaximumTemperatureCelsius = 60.0;
		inline constexpr double MinimumRelativeHumidityPercent = 0.0;
		inline constexpr double MaximumRelativeHumidityPercent = 100.0;
		inline constexpr double MinimumBatMassKilograms = 0.30;
		inline constexpr double MaximumBatMassKilograms = 1.50;
	} // namespace EnvironmentLimits

	// Single definition of the environment settings used by the application and the JSON files.
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
		// Optional fields (these defaults apply when absent): site altitude above sea level, air
		// temperature and relative humidity, which set the air density, and the whole-bat mass.
		double altitudeMeters{ 0.0 };
		double temperatureCelsius{ 15.0 };
		double relativeHumidityPercent{ 0.0 };
		double batMassKilograms{ 0.885 };
	};

	// Layout version of the user's settings directory that this build writes (see PresetCatalog).
	inline constexpr int CurrentSettingsLayoutVersion = 2;

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
		// Pitch selection state. Presets are read-only at runtime, so the selected pitch settings
		// entry, its active profile and the random flag are remembered here instead.
		std::wstring selectedPitchSettingsFile{};
		std::optional<int> activePitchProfileIndex{};
		std::optional<bool> randomPitchProfileEnabled{};
		// 0 when absent. While it is below CurrentSettingsLayoutVersion, startup moves user copies of
		// bundled presets aside (PresetCatalog::FindLegacyProvisionedCopies) and then records the
		// current version.
		int settingsLayoutVersion{ 0 };
	};

	[[nodiscard]] bool IsValidEnvironmentSettings(const EnvironmentRuntimeSettings& settings, std::wstring& errorMessage);

	class RuntimeSettingsJsonStorage final
	{
	  public:
		// Saves replace the file atomically and keep the previous valid file as "<name>.bak".
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
