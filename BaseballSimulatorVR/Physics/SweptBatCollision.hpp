#pragma once

#include "Vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace BaseballSimulatorVR::Physics
{
	struct BatSegment final
	{
		Vector3 start{};
		Vector3 end{};
	};

	struct SweptBatContact final
	{
		double frameFraction{ 0.0 };
		double batFraction{ 0.0 };
		Vector3 ballPosition{};
		Vector3 batPosition{};
		Vector3 normal{};
		Vector3 batAxis{};
		Vector3 batContactVelocity{};
		Vector3 batAngularVelocity{};
	};

	namespace SweptBatCollisionDetail
	{
		struct Roots final
		{
			std::array<double, 4> values{};
			std::size_t count{ 0 };

			void Add(double value) noexcept
			{
				for (std::size_t i = 0; i < count; ++i)
					if (std::abs(values[i] - value) < 1e-12) return;
				if (count == values.size()) return;
				values[count++] = value;
				std::sort(values.begin(), values.begin() + count);
			}
		};

		[[nodiscard]] inline double Evaluate(
		    const std::array<double, 5>& coefficients, int degree, double time) noexcept
		{
			double value = coefficients[static_cast<std::size_t>(degree)];
			for (int i = degree - 1; i >= 0; --i)
				value = value * time + coefficients[static_cast<std::size_t>(i)];
			return value;
		}

		// Derivative roots partition a polynomial into monotonic intervals.
		// Checking those boundaries also finds even-multiplicity tangent roots;
		// sign-change-only root searches would miss them.
		[[nodiscard]] inline Roots FindRoots(
		    std::array<double, 5> coefficients, int degree) noexcept
		{
			Roots roots{};
			while (degree > 0 && coefficients[static_cast<std::size_t>(degree)] == 0.0) --degree;
			if (degree == 0) return roots;
			double scale = 0.0;
			for (int i = 0; i <= degree; ++i)
				scale = (std::max)(scale, std::abs(coefficients[static_cast<std::size_t>(i)]));
			if (!std::isfinite(scale) || scale == 0.0) return roots;
			for (double& value : coefficients) value /= scale;
			std::array<double, 5> derivative{};
			for (int i = 1; i <= degree; ++i)
				derivative[static_cast<std::size_t>(i - 1)] = i * coefficients[static_cast<std::size_t>(i)];
			const Roots criticalPoints = FindRoots(derivative, degree - 1);
			std::array<double, 6> boundaries{};
			std::size_t boundaryCount = 1;
			for (std::size_t i = 0; i < criticalPoints.count; ++i)
				if (criticalPoints.values[i] > 0.0 && criticalPoints.values[i] < 1.0)
					boundaries[boundaryCount++] = criticalPoints.values[i];
			boundaries[boundaryCount++] = 1.0;
			constexpr double rootTolerance = 64.0 * std::numeric_limits<double>::epsilon();
			for (std::size_t i = 0; i < boundaryCount; ++i)
				if (std::abs(Evaluate(coefficients, degree, boundaries[i])) <= rootTolerance)
					roots.Add(boundaries[i]);
			for (std::size_t i = 1; i < boundaryCount; ++i)
			{
				double left = boundaries[i - 1];
				double right = boundaries[i];
				double leftValue = Evaluate(coefficients, degree, left);
				const double rightValue = Evaluate(coefficients, degree, right);
				if (leftValue == 0.0 || rightValue == 0.0 ||
				    std::signbit(leftValue) == std::signbit(rightValue)) continue;
				for (int iteration = 0; iteration < 64; ++iteration)
				{
					const double middle = (left + right) * 0.5;
					const double middleValue = Evaluate(coefficients, degree, middle);
					if (middleValue == 0.0) { left = right = middle; break; }
					if (std::signbit(middleValue) == std::signbit(leftValue))
					{
						left = middle;
						leftValue = middleValue;
					}
					else right = middle;
				}
				roots.Add((left + right) * 0.5);
			}
			return roots;
		}
	}

	// A ball and both bat endpoints interpolate over the SAME frame fraction.
	// Capsule end contacts solve quadratics; interior contacts solve the quartic
	// |(ball-start) x (end-start)|^2 - radius^2 |end-start|^2 = 0.
	// Linear endpoint interpolation approximates a rigid swing over one XR frame.
	[[nodiscard]] inline std::optional<SweptBatContact> FindSweptBatContact(
	    const Vector3& previousBall, const Vector3& currentBall,
	    const BatSegment& previousBat, const BatSegment& currentBat,
	    double combinedRadiusMeters, double frameSeconds) noexcept
	{
		if (!(combinedRadiusMeters > 0.0) || !std::isfinite(combinedRadiusMeters) ||
		    !(frameSeconds > 0.0) || !std::isfinite(frameSeconds)) return std::nullopt;
		const auto finite = [](const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		};
		if (!finite(previousBall) || !finite(currentBall) || !finite(previousBat.start) ||
		    !finite(previousBat.end) || !finite(currentBat.start) || !finite(currentBat.end)) return std::nullopt;
		const Vector3 ballDelta = currentBall - previousBall;
		const Vector3 startDelta = currentBat.start - previousBat.start;
		const Vector3 endDelta = currentBat.end - previousBat.end;
		constexpr double contactToleranceMeters = 1e-6;
		std::optional<SweptBatContact> earliestContact{};
		const auto considerTime = [&](double time)
		{
			const Vector3 start = previousBat.start + startDelta * time;
			const Vector3 axis = previousBat.end + endDelta * time - start;
			const double axisLengthSquared = LengthSquared(axis);
			if (!std::isfinite(axisLengthSquared) || axisLengthSquared < 1e-10) return;
			const Vector3 ball = previousBall + ballDelta * time;
			const double batFraction = std::clamp(Dot(ball - start, axis) / axisLengthSquared, 0.0, 1.0);
			const Vector3 point = start + axis * batFraction;
			const Vector3 separation = ball - point;
			const double distance = Length(separation);
			if (!std::isfinite(distance)) return;
			if (distance <= combinedRadiusMeters + contactToleranceMeters)
			{
				const Vector3 contactVelocity = (startDelta + (endDelta - startDelta) * batFraction) / frameSeconds;
				Vector3 normal = NormalizeOrZero(separation);
				// An exactly concentric start has no geometrical surface normal.
				if (LengthSquared(normal) < 0.5)
				{
					normal = NormalizeOrZero(contactVelocity - ballDelta / frameSeconds);
					if (LengthSquared(normal) < 0.5) return;
				}
				if (!finite(contactVelocity)) return;
				const Vector3 angularVelocity = Cross(axis, endDelta - startDelta) /
				    (axisLengthSquared * frameSeconds);
				if (!finite(angularVelocity)) return;
				if (!earliestContact || time < earliestContact->frameFraction)
					earliestContact = SweptBatContact{ time, batFraction, ball, point, normal,
					    NormalizeOrZero(axis), contactVelocity, angularVelocity };
			}
		};
		considerTime(0.0);
		if (earliestContact) return earliestContact;
		const double radiusSquared = combinedRadiusMeters * combinedRadiusMeters;
		const auto considerRoots = [&](const std::array<double, 5>& coefficients, int degree)
		{
			const auto roots = SweptBatCollisionDetail::FindRoots(coefficients, degree);
			for (std::size_t i = 0; i < roots.count; ++i) considerTime(roots.values[i]);
		};
		const auto considerEnd = [&](const Vector3& endpoint, const Vector3& endpointDelta)
		{
			const Vector3 offset = previousBall - endpoint;
			const Vector3 relativeDelta = ballDelta - endpointDelta;
			considerRoots({ LengthSquared(offset) - radiusSquared,
			    2.0 * Dot(offset, relativeDelta), LengthSquared(relativeDelta), 0.0, 0.0 }, 2);
		};
		considerEnd(previousBat.start, startDelta);
		considerEnd(previousBat.end, endDelta);
		const Vector3 offset = previousBall - previousBat.start;
		const Vector3 relativeDelta = ballDelta - startDelta;
		const Vector3 axis = previousBat.end - previousBat.start;
		const Vector3 axisDelta = endDelta - startDelta;
		const Vector3 cross0 = Cross(offset, axis);
		const Vector3 cross1 = Cross(offset, axisDelta) + Cross(relativeDelta, axis);
		const Vector3 cross2 = Cross(relativeDelta, axisDelta);
		considerRoots({ LengthSquared(cross0) - radiusSquared * LengthSquared(axis),
		    2.0 * Dot(cross0, cross1) - 2.0 * radiusSquared * Dot(axis, axisDelta),
		    LengthSquared(cross1) + 2.0 * Dot(cross0, cross2) - radiusSquared * LengthSquared(axisDelta),
		    2.0 * Dot(cross1, cross2), LengthSquared(cross2) }, 4);
		return earliestContact;
	}
}
