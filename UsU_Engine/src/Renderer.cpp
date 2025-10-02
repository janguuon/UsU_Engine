#include "Renderer.h"
#include <stdexcept>
#include <windows.h>
using namespace DirectX;

bool Renderer::Initialize(ID3D12Device* device)
{
    m_device = device;

    // Create constant buffer (upload heap, 256-byte aligned)
    const UINT cbSize = (sizeof(PerObjectCB) + 255) & ~255u;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = cbSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cb))))
        return false;

    // Map once
    if (FAILED(m_cb->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped))))
        return false;

    return true;
}

bool Renderer::CreatePipeline(const wchar_t* shaderFile)
{
    // Compile shaders
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    HRESULT hrVS = D3DCompileFromFile(shaderFile, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vsBlob, &err);
    if (FAILED(hrVS)) {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        OutputDebugStringW(L"[DX12] VS compile failed: ");
        OutputDebugStringW(shaderFile);
        OutputDebugStringW(L"\n");
        // MessageBoxW(nullptr, shaderFile, L"VS compile failed", MB_OK);
        return false;
    }
    err.Reset();
    HRESULT hrPS = D3DCompileFromFile(shaderFile, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &psBlob, &err);
    if (FAILED(hrPS)) {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        OutputDebugStringW(L"[DX12] PS compile failed: ");
        OutputDebugStringW(shaderFile);
        OutputDebugStringW(L"\n");
        // MessageBoxW(nullptr, shaderFile, L"PS compile failed", MB_OK);
        return false;
    }

    // Root signature: one root CBV at b0
    D3D12_ROOT_PARAMETER rp{};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rp.Descriptor.ShaderRegister = 0; // b0

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rp;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, sigErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sigErr)))
        return false;
    if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig))))
        return false;

    // Input layout
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, uv),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSig.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = FALSE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    psoDesc.BlendState = blend;
    psoDesc.SampleMask = UINT_MAX;
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    // Disable culling to avoid missing faces due to OBJ winding differences
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;
    rast.ForcedSampleCount = 0;
    rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.RasterizerState = rast;
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    ds.StencilEnable = FALSE;
    psoDesc.DepthStencilState = ds;
    psoDesc.InputLayout = { layout, _countof(layout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso))))
        return false;

    return true;
}

bool Renderer::CreateBuffer(size_t byteSize, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&out)));
}

bool Renderer::UploadMesh(const Mesh& mesh)
{
    const auto& vertices = mesh.GetVertices();
    const auto& indices  = mesh.GetIndices();
    if (vertices.empty() || indices.empty()) return false;

    const size_t vbBytes = vertices.size() * sizeof(Vertex);
    const size_t ibBytes = indices.size()  * sizeof(uint32_t);

    // For simplicity, keep both in UPLOAD heap and bind directly
    if (!CreateBuffer(vbBytes, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, m_vertexBuffer)) return false;
    if (!CreateBuffer(ibBytes, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, m_indexBuffer)) return false;

    // Upload
    void* p = nullptr;
    if (FAILED(m_vertexBuffer->Map(0, nullptr, &p))) return false;
    memcpy(p, vertices.data(), vbBytes);
    m_vertexBuffer->Unmap(0, nullptr);

    if (FAILED(m_indexBuffer->Map(0, nullptr, &p))) return false;
    memcpy(p, indices.data(), ibBytes);
    m_indexBuffer->Unmap(0, nullptr);

    // Views
    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.StrideInBytes = sizeof(Vertex);
    m_vbView.SizeInBytes = static_cast<UINT>(vbBytes);

    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibView.Format = DXGI_FORMAT_R32_UINT;
    m_ibView.SizeInBytes = static_cast<UINT>(ibBytes);

    m_indexCount = static_cast<UINT>(indices.size());
    return true;
}

void Renderer::UpdateCB(const XMFLOAT4X4& mvp)
{
    if (!m_cbMapped) return;
    m_cbMapped->mvp = mvp;
}

void Renderer::RecordDraw(ID3D12GraphicsCommandList* cmdList, UINT indexCount)
{
    cmdList->SetGraphicsRootSignature(m_rootSig.Get());
    cmdList->SetPipelineState(m_pso.Get());

    // Root CBV (as root descriptor)
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_cb->GetGPUVirtualAddress();
    cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

    // IA
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    cmdList->IASetIndexBuffer(&m_ibView);

    cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
