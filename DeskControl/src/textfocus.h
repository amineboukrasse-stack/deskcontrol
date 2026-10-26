// DeskControl - on-screen keyboard helpers and "show the keyboard when a text field is clicked".
#pragma once
#include "platform.h"

#include <atomic>
#include <thread>

// The keyboard that is shown: DeskControl's own (default) or the Windows one (osk.exe).
namespace Osk {
void setBuiltin(bool builtin);
HWND find();        // the keyboard window if it is open, else nullptr
void launch();      // show it (Windows keyboard: start osk.exe, or restore it if minimised)
void close();
void toggle();      // open, or close/minimise when already open
}

class TextFocus {
 public:
  ~TextFocus() { stop(); }
  void start();
  void stop();
  void configure(bool autoShow, bool autoHide) { show_ = autoShow; hide_ = autoHide; }
  void notifyClick();             // call after DeskControl released a left click

 private:
  void run();
  void handleClick(void* uiAutomation);

  std::thread th_;
  std::atomic<bool> quit_{false};
  std::atomic<bool> show_{false}, hide_{false};
  HANDLE ev_ = nullptr;
  bool autoOpened_ = false;       // only auto-close a keyboard that we opened ourselves
};
