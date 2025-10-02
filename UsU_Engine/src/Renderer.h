#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include "Mesh.h"

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

struct alignas(256) PerObjectCB
{
    DirectX::XMFLOAT4X4 mvp;
};

class Renderer
{
public:
    bool Initialize(ID3D12Device* device);
    bool CreatePipeline(const wchar_t* shaderFile);
    bool UploadMesh(const Mesh& mesh);
    void UpdateCB(const DirectX::XMFLOAT4X4& mvp);
    void RecordDraw(ID3D12GraphicsCommandList* cmdList, UINT indexCount);
    UINT GetIndexCount() const { return m_indexCount; }

private:
    bool CreateBuffer(size_t byteSize, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, ComPtr<ID3D12Resource>& out);

private:
    ID3D12Device* m_device = nullptr;
    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;

    // Buffers (upload heap for simplicity)
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView{};
    D3D12_INDEX_BUFFER_VIEW  m_ibView{};
    UINT m_indexCount = 0;

    // Constant buffer (upload)
    ComPtr<ID3D12Resource> m_cb;
    PerObjectCB* m_cbMapped = nullptr;
};
