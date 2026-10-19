#pragma once

#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace BaseballSimulatorVR::Common
{
	inline constexpr std::size_t MetricsControlButtonCount = 17;
	inline constexpr std::size_t MetricsControlColumns = 6;
	inline constexpr int MetricsZoneBottomDownIndex = 13;
	inline constexpr int MetricsZoneBottomUpIndex = 14;
	inline constexpr int MetricsZoneTopDownIndex = 15;
	inline constexpr int MetricsZoneTopUpIndex = 16;
	inline constexpr double MetricsPanelHeightMeters = .29;

	template <class TPanel, class TRect>
	[[nodiscard]] bool BuildMetricsPanelControlLayout(const TPanel& panel, TRect& body,
	    std::array<TRect,MetricsControlButtonCount>& buttons)
	{
		if (panel.columns <= 0 || panel.rows <= 1) return false;
		const double left = -panel.widthMeters * .5 + panel.paddingMeters;
		const double right = panel.widthMeters * .5 - panel.paddingMeters;
		const double top = panel.heightMeters * .5 - panel.paddingMeters;
		const double bottom = -panel.heightMeters * .5 + panel.paddingMeters;
		const double cellHeight = (top - bottom - panel.cellGapMeters * (panel.rows - 1)) / panel.rows;
		if (cellHeight <= 0.) return false;
		const double gap = std::clamp(panel.cellGapMeters * .8, .0025, .0100);
		const double buttonHeight = std::clamp(cellHeight * .80, .018, .045);
		constexpr std::size_t rows = (MetricsControlButtonCount + MetricsControlColumns - 1) / MetricsControlColumns;
		const double buttonTop = bottom + buttonHeight * rows + gap * (rows - 1);
		const double buttonWidth = (right - left - gap * (MetricsControlColumns - 1)) / MetricsControlColumns;
		body = {left, top - cellHeight - gap, right, buttonTop + gap};
		if (buttonWidth <= 0. || body.top <= body.bottom) return false;
		for (std::size_t i = 0; i < buttons.size(); ++i)
		{
			const double x = left + static_cast<double>(i % MetricsControlColumns) * (buttonWidth + gap);
			const double y = buttonTop - static_cast<double>(i / MetricsControlColumns) * (buttonHeight + gap);
			buttons[i] = {x, y, x + buttonWidth, y - buttonHeight};
		}
		return true;
	}

	template <class TPanel, class TRect>
	[[nodiscard]] std::optional<int> FindMetricsPanelControl(const TPanel& panel,
	    const Physics::Vector3& hit, const std::array<TRect,MetricsControlButtonCount>& buttons)
	{
		const auto delta = hit - panel.center;
		const double x = Physics::Dot(delta, panel.rightAxis), y = Physics::Dot(delta, panel.upAxis);
		for (std::size_t i = 0; i < buttons.size(); ++i)
			if (x >= buttons[i].left && x <= buttons[i].right && y <= buttons[i].top && y >= buttons[i].bottom)
				return static_cast<int>(i);
		return std::nullopt;
	}
}
