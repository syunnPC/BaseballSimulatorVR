#include "PitchFlightAnalysis.hpp"
#include "../Common/HomePlateGeometry.hpp"
#include "PitchSimulationService.hpp"

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Simulation
{
	namespace
	{
		void AppendPoint(PitchFlightReport& report, const Physics::Vector3& point, bool force)
		{
			if (report.trace.empty() || Physics::LengthSquared(point - report.trace.back()) > .06 * .06)
				report.trace.push_back(point);
			else if (force && Physics::LengthSquared(point - report.trace.back()) > 1e-16)
				report.trace.push_back(point);
		}
	}

	void PitchFlightAnalysis::Reset(const Baseball::PitchParameters& parameters, double plateFrontDepth,
	    double moundHeight, const Common::StrikeZoneRuntimeSettings& zone)
	{
		m_plateFrontDepth = plateFrontDepth;
		m_moundHeight = moundHeight;
		m_radius = parameters.ballProperties.radiusMeters;
		m_zone = zone;
		m_parameters = parameters;
		m_touchesZone = false;
		m_actual = {};
		m_actualSegments.clear();
		m_actual.trace.push_back({0, parameters.releaseHeightMeters + moundHeight, 0});
		m_lastPosition = {0, parameters.releaseHeightMeters, 0};
		m_preview = Predict(parameters);
		UpdateBreak(m_preview);
	}

	std::optional<Physics::Vector3> PitchFlightAnalysis::ReferencePositionAtDepth(const double depth) const
	{
		const double forward = m_parameters.initialVelocity.z;
		if (!std::isfinite(depth) || !std::isfinite(forward) || forward <= 0.) return std::nullopt;
		const double t = depth / forward;
		return Physics::Vector3{m_parameters.initialVelocity.x * t,
		    m_parameters.releaseHeightMeters + m_moundHeight + m_parameters.initialVelocity.y * t, depth};
	}

	void PitchFlightAnalysis::UpdateBreak(PitchFlightReport& report) const
	{
		const auto reference = ReferencePositionAtDepth(m_plateFrontDepth);
		if (reference && report.frontCrossing)
			report.breakMeters = *report.frontCrossing - *reference;
	}

	void PitchFlightAnalysis::Observe(PitchFlightReport& report, bool& touchesZone,
	    const Physics::Vector3& from, const Physics::Vector3& to) const
	{
		if (report.call != Common::PitchZoneCall::Pending) return;
		auto a = from, b = to;
		a.y += m_moundHeight;
		b.y += m_moundHeight;
		const double endDepth = m_plateFrontDepth + Common::HomePlateDepthMeters + m_radius;
		if (b.z > endDepth && b.z > a.z)
			b = a + (b - a) * std::clamp((endDepth - a.z) / (b.z - a.z), 0., 1.);
		if (!report.frontCrossing && a.z <= m_plateFrontDepth && b.z >= m_plateFrontDepth && b.z > a.z)
		{
			report.frontCrossing = a + (b - a) * ((m_plateFrontDepth - a.z) / (b.z - a.z));
			UpdateBreak(report);
		}
		if ((std::max)(a.z, b.z) >= m_plateFrontDepth - m_radius &&
		    (std::min)(a.z, b.z) <= endDepth)
			touchesZone = touchesZone || Common::SweptBallTouchesStrikeZone(a, b, m_radius,
			    m_plateFrontDepth, m_zone.bottomMeters, m_zone.bottomMeters + m_zone.heightMeters);
		const bool passed = b.z >= endDepth - 1e-10;
		AppendPoint(report, b, passed);
		if (passed) report.call = touchesZone ? Common::PitchZoneCall::Strike : Common::PitchZoneCall::Ball;
	}

	PitchFlightReport PitchFlightAnalysis::Predict(const Baseball::PitchParameters& parameters) const
	{
		PitchSimulationService simulation;
		simulation.Reset(parameters);
		PitchFlightReport report;
		report.trace.reserve(320);
		report.trace.push_back({0, parameters.releaseHeightMeters + m_moundHeight, 0});
		bool touches = false;
		const int maxSteps = static_cast<int>(std::clamp(
		    (4. * m_plateFrontDepth / (std::max)(parameters.initialVelocity.z, 1.) + 2.) * 1000., 2000., 60000.));
		for (int step = 0; step < maxSteps && report.call == Common::PitchZoneCall::Pending; ++step)
		{
			const auto previous = simulation.GetBallState().position;
			simulation.Tick(std::chrono::duration<double>(.001));
			Observe(report, touches, previous, simulation.GetBallState().position);
		}
		return report;
	}

	std::vector<Physics::Vector3> PitchFlightAnalysis::BuildMovementTrace(const PitchFlightReport& report) const
	{
		if (report.trace.empty()) return {};
		const auto anchor = ReferencePositionAtDepth(report.trace.back().z);
		if (!anchor) return {};
		std::vector<Physics::Vector3> result;
		result.reserve(report.trace.size());
		for (const auto& point : report.trace)
		{
			const auto reference = ReferencePositionAtDepth(point.z);
			if (!reference) return {};
			// Remove only the initial straight launch ray; gravity and aerodynamic bending remain.
			const Physics::Vector3 movement{anchor->x + point.x - reference->x,
			    anchor->y + point.y - reference->y, point.z};
			if (!result.empty() && std::hypot(movement.x - result.back().x, movement.y - result.back().y) <= 1e-8)
				result.back() = movement;
			else result.push_back(movement);
		}
		return result;
	}

	void PitchFlightAnalysis::ObserveSegment(const Physics::Vector3& from, const Physics::Vector3& to)
	{
		if (!IsResolved() && Physics::LengthSquared(to - from) > 1e-20)
		{
			auto a = from, b = to;
			a.y += m_moundHeight;
			b.y += m_moundHeight;
			m_actualSegments.push_back({a, b});
		}
		Observe(m_actual, m_touchesZone, from, to);
		m_lastPosition = to;
	}

	void PitchFlightAnalysis::FinishContact(const Physics::Vector3& position)
	{
		if (IsResolved()) return;
		auto point = position;
		point.y += m_moundHeight;
		AppendPoint(m_actual, point, true);
		m_actual.call = Common::PitchZoneCall::Contact;
	}

	void PitchFlightAnalysis::SetPendingZone(const Common::StrikeZoneRuntimeSettings& zone)
	{
		if (IsResolved()) return;
		m_zone = zone;
		m_touchesZone = false;
		for (const auto& segment : m_actualSegments)
			m_touchesZone = m_touchesZone || Common::SweptBallTouchesStrikeZone(
			    segment[0], segment[1], m_radius, m_plateFrontDepth,
			    zone.bottomMeters, zone.bottomMeters + zone.heightMeters);
		m_preview = Predict(m_parameters);
		UpdateBreak(m_preview);
	}
}
