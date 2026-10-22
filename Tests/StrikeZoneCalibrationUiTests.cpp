#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/StrikeZoneCalibrationUi.hpp"
#include "../BaseballSimulatorVR/Common/UiTextAtlas.hpp"

#include <cmath>
#include <limits>

void RunStrikeZoneCalibrationUiTests()
{
	using namespace BaseballSimulatorVR;
	using namespace Common;
	using Status = StrikeZoneCalibrationStatus;
	StrikeZoneHeightCalibrator calibrator;
	const auto expect = [&](Status status, std::wstring_view text)
	{
		const auto progress = calibrator.GetProgress();
		const auto ui = BuildStrikeZoneCalibrationUi(progress);
		Tests::Check(progress.status == status && ui.statusLine.find(text) != std::wstring::npos,
		    "Calibration UI describes the actual acquisition state");
		Tests::Check(!ui.buttonLabel.empty() && !ui.instructionLine.empty(),
		    "Every calibration state has a dedicated action and a useful instruction");
	};
	expect(Status::WaitingForFloor, L"床基準なし");
	FloorEyeHeightSample sample{ 1.674, 0.0, 1, true, false };
	(void)calibrator.AddSample(sample);
	expect(Status::WaitingForTracking, L"追跡待ち");
	sample.isPositionTracked = true;
	sample.heightMeters = std::numeric_limits<double>::quiet_NaN();
	(void)calibrator.AddSample(sample);
	expect(Status::InvalidHeight, L"高さ範囲外");
	sample.heightMeters = 1.674;
	(void)calibrator.AddSample(sample);
	expect(Status::Steady, L"校正中");
	for (int frame = 1; frame <= 90; ++frame)
	{
		sample.sampleTimeSeconds = frame / 90.0;
		(void)calibrator.AddSample(sample);
	}
	const auto partial = calibrator.GetProgress();
	Tests::Check(std::abs(partial.stableDurationSeconds - 1.0) < 1e-9 &&
	    std::abs(partial.fraction - 1.0 / 3.0) < 1e-9,
	    "One steady second exposes one third progress without completing calibration");
	Tests::Check(BuildStrikeZoneCalibrationUi(partial).statusLine.find(L"1.0 / 3.0秒") != std::wstring::npos,
	    "Steady progress displays elapsed and required duration");
	for (int duplicate = 0; duplicate < 30; ++duplicate) (void)calibrator.AddSample(sample);
	Tests::Check(calibrator.GetProgress().fraction == partial.fraction,
	    "Repeated UI reads of the same XR sample cannot advance the progress display");
	sample.heightMeters += .08;
	sample.sampleTimeSeconds += 1.0 / 90.0;
	(void)calibrator.AddSample(sample);
	expect(Status::Moving, L"動きを検出");
	Tests::Check(calibrator.GetProgress().fraction == 0.0,
	    "Height motion resets the visible acquisition progress");
	const double restartTime = sample.sampleTimeSeconds;
	for (int frame = 1; frame <= 18; ++frame)
	{
		sample.sampleTimeSeconds = restartTime + frame / 90.0;
		(void)calibrator.AddSample(sample);
	}
	expect(Status::Moving, L"動きを検出");
	for (int frame = 19; frame <= 270; ++frame)
	{
		sample.sampleTimeSeconds = restartTime + frame / 90.0;
		(void)calibrator.AddSample(sample);
	}
	expect(Status::Complete, L"校正完了");
	Tests::Check(calibrator.GetProgress().fraction == 1.0 &&
	    BuildStrikeZoneCalibrationUi(calibrator.GetProgress()).buttonLabel == L"ゾーン再校正",
	    "Completed calibration exposes full progress and a clearly named restart button");
	calibrator.Reset();
	Tests::Check(calibrator.GetProgress().fraction == 0.0 && !calibrator.IsComplete(),
	    "The dedicated calibration action restarts progress on its own");
	for (int frame = 0; frame <= 30; ++frame)
	{
		sample.sampleTimeSeconds = frame * .1;
		(void)calibrator.AddSample(sample);
	}
	Tests::Check(calibrator.GetProgress().stableDurationSeconds == 3.0 &&
	    calibrator.GetProgress().fraction < 1.0 && !calibrator.IsComplete(),
	    "Sparse tracking samples cannot display 100 percent before the count requirement is met");
	sample.isFloorReferenced = false;
	(void)calibrator.AddSample(sample);
	expect(Status::WaitingForFloor, L"床基準なし");
	Tests::Check(calibrator.GetProgress().fraction == 0.0,
	    "Losing the floor clears any previously displayed acquisition progress");
	calibrator.UseManualZone();
	expect(Status::Manual, L"手動調整");
	calibrator.UseSavedZone();
	expect(Status::Saved, L"保存値");

	UiText::PanelLayout panel;
	panel.widthMeters = .36;
	panel.heightMeters = .236;
	panel.paddingMeters = .010;
	panel.cellGapMeters = .004;
	panel.columns = CalibrationMainPanelColumns;
	panel.rows = CalibrationMainPanelRows;
	panel.center = { .3, 1.2, -.8 };
	panel.rightAxis = { .8, 0, .6 };
	panel.upAxis = { 0, 1, 0 };
	const double cellWidth = (panel.widthMeters - 2 * panel.paddingMeters -
	    panel.cellGapMeters * (panel.columns - 1)) / panel.columns;
	const double cellHeight = (panel.heightMeters - 2 * panel.paddingMeters -
	    panel.cellGapMeters * (panel.rows - 1)) / panel.rows;
	const auto rect = UiText::BuildPanelCellRect(-panel.widthMeters * .5 + panel.paddingMeters,
	    panel.heightMeters * .5 - panel.paddingMeters, cellWidth, cellHeight,
	    panel.cellGapMeters, panel.columns, CalibrationMainButtonCellIndex);
	const auto world = [&](double x, double y)
	{ return panel.center + panel.rightAxis * x + panel.upAxis * y; };
	Tests::Check(FindOrientedUiPanelCellIndex(panel, world((rect.left + rect.right) * .5,
	    (rect.top + rect.bottom) * .5)) == CalibrationMainButtonCellIndex,
	    "A laser hit on the rendered calibration button resolves to its dedicated main-panel action");
	Tests::Check(!FindOrientedUiPanelCellIndex(panel, world(rect.right + panel.cellGapMeters * .5,
	    (rect.top + rect.bottom) * .5)),
	    "The gap next to the calibration button cannot trigger calibration");
	Tests::Check(FindOrientedUiPanelCellIndex(panel, world(rect.right + panel.cellGapMeters + cellWidth * .5,
	    (rect.top + rect.bottom) * .5)) != CalibrationMainButtonCellIndex,
	    "Reading calibration status alongside the button does not activate the action");
}
