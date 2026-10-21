#include "PitchPresetJsonStorage.hpp"
#include "JsonSettingsReader.hpp"
#include "../Common/AtomicFileWriter.hpp"
#include "../Common/JsonWriter.hpp"
#include "../Common/TextConversion.hpp"

#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		namespace Json = Common::JsonKeyValueParser;
		namespace JsonWriter = Common::JsonWriter;

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
				return "Fixed drag factor k = rho*A*C_D/(2m) [1/m] at 1.225 kg/m^3 (drag acceleration = k*v^2); a real baseball is about 0.0066. Ignored when useCoefficientTable is true.";
			}
			if (fieldName == "liftCoefficient")
			{
				return "Fixed Magnus factor [1/s] (acceleration = factor*v*(rpm/3000)*sin(angle between spin axis and velocity)). Ignored when useCoefficientTable is true.";
			}
			if (fieldName == "useCoefficientTable")
			{
				return "Use speed/spin-dependent physical drag and Magnus coefficients (Nathan 2017 fits) instead of the fixed factors.";
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
				return "Seam perturbation phase [rad]; for a knuckleball, the seams' starting orientation.";
			}
			if (fieldName == "knuckleball")
			{
				return "Knuckleball: the slowly turning seams push the ball one way and then the other. Pitchers vary the seams' orientation, spin axis and rate from throw to throw.";
			}
			return "";
		}

		[[nodiscard]] bool ReadPitchParameters(
		    JsonSettings::MemberReader& reader,
		    Baseball::PitchParameters& outputPitchParameters)
		{
			Baseball::PitchParameters parameters{};
			double initialSpeedMetersPerSecond = 0.0;
			if (!reader.Double("releaseHeightMeters", parameters.releaseHeightMeters, false) ||
			    !reader.Double("initialSpeedMetersPerSecond", initialSpeedMetersPerSecond, true) ||
			    !reader.Double("launchElevationDegrees", parameters.launchElevationDegrees, false) ||
			    !reader.Bool("usePhysicalAerodynamics", parameters.usePhysicalAerodynamics, false) ||
			    !reader.Bool("underhandToss", parameters.underhandToss, false) ||
			    !reader.Range("launchElevationDegrees", parameters.launchElevationDegrees, -75.0, 75.0) ||
			    !reader.Double("spinAxisX", parameters.spinAxis.x, true) ||
			    !reader.Double("spinAxisY", parameters.spinAxis.y, true) ||
			    !reader.Double("spinAxisZ", parameters.spinAxis.z, true) ||
			    !reader.Double("spinRpm", parameters.spinRpm, true) ||
			    !reader.Double("dragCoefficient", parameters.dragCoefficient, true) ||
			    !reader.Double("liftCoefficient", parameters.liftCoefficient, true) ||
			    !reader.Bool("useCoefficientTable", parameters.useCoefficientTable, true) ||
			    !reader.Bool("enableSeamPerturbation", parameters.enableSeamPerturbation, true) ||
			    !reader.Double("seamPerturbationAmplitude", parameters.seamPerturbationAmplitude, true) ||
			    !reader.Double("seamPerturbationFrequencyHz", parameters.seamPerturbationFrequencyHz, true) ||
			    !reader.Double("seamPerturbationPhase", parameters.seamPerturbationPhase, true) ||
			    !reader.Bool("knuckleball", parameters.knuckleball, false))
			{
				return false;
			}
			const double speed = std::abs(initialSpeedMetersPerSecond);
			parameters.initialVelocity = Physics::Vector3{ 0.0,
			    speed * std::tan(parameters.launchElevationDegrees * std::numbers::pi / 180.0), speed };
			outputPitchParameters = parameters;
			return true;
		}

		void AppendPitchParameterEntries(
		    JsonWriter::ObjectBuilder& json,
		    const Baseball::PitchParameters& pitchParameters)
		{
			const auto addNumber = [&](const std::string_view key, const double value)
			{
				json.AddComment(key, GetPitchParameterComment(key));
				(void)json.AddNumber(key, value);
			};
			const auto addBool = [&](const std::string_view key, const bool value)
			{
				json.AddComment(key, GetPitchParameterComment(key));
				json.AddBool(key, value);
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
			if (pitchParameters.knuckleball)
			{
				addBool("knuckleball", true);
			}
		}
	} // namespace

	Baseball::PitchParameters MirrorPitchParametersForOppositeHand(
	    const Baseball::PitchParameters& parameters) noexcept
	{
		Baseball::PitchParameters mirrored = parameters;
		mirrored.initialVelocity.x = -parameters.initialVelocity.x;
		mirrored.spinAxis.y = -parameters.spinAxis.y;
		mirrored.spinAxis.z = -parameters.spinAxis.z;
		return mirrored;
	}

	PitchRuntimeSettings MirrorPitchSettingsForOppositeHand(const PitchRuntimeSettings& settings)
	{
		PitchRuntimeSettings mirrored = settings;
		mirrored.pitcherThrowsLeftHanded = !settings.pitcherThrowsLeftHanded;
		mirrored.mirrorForLeftHandedPitcher = false;
		mirrored.pitchParameters = MirrorPitchParametersForOppositeHand(settings.pitchParameters);
		for (PitchProfileEntry& profile : mirrored.pitchProfiles)
		{
			profile.pitchParameters = MirrorPitchParametersForOppositeHand(profile.pitchParameters);
		}
		return mirrored;
	}

	bool PitchPresetJsonStorage::SavePitchSettings(
	    std::wstring_view filePath,
	    const PitchRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		JsonWriter::ObjectBuilder json{};
		if (!settings.displayName.empty())
		{
			const std::optional<std::string> displayName = Common::TextConversion::TryWideToUtf8(settings.displayName);
			if (!displayName.has_value())
			{
				errorMessage = L"Failed to convert the preset display name to UTF-8.";
				return false;
			}
			json.AddComment("displayName", "Preset name shown in the pitch panel; the throwing hand is appended.");
			json.AddString("displayName", *displayName);
		}
		if (settings.mirrorForLeftHandedPitcher)
		{
			json.AddComment("mirrorForLeftHandedPitcher",
			    "Also offer the opposite-hand variant (lateral velocity and spin axis Y/Z mirrored).");
			json.AddBool("mirrorForLeftHandedPitcher", true);
		}
		AppendPitchParameterEntries(json, settings.pitchParameters);
		json.AddComment("pitcherThrowsLeftHanded", "Pitcher throws with the left hand when true; right hand when false.");
		json.AddBool("pitcherThrowsLeftHanded", settings.pitcherThrowsLeftHanded);

		std::string environmentFilePath{ "EnvironmentSettings.user.json" };
		if (!settings.environmentSettingsFile.empty())
		{
			const std::optional<std::string> environmentFilePathUtf8 =
			    Common::TextConversion::TryWideToUtf8(settings.environmentSettingsFile);
			if (!environmentFilePathUtf8.has_value())
			{
				errorMessage = L"Failed to convert environment settings file path to UTF-8.";
				return false;
			}
			environmentFilePath = *environmentFilePathUtf8;
		}
		json.AddComment("environmentSettingsFile", "Referenced environment settings JSON file name.");
		json.AddString("environmentSettingsFile", environmentFilePath);
		json.AddComment("activePitchProfileIndex", "Selected pitch profile index when random selection is disabled.");
		json.AddInteger("activePitchProfileIndex", settings.activePitchProfileIndex);
		json.AddComment("randomPitchProfileEnabled", "If true, choose pitch profile randomly.");
		json.AddBool("randomPitchProfileEnabled", settings.randomPitchProfileEnabled);

		std::vector<JsonWriter::ObjectBuilder> profiles{};
		profiles.reserve(settings.pitchProfiles.size());
		for (const PitchProfileEntry& profile : settings.pitchProfiles)
		{
			std::string profileName{ "Pitch" };
			if (!profile.name.empty())
			{
				const std::optional<std::string> profileNameUtf8 = Common::TextConversion::TryWideToUtf8(profile.name);
				if (!profileNameUtf8.has_value())
				{
					errorMessage = L"Failed to convert pitch profile name to UTF-8.";
					return false;
				}
				profileName = *profileNameUtf8;
			}
			JsonWriter::ObjectBuilder& entry = profiles.emplace_back();
			entry.AddComment("name", "Pitch profile display name used in UI/logs.");
			entry.AddString("name", profileName);
			AppendPitchParameterEntries(entry, profile.pitchParameters);
		}
		json.AddComment("pitchProfiles", "Array of pitch profiles and their pitch parameters.");
		json.AddObjectArray("pitchProfiles", profiles);
		if (json.HasFailed())
		{
			errorMessage = L"Pitch settings contain a value that is not a finite number.";
			return false;
		}

		const Common::FileOperationResult result = Common::WriteFileAtomically(
		    std::filesystem::path{ filePath }, json.BuildDocument(), JsonSettings::SettingsWriteOptions());
		if (!result)
		{
			errorMessage = L"Failed to save the pitch settings JSON: " + result.errorMessage;
			return false;
		}
		return true;
	}

	std::optional<PitchRuntimeSettings> PitchPresetJsonStorage::LoadPitchSettings(
	    std::wstring_view filePath, std::wstring& errorMessage) const
	{
		errorMessage.clear();
		std::wstring readError{};
		const std::optional<std::string> text = Common::ReadWholeFile(std::filesystem::path{ filePath }, readError);
		if (!text.has_value())
		{
			errorMessage = L"Preset JSON file could not be opened: " + readError;
			return std::nullopt;
		}
		return ParsePitchSettings(*text, errorMessage);
	}

	std::optional<PitchRuntimeSettings> PitchPresetJsonStorage::ParsePitchSettings(
	    const std::string_view jsonText, std::wstring& errorMessage) const
	{
		errorMessage.clear();
		const Json::JsonDocument document = Json::JsonDocument::Parse(jsonText);
		if (!document.IsValid())
		{
			errorMessage = L"Preset JSON is not valid JSON: " + JsonSettings::DescribeDiagnostic(document.Error());
			return std::nullopt;
		}

		PitchRuntimeSettings settings{};
		JsonSettings::MemberReader reader{ document.Root(), L"Preset JSON is invalid", errorMessage };
		if (!reader.Bool("pitcherThrowsLeftHanded", settings.pitcherThrowsLeftHanded, false) ||
		    !reader.WideString("displayName", settings.displayName, false) ||
		    !reader.Bool("mirrorForLeftHandedPitcher", settings.mirrorForLeftHandedPitcher, false) ||
		    !ReadPitchParameters(reader, settings.pitchParameters) ||
		    !reader.WideString("environmentSettingsFile", settings.environmentSettingsFile, false) ||
		    !reader.Int("activePitchProfileIndex", settings.activePitchProfileIndex, false) ||
		    !reader.Bool("randomPitchProfileEnabled", settings.randomPitchProfileEnabled, false))
		{
			return std::nullopt;
		}
		if (settings.environmentSettingsFile.empty())
		{
			settings.environmentSettingsFile = std::wstring{ DefaultEnvironmentSettingsReference };
		}

		const auto profileArray = reader.Object().ReadObjectArray("pitchProfiles");
		if (profileArray.status == Json::KeyReadStatus::Invalid)
		{
			errorMessage = L"Preset JSON pitchProfiles array format is invalid: " +
			    JsonSettings::DescribeDiagnostic(profileArray.diagnostic);
			return std::nullopt;
		}
		if (profileArray.status == Json::KeyReadStatus::Valid)
		{
			settings.pitchProfiles.reserve(profileArray.value->size());
			for (const Json::JsonObjectView& profileObject : *profileArray.value)
			{
				JsonSettings::MemberReader profileReader{ profileObject, L"Preset JSON pitch profile is invalid",
					                                      errorMessage };
				PitchProfileEntry profile{};
				profile.name.clear();
				if (!profileReader.WideString("name", profile.name, true))
				{
					return std::nullopt;
				}
				if (profile.name.empty())
				{
					(void)profileReader.Fail("name", "the pitch profile name must not be empty");
					return std::nullopt;
				}
				if (!ReadPitchParameters(profileReader, profile.pitchParameters))
				{
					return std::nullopt;
				}
				settings.pitchProfiles.push_back(std::move(profile));
			}
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
		settings.environmentSettingsFile = std::wstring{ DefaultEnvironmentSettingsReference };
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
