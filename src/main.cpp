// ---------------------------------------------------------------------------
//  FPS Booster - otimizador de PC
//  Janela Win32 + DirectX 11 + Dear ImGui
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "cleaner.h"
#include "menu.h"
#include "theme.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

// Direct3D
static ID3D11Device*           g_device            = nullptr;
static ID3D11DeviceContext*    g_deviceContext     = nullptr;
static IDXGISwapChain*         g_swapChain         = nullptr;
static ID3D11RenderTargetView* g_renderTarget      = nullptr;
static bool                    g_swapChainOccluded = false;
static UINT                    g_resizeWidth = 0, g_resizeHeight = 0;

static bool CreateDeviceD3D(HWND window);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    const int width  = static_cast<int>(920 * scale);
    const int height = static_cast<int>(580 * scale);
    const int left   = (::GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int top    = (::GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    WNDCLASSEXW windowClass = { sizeof(windowClass), CS_CLASSDC, WndProc, 0L, 0L,
                                ::GetModuleHandle(nullptr), nullptr, ::LoadCursor(nullptr, IDC_ARROW),
                                nullptr, nullptr, L"BoostMenu", nullptr };
    ::RegisterClassExW(&windowClass);

    HWND window = ::CreateWindowExW(WS_EX_APPWINDOW, windowClass.lpszClassName, L"FPS Booster",
                                    WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                                    left, top, width, height,
                                    nullptr, nullptr, windowClass.hInstance, nullptr);

    if (window == nullptr || !CreateDeviceD3D(window))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        return 1;
    }

    ::ShowWindow(window, SW_SHOW);
    ::UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    initMenu(scale);

    AppWindow app;
    app.handle = window;
    MSG  message;
    bool wasMinimized = false;

    while (app.running)
    {
        while (::PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&message);
            ::DispatchMessage(&message);
            if (message.message == WM_QUIT)
                app.running = false;
        }
        if (!app.running)
            break;

        // Minimizado: nao desenha nada e devolve ao Windows a memoria que o
        // programa nao esta usando. Ela volta sozinha quando a janela reaparece.
        const bool minimized = (::IsIconic(window) != FALSE);
        if (minimized != wasMinimized)
        {
            wasMinimized = minimized;
            if (minimized)
                ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
        }
        if (minimized)
        {
            ::Sleep(20);
            continue;
        }

        // Janela coberta por outra: economiza CPU
        if (g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;

        if (g_resizeWidth != 0 && g_resizeHeight != 0)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        drawMenu(app);

        ImGui::Render();
        const float clearColor[4] = { g_theme.background.x, g_theme.background.y, g_theme.background.z, 1.0f };
        g_deviceContext->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_deviceContext->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT present = g_swapChain->Present(1, 0);
        g_swapChainOccluded = (present == DXGI_STATUS_OCCLUDED);
    }

    shutdownCleaner();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(window);
    ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}

// ---------------------------------------------------------------------------
//  Direct3D
// ---------------------------------------------------------------------------
static bool CreateDeviceD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC description = {};
    description.BufferCount                        = 2;
    description.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator   = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    description.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow                       = window;
    description.SampleDesc.Count                   = 1;
    description.Windowed                           = TRUE;
    description.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL selectedLevel;

    HRESULT result = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                     levels, 2, D3D11_SDK_VERSION, &description,
                                                     &g_swapChain, &g_device, &selectedLevel, &g_deviceContext);
    if (result == DXGI_ERROR_UNSUPPORTED)
        result = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                 levels, 2, D3D11_SDK_VERSION, &description,
                                                 &g_swapChain, &g_device, &selectedLevel, &g_deviceContext);
    if (result != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain)     { g_swapChain->Release();     g_swapChain = nullptr; }
    if (g_deviceContext) { g_deviceContext->Release(); g_deviceContext = nullptr; }
    if (g_device)        { g_device->Release();        g_device = nullptr; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer == nullptr)
        return;

    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
}

// ---------------------------------------------------------------------------
//  Mensagens da janela
// ---------------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_resizeWidth  = static_cast<UINT>(LOWORD(lParam));
        g_resizeHeight = static_cast<UINT>(HIWORD(lParam));
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)    // desativa o menu do ALT
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
