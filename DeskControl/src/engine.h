// DeskControl - Windows engine: owns the polling thread, talks to XInput and
// injects input with SendInput. The actual mapping logic lives in Core.
#pragma once
#include "platform.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core.h"
#include "textfocus.h"

struct EngineStatus {
  PadState pad;
  int slot = -1;               // XInput slot in use, -1 if none
  int battery = -1;            // 0 empty, 1 low, 2 medium, 3 full, -1 unknown
  bool wired = false;
  bool enabled = true;
  bool paused = false;         // paused by an app rule
  int speedIdx = 0;
  std::string speedName;
  bool precision = false;
  bool vibration = true;
  std::string fgExe;
  uint8_t lt = 0, rt = 0;      // raw analog trigger values
};

struct LogLine { double time; std::string text; };

class Engine : public IOutput {
 public:
  Engine();
  ~Engine();

  void start(HWND notifyWindow);
  void stop();

  void setProfile(const Profile& p, bool reset);
  void setDevice(int controller, int pollHz);     // controller: -1 auto, 0..3
  void setEnabled(bool e);
  void setSuspended(bool s);                      // GUI is capturing input: emit nothing
  void setPaused(bool p);                         // app rule says "pause"
  void setSpeedIndex(int i);
  void setVibration(bool v);
  void setKeyboardAuto(bool autoShow, bool autoHide) { textFocus_.configure(autoShow, autoHide); }
  void setKeyboardMode(bool builtin, bool dpadSteps);   // built-in vs Windows keyboard; D-pad steps between keys
  void testRumble();

  EngineStatus status();
  std::vector<LogLine> takeLog();

  // IOutput
  void key(uint16_t vk, bool down) override;
  void mouseButton(int mb, bool down) override;
  void wheel(int amount, bool horizontal) override;
  void moveCursor(int dx, int dy, bool relative) override;
  void rumble(int ms, float strength) override;
  void event(int ev) override;
  void log(const std::string& line) override;

 private:
  void run();
  bool pollPad(double now, PadState& pad);
  void keyboardDpad(PadState& pad, double now);   // D-pad drives the on-screen keyboard while the cursor is on it
  void applyRumble(double now);
  double nowSec() const;

  typedef DWORD(WINAPI* PFN_GetState)(DWORD, void*);
  typedef DWORD(WINAPI* PFN_SetState)(DWORD, void*);
  typedef DWORD(WINAPI* PFN_GetBattery)(DWORD, BYTE, void*);

  Core core_;
  TextFocus textFocus_;
  std::mutex mu_;
  std::mutex logMu_;
  std::vector<LogLine> log_;
  std::thread thread_;
  std::atomic<bool> quit_{false};
  HWND notify_ = nullptr;

  HMODULE xinput_ = nullptr;
  PFN_GetState xGetState_ = nullptr;
  PFN_SetState xSetState_ = nullptr;
  PFN_GetBattery xGetBattery_ = nullptr;

  int slot_ = -1;
  int cfgController_ = -1;
  int pollHz_ = 150;
  double lastScan_ = -10.0;
  double nextBattery_ = 0.0;
  int battery_ = -1;
  bool wired_ = false;

  bool suspended_ = false;
  bool paused_ = false;
  uint8_t lt_ = 0, rt_ = 0;
  PadState lastPad_;
  std::string fgExe_;

  double vibUntil_ = 0.0;
  float vibStrength_ = 0.0f;
  bool vibActive_ = false;

  std::atomic<bool> kbDpad_{true};
  uint32_t kbSwallow_ = 0;           // D-pad buttons held for the keyboard: hidden from the input core until released
  bool kbHeld_[4] = {false, false, false, false};
  double kbNext_[4] = {0, 0, 0, 0};

  LARGE_INTEGER freq_;
};
