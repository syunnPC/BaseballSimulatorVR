#pragma once

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Common
{
	// A vibration that starts at its peak and dies away. OpenXR plays one constant pulse at a
	// time and interrupts a playing pulse with the next, so the envelope is played as a run of
	// short pulses, one per frame, each at the envelope's level at that moment.
	struct HapticEnvelope final
	{
		bool rightHand{};
		double peakAmplitude{};
		double decaySeconds{};
		double lengthSeconds{};
		double elapsedSeconds{};

		[[nodiscard]] bool IsPlaying() const noexcept
		{
			return elapsedSeconds < lengthSeconds && peakAmplitude > 0.0;
		}

		[[nodiscard]] double AmplitudeAt(const double seconds) const noexcept
		{
			if (seconds < 0.0 || seconds >= lengthSeconds) return 0.0;
			return std::clamp(peakAmplitude * std::exp(-seconds / (std::max)(decaySeconds, 1e-3)), 0.0, 1.0);
		}
	};

	// Shorter pulses than this get lost on some runtimes and motors; each pulse also outlasts
	// a late frame so the vibration does not drop out before the next one replaces it.
	inline constexpr double MinimumHapticPulseSeconds = .02;

	// The length of the pulse to send for the envelope's current level, given the frame time.
	[[nodiscard]] inline double HapticPulseSeconds(const HapticEnvelope& envelope, const double frameSeconds) noexcept
	{
		const double remaining = envelope.lengthSeconds - envelope.elapsedSeconds;
		return std::clamp(2.0 * frameSeconds, MinimumHapticPulseSeconds, (std::max)(remaining, MinimumHapticPulseSeconds));
	}

	// A touch slower than this (the bat resting or pushed against a ball on the tee) is not a hit
	// and is not felt as one.
	inline constexpr double BatHapticMinimumImpactMetersPerSecond = 1.5;

	// How a hit feels in the hands. `impactSpeed` is the ball's speed into the bat along the
	// contact normal; `sting` runs from 0 on the sweet spot to 1 at the handle or the end. A
	// well-struck ball is a firm, brief jolt; a miss-hit jars harder and the bat keeps buzzing.
	[[nodiscard]] inline HapticEnvelope BatContactHapticEnvelope(
	    const bool rightHand, const double impactSpeed, const double sting) noexcept
	{
		HapticEnvelope envelope;
		envelope.rightHand = rightHand;
		if (!(impactSpeed >= BatHapticMinimumImpactMetersPerSecond)) return envelope;
		const double impact = std::clamp(impactSpeed / 35.0, 0.0, 1.0);
		const double jar = std::clamp(sting, 0.0, 1.0);
		envelope.peakAmplitude = std::clamp(.45 + .40 * impact + .35 * jar, 0.0, 1.0);
		envelope.decaySeconds = .025 + .12 * jar;
		envelope.lengthSeconds = .07 + .23 * jar;
		return envelope;
	}

	// The pop of a pitch into the catcher's mitt, harder for a faster pitch.
	[[nodiscard]] inline HapticEnvelope MittCatchHapticEnvelope(const bool rightHand, const double pitchSpeed) noexcept
	{
		HapticEnvelope envelope;
		envelope.rightHand = rightHand;
		envelope.peakAmplitude = std::clamp(pitchSpeed / 40.0, .4, 1.0);
		envelope.decaySeconds = .03;
		envelope.lengthSeconds = .09;
		return envelope;
	}
}
