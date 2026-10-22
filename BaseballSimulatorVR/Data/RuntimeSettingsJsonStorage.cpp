#include "RuntimeSettingsJsonStorage.hpp"
#include "JsonSettingsReader.hpp"
#include "../Common/AtomicFileWriter.hpp"
#include "../Common/JsonWriter.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/ZoneMaskUtilities.hpp"
#include "../Common/StrikeZoneSettings.hpp"
#include "../Common/TextConversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		namespace JsonWriter = Common::JsonWriter;

		constexpr int DuelTargetZoneCount =
		    Common::RuntimeLimits::DuelZoneCount;
		constexpr double MinimumDuelPitchIntervalSeconds =
		    Common::RuntimeLimits::MinimumDuelPitchIntervalSeconds;
		constexpr double MaximumDuelPitchIntervalSeconds =
		    Common::RuntimeLimits::MaximumDuelPitchIntervalSeconds;
		constexpr double MinimumPlaybackSpeedPercent =
		    Common::RuntimeLimits::MinimumPlaybackSpeedPercent;
		constexpr double MaximumPlaybackSpeedPercent =
		    Common::RuntimeLimits::MaximumPlaybackSpeedPercent;

		[[nodiscard]] double ClampDuelPitchIntervalSeconds(
		    const double value) noexcept
		{
			return std::clamp(
			    value,
			    MinimumDuelPitchIntervalSeconds,
			    MaximumDuelPitchIntervalSeconds);
		}

		[[nodiscard]] double ClampPlaybackSpeedPercent(
		    const double value) noexcept
		{
			return std::clamp(
			    value,
			    MinimumPlaybackSpeedPercent,
			    MaximumPlaybackSpeedPercent);
		}

		[[nodiscard]] std::optional<int> NormalizeLegacyZoneIndex(
		    const std::optional<int> zoneIndex) noexcept
		{
			if (!zoneIndex.has_value())
			{
				return std::nullopt;
			}
			if (!Common::IsValidZoneIndex(zoneIndex.value(), DuelTargetZoneCount))
			{
				return std::nullopt;
			}
			return zoneIndex;
		}

		[[nodiscard]] bool IsInRange(const double value, const double minimum, const double maximum) noexcept
		{
			return std::isfinite(value) && value >= minimum && value <= maximum;
		}

		[[nodiscard]] bool WriteDocument(
		    const std::wstring_view filePath,
		    const JsonWriter::ObjectBuilder& document,
		    const std::wstring_view description,
		    std::wstring& errorMessage)
		{
			if (document.HasFailed())
			{
				errorMessage = std::wstring{ description } + L" contains a value that is not a finite number.";
				return false;
			}
			const Common::FileOperationResult result = Common::WriteFileAtomically(
			    std::filesystem::path{ filePath }, document.BuildDocument(), JsonSettings::SettingsWriteOptions());
			if (!result)
			{
				errorMessage = L"Failed to save " + std::wstring{ description } + L": " + result.errorMessage;
				return false;
			}
			return true;
		}
	} // namespace

	bool IsValidEnvironmentSettings(const EnvironmentRuntimeSettings& settings, std::wstring& errorMessage)
	{
		errorMessage.clear();
		if (!settings.ballProperties.IsValid())
		{
			errorMessage = L"Environment ball radius, mass or restitution is out of range.";
			return false;
		}
		if (!IsInRange(settings.altitudeMeters, EnvironmentLimits::MinimumAltitudeMeters,
		        EnvironmentLimits::MaximumAltitudeMeters))
		{
			errorMessage = L"Environment altitudeMeters must be between -500 and 5000.";
			return false;
		}
		if (!IsInRange(settings.temperatureCelsius, EnvironmentLimits::MinimumTemperatureCelsius,
		        EnvironmentLimits::MaximumTemperatureCelsius))
		{
			errorMessage = L"Environment temperatureCelsius must be between -40 and 60.";
			return false;
		}
		if (!IsInRange(settings.relativeHumidityPercent, EnvironmentLimits::MinimumRelativeHumidityPercent,
		        EnvironmentLimits::MaximumRelativeHumidityPercent))
		{
			errorMessage = L"Environment relativeHumidityPercent must be between 0 and 100.";
			return false;
		}
		if (!IsInRange(settings.batMassKilograms, EnvironmentLimits::MinimumBatMassKilograms,
		        EnvironmentLimits::MaximumBatMassKilograms))
		{
			errorMessage = L"Environment batMassKilograms must be between 0.3 and 1.5.";
			return false;
		}
		return true;
	}

	bool RuntimeSettingsJsonStorage::SaveEnvironment(
	    std::wstring_view filePath,
	    const EnvironmentRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		if (!IsValidEnvironmentSettings(settings, errorMessage))
		{
			return false;
		}

		JsonWriter::ObjectBuilder json{};
		bool ok = true;
		const auto number = [&](const std::string_view key, const std::string_view comment, const double value)
		{
			json.AddComment(key, comment);
			ok = json.AddNumber(key, value) && ok;
		};
		const auto boolean = [&](const std::string_view key, const std::string_view comment, const bool value)
		{
			json.AddComment(key, comment);
			json.AddBool(key, value);
		};
		number("ballRadiusMeters", "Ball radius [m].", settings.ballProperties.radiusMeters);
		number("ballMassKilograms", "Ball mass [kg].", settings.ballProperties.massKilograms);
		number("ballRestitutionAtReferenceSpeed", "Ball COR at 71.5264 m/s normal closing speed.",
		    settings.ballProperties.restitutionAtReferenceSpeed);
		number("pitchDistanceMeters", "Pitch distance from release area to home plate [m]",
		    settings.pitchDistanceMeters);
		boolean("enableMoundHeightOffset", "Apply mound height offset to player viewpoint",
		    settings.enableMoundHeightOffset);
		number("moundHeightOffsetMeters", "Mound height offset [m] (always applied to pitch release/zone space)",
		    settings.moundHeightOffsetMeters);
		number("windVelocityX", "Wind velocity X [m/s], positive to batter-right.", settings.windVelocity.x);
		number("windVelocityY", "Wind velocity Y [m/s], positive upward.", settings.windVelocity.y);
		number("windVelocityZ", "Wind velocity Z [m/s], positive toward home plate.", settings.windVelocity.z);
		boolean("enableHeightDependentWind", "Enable height-dependent wind shear.",
		    settings.enableHeightDependentWind);
		number("windShearFactor", "Cross-wind shear factor [m/s per meter above reference height].",
		    settings.windShearFactor);
		number("batRadiusMeters", "Maximum barrel radius [m]", settings.batRadiusMeters);
		number("batLengthMeters", "Total bat length from knob face to tip [m]", settings.batLengthMeters);
		number("batMassKilograms", "Bat mass [kg] (optional, default 0.885).", settings.batMassKilograms);
		number("batAngleOffsetDegrees", "Bat angle offset around local right axis [deg]",
		    settings.batAngleOffsetDegrees);
		number("batYawOffsetDegrees", "Bat yaw offset around controller local up axis [deg]",
		    settings.batYawOffsetDegrees);
		number("groundFanRadiusMeters", "Ground fan outer radius [m]", settings.groundFanRadiusMeters);
		number("altitudeMeters", "Field altitude above sea level [m] (optional, default 0).",
		    settings.altitudeMeters);
		number("temperatureCelsius", "Air temperature [deg C] (optional, default 15).", settings.temperatureCelsius);
		number("relativeHumidityPercent", "Relative humidity [%] (optional, default 0).",
		    settings.relativeHumidityPercent);
		json.AddComment("integratorType", "Pitch simulation integrator (Rk4 or SemiImplicitEuler)");
		json.AddString("integratorType", Physics::GetPitchIntegratorToken(settings.integratorType));
		if (!ok)
		{
			errorMessage = L"Environment settings contain a value that is not a finite number.";
			return false;
		}
		return WriteDocument(filePath, json, L"environment settings JSON", errorMessage);
	}

	std::optional<EnvironmentRuntimeSettings> RuntimeSettingsJsonStorage::LoadEnvironment(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		const std::optional<JsonSettings::Json::JsonDocument> document =
		    JsonSettings::ReadDocument(filePath, L"Environment settings JSON", errorMessage);
		if (!document.has_value())
		{
			return std::nullopt;
		}

		EnvironmentRuntimeSettings settings{};
		JsonSettings::MemberReader reader{ document->Root(), L"Environment settings JSON is invalid", errorMessage };
		const auto& root = reader.Object();
		std::string integratorToken{};
		const bool readSucceeded =
		    reader.Double("ballRestitutionAtReferenceSpeed", settings.ballProperties.restitutionAtReferenceSpeed, true) &&
		    reader.Double("ballRadiusMeters", settings.ballProperties.radiusMeters, false) &&
		    reader.Double("ballMassKilograms", settings.ballProperties.massKilograms, false) &&
		    reader.Range("ballRestitutionAtReferenceSpeed", settings.ballProperties.restitutionAtReferenceSpeed, 0.0, 1.0) &&
		    reader.Range("ballRadiusMeters", settings.ballProperties.radiusMeters, 0.01, 0.10) &&
		    reader.Range("ballMassKilograms", settings.ballProperties.massKilograms, 0.01, 1.0) &&
		    reader.Double("pitchDistanceMeters", settings.pitchDistanceMeters, true) &&
		    reader.Bool("enableMoundHeightOffset", settings.enableMoundHeightOffset, true) &&
		    reader.Double("moundHeightOffsetMeters", settings.moundHeightOffsetMeters, true) &&
		    reader.Double("windVelocityX", settings.windVelocity.x, false) &&
		    reader.Double("windVelocityY", settings.windVelocity.y, false) &&
		    reader.Double("windVelocityZ", settings.windVelocity.z, false) &&
		    reader.Bool("enableHeightDependentWind", settings.enableHeightDependentWind, false) &&
		    reader.Double("windShearFactor", settings.windShearFactor, false) &&
		    reader.Double("batRadiusMeters", settings.batRadiusMeters, false) &&
		    reader.Double("batLengthMeters", settings.batLengthMeters, false) &&
		    reader.Double("batAngleOffsetDegrees", settings.batAngleOffsetDegrees, false) &&
		    reader.Double("batYawOffsetDegrees", settings.batYawOffsetDegrees, false) &&
		    reader.Double("groundFanRadiusMeters", settings.groundFanRadiusMeters, false) &&
		    reader.Double("altitudeMeters", settings.altitudeMeters, false) &&
		    reader.Range("altitudeMeters", settings.altitudeMeters, EnvironmentLimits::MinimumAltitudeMeters,
		        EnvironmentLimits::MaximumAltitudeMeters) &&
		    reader.Double("temperatureCelsius", settings.temperatureCelsius, false) &&
		    reader.Range("temperatureCelsius", settings.temperatureCelsius, EnvironmentLimits::MinimumTemperatureCelsius,
		        EnvironmentLimits::MaximumTemperatureCelsius) &&
		    reader.Double("relativeHumidityPercent", settings.relativeHumidityPercent, false) &&
		    reader.Range("relativeHumidityPercent", settings.relativeHumidityPercent,
		        EnvironmentLimits::MinimumRelativeHumidityPercent, EnvironmentLimits::MaximumRelativeHumidityPercent) &&
		    reader.Double("batMassKilograms", settings.batMassKilograms, false) &&
		    reader.Range("batMassKilograms", settings.batMassKilograms, EnvironmentLimits::MinimumBatMassKilograms,
		        EnvironmentLimits::MaximumBatMassKilograms) &&
		    reader.String("integratorType", integratorToken, false);
		if (!readSucceeded)
		{
			return std::nullopt;
		}
		if (root.Contains("integratorType"))
		{
			if (integratorToken == "SemiImplicitEuler")
			{
				settings.integratorType = Physics::PitchIntegratorType::SemiImplicitEuler;
			}
			else if (integratorToken == "Rk4")
			{
				settings.integratorType = Physics::PitchIntegratorType::Rk4;
			}
			else
			{
				(void)reader.Fail("integratorType", "expected \"Rk4\" or \"SemiImplicitEuler\"");
				return std::nullopt;
			}
		}
		return settings;
	}

	bool RuntimeSettingsJsonStorage::SavePlay(
	    std::wstring_view filePath,
	    const PlayRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		if (!Physics::IsValidBatMaterial(settings.batMaterial))
		{
			errorMessage = L"Play settings batMaterial is invalid.";
			return false;
		}
		if (Common::GetPitchTrajectoryDisplayModeToken(settings.pitchTrajectoryDisplayMode).empty() ||
		    Common::GetStrikeZoneDisplayModeToken(settings.strikeZoneDisplayMode).empty())
		{
			errorMessage = L"Play settings overlay display mode is invalid.";
			return false;
		}
		if (!Common::IsValidStrikeZoneSettings({settings.strikeZoneBottomMeters,
		        settings.strikeZoneHeightMeters, settings.strikeZoneWidthMeters}))
		{
			errorMessage = L"Play settings strike zone is out of range.";
			return false;
		}
		// Reject rather than clamp: an infinite value would otherwise be saved as a limit.
		if (!std::isfinite(settings.duelPitchIntervalSeconds) ||
		    !std::isfinite(settings.playbackSpeedPercent))
		{
			errorMessage = L"Play settings pitch interval and playback speed must be finite.";
			return false;
		}
		const std::optional<std::string> selectedFile =
		    Common::TextConversion::TryWideToUtf8(settings.selectedPitchSettingsFile);
		if (!selectedFile.has_value())
		{
			errorMessage = L"Play settings selectedPitchSettingsFile is not valid Unicode.";
			return false;
		}

		JsonWriter::ObjectBuilder json{};
		bool ok = true;
		json.AddComment("strikeZoneBottomMeters", "Strike zone bottom height from ground [m]");
		json.AddComment("strikeZoneConfigured", "Player zone configured by calibration or manual adjustment.");
		json.AddBool("strikeZoneConfigured", settings.strikeZoneConfigured);
		ok = json.AddNumber("strikeZoneBottomMeters", settings.strikeZoneBottomMeters) && ok;
		json.AddComment("strikeZoneHeightMeters", "Strike zone height [m]");
		ok = json.AddNumber("strikeZoneHeightMeters", settings.strikeZoneHeightMeters) && ok;
		json.AddComment("strikeZoneWidthMeters", "Strike zone width [m]");
		ok = json.AddNumber("strikeZoneWidthMeters", settings.strikeZoneWidthMeters) && ok;
		json.AddComment("duelPitchIntervalSeconds", "Wait interval between pitches in duel mode [s]");
		ok = json.AddNumber("duelPitchIntervalSeconds", ClampDuelPitchIntervalSeconds(settings.duelPitchIntervalSeconds)) && ok;
		json.AddComment("playbackSpeedPercent", "Trajectory playback speed [%], 100 = realtime");
		ok = json.AddNumber("playbackSpeedPercent", ClampPlaybackSpeedPercent(settings.playbackSpeedPercent)) && ok;
		json.AddComment("duelPitchPreviewEnabled", "Show predicted zone-pass marker in batting duel mode");
		json.AddBool("duelPitchPreviewEnabled", settings.duelPitchPreviewEnabled);
		json.AddComment("batUseRightHand", "Use right hand for bat control (false = left hand)");
		json.AddBool("batUseRightHand", settings.batUseRightHand);
		json.AddComment("batMaterial", "Bat material: Wood, Metal, Urethane or Beginner.");
		json.AddString("batMaterial", Physics::BatMaterialName(settings.batMaterial));
		json.AddComment("pitchTrajectoryDisplayMode", "Pitch trail: None, Full or ZoneOnly (inside strike-zone volume).");
		json.AddString("pitchTrajectoryDisplayMode",
		    Common::GetPitchTrajectoryDisplayModeToken(settings.pitchTrajectoryDisplayMode));
		json.AddComment("strikeZoneDisplayMode", "Strike zone: None, Plane or Box.");
		json.AddString("strikeZoneDisplayMode", Common::GetStrikeZoneDisplayModeToken(settings.strikeZoneDisplayMode));
		json.AddComment("battedTrajectoryVisible", "Show batted-ball trajectory in every mode, independently of pitches");
		json.AddBool("battedTrajectoryVisible", settings.battedTrajectoryVisible);
		json.AddComment("selectedPitchSettingsFile", "Last selected pitch settings entry (presets themselves are never rewritten).");
		json.AddString("selectedPitchSettingsFile", *selectedFile);
		json.AddComment("activePitchProfileIndex", "Selected pitch profile of that entry (null = the file's default).");
		if (settings.activePitchProfileIndex.has_value())
			json.AddInteger("activePitchProfileIndex", *settings.activePitchProfileIndex);
		else
			json.AddNull("activePitchProfileIndex");
		json.AddComment("randomPitchProfileEnabled", "Random pitch selection (null = the file's default).");
		if (settings.randomPitchProfileEnabled.has_value())
			json.AddBool("randomPitchProfileEnabled", *settings.randomPitchProfileEnabled);
		else
			json.AddNull("randomPitchProfileEnabled");
		json.AddComment("settingsLayoutVersion", "Settings directory layout version (managed by the application).");
		json.AddInteger("settingsLayoutVersion", settings.settingsLayoutVersion);
		json.AddComment("duelTargetZoneMask", "Target zone bit mask for duel mode (25-zone grid)");
		const std::uint32_t normalizedZoneMask = Common::NormalizeZoneMask(
		    settings.duelTargetZoneMask,
		    DuelTargetZoneCount);
		json.AddInteger("duelTargetZoneMask", normalizedZoneMask);
		json.AddComment("duelTargetZoneIndex", "Single-zone selection index (null if none)");
		std::optional<int> fallbackZoneIndex = NormalizeLegacyZoneIndex(
		    settings.duelTargetZoneIndex);
		if (!fallbackZoneIndex.has_value() &&
		    (normalizedZoneMask != 0U))
		{
			fallbackZoneIndex = Common::FindFirstSelectedZoneIndex(
			    normalizedZoneMask,
			    DuelTargetZoneCount);
		}
		if (fallbackZoneIndex.has_value())
			json.AddInteger("duelTargetZoneIndex", *fallbackZoneIndex);
		else
			json.AddNull("duelTargetZoneIndex");
		if (!ok)
		{
			errorMessage = L"Play settings contain a value that is not a finite number.";
			return false;
		}
		return WriteDocument(filePath, json, L"play settings JSON", errorMessage);
	}

	std::optional<PlayRuntimeSettings> RuntimeSettingsJsonStorage::LoadPlay(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		const std::optional<JsonSettings::Json::JsonDocument> document =
		    JsonSettings::ReadDocument(filePath, L"Play settings JSON", errorMessage);
		if (!document.has_value())
		{
			return std::nullopt;
		}

		PlayRuntimeSettings settings{};
		JsonSettings::MemberReader reader{ document->Root(), L"Play settings JSON is invalid", errorMessage };
		if (!reader.Bool("strikeZoneConfigured", settings.strikeZoneConfigured, true) ||
		    !reader.Double("strikeZoneBottomMeters", settings.strikeZoneBottomMeters, true) ||
		    !reader.Double("strikeZoneHeightMeters", settings.strikeZoneHeightMeters, true) ||
		    !reader.Double("strikeZoneWidthMeters", settings.strikeZoneWidthMeters, true))
		{
			return std::nullopt;
		}
		if (!Common::IsValidStrikeZoneSettings({settings.strikeZoneBottomMeters,
		        settings.strikeZoneHeightMeters, settings.strikeZoneWidthMeters}))
		{
			(void)reader.Fail("strikeZoneHeightMeters",
			    "the strike zone (bottom, height, width) is outside the supported range");
			return std::nullopt;
		}

		std::string materialToken{};
		if (!reader.String("batMaterial", materialToken, true))
		{
			return std::nullopt;
		}
		const auto material = Physics::ParseBatMaterial(materialToken);
		if (!material)
		{
			(void)reader.Fail("batMaterial", "expected \"Wood\", \"Metal\", \"Urethane\" or \"Beginner\"");
			return std::nullopt;
		}
		settings.batMaterial = *material;

		std::string pitchDisplay{};
		if (!reader.String("pitchTrajectoryDisplayMode", pitchDisplay, true))
		{
			return std::nullopt;
		}
		if (pitchDisplay != "None" && pitchDisplay != "Full" && pitchDisplay != "ZoneOnly")
		{
			(void)reader.Fail("pitchTrajectoryDisplayMode", "expected \"None\", \"Full\" or \"ZoneOnly\"");
			return std::nullopt;
		}
		settings.pitchTrajectoryDisplayMode = pitchDisplay == "None"
		    ? Common::PitchTrajectoryDisplayMode::None : pitchDisplay == "Full"
		    ? Common::PitchTrajectoryDisplayMode::Full : Common::PitchTrajectoryDisplayMode::ZoneOnly;

		std::string zoneDisplay{};
		if (!reader.String("strikeZoneDisplayMode", zoneDisplay, true))
		{
			return std::nullopt;
		}
		if (zoneDisplay != "None" && zoneDisplay != "Plane" && zoneDisplay != "Box")
		{
			(void)reader.Fail("strikeZoneDisplayMode", "expected \"None\", \"Plane\" or \"Box\"");
			return std::nullopt;
		}
		settings.strikeZoneDisplayMode = zoneDisplay == "None"
		    ? Common::StrikeZoneDisplayMode::None : zoneDisplay == "Plane"
		    ? Common::StrikeZoneDisplayMode::Plane : Common::StrikeZoneDisplayMode::Box;

		const auto& root = reader.Object();
		std::uint32_t zoneMask = 0U;
		std::optional<int> activeProfile{};
		bool randomProfile = false;
		if (!reader.Double("duelPitchIntervalSeconds", settings.duelPitchIntervalSeconds, true) ||
		    !reader.Double("playbackSpeedPercent", settings.playbackSpeedPercent, true) ||
		    !reader.Bool("duelPitchPreviewEnabled", settings.duelPitchPreviewEnabled, false) ||
		    !reader.Bool("batUseRightHand", settings.batUseRightHand, false) ||
		    !reader.Bool("battedTrajectoryVisible", settings.battedTrajectoryVisible, false) ||
		    !reader.OptionalInt("duelTargetZoneIndex", settings.duelTargetZoneIndex, false) ||
		    !reader.UnsignedInt("duelTargetZoneMask", zoneMask, false) ||
		    !reader.WideString("selectedPitchSettingsFile", settings.selectedPitchSettingsFile, false) ||
		    !reader.OptionalInt("activePitchProfileIndex", activeProfile, false) ||
		    !reader.Int("settingsLayoutVersion", settings.settingsLayoutVersion, false))
		{
			return std::nullopt;
		}
		settings.activePitchProfileIndex = activeProfile;
		if (const auto randomResult = root.ReadValue("randomPitchProfileEnabled");
		    randomResult.status == JsonSettings::Json::KeyReadStatus::Valid &&
		    randomResult.value->type != JsonSettings::Json::ValueType::Null)
		{
			if (!reader.Bool("randomPitchProfileEnabled", randomProfile, true))
			{
				return std::nullopt;
			}
			settings.randomPitchProfileEnabled = randomProfile;
		}

		if (root.Contains("duelTargetZoneMask"))
		{
			settings.duelTargetZoneMask = zoneMask;
		}
		else
		{
			settings.duelTargetZoneMask = 0U;
			if (settings.duelTargetZoneIndex.has_value())
			{
				const int legacyZoneIndex = settings.duelTargetZoneIndex.value();
				if ((legacyZoneIndex < 0) || (legacyZoneIndex >= DuelTargetZoneCount))
				{
					(void)reader.Fail("duelTargetZoneIndex", "the zone index must be between 0 and 24");
					return std::nullopt;
				}
				settings.duelTargetZoneMask = Common::BuildZoneBit(legacyZoneIndex);
			}
		}

		settings.duelTargetZoneMask = Common::NormalizeZoneMask(
		    settings.duelTargetZoneMask,
		    DuelTargetZoneCount);
		settings.duelPitchIntervalSeconds = ClampDuelPitchIntervalSeconds(
		    settings.duelPitchIntervalSeconds);
		settings.playbackSpeedPercent = ClampPlaybackSpeedPercent(
		    settings.playbackSpeedPercent);
		settings.duelTargetZoneIndex = NormalizeLegacyZoneIndex(
		    settings.duelTargetZoneIndex);
		if (settings.duelTargetZoneMask != 0U)
		{
			settings.duelTargetZoneIndex = Common::FindFirstSelectedZoneIndex(
			    settings.duelTargetZoneMask,
			    DuelTargetZoneCount);
		}

		return settings;
	}
} // namespace BaseballSimulatorVR::Data
