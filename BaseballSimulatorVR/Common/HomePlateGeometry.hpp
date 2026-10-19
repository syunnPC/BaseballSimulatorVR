#pragma once

#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace BaseballSimulatorVR::Common
{
	// A 17-inch square with its two rear corners removed.
	// https://www.mlb.com/glossary/rules/field-dimensions
	inline constexpr double HomePlateWidthMeters = 0.4318;
	inline constexpr double HomePlateDepthMeters = 0.4318;

	[[nodiscard]] inline std::array<Physics::Vector3, 5> GetHomePlateVertices(
	    double frontDepth, double height = 0.0) noexcept
	{
		constexpr double halfWidth = HomePlateWidthMeters * 0.5;
		constexpr double shoulderDepth = HomePlateDepthMeters * 0.5;
		return {{{-halfWidth, height, frontDepth}, {halfWidth, height, frontDepth},
		    {halfWidth, height, frontDepth + shoulderDepth},
		    {0.0, height, frontDepth + HomePlateDepthMeters},
		    {-halfWidth, height, frontDepth + shoulderDepth}}};
	}

	namespace HomePlateGeometryDetail
	{
		[[nodiscard]] inline Physics::Vector3 ClosestPointOnPrism(
		    const Physics::Vector3& point, double bottom, double top) noexcept
		{
			const auto vertices = GetHomePlateVertices(0.0);
			bool inside = true;
			double closestDistanceSquared = (std::numeric_limits<double>::infinity)();
			Physics::Vector3 closest{};
			for (std::size_t i = 0; i < vertices.size(); ++i)
			{
				const auto& a = vertices[i];
				const auto edge = vertices[(i + 1) % vertices.size()] - a;
				const double dx = point.x - a.x, dz = point.z - a.z;
				inside = inside && edge.x * dz - edge.z * dx >= 0.0;
				const double blend = std::clamp(
				    (dx * edge.x + dz * edge.z) / Physics::LengthSquared(edge), 0.0, 1.0);
				const auto candidate = a + edge * blend;
				const double ex = point.x - candidate.x, ez = point.z - candidate.z;
				const double distanceSquared = ex * ex + ez * ez;
				if (distanceSquared < closestDistanceSquared)
				{
					closestDistanceSquared = distanceSquared;
					closest = candidate;
				}
			}
			if (inside)
			{
				closest.x = point.x;
				closest.z = point.z;
			}
			closest.y = std::clamp(point.y, bottom, top);
			return closest;
		}
	}

	// Positions use simulation x/z and height above the plate's ground plane.
	// Distance to this convex prism includes rounded sphere contacts at every edge and vertex.
	[[nodiscard]] inline bool SweptBallTouchesStrikeZone(Physics::Vector3 from,
	    Physics::Vector3 to, double ballRadius, double frontDepth, double bottom, double top) noexcept
	{
		const auto finite = [](const Physics::Vector3& point)
		{
			return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
		};
		if (!finite(from) || !finite(to) || !std::isfinite(ballRadius) || ballRadius < 0.0 ||
		    !std::isfinite(frontDepth) || !std::isfinite(bottom) || !std::isfinite(top) || top < bottom)
		{
			return false;
		}
		from.z -= frontDepth;
		to.z -= frontDepth;
		const double scale = (std::max)({1.0, std::abs(frontDepth), std::abs(from.x),
		    std::abs(from.y), std::abs(from.z), std::abs(to.x), std::abs(to.y), std::abs(to.z),
		    std::abs(bottom), std::abs(top), ballRadius});
		const double roundoff = 32.0 * std::numeric_limits<double>::epsilon() * scale;
		const double extent = ballRadius + roundoff;
		constexpr double halfWidth = HomePlateWidthMeters * 0.5;
		if ((std::max)(from.x, to.x) < -halfWidth - extent ||
		    (std::min)(from.x, to.x) > halfWidth + extent ||
		    (std::max)(from.y, to.y) < bottom - extent ||
		    (std::min)(from.y, to.y) > top + extent ||
		    (std::max)(from.z, to.z) < -extent ||
		    (std::min)(from.z, to.z) > HomePlateDepthMeters + extent)
		{
			return false;
		}
		const auto direction = to - from;
		const auto displacement = [&](double blend)
		{
			const auto point = from + direction * blend;
			return point - HomePlateGeometryDetail::ClosestPointOnPrism(point, bottom, top);
		};
		const auto firstDisplacement = displacement(0.0);
		const auto lastDisplacement = displacement(1.0);
		double minimumDistanceSquared = (std::min)(
		    Physics::LengthSquared(firstDisplacement), Physics::LengthSquared(lastDisplacement));
		const double contactDistanceSquared = extent * extent;
		if (minimumDistanceSquared <= contactDistanceSquared)
		{
			return true;
		}
		if (Physics::Dot(firstDisplacement, direction) < 0.0 &&
		    Physics::Dot(lastDisplacement, direction) > 0.0)
		{
			// Squared distance to a convex set is convex and differentiable. Its derivative
			// is 2 * dot(point - closestPoint, direction), so bisection finds the global minimum.
			double low = 0.0, high = 1.0;
			for (int iteration = 0; iteration < 80; ++iteration)
			{
				const double middle = low + (high - low) * 0.5;
				const auto delta = displacement(middle);
				minimumDistanceSquared = (std::min)(minimumDistanceSquared, Physics::LengthSquared(delta));
				if (minimumDistanceSquared <= contactDistanceSquared)
				{
					return true;
				}
				const double derivative = Physics::Dot(delta, direction);
				if (derivative == 0.0)
				{
					break;
				}
				if (derivative < 0.0)
				{
					low = middle;
				}
				else
				{
					high = middle;
				}
			}
		}
		// Permit only floating-point roundoff at tangency, in metres rather than squared metres.
		return std::sqrt(minimumDistanceSquared) <= extent;
	}
} // namespace BaseballSimulatorVR::Common
