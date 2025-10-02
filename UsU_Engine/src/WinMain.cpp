#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <string>
#include <DirectXMath.h>
#include "Renderer.h"
#include "Mesh.h"
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

// Globals
static const UINT kFrameCount = 2;
HWND g_hWnd = nullptr;
UINT g_width = 1280;
UINT g_height = 720;

ComPtr<ID3D12Device> g_device;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT g_rtvDescriptorSize = 0;
ComPtr<ID3D12Resource> g_renderTargets[kFrameCount];
ComPtr<ID3D12CommandAllocator> g_commandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12Fence> g_fence;
UINT64 g_fenceValue = 0;
HANDLE g_fenceEvent = nullptr;
UINT g_frameIndex = 0;

// App-level renderer/mesh
Renderer g_renderer;
Mesh     g_mesh;

static std::wstring GetExecutableDir()
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path, len);
    size_t pos = p.find_last_of(L"/\\");
    if (pos != std::wstring::npos) return p.substr(0, pos);
    return L".";
}

static bool Exists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ResolveAssetPath(const std::wstring& exeDir, const std::wstring& rel)
{
    // Try exeDir\rel, exeDir\..\rel, exeDir\..\..\rel
    std::wstring cands[3] = {
        exeDir + L"\\" + rel,
        exeDir + L"\\..\\" + rel,
        exeDir + L"\\..\\..\\" + rel
    };
    for (auto& c : cands) {
        if (Exists(c)) return c;
    }
    return cands[0];
}

void ThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    assert(false);
    PostQuitMessage(1);
  }
}

void SignalAndWaitForGPU() {
  const UINT64 fenceToWaitFor = ++g_fenceValue;
  ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceToWaitFor));
  if (g_fence->GetCompletedValue() < fenceToWaitFor) {
    ThrowIfFailed(g_fence->SetEventOnCompletion(fenceToWaitFor, g_fenceEvent));
    WaitForSingleObject(g_fenceEvent, INFINITE);
  }
}

  void WaitForGPU() {
    const UINT64 fenceToWaitFor = ++g_fenceValue;
    ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceToWaitFor));
    if (g_fence->GetCompletedValue() < fenceToWaitFor) {
      ThrowIfFailed(g_fence->SetEventOnCompletion(fenceToWaitFor, g_fenceEvent));
      WaitForSingleObject(g_fenceEvent, INFINITE);
    }
  }

  void CreateDeviceAndSwapchain() {
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    {
      ComPtr<ID3D12Debug> debugController;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
      }
    }
#endif

    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    // Create device (pick first hardware adapter that supports D3D12)
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc = {};
      adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)))) {
        break;
      }
    }
    if (!g_device) {
      ComPtr<IDXGIAdapter> warp;
      ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
      ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
    }

    // Command queue
    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(g_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&g_commandQueue)));

    // Swapchain
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = kFrameCount;
    scDesc.Width = g_width;
    scDesc.Height = g_height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swap1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(g_commandQueue.Get(), g_hWnd, &scDesc, nullptr, nullptr, &swap1));
    ThrowIfFailed(factory->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swap1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Back buffers and RTVs
    {
      D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
      for (UINT n = 0; n < kFrameCount; ++n) {
        ThrowIfFailed(g_swapChain->GetBuffer(n, IID_PPV_ARGS(&g_renderTargets[n])));
        g_device->CreateRenderTargetView(g_renderTargets[n].Get(), nullptr, rtv);
        rtv.ptr += static_cast<SIZE_T>(g_rtvDescriptorSize);
      }
    }

    // Command allocator/list
    ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator)));
    ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList)));
    ThrowIfFailed(g_commandList->Close());

    // Fence and event
    ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValue = 0;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }

  void Resize(UINT width, UINT height) {
    if (!g_swapChain || width == 0 || height == 0) return;
    WaitForGPU();

    for (UINT n = 0; n < kFrameCount; ++n) {
      g_renderTargets[n].Reset();
    }

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    ThrowIfFailed(g_swapChain->GetDesc(&scDesc));
    ThrowIfFailed(g_swapChain->ResizeBuffers(kFrameCount, width, height, scDesc.BufferDesc.Format, scDesc.Flags));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < kFrameCount; ++n) {
      ThrowIfFailed(g_swapChain->GetBuffer(n, IID_PPV_ARGS(&g_renderTargets[n])));
      g_device->CreateRenderTargetView(g_renderTargets[n].Get(), nullptr, rtv);
      rtv.ptr += static_cast<SIZE_T>(g_rtvDescriptorSize);
    }

    g_width = width;
    g_height = height;
  }

  void PopulateCommandList() {
    // Ensure the previous frame finished before we reset the allocator
    SignalAndWaitForGPU();
    ThrowIfFailed(g_commandAllocator->Reset());
    ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), nullptr));
    
    // Viewport & Scissor
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width  = static_cast<float>(g_width);
    viewport.Height = static_cast<float>(g_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_commandList->RSSetViewports(1, &viewport);

    RECT scissor{ 0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height) };
    g_commandList->RSSetScissorRects(1, &scissor);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(g_frameIndex) * static_cast<SIZE_T>(g_rtvDescriptorSize);

    // Bind render target
    g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear to blue
    float clearColor[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Update simple MVP (identity)
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMStoreFloat4x4(&mvp, DirectX::XMMatrixIdentity());
    g_renderer.UpdateCB(mvp);

    // Record draw
    if (g_renderer.GetIndexCount() > 0) {
        g_renderer.RecordDraw(g_commandList.Get(), g_renderer.GetIndexCount());
    }

    // Transition back to present
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(g_commandList->Close());
  }

  void Render() {
    PopulateCommandList();
    ID3D12CommandList* ppCommandLists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    ThrowIfFailed(g_swapChain->Present(1, 0));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
  }

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    } break;
    case WM_SIZE: {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        Resize(w, h);
    } break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, INT nCmdShow) {
    // Register class
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"UsUEngineWindowClass";
    RegisterClassExW(&wcex);

    RECT rc = { 0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height) };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hWnd = CreateWindowW(L"UsUEngineWindowClass", L"UsU Engine (DX12)", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hWnd, nCmdShow);

    CreateDeviceAndSwapchain();

    // Initialize renderer and load a simple mesh
    if (!g_renderer.Initialize(g_device.Get())) {
        PostQuitMessage(1);
        return 0;
    }
    std::wstring exeDir = GetExecutableDir();
    std::wstring shaderPath = ResolveAssetPath(exeDir, L"src\\shaders.hlsl");
    if (!g_renderer.CreatePipeline(shaderPath.c_str())) {
        PostQuitMessage(1);
        return 0;
    }
    // Try load sample OBJ; fallback to triangle
    std::wstring objPath = ResolveAssetPath(exeDir, L"assets\\mesh\\cube.obj");
    if (!g_mesh.LoadOBJ(objPath)) {
        g_mesh.SetDefaultTriangle();
    }
    if (!g_renderer.UploadMesh(g_mesh)) {
        PostQuitMessage(1);
        return 0;
    }

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Render();
        }
    }

    WaitForGPU();
    CloseHandle(g_fenceEvent);

    return 0;
}
