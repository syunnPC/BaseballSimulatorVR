#include "BatBallCollision.hpp"

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Physics
{
	namespace
	{
		// Nathan (2000), Table I: R161 inertia I_cm = M * radiusOfGyration^2.
		constexpr double ReferenceBatLengthMeters = 0.84;
		constexpr double BatMassKilograms = 0.885;
		constexpr double ReferenceCenterOfMassMeters = 0.564;
		constexpr double ReferenceRadiusOfGyrationMeters = 0.23;
		// 2017 wood bat: Iz / (M R^2), converted from Table II (9.1 oz in^2,
		// 29.41 oz, 1.16 in). Scale to this model's mass and configured radius.
		constexpr double BatAxialInertiaFactor = 9.1 / (29.41 * 1.16 * 1.16);
		constexpr double MinimumMeasuredImpactSpeedMetersPerSecond = 26.8224;
		constexpr double MilesPerHourToMetersPerSecond = 0.44704;

		[[nodiscard]] bool IsFinite(const Vector3& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y) &&
			       std::isfinite(value.z);
		}
	}

	BatBallCollisionResult ResolveBatBallCollision(
	    const BatBallCollisionInput& input) noexcept
	{
		BatBallCollisionResult result{};
		result.ballVelocityWorld = input.ballVelocityWorld;
		result.ballAngularVelocityWorld = input.ballAngularVelocityWorld;
		if (!IsFinite(input.ballVelocityWorld) ||
		    !IsFinite(input.ballAngularVelocityWorld) ||
		    !IsFinite(input.batContactVelocityWorld) ||
		    !IsFinite(input.collisionNormal) ||
		    !IsFinite(input.batAxisDirectionWorld) ||
		    !std::isfinite(input.batLengthMeters) || input.batLengthMeters <= 0.0 ||
		    !std::isfinite(input.batRadiusMeters) || input.batRadiusMeters <= 0.0 ||
		    input.batRadiusMeters > input.batLengthMeters ||
		    !std::isfinite(input.contactDistanceFromHandleMeters) ||
		    input.contactDistanceFromHandleMeters < 0.0 ||
		    input.contactDistanceFromHandleMeters > input.batLengthMeters ||
		    !std::isfinite(input.restitutionAtReferenceSpeed) ||
		    input.restitutionAtReferenceSpeed < 0.0 ||
		    input.restitutionAtReferenceSpeed > 1.0)
		{
			return result;
		}

		const Vector3 normal = NormalizeOrZero(input.collisionNormal);
		const Vector3 batAxis = NormalizeOrZero(input.batAxisDirectionWorld);
		if (LengthSquared(normal) < 0.5 || LengthSquared(batAxis) < 0.5)
		{
			return result;
		}
		const double closingSpeed = -Dot(
		    input.ballVelocityWorld - input.batContactVelocityWorld, normal);
		if (!std::isfinite(closingSpeed) || closingSpeed <= 0.0)
		{
			return result;
		}

		const double lengthScale = input.batLengthMeters / ReferenceBatLengthMeters;
		const double centerOfMass = ReferenceCenterOfMassMeters * lengthScale;
		const double radiusOfGyration = ReferenceRadiusOfGyrationMeters * lengthScale;
		const double inertia = BatMassKilograms * radiusOfGyration * radiusOfGyration;
		const Vector3 leverArm = batAxis *
		    (input.contactDistanceFromHandleMeters - centerOfMass);
		const double inverseEffectiveBatMass =
		    (1.0 / BatMassKilograms) + LengthSquared(Cross(leverArm, normal)) / inertia;
		if (!std::isfinite(inverseEffectiveBatMass) || inverseEffectiveBatMass <= 0.0)
		{
			return result;
		}

		// Nathan (2003), Sec. II B/C: COR falls approximately 0.55 -> 0.45
		// between 60 and 160 mph. Hold endpoints outside that measured interval.
		const double measuredSpeed = std::clamp(closingSpeed,
		    MinimumMeasuredImpactSpeedMetersPerSecond,
		    BatBallReferenceImpactSpeedMetersPerSecond);
		const double standardRestitution = 0.45 +
		    0.001 * (BatBallReferenceImpactSpeedMetersPerSecond - measuredSpeed) /
		    MilesPerHourToMetersPerSecond;
		result.restitution = std::clamp(input.restitutionAtReferenceSpeed *
		    (standardRestitution / DefaultBatBallRestitution), 0.0, 1.0);
		result.effectiveBatMassKilograms = 1.0 / inverseEffectiveBatMass;
		const double recoilFactor = BaseballMassKilograms * inverseEffectiveBatMass;
		result.collisionEfficiency = (result.restitution - recoilFactor) /
		    (1.0 + recoilFactor);
		const double ballInertia = BaseballInertiaFactor * BaseballMassKilograms *
		    BaseballRadiusMeters * BaseballRadiusMeters;
		const Vector3 ballSurfaceLever = normal * -BaseballRadiusMeters;
		const Vector3 batSurfaceLever = leverArm + normal * input.batRadiusMeters;
		const double axialInertia = BatAxialInertiaFactor * BatMassKilograms *
		    input.batRadiusMeters * input.batRadiusMeters;
		const auto inverseBatInertia = [&](const Vector3& angularImpulse)
		{
			const Vector3 axial = batAxis * Dot(angularImpulse, batAxis);
			return (angularImpulse - axial) / inertia + axial / axialInertia;
		};
		const auto contactMobility = [&](const Vector3& impulse)
		{
			return impulse * (1.0 / BaseballMassKilograms + 1.0 / BatMassKilograms) +
			    Cross(Cross(ballSurfaceLever, impulse) / ballInertia, ballSurfaceLever) +
			    Cross(inverseBatInertia(Cross(batSurfaceLever, impulse)), batSurfaceLever);
		};
		// K is the complete 3D mobility at the two surfaces, including cross terms
		// between a normal impulse and motion parallel to the bat's long axis.
		const Vector3 seed = std::abs(normal.x) < 0.8 ? Vector3{ 1.0, 0.0, 0.0 }
		    : Vector3{ 0.0, 1.0, 0.0 };
		const Vector3 tangent = NormalizeOrZero(Cross(normal, seed));
		const Vector3 secondTangent = Cross(normal, tangent);
		const Vector3 normalColumn = contactMobility(normal);
		const Vector3 firstColumn = contactMobility(tangent);
		const Vector3 secondColumn = contactMobility(secondTangent);
		const double kn = Dot(normal, normalColumn);
		const double k1 = Dot(tangent, normalColumn);
		const double k2 = Dot(secondTangent, normalColumn);
		const double c1 = k1 / kn;
		const double c2 = k2 / kn;
		const double baseNormalImpulse = (1.0 + result.restitution) * closingSpeed / kn;
		// Eliminate Jn using normal COR, retaining normal/tangent coupling.
		const double a = Dot(tangent, firstColumn) - k1 * c1;
		const double b = Dot(tangent, secondColumn) - k1 * c2;
		const double d = Dot(secondTangent, secondColumn) - k2 * c2;
		const Vector3 surfaceRelativeVelocity = input.ballVelocityWorld +
		    Cross(input.ballAngularVelocityWorld, ballSurfaceLever) - input.batContactVelocityWorld;
		const double u1 = Dot(surfaceRelativeVelocity, tangent);
		const double u2 = Dot(surfaceRelativeVelocity, secondTangent);
		const double f1 = (1.0 + WoodBatTangentialRestitution) * u1 + k1 * baseNormalImpulse;
		const double f2 = (1.0 + WoodBatTangentialRestitution) * u2 + k2 * baseNormalImpulse;
		const double mu = BatBallFrictionCoefficient;
		const double muSquared = mu * mu;
		// With radius <= length, the physical inertia model gives |mu*c| < 1.
		// The section of the Coulomb cone in this normal-COR plane is an ellipse.
		const double qa = 1.0 - muSquared * c1 * c1;
		const double qb = -muSquared * c1 * c2;
		const double qd = 1.0 - muSquared * c2 * c2;
		if (!(kn > 0.0) || !(a > 0.0) || !(a * d - b * b > 0.0) ||
		    !(qa > 0.0) || !(qa * qd - qb * qb > 0.0) ||
		    !std::isfinite(a * d - b * b)) return result;
		struct TangentImpulse final { double first; double second; };
		const auto solveWithMultiplier = [&](double multiplier)
		{
			const double aa = a + multiplier * qa;
			const double bb = b + multiplier * qb;
			const double dd = d + multiplier * qd;
			const double g1 = f1 + multiplier * muSquared * baseNormalImpulse * c1;
			const double g2 = f2 + multiplier * muSquared * baseNormalImpulse * c2;
			const double determinant = aa * dd - bb * bb;
			return TangentImpulse{ (-g1 * dd + bb * g2) / determinant,
			    (-aa * g2 + bb * g1) / determinant };
		};
		const auto normalImpulseFor = [&](const TangentImpulse& impulse)
		{
			return baseNormalImpulse - c1 * impulse.first - c2 * impulse.second;
		};
		const auto isInsideFrictionCone = [&](const TangentImpulse& impulse)
		{
			const double jn = normalImpulseFor(impulse);
			return jn >= 0.0 && std::hypot(impulse.first, impulse.second) <= mu * jn;
		};
		TangentImpulse jt = solveWithMultiplier(0.0);
		if (!isInsideFrictionCone(jt))
		{
			result.tangentialRestitutionLimitedByFriction = true;
			// Convex quadratic minimization with a Coulomb-cone constraint. A
			// single nonnegative Lagrange multiplier finds the unique boundary.
			double upper = (std::max)(a, d);
			for (int i = 0; i < 64 && !isInsideFrictionCone(solveWithMultiplier(upper)); ++i)
				upper *= 2.0;
			if (!std::isfinite(upper) || !isInsideFrictionCone(solveWithMultiplier(upper))) return result;
			double lower = 0.0;
			for (int i = 0; i < 64; ++i)
			{
				const double middle = lower + (upper - lower) * 0.5;
				if (isInsideFrictionCone(solveWithMultiplier(middle))) upper = middle;
				else lower = middle;
			}
			jt = solveWithMultiplier(upper);
		}
		// Limit the coupled impulse by the kinetic-energy quadratic while
		// preserving normal COR and the friction cone.
		const double baseEnergy = -0.5 * (1.0 - result.restitution * result.restitution) *
		    closingSpeed * closingSpeed / kn;
		const double h1 = u1 + c1 * closingSpeed;
		const double h2 = u2 + c2 * closingSpeed;
		const double linearEnergy = h1 * jt.first + h2 * jt.second;
		const double quadraticEnergy = a * jt.first * jt.first +
		    2.0 * b * jt.first * jt.second + d * jt.second * jt.second;
		if (baseEnergy + linearEnergy + 0.5 * quadraticEnergy > 0.0 && quadraticEnergy > 0.0)
		{
			result.tangentialRestitutionLimitedByEnergy = true;
			const double discriminant = (std::max)(0.0,
			    linearEnergy * linearEnergy - 2.0 * quadraticEnergy * baseEnergy);
			const double root = std::sqrt(discriminant);
			const double fraction = linearEnergy >= 0.0
			    ? (root + linearEnergy > 0.0 ? -2.0 * baseEnergy / (root + linearEnergy) : 0.0)
			    : (root - linearEnergy) / quadraticEnergy;
			jt.first *= std::clamp(fraction, 0.0, 1.0);
			jt.second *= std::clamp(fraction, 0.0, 1.0);
		}
		result.normalImpulseNewtonSeconds = normalImpulseFor(jt);
		result.tangentialImpulseWorld = tangent * jt.first + secondTangent * jt.second;
		const Vector3 totalImpulse = normal * result.normalImpulseNewtonSeconds + result.tangentialImpulseWorld;
		const Vector3 nextVelocity = input.ballVelocityWorld + totalImpulse / BaseballMassKilograms;
		const Vector3 nextAngularVelocity = input.ballAngularVelocityWorld +
		    Cross(ballSurfaceLever, totalImpulse) / ballInertia;
		if (!IsFinite(nextVelocity) || !IsFinite(nextAngularVelocity) || !std::isfinite(result.normalImpulseNewtonSeconds))
		{
			BatBallCollisionResult rejected{};
			rejected.ballVelocityWorld = input.ballVelocityWorld;
			rejected.ballAngularVelocityWorld = input.ballAngularVelocityWorld;
			return rejected;
		}
		result.ballVelocityWorld = nextVelocity;
		result.ballAngularVelocityWorld = nextAngularVelocity;
		result.hasImpulse = true;
		return result;
	}
} // namespace BaseballSimulatorVR::Physics
