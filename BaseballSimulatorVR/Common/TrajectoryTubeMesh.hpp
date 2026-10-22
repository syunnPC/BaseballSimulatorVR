#pragma once

#include "PreparedWorldProjection.hpp"

#include <cstdint>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	class TrajectoryTubeMesh final
	{
	  public:
		// Incoming tangents and transported frames depend only on preceding samples.
		void Build(const std::vector<Physics::Vector3>& points,
		    const Physics::Vector3& worldOffset, double radius)
		{
			vertices.clear();
			if (points.size() < 2) return;
			Physics::Vector3 previous = points.front() + worldOffset;
			Physics::Vector3 tangent{}, right{};
			bool started = false;
			for (std::size_t index = 1; index < points.size(); ++index)
			{
				const auto current = points[index] + worldOffset;
				const auto delta = current - previous;
				if (Physics::LengthSquared(delta) <= 1e-12) continue;
				const auto nextTangent = Physics::NormalizeOrZero(delta);
				if (!started)
				{
					tangent = nextTangent;
					right = Physics::NormalizeOrZero(Physics::Cross(tangent,
					    std::abs(tangent.y) < .9 ? Physics::Vector3{0.,1.,0.} : Physics::Vector3{1.,0.,0.}));
					AppendRing(previous, tangent, right, radius);
					started = true;
				}
				else
				{
					const auto rotation = Physics::Cross(tangent, nextTangent);
					const double cosine = std::clamp(Physics::Dot(tangent, nextTangent), -1., 1.);
					if (cosine > -1. + 1e-9)
						right = right + Physics::Cross(rotation, right) +
						    Physics::Cross(rotation, Physics::Cross(rotation, right)) / (1. + cosine);
					// An exact reversal rotates about the existing right axis.
					tangent = nextTangent;
					right = Physics::NormalizeOrZero(right - tangent * Physics::Dot(right, tangent));
				}
				AppendRing(current, tangent, right, radius);
				previous = current;
			}
		}

		template <class TTriangle> void ForEachTriangle(TTriangle&& triangle) const
		{
			if (vertices.size() < 8) return;
			triangle(0U, 2U, 1U);
			triangle(0U, 3U, 2U);
			for (std::uint32_t ring = 4; ring < vertices.size(); ring += 4)
			for (std::uint32_t side = 0; side < 4; ++side)
			{
				const auto next = (side + 1) % 4;
				triangle(ring - 4 + side, ring - 4 + next, ring + next);
				triangle(ring - 4 + side, ring + next, ring + side);
			}
			const auto end = static_cast<std::uint32_t>(vertices.size() - 4);
			triangle(end, end + 1, end + 2);
			triangle(end, end + 2, end + 3);
		}

		std::vector<Physics::Vector3> vertices;

	  private:
		void AppendRing(const Physics::Vector3& center, const Physics::Vector3& tangent,
		    const Physics::Vector3& right, double radius)
		{
			const auto up = Physics::Cross(tangent, right);
			vertices.push_back(center + right * radius);
			vertices.push_back(center + up * radius);
			vertices.push_back(center - right * radius);
			vertices.push_back(center - up * radius);
		}
	};

	class TrajectoryTubeProjectionWorkspace final
	{
	  public:
		template <class TTriangle>
		void Emit(const TrajectoryTubeMesh& mesh, const ViewProjectionState& view,
		    LONG width, LONG height, double nearClip, double farClip,
		    const float* color, TTriangle&& triangle)
		{
			const PreparedWorldProjection projection(view, width, height, nearClip, farClip);
			m_projected.resize(mesh.vertices.size());
			m_visible.resize(mesh.vertices.size());
			for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
				m_visible[i] = projection.Project(mesh.vertices[i], &m_projected[i], false);
			const auto q = view.orientation;
			const double w = view.orientationW;
			const Physics::Vector3 back{2. * (q.x * q.z + q.y * w),
			    2. * (q.y * q.z - q.x * w), 1. - 2. * (q.x * q.x + q.y * q.y)};
			const auto depth = [&](const Physics::Vector3& point)
			{ return -Physics::Dot(point - view.position, back); };
			mesh.ForEachTriangle([&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
			{
				const auto& a = mesh.vertices[i0];
				const auto& b = mesh.vertices[i1];
				const auto& c = mesh.vertices[i2];
				if (Physics::Dot(Physics::Cross(b - a, c - a), view.position - a) <= 0.) return;
				if (m_visible[i0] && m_visible[i1] && m_visible[i2])
				{
					triangle(m_projected[i0], m_projected[i1], m_projected[i2], color);
					return;
				}
				// Clip faces that cross the near plane so their visible part is still drawn.
				const std::array<Physics::Vector3,3> input{a,b,c};
				std::array<Physics::Vector3,4> clipped;
				std::size_t count = 0;
				constexpr double PlaneEpsilon = 1e-7;
				const double clipDepth = nearClip + PlaneEpsilon;
				for (std::size_t i = 0; i < input.size(); ++i)
				{
					const auto& start = input[i];
					const auto& end = input[(i + 1) % input.size()];
					const double startDepth = depth(start), endDepth = depth(end);
					const bool startInside = startDepth >= clipDepth, endInside = endDepth >= clipDepth;
					if (startInside) clipped[count++] = start;
					if (startInside != endInside)
						clipped[count++] = start + (end - start) *
						    ((clipDepth - startDepth) / (endDepth - startDepth));
				}
				if (count < 3) return;
				std::array<ScreenPoint,4> projected;
				for (std::size_t i = 0; i < count; ++i)
					if (!projection.Project(clipped[i], &projected[i], false)) return;
				for (std::size_t i = 1; i + 1 < count; ++i)
					triangle(projected[0], projected[i], projected[i + 1], color);
			});
		}

	  private:
		std::vector<ScreenPoint> m_projected;
		std::vector<std::uint8_t> m_visible;
	};
}
