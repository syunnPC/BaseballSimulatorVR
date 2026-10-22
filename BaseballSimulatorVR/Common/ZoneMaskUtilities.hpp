#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
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
	// The zone grid's outer ring lies outside the strike zone. Where a pitch is picked from
	// strike and ball cells together, a ball cell beside the zone comes up about a third as
	// often as a strike cell and a far corner about a seventh as often, so about two pitches
	// in three go to the zone with every cell in play. Ball cells picked on their own are
	// equally likely.
	inline constexpr double BallZoneEdgeCellWeight = .35;
	inline constexpr double BallZoneCornerCellWeight = .15;

	[[nodiscard]] constexpr bool IsBallZoneCell(const int zoneIndex, const int columns, const int rows) noexcept
	{
		const int row = zoneIndex / columns, column = zoneIndex % columns;
		return row == 0 || column == 0 || row == rows - 1 || column == columns - 1;
	}

	[[nodiscard]] inline double ZoneCellPickWeight(
	    const int zoneIndex, const int columns, const int rows, const bool strikeCellsInPlay) noexcept
	{
		if (!strikeCellsInPlay || !IsBallZoneCell(zoneIndex, columns, rows)) return 1.0;
		const int row = zoneIndex / columns, column = zoneIndex % columns;
		const bool corner = (row == 0 || row == rows - 1) && (column == 0 || column == columns - 1);
		return corner ? BallZoneCornerCellWeight : BallZoneEdgeCellWeight;
	}

	template <typename TRandomEngine>
	[[nodiscard]] int PickZoneCell(
	    const std::vector<int>& candidates, const int columns, const int rows, TRandomEngine& random)
	{
		if (candidates.empty()) return -1;
		const bool strikeCellsInPlay = std::any_of(candidates.begin(), candidates.end(),
		    [&](const int zoneIndex) { return !IsBallZoneCell(zoneIndex, columns, rows); });
		double total = 0.0;
		for (const int zoneIndex : candidates) total += ZoneCellPickWeight(zoneIndex, columns, rows, strikeCellsInPlay);
		double remaining = std::uniform_real_distribution<double>(0.0, total)(random);
		for (const int zoneIndex : candidates)
		{
			remaining -= ZoneCellPickWeight(zoneIndex, columns, rows, strikeCellsInPlay);
			if (remaining < 0.0) return zoneIndex;
		}
		return candidates.back();
	}
} // namespace BaseballSimulatorVR::Common
