#include "RuntimeSettingsJsonStorage.hpp"
#include "../Common/JsonKeyValueParser.hpp"
#include "../Common/RuntimeLimits.hpp"
#include "../Common/ZoneMaskUtilities.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		constexpr int DuelTargetZoneCount =
		    Common::RuntimeLimits::DuelZoneCount;

		[[nodiscard]] bool TryReadIntegrator(
		    const std::string& jsonText,
		    const std::string_view key,
		    Physics::PitchIntegratorType& integratorType)
		{
			const std::optional<std::string> rawValue =
			    Common::JsonKeyValueParser::ExtractRawValue(jsonText, key);
			if (!rawValue.has_value())
			{
				return false;
			}

			if (*rawValue == "SemiImplicitEuler")
			{
				integratorType = Physics::PitchIntegratorType::SemiImplicitEuler;
				return true;
			}
			if (*rawValue == "Rk4")
			{
				integratorType = Physics::PitchIntegratorType::Rk4;
				return true;
			}

			return false;
		}

		template <typename TReadFunction, typename TApplyFunction>
		[[nodiscard]] bool TryReadOptionalField(
		    const std::string& jsonText,
		    const std::string_view key,
		    const std::wstring_view invalidMessage,
		    TReadFunction&& readFunction,
		    TApplyFunction&& applyFunction,
		    std::wstring& errorMessage)
		{
			const auto readResult = readFunction(jsonText, key);
			if (readResult.status ==
			    Common::JsonKeyValueParser::KeyReadStatus::Missing)
			{
				return true;
			}
			if (!readResult.value.has_value())
			{
				errorMessage = std::wstring{ invalidMessage };
				return false;
			}

			applyFunction(*readResult.value);
			return true;
		}

	} // namespace

	bool RuntimeSettingsJsonStorage::SaveEnvironment(
	    std::wstring_view filePath,
	    const EnvironmentRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ofstream outputFile{ std::filesystem::path(filePath) };
		if (!outputFile.is_open())
		{
			errorMessage = L"Failed to open environment settings JSON file.";
			return false;
		}

		outputFile.setf(std::ios::fixed);
		outputFile.precision(std::numeric_limits<double>::max_digits10);
		outputFile << "{\n";
		outputFile << "  \"_comment_pitchDistanceMeters\": \"Pitch distance from release area to home plate [m]\",\n";
		outputFile << "  \"pitchDistanceMeters\": " << settings.pitchDistanceMeters << ",\n";
		outputFile << "  \"_comment_enableMoundHeightOffset\": \"Apply mound height offset to player viewpoint\",\n";
		outputFile << "  \"enableMoundHeightOffset\": "
		           << (settings.enableMoundHeightOffset ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_moundHeightOffsetMeters\": \"Mound height offset [m] (always applied to pitch release/zone space)\",\n";
		outputFile << "  \"moundHeightOffsetMeters\": " << settings.moundHeightOffsetMeters
		           << ",\n";
		outputFile << "  \"_comment_strikeZoneBottomMeters\": \"Strike zone bottom height from ground [m]\",\n";
		outputFile << "  \"strikeZoneBottomMeters\": " << settings.strikeZoneBottomMeters
		           << ",\n";
		outputFile << "  \"_comment_strikeZoneHeightMeters\": \"Strike zone height [m]\",\n";
		outputFile << "  \"strikeZoneHeightMeters\": " << settings.strikeZoneHeightMeters
		           << ",\n";
		outputFile << "  \"_comment_strikeZoneWidthMeters\": \"Strike zone width [m]\",\n";
		outputFile << "  \"strikeZoneWidthMeters\": " << settings.strikeZoneWidthMeters
		           << ",\n";
		outputFile << "  \"_comment_windVelocityX\": \"Wind velocity X [m/s], positive to batter-right.\",\n";
		outputFile << "  \"windVelocityX\": " << settings.windVelocity.x << ",\n";
		outputFile << "  \"_comment_windVelocityY\": \"Wind velocity Y [m/s], positive upward.\",\n";
		outputFile << "  \"windVelocityY\": " << settings.windVelocity.y << ",\n";
		outputFile << "  \"_comment_windVelocityZ\": \"Wind velocity Z [m/s], positive toward home plate.\",\n";
		outputFile << "  \"windVelocityZ\": " << settings.windVelocity.z << ",\n";
		outputFile << "  \"_comment_enableHeightDependentWind\": \"Enable height-dependent wind shear.\",\n";
		outputFile << "  \"enableHeightDependentWind\": "
		           << (settings.enableHeightDependentWind ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_windShearFactor\": \"Cross-wind shear factor [m/s per meter above reference height].\",\n";
		outputFile << "  \"windShearFactor\": " << settings.windShearFactor << ",\n";
		outputFile << "  \"_comment_batRadiusMeters\": \"Bat radius [m]\",\n";
		outputFile << "  \"batRadiusMeters\": " << settings.batRadiusMeters << ",\n";
		outputFile << "  \"_comment_batLengthMeters\": \"Bat length [m]\",\n";
		outputFile << "  \"batLengthMeters\": " << settings.batLengthMeters << ",\n";
		outputFile << "  \"_comment_batBallRestitution\": \"Ball-bat restitution coefficient\",\n";
		outputFile << "  \"batBallRestitution\": " << settings.batBallRestitution << ",\n";
		outputFile << "  \"_comment_batAngleOffsetDegrees\": \"Bat angle offset around local right axis [deg]\",\n";
		outputFile << "  \"batAngleOffsetDegrees\": "
		           << settings.batAngleOffsetDegrees << ",\n";
		outputFile << "  \"_comment_batYawOffsetDegrees\": \"Bat yaw offset around controller local up axis [deg]\",\n";
		outputFile << "  \"batYawOffsetDegrees\": "
		           << settings.batYawOffsetDegrees << ",\n";
		outputFile << "  \"_comment_groundFanRadiusMeters\": \"Ground fan outer radius [m]\",\n";
		outputFile << "  \"groundFanRadiusMeters\": "
		           << settings.groundFanRadiusMeters << ",\n";
		outputFile << "  \"_comment_integratorType\": \"Pitch simulation integrator (Rk4 or SemiImplicitEuler)\",\n";
		outputFile << "  \"integratorType\": \""
		           << Physics::GetPitchIntegratorToken(settings.integratorType) << "\"\n";
		outputFile << "}\n";
		if (!outputFile.good())
		{
			errorMessage = L"Failed while writing environment settings JSON.";
			return false;
		}
		return true;
	}

	std::optional<EnvironmentRuntimeSettings> RuntimeSettingsJsonStorage::LoadEnvironment(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		std::ifstream inputFile{ std::filesystem::path(filePath) };
		if (!inputFile.is_open())
		{
			errorMessage = L"Environment settings JSON file could not be opened.";
			return std::nullopt;
		}

		std::stringstream buffer{};
		buffer << inputFile.rdbuf();
		if (!inputFile.good() && !inputFile.eof())
		{
			errorMessage = L"Failed while reading environment settings JSON.";
			return std::nullopt;
		}

		const std::string jsonText = buffer.str();
		EnvironmentRuntimeSettings settings{};
		if (!Common::JsonKeyValueParser::TryReadDouble(
		        jsonText,
		        "pitchDistanceMeters",
		        settings.pitchDistanceMeters) ||
		    !Common::JsonKeyValueParser::TryReadBool(
		        jsonText,
		        "enableMoundHeightOffset",
		        settings.enableMoundHeightOffset) ||
		    !Common::JsonKeyValueParser::TryReadDouble(
		        jsonText,
		        "moundHeightOffsetMeters",
		        settings.moundHeightOffsetMeters))
		{
			errorMessage = L"Environment settings JSON format is invalid.";
			return std::nullopt;
		}
		if (!TryReadOptionalField(
		        jsonText,
		        "strikeZoneBottomMeters",
		        L"Environment settings strikeZoneBottomMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.strikeZoneBottomMeters = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "strikeZoneHeightMeters",
		        L"Environment settings strikeZoneHeightMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.strikeZoneHeightMeters = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "strikeZoneWidthMeters",
		        L"Environment settings strikeZoneWidthMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.strikeZoneWidthMeters = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "windVelocityX",
		        L"Environment settings windVelocityX is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.windVelocity.x = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "windVelocityY",
		        L"Environment settings windVelocityY is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.windVelocity.y = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "windVelocityZ",
		        L"Environment settings windVelocityZ is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.windVelocity.z = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "enableHeightDependentWind",
		        L"Environment settings enableHeightDependentWind is invalid.",
		        Common::JsonKeyValueParser::ReadBoolResult,
		        [&](const bool value)
		        {
			        settings.enableHeightDependentWind = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "windShearFactor",
		        L"Environment settings windShearFactor is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.windShearFactor = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batRadiusMeters",
		        L"Environment settings batRadiusMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.batRadiusMeters = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batLengthMeters",
		        L"Environment settings batLengthMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.batLengthMeters = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batBallRestitution",
		        L"Environment settings batBallRestitution is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.batBallRestitution = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batAngleOffsetDegrees",
		        L"Environment settings batAngleOffsetDegrees is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.batAngleOffsetDegrees = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batYawOffsetDegrees",
		        L"Environment settings batYawOffsetDegrees is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.batYawOffsetDegrees = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "groundFanRadiusMeters",
		        L"Environment settings groundFanRadiusMeters is invalid.",
		        Common::JsonKeyValueParser::ReadDoubleResult,
		        [&](const double value)
		        {
			        settings.groundFanRadiusMeters = value;
		        },
		        errorMessage))
		{
			return std::nullopt;
		}
		if (Common::JsonKeyValueParser::ExtractRawValue(
		        jsonText,
		        "integratorType")
		        .has_value() &&
		    !TryReadIntegrator(
		        jsonText,
		        "integratorType",
		        settings.integratorType))
		{
			errorMessage = L"Environment settings integratorType is invalid.";
			return std::nullopt;
		}

		return settings;
	}

	bool RuntimeSettingsJsonStorage::SavePlay(
	    std::wstring_view filePath,
	    const PlayRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ofstream outputFile{ std::filesystem::path(filePath) };
		if (!outputFile.is_open())
		{
			errorMessage = L"Failed to open play settings JSON file.";
			return false;
		}

		outputFile.setf(std::ios::fixed);
		outputFile.precision(std::numeric_limits<double>::max_digits10);
		outputFile << "{\n";
		outputFile << "  \"_comment_duelPitchIntervalSeconds\": \"Wait interval between pitches in duel mode [s]\",\n";
		outputFile << "  \"duelPitchIntervalSeconds\": "
		           << settings.duelPitchIntervalSeconds << ",\n";
		outputFile << "  \"_comment_playbackSpeedPercent\": \"Trajectory playback speed [%], 100 = realtime\",\n";
		outputFile << "  \"playbackSpeedPercent\": "
		           << settings.playbackSpeedPercent << ",\n";
		outputFile << "  \"_comment_duelPitchPreviewEnabled\": \"Show predicted zone-pass marker in batting duel mode\",\n";
		outputFile << "  \"duelPitchPreviewEnabled\": "
		           << (settings.duelPitchPreviewEnabled ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_batUseRightHand\": \"Use right hand for bat control (false = left hand)\",\n";
		outputFile << "  \"batUseRightHand\": "
		           << (settings.batUseRightHand ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_pitchTrajectoryVisible\": \"Show trajectory in PitchTrajectory mode\",\n";
		outputFile << "  \"pitchTrajectoryVisible\": "
		           << (settings.pitchTrajectoryVisible ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_battedTrajectoryVisible\": \"Show trajectory in BattingDuel/Tee mode\",\n";
		outputFile << "  \"battedTrajectoryVisible\": "
		           << (settings.battedTrajectoryVisible ? "true" : "false")
		           << ",\n";
		outputFile << "  \"_comment_duelTargetZoneMask\": \"Target zone bit mask for duel mode (25-zone grid)\",\n";
		const std::uint32_t normalizedZoneMask = Common::NormalizeZoneMask(
		    settings.duelTargetZoneMask,
		    DuelTargetZoneCount);
		outputFile << "  \"duelTargetZoneMask\": "
		           << normalizedZoneMask << ",\n";
		outputFile << "  \"_comment_duelTargetZoneIndex\": \"Legacy single-zone index for compatibility (null if none)\",\n";
		outputFile << "  \"duelTargetZoneIndex\": ";
		std::optional<int> fallbackZoneIndex = settings.duelTargetZoneIndex;
		if (!fallbackZoneIndex.has_value() &&
		    (normalizedZoneMask != 0U))
		{
			fallbackZoneIndex = Common::FindFirstSelectedZoneIndex(
			    normalizedZoneMask,
			    DuelTargetZoneCount);
		}
		if (fallbackZoneIndex.has_value())
		{
			outputFile << fallbackZoneIndex.value();
		}
		else
		{
			outputFile << "null";
		}
		outputFile << "\n}\n";

		if (!outputFile.good())
		{
			errorMessage = L"Failed while writing play settings JSON.";
			return false;
		}
		return true;
	}

	std::optional<PlayRuntimeSettings> RuntimeSettingsJsonStorage::LoadPlay(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();
		std::ifstream inputFile{ std::filesystem::path(filePath) };
		if (!inputFile.is_open())
		{
			errorMessage = L"Play settings JSON file could not be opened.";
			return std::nullopt;
		}

		std::stringstream buffer{};
		buffer << inputFile.rdbuf();
		if (!inputFile.good() && !inputFile.eof())
		{
			errorMessage = L"Failed while reading play settings JSON.";
			return std::nullopt;
		}

		const std::string jsonText = buffer.str();
		PlayRuntimeSettings settings{};
		if (!Common::JsonKeyValueParser::TryReadDouble(
		        jsonText,
		        "duelPitchIntervalSeconds",
		        settings.duelPitchIntervalSeconds) ||
		    !Common::JsonKeyValueParser::TryReadDouble(
		        jsonText,
		        "playbackSpeedPercent",
		        settings.playbackSpeedPercent))
		{
			errorMessage = L"Play settings JSON format is invalid.";
			return std::nullopt;
		}
		if (!TryReadOptionalField(
		        jsonText,
		        "duelPitchPreviewEnabled",
		        L"Play settings duelPitchPreviewEnabled is invalid.",
		        Common::JsonKeyValueParser::ReadBoolResult,
		        [&](const bool value)
		        {
			        settings.duelPitchPreviewEnabled = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "batUseRightHand",
		        L"Play settings batUseRightHand is invalid.",
		        Common::JsonKeyValueParser::ReadBoolResult,
		        [&](const bool value)
		        {
			        settings.batUseRightHand = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "pitchTrajectoryVisible",
		        L"Play settings pitchTrajectoryVisible is invalid.",
		        Common::JsonKeyValueParser::ReadBoolResult,
		        [&](const bool value)
		        {
			        settings.pitchTrajectoryVisible = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "battedTrajectoryVisible",
		        L"Play settings battedTrajectoryVisible is invalid.",
		        Common::JsonKeyValueParser::ReadBoolResult,
		        [&](const bool value)
		        {
			        settings.battedTrajectoryVisible = value;
		        },
		        errorMessage) ||
		    !TryReadOptionalField(
		        jsonText,
		        "duelTargetZoneIndex",
		        L"Play settings duelTargetZoneIndex is invalid.",
		        Common::JsonKeyValueParser::ReadOptionalIntResult,
		        [&](const std::optional<int>& value)
		        {
			        settings.duelTargetZoneIndex = value;
		        },
		        errorMessage))
		{
			return std::nullopt;
		}

		const Common::JsonKeyValueParser::KeyReadResult<std::uint32_t>
		    duelTargetZoneMaskResult =
		        Common::JsonKeyValueParser::ReadUnsignedIntResult(
		            jsonText,
		            "duelTargetZoneMask");
		if (duelTargetZoneMaskResult.status ==
		    Common::JsonKeyValueParser::KeyReadStatus::Invalid)
		{
			errorMessage = L"Play settings duelTargetZoneMask is invalid.";
			return std::nullopt;
		}
		if (duelTargetZoneMaskResult.status ==
		    Common::JsonKeyValueParser::KeyReadStatus::Valid)
		{
			settings.duelTargetZoneMask =
			    duelTargetZoneMaskResult.value.value_or(0U);
		}
		else
		{
			settings.duelTargetZoneMask = 0U;
			if (settings.duelTargetZoneIndex.has_value())
			{
				const int legacyZoneIndex = settings.duelTargetZoneIndex.value();
				if ((legacyZoneIndex < 0) || (legacyZoneIndex >= DuelTargetZoneCount))
				{
					errorMessage =
					    L"Play settings duelTargetZoneIndex is out of range.";
					return std::nullopt;
				}
				settings.duelTargetZoneMask = Common::BuildZoneBit(legacyZoneIndex);
			}
		}

		settings.duelTargetZoneMask = Common::NormalizeZoneMask(
		    settings.duelTargetZoneMask,
		    DuelTargetZoneCount);

		return settings;
	}
} // namespace BaseballSimulatorVR::Data
