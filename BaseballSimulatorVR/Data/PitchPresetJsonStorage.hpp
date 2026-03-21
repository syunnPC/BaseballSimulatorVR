#pragma once

#include "../Baseball/PitchParameters.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BaseballSimulatorVR::Data
{
	struct PitchProfileEntry final
	{
		std::wstring name{ L"Pitch" };
		Baseball::PitchParameters pitchParameters{};
	};

	struct PitchRuntimeSettings final
	{
		Baseball::PitchParameters pitchParameters{};
		std::wstring environmentSettingsFile{ L"EnvironmentSettings.user.json" };
		std::vector<PitchProfileEntry> pitchProfiles{};
		int activePitchProfileIndex{ -1 };
		bool randomPitchProfileEnabled{ true };
	};

	class PitchPresetJsonStorage final
	{
	  public:
		[[nodiscard]] bool SavePitchSettings(
		    std::wstring_view filePath,
		    const PitchRuntimeSettings& settings,
		    std::wstring& errorMessage) const;
		[[nodiscard]] std::optional<PitchRuntimeSettings> LoadPitchSettings(
		    std::wstring_view filePath,
		    std::wstring& errorMessage) const;
		[[nodiscard]] bool Save(std::wstring_view filePath, const Baseball::PitchParameters& pitchParameters, std::wstring& errorMessage) const;
		[[nodiscard]] std::optional<Baseball::PitchParameters> Load(std::wstring_view filePath, std::wstring& errorMessage) const;
	};
} // namespace BaseballSimulatorVR::Data
