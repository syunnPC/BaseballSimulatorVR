#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace BaseballSimulatorVR::Physics
{
	enum class BatMaterial { Wood, Metal, Urethane };

	struct BatMaterialProfile final
	{
		double restitutionMultiplier;
		double sweetSpotStartRatio;
		double sweetSpotEndRatio;
	};

	// Russell: a representative sweet zone lies 5-7 inches from a 34-inch bat's tip.
	// Material multipliers and the off-center response are simulation parameters.
	inline constexpr std::array<BatMaterialProfile, 3> BatMaterialProfiles{
		BatMaterialProfile{1.00, 27.0 / 34.0, 29.0 / 34.0},
		BatMaterialProfile{1.08, 27.0 / 34.0, 29.0 / 34.0},
		BatMaterialProfile{1.12, 27.0 / 34.0, 29.0 / 34.0}
	};

	[[nodiscard]] constexpr bool IsValidBatMaterial(BatMaterial material) noexcept
	{
		return material == BatMaterial::Wood || material == BatMaterial::Metal ||
		    material == BatMaterial::Urethane;
	}

	// material must satisfy IsValidBatMaterial.
	[[nodiscard]] constexpr const BatMaterialProfile& GetBatMaterialProfile(
	    BatMaterial material) noexcept
	{
		return BatMaterialProfiles[static_cast<std::size_t>(material)];
	}

	[[nodiscard]] constexpr std::string_view BatMaterialName(BatMaterial material) noexcept
	{
		constexpr std::array<std::string_view, 3> names{"Wood", "Metal", "Urethane"};
		return names[static_cast<std::size_t>(material)];
	}

	[[nodiscard]] constexpr std::optional<BatMaterial> ParseBatMaterial(
	    std::string_view name) noexcept
	{
		for (const auto material : {BatMaterial::Wood, BatMaterial::Metal, BatMaterial::Urethane})
			if (BatMaterialName(material) == name) return material;
		return std::nullopt;
	}

	// contactRatio is measured from the full handle origin (0) to the tip (1).
	[[nodiscard]] inline double BatContactRestitutionScale(
	    BatMaterial material, double contactRatio) noexcept
	{
		const auto& profile = GetBatMaterialProfile(material);
		const double centerRatio = 0.5 * (profile.sweetSpotStartRatio + profile.sweetSpotEndRatio);
		constexpr double falloffRatio = 2.0 / 34.0;
		constexpr double minimumResponse = 0.55;
		const double normalizedMiss = (contactRatio - centerRatio) / falloffRatio;
		const double contactQuality = minimumResponse + (1.0 - minimumResponse) *
		    std::exp(-0.5 * normalizedMiss * normalizedMiss);
		return profile.restitutionMultiplier * contactQuality;
	}
}
