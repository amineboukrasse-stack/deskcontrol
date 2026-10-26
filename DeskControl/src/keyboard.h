// DeskControl - built-in on-screen keyboard.
//
// A small always-on-top window that never takes keyboard focus, so whatever you were typing
// into keeps receiving the keys. It runs on its own thread and is driven by ordinary mouse
// clicks (which is exactly what the controller's cursor produces), plus D-pad stepping from the
// engine thread. Every public method is safe to call from any thread.
#pragma once
#include "platform.h"

#include <atomic>
#include <thread>

#define WM_DC_KB_SHOW    (WM_APP + 30)
#define WM_DC_KB_HIDE    (WM_APP + 31)
#define WM_DC_KB_TOGGLE  (WM_APP + 32)
#define WM_DC_KB_PADSTEP (WM_APP + 33)   // wParam = dx + 1, lParam = dy + 1
#define WM_DC_KB_REDRAW  (WM_APP + 34)
#define WM_DC_KB_QUIT    (WM_APP + 35)

class Keyboard {
 public:
  static Keyboard& get();

  void start();                       // creates the window thread
  void stop();

  void show();
  void hide();
  void toggle();
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }

  bool cursorOver() const;            // is the mouse cursor on the visible keyboard?
  void padStep(int dx, int dy);       // move the cursor one key left/right/up/down
  void setAccent(COLORREF c);

 private:
  Keyboard() = default;
  void run();
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  struct Impl;                        // window state, only touched on the keyboard thread

  std::thread th_;
  HANDLE ready_ = nullptr;
  std::atomic<HWND> hwnd_{nullptr};
  std::atomic<bool> visible_{false};
  std::atomic<COLORREF> accent_{RGB(79, 140, 255)};
};
