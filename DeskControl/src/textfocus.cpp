#include "textfocus.h"

#include "keyboard.h"

#include <objbase.h>
#include <shellapi.h>
#include <uiautomation.h>

namespace Osk {

static std::atomic<bool> g_builtin{true};

void setBuiltin(bool builtin) { g_builtin = builtin; }

HWND find() {
  if (g_builtin) {
    Keyboard& kb = Keyboard::get();
    return kb.visible() ? kb.hwnd() : nullptr;
  }
  return FindWindowW(L"OSKMainClass", nullptr);   // window class, so it works in every language
}

void launch() {
  if (g_builtin) { Keyboard::get().show(); return; }
  HWND w = find();
  if (!w) ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_SHOWNORMAL);
  else if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
}

void close() {
  if (g_builtin) { Keyboard::get().hide(); return; }
  HWND w = find();
  if (w) PostMessageW(w, WM_CLOSE, 0, 0);
}

void toggle() {
  if (g_builtin) { Keyboard::get().toggle(); return; }
  HWND w = find();
  if (!w) launch();
  else if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
  else ShowWindow(w, SW_MINIMIZE);
}

}  // namespace Osk

void TextFocus::start() {
  if (th_.joinable()) return;
  ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  quit_ = false;
  th_ = std::thread(&TextFocus::run, this);
}

void TextFocus::stop() {
  if (!th_.joinable()) return;
  quit_ = true;
  SetEvent(ev_);
  th_.join();
  CloseHandle(ev_);
  ev_ = nullptr;
}

void TextFocus::notifyClick() {
  if ((show_ || hide_) && ev_) SetEvent(ev_);
}

void TextFocus::run() {
  // UI Automation can block while a target app is busy, so it lives on its own COM thread
  // and never delays controller polling.
  const bool comOk = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  IUIAutomation* uia = nullptr;

  while (!quit_) {
    if (WaitForSingleObject(ev_, 250) != WAIT_OBJECT_0) continue;
    if (quit_) break;
    Sleep(180);                       // let the clicked control take keyboard focus
    ResetEvent(ev_);
    if (!show_ && !hide_) continue;
    if (!uia && comOk)
      CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    handleClick(uia);
  }

  if (uia) uia->Release();
  if (comOk) CoUninitialize();
}

static bool focusIsTextField(IUIAutomation* uia) {
  bool text = false;
  CONTROLTYPEID ct = 0;
  IUIAutomationElement* el = nullptr;

  if (uia && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
    el->get_CurrentControlType(&ct);
    if (ct == UIA_EditControlTypeId || ct == UIA_DocumentControlTypeId) {
      IUIAutomationValuePattern* vp = nullptr;
      if (SUCCEEDED(el->GetCurrentPattern(UIA_ValuePatternId, reinterpret_cast<IUnknown**>(&vp))) && vp) {
        BOOL readOnly = FALSE;
        vp->get_CurrentIsReadOnly(&readOnly);
        text = !readOnly;
        vp->Release();
      } else {
        text = (ct == UIA_EditControlTypeId);        // an edit box without a value pattern is still editable
      }
      if (!text && ct == UIA_DocumentControlTypeId) {   // rich editors (Word, web editors)
        IUnknown* tep = nullptr;
        if (SUCCEEDED(el->GetCurrentPattern(UIA_TextEditPatternId, &tep)) && tep) { text = true; tep->Release(); }
      }
    }
    el->Release();
  }

  // Custom-drawn editors (Scintilla, Office...) do not expose Edit controls but do own a caret.
  if (!text && ct != UIA_EditControlTypeId && ct != UIA_DocumentControlTypeId) {
    HWND fg = GetForegroundWindow();
    if (fg) {
      GUITHREADINFO gi = {};
      gi.cbSize = sizeof(gi);
      if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gi) && gi.hwndCaret) text = true;
    }
  }
  return text;
}

void TextFocus::handleClick(void* p) {
  IUIAutomation* uia = static_cast<IUIAutomation*>(p);
  HWND osk = Osk::find();
  if (!osk) autoOpened_ = false;      // the user closed it themselves

  POINT pt;                           // clicking a key on the keyboard itself must never hide it
  if (osk && GetCursorPos(&pt)) {
    HWND under = WindowFromPoint(pt);
    if (under && GetAncestor(under, GA_ROOT) == osk) return;
  }

  const bool text = focusIsTextField(uia);
  if (text && show_) {
    if (!osk) { Osk::launch(); autoOpened_ = true; }
    else if (IsIconic(osk)) Osk::launch();
  } else if (!text && hide_ && autoOpened_ && osk) {
    Osk::close();
    autoOpened_ = false;
  }
}
