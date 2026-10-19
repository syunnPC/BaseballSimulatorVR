#pragma once

#include "Vector3.hpp"
#include "BatGeometry.hpp"

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
		Vector3 batSurfacePosition{};
		Vector3 batSurfaceOffset{};
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

	namespace SweptBatCollisionDetail
	{
		struct ProfilePolynomial final
		{
			std::array<long double, 13> coefficients{};
			int degree{ 0 };

			[[nodiscard]] long double At(long double time) const noexcept
			{
				long double result = coefficients[static_cast<std::size_t>(degree)];
				for (int i = degree - 1; i >= 0; --i)
					result = result * time + coefficients[static_cast<std::size_t>(i)];
				return result;
			}
		};

		[[nodiscard]] inline ProfilePolynomial operator+(
		    const ProfilePolynomial& a, const ProfilePolynomial& b) noexcept
		{
			ProfilePolynomial result{};
			result.degree = (std::max)(a.degree, b.degree);
			for (int i = 0; i <= result.degree; ++i)
				result.coefficients[static_cast<std::size_t>(i)] =
				    a.coefficients[static_cast<std::size_t>(i)] + b.coefficients[static_cast<std::size_t>(i)];
			return result;
		}

		[[nodiscard]] inline ProfilePolynomial operator*(
		    const ProfilePolynomial& a, long double scale) noexcept
		{
			ProfilePolynomial result = a;
			for (auto& value : result.coefficients) value *= scale;
			return result;
		}

		[[nodiscard]] inline ProfilePolynomial operator-(
		    const ProfilePolynomial& a, const ProfilePolynomial& b) noexcept
		{
			return a + b * -1.0L;
		}

		[[nodiscard]] inline ProfilePolynomial operator*(
		    const ProfilePolynomial& a, const ProfilePolynomial& b) noexcept
		{
			ProfilePolynomial result{};
			result.degree = a.degree + b.degree;
			for (int i = 0; i <= a.degree; ++i)
				for (int j = 0; j <= b.degree; ++j)
					result.coefficients[static_cast<std::size_t>(i + j)] +=
					    a.coefficients[static_cast<std::size_t>(i)] * b.coefficients[static_cast<std::size_t>(j)];
			return result;
		}

		struct ProfileRoots final
		{
			// Near-zero critical points are retained as candidates as well as actual roots.
			std::array<double, 64> values{};
			std::size_t count{ 0 };

			void Add(double value) noexcept
			{
				for (std::size_t i = 0; i < count; ++i)
					if (std::abs(values[i] - value) < 1e-12) return;
				if (count < values.size()) values[count++] = value;
			}
		};

		[[nodiscard]] inline ProfileRoots FindProfileRoots(ProfilePolynomial polynomial) noexcept
		{
			ProfileRoots roots{};
			while (polynomial.degree > 0 &&
			    polynomial.coefficients[static_cast<std::size_t>(polynomial.degree)] == 0.0L) --polynomial.degree;
			if (polynomial.degree == 0) return roots;
			long double scale = 0.0L;
			for (const auto value : polynomial.coefficients) scale = (std::max)(scale, std::abs(value));
			if (!(scale > 0.0L) || !std::isfinite(scale)) return roots;
			polynomial = polynomial * (1.0L / scale);
			ProfilePolynomial derivative{};
			derivative.degree = polynomial.degree - 1;
			for (int i = 1; i <= polynomial.degree; ++i)
				derivative.coefficients[static_cast<std::size_t>(i - 1)] =
				    i * polynomial.coefficients[static_cast<std::size_t>(i)];
			const auto critical = FindProfileRoots(derivative);
			std::array<double, 66> boundaries{};
			std::size_t count = 1;
			for (std::size_t i = 0; i < critical.count; ++i)
				if (critical.values[i] > 0.0 && critical.values[i] < 1.0) boundaries[count++] = critical.values[i];
			boundaries[count++] = 1.0;
			std::sort(boundaries.begin(), boundaries.begin() + count);
			constexpr long double tolerance = 256.0L * std::numeric_limits<long double>::epsilon();
			for (std::size_t i = 0; i < count; ++i)
				if (std::abs(polynomial.At(boundaries[i])) <= tolerance) roots.Add(boundaries[i]);
			for (std::size_t i = 1; i < count; ++i)
			{
				long double left = boundaries[i - 1];
				long double right = boundaries[i];
				long double leftValue = polynomial.At(left);
				const long double rightValue = polynomial.At(right);
				if (leftValue == 0.0L || rightValue == 0.0L ||
				    std::signbit(leftValue) == std::signbit(rightValue)) continue;
				for (int iteration = 0; iteration < 64; ++iteration)
				{
					const long double middle = (left + right) * .5L;
					const long double value = polynomial.At(middle);
					if (value == 0.0L) { left = right = middle; break; }
					if (std::signbit(value) == std::signbit(leftValue)) { left = middle; leftValue = value; }
					else right = middle;
				}
				roots.Add(static_cast<double>((left + right) * .5L));
			}
			std::sort(roots.values.begin(), roots.values.begin() + roots.count);
			return roots;
		}

		[[nodiscard]] inline ProfilePolynomial DotPolynomial(
		    const Vector3& a, const Vector3& da, const Vector3& b, const Vector3& db) noexcept
		{
			return { { Dot(a, b), Dot(a, db) + Dot(da, b), Dot(da, db) }, 2 };
		}

		struct ProfileDistance final
		{
			double distance{};
			double fraction{};
			Vector3 surface{};
			Vector3 axisPoint{};
			Vector3 normal{};
		};

		// A surface of revolution reduces closest-point queries to its axial/radial silhouette.
		[[nodiscard]] inline ProfileDistance DistanceToProfile(
		    const Vector3& ball, const Vector3& start, const Vector3& axis, double maximumRadius) noexcept
		{
			const double length = Length(axis);
			const Vector3 direction = axis / length;
			const Vector3 offset = ball - start;
			const double axial = Dot(offset, direction);
			const Vector3 radialOffset = offset - direction * axial;
			const double radial = Length(radialOffset);
			Vector3 radialDirection = radial > 1e-12 ? radialOffset / radial : NormalizeOrZero(Cross(direction,
			    std::abs(direction.y) < .9 ? Vector3{ 0.0, 1.0, 0.0 } : Vector3{ 1.0, 0.0, 0.0 }));
			double closestSquared = std::numeric_limits<double>::infinity();
			double closestAxial = 0.0;
			double closestRadial = 0.0;
			Vector3 surfaceNormal{};
			const auto edge = [&](double az, double ar, double bz, double br)
			{
				const double dz = bz - az;
				const double dr = br - ar;
				const double parameter = std::clamp(((axial - az) * dz + (radial - ar) * dr) /
				    (dz * dz + dr * dr), 0.0, 1.0);
				const double z = az + parameter * dz;
				const double r = ar + parameter * dr;
				const double squared = (axial - z) * (axial - z) + (radial - r) * (radial - r);
				if (squared < closestSquared)
				{
					closestSquared = squared;
					closestAxial = z;
					closestRadial = r;
					surfaceNormal = NormalizeOrZero(direction * -dr + radialDirection * dz);
				}
			};
			// The knob has a bounded flat face; the tip closes on the axis.
			edge(0.0, 0.0, 0.0, BatBodyProfile.front().radiusRatio * maximumRadius);
			for (std::size_t i = 1; i < BatBodyProfile.size(); ++i)
			{
				const auto& a = BatBodyProfile[i - 1];
				const auto& b = BatBodyProfile[i];
				edge(a.fraction * length, a.radiusRatio * maximumRadius,
				    b.fraction * length, b.radiusRatio * maximumRadius);
			}
			const bool inside = axial >= 0.0 && axial <= length &&
			    radial <= BatRadiusAtFraction(axial / length, maximumRadius);
			ProfileDistance result{};
			result.axisPoint = start + direction * closestAxial;
			result.surface = result.axisPoint + radialDirection * closestRadial;
			result.distance = std::sqrt(closestSquared) * (inside ? -1.0 : 1.0);
			result.fraction = closestAxial / length;
			result.normal = std::abs(result.distance) > 1e-12 ?
			    (ball - result.surface) / result.distance : surfaceNormal;
			return result;
		}
	}

	// Each side is a conical frustum; the rings join its finite boundaries.
	// Eliminating square roots gives degree <=12 side and <=6 ring equations.
	// Exact closest-profile queries reject the extra roots introduced by squaring.
	[[nodiscard]] inline std::optional<SweptBatContact> FindSweptBatProfileContact(
	    const Vector3& previousBall, const Vector3& currentBall,
	    const BatSegment& previousBat, const BatSegment& currentBat,
	    double batRadiusMeters, double ballRadiusMeters, double frameSeconds) noexcept
	{
		using namespace SweptBatCollisionDetail;
		if (!(batRadiusMeters > 0.0) || !std::isfinite(batRadiusMeters) ||
		    !(ballRadiusMeters > 0.0) || !std::isfinite(ballRadiusMeters) ||
		    !(frameSeconds > 0.0) || !std::isfinite(frameSeconds)) return std::nullopt;
		const auto finite = [](const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		};
		if (!finite(previousBall) || !finite(currentBall) || !finite(previousBat.start) ||
		    !finite(previousBat.end) || !finite(currentBat.start) || !finite(currentBat.end)) return std::nullopt;
		const Vector3 startDelta = currentBat.start - previousBat.start;
		const Vector3 axis = previousBat.end - previousBat.start;
		if (!(LengthSquared(axis) > 1e-10) ||
		    !(LengthSquared(currentBat.end - currentBat.start) > 1e-10)) return std::nullopt;
		if (!FindSweptBatContact(previousBall, currentBall, previousBat, currentBat,
		    batRadiusMeters + ballRadiusMeters, frameSeconds) &&
		    DistanceToProfile(previousBall, previousBat.start, axis, batRadiusMeters).distance > ballRadiusMeters)
			return std::nullopt;
		const Vector3 axisDelta = (currentBat.end - previousBat.end) - startDelta;
		const Vector3 offset = previousBall - previousBat.start;
		const Vector3 relativeDelta = (currentBall - previousBall) - startDelta;
		const auto s = DotPolynomial(axis, axisDelta, axis, axisDelta);
		const auto q = DotPolynomial(offset, relativeDelta, axis, axisDelta);
		const Vector3 c0 = Cross(offset, axis);
		const Vector3 c1 = Cross(offset, axisDelta) + Cross(relativeDelta, axis);
		const Vector3 c2 = Cross(relativeDelta, axisDelta);
		const ProfilePolynomial crossSquared{ { LengthSquared(c0), 2.0 * Dot(c0, c1),
		    LengthSquared(c1) + 2.0 * Dot(c0, c2), 2.0 * Dot(c1, c2), LengthSquared(c2) }, 4 };
		const double ballSquared = ballRadiusMeters * ballRadiusMeters;
		std::optional<SweptBatContact> earliest{};
		const auto sample = [&](double time)
		{
			return DistanceToProfile(offset + relativeDelta * time, {}, axis + axisDelta * time, batRadiusMeters);
		};
		const auto consider = [&](double time, bool initial = false)
		{
			if (!(s.At(time) > 1e-10L)) return;
			// Polish squared-polynomial roots using the signed distance to the actual surface.
			for (int iteration = 0; !initial && iteration < 32; ++iteration)
			{
				const auto distance = sample(time);
				const Vector3 currentAxis = axis + axisDelta * time;
				const Vector3 omega = Cross(currentAxis, axisDelta) / LengthSquared(currentAxis);
				const Vector3 surfaceDelta = axisDelta * distance.fraction +
				    Cross(omega, distance.surface - distance.axisPoint);
				const double derivative = Dot(distance.normal, relativeDelta - surfaceDelta);
				if (std::abs(derivative) < 1e-12) break;
				const double step = (distance.distance - ballRadiusMeters) / derivative;
				if (std::abs(step) < 1e-12) break;
				const double next = time - step;
				if (!(next >= 0.0 && next <= 1.0) || !(s.At(next) > 1e-10L) || std::abs(step) > .05) break;
				time = next;
			}
			const auto distance = sample(time);
			constexpr double tolerance = 1e-8;
			if (!std::isfinite(distance.distance) || distance.distance > ballRadiusMeters + tolerance ||
			    (!initial && distance.distance < ballRadiusMeters - tolerance)) return;
			if (earliest && time >= earliest->frameFraction) return;
			const Vector3 currentAxis = axis + axisDelta * time;
			const Vector3 start = previousBat.start + startDelta * time;
			earliest = SweptBatContact{ time, distance.fraction,
			    previousBall + (currentBall - previousBall) * time, start + distance.axisPoint,
			    distance.normal, NormalizeOrZero(currentAxis),
			    (startDelta + axisDelta * distance.fraction) / frameSeconds,
			    Cross(currentAxis, axisDelta) / (LengthSquared(currentAxis) * frameSeconds),
			    start + distance.surface, distance.surface - distance.axisPoint };
		};
		consider(0.0, true);
		if (earliest) return earliest;
		const auto solve = [&](const ProfilePolynomial& polynomial)
		{
			const auto roots = FindProfileRoots(polynomial);
			for (std::size_t i = 0; i < roots.count; ++i) consider(roots.values[i]);
		};
		// The flat knob face: axial distance to the plane equals the ball radius.
		solve(q * q - s * ballSquared);
		for (std::size_t i = 0; i < BatBodyProfile.size(); ++i)
		{
			const auto& node = BatBodyProfile[i];
			const double radius = node.radiusRatio * batRadiusMeters;
			const Vector3 ringOffset = offset - axis * node.fraction;
			const Vector3 ringDelta = relativeDelta - axisDelta * node.fraction;
			// A ring lies inside its bounding sphere, so this quadratic culls remote rings.
			const auto ringLength = DotPolynomial(ringOffset, ringDelta, ringOffset, ringDelta);
			const double ringBound = radius + ballRadiusMeters;
			const double a = LengthSquared(ringDelta);
			const double nearestTime = a > 0.0 ? std::clamp(-Dot(ringOffset, ringDelta) / a, 0.0, 1.0) : 0.0;
			if (LengthSquared(ringOffset + ringDelta * nearestTime) <= ringBound * ringBound + 1e-12)
			{
				auto ringEquation = ringLength;
				ringEquation.coefficients[0] += radius * radius - ballSquared;
				solve(s * ringEquation * ringEquation - crossSquared * (4.0 * radius * radius));
			}
			if (i == 0) continue;
			const auto& previous = BatBodyProfile[i - 1];
			const double f0 = previous.fraction;
			const double df = node.fraction - f0;
			const double r0 = previous.radiusRatio * batRadiusMeters;
			const double dr = radius - r0;
			const BatSegment oldPart{ previousBat.start + axis * f0,
			    previousBat.start + axis * node.fraction };
			const Vector3 newAxis = axis + axisDelta;
			const BatSegment newPart{ currentBat.start + newAxis * f0,
			    currentBat.start + newAxis * node.fraction };
			const auto bound = FindSweptBatContact(previousBall, currentBall, oldPart, newPart,
			    (std::max)(r0, radius) + ballRadiusMeters, frameSeconds);
			if (!bound || (earliest && bound->frameFraction > earliest->frameFraction)) continue;
			if (dr == 0.0)
			{
				solve(crossSquared - s * ((radius + ballRadiusMeters) * (radius + ballRadiusMeters)));
				continue;
			}
			const auto x = (q - s * f0) * dr + s * (df * r0);
			const auto sk = s * crossSquared;
			const auto p = x * x + sk * (df * df) -
			    (s * s * (df * df) + s * (dr * dr)) * ballSquared;
			solve(p * p - x * x * sk * (4.0 * df * df));
		}
		consider(1.0);
		return earliest;
	}
}
