#pragma once

#include "../Baseball/PitchParameters.hpp"

#include <string_view>

namespace BaseballSimulatorVR::Data
{
	enum class PitchPresetType
	{
		Fastball = 0,
		Slider = 1,
		Curveball = 2,
		Changeup = 3,
		Custom = 4,
	};

	class PresetRepository final
	{
	  public:
		[[nodiscard]] Baseball::PitchParameters LoadDefaultPitchParameters() const;
		[[nodiscard]] Baseball::PitchParameters LoadPitchParameters(PitchPresetType presetType) const;
		[[nodiscard]] std::wstring_view GetPresetName(PitchPresetType presetType) const noexcept;
	};
} // namespace BaseballSimulatorVR::Data
