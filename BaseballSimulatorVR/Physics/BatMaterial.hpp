#pragma once

#include "BatGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace BaseballSimulatorVR::Physics
{
	// Beginner is a forgiving practice bat with no sweet spot: every hit outside the grip
	// launches the ball as well as a regular bat's sweet spot would, only livelier.
	enum class BatMaterial { Wood, Metal, Urethane, Beginner };

	struct BatMaterialProfile final
	{
		double restitutionMultiplier;
		double sweetSpotStartRatio;
		double sweetSpotEndRatio;
	};

	// Russell: a representative sweet zone lies 5-7 inches from a 34-inch bat's tip.
	// Material multipliers and the off-center response are simulation parameters.
	// The beginner bat's marked zone stops where the tip rounds off, but its response
	// stays at full strength out to the tip.
	inline constexpr std::array<BatMaterialProfile, 4> BatMaterialProfiles{
		BatMaterialProfile{1.00, 27.0 / 34.0, 29.0 / 34.0},
		BatMaterialProfile{1.08, 27.0 / 34.0, 29.0 / 34.0},
		BatMaterialProfile{1.12, 27.0 / 34.0, 29.0 / 34.0},
		BatMaterialProfile{1.30, BatGripEndRatio, .965}
	};
	inline constexpr std::array<BatMaterial, 4> AllBatMaterials{
		BatMaterial::Wood, BatMaterial::Metal, BatMaterial::Urethane, BatMaterial::Beginner};

	[[nodiscard]] constexpr bool IsValidBatMaterial(BatMaterial material) noexcept
	{
		return material == BatMaterial::Wood || material == BatMaterial::Metal ||
		    material == BatMaterial::Urethane || material == BatMaterial::Beginner;
	}

	// material must satisfy IsValidBatMaterial.
	[[nodiscard]] constexpr const BatMaterialProfile& GetBatMaterialProfile(
	    BatMaterial material) noexcept
	{
		return BatMaterialProfiles[static_cast<std::size_t>(material)];
	}

	[[nodiscard]] constexpr std::string_view BatMaterialName(BatMaterial material) noexcept
	{
		constexpr std::array<std::string_view, 4> names{"Wood", "Metal", "Urethane", "Beginner"};
		return names[static_cast<std::size_t>(material)];
	}

	[[nodiscard]] constexpr std::optional<BatMaterial> ParseBatMaterial(
	    std::string_view name) noexcept
	{
		for (const auto material : AllBatMaterials)
			if (BatMaterialName(material) == name) return material;
		return std::nullopt;
	}

	// Fraction of the sweet-spot restitution that remains far from it. Together
	// with the 2 in falloff this keeps the fastest exit inside the marked zone.
	inline constexpr double MinimumBatContactResponse = 0.55;

	// contactRatio is measured from the full handle origin (0) to the tip (1).
	[[nodiscard]] inline double BatContactRestitutionScale(
	    BatMaterial material, double contactRatio) noexcept
	{
		const auto& profile = GetBatMaterialProfile(material);
		const double centerRatio = 0.5 * (profile.sweetSpotStartRatio + profile.sweetSpotEndRatio);
		constexpr double falloffRatio = 2.0 / 34.0;
		// The beginner bat is fully lively from the end of the grip to the tip and only
		// falls off into the grip.
		const double normalizedMiss = material == BatMaterial::Beginner
		    ? (std::min)(0.0, contactRatio - profile.sweetSpotStartRatio) / falloffRatio
		    : (contactRatio - centerRatio) / falloffRatio;
		const double contactQuality = MinimumBatContactResponse + (1.0 - MinimumBatContactResponse) *
		    std::exp(-0.5 * normalizedMiss * normalizedMiss);
		return profile.restitutionMultiplier * contactQuality;
	}

	// Point along the bat (as a fraction from the handle) where the collision response is
	// evaluated. Ordinary bats respond where the ball hits; the beginner bat responds as if
	// every hit beyond the grip met a regular bat's sweet spot, so its effective mass and
	// swing speed are the same along the whole barrel. The change blends in over the upper
	// half of the grip so the response stays continuous.
	[[nodiscard]] inline double BatResponseRatio(BatMaterial material, double contactRatio) noexcept
	{
		if (material != BatMaterial::Beginner) return contactRatio;
		constexpr double sweetCenter = 28.0 / 34.0;
		constexpr double blendRatio = BatGripEndRatio - BatGripCenterRatio;
		const double u = std::clamp((contactRatio - (BatGripEndRatio - blendRatio)) / blendRatio, 0.0, 1.0);
		return contactRatio + (sweetCenter - contactRatio) * u * u * (3.0 - 2.0 * u);
	}
}
