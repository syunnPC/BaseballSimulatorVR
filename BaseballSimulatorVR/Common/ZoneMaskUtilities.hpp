#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	[[nodiscard]] constexpr int ClampZoneCount(const int zoneCount) noexcept
	{
		return std::clamp(zoneCount, 0, 32);
	}

	[[nodiscard]] constexpr std::uint32_t BuildZoneBit(const int zoneIndex) noexcept
	{
		if ((zoneIndex < 0) || (zoneIndex >= 32))
		{
			return 0U;
		}
		return static_cast<std::uint32_t>(1U) << zoneIndex;
	}

	[[nodiscard]] constexpr bool IsValidZoneIndex(
	    const int zoneIndex,
	    const int zoneCount) noexcept
	{
		const int boundedZoneCount = ClampZoneCount(zoneCount);
		return (zoneIndex >= 0) && (zoneIndex < boundedZoneCount);
	}

	[[nodiscard]] constexpr std::uint32_t NormalizeZoneMask(
	    const std::uint32_t zoneMask,
	    const int zoneCount) noexcept
	{
		const int boundedZoneCount = ClampZoneCount(zoneCount);
		if (boundedZoneCount == 0)
		{
			return 0U;
		}
		if (boundedZoneCount >= 32)
		{
			return zoneMask;
		}

		const std::uint32_t validBits =
		    (static_cast<std::uint32_t>(1U) << boundedZoneCount) - 1U;
		return zoneMask & validBits;
	}

	[[nodiscard]] inline std::optional<int> FindFirstSelectedZoneIndex(
	    const std::uint32_t zoneMask,
	    const int zoneCount) noexcept
	{
		const std::uint32_t normalizedZoneMask = NormalizeZoneMask(
		    zoneMask,
		    zoneCount);
		if (normalizedZoneMask == 0U)
		{
			return std::nullopt;
		}

		const int boundedZoneCount = ClampZoneCount(zoneCount);
		for (int zoneIndex = 0; zoneIndex < boundedZoneCount; ++zoneIndex)
		{
			if ((normalizedZoneMask & BuildZoneBit(zoneIndex)) != 0U)
			{
				return zoneIndex;
			}
		}

		return std::nullopt;
	}

	[[nodiscard]] inline std::vector<int> CollectSelectedZoneIndices(
	    const std::uint32_t zoneMask,
	    const int zoneCount)
	{
		const std::uint32_t normalizedZoneMask = NormalizeZoneMask(
		    zoneMask,
		    zoneCount);
		const int boundedZoneCount = ClampZoneCount(zoneCount);
		std::vector<int> selectedZoneIndices{};
		if ((normalizedZoneMask == 0U) || (boundedZoneCount <= 0))
		{
			return selectedZoneIndices;
		}

		selectedZoneIndices.reserve(static_cast<std::size_t>(boundedZoneCount));
		for (int zoneIndex = 0; zoneIndex < boundedZoneCount; ++zoneIndex)
		{
			if ((normalizedZoneMask & BuildZoneBit(zoneIndex)) != 0U)
			{
				selectedZoneIndices.push_back(zoneIndex);
			}
		}
		return selectedZoneIndices;
	}

	[[nodiscard]] inline std::wstring BuildZoneSelectionLabel(
	    const std::uint32_t zoneMask,
	    const int zoneCount,
	    const std::wstring_view emptyLabel = L"random",
	    const std::wstring_view separator = L"+")
	{
		const std::uint32_t normalizedZoneMask = NormalizeZoneMask(
		    zoneMask,
		    zoneCount);
		if (normalizedZoneMask == 0U)
		{
			return std::wstring{ emptyLabel };
		}

		const int boundedZoneCount = ClampZoneCount(zoneCount);
		std::wstring label{};
		for (int zoneIndex = 0; zoneIndex < boundedZoneCount; ++zoneIndex)
		{
			if ((normalizedZoneMask & BuildZoneBit(zoneIndex)) == 0U)
			{
				continue;
			}
			if (!label.empty())
			{
				label += separator;
			}
			label += std::to_wstring(zoneIndex + 1);
		}

		return label;
	}
} // namespace BaseballSimulatorVR::Common
