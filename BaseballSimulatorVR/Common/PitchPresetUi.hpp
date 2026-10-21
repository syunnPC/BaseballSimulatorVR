#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace BaseballSimulatorVR::Common
{
	inline constexpr int PitchPanelColumns = 5;
	inline constexpr int PitchPanelRows = 11;
	inline constexpr int PitchPresetNameFirstRow = 9;
	inline constexpr double PitchPanelHeightMeters = .546;

	template <class TPanel>
	void ConfigureExpandedPitchPanel(TPanel& panel)
	{
		panel.widthMeters = .34;
		panel.heightMeters = PitchPanelHeightMeters;
		panel.paddingMeters = .009;
		panel.cellGapMeters = .004;
		panel.columns = PitchPanelColumns;
		panel.rows = PitchPanelRows;
	}

	template <class TPanel, class TRect>
	[[nodiscard]] bool BuildPitchPresetNameRect(const TPanel& panel, TRect& rect)
	{
		if (panel.columns != PitchPanelColumns || panel.rows != PitchPanelRows) return false;
		const double left = -panel.widthMeters * .5 + panel.paddingMeters;
		const double right = panel.widthMeters * .5 - panel.paddingMeters;
		const double top = panel.heightMeters * .5 - panel.paddingMeters;
		const double bottom = -panel.heightMeters * .5 + panel.paddingMeters;
		const double cellHeight = (top - bottom - panel.cellGapMeters * (panel.rows - 1)) / panel.rows;
		if (right <= left || cellHeight <= 0.) return false;
		rect = {left, top - PitchPresetNameFirstRow * (cellHeight + panel.cellGapMeters), right, bottom};
		return rect.top > rect.bottom;
	}

	// Label of a pitch settings entry: the preset's "displayName" from its JSON file, followed by the
	// throwing hand for presets that offer both hands (mirrorForLeftHandedPitcher).
	[[nodiscard]] inline std::wstring BuildPitchPresetLabel(
	    const std::wstring_view displayName, const std::optional<bool> pitcherThrowsLeftHanded)
	{
		std::wstring label{ displayName };
		if (pitcherThrowsLeftHanded.has_value())
			label += *pitcherThrowsLeftHanded ? L" / 左投げ" : L" / 右投げ";
		return label;
	}

	// The application publishes the finished label (above) in UiTelemetry::pitchSettingsFileName, so
	// the XR panel shows it unchanged. Display names live in the preset JSON files, not in code.
	[[nodiscard]] inline std::wstring BuildPitchPresetLabel(const std::wstring_view label)
	{
		return std::wstring{ label };
	}

	template <class TAdvance>
	[[nodiscard]] std::wstring WrapPitchPresetLabel(const std::wstring_view label,
	    const double maximumAdvance, TAdvance&& advance)
	{
		if (!(maximumAdvance > 0.)) throw std::invalid_argument("Preset label width must be positive");
		std::wstring wrapped;
		double lineAdvance = 0.;
		for (std::size_t index = 0; index < label.size();)
		{
			const auto first = static_cast<std::uint32_t>(label[index]);
			std::uint32_t scalar = first;
			std::size_t length = 1;
			if (first >= 0xD800U && first <= 0xDBFFU && index + 1 < label.size())
			{
				const auto second = static_cast<std::uint32_t>(label[index + 1]);
				if (second >= 0xDC00U && second <= 0xDFFFU)
				{
					scalar = 0x10000U + ((first - 0xD800U) << 10U) + second - 0xDC00U;
					length = 2;
				}
			}
			if (scalar == L'\n') lineAdvance = 0.;
			else
			{
				const double glyphAdvance = advance(scalar);
				if (lineAdvance > 0. && lineAdvance + glyphAdvance > maximumAdvance)
				{
					wrapped.push_back(L'\n');
					lineAdvance = 0.;
				}
				lineAdvance += glyphAdvance;
			}
			wrapped.append(label.substr(index,length));
			index += length;
		}
		return wrapped;
	}
}
