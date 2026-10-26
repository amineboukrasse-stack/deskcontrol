#include "keyboard.h"

#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <set>
#include <string>
#include <vector>

#include "kbdlayout.h"

namespace {

const wchar_t* kClassName = L"DeskControlKeyboardWindow";
enum : UINT_PTR { ID_REPEAT = 1, ID_POLL = 2 };
const UINT kRepeatDelayMs = 450;
const UINT kRepeatRateMs = 40;

const COLORREF kBg = RGB(22, 25, 32);
const COLORREF kHeader = RGB(30, 34, 43);
const COLORREF kBorder = RGB(58, 64, 80);
const COLORREF kKey = RGB(46, 52, 66);
const COLORREF kKeyDim = RGB(36, 41, 53);
const COLORREF kKeyHover = RGB(66, 75, 96);
const COLORREF kText = RGB(232, 235, 242);
const COLORREF kTextDim = RGB(140, 149, 168);

COLORREF mix(COLORREF a, COLORREF b, int pctB) {
  auto m = [&](int x, int y) { return (x * (100 - pctB) + y * pctB) / 100; };
  return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}

float screenDpi() {
  HDC dc = GetDC(nullptr);
  const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc) ReleaseDC(nullptr, dc);
  return d > 0 ? d / 96.0f : 1.0f;
}

}  // namespace

// ------------------------------------------------------------------ window state
struct Keyboard::Impl {
  Keyboard* owner = nullptr;
  HWND hwnd = nullptr;
  float dpi = 1.0f;

  HFONT fontKey = nullptr, fontSmall = nullptr, fontHeader = nullptr;
  int fontU = 0;

  int hover = -1;                  // key under the cursor
  int down = -1;                   // key held down by the mouse
  bool closeHover = false, closeDown = false;
  bool tracking = false;
  bool placed = false;
  std::set<uint16_t> latched;      // sticky Shift / Ctrl / Alt waiting for the next key

  std::vector<std::wstring> labels;
  bool lastShift = false, lastCaps = false;
  HKL lastLayout = nullptr;

  struct Geo { float u, ox, oy; int hdr, pad; RECT closeBtn; };

  ~Impl() {
    if (fontKey) DeleteObject(fontKey);
    if (fontSmall) DeleteObject(fontSmall);
    if (fontHeader) DeleteObject(fontHeader);
  }

  Geo geo() const {
    RECT c;
    GetClientRect(hwnd, &c);
    Geo g;
    g.hdr = (int)(30 * dpi);
    g.pad = (int)(10 * dpi);
    const float availW = (float)(c.right - c.left - 2 * g.pad);
    const float availH = (float)(c.bottom - c.top - g.hdr - 2 * g.pad);
    g.u = std::max(8.0f, std::min(availW / kbWidth(), availH / kbHeight()));
    g.ox = c.left + ((c.right - c.left) - g.u * kbWidth()) * 0.5f;
    g.oy = c.top + g.hdr + g.pad + (availH - g.u * kbHeight()) * 0.5f;
    const int bw = (int)(g.hdr * 1.4f);
    g.closeBtn = RECT{c.right - bw, 0, c.right, g.hdr};
    return g;
  }

  // Full cell of a key (including the gap around it) - used for hit testing and for D-pad targets.
  RECT cell(const Geo& g, int i) const {
    const KbKey& k = kbKeys()[i];
    return RECT{(LONG)(g.ox + k.x * g.u), (LONG)(g.oy + k.y * g.u),
                (LONG)(g.ox + (k.x + k.w) * g.u), (LONG)(g.oy + (k.y + k.h) * g.u)};
  }

  int hitKey(const Geo& g, POINT pt) const {
    const int n = (int)kbKeys().size();
    for (int i = 0; i < n; ++i) {
      RECT r = cell(g, i);
      if (PtInRect(&r, pt)) return i;
    }
    return -1;
  }

  // ---------------------------------------------------------------- captions
  static HKL foregroundLayout() {
    HWND fg = GetForegroundWindow();
    const DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    return GetKeyboardLayout(tid);
  }

  static std::wstring caption(const KbKey& k, bool shift, bool caps, HKL layout) {
    if (k.label) return Platform::toWide(k.label);
    BYTE ks[256] = {};
    if (shift) ks[VK_SHIFT] = ks[VK_LSHIFT] = 0x80;
    if (caps) ks[VK_CAPITAL] = 1;
    WCHAR buf[8] = {};
    const UINT sc = MapVirtualKeyExW(k.vk, MAPVK_VK_TO_VSC, layout);
    // Flag 0x4: do not change the keyboard state (dead keys), Windows 10 1607 and newer.
    const int n = ToUnicodeEx(k.vk, sc, ks, buf, 8, 0x4, layout);
    if ((n == 1 || n == -1) && buf[0] >= 0x20) return std::wstring(1, buf[0]);
    return Platform::toWide(shift ? k.hi : k.lo);
  }

  bool shiftOn() const { return latched.count(0xA0) || latched.count(0xA1); }

  // Re-reads captions when Shift, Caps Lock or the keyboard layout changed. True if anything changed.
  bool refreshLabels(bool force) {
    const bool shift = shiftOn();
    const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    const HKL layout = foregroundLayout();
    if (!force && shift == lastShift && caps == lastCaps && layout == lastLayout && !labels.empty()) return false;
    lastShift = shift; lastCaps = caps; lastLayout = layout;
    const auto& keys = kbKeys();
    labels.resize(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) labels[i] = caption(keys[i], shift, caps, layout);
    return true;
  }

  void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

  // ---------------------------------------------------------------- typing
  void releaseLatched() {
    if (latched.empty()) return;
    for (uint16_t vk : latched) Platform::sendKey(vk, false);
    latched.clear();
    refreshLabels(true);
    redraw();
  }

  void tap(uint16_t vk) {
    Platform::sendKey(vk, true);
    Platform::sendKey(vk, false);
  }

  void pressKey(int idx) {
    const uint16_t vk = kbKeys()[idx].vk;
    if (kbIsModifier(vk)) {                       // tap to latch, tap again to let go
      if (latched.count(vk)) { Platform::sendKey(vk, false); latched.erase(vk); }
      else { Platform::sendKey(vk, true); latched.insert(vk); }
      refreshLabels(true);
      redraw();
      return;
    }
    if (kbIsWinKey(vk) || vk == 0x14) {           // Win / Caps Lock: a single tap
      tap(vk);
      releaseLatched();
      refreshLabels(true);
      redraw();
      return;
    }
    down = idx;
    Platform::sendKey(vk, true);
    SetTimer(hwnd, ID_REPEAT, kRepeatDelayMs, nullptr);
    redraw();
  }

  void releaseKey() {
    if (down < 0) return;
    KillTimer(hwnd, ID_REPEAT);
    Platform::sendKey(kbKeys()[down].vk, false);
    down = -1;
    releaseLatched();
    redraw();
  }

  void releaseAll() {
    releaseKey();
    releaseLatched();
  }

  // ---------------------------------------------------------------- window management
  void place() {
    POINT cur = {0, 0};
    GetCursorPos(&cur);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT wa = mi.rcWork;
    dpi = screenDpi();
    const int pad = (int)(10 * dpi), hdr = (int)(30 * dpi);
    const int workW = wa.right - wa.left;
    int w = std::min((int)(workW * 0.62f), (int)(1000 * dpi));
    w = std::max(w, (int)(640 * dpi));
    w = std::min(w, workW - 20);
    const float u = (float)(w - 2 * pad) / kbWidth();
    const int h = hdr + 2 * pad + (int)(u * kbHeight());
    const int x = wa.left + (workW - w) / 2;
    const int y = wa.bottom - h - (int)(16 * dpi);
    SetWindowPos(hwnd, HWND_TOPMOST, x, std::max<int>(wa.top, y), w, h, SWP_NOACTIVATE);
    placed = true;
  }

  void show() {
    dpi = screenDpi();
    if (!placed) place();
    refreshLabels(true);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    owner->visible_ = true;
    SetTimer(hwnd, ID_POLL, 400, nullptr);
    redraw();
  }

  void hide() {
    releaseAll();
    KillTimer(hwnd, ID_POLL);
    ShowWindow(hwnd, SW_HIDE);
    owner->visible_ = false;
    hover = -1;
  }

  void padStep(int dx, int dy) {
    POINT pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(hwnd, &pt);
    const Geo g = geo();
    int target;
    const int cur = hitKey(g, pt);
    if (cur < 0) target = kbNearest((pt.x - g.ox) / g.u, (pt.y - g.oy) / g.u);   // first step: land on a key
    else target = kbNavigate(cur, dx, dy);
    if (target < 0) return;
    RECT r = cell(g, target);
    POINT c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ClientToScreen(hwnd, &c);
    SetCursorPos(c.x, c.y);
    hover = target;
    redraw();
  }

  void ensureFonts(float u) {
    const int key = std::max(10, (int)(u * 0.40f));
    if (fontKey && fontU == key) return;
    fontU = key;
    if (fontKey) DeleteObject(fontKey);
    if (fontSmall) DeleteObject(fontSmall);
    if (fontHeader) DeleteObject(fontHeader);
    auto make = [](int px, int weight) {
      return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    fontKey = make(key, FW_NORMAL);
    fontSmall = make(std::max(9, (int)(key * 0.68f)), FW_SEMIBOLD);
    fontHeader = make((int)(12 * dpi), FW_SEMIBOLD);
  }

  // ---------------------------------------------------------------- painting
  void fillRound(HDC dc, const RECT& r, COLORREF col, int radius) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
  }

  void outlineRound(HDC dc, const RECT& r, COLORREF col, int radius, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, col);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
  }

  void paint(HDC dc, const RECT& client) {
    const Geo g = geo();
    ensureFonts(g.u);
    if (labels.empty()) refreshLabels(true);
    const COLORREF accent = owner->accent_;
    const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    RECT hr = {client.left, client.top, client.right, client.top + g.hdr};
    HBRUSH hb = CreateSolidBrush(kHeader);
    FillRect(dc, &hr, hb);
    DeleteObject(hb);

    SetBkMode(dc, TRANSPARENT);

    // header: title, hint, close button
    SelectObject(dc, fontHeader);
    SetTextColor(dc, kText);
    RECT tr = {g.pad + 2, 0, g.closeBtn.left, g.hdr};
    const wchar_t* title = L"DeskControl keyboard";
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SIZE ts = {};
    GetTextExtentPoint32W(dc, title, (int)wcslen(title), &ts);
    SetTextColor(dc, kTextDim);
    RECT hint = {g.pad + 2 + ts.cx + (int)(18 * dpi), 0, g.closeBtn.left - g.pad, g.hdr};
    DrawTextW(dc, L"D-pad steps between keys · A types", -1, &hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (closeDown || closeHover) {
      RECT cb = g.closeBtn;
      cb.left += (int)(6 * dpi); cb.right -= (int)(6 * dpi); cb.top += (int)(4 * dpi); cb.bottom -= (int)(4 * dpi);
      fillRound(dc, cb, closeDown ? RGB(196, 64, 64) : RGB(70, 76, 94), (int)(8 * dpi));
    }
    {
      const int cxm = (g.closeBtn.left + g.closeBtn.right) / 2, cym = g.hdr / 2, d = (int)(5 * dpi);
      HPEN pen = CreatePen(PS_SOLID, std::max(1, (int)(1.6f * dpi)), (closeDown || closeHover) ? kText : kTextDim);
      HGDIOBJ op = SelectObject(dc, pen);
      MoveToEx(dc, cxm - d, cym - d, nullptr); LineTo(dc, cxm + d + 1, cym + d + 1);
      MoveToEx(dc, cxm + d, cym - d, nullptr); LineTo(dc, cxm - d - 1, cym + d + 1);
      SelectObject(dc, op);
      DeleteObject(pen);
    }

    // keys
    const int gap = std::max(2, (int)(g.u * 0.07f));
    const int radius = std::max(6, (int)(g.u * 0.22f));
    const auto& keys = kbKeys();
    for (size_t i = 0; i < keys.size(); ++i) {
      const KbKey& k = keys[i];
      RECT r = cell(g, (int)i);
      r.left += gap / 2; r.top += gap / 2; r.right -= gap - gap / 2; r.bottom -= gap - gap / 2;

      const bool isLatched = latched.count(k.vk) != 0;
      const bool isCaps = k.vk == 0x14 && caps;
      const bool isDown = (int)i == down;
      COLORREF col = (k.label && k.vk != 0x20) ? kKeyDim : kKey;   // character keys and Space are the brighter ones
      if ((int)i == hover) col = kKeyHover;
      if (isLatched || isCaps) col = mix(accent, kKey, 35);
      if (isDown) col = accent;
      fillRound(dc, r, col, radius);
      if ((int)i == hover && !isDown) outlineRound(dc, r, accent, radius, std::max(1, (int)(1.5f * dpi)));

      const std::wstring& text = i < labels.size() ? labels[i] : std::wstring();
      if (!text.empty()) {
        const bool big = text.size() == 1;
        SelectObject(dc, big ? fontKey : fontSmall);
        SetTextColor(dc, (isLatched || isCaps || isDown) ? RGB(255, 255, 255) : kText);
        DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }
    }

    // border
    RECT b = client;
    outlineRound(dc, RECT{b.left, b.top, b.right, b.bottom}, kBorder, (int)(14 * dpi), 1);
  }

  void onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT c;
    GetClientRect(hwnd, &c);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    paint(mem, c);
    BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
  }

  // ---------------------------------------------------------------- messages
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;                      // clicking a key must never steal focus from the app
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT:
        onPaint();
        return 0;
      case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = (LONG)(480 * dpi);
        mm->ptMinTrackSize.y = (LONG)(190 * dpi);
        return 0;
      }
      case WM_SIZE: {
        RECT c;
        GetClientRect(hwnd, &c);
        HRGN rgn = CreateRoundRectRgn(0, 0, c.right + 1, c.bottom + 1, (int)(14 * dpi), (int)(14 * dpi));
        SetWindowRgn(hwnd, rgn, TRUE);            // the system owns the region afterwards
        redraw();
        return 0;
      }
      case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT wr;
        GetWindowRect(hwnd, &wr);
        const Geo g = geo();
        POINT cp = pt;
        ScreenToClient(hwnd, &cp);
        if (PtInRect(&g.closeBtn, cp)) return HTCLIENT;
        const int e = std::max(4, (int)(7 * dpi));
        const bool L = pt.x < wr.left + e, R = pt.x >= wr.right - e, T = pt.y < wr.top + e, B = pt.y >= wr.bottom - e;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        if (cp.y < g.hdr) return HTCAPTION;
        return HTCLIENT;
      }
      case WM_NCLBUTTONDOWN:
      case WM_NCLBUTTONDBLCLK:
        if (msg == WM_NCLBUTTONDBLCLK && wp == HTCAPTION) return 0;
        break;
      case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!tracking) {
          TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
          TrackMouseEvent(&t);
          tracking = true;
        }
        const Geo g = geo();
        const int h = hitKey(g, pt);
        const bool ch = PtInRect(&g.closeBtn, pt) != 0;
        if (h != hover || ch != closeHover) { hover = h; closeHover = ch; redraw(); }
        return 0;
      }
      case WM_MOUSELEAVE:
        tracking = false;
        if (hover != -1 || closeHover) { hover = -1; closeHover = false; redraw(); }
        return 0;
      case WM_LBUTTONDOWN:
      case WM_LBUTTONDBLCLK: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const Geo g = geo();
        SetCapture(hwnd);
        if (PtInRect(&g.closeBtn, pt)) { closeDown = true; redraw(); return 0; }
        const int k = hitKey(g, pt);
        if (k >= 0) { releaseKey(); pressKey(k); }
        return 0;
      }
      case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const Geo g = geo();
        const bool wasClose = closeDown;
        closeDown = false;
        releaseKey();
        if (GetCapture() == hwnd) ReleaseCapture();
        if (wasClose && PtInRect(&g.closeBtn, pt)) hide();
        else redraw();
        return 0;
      }
      case WM_CAPTURECHANGED:
        closeDown = false;
        releaseKey();                              // never leave a key stuck down
        return 0;
      case WM_TIMER:
        if (wp == ID_REPEAT) {
          if (down >= 0) {
            Platform::sendKey(kbKeys()[down].vk, true);          // held keys auto-repeat
            SetTimer(hwnd, ID_REPEAT, kRepeatRateMs, nullptr);
          } else {
            KillTimer(hwnd, ID_REPEAT);
          }
        } else if (wp == ID_POLL) {
          if (refreshLabels(false)) redraw();
        }
        return 0;
      case WM_DC_KB_SHOW: show(); return 0;
      case WM_DC_KB_HIDE: hide(); return 0;
      case WM_DC_KB_TOGGLE:
        if (IsWindowVisible(hwnd)) hide(); else show();
        return 0;
      case WM_DC_KB_PADSTEP:
        if (IsWindowVisible(hwnd)) padStep((int)wp - 1, (int)lp - 1);
        return 0;
      case WM_DC_KB_REDRAW:
        redraw();
        return 0;
      case WM_DC_KB_QUIT:
        DestroyWindow(hwnd);
        return 0;
      case WM_CLOSE:
        hide();
        return 0;
      case WM_DESTROY:
        releaseAll();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
};

// ------------------------------------------------------------------ Keyboard
Keyboard& Keyboard::get() {
  static Keyboard kb;
  return kb;
}

LRESULT CALLBACK Keyboard::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(h, msg, wp, lp);
  return self->handle(msg, wp, lp);
}

void Keyboard::start() {
  if (th_.joinable()) return;
  ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  th_ = std::thread(&Keyboard::run, this);
  WaitForSingleObject(ready_, 3000);
}

void Keyboard::stop() {
  if (!th_.joinable()) return;
  HWND h = hwnd_;
  if (h) PostMessageW(h, WM_DC_KB_QUIT, 0, 0);
  th_.join();
  if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
}

void Keyboard::run() {
  Impl impl;
  impl.owner = this;
  impl.dpi = screenDpi();

  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &Keyboard::proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClassName, L"DeskControl keyboard",
                           WS_POPUP, 0, 0, 800, 300, nullptr, nullptr, inst, &impl);
  hwnd_ = h;
  if (ready_) SetEvent(ready_);

  if (h) {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  hwnd_ = nullptr;
  visible_ = false;
  UnregisterClassW(kClassName, inst);
}

void Keyboard::show() { HWND h = hwnd_; if (h) PostMessageW(h, WM_DC_KB_SHOW, 0, 0); }
void Keyboard::hide() { HWND h = hwnd_; if (h) PostMessageW(h, WM_DC_KB_HIDE, 0, 0); }
void Keyboard::toggle() { HWND h = hwnd_; if (h) PostMessageW(h, WM_DC_KB_TOGGLE, 0, 0); }

void Keyboard::padStep(int dx, int dy) {
  HWND h = hwnd_;
  if (h) PostMessageW(h, WM_DC_KB_PADSTEP, (WPARAM)(dx + 1), (LPARAM)(dy + 1));
}

void Keyboard::setAccent(COLORREF c) {
  accent_ = c;
  HWND h = hwnd_;
  if (h) PostMessageW(h, WM_DC_KB_REDRAW, 0, 0);
}

bool Keyboard::cursorOver() const {
  HWND h = hwnd_;
  if (!h || !visible_) return false;
  POINT pt;
  RECT r;
  if (!GetCursorPos(&pt) || !GetWindowRect(h, &r)) return false;
  return PtInRect(&r, pt) != 0;
}
