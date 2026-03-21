#pragma once

#include <algorithm>
#include <concepts>

namespace BaseballSimulatorVR::Common::Math
{
	template <std::floating_point TValue>
	[[nodiscard]] constexpr TValue Clamp01(const TValue value) noexcept
	{
		return std::clamp(value, static_cast<TValue>(0), static_cast<TValue>(1));
	}

	template <std::floating_point TValue>
	[[nodiscard]] constexpr TValue Lerp(
	    const TValue startValue,
	    const TValue endValue,
	    const TValue blendFactor) noexcept
	{
		return startValue +
		       ((endValue - startValue) * blendFactor);
	}
} // namespace BaseballSimulatorVR::Common::Math
