#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

class Mesh
{
public:
    bool LoadOBJ(const std::wstring& path);

    const std::vector<Vertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32_t>& GetIndices()  const { return m_indices; }

    void SetDefaultTriangle();

private:
    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
};
