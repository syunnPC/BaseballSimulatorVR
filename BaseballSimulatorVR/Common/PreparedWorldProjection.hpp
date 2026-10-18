#pragma once

#include "../Framework.hpp"
#include "ViewProjectionState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace BaseballSimulatorVR::Common
{
	struct ScreenPoint final
	{
		double x{0.0};
		double y{0.0};
		float depth{0.0F};
	};
	// Precompute camera rotation and asymmetric FOV for all vertices in a view.
	class PreparedWorldProjection final
	{
	  public:
		PreparedWorldProjection(const ViewProjectionState& view, LONG width, LONG height,
		    double nearClip, double farClip)
		    : m_position(view.position), m_width(width), m_height(height), m_nearClip(nearClip),
		      m_farClip(farClip)
		{
			m_valid = view.isValid && width > 0 && height > 0;
			if (!m_valid)
			{
				return;
			}
			const auto q = view.orientation;
			const double w = view.orientationW;
			m_rotation = {1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y - q.z * w),
			    2.0 * (q.x * q.z + q.y * w), 2.0 * (q.x * q.y + q.z * w),
			    1.0 - 2.0 * (q.x * q.x + q.z * q.z), 2.0 * (q.y * q.z - q.x * w),
			    2.0 * (q.x * q.z - q.y * w), 2.0 * (q.y * q.z + q.x * w),
			    1.0 - 2.0 * (q.x * q.x + q.y * q.y)};
			m_tanLeft = std::tan(view.fovLeftRadians);
			m_tanDown = std::tan(view.fovDownRadians);
			m_horizontalRange = std::tan(view.fovRightRadians) - m_tanLeft;
			m_verticalRange = std::tan(view.fovUpRadians) - m_tanDown;
			m_valid = m_horizontalRange > 1e-6 && m_verticalRange > 1e-6;
		}
		[[nodiscard]] bool Project(const Physics::Vector3& world, ScreenPoint* projected,
		    bool requireVisibleBounds = true) const
		{
			if (!m_valid || projected == nullptr)
			{
				return false;
			}
			const auto delta = world - m_position;
			const double viewX =
			    m_rotation[0] * delta.x + m_rotation[3] * delta.y + m_rotation[6] * delta.z;
			const double viewY =
			    m_rotation[1] * delta.x + m_rotation[4] * delta.y + m_rotation[7] * delta.z;
			const double viewZ =
			    m_rotation[2] * delta.x + m_rotation[5] * delta.y + m_rotation[8] * delta.z;
			if (viewZ >= -m_nearClip)
			{
				return false;
			}
			const double invDepth = 1.0 / (-viewZ);
			const double normalizedX = (viewX * invDepth - m_tanLeft) / m_horizontalRange;
			const double normalizedY = (viewY * invDepth - m_tanDown) / m_verticalRange;
			if (requireVisibleBounds &&
			    (normalizedX < 0 || normalizedX > 1 || normalizedY < 0 || normalizedY > 1))
			{
				return false;
			}
			const double longMin = static_cast<double>((std::numeric_limits<LONG>::min)());
			const double longMax = static_cast<double>((std::numeric_limits<LONG>::max)());
			projected->x = std::clamp(
			    normalizedX * static_cast<double>((std::max)(1L, m_width - 1)), longMin, longMax);
			projected->y =
			    std::clamp((1.0 - normalizedY) * static_cast<double>((std::max)(1L, m_height - 1)),
			        longMin, longMax);
			projected->depth = static_cast<float>(
			    std::clamp(((-viewZ) - m_nearClip) / (m_farClip - m_nearClip), 0.0, 1.0));
			return true;
		}

	  private:
		Physics::Vector3 m_position;
		std::array<double, 9> m_rotation{};
		LONG m_width, m_height;
		double m_nearClip, m_farClip, m_tanLeft{}, m_tanDown{}, m_horizontalRange{},
		    m_verticalRange{};
		bool m_valid{};
	};
} // namespace BaseballSimulatorVR::Common
