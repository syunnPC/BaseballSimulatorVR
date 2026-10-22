#pragma once

#include "PreparedWorldProjection.hpp"

#include <cstdint>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	struct WorldMeshTriangle final
	{
		std::array<std::uint32_t, 3> indices{};
		std::array<float, 4> color{};
	};
	struct IndexedWorldMesh final
	{
		std::vector<Physics::Vector3> vertices;
		std::vector<WorldMeshTriangle> triangles;
	};
	class WorldMeshProjectionWorkspace final
	{
	  public:
		template <typename TTriangle>
		void Emit(const IndexedWorldMesh& mesh, const PreparedWorldProjection& projection,
		    TTriangle&& triangle)
		{
			// Reuse storage each frame and project each shared world vertex once.
			// Faces crossing the near plane are clipped, not dropped; dropping them
			// would cut holes into the floor right around the player.
			m_projected.resize(mesh.vertices.size());
			m_valid.resize(mesh.vertices.size());
			for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
			{
				m_valid[i] = projection.Project(mesh.vertices[i], &m_projected[i], false);
			}
			for (const auto& face : mesh.triangles)
			{
				const auto& indices = face.indices;
				if (m_valid[indices[0]] && m_valid[indices[1]] && m_valid[indices[2]])
				{
					triangle(m_projected[indices[0]], m_projected[indices[1]],
					    m_projected[indices[2]], face.color.data());
				}
				else
				{
					projection.EmitNearClippedTriangle(mesh.vertices[indices[0]],
					    mesh.vertices[indices[1]], mesh.vertices[indices[2]],
					    [&](const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c)
					    { triangle(a, b, c, face.color.data()); });
				}
			}
		}

	  private:
		std::vector<ScreenPoint> m_projected;
		std::vector<std::uint8_t> m_valid;
	};
} // namespace BaseballSimulatorVR::Common
