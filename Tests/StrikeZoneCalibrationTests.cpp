#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/StrikeZoneCalibration.hpp"
#include "../BaseballSimulatorVR/Common/StrikeZoneSettings.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace
{
	using namespace BaseballSimulatorVR::Common;

	FloorEyeHeightSample Sample(const double eyeHeight, const double time,
	                           const std::uint64_t revision = 0)
	{
		return FloorEyeHeightSample{ eyeHeight, time, revision, true, true };
	}

	std::optional<StrikeZoneHeightCalibration> FeedStable(
	    StrikeZoneHeightCalibrator& calibrator, const double eyeHeight,
	    const double startTime, const std::uint64_t revision = 0)
	{
		std::optional<StrikeZoneHeightCalibration> result;
		for (int frame = 0; frame <= 270; ++frame)
		{
			const auto next = calibrator.AddSample(
			    Sample(eyeHeight, startTime + static_cast<double>(frame) / 90.0, revision));
			if (next) result = next;
		}
		return result;
	}
}

void RunStrikeZoneCalibrationTests()
{
	using namespace BaseballSimulatorVR::Common;
	const auto near = [](const double a, const double b) { return Tests::Near(a, b, 1e-9, 0.0); };
	const double standingEyeHeight = 1.8 * EyeHeightToFullHeightFraction;
	Tests::Check(near(EstimatePlayerHeightFromEyeHeight(standingEyeHeight), 1.8),
	             "Standing eye height converts to full height");
	const auto adultZone = ComputeStrikeZoneFromPlayerHeight(1.8);
	Tests::Check(near(adultZone.bottomMeters, 0.504) && near(adultZone.heightMeters, 0.756),
	             "Adult zone uses the documented height fractions");
	const auto childZone = ComputeStrikeZoneFromPlayerHeight(1.0);
	Tests::Check(near(childZone.bottomMeters, 0.280) && near(childZone.heightMeters, 0.420),
	             "Short player zone preserves proportional standing body boundaries");
	const auto representative = ComputeStrikeZoneFromPlayerHeight(1.7058);
	Tests::Check(std::abs(representative.bottomMeters + representative.heightMeters - 1.2029) < .02 &&
	    representative.bottomMeters > .27 * 1.7058,
	    "Standing upper boundary approximates the AIST torso midpoint while the knee boundary is slightly higher than ABS");
	for (double height : {1.0, 1.2, 1.5, 1.8, 2.0, 2.5})
	{
		const auto estimated = ComputeStrikeZoneFromPlayerHeight(height);
		Tests::Check(IsValidStrikeZoneSettings({estimated.bottomMeters, estimated.heightMeters, .44}),
		    "Every supported player height produces a persistable zone without clamping");
	}

	const std::array invalidHeights{
		0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()
	};
	for (const double height : invalidHeights)
	{
		Tests::Check(EstimatePlayerHeightFromEyeHeight(height) == 0.0,
		             "Invalid eye height cannot propagate into player height");
		const auto zone = ComputeStrikeZoneFromPlayerHeight(height);
		Tests::Check(zone.bottomMeters == 0.0 && zone.heightMeters == 0.0,
		             "Invalid player height produces no zone");
	}

	StrikeZoneHeightCalibrator calibrator;
	Tests::Check(!calibrator.AddSample(Sample(standingEyeHeight, 0.0)),
	             "One valid frame cannot calibrate a player");
	for (int duplicate = 0; duplicate < 500; ++duplicate)
	{
		Tests::Check(!calibrator.AddSample(Sample(standingEyeHeight, 0.0)),
		             "Duplicate XR frames cannot advance calibration");
	}
	Tests::Check(calibrator.GetSampleCount() == 1, "Repeated reads count as one XR sample");
	for (int frame = 1; frame < 270; ++frame)
	{
		Tests::Check(!calibrator.AddSample(Sample(
		    standingEyeHeight + ((frame % 2 == 0) ? 0.005 : -0.005),
		    static_cast<double>(frame) / 90.0)),
		    "Natural small head sway waits for a complete stable interval");
	}
	const auto stable = calibrator.AddSample(Sample(standingEyeHeight + 0.005, 3.0));
	Tests::Check(stable && near(stable->playerHeightMeters, 1.8),
	             "Three seconds of tracked standing samples calibrate using the mean");
	Tests::Check(calibrator.IsComplete(), "Calibration marks completion");
	Tests::Check(!calibrator.AddSample(Sample(standingEyeHeight, 3.01)),
	             "A completed calibration emits its result only once");
	calibrator.Reset();
	Tests::Check(!calibrator.IsComplete() && calibrator.GetSampleCount() == 0,
	             "Explicit recalibration starts a fresh sample window");
	const auto shorterPlayer = FeedStable(calibrator, 1.2 * EyeHeightToFullHeightFraction, 10.0);
	Tests::Check(shorterPlayer && near(shorterPlayer->playerHeightMeters, 1.2),
	             "Explicit recalibration replaces an earlier player's height");

	// Standing up during startup must invalidate the earlier crouched samples.
	calibrator.Reset();
	for (int frame = 0; frame < 90; ++frame)
	{
		Tests::Check(!calibrator.AddSample(Sample(1.1, static_cast<double>(frame) / 90.0)),
		             "Brief initial crouch cannot complete calibration");
	}
	const auto afterStanding = FeedStable(calibrator, standingEyeHeight, 1.0);
	Tests::Check(afterStanding && near(afterStanding->playerHeightMeters, 1.8),
	             "A height transition discards initial crouched samples");

	// Every invalid measurement interrupts continuity; no old observations bridge it.
	for (int invalidCase = 0; invalidCase < 8; ++invalidCase)
	{
		calibrator.Reset();
		(void)calibrator.AddSample(Sample(standingEyeHeight, 0.0));
		(void)calibrator.AddSample(Sample(standingEyeHeight, 0.01));
		auto invalid = Sample(standingEyeHeight, 0.02);
		switch (invalidCase)
		{
		case 0: invalid.isFloorReferenced = false; break;
		case 1: invalid.isPositionTracked = false; break;
		case 2: invalid.heightMeters = std::numeric_limits<double>::quiet_NaN(); break;
		case 3: invalid.heightMeters = std::numeric_limits<double>::infinity(); break;
		case 4: invalid.sampleTimeSeconds = std::numeric_limits<double>::quiet_NaN(); break;
		case 5: invalid.sampleTimeSeconds = std::numeric_limits<double>::infinity(); break;
		case 6: invalid.heightMeters = 0.8; break;
		case 7: invalid.heightMeters = 2.6; break;
		}
		Tests::Check(!calibrator.AddSample(invalid) && calibrator.GetSampleCount() == 0,
		             "LOCAL, untracked, nonfinite and implausible samples clear the window");
		Tests::Check(FeedStable(calibrator, standingEyeHeight, 1.0).has_value(),
		             "Calibration recovers after a rejected measurement");
	}

	calibrator.Reset();
	(void)calibrator.AddSample(Sample(standingEyeHeight, 1.0));
	(void)calibrator.AddSample(Sample(standingEyeHeight, 1.01));
	(void)calibrator.AddSample(Sample(standingEyeHeight, 1.3));
	Tests::Check(calibrator.GetSampleCount() == 1 && calibrator.GetStableDurationSeconds() == 0.0,
	             "A tracking gap restarts the stability interval");
	(void)calibrator.AddSample(Sample(standingEyeHeight, 1.31, 1));
	Tests::Check(calibrator.GetSampleCount() == 1,
	             "A floor reference change starts a new coordinate-system window");
	(void)calibrator.AddSample(Sample(standingEyeHeight, 1.30, 1));
	Tests::Check(calibrator.GetSampleCount() == 1,
	             "A backwards XR timestamp cannot extend a stable window");
	Tests::Check(FeedStable(calibrator, standingEyeHeight, 1.31, 1).has_value(),
	             "Calibration recovers after reference and timestamp discontinuities");

	// Elapsed wall time alone is insufficient when tracking provides too few frames.
	calibrator.Reset();
	for (int sampleIndex = 0; sampleIndex <= 30; ++sampleIndex)
	{
		Tests::Check(!calibrator.AddSample(Sample(
		    standingEyeHeight, static_cast<double>(sampleIndex) * 0.1)),
		    "Three seconds with sparse samples does not bypass the sample-count requirement");
	}
	calibrator.Reset();
	const auto smallestPlayer = FeedStable(calibrator, EyeHeightToFullHeightFraction, 0.0);
	Tests::Check(smallestPlayer && near(smallestPlayer->strikeZone.heightMeters, 0.420),
	             "The lower supported standing-height boundary calibrates without clamping");
	calibrator.Reset();
	const auto tallestPlayer = FeedStable(calibrator, 2.5 * EyeHeightToFullHeightFraction, 0.0);
	Tests::Check(tallestPlayer && near(tallestPlayer->playerHeightMeters, 2.5),
	             "The upper supported standing-height boundary calibrates");
	calibrator.Reset();
	(void)calibrator.AddSample(Sample(standingEyeHeight, 0.0));
	calibrator.UseManualZone();
	Tests::Check(calibrator.IsComplete() && calibrator.GetSampleCount() == 0 &&
	    calibrator.GetProgress().status == StrikeZoneCalibrationStatus::Manual &&
	    !FeedStable(calibrator, standingEyeHeight, 1.0),
	    "A manual adjustment stops an unfinished calibration from overwriting the chosen zone");
	calibrator.UseSavedZone();
	Tests::Check(calibrator.GetProgress().status == StrikeZoneCalibrationStatus::Saved &&
	    !FeedStable(calibrator, standingEyeHeight, 5.0),
	    "Restored player settings stay active until an explicit recalibration");
	calibrator.Reset();
	Tests::Check(FeedStable(calibrator, standingEyeHeight, 9.0).has_value(),
	    "Explicit recalibration resumes height acquisition after manual or saved settings");
	StrikeZoneRuntimeSettings boundary{.5, .205, .44};
	Tests::Check(AdjustStrikeZoneBoundary(boundary, true, -.01) &&
	    IsValidStrikeZoneSettings(boundary) && near(boundary.bottomMeters, .5) &&
	    near(boundary.heightMeters, .2) && !AdjustStrikeZoneBoundary(boundary, true, -.01),
	    "Reaching the minimum manual zone height remains valid at floating-point boundaries");
}
