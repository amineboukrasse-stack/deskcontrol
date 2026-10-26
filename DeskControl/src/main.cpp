// DeskControl - entry point: window, Direct3D 11 swap chain, tray icon, main loop.
#include "platform.h"

#include <d3d11.h>
#include <shellapi.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "textfocus.h"
#include "ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

const wchar_t* kClassName = L"DeskControlWindow";
const wchar_t* kMutexName = L"DeskControl_SingleInstance";
const UINT kTrayId = 1;
const UINT kCmdOpen = 1, kCmdToggle = 2, kCmdExit = 3, kCmdKeyboard = 4, kCmdProfileBase = 100;

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

HWND g_hwnd = nullptr;
UI* g_ui = nullptr;
bool g_visible = false;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_nid = {};
std::wstring g_lastTip;
float g_dpi = 1.0f;

void createRenderTarget() {
  ID3D11Texture2D* back = nullptr;
  if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
  }
}

void cleanupRenderTarget() {
  if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool createDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
  if (hr == DXGI_ERROR_UNSUPPORTED)   // no GPU / remote session: fall back to the software rasteriser
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
  if (FAILED(hr)) return false;
  createRenderTarget();
  return true;
}

void cleanupDevice() {
  cleanupRenderTarget();
  if (g_swap) { g_swap->Release(); g_swap = nullptr; }
  if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
  if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

void showWindow(bool show) {
  g_visible = show;
  if (g_ui) g_ui->setWindowVisible(show);
  if (show) {
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
  } else {
    ShowWindow(g_hwnd, SW_HIDE);
  }
}

void addTrayIcon() {
  g_nid = {};
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_hwnd;
  g_nid.uID = kTrayId;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WM_DESKCONTROL_TRAY;
  g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  lstrcpynW(g_nid.szTip, L"DeskControl", 127);
  Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void updateTrayTip() {
  if (!g_ui) return;
  std::wstring tip = Platform::toWide(g_ui->trayTooltip());
  if (tip.size() > 120) tip.resize(120);
  if (tip == g_lastTip) return;
  g_lastTip = tip;
  lstrcpynW(g_nid.szTip, tip.c_str(), 127);
  g_nid.uFlags = NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void showTrayMenu() {
  HMENU menu = CreatePopupMenu();
  HMENU profiles = CreatePopupMenu();
  const auto& names = g_ui->profileNames();
  for (size_t i = 0; i < names.size(); ++i)
    AppendMenuW(profiles, MF_STRING | (names[i] == g_ui->activeProfile() ? MF_CHECKED : 0), kCmdProfileBase + i, Platform::toWide(names[i]).c_str());

  AppendMenuW(menu, MF_STRING, kCmdOpen, g_visible ? L"Hide window" : L"Open DeskControl");
  AppendMenuW(menu, MF_STRING | (g_ui->enabled() ? MF_CHECKED : 0), kCmdToggle, L"DeskControl enabled");
  AppendMenuW(menu, MF_STRING, kCmdKeyboard, L"On-screen keyboard");
  AppendMenuW(menu, MF_POPUP, (UINT_PTR)profiles, L"Profile");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_hwnd);        // required so the menu closes when clicking elsewhere
  const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_hwnd, nullptr);
  PostMessageW(g_hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);                  // also destroys the submenu

  if (cmd == kCmdOpen) showWindow(!g_visible);
  else if (cmd == kCmdToggle) g_ui->setEnabled(!g_ui->enabled());
  else if (cmd == kCmdKeyboard) Osk::toggle();
  else if (cmd == kCmdExit) g_ui->requestQuit();
  else if (cmd >= kCmdProfileBase && cmd - kCmdProfileBase < names.size()) g_ui->selectProfile(names[cmd - kCmdProfileBase]);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

  if (msg == g_taskbarCreated && g_taskbarCreated) { addTrayIcon(); g_lastTip.clear(); return 0; }

  switch (msg) {
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      mm->ptMinTrackSize.x = (LONG)(900 * g_dpi);
      mm->ptMinTrackSize.y = (LONG)(620 * g_dpi);
      return 0;
    }
    case WM_SIZE:
      if (g_dev && wp != SIZE_MINIMIZED) {
        cleanupRenderTarget();
        g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        createRenderTarget();
      }
      return 0;
    case WM_SYSCOMMAND:
      if ((wp & 0xFFF0) == SC_KEYMENU) return 0;   // no ALT menu beep
      break;
    case WM_CLOSE:
      if (g_ui && g_ui->closeToTray()) showWindow(false);
      else if (g_ui) g_ui->requestQuit();
      return 0;
    case WM_DPICHANGED: {
      const RECT* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_DESKCONTROL_EVENT:
      if ((int)wp == EV_TOGGLE_WINDOW) showWindow(!g_visible);
      else if (g_ui) g_ui->onEvent((int)wp);
      return 0;
    case WM_DESKCONTROL_SHOW:
      showWindow(true);
      return 0;
    case WM_DESKCONTROL_TRAY:
      if (LOWORD(lp) == WM_LBUTTONUP) showWindow(!g_visible);
      else if (LOWORD(lp) == WM_RBUTTONUP && g_ui) showTrayMenu();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  bool startHidden = false, elevatedRestart = false;
  {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
      if (wcscmp(argv[i], L"--minimized") == 0) startHidden = true;
      if (wcscmp(argv[i], L"--elevated") == 0) elevatedRestart = true;
    }
    if (argv) LocalFree(argv);
  }

  // Single instance. After "restart as administrator" the old process may still be shutting down.
  HANDLE mutex = nullptr;
  for (int attempt = 0; attempt < (elevatedRestart ? 40 : 1); ++attempt) {
    mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    CloseHandle(mutex);
    mutex = nullptr;
    Sleep(100);
  }
  if (!mutex) {
    HWND other = FindWindowW(kClassName, nullptr);
    if (other) PostMessageW(other, WM_DESKCONTROL_SHOW, 0, 0);
    return 0;
  }

  ImGui_ImplWin32_EnableDpiAwareness();
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = wndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
  g_hwnd = CreateWindowExW(0, kClassName, L"DeskControl", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           (int)(1060 * g_dpi), (int)(720 * g_dpi), nullptr, nullptr, hInst, nullptr);
  if (!g_hwnd) return 1;
  g_dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(g_hwnd);

  if (!createDevice(g_hwnd)) {
    MessageBoxW(nullptr, L"DeskControl could not start Direct3D 11 graphics.", L"DeskControl", MB_ICONERROR);
    cleanupDevice();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(g_dev, g_ctx);

  UI ui;
  g_ui = &ui;
  ui.init(g_hwnd, g_dpi);
  addTrayIcon();

  if (startHidden || ui.startMinimized()) {
    g_visible = false;
    ui.setWindowVisible(false);
  } else {
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    g_visible = true;
  }

  const float clear[4] = {0.067f, 0.075f, 0.094f, 1.0f};
  bool done = false;
  while (!done) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done || ui.wantQuit()) break;

    ui.tick();
    updateTrayTip();

    if (!g_visible || IsIconic(g_hwnd)) {            // nothing to draw: sleep until input or 50 ms
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ui.frame();
    ImGui::Render();

    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (g_swap->Present(1, 0) == DXGI_STATUS_OCCLUDED) Sleep(20);
  }

  ui.shutdown();
  g_ui = nullptr;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  cleanupDevice();
  DestroyWindow(g_hwnd);
  UnregisterClassW(kClassName, hInst);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
