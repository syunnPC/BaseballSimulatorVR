#pragma once

#include "../Baseball/PitchParameters.hpp"
#include "../Common/FieldPresentationState.hpp"
#include "../Common/StrikeZoneSettings.hpp"
#include <array>

namespace BaseballSimulatorVR::Simulation
{
	struct PitchFlightReport final
	{
		Common::PitchZoneCall call{Common::PitchZoneCall::Pending};
		std::vector<Physics::Vector3> trace{};
		std::optional<Physics::Vector3> frontCrossing{};
		std::optional<Physics::Vector3> breakMeters{};
	};

	class PitchFlightAnalysis final
	{
	public:
		void Reset(const Baseball::PitchParameters& parameters, double plateFrontDepth,
		    double moundHeight, const Common::StrikeZoneRuntimeSettings& zone);
		void ObserveSegment(const Physics::Vector3& from, const Physics::Vector3& to);
		void ObservePosition(const Physics::Vector3& position) { ObserveSegment(m_lastPosition, position); }
		void FinishContact(const Physics::Vector3& position);
		void SetPendingZone(const Common::StrikeZoneRuntimeSettings& zone);
		[[nodiscard]] const Common::StrikeZoneRuntimeSettings& Zone() const noexcept { return m_zone; }
		[[nodiscard]] const PitchFlightReport& Actual() const noexcept { return m_actual; }
		[[nodiscard]] const PitchFlightReport& Preview() const noexcept { return m_preview; }
		[[nodiscard]] bool IsResolved() const noexcept { return m_actual.call != Common::PitchZoneCall::Pending; }
		[[nodiscard]] std::vector<Physics::Vector3> BuildMovementTrace(const PitchFlightReport& report) const;
	private:
		[[nodiscard]] PitchFlightReport Predict(const Baseball::PitchParameters& parameters) const;
		[[nodiscard]] std::optional<Physics::Vector3> ReferencePositionAtDepth(double depth) const;
		void Observe(PitchFlightReport& report, bool& touchesZone,
		    const Physics::Vector3& from, const Physics::Vector3& to) const;
		void UpdateBreak(PitchFlightReport& report) const;
		double m_plateFrontDepth{}, m_moundHeight{}, m_radius{.037};
		Common::StrikeZoneRuntimeSettings m_zone{};
		Baseball::PitchParameters m_parameters{};
		bool m_touchesZone{};
		Physics::Vector3 m_lastPosition{};
		std::vector<std::array<Physics::Vector3, 2>> m_actualSegments{};
		PitchFlightReport m_actual{}, m_preview{};
	};
}
