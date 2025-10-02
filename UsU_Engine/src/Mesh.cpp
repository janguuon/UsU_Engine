#include "Mesh.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <windows.h>
#include <cstdio>

using namespace DirectX;

void Mesh::SetDefaultTriangle()
{
    m_vertices = {
        { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT3(0,0,-1), XMFLOAT2(0,1) },
        { XMFLOAT3( 0.0f,  0.5f, 0.0f), XMFLOAT3(0,0,-1), XMFLOAT2(0.5f,0) },
        { XMFLOAT3( 0.5f, -0.5f, 0.0f), XMFLOAT3(0,0,-1), XMFLOAT2(1,1) }
    };
    m_indices = { 0,1,2 };
}

static std::string WStringToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], size, nullptr, nullptr);
    return out;
}

static bool ParseOBJ(const std::wstring& path,
                     std::vector<XMFLOAT3>& positions,
                     std::vector<XMFLOAT3>& normals,
                     std::vector<XMFLOAT2>& uvs,
                     std::vector<uint32_t>& outIndices,
                     std::vector<Vertex>& outVertices)
{
    std::ifstream file(WStringToUtf8(path).c_str());
    if (!file.is_open()) return false;

    std::vector<XMFLOAT3> tempPos;
    std::vector<XMFLOAT3> tempNrm;
    std::vector<XMFLOAT2> tempUV;

    std::string line;
    std::unordered_map<std::string, uint32_t> vertMap;
    outVertices.clear();
    outIndices.clear();

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string tag; iss >> tag;
        if (tag == "v") {
            float x,y,z; iss >> x >> y >> z; tempPos.push_back({x,y,z});
        } else if (tag == "vn") {
            float x,y,z; iss >> x >> y >> z; tempNrm.push_back({x,y,z});
        } else if (tag == "vt") {
            float u,v; iss >> u >> v; tempUV.push_back({u,v});
        } else if (tag == "f") {
            // supports triangles only
            std::string vstr[3];
            iss >> vstr[0] >> vstr[1] >> vstr[2];
            for (int i=0;i<3;++i) {
                auto it = vertMap.find(vstr[i]);
                if (it == vertMap.end()) {
                    // parse like pos/uv/nrm (indices are 1-based)
                    int pi=0, ti=0, ni=0;
                    sscanf_s(vstr[i].c_str(), "%d/%d/%d", &pi, &ti, &ni);
                    Vertex v{};
                    if (pi>0 && pi<= (int)tempPos.size()) v.position = tempPos[pi-1];
                    if (ni>0 && ni<= (int)tempNrm.size()) v.normal = tempNrm[ni-1];
                    if (ti>0 && ti<= (int)tempUV.size()) v.uv = tempUV[ti-1];
                    uint32_t newIndex = (uint32_t)outVertices.size();
                    outVertices.push_back(v);
                    vertMap[vstr[i]] = newIndex;
                    outIndices.push_back(newIndex);
                } else {
                    outIndices.push_back(it->second);
                }
            }
        }
    }
    return !outVertices.empty();
}

bool Mesh::LoadOBJ(const std::wstring& path)
{
    std::vector<XMFLOAT3> positions, normals;
    std::vector<XMFLOAT2> uvs;
    m_vertices.clear();
    m_indices.clear();
    if (!ParseOBJ(path, positions, normals, uvs, m_indices, m_vertices))
        return false;
    return true;
}
