// DeskControl - small Win32 helpers.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

#define WM_DESKCONTROL_EVENT (WM_APP + 10)   // wParam = CoreEvent
#define WM_DESKCONTROL_TRAY  (WM_APP + 11)
#define WM_DESKCONTROL_SHOW  (WM_APP + 12)   // sent by a second instance

namespace Platform {

std::string toUtf8(const std::wstring& w);
std::wstring toWide(const std::string& s);

std::wstring exePath();
std::wstring exeDir();
std::wstring dataDir();                 // %APPDATA%\DeskControl, or next to the exe in portable mode

void sendKey(uint16_t vk, bool down);   // inject one key event (arrows etc. are sent as extended keys)

bool isElevated();
bool relaunchElevated();                // true if a new elevated instance was started

bool getAutostart();
bool setAutostart(bool enable);

void openFolder(const std::wstring& path);
bool openFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring& outPath);

std::string foregroundExe();            // lower-case file name, "" if unknown
std::vector<std::string> visibleWindowExes();   // for the "add app" picker

}  // namespace Platform
