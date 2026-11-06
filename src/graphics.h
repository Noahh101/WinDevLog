#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

extern ID3D11Device* d3dDevice;
extern ID3D11DeviceContext* d3dContext;
extern IDXGISwapChain* swapChain;
extern ID3D11RenderTargetView* renderTargetView;
extern bool imguiInitialized;

bool CreateDeviceD3D(HWND window);
void CreateRenderTarget();
void CleanupDeviceD3D();
void CleanupRenderTarget();
