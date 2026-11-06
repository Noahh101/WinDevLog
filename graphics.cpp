#include "graphics.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

ID3D11Device* d3dDevice = nullptr;
ID3D11DeviceContext* d3dContext = nullptr;
IDXGISwapChain* swapChain = nullptr;
ID3D11RenderTargetView* renderTargetView = nullptr;
bool imguiInitialized = false;

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (swapChain != nullptr &&
        SUCCEEDED(swapChain->GetBuffer(
            0, IID_PPV_ARGS(&backBuffer))))
    {
        d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        backBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (renderTargetView != nullptr)
    {
        renderTargetView->Release();
        renderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &swapChainDesc, &swapChain, &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(result))
    {
        CleanupDeviceD3D();
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &swapChainDesc, &swapChain, &d3dDevice, &featureLevel, &d3dContext);
        if (FAILED(result))
        {
            CleanupDeviceD3D();
            return false;
        }
    }

    CreateRenderTarget();
    if (renderTargetView == nullptr)
    {
        CleanupDeviceD3D();
        return false;
    }
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (swapChain != nullptr)
    {
        swapChain->Release();
        swapChain = nullptr;
    }
    if (d3dContext != nullptr)
    {
        d3dContext->Release();
        d3dContext = nullptr;
    }
    if (d3dDevice != nullptr)
    {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }
}
