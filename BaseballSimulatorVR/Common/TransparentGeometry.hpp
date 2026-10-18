#pragma once

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace BaseballSimulatorVR::Common
{
    // Vertex.position[2] is the normalized depth emitted by the shared world
    // projection. Larger values are farther from the camera.
    template <typename TVertex>
    void SortTransparentTrianglesBackToFront(std::vector<TVertex>& vertices)
    {
        if (vertices.size() % 3 != 0)
            throw std::logic_error("Transparent geometry must contain complete triangles");
        struct Triangle final { std::array<TVertex,3> vertices; float depth; };
        std::vector<Triangle> triangles;
        triangles.reserve(vertices.size()/3);
        for (std::size_t i=0;i<vertices.size();i+=3)
            triangles.push_back({{vertices[i],vertices[i+1],vertices[i+2]},
                (vertices[i].position[2]+vertices[i+1].position[2]+vertices[i+2].position[2])/3.0F});
        std::stable_sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b) { return a.depth>b.depth; });
        std::size_t index=0;
        for (const auto& triangle : triangles)
            for (const auto& vertex : triangle.vertices) vertices[index++]=vertex;
    }
}
