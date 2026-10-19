#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
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

	[[nodiscard]] inline std::wstring BuildPitchPresetLabel(const std::wstring_view fileName)
	{
		constexpr std::wstring_view prefix = L"PitchSettings.", suffix = L".json";
		if (!fileName.starts_with(prefix) || !fileName.ends_with(suffix) ||
		    fileName.size() <= prefix.size() + suffix.size()) return std::wstring{fileName};
		const auto stem = fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
		if (stem == L"sample") return L"サンプル設定";
		if (stem == L"user") return L"ユーザー設定";
		if (stem == L"Yamamoto") return L"山本由伸\n硬式野球 / 右投げ";
		if (stem == L"Toss.Baseball") return L"トス\n硬式野球";
		if (stem == L"Toss.Softball") return L"トス\nソフトボール";
		constexpr std::array<std::pair<std::wstring_view,std::wstring_view>, 11> categories{{
			{L"Difficulty.VeryEasy", L"難易度 ベリーイージー"},
			{L"Challenge1", L"チャレンジ1"},
			{L"Challenge2", L"チャレンジ2"},
			{L"Beginner.Softball", L"初心者 / 標準変化\nソフトボール"},
			{L"Beginner.BigBreak.Softball", L"初心者 / 大きな変化\nソフトボール"},
			{L"SlowFastball.Baseball", L"低速ストレート\n硬式野球"},
			{L"SlowFastball.Softball", L"低速ストレート\nソフトボール"},
			{L"Difficulty.Easy", L"難易度 かんたん"},
			{L"Difficulty.Normal", L"難易度 標準"},
			{L"Difficulty.Hard", L"難易度 難しい"},
			{L"Difficulty.Expert", L"難易度 上級"},
		}};
		for (const auto& [key,label] : categories)
		{
			if (stem.size() != key.size() + 4 || !stem.starts_with(key) || stem[key.size()] != L'.') continue;
			const auto hand = stem.substr(key.size() + 1);
			if (hand == L"LHP") return std::wstring{label} + L" / 左投げ";
			if (hand == L"RHP") return std::wstring{label} + L" / 右投げ";
		}
		return std::wstring{fileName};
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
