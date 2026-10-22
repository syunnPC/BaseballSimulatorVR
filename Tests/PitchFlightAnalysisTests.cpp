#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchFlightAnalysis.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchSimulationService.hpp"
#include "../BaseballSimulatorVR/Common/HomePlateGeometry.hpp"

#include <cmath>

void RunPitchFlightAnalysisTests()
{
	using namespace BaseballSimulatorVR;
	using Call = Common::PitchZoneCall;
	Baseball::PitchParameters parameters;
	parameters.initialVelocity = {0, 0, 40};
	parameters.releaseHeightMeters = 1.8;
	parameters.spinRpm = 0;
	parameters.dragCoefficient = 0;
	parameters.useCoefficientTable = false;
	const Common::StrikeZoneRuntimeSettings zone{.5, 1., .44};
	Simulation::PitchFlightAnalysis analysis;
	analysis.Reset(parameters, 18.44, .18, zone);
	Tests::Check(!analysis.IsResolved() && analysis.Actual().trace.size() == 1 &&
	    analysis.Preview().call == Call::Strike && analysis.Preview().breakMeters &&
	    std::abs(analysis.Preview().breakMeters->y + .5 * 9.80665 * std::pow(18.44 / 40., 2)) < .000002 &&
	    std::abs(analysis.Preview().breakMeters->x) < 1e-12,
	    "A spinless pitch reports its actual gravitational drop from the initial launch ray without publishing a verdict");
	Simulation::PitchSimulationService actual;
	actual.Reset(parameters);
	bool sawFront = false;
	for (int step = 0; step < 1000 && !analysis.IsResolved(); ++step)
	{
		actual.Tick(std::chrono::duration<double>(.001));
		const auto point = actual.GetBallState().position;
		analysis.ObservePosition(point);
		if (point.z >= 18.44 && point.z < 18.44 + Common::HomePlateDepthMeters)
		{
			sawFront = true;
			Tests::Check(!analysis.IsResolved(), "The front-plane crossing does not finalize a volumetric call");
		}
	}
	Tests::Check(sawFront && analysis.Actual().call == Call::Strike && analysis.Actual().breakMeters &&
	    Physics::Length(*analysis.Actual().breakMeters - *analysis.Preview().breakMeters) < 1e-10 &&
	    std::abs(analysis.Actual().trace.back().z - 18.44 - Common::HomePlateDepthMeters - .037) < 1e-10,
	    "An actual pitch is finalized only when its trailing edge clears the plate, with a clipped diagram endpoint");
	for (const auto* report : {&analysis.Actual(), &analysis.Preview()})
	{
		const auto movement = analysis.BuildMovementTrace(*report);
		Tests::Check(movement.size() > 2 && movement.back().y < movement.front().y - 1. &&
		    Physics::Length(movement.back() - report->trace.back()) < 1e-10,
		    "A zero-RPM pitch shows gravitational drop as a line anchored to its actual endpoint");
	}
	const auto completedTrace = analysis.Actual().trace;
	analysis.ObservePosition({20, 10, 50});
	Tests::Check(analysis.Actual().trace.size() == completedTrace.size() &&
	    Physics::Length(analysis.Actual().trace.back() - completedTrace.back()) < 1e-12,
	    "The finished pitch diagram is immutable while the ball continues into the catcher area");

	analysis.Reset(parameters, 18.44, .18, zone);
	analysis.ObserveSegment({2, .8, 18}, {2, .8, 19});
	Tests::Check(analysis.Actual().call == Call::Ball && analysis.Preview().call == Call::Strike,
	    "The verdict uses the actual path rather than substituting a cached predicted strike");
	analysis.Reset(parameters, 18.44, .18, zone);
	analysis.ObserveSegment({.2159 + .037, .8, 18}, {.2159 + .037, .8, 19});
	Tests::Check(analysis.Actual().call == Call::Strike,
	    "A ball whose surface grazes the plate side is a strike even with its center outside the plate");
	analysis.Reset(parameters, 18.44, 0., {.55, 1., .44});
	analysis.ObservePosition({0, .50, 18.43});
	analysis.ObservePosition({0, .514, 18.45});
	analysis.ObservePosition({0, .50, 18.47});
	analysis.SetPendingZone({.55, 1., .44});
	analysis.ObservePosition({0, .40, 19.});
	Tests::Check(analysis.Actual().call == Call::Strike,
	    "Rechecking a pending zone retains grazing samples omitted by the decimated display trace");
	analysis.SetPendingZone({1.4, .3, .44});
	Tests::Check(analysis.Actual().call == Call::Strike && analysis.Zone().bottomMeters == .55,
	    "A completed verdict and its plotted zone stay associated with that pitch until the next reset");

	analysis.Reset(parameters, 18.44, .18, zone);
	const Physics::Vector3 contact{.1, .9, 17.9};
	analysis.ObservePosition(contact);
	analysis.FinishContact(contact);
	Tests::Check(analysis.Actual().call == Call::Contact && !analysis.Actual().breakMeters &&
	    Physics::Length(analysis.Actual().trace.back() - Physics::Vector3{.1, 1.08, 17.9}) < 1e-12,
	    "A hit before the plate freezes the real pitch at contact and does not invent a plate break measurement");
	const auto contactTrace = analysis.Actual().trace;
	analysis.ObservePosition({2, 8, 0});
	Tests::Check(analysis.Actual().trace.size() == contactTrace.size() &&
	    Physics::Length(analysis.Actual().trace.back() - contactTrace.back()) < 1e-12,
	    "Outgoing batted motion cannot enter a completed pitch diagram");

	parameters.spinRpm = 2600;
	parameters.spinAxis = {.8, .6, 0};
	parameters.liftCoefficient = .05;
	for (const bool physical : {false, true})
	{
		parameters.usePhysicalAerodynamics = physical;
		parameters.useCoefficientTable = !physical;
		parameters.windVelocity = {2, 0, 1};
		analysis.Reset(parameters, 18.44, .18, zone);
		const auto right = analysis.Preview();
		const auto rightMovement = analysis.BuildMovementTrace(right);
		parameters.spinAxis.y = -.6;
		parameters.windVelocity.x = -2;
		analysis.Reset(parameters, 18.44, .18, zone);
		const auto left = analysis.Preview();
		const auto leftMovement = analysis.BuildMovementTrace(left);
		Tests::Check(right.breakMeters && left.breakMeters &&
		    std::abs(right.breakMeters->x + left.breakMeters->x) < 1e-10 &&
		    std::abs(right.breakMeters->y - left.breakMeters->y) < 1e-10 &&
		    right.breakMeters->x > 0 && right.breakMeters->y < 0,
		    "Classic and physical lift models produce mirrored horizontal and equal vertical bending in mirrored wind");
		Tests::Check(rightMovement.size() > 2 && leftMovement.size() == rightMovement.size() &&
		    rightMovement.back().x > rightMovement.front().x && rightMovement.back().y < rightMovement.front().y &&
		    Physics::Length(rightMovement.back() - right.trace.back()) < 1e-10,
		    "The movement-only line shows air-force and gravitational bending and retains the actual final position");
		for (std::size_t i = 0; i < rightMovement.size(); ++i)
			Tests::Check(std::abs(rightMovement[i].x + leftMovement[i].x) < 1e-10 &&
			    std::abs(rightMovement[i].y - leftMovement[i].y) < 1e-10,
			    "Left and right deliveries mirror every movement-only sample without changing its vertical break");
		parameters.spinAxis.y = .6;
		parameters.windVelocity.x = 2;
	}
	parameters.spinAxis = {};
	parameters.usePhysicalAerodynamics = false;
	parameters.useCoefficientTable = true;
	analysis.Reset(parameters, 18.44, .18, zone);
	Tests::Check(analysis.Preview().breakMeters && analysis.Preview().breakMeters->y < -1.,
	    "Gravity remains visible with a zero spin axis and RPM-dependent drag");
	parameters.initialVelocity = {1.5, 2., 40.};
	analysis.Reset(parameters, 18.44, .18, zone);
	Tests::Check(analysis.BuildMovementTrace(analysis.Preview()).size() > 2,
	    "An angled release shows its gravity and wind bending as a movement line");
	for (const double rpm : {0., 900., 3500.})
	{
		parameters.spinRpm = rpm;
		analysis.Reset(parameters, 18.44, .18, zone);
		Simulation::PitchFlightReport straight;
		for (const double depth : {0., 2., 9., 18.44})
		{
			const double time = depth / parameters.initialVelocity.z;
			straight.trace.push_back({parameters.initialVelocity.x * time,
			    parameters.releaseHeightMeters + .18 + parameters.initialVelocity.y * time, depth});
		}
		const auto point = analysis.BuildMovementTrace(straight);
		Tests::Check(point.size() == 1 && Physics::Length(point.back() - straight.trace.back()) < 1e-10,
		    "Only a geometrically straight path collapses to a point, independently of the RPM metadata");
	}
	parameters.initialVelocity = {0, 0, 40};
	analysis.Reset(parameters, 18.44, .18, zone);
	analysis.SetPendingZone({1.4, .3, .44});
	Tests::Check(analysis.Preview().call == Call::Ball && analysis.Actual().call == Call::Pending,
	    "Loading or calibrating the player zone updates the pending prediction without publishing a verdict");
	analysis.SetPendingZone(zone);
	Tests::Check(analysis.Preview().call == Call::Strike,
	    "Restoring a pending player zone rechecks the cached launch against its new height");
	parameters.ballProperties.radiusMeters = .0485;
	parameters.ballProperties.massKilograms = .190;
	parameters.initialVelocity = {0, 3.5, 80. / 3.6};
	parameters.spinRpm = 0;
	parameters.usePhysicalAerodynamics = true;
	parameters.windVelocity = {};
	analysis.Reset(parameters, 18.44, .18, zone);
	const auto forkMovement = analysis.BuildMovementTrace(analysis.Preview());
	Tests::Check(forkMovement.size() > 2 && forkMovement.back().y < forkMovement.front().y - 2. &&
	    analysis.Preview().breakMeters && analysis.Preview().breakMeters->y < -2.,
	    "A physical zero-RPM softball fork retains its large fall on the plotted line and centimeter metrics");
	analysis.Reset(parameters, 9., 0., zone);
	Tests::Check(analysis.Actual().call == Call::Pending && analysis.Actual().trace.size() == 1 &&
	    std::abs(analysis.Preview().trace.back().z - 9. - Common::HomePlateDepthMeters - .0485) < 1e-10,
	    "Reset clears previous calls and traces and uses the current softball radius and toss distance");
}
