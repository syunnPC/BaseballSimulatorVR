#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Physics/BatBallCollision.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace
{
	[[nodiscard]] bool Near(double left, double right)
	{
		return std::abs(left - right) < 1e-9;
	}

	struct BatRecoil final
	{
		BaseballSimulatorVR::Physics::Vector3 centerVelocity{};
		BaseballSimulatorVR::Physics::Vector3 angularVelocity{};
		BaseballSimulatorVR::Physics::Vector3 surfaceVelocity{};
		double beforeEnergy{};
		double afterEnergy{};
	};
	[[nodiscard]] BatRecoil ComputeBatRecoil(
	    const BaseballSimulatorVR::Physics::BatBallCollisionInput& input,
	    const BaseballSimulatorVR::Physics::BatBallCollisionResult& hit)
	{
		using namespace BaseballSimulatorVR::Physics;
		const Vector3 n = NormalizeOrZero(input.collisionNormal);
		const Vector3 axis = NormalizeOrZero(input.batAxisDirectionWorld);
		const Vector3 impulse = n * hit.normalImpulseNewtonSeconds + hit.tangentialImpulseWorld;
		const double scale = input.batLengthMeters / 0.84;
		const double i0 = 0.885 * 0.23 * 0.23 * scale * scale;
		const double iz = (9.1 / (29.41 * 1.16 * 1.16)) * 0.885 *
		    input.batRadiusMeters * input.batRadiusMeters;
		const Vector3 lever = axis * (input.contactDistanceFromHandleMeters - 0.564 * scale) +
		    n * input.batRadiusMeters;
		const Vector3 torque = -Cross(lever, impulse);
		const Vector3 axialTorque = axis * Dot(torque, axis);
		BatRecoil bat;
		bat.centerVelocity = input.batContactVelocityWorld - impulse / 0.885;
		bat.angularVelocity = (torque - axialTorque) / i0 + axialTorque / iz;
		bat.surfaceVelocity = bat.centerVelocity + Cross(bat.angularVelocity, lever);
		bat.beforeEnergy = 0.5 * 0.885 * LengthSquared(input.batContactVelocityWorld);
		const Vector3 axialOmega = axis * Dot(bat.angularVelocity, axis);
		bat.afterEnergy = 0.5 * 0.885 * LengthSquared(bat.centerVelocity) +
		    0.5 * i0 * LengthSquared(bat.angularVelocity - axialOmega) +
		    0.5 * iz * LengthSquared(axialOmega);
		return bat;
	}
}

void RunBatBallCollisionTests()
{
	using namespace BaseballSimulatorVR::Physics;
	BatBallCollisionInput input{};
	input.ballVelocityWorld = { -40.0, 3.0, 2.0 };
	input.batContactVelocityWorld = { 30.0, -4.0, 8.0 };
	input.collisionNormal = { 1.0, 0.0, 0.0 };
	input.batAxisDirectionWorld = { 0.0, 1.0, 0.0 };
	input.contactDistanceFromHandleMeters = 0.70;
	const auto hit = ResolveBatBallCollision(input);
	Tests::Check(hit.hasImpulse, "Approaching ball receives a bat impulse");
	Tests::Check(hit.ballVelocityWorld.x > 40.0 && hit.ballVelocityWorld.x < 46.0,
	    "40 m/s pitch and 30 m/s bat produce a plausible 40-46 m/s exit speed");
	Tests::Check(hit.effectiveBatMassKilograms > 0.60 &&
	    hit.effectiveBatMassKilograms < 0.70, "R161 barrel effective mass includes angular recoil");
	Tests::Check(Near(hit.ballVelocityWorld.y - 3.0,
	    hit.tangentialImpulseWorld.y / BaseballMassKilograms) &&
	    Near(hit.ballVelocityWorld.z - 2.0,
	        hit.tangentialImpulseWorld.z / BaseballMassKilograms),
	    "Tangential changes are exactly the surface friction impulse, with no swing boost");
	const auto batRecoil = ComputeBatRecoil(input, hit);
	Tests::Check(Near(hit.ballVelocityWorld.x - batRecoil.surfaceVelocity.x, hit.restitution * 70.0),
	    "Normal COR holds at the recoiling bat surface even with 3D friction coupling");
	const Vector3 beforeMomentum = input.ballVelocityWorld * BaseballMassKilograms +
	    input.batContactVelocityWorld * 0.885;
	const Vector3 afterMomentum = hit.ballVelocityWorld * BaseballMassKilograms + batRecoil.centerVelocity * 0.885;
	Tests::Check(Near(Length(beforeMomentum - afterMomentum), 0.0),
	    "The full ball and bat conserve linear momentum in all three dimensions");
	const double sphereInertia = BaseballInertiaFactor * BaseballMassKilograms *
	    BaseballRadiusMeters * BaseballRadiusMeters;
	const double beforeEnergy = 0.5 * BaseballMassKilograms * LengthSquared(input.ballVelocityWorld) +
	    batRecoil.beforeEnergy;
	const double afterEnergy = 0.5 * BaseballMassKilograms * LengthSquared(hit.ballVelocityWorld) +
	    0.5 * sphereInertia * LengthSquared(hit.ballAngularVelocityWorld) + batRecoil.afterEnergy;
	Tests::Check(afterEnergy < beforeEnergy, "The coupled collision dissipates total translational and rotational energy");

	auto moved = input;
	moved.ballVelocityWorld += Vector3{ 11.0, -7.0, 3.0 };
	moved.batContactVelocityWorld += Vector3{ 11.0, -7.0, 3.0 };
	const auto translated = ResolveBatBallCollision(moved);
	Tests::Check(Near(translated.ballVelocityWorld.x, hit.ballVelocityWorld.x + 11.0) &&
	    Near(translated.ballVelocityWorld.y, hit.ballVelocityWorld.y - 7.0) &&
	    Near(translated.ballVelocityWorld.z, hit.ballVelocityWorld.z + 3.0),
	    "Collision is invariant under a common inertial frame translation");
	Tests::Check(Near(Length(translated.ballAngularVelocityWorld - hit.ballAngularVelocityWorld), 0.0),
	    "Changing the inertial frame cannot change generated spin");
	auto normalized = input;
	normalized.collisionNormal *= 5.0;
	normalized.batAxisDirectionWorld *= 3.0;
	Tests::Check(Near(ResolveBatBallCollision(normalized).ballVelocityWorld.x, hit.ballVelocityWorld.x),
	    "Direction magnitudes do not affect response");

	auto tipInput = input;
	tipInput.contactDistanceFromHandleMeters = tipInput.batLengthMeters;
	const auto tipHit = ResolveBatBallCollision(tipInput);
	Tests::Check(tipHit.effectiveBatMassKilograms < hit.effectiveBatMassKilograms &&
	    tipHit.ballVelocityWorld.x < hit.ballVelocityWorld.x,
	    "End hit transfers less normal momentum than a barrel hit at the same velocity");
	auto longerInput = input;
	longerInput.batLengthMeters *= 1.2;
	longerInput.contactDistanceFromHandleMeters *= 1.2;
	Tests::Check(Near(ResolveBatBallCollision(longerInput).effectiveBatMassKilograms,
	    hit.effectiveBatMassKilograms), "Length scales inertia consistently for the same normalized position");
	auto endOn = input;
	endOn.batAxisDirectionWorld = input.collisionNormal;
	Tests::Check(Near(ResolveBatBallCollision(endOn).effectiveBatMassKilograms, 0.885),
	    "An axial normal through the center line causes no angular recoil");

	auto speedInput = input;
	speedInput.batContactVelocityWorld = {};
	speedInput.ballVelocityWorld = { -26.8224, 0.0, 0.0 };
	Tests::Check(Near(ResolveBatBallCollision(speedInput).restitution, 0.55),
	    "Default COR is 0.55 at the 60 mph measured endpoint");
	speedInput.ballVelocityWorld.x = -BatBallReferenceImpactSpeedMetersPerSecond;
	Tests::Check(Near(ResolveBatBallCollision(speedInput).restitution, 0.45),
	    "Default reference COR is 0.45 at 160 mph");
	speedInput.ballVelocityWorld.x = -1.0;
	Tests::Check(Near(ResolveBatBallCollision(speedInput).restitution, 0.55),
	    "Slow impacts hold the measured low speed endpoint");
	speedInput.ballVelocityWorld.x = -120.0;
	Tests::Check(Near(ResolveBatBallCollision(speedInput).restitution, 0.45),
	    "Fast impacts hold the measured high speed endpoint");
	speedInput.restitutionAtReferenceSpeed = 0.0;
	const auto inelastic = ResolveBatBallCollision(speedInput);
	const auto inelasticBat = ComputeBatRecoil(speedInput, inelastic);
	Tests::Check(inelastic.hasImpulse && Near(inelastic.restitution, 0.0) &&
	    Near(inelastic.ballVelocityWorld.x, inelasticBat.surfaceVelocity.x),
	    "Zero COR still transfers momentum and yields equal normal post-impact speeds");

	auto teeInput = input;
	teeInput.ballVelocityWorld = {};
	teeInput.batContactVelocityWorld = { 1.0, 0.0, 0.0 };
	const auto slowTee = ResolveBatBallCollision(teeInput);
	Tests::Check(slowTee.hasImpulse && slowTee.ballVelocityWorld.x < 2.0,
	    "Slow tee contact has no artificial launch-speed floor");
	teeInput.batContactVelocityWorld = {};
	Tests::Check(!ResolveBatBallCollision(teeInput).hasImpulse,
	    "Resting ball and bat produce no impulse");
	teeInput.batContactVelocityWorld = { -3.0, 0.0, 0.0 };
	Tests::Check(!ResolveBatBallCollision(teeInput).hasImpulse,
	    "Separating bat receives no second collision impulse");
	teeInput.batContactVelocityWorld = { 0.0, 30.0, 0.0 };
	Tests::Check(!ResolveBatBallCollision(teeInput).hasImpulse,
	    "A tangential swing cannot inject normal launch speed");

	// Independent comparison against Eqs. 4, 7 and 8 in Kensrud/Nathan/Smith
	// (2017): a 34 m/s bat strikes a stationary, initially nonspinning ball.
	BatBallCollisionInput oblique{};
	oblique.ballVelocityWorld = {};
	oblique.batContactVelocityWorld = { 0.0, 0.0, -34.0 };
	oblique.batAxisDirectionWorld = { 1.0, 0.0, 0.0 };
	oblique.contactDistanceFromHandleMeters = 0.70;
	const double angle = 15.0 * std::numbers::pi / 180.0;
	oblique.collisionNormal = { 0.0, std::sin(angle), -std::cos(angle) };
	const Vector3 tangent{ 0.0, std::cos(angle), std::sin(angle) };
	const auto spun = ResolveBatBallCollision(oblique);
	Tests::Check(spun.hasImpulse && spun.ballAngularVelocityWorld.x > 0.0,
	    "Undercutting a tee ball creates backspin through a tangential impulse");
	const double batI0 = 0.885 * 0.23 * 0.23;
	const double batIz = (9.1 / (29.41 * 1.16 * 1.16)) * 0.885 *
	    oblique.batRadiusMeters * oblique.batRadiusMeters;
	const double transverseRecoil = (BaseballMassKilograms / 3.5) *
	    (1.0 / 0.885 + (0.70 - 0.564) * (0.70 - 0.564) / batI0 +
	        oblique.batRadiusMeters * oblique.batRadiusMeters / batIz);
	const double expectedSpin = (1.0 + WoodBatTangentialRestitution) * 34.0 * std::sin(angle) /
	    (BaseballRadiusMeters * (1.0 + transverseRecoil) * (1.0 + BaseballInertiaFactor));
	Tests::Check(Near(spun.ballAngularVelocityWorld.x, expectedSpin),
	    "Generated spin matches the experimental paper's finite-bat-recoil equation");
	Tests::Check(Near(Dot(spun.ballVelocityWorld, tangent),
	    -BaseballInertiaFactor * BaseballRadiusMeters * expectedSpin),
	    "Spin generation removes the corresponding tangential linear momentum");
	const double ballI = BaseballInertiaFactor * BaseballMassKilograms *
	    BaseballRadiusMeters * BaseballRadiusMeters;
	const Vector3 angularMomentumAtContact = spun.ballAngularVelocityWorld * ballI +
	    Cross(oblique.collisionNormal * BaseballRadiusMeters,
	        spun.ballVelocityWorld * BaseballMassKilograms);
	Tests::Check(Near(Length(angularMomentumAtContact), 0.0),
	    "An oblique tee collision conserves ball angular momentum about the surface contact");
	const Vector3 beforeSlip = -oblique.batContactVelocityWorld;
	const double tangentBefore = Dot(beforeSlip, tangent);
	const auto spunBat = ComputeBatRecoil(oblique, spun);
	const double tangentAfter = Dot(spun.ballVelocityWorld +
	    Cross(spun.ballAngularVelocityWorld, oblique.collisionNormal * -BaseballRadiusMeters) -
	        spunBat.surfaceVelocity, tangent);
	Tests::Check(Near(tangentAfter, -WoodBatTangentialRestitution * tangentBefore),
	    "The gripping contact satisfies the measured tangential COR after bat recoil");

	auto oppositeOffset = oblique;
	oppositeOffset.collisionNormal.y *= -1.0;
	const auto topspin = ResolveBatBallCollision(oppositeOffset);
	Tests::Check(Near(topspin.ballAngularVelocityWorld.x, -spun.ballAngularVelocityWorld.x) &&
	    Near(topspin.ballVelocityWorld.y, -spun.ballVelocityWorld.y),
	    "An equal overcut reverses spin and launch angle without changing speed");
	auto incomingSpin = oblique;
	incomingSpin.ballAngularVelocityWorld = { 200.0, 0.0, 0.0 };
	const auto withSpin = ResolveBatBallCollision(incomingSpin);
	Tests::Check(!Near(withSpin.ballAngularVelocityWorld.x, 200.0) &&
	    !Near(withSpin.ballAngularVelocityWorld.x, spun.ballAngularVelocityWorld.x),
	    "Incoming pitch spin participates in the surface slip rather than passing through unchanged");
	const Vector3 incomingMomentum = incomingSpin.ballAngularVelocityWorld * ballI +
	    Cross(incomingSpin.collisionNormal * BaseballRadiusMeters,
	        incomingSpin.ballVelocityWorld * BaseballMassKilograms);
	const Vector3 outgoingMomentum = withSpin.ballAngularVelocityWorld * ballI +
	    Cross(incomingSpin.collisionNormal * BaseballRadiusMeters,
	        withSpin.ballVelocityWorld * BaseballMassKilograms);
	Tests::Check(Near(Length(outgoingMomentum - incomingMomentum), 0.0),
	    "Angular momentum about contact is also preserved for a spinning incoming pitch");
	auto gyro = oblique;
	gyro.collisionNormal = { 0.0, 0.0, -1.0 };
	gyro.ballAngularVelocityWorld = { 0.0, 0.0, 200.0 };
	const auto gyroHit = ResolveBatBallCollision(gyro);
	Tests::Check(Near(gyroHit.ballAngularVelocityWorld.z, 200.0),
	    "Spin about the contact normal is preserved by the surface impulse");
	auto grazing = oblique;
	grazing.collisionNormal = { 0.0, std::sin(89.9 * std::numbers::pi / 180.0),
	    -std::cos(89.9 * std::numbers::pi / 180.0) };
	const auto graze = ResolveBatBallCollision(grazing);
	Tests::Check(Near(Length(graze.tangentialImpulseWorld),
	    BatBallFrictionCoefficient * graze.normalImpulseNewtonSeconds),
	    "A grazing contact is limited by available normal load, not an exit-speed cap");
	Tests::Check(graze.tangentialRestitutionLimitedByFriction,
	    "The constrained solver identifies contact outside the measured gripping regime");
	// At en=1 and zero reduced tangential energy gradient, a different fixed
	// tangential COR would create energy. The passive solution is Jt=0 while
	// normal COR is still exact; this exercises the energy boundary explicitly.
	auto elastic = input;
	elastic.ballAngularVelocityWorld = {};
	elastic.restitutionAtReferenceSpeed = 1.0;
	elastic.ballVelocityWorld = elastic.batContactVelocityWorld + Vector3{ -80.0, 0.0, 0.0 };
	const double kn = 1.0 / BaseballMassKilograms + 1.0 / 0.885 +
	    (0.70 - 0.564) * (0.70 - 0.564) / (0.885 * 0.23 * 0.23);
	const double coupling = -(0.70 - 0.564) * elastic.batRadiusMeters /
	    (0.885 * 0.23 * 0.23);
	elastic.ballVelocityWorld.y += (coupling / kn) * -80.0;
	const auto energyBoundary = ResolveBatBallCollision(elastic);
	const auto elasticBat = ComputeBatRecoil(elastic, energyBoundary);
	Tests::Check(energyBoundary.hasImpulse && energyBoundary.tangentialRestitutionLimitedByEnergy &&
	    Length(energyBoundary.tangentialImpulseWorld) < 1e-10 &&
	    Near(energyBoundary.ballVelocityWorld.x - elasticBat.surfaceVelocity.x, 80.0),
	    "A coupled elastic contact enforces passive energy without breaking normal restitution");

	for (double batSpeed : std::array{ 28.0, 32.18688, 36.0, 39.0 })
	{
		auto normalHit = oblique;
		normalHit.ballVelocityWorld = { 0.0, 0.0, 40.0 };
		normalHit.batContactVelocityWorld = { 0.0, 0.0, -batSpeed };
		normalHit.collisionNormal = { 0.0, 0.0, -1.0 };
		const auto normalResult = ResolveBatBallCollision(normalHit);
		Tests::Check(normalResult.hasImpulse && Length(normalResult.ballVelocityWorld) > 40.0 &&
		    Length(normalResult.ballVelocityWorld) < 55.0,
		    "A measured 28-39 m/s bat against a 40 m/s pitch has a plausible uncapped 40-55 m/s normal exit");
	}

	// Sample 3D end/axial impacts, incoming spin, and COR 0, .45, .5, 1.
	// Derive bat recoil independently from each computed impulse.
	std::mt19937 random(0xBA7BA11);
	std::uniform_real_distribution<double> uniform(-1.0, 1.0);
	for (int sample = 0; sample < 4000; ++sample)
	{
		BatBallCollisionInput spatial{};
		spatial.batLengthMeters = 0.85;
		spatial.batRadiusMeters = 0.032;
		spatial.contactDistanceFromHandleMeters = (uniform(random) + 1.0) * 0.425;
		spatial.batAxisDirectionWorld = NormalizeOrZero({ uniform(random), uniform(random), uniform(random) });
		spatial.collisionNormal = NormalizeOrZero({ uniform(random), uniform(random), uniform(random) });
		spatial.batContactVelocityWorld = { 32.0 * uniform(random), 32.0 * uniform(random), 32.0 * uniform(random) };
		spatial.ballVelocityWorld = spatial.batContactVelocityWorld +
		    Vector3{ 70.0 * uniform(random), 70.0 * uniform(random), 70.0 * uniform(random) };
		const double initialNormal = Dot(spatial.ballVelocityWorld - spatial.batContactVelocityWorld,
		    spatial.collisionNormal);
		if (initialNormal >= -0.001)
			spatial.ballVelocityWorld -= spatial.collisionNormal * (initialNormal + 0.01);
		spatial.ballAngularVelocityWorld = { 500.0 * uniform(random), 500.0 * uniform(random), 500.0 * uniform(random) };
		spatial.restitutionAtReferenceSpeed = std::array{ 0.0, 0.45, 0.5, 1.0 }[static_cast<std::size_t>(sample) % 4];
		const auto contact = ResolveBatBallCollision(spatial);
		Tests::Check(contact.hasImpulse, "A finite approaching spatial collision has a constrained impulse solution");
		const auto bat = ComputeBatRecoil(spatial, contact);
		const Vector3 relativeAfter = contact.ballVelocityWorld +
		    Cross(contact.ballAngularVelocityWorld, spatial.collisionNormal * -BaseballRadiusMeters) - bat.surfaceVelocity;
		const double expectedNormal = -contact.restitution * Dot(spatial.ballVelocityWorld -
		    spatial.batContactVelocityWorld, spatial.collisionNormal);
		Tests::Check(std::abs(Dot(relativeAfter, spatial.collisionNormal) - expectedNormal) < 1e-8,
		    "Normal COR remains exact after friction-cone and passive-energy constraints in 3D");
		Tests::Check(Length(contact.tangentialImpulseWorld) <=
		    BatBallFrictionCoefficient * contact.normalImpulseNewtonSeconds + 1e-10,
		    "All spatial contacts satisfy the Coulomb friction cone with nonnegative normal impulse");
		const double energyBefore = 0.5 * BaseballMassKilograms * LengthSquared(spatial.ballVelocityWorld) +
		    0.5 * sphereInertia * LengthSquared(spatial.ballAngularVelocityWorld) + bat.beforeEnergy;
		const double energyAfter = 0.5 * BaseballMassKilograms * LengthSquared(contact.ballVelocityWorld) +
		    0.5 * sphereInertia * LengthSquared(contact.ballAngularVelocityWorld) + bat.afterEnergy;
		Tests::Check(energyAfter <= energyBefore + 1e-8,
		    "The coupled ball-bat system never creates kinetic energy, including elastic-COR settings");
	}

	const double nan = std::numeric_limits<double>::quiet_NaN();
	const std::array invalidInputs{
	    [&] { auto v = input; v.batLengthMeters = 0.0; return v; }(),
	    [&] { auto v = input; v.contactDistanceFromHandleMeters = -0.1; return v; }(),
	    [&] { auto v = input; v.contactDistanceFromHandleMeters = 1.0; return v; }(),
	    [&] { auto v = input; v.restitutionAtReferenceSpeed = 1.01; return v; }(),
	    [&] { auto v = input; v.restitutionAtReferenceSpeed = nan; return v; }(),
	    [&] { auto v = input; v.batContactVelocityWorld.x = nan; return v; }(),
	    [&] { auto v = input; v.ballAngularVelocityWorld.x = nan; return v; }(),
	    [&] { auto v = input; v.batRadiusMeters = 0.0; return v; }(),
	    [&] { auto v = input; v.collisionNormal = {}; return v; }(),
	    [&] { auto v = input; v.batAxisDirectionWorld = {}; return v; }(),
	};
	for (const auto& invalid : invalidInputs)
	{
		const auto rejected = ResolveBatBallCollision(invalid);
		Tests::Check(!rejected.hasImpulse && Near(rejected.ballVelocityWorld.x, input.ballVelocityWorld.x),
		    "Invalid collision input is rejected without synthesizing a bat response");
	}
}
