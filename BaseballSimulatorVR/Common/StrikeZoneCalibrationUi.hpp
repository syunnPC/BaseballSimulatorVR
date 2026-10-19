#pragma once

#include "StrikeZoneCalibration.hpp"
#include "../Physics/Vector3.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace BaseballSimulatorVR::Common
{
	inline constexpr int CalibrationMainPanelColumns = 4;
	inline constexpr int CalibrationMainPanelRows = 3;
	inline constexpr int CalibrationMainButtonCellIndex = 8;
	inline constexpr int CalibrationMetricsButtonIndex = 10;

	template <typename TPanelLayout>
	[[nodiscard]] std::optional<int> FindOrientedUiPanelCellIndex(
	    const TPanelLayout& panel, const Physics::Vector3& hitPoint)
	{
		if (panel.columns <= 0 || panel.rows <= 0) return std::nullopt;
		const double left = -panel.widthMeters * .5 + panel.paddingMeters;
		const double top = panel.heightMeters * .5 - panel.paddingMeters;
		const double width = (panel.widthMeters - 2 * panel.paddingMeters -
		    panel.cellGapMeters * (panel.columns - 1)) / panel.columns;
		const double height = (panel.heightMeters - 2 * panel.paddingMeters -
		    panel.cellGapMeters * (panel.rows - 1)) / panel.rows;
		if (width <= 0 || height <= 0) return std::nullopt;
		const auto relative = hitPoint - panel.center;
		const double x = Physics::Dot(relative, panel.rightAxis);
		const double y = Physics::Dot(relative, panel.upAxis);
		for (int row = 0; row < panel.rows; ++row)
		{
			const double cellTop = top - row * (height + panel.cellGapMeters);
			for (int column = 0; column < panel.columns; ++column)
			{
				const double cellLeft = left + column * (width + panel.cellGapMeters);
				if (x >= cellLeft && x <= cellLeft + width && y <= cellTop && y >= cellTop - height)
					return row * panel.columns + column;
			}
		}
		return std::nullopt;
	}

	struct StrikeZoneCalibrationUi final
	{
		std::wstring buttonLabel;
		std::wstring statusLine;
		std::wstring instructionLine;
	};

	[[nodiscard]] inline StrikeZoneCalibrationUi BuildStrikeZoneCalibrationUi(
	    const StrikeZoneCalibrationProgress& progress)
	{
		using Status = StrikeZoneCalibrationStatus;
		StrikeZoneCalibrationUi ui{ L"ゾーン校正", {}, L"直立して静止・3秒" };
		switch (progress.status)
		{
		case Status::WaitingForFloor:
			ui.statusLine = L"校正待ち：床基準なし";
			ui.instructionLine = L"XRの床設定を確認";
			break;
		case Status::WaitingForTracking:
			ui.statusLine = L"校正待ち：HMD追跡待ち";
			break;
		case Status::InvalidHeight:
			ui.statusLine = L"校正待ち：高さ範囲外";
			ui.instructionLine = L"HMDを装着して直立";
			break;
		case Status::Moving:
			ui.statusLine = L"校正中：動きを検出";
			ui.buttonLabel = L"校正やり直す";
			break;
		case Status::Steady:
		{
			std::wostringstream text;
			text << std::fixed << std::setprecision(1) << L"校正中 "
			     << progress.stableDurationSeconds << L" / 3.0秒 ("
			     << static_cast<int>(progress.fraction * 100.0) << L"%)";
			ui.statusLine = text.str();
			ui.buttonLabel = L"校正やり直す";
			break;
		}
		case Status::Complete:
			ui.statusLine = L"ゾーン校正完了";
			ui.buttonLabel = L"ゾーン再校正";
			ui.instructionLine = L"別の人は再校正";
			break;
		case Status::Manual:
			ui.statusLine = L"ゾーン：手動調整";
			ui.buttonLabel = L"ゾーン再校正";
			ui.instructionLine = L"自動推定は再校正";
			break;
		case Status::Saved:
			ui.statusLine = L"ゾーン：保存値を使用";
			ui.buttonLabel = L"ゾーン再校正";
			ui.instructionLine = L"別の人は再校正";
			break;
		}
		return ui;
	}
}
