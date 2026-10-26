#include "platform.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <set>

#include "model.h"

namespace Platform {

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

std::wstring toWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::wstring exePath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  return std::wstring(buf, n);
}

std::wstring exeDir() {
  std::wstring p = exePath();
  size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::wstring dataDir() {
  // Portable mode: a file called portable.txt next to the exe keeps everything beside it.
  std::wstring portable = exeDir() + L"\\portable.txt";
  if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return exeDir() + L"\\DeskControlData";

  wchar_t buf[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
    const std::wstring dir = std::wstring(buf) + L"\\DeskControl";
    // The app used to be called Gopher360: carry the old profiles and settings over once.
    const std::wstring old = std::wstring(buf) + L"\\Gopher360";
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
      MoveFileW(old.c_str(), dir.c_str());
    return dir;
  }
  return exeDir() + L"\\DeskControlData";
}

static bool isExtendedKey(uint16_t vk) {
  switch (vk) {
    case 0x21: case 0x22: case 0x23: case 0x24:         // PgUp PgDn End Home
    case 0x25: case 0x26: case 0x27: case 0x28:         // arrows (otherwise they act as numpad arrows)
    case 0x2D: case 0x2E:                               // Insert Delete
    case 0x5B: case 0x5C: case 0x5D:                    // Windows keys, menu
    case 0x6F: case 0x90:                               // numpad divide, NumLock
    case 0xA3: case 0xA5:                               // right Ctrl / right Alt
      return true;
    default: return false;
  }
}

void sendKey(uint16_t vk, bool down) {
  INPUT in = {};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  if (vk < 0xA6 || vk > 0xB7) in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  if (!down) in.ki.dwFlags |= KEYEVENTF_KEYUP;
  if (isExtendedKey(vk)) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  SendInput(1, &in, sizeof(INPUT));
}

bool isElevated() {
  bool elevated = false;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION e;
    DWORD size = sizeof(e);
    if (GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size)) elevated = e.TokenIsElevated != 0;
    CloseHandle(token);
  }
  return elevated;
}

bool relaunchElevated() {
  std::wstring path = exePath();
  HINSTANCE r = ShellExecuteW(nullptr, L"runas", path.c_str(), L"--elevated", nullptr, SW_SHOWNORMAL);
  return (INT_PTR)r > 32;
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"DeskControl";
static const wchar_t* kOldRunValue = L"Gopher360";      // the name before the rename

bool getAutostart() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
  bool exists = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(key);
  return exists;
}

bool setAutostart(bool enable) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
  bool ok;
  RegDeleteValueW(key, kOldRunValue);                    // drop the entry the old name left behind
  if (enable) {
    std::wstring cmd = L"\"" + exePath() + L"\" --minimized";
    ok = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  } else {
    LONG r = RegDeleteValueW(key, kRunValue);
    ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
  }
  RegCloseKey(key);
  return ok;
}

void openFolder(const std::wstring& path) {
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool openFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring& outPath) {
  wchar_t file[MAX_PATH * 2] = {0};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = file;
  ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
  ofn.lpstrTitle = title;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn)) return false;
  outPath = file;
  return true;
}

static std::string exeNameForWindow(HWND hwnd) {
  if (!hwnd) return std::string();
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid) return std::string();
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return std::string();           // e.g. an elevated process we cannot inspect
  wchar_t buf[MAX_PATH * 2];
  DWORD len = (DWORD)(sizeof(buf) / sizeof(buf[0]));
  std::string result;
  if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
    std::wstring full(buf, len);
    size_t slash = full.find_last_of(L"\\/");
    result = toLower(toUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1)));
  }
  CloseHandle(h);
  return result;
}

std::string foregroundExe() { return exeNameForWindow(GetForegroundWindow()); }

static BOOL CALLBACK enumWindowsCb(HWND hwnd, LPARAM lp) {
  auto* out = reinterpret_cast<std::set<std::string>*>(lp);
  if (!IsWindowVisible(hwnd)) return TRUE;
  if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
  if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
  std::string exe = exeNameForWindow(hwnd);
  if (!exe.empty()) out->insert(exe);
  return TRUE;
}

std::vector<std::string> visibleWindowExes() {
  std::set<std::string> names;
  EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&names));
  return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace Platform
