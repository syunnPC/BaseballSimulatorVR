#include "PitchPresetJsonStorage.hpp"
#include "../Common/JsonKeyValueParser.hpp"
#include "../Framework.hpp"

#include <filesystem>
#include <fstream>
#include <locale>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		constexpr std::string_view DefaultEnvironmentSettingsFileName =
		    "EnvironmentSettings.user.json";

		[[nodiscard]] std::string EscapeJsonString(const std::string_view value)
		{
			constexpr char HexDigits[] = "0123456789ABCDEF";
			std::string escapedValue{};
			escapedValue.reserve(value.size() + 8U);
			for (const unsigned char currentCharacter : value)
			{
				switch (currentCharacter)
				{
				case '\"':
					escapedValue += "\\\"";
					break;
				case '\\':
					escapedValue += "\\\\";
					break;
				case '\b':
					escapedValue += "\\b";
					break;
				case '\f':
					escapedValue += "\\f";
					break;
				case '\n':
					escapedValue += "\\n";
					break;
				case '\r':
					escapedValue += "\\r";
					break;
				case '\t':
					escapedValue += "\\t";
					break;
				default:
					if (currentCharacter < 0x20U)
					{
						escapedValue += "\\u00";
						escapedValue.push_back(HexDigits[(currentCharacter >> 4U) & 0x0FU]);
						escapedValue.push_back(HexDigits[currentCharacter & 0x0FU]);
					}
					else
					{
						escapedValue.push_back(static_cast<char>(currentCharacter));
					}
					break;
				}
			}

			return escapedValue;
		}

		[[nodiscard]] bool TryReadDouble(
		    const std::string& jsonText, std::string_view key, double& outputValue)
		{
			return Common::JsonKeyValueParser::TryReadDouble(jsonText, key, outputValue);
		}

		[[nodiscard]] bool TryReadBool(
		    const std::string& jsonText, std::string_view key, bool& outputValue)
		{
			return Common::JsonKeyValueParser::TryReadBool(jsonText, key, outputValue);
		}

		[[nodiscard]] std::optional<std::string> TryConvertWideToUtf8(const std::wstring_view text)
		{
			if (text.empty())
			{
				return std::string{};
			}

			const int utf8Size = WideCharToMultiByte(
			    CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr,
			    0, nullptr, nullptr);
			if (utf8Size <= 0)
			{
				return std::nullopt;
			}

			std::string utf8Text(static_cast<std::size_t>(utf8Size), '\0');
			const int converted = WideCharToMultiByte(
			    CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			    utf8Text.data(), utf8Size, nullptr, nullptr);
			if (converted != utf8Size)
			{
				return std::nullopt;
			}

			return utf8Text;
		}

		[[nodiscard]] std::optional<std::wstring> TryConvertUtf8ToWide(
		    const std::string_view utf8Text)
		{
			if (utf8Text.empty())
			{
				return std::wstring{};
			}

			const int wideSize = MultiByteToWideChar(
			    CP_UTF8, MB_ERR_INVALID_CHARS, utf8Text.data(), static_cast<int>(utf8Text.size()),
			    nullptr, 0);
			if (wideSize <= 0)
			{
				return std::nullopt;
			}

			std::wstring wideText(static_cast<std::size_t>(wideSize), L'\0');
			const int converted = MultiByteToWideChar(
			    CP_UTF8, MB_ERR_INVALID_CHARS, utf8Text.data(), static_cast<int>(utf8Text.size()),
			    wideText.data(), wideSize);
			if (converted != wideSize)
			{
				return std::nullopt;
			}

			return wideText;
		}

		[[nodiscard]] std::string BuildPrefixedKey(
		    std::string_view prefix, std::string_view fieldName)
		{
			if (prefix.empty())
			{
				return std::string{ fieldName };
			}
			std::string key{ prefix };
			key += fieldName;
			return key;
		}

		[[nodiscard]] std::string BuildPrefixedCommentKey(
		    std::string_view prefix, std::string_view fieldName)
		{
			std::string key{ "_comment_" };
			key += BuildPrefixedKey(prefix, fieldName);
			return key;
		}

		[[nodiscard]] std::string_view GetPitchParameterComment(const std::string_view fieldName)
		{
			if (fieldName == "initialSpeedMetersPerSecond")
			{
				return "Initial forward speed toward home plate [m/s].";
			}
			if (fieldName == "launchElevationDegrees") return "Initial upward launch angle [deg].";
			if (fieldName == "usePhysicalAerodynamics") return "Use physical drag and lift for pitched flight.";
			if (fieldName == "underhandToss") return "Use an underhand toss delivery.";
			if (fieldName == "releaseHeightMeters")
			{
				return "Pitch release height [m] before mound offset.";
			}
			if (fieldName == "spinAxisX")
			{
				return "Spin axis vector X component.";
			}
			if (fieldName == "spinAxisY")
			{
				return "Spin axis vector Y component.";
			}
			if (fieldName == "spinAxisZ")
			{
				return "Spin axis vector Z component.";
			}
			if (fieldName == "spinRpm")
			{
				return "Spin rate [rpm].";
			}
			if (fieldName == "dragCoefficient")
			{
				return "Drag coefficient.";
			}
			if (fieldName == "liftCoefficient")
			{
				return "Lift coefficient.";
			}
			if (fieldName == "useCoefficientTable")
			{
				return "Enable velocity/spin-dependent coefficient table.";
			}
			if (fieldName == "enableSeamPerturbation")
			{
				return "Enable seam perturbation.";
			}
			if (fieldName == "seamPerturbationAmplitude")
			{
				return "Seam perturbation amplitude.";
			}
			if (fieldName == "seamPerturbationFrequencyHz")
			{
				return "Seam perturbation frequency [Hz].";
			}
			if (fieldName == "seamPerturbationPhase")
			{
				return "Seam perturbation phase [rad].";
			}
			return "";
		}

		[[nodiscard]] bool TryReadPitchParametersWithPrefix(
		    const std::string& jsonText,
		    std::string_view prefix,
		    Baseball::PitchParameters& outputPitchParameters)
		{
			Baseball::PitchParameters parameters{};
			const std::string releaseHeightKey = BuildPrefixedKey(prefix, "releaseHeightMeters");
			if (Common::JsonKeyValueParser::ExtractRawValue(jsonText, releaseHeightKey)
			        .has_value() &&
			    !TryReadDouble(jsonText, releaseHeightKey, parameters.releaseHeightMeters))
			{
				return false;
			}
			double initialSpeedMetersPerSecond = 0.0;
			const std::string initialSpeedKey =
			    BuildPrefixedKey(prefix, "initialSpeedMetersPerSecond");
			if (!TryReadDouble(jsonText, initialSpeedKey, initialSpeedMetersPerSecond))
			{
				return false;
			}
			const auto readOptionalNumber = [&](const std::string_view name, double& value)
			{
				const auto result = Common::JsonKeyValueParser::ReadDoubleResult(jsonText, BuildPrefixedKey(prefix, name));
				if (result.status == Common::JsonKeyValueParser::KeyReadStatus::Missing) return true;
				if (!result.value) return false;
				value = *result.value;
				return true;
			};
			const auto readOptionalBool = [&](const std::string_view name, bool& value)
			{
				const auto result = Common::JsonKeyValueParser::ReadBoolResult(jsonText, BuildPrefixedKey(prefix, name));
				if (result.status == Common::JsonKeyValueParser::KeyReadStatus::Missing) return true;
				if (!result.value) return false;
				value = *result.value;
				return true;
			};
			if (!readOptionalNumber("launchElevationDegrees", parameters.launchElevationDegrees) ||
			    !readOptionalBool("usePhysicalAerodynamics", parameters.usePhysicalAerodynamics) ||
			    !readOptionalBool("underhandToss", parameters.underhandToss) ||
			    parameters.launchElevationDegrees < -75.0 || parameters.launchElevationDegrees > 75.0)
				return false;
			parameters.initialVelocity = Physics::Vector3{ 0.0,
			    std::abs(initialSpeedMetersPerSecond) * std::tan(parameters.launchElevationDegrees * std::numbers::pi / 180.0),
			    std::abs(initialSpeedMetersPerSecond) };
			if (!TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "spinAxisX"), parameters.spinAxis.x) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "spinAxisY"), parameters.spinAxis.y) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "spinAxisZ"), parameters.spinAxis.z) ||
			    !TryReadDouble(jsonText, BuildPrefixedKey(prefix, "spinRpm"), parameters.spinRpm) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "dragCoefficient"),
			        parameters.dragCoefficient) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "liftCoefficient"),
			        parameters.liftCoefficient) ||
			    !TryReadBool(
			        jsonText, BuildPrefixedKey(prefix, "useCoefficientTable"),
			        parameters.useCoefficientTable) ||
			    !TryReadBool(
			        jsonText, BuildPrefixedKey(prefix, "enableSeamPerturbation"),
			        parameters.enableSeamPerturbation) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "seamPerturbationAmplitude"),
			        parameters.seamPerturbationAmplitude) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "seamPerturbationFrequencyHz"),
			        parameters.seamPerturbationFrequencyHz) ||
			    !TryReadDouble(
			        jsonText, BuildPrefixedKey(prefix, "seamPerturbationPhase"),
			        parameters.seamPerturbationPhase))
			{
				return false;
			}

			outputPitchParameters = parameters;
			return true;
		}

		void AppendPitchParameterEntries(
		    std::vector<std::string>* const entries,
		    std::string_view prefix,
		    const Baseball::PitchParameters& pitchParameters)
		{
			if (entries == nullptr)
			{
				return;
			}

			auto addNumber = [&](std::string_view key, const double value) -> void
			{
				std::ostringstream commentLine{};
				commentLine << "\"" << BuildPrefixedCommentKey(prefix, key) << "\": \""
				            << GetPitchParameterComment(key) << "\"";
				entries->push_back(commentLine.str());

				std::ostringstream line{};
				line.imbue(std::locale::classic());
				line.setf(std::ios::fixed);
				line.precision(std::numeric_limits<double>::max_digits10);
				line << "\"" << BuildPrefixedKey(prefix, key) << "\": " << value;
				entries->push_back(line.str());
			};
			auto addBool = [&](std::string_view key, const bool value) -> void
			{
				std::ostringstream commentLine{};
				commentLine << "\"" << BuildPrefixedCommentKey(prefix, key) << "\": \""
				            << GetPitchParameterComment(key) << "\"";
				entries->push_back(commentLine.str());

				std::ostringstream line{};
				line.imbue(std::locale::classic());
				line << "\"" << BuildPrefixedKey(prefix, key)
				     << "\": " << (value ? "true" : "false");
				entries->push_back(line.str());
			};

			addNumber("initialSpeedMetersPerSecond", std::abs(pitchParameters.initialVelocity.z));
			addNumber("launchElevationDegrees", pitchParameters.launchElevationDegrees);
			addBool("usePhysicalAerodynamics", pitchParameters.usePhysicalAerodynamics);
			addBool("underhandToss", pitchParameters.underhandToss);
			addNumber("releaseHeightMeters", pitchParameters.releaseHeightMeters);
			addNumber("spinAxisX", pitchParameters.spinAxis.x);
			addNumber("spinAxisY", pitchParameters.spinAxis.y);
			addNumber("spinAxisZ", pitchParameters.spinAxis.z);
			addNumber("spinRpm", pitchParameters.spinRpm);
			addNumber("dragCoefficient", pitchParameters.dragCoefficient);
			addNumber("liftCoefficient", pitchParameters.liftCoefficient);
			addBool("useCoefficientTable", pitchParameters.useCoefficientTable);
			addBool("enableSeamPerturbation", pitchParameters.enableSeamPerturbation);
			addNumber("seamPerturbationAmplitude", pitchParameters.seamPerturbationAmplitude);
			addNumber("seamPerturbationFrequencyHz", pitchParameters.seamPerturbationFrequencyHz);
			addNumber("seamPerturbationPhase", pitchParameters.seamPerturbationPhase);
		}
	} // namespace

	bool PitchPresetJsonStorage::SavePitchSettings(
	    std::wstring_view filePath,
	    const PitchRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ofstream outputFile{ std::filesystem::path(filePath) };
		if (!outputFile.is_open())
		{
			errorMessage = L"Failed to open preset JSON file for writing.";
			return false;
		}

		std::vector<std::string> entries{};
		entries.reserve(32U + (settings.pitchProfiles.size() * 48U));
		AppendPitchParameterEntries(&entries, "", settings.pitchParameters);
		entries.push_back(
		    "\"_comment_pitcherThrowsLeftHanded\": \"Pitcher throws with the left hand when true; right hand when false.\"");
		entries.push_back(std::string{ "\"pitcherThrowsLeftHanded\": " } +
		                  (settings.pitcherThrowsLeftHanded ? "true" : "false"));

		std::string environmentFilePath{ DefaultEnvironmentSettingsFileName };
		if (!settings.environmentSettingsFile.empty())
		{
			const std::optional<std::string> environmentFilePathUtf8 =
			    TryConvertWideToUtf8(settings.environmentSettingsFile);
			if (!environmentFilePathUtf8.has_value())
			{
				errorMessage = L"Failed to convert environment settings file path to UTF-8.";
				return false;
			}
			environmentFilePath = *environmentFilePathUtf8;
		}
		entries.push_back(
		    "\"_comment_environmentSettingsFile\": \"Referenced environment settings "
		    "JSON file name.\"");
		entries.push_back(
		    "\"environmentSettingsFile\": \"" + EscapeJsonString(environmentFilePath) + "\"");

		entries.push_back(
		    "\"_comment_activePitchProfileIndex\": \"Selected pitch profile index "
		    "when random selection is disabled.\"");
		{
			std::ostringstream indexEntry{};
			indexEntry.imbue(std::locale::classic());
			indexEntry << "\"activePitchProfileIndex\": " << settings.activePitchProfileIndex;
			entries.push_back(indexEntry.str());
		}
		entries.push_back(
		    "\"_comment_randomPitchProfileEnabled\": \"If true, choose pitch profile randomly.\"");
		entries.push_back(
		    std::string{ "\"randomPitchProfileEnabled\": " } +
		    (settings.randomPitchProfileEnabled ? "true" : "false"));

		outputFile.imbue(std::locale::classic());
		outputFile << "{\n";
		for (const std::string& entry : entries)
		{
			outputFile << "  " << entry << ",\n";
		}

		outputFile << "  \"_comment_pitchProfiles\": \"Array of pitch profiles and their pitch "
		              "parameters.\",\n";
		outputFile << "  \"pitchProfiles\": [\n";
		for (std::size_t profileIndex = 0; profileIndex < settings.pitchProfiles.size();
		     ++profileIndex)
		{
			const PitchProfileEntry& profile = settings.pitchProfiles[profileIndex];
			std::string profileName{ "Pitch" };
			if (!profile.name.empty())
			{
				const std::optional<std::string> profileNameUtf8 =
				    TryConvertWideToUtf8(profile.name);
				if (!profileNameUtf8.has_value())
				{
					errorMessage = L"Failed to convert pitch profile name to UTF-8.";
					return false;
				}
				profileName = *profileNameUtf8;
			}

			std::vector<std::string> profileEntries{};
			profileEntries.reserve(48U);
			profileEntries.push_back(
			    "\"_comment_name\": \"Pitch profile display name used in UI/logs.\"");
			profileEntries.push_back("\"name\": \"" + EscapeJsonString(profileName) + "\"");
			AppendPitchParameterEntries(&profileEntries, "", profile.pitchParameters);

			outputFile << "    {\n";
			for (std::size_t entryIndex = 0; entryIndex < profileEntries.size(); ++entryIndex)
			{
				outputFile << "      " << profileEntries[entryIndex];
				if (entryIndex + 1U < profileEntries.size())
				{
					outputFile << ",";
				}
				outputFile << "\n";
			}
			outputFile << "    }";
			if (profileIndex + 1U < settings.pitchProfiles.size())
			{
				outputFile << ",";
			}
			outputFile << "\n";
		}
		outputFile << "  ]\n";
		outputFile << "}\n";

		if (!outputFile.good())
		{
			errorMessage = L"Failed while writing preset JSON file.";
			return false;
		}

		return true;
	}

	std::optional<PitchRuntimeSettings> PitchPresetJsonStorage::LoadPitchSettings(
	    std::wstring_view filePath, std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ifstream inputFile{ std::filesystem::path(filePath) };
		if (!inputFile.is_open())
		{
			errorMessage = L"Preset JSON file could not be opened.";
			return std::nullopt;
		}

		std::stringstream buffer{};
		buffer << inputFile.rdbuf();
		if (!inputFile.good() && !inputFile.eof())
		{
			errorMessage = L"Failed while reading preset JSON file.";
			return std::nullopt;
		}

		const std::string jsonText = buffer.str();
		PitchRuntimeSettings settings{};
		const auto throwingHand =
		    Common::JsonKeyValueParser::ReadBoolResult(jsonText, "pitcherThrowsLeftHanded");
		if (throwingHand.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid)
		{
			errorMessage = L"Preset JSON pitcherThrowsLeftHanded must be a boolean.";
			return std::nullopt;
		}
		if (throwingHand.value.has_value())
			settings.pitcherThrowsLeftHanded = *throwingHand.value;
		if (!TryReadPitchParametersWithPrefix(jsonText, "", settings.pitchParameters))
		{
			errorMessage = L"Preset JSON format is invalid or missing required fields.";
			return std::nullopt;
		}

		const auto environmentSettingsFile =
		    Common::JsonKeyValueParser::ReadStringResult(jsonText, "environmentSettingsFile");
		if (environmentSettingsFile.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid)
		{
			errorMessage = L"Preset JSON environmentSettingsFile is not a valid JSON string.";
			return std::nullopt;
		}
		if ((environmentSettingsFile.status == Common::JsonKeyValueParser::KeyReadStatus::Valid) &&
		    !environmentSettingsFile.value->empty())
		{
			const std::optional<std::wstring> wideEnvironmentSettingsFile =
			    TryConvertUtf8ToWide(*environmentSettingsFile.value);
			if (!wideEnvironmentSettingsFile.has_value())
			{
				errorMessage = L"Preset JSON environmentSettingsFile is not valid UTF-8.";
				return std::nullopt;
			}
			settings.environmentSettingsFile = *wideEnvironmentSettingsFile;
		}
		else
		{
			settings.environmentSettingsFile = L"EnvironmentSettings.user.json";
		}

		const auto activeProfile =
		    Common::JsonKeyValueParser::ReadIntResult(jsonText, "activePitchProfileIndex");
		const auto randomProfile =
		    Common::JsonKeyValueParser::ReadBoolResult(jsonText, "randomPitchProfileEnabled");
		if (activeProfile.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid ||
		    randomProfile.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid)
		{
			errorMessage = L"Preset JSON pitch profile selection fields are invalid.";
			return std::nullopt;
		}
		if (activeProfile.value.has_value())
			settings.activePitchProfileIndex = *activeProfile.value;
		if (randomProfile.value.has_value())
			settings.randomPitchProfileEnabled = *randomProfile.value;

		const auto profileArrayResult =
		    Common::JsonKeyValueParser::ReadObjectArrayResult(jsonText, "pitchProfiles");
		if (profileArrayResult.status == Common::JsonKeyValueParser::KeyReadStatus::Valid)
		{
			const auto& profileObjectTexts = *profileArrayResult.value;

			settings.pitchProfiles.clear();
			settings.pitchProfiles.reserve(profileObjectTexts.size());
			for (const std::string_view profileObjectText : profileObjectTexts)
			{
				const std::string profileJson{ profileObjectText };
				const auto profileName =
				    Common::JsonKeyValueParser::ReadStringResult(profileJson, "name");
				if (profileName.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid)
				{
					errorMessage = L"Preset JSON pitch profile name is not a valid JSON string.";
					return std::nullopt;
				}
				if ((profileName.status != Common::JsonKeyValueParser::KeyReadStatus::Valid) ||
				    profileName.value->empty())
				{
					errorMessage = L"Preset JSON pitch profile name is missing.";
					return std::nullopt;
				}

				PitchProfileEntry profile{};
				const std::optional<std::wstring> wideProfileName =
				    TryConvertUtf8ToWide(*profileName.value);
				if (!wideProfileName.has_value())
				{
					errorMessage = L"Preset JSON pitch profile name is not valid UTF-8.";
					return std::nullopt;
				}
				profile.name = *wideProfileName;
				if (!TryReadPitchParametersWithPrefix(profileJson, "", profile.pitchParameters))
				{
					errorMessage = L"Preset JSON pitch profile fields are invalid.";
					return std::nullopt;
				}

				settings.pitchProfiles.push_back(profile);
			}
		}
		else if (profileArrayResult.status == Common::JsonKeyValueParser::KeyReadStatus::Invalid)
		{
			errorMessage = L"Preset JSON pitchProfiles array format is invalid.";
			return std::nullopt;
		}
		if (settings.pitchProfiles.empty())
		{
			PitchProfileEntry fallbackProfile{};
			fallbackProfile.name = L"Custom";
			fallbackProfile.pitchParameters = settings.pitchParameters;
			settings.pitchProfiles.push_back(fallbackProfile);
		}
		if (settings.activePitchProfileIndex < 0)
		{
			settings.activePitchProfileIndex = 0;
		}
		if (settings.activePitchProfileIndex >= static_cast<int>(settings.pitchProfiles.size()))
		{
			settings.activePitchProfileIndex = static_cast<int>(settings.pitchProfiles.size()) - 1;
		}

		return settings;
	}

	bool PitchPresetJsonStorage::Save(
	    std::wstring_view filePath,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring& errorMessage) const
	{
		PitchRuntimeSettings settings{};
		settings.pitchParameters = pitchParameters;
		settings.environmentSettingsFile = L"EnvironmentSettings.user.json";
		settings.pitchProfiles =
		    std::vector<PitchProfileEntry>{ PitchProfileEntry{ L"Custom", pitchParameters } };
		settings.activePitchProfileIndex = 0;
		settings.randomPitchProfileEnabled = false;
		return SavePitchSettings(filePath, settings, errorMessage);
	}

	std::optional<Baseball::PitchParameters> PitchPresetJsonStorage::Load(
	    std::wstring_view filePath, std::wstring& errorMessage) const
	{
		const std::optional<PitchRuntimeSettings> settings =
		    LoadPitchSettings(filePath, errorMessage);
		if (!settings.has_value())
		{
			return std::nullopt;
		}
		return settings->pitchParameters;
	}
} // namespace BaseballSimulatorVR::Data
