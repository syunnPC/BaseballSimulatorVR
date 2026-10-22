#pragma once

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Common
{
	// A vibration that holds at its peak, then dies away. OpenXR plays one constant-strength
	// pulse at a time and a new pulse replaces the playing one, so the envelope is played as
	// one short pulse per frame at the envelope's current strength.
	struct HapticEnvelope final
	{
		bool rightHand{};
		double peakAmplitude{};
		double holdSeconds{};
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
			const double fade = seconds <= holdSeconds
			    ? 1.0 : std::exp(-(seconds - holdSeconds) / (std::max)(decaySeconds, 1e-3));
			return std::clamp(peakAmplitude * fade, 0.0, 1.0);
		}
	};

	// Shorter pulses are lost on some runtimes; each pulse also outlasts a late frame so the
	// vibration does not drop out before the next one replaces it.
	inline constexpr double MinimumHapticPulseSeconds = .02;

	// The length of the pulse to send for the envelope's current strength.
	[[nodiscard]] inline double HapticPulseSeconds(const HapticEnvelope& envelope, const double frameSeconds) noexcept
	{
		const double remaining = envelope.lengthSeconds - envelope.elapsedSeconds;
		return std::clamp(2.0 * frameSeconds, MinimumHapticPulseSeconds, (std::max)(remaining, MinimumHapticPulseSeconds));
	}

	// Slower touches, such as the bat resting against the ball on the tee, are not felt.
	inline constexpr double BatHapticMinimumImpactMetersPerSecond = 1.5;

	// How a hit feels in the hands. `impactSpeed` is the ball's speed into the bat along the
	// contact normal; `sting` runs from 0 on the sweet spot to 1 at the handle or the end. A
	// well-struck ball is a solid knock; a miss-hit jars harder and the bat keeps buzzing.
	[[nodiscard]] inline HapticEnvelope BatContactHapticEnvelope(
	    const bool rightHand, const double impactSpeed, const double sting) noexcept
	{
		HapticEnvelope envelope;
		envelope.rightHand = rightHand;
		if (!(impactSpeed >= BatHapticMinimumImpactMetersPerSecond)) return envelope;
		const double impact = std::clamp(impactSpeed / 35.0, 0.0, 1.0);
		const double jar = std::clamp(sting, 0.0, 1.0);
		envelope.peakAmplitude = std::clamp(.8 + .2 * impact + .2 * jar, 0.0, 1.0);
		envelope.holdSeconds = .05 + .01 * jar;
		envelope.decaySeconds = .06 + .10 * jar;
		envelope.lengthSeconds = .15 + .20 * jar;
		return envelope;
	}

	// The pop of a pitch into the catcher's mitt, harder for a faster pitch.
	[[nodiscard]] inline HapticEnvelope MittCatchHapticEnvelope(const bool rightHand, const double pitchSpeed) noexcept
	{
		HapticEnvelope envelope;
		envelope.rightHand = rightHand;
		envelope.peakAmplitude = std::clamp(pitchSpeed / 40.0, .6, 1.0);
		envelope.holdSeconds = .04;
		envelope.decaySeconds = .05;
		envelope.lengthSeconds = .12;
		return envelope;
	}
}
