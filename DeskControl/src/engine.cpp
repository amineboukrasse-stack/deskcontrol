#include "engine.h"

#include "keyboard.h"

#include <algorithm>
#include <cmath>

#include <shellapi.h>
#include <xinput.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

extern "C" {
// winmm - raises the system timer resolution so the 150 Hz loop is accurate.
__declspec(dllimport) UINT WINAPI timeBeginPeriod(UINT);
__declspec(dllimport) UINT WINAPI timeEndPeriod(UINT);
}

namespace {

struct BatteryInfo { BYTE type; BYTE level; };

}  // namespace

Engine::Engine() : core_(this) {
  QueryPerformanceFrequency(&freq_);
}

Engine::~Engine() { stop(); }

double Engine::nowSec() const {
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)freq_.QuadPart;
}

void Engine::start(HWND notifyWindow) {
  if (thread_.joinable()) return;
  notify_ = notifyWindow;

  const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
  for (auto d : dlls) {
    xinput_ = LoadLibraryW(d);
    if (xinput_) break;
  }
  if (xinput_) {
    xGetState_ = (PFN_GetState)GetProcAddress(xinput_, "XInputGetState");
    xSetState_ = (PFN_SetState)GetProcAddress(xinput_, "XInputSetState");
    xGetBattery_ = (PFN_GetBattery)GetProcAddress(xinput_, "XInputGetBatteryInformation");
  }
  if (!xGetState_) log("XInput could not be loaded - controllers will not work.");

  quit_ = false;
  Keyboard::get().start();
  textFocus_.start();
  thread_ = std::thread(&Engine::run, this);
}

void Engine::stop() {
  if (!thread_.joinable()) return;
  quit_ = true;
  thread_.join();
  textFocus_.stop();
  Keyboard::get().stop();
  if (xinput_) { FreeLibrary(xinput_); xinput_ = nullptr; }
}

// ------------------------------------------------------------ thread-safe API
void Engine::setProfile(const Profile& p, bool reset) {
  std::lock_guard<std::mutex> lk(mu_);
  core_.setProfile(p, reset);
}

void Engine::setDevice(int controller, int pollHz) {
  std::lock_guard<std::mutex> lk(mu_);
  if (controller != cfgController_) { cfgController_ = controller; slot_ = -1; lastScan_ = -10.0; }
  pollHz_ = std::max(30, std::min(500, pollHz));
}

void Engine::setEnabled(bool e) { std::lock_guard<std::mutex> lk(mu_); core_.setEnabled(e); }
void Engine::setSuspended(bool s) { std::lock_guard<std::mutex> lk(mu_); suspended_ = s; }
void Engine::setPaused(bool p) { std::lock_guard<std::mutex> lk(mu_); paused_ = p; }
void Engine::setSpeedIndex(int i) { std::lock_guard<std::mutex> lk(mu_); core_.setSpeedIndex(i); }
void Engine::setVibration(bool v) { std::lock_guard<std::mutex> lk(mu_); core_.setVibration(v); }

void Engine::setKeyboardMode(bool builtin, bool dpadSteps) {
  Osk::setBuiltin(builtin);
  if (!builtin) Keyboard::get().hide();
  kbDpad_ = dpadSteps;
}

void Engine::testRumble() {
  std::lock_guard<std::mutex> lk(mu_);
  vibUntil_ = nowSec() + 0.5;
  vibStrength_ = std::max(0.2f, core_.profile().vibrationStrength);
}

EngineStatus Engine::status() {
  std::lock_guard<std::mutex> lk(mu_);
  EngineStatus s;
  s.pad = lastPad_;
  s.slot = slot_;
  s.battery = battery_;
  s.wired = wired_;
  s.enabled = core_.enabled();
  s.paused = paused_;
  s.speedIdx = core_.speedIndex();
  s.speedName = core_.currentSpeedName();
  s.precision = core_.precision();
  s.vibration = core_.vibration();
  s.fgExe = fgExe_;
  s.lt = lt_;
  s.rt = rt_;
  return s;
}

std::vector<LogLine> Engine::takeLog() {
  std::lock_guard<std::mutex> lk(logMu_);
  std::vector<LogLine> out;
  out.swap(log_);
  return out;
}

// -------------------------------------------------------------------- IOutput
void Engine::key(uint16_t vk, bool down) { Platform::sendKey(vk, down); }

void Engine::mouseButton(int mb, bool down) {
  INPUT in = {};
  in.type = INPUT_MOUSE;
  switch (mb) {
    case M_LEFT: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case M_RIGHT: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case M_MIDDLE: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case M_X1: in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON1; break;
    case M_X2: in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON2; break;
    default: return;
  }
  SendInput(1, &in, sizeof(INPUT));
  if (mb == M_LEFT && !down) textFocus_.notifyClick();   // did that click land in a text field?
}

void Engine::wheel(int amount, bool horizontal) {
  INPUT in = {};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
  in.mi.mouseData = (DWORD)amount;       // signed delta stored in a DWORD, as the API expects
  SendInput(1, &in, sizeof(INPUT));
}

void Engine::moveCursor(int dx, int dy, bool relative) {
  if (relative) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    SendInput(1, &in, sizeof(INPUT));
  } else {
    POINT p;
    if (GetCursorPos(&p)) SetCursorPos(p.x + dx, p.y + dy);
  }
}

void Engine::rumble(int ms, float strength) {
  // Called from inside step() with mu_ held - only record the request.
  vibUntil_ = nowSec() + ms / 1000.0;
  vibStrength_ = std::max(0.0f, std::min(1.0f, strength));
}

void Engine::event(int ev) {
  if (ev == EV_TOGGLE_OSK) { Osk::toggle(); return; }
  if (notify_) PostMessageW(notify_, WM_DESKCONTROL_EVENT, (WPARAM)ev, 0);
}

void Engine::log(const std::string& line) {
  std::lock_guard<std::mutex> lk(logMu_);
  if (log_.size() > 500) log_.erase(log_.begin(), log_.begin() + 100);
  log_.push_back({nowSec(), line});
}

// ----------------------------------------------------------------- the thread
bool Engine::pollPad(double now, PadState& pad) {
  pad = PadState();
  if (!xGetState_) return false;

  XINPUT_STATE st;
  bool got = false;

  if (slot_ >= 0) {
    ZeroMemory(&st, sizeof(st));
    if (xGetState_((DWORD)slot_, &st) == ERROR_SUCCESS) got = true;
    else {
      log("Controller " + std::to_string(slot_ + 1) + " disconnected");
      slot_ = -1;
      battery_ = -1;
      lastScan_ = now;
    }
  }
  if (!got) {
    // Probing empty slots is comparatively slow, so only look once a second.
    if (now - lastScan_ < 1.0) return false;
    lastScan_ = now;
    const int first = cfgController_ < 0 ? 0 : cfgController_;
    const int last = cfgController_ < 0 ? 3 : cfgController_;
    for (int s = first; s <= last && !got; ++s) {
      ZeroMemory(&st, sizeof(st));
      if (xGetState_((DWORD)s, &st) == ERROR_SUCCESS) {
        slot_ = s;
        got = true;
        nextBattery_ = 0;
        log("Controller " + std::to_string(s + 1) + " connected");
      }
    }
    if (!got) return false;
  }

  const XINPUT_GAMEPAD& g = st.Gamepad;
  const int thr = core_.profile().triggerThreshold;
  uint32_t bits = 0;
  for (int i = 0; i < B_LT; ++i)
    if (kXInputMask[i] && (g.wButtons & kXInputMask[i])) bits |= 1u << i;
  if (g.bLeftTrigger > thr) bits |= 1u << B_LT;
  if (g.bRightTrigger > thr) bits |= 1u << B_RT;

  pad.connected = true;
  pad.bits = bits;
  pad.lx = g.sThumbLX; pad.ly = g.sThumbLY;
  pad.rx = g.sThumbRX; pad.ry = g.sThumbRY;
  lt_ = g.bLeftTrigger;
  rt_ = g.bRightTrigger;

  if (xGetBattery_ && now >= nextBattery_) {
    nextBattery_ = now + 10.0;
    BatteryInfo bi = {0, 0};
    if (xGetBattery_((DWORD)slot_, 0 /* BATTERY_DEVTYPE_GAMEPAD */, &bi) == ERROR_SUCCESS) {
      wired_ = (bi.type == 1);
      battery_ = (bi.type == 0 || bi.type == 0xFF || wired_) ? -1 : (int)bi.level;
    }
  }
  return true;
}

void Engine::keyboardDpad(PadState& pad, double now) {
  static const int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};          // B_UP, B_DOWN, B_LEFT, B_RIGHT
  const uint32_t dpad = (1u << B_UP) | (1u << B_DOWN) | (1u << B_LEFT) | (1u << B_RIGHT);

  const bool active = kbDpad_ && pad.connected && core_.enabled() && !suspended_ && !paused_ &&
                      Keyboard::get().cursorOver();
  if (active) {
    kbSwallow_ |= pad.bits & dpad;
    for (int d = 0; d < 4; ++d) {
      const bool down = (pad.bits >> d & 1u) != 0;                               // B_UP..B_RIGHT are bits 0..3
      if (!down) { kbHeld_[d] = false; continue; }
      if (!kbHeld_[d]) {
        kbHeld_[d] = true;
        kbNext_[d] = now + 0.35;
        Keyboard::get().padStep(kDirs[d][0], kDirs[d][1]);
      } else if (now >= kbNext_[d]) {
        kbNext_[d] = now + 0.09;
        Keyboard::get().padStep(kDirs[d][0], kDirs[d][1]);
      }
    }
  } else {
    for (bool& h : kbHeld_) h = false;
  }
  // A button swallowed here stays hidden until it is let go, so leaving the keyboard with the
  // D-pad still held does not suddenly type arrow keys.
  kbSwallow_ &= pad.bits;
  pad.bits &= ~kbSwallow_;
}

void Engine::applyRumble(double now) {
  if (!xSetState_ || slot_ < 0) { vibActive_ = false; return; }
  const bool want = now < vibUntil_ && vibStrength_ > 0.0f;
  if (want == vibActive_) return;
  vibActive_ = want;
  XINPUT_VIBRATION v = {};
  if (want) v.wLeftMotorSpeed = v.wRightMotorSpeed = (WORD)(vibStrength_ * 65535.0f);
  xSetState_((DWORD)slot_, &v);
}

void Engine::run() {
  timeBeginPeriod(1);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

  double last = nowSec();
  double nextFg = 0.0;

  while (!quit_) {
    const double t0 = nowSec();
    const double dt = t0 - last;
    last = t0;

    if (t0 >= nextFg) {                       // foreground app (for app rules) - 3x per second
      nextFg = t0 + 0.3;
      std::string fg = Platform::foregroundExe();
      std::lock_guard<std::mutex> lk(mu_);
      fgExe_ = fg;
    }

    int hz;
    {
      std::lock_guard<std::mutex> lk(mu_);
      PadState pad;
      pollPad(t0, pad);
      lastPad_ = pad;                       // the UI shows the real pad, the core gets the filtered one
      keyboardDpad(pad, t0);
      core_.setSuspended(suspended_ || paused_);
      core_.step(pad, t0, dt);
      applyRumble(t0);
      hz = pollHz_;
    }

    const double rem = (t0 + 1.0 / hz) - nowSec();
    if (rem > 0.0) {
      if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(rem * 1e7);
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, 50);
      } else {
        Sleep((DWORD)std::max(1.0, rem * 1000.0));
      }
    }
  }

  {   // leave nothing pressed and the controller quiet
    std::lock_guard<std::mutex> lk(mu_);
    core_.releaseAll();
    if (xSetState_ && slot_ >= 0) { XINPUT_VIBRATION v = {}; xSetState_((DWORD)slot_, &v); }
  }
  if (timer) CloseHandle(timer);
  timeEndPeriod(1);
}
