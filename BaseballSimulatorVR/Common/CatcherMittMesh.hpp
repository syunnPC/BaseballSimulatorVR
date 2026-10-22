#pragma once

#include "CatcherMittState.hpp"
#include "DebugWorldProjection.hpp"

#include <array>
#include <numbers>
#include <optional>

namespace BaseballSimulatorVR::Common
{
	namespace CatcherMittStyle
	{
		inline constexpr std::array<float, 4> PocketColor{.30F, .16F, .06F, 1.0F};
		inline constexpr std::array<float, 4> LeatherColor{.52F, .29F, .11F, 1.0F};
		inline constexpr std::array<float, 4> BackColor{.40F, .22F, .08F, 1.0F};
		inline constexpr std::array<float, 4> LaceColor{.17F, .09F, .04F, 1.0F};
	}

	// A rounded catcher's mitt: a padded oval with a recessed pocket on the palm side and
	// laces around the rim. Squeezing folds the thumb and finger sides in over the pocket.
	template <typename TTriangle>
	void EmitCatcherMittWorldMesh(const CatcherMittState& mitt, TTriangle&& triangle)
	{
		using Physics::Vector3;
		const CatcherMittFrame frame = BuildCatcherMittFrame(mitt);
		const double closed = std::clamp(mitt.closedAmount, 0.0, 1.0);
		constexpr int Segments = 24;
		constexpr int Rings = 5;
		constexpr double HalfWidth = .15, HalfHeight = .16;
		// The pocket sits a little below the mitt's middle, toward the heel.
		const Vector3 center = frame.pocket + frame.along * .03;
		const auto point = [&](const double x, const double y, const double depth)
		{
			const double fold = closed * .11 * (x / HalfWidth) * (x / HalfWidth);
			const double squeeze = 1.0 - .35 * closed;
			return center + frame.across * (x * squeeze) + frame.along * y + frame.palm * (depth + fold);
		};
		const auto outline = [&](const int segment, const double scale, double& x, double& y)
		{
			const double angle = 2.0 * std::numbers::pi * segment / Segments;
			// A rounded-square oval (superellipse), slightly taller than it is wide.
			const double c = std::cos(angle), s = std::sin(angle);
			x = HalfWidth * scale * std::copysign(std::pow(std::abs(c), .8), c);
			y = HalfHeight * scale * std::copysign(std::pow(std::abs(s), .8), s);
		};
		const auto front = [&](const int segment, const int ring)
		{
			double x = 0.0, y = 0.0;
			const double r = static_cast<double>(ring) / Rings;
			outline(segment, r, x, y);
			// Recessed toward the middle of the pocket, raised padding at the rim.
			return point(x, y, -.035 * (1.0 - r * r) + .012 * r * r * r);
		};
		const auto back = [&](const int segment, const int ring)
		{
			double x = 0.0, y = 0.0;
			const double r = static_cast<double>(ring) / Rings;
			outline(segment, r, x, y);
			return point(x, y, -.065 + .02 * r * r);
		};
		for (int segment = 0; segment < Segments; ++segment)
		{
			const int next = (segment + 1) % Segments;
			for (int ring = 0; ring < Rings; ++ring)
			{
				const auto& pocket = ring < 3 ? CatcherMittStyle::PocketColor : CatcherMittStyle::LeatherColor;
				triangle(front(segment, ring), front(next, ring + 1), front(segment, ring + 1), pocket);
				triangle(front(segment, ring), front(next, ring), front(next, ring + 1), pocket);
				triangle(back(segment, ring), back(segment, ring + 1), back(next, ring + 1), CatcherMittStyle::BackColor);
				triangle(back(segment, ring), back(next, ring + 1), back(next, ring), CatcherMittStyle::BackColor);
			}
			// The rolled rim between front and back, laced every other segment.
			const auto& rim = segment % 2 == 0 ? CatcherMittStyle::LaceColor : CatcherMittStyle::LeatherColor;
			triangle(front(segment, Rings), front(next, Rings), back(next, Rings), rim);
			triangle(front(segment, Rings), back(next, Rings), back(segment, Rings), rim);
		}
	}

	template <typename TDrawTriangleFn>
	void EmitProjectedCatcherMitt(const std::optional<CatcherMittState>& mitt, const ViewProjectionState& view,
	    const LONG width, const LONG height, TDrawTriangleFn&& drawTriangle)
	{
		if (!mitt || !view.isValid || width <= 0 || height <= 0) return;
		const auto light = Physics::NormalizeOrZero(Physics::Vector3{-0.34, 0.78, -0.52});
		EmitCatcherMittWorldMesh(*mitt,
		    [&](const Physics::Vector3& a, const Physics::Vector3& b, const Physics::Vector3& c,
		        const std::array<float, 4>& color)
		    {
			    const auto normal = Physics::NormalizeOrZero(Physics::Cross(b - a, c - a));
			    const auto toEye = Physics::NormalizeOrZero(view.position - (a + b + c) / 3.0);
			    // Leather looks the same from either side of a fold; light it by its facing.
			    const double diffuse = std::abs(Physics::Dot(normal, light));
			    const double facing = std::abs(Physics::Dot(normal, toEye));
			    const auto scale = static_cast<float>(.35 + .45 * diffuse + .20 * facing);
			    const std::array<float, 4> shaded{color[0] * scale, color[1] * scale, color[2] * scale, color[3]};
			    EmitProjectedWorldTriangle(a, b, c, view, width, height, shaded.data(), drawTriangle);
		    });
	}
}
