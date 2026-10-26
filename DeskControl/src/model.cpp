#include "model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

// ------------------------------------------------------------------ helpers
std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string toLower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool iequals(const std::string& a, const std::string& b) { return toLower(a) == toLower(b); }

int popcount32(uint32_t v) {
  int n = 0;
  while (v) { n += (int)(v & 1u); v >>= 1; }
  return n;
}

static std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

static bool parseBool(const std::string& v, bool def) {
  std::string l = toLower(trim(v));
  if (l == "1" || l == "true" || l == "yes" || l == "on") return true;
  if (l == "0" || l == "false" || l == "no" || l == "off") return false;
  return def;
}

static bool parseLong(const std::string& v, long& out) {
  std::string t = trim(v);
  if (t.empty()) return false;
  char* end = nullptr;
  long x = std::strtol(t.c_str(), &end, 0);
  if (end == t.c_str() || *end != '\0') return false;
  out = x;
  return true;
}

static bool parseFloat(const std::string& v, float& out) {
  std::string t = trim(v);
  if (t.empty()) return false;
  char* end = nullptr;
  float x = std::strtof(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0') return false;
  out = x;
  return true;
}

static std::string fmtFloat(float f) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.5g", (double)f);
  return buf;
}

// ------------------------------------------------------------------ buttons
static const char* kBtnNames[B_COUNT] = {
  "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Start", "Back",
  "Left stick click", "Right stick click", "Left bumper (LB)", "Right bumper (RB)",
  "A", "B", "X", "Y", "Left trigger (LT)", "Right trigger (RT)"
};
static const char* kBtnIds[B_COUNT] = {
  "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "START", "BACK",
  "LEFT_THUMB", "RIGHT_THUMB", "LB", "RB", "A", "B", "X", "Y", "LT", "RT"
};

const uint16_t kXInputMask[B_COUNT] = {
  0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
  0x0100, 0x0200, 0x1000, 0x2000, 0x4000, 0x8000, 0, 0
};

const char* btnName(int b) { return (b >= 0 && b < B_COUNT) ? kBtnNames[b] : "?"; }
const char* btnId(int b) { return (b >= 0 && b < B_COUNT) ? kBtnIds[b] : "?"; }
int btnFromId(const std::string& id) {
  std::string u = id;
  for (auto& c : u) c = (char)std::toupper((unsigned char)c);
  for (int i = 0; i < B_COUNT; ++i) if (u == kBtnIds[i]) return i;
  return -1;
}

// ------------------------------------------------------------------ actions
static const char* kMouseNames[M_COUNT] = {
  "Left click", "Right click", "Middle click", "Back button (X1)", "Forward button (X2)"
};
static const char* kMouseIds[M_COUNT] = { "left", "right", "middle", "x1", "x2" };

static const char* kSpecialNames[S_COUNT] = {
  "None", "Enable / disable DeskControl", "Toggle vibration", "Cycle cursor speed",
  "Cursor speed up", "Cursor speed down", "Show / hide window",
  "On-screen keyboard", "Precision mode (hold)", "Next profile"
};
static const char* kSpecialIds[S_COUNT] = {
  "none", "toggle_enabled", "toggle_vibration", "cycle_speed", "speed_up",
  "speed_down", "toggle_window", "toggle_osk", "precision", "next_profile"
};
static const char* kSpecialHelp[S_COUNT] = {
  "",
  "Pauses or resumes all controller input. Works even while DeskControl is disabled.",
  "Turns controller rumble feedback on or off.",
  "Steps through your cursor speed presets and wraps around.",
  "Moves to the next faster cursor speed preset.",
  "Moves to the next slower cursor speed preset.",
  "Shows the DeskControl window, or hides it to the tray.",
  "Shows the on-screen keyboard, or hides it if it is already open.",
  "While held, the cursor moves slower for precise aiming (see Sticks > Precision).",
  "Switches to the next profile in your list."
};

const char* mouseName(int m) { return (m >= 0 && m < M_COUNT) ? kMouseNames[m] : "?"; }
const char* specialName(int s) { return (s >= 0 && s < S_COUNT) ? kSpecialNames[s] : "?"; }
const char* specialHelp(int s) { return (s >= 0 && s < S_COUNT) ? kSpecialHelp[s] : ""; }

const std::vector<KeyInfo>& allKeys() {
  static std::vector<KeyInfo> keys;
  if (!keys.empty()) return keys;
  auto add = [&](uint16_t vk, const std::string& n) { keys.push_back({vk, n}); };

  add(0x0D, "Enter"); add(0x1B, "Escape"); add(0x20, "Space"); add(0x09, "Tab");
  add(0x08, "Backspace"); add(0x2E, "Delete"); add(0x2D, "Insert");
  add(0x26, "Up arrow"); add(0x28, "Down arrow"); add(0x25, "Left arrow"); add(0x27, "Right arrow");
  add(0x24, "Home"); add(0x23, "End"); add(0x21, "Page Up"); add(0x22, "Page Down");
  add(0x14, "Caps Lock"); add(0x2C, "Print Screen"); add(0x91, "Scroll Lock"); add(0x13, "Pause");
  add(0x90, "Num Lock"); add(0x5D, "Menu key");
  add(0x5B, "Left Windows"); add(0x5C, "Right Windows");
  add(0xA0, "Left Shift"); add(0xA1, "Right Shift");
  add(0xA2, "Left Ctrl"); add(0xA3, "Right Ctrl");
  add(0xA4, "Left Alt"); add(0xA5, "Right Alt");
  for (int c = 'A'; c <= 'Z'; ++c) add((uint16_t)c, std::string(1, (char)c));
  for (int c = '0'; c <= '9'; ++c) add((uint16_t)c, std::string(1, (char)c));
  for (int i = 1; i <= 24; ++i) add((uint16_t)(0x6F + i), "F" + std::to_string(i));
  for (int i = 0; i <= 9; ++i) add((uint16_t)(0x60 + i), "Numpad " + std::to_string(i));
  add(0x6A, "Numpad *"); add(0x6B, "Numpad +"); add(0x6D, "Numpad -");
  add(0x6E, "Numpad ."); add(0x6F, "Numpad /");
  add(0xBA, "; (semicolon)"); add(0xBB, "= (equals)"); add(0xBC, ", (comma)");
  add(0xBD, "- (minus)"); add(0xBE, ". (period)"); add(0xBF, "/ (slash)");
  add(0xC0, "` (backtick)"); add(0xDB, "[ (left bracket)"); add(0xDC, "\\ (backslash)");
  add(0xDD, "] (right bracket)"); add(0xDE, "' (quote)");
  add(0xAD, "Volume mute"); add(0xAE, "Volume down"); add(0xAF, "Volume up");
  add(0xB0, "Media next track"); add(0xB1, "Media previous track");
  add(0xB2, "Media stop"); add(0xB3, "Media play / pause");
  add(0xB4, "Launch mail"); add(0xB5, "Launch media player");
  add(0xB6, "Launch app 1"); add(0xB7, "Launch app 2");
  add(0xA6, "Browser back"); add(0xA7, "Browser forward"); add(0xA8, "Browser refresh");
  add(0xA9, "Browser stop"); add(0xAA, "Browser search"); add(0xAB, "Browser favorites");
  add(0xAC, "Browser home");
  add(0x5F, "Sleep");
  return keys;
}

std::string keyName(uint16_t vk) {
  static std::map<uint16_t, std::string> m;
  if (m.empty()) for (const auto& k : allKeys()) m[k.vk] = k.name;
  auto it = m.find(vk);
  if (it != m.end()) return it->second;
  char buf[24];
  std::snprintf(buf, sizeof(buf), "Key 0x%02X", (unsigned)vk);
  return buf;
}

std::string actionToString(const Action& a) {
  switch (a.type) {
    case ActType::None: return "Not assigned";
    case ActType::Mouse: return mouseName(a.mouse);
    case ActType::Special: return specialName(a.special);
    case ActType::Key: {
      std::string s;
      if (a.mods & GM_CTRL) s += "Ctrl+";
      if (a.mods & GM_SHIFT) s += "Shift+";
      if (a.mods & GM_ALT) s += "Alt+";
      if (a.mods & GM_WIN) s += "Win+";
      s += keyName(a.vk);
      if (a.repeat) s += " (repeats)";
      return s;
    }
  }
  return "?";
}

std::string actionToText(const Action& a) {
  switch (a.type) {
    case ActType::None: return "none";
    case ActType::Mouse: return std::string("mouse:") + kMouseIds[a.mouse < M_COUNT ? a.mouse : 0];
    case ActType::Special: return std::string("special:") + kSpecialIds[a.special < S_COUNT ? a.special : 0];
    case ActType::Key: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "key:0x%02X", (unsigned)a.vk);
      std::string s = buf;
      if (a.mods & GM_CTRL) s += "+ctrl";
      if (a.mods & GM_SHIFT) s += "+shift";
      if (a.mods & GM_ALT) s += "+alt";
      if (a.mods & GM_WIN) s += "+win";
      if (a.repeat) s += ",repeat";
      return s;
    }
  }
  return "none";
}

bool actionFromText(const std::string& text, Action& out) {
  std::string s = toLower(trim(text));
  out = Action();
  if (s.empty() || s == "none" || s == "0") return true;

  auto parts = split(s, ',');
  std::string main = trim(parts[0]);
  bool repeat = false;
  for (size_t i = 1; i < parts.size(); ++i)
    if (trim(parts[i]) == "repeat") repeat = true;

  if (main.rfind("key:", 0) == 0) {
    auto toks = split(main.substr(4), '+');
    long vk = 0;
    if (!parseLong(toks[0], vk) || vk < 1 || vk > 254) return false;
    uint8_t mods = 0;
    for (size_t i = 1; i < toks.size(); ++i) {
      std::string t = trim(toks[i]);
      if (t == "ctrl") mods |= GM_CTRL;
      else if (t == "shift") mods |= GM_SHIFT;
      else if (t == "alt") mods |= GM_ALT;
      else if (t == "win") mods |= GM_WIN;
      else return false;
    }
    out = Action::key((uint16_t)vk, mods, repeat);
    return true;
  }
  if (main.rfind("mouse:", 0) == 0) {
    std::string id = trim(main.substr(6));
    for (int i = 0; i < M_COUNT; ++i)
      if (id == kMouseIds[i]) { out = Action::mouseBtn((uint8_t)i); return true; }
    return false;
  }
  if (main.rfind("special:", 0) == 0) {
    std::string id = trim(main.substr(8));
    for (int i = 1; i < S_COUNT; ++i)
      if (id == kSpecialIds[i]) { out = Action::sp((uint8_t)i); return true; }
    return false;
  }
  return false;
}

// ------------------------------------------------------------------ profile
std::vector<SpeedPreset> Profile::defaultSpeeds() {
  return { {"Ultra low", 0.005f}, {"Low", 0.015f}, {"Medium", 0.025f}, {"High", 0.04f} };
}

Profile Profile::makeDefault() {
  Profile p;
  p.name = "Default";
  p.speeds = defaultSpeeds();
  p.defaultSpeed = 2;

  p.bind[0][B_UP] = Action::key(0x26, 0, true);      // arrows (with auto-repeat)
  p.bind[0][B_DOWN] = Action::key(0x28, 0, true);
  p.bind[0][B_LEFT] = Action::key(0x25, 0, true);
  p.bind[0][B_RIGHT] = Action::key(0x27, 0, true);
  p.bind[0][B_START] = Action::key(0x5B);            // Windows key
  p.bind[0][B_BACK] = Action::key(0xA8);             // browser refresh
  p.bind[0][B_LTHUMB] = Action::mouseBtn(M_MIDDLE);
  p.bind[0][B_RTHUMB] = Action::key(0x71);           // F2
  p.bind[0][B_LB] = Action::key(0xA6);               // browser back
  p.bind[0][B_RB] = Action::key(0xA7);               // browser forward
  p.bind[0][B_A] = Action::mouseBtn(M_LEFT);
  p.bind[0][B_B] = Action::key(0x0D);                // Enter
  p.bind[0][B_X] = Action::mouseBtn(M_RIGHT);
  p.bind[0][B_Y] = Action::sp(S_TOGGLE_WINDOW);
  p.bind[0][B_LT] = Action::key(0x20);               // Space
  p.bind[0][B_RT] = Action::key(0x08, 0, true);      // Backspace (repeats)

  auto bit = [](int b) { return 1u << b; };
  p.chords.push_back({bit(B_START) | bit(B_BACK), Action::sp(S_TOGGLE_ENABLED)});
  p.chords.push_back({bit(B_BACK) | bit(B_LB), Action::sp(S_TOGGLE_VIBRATION)});
  p.chords.push_back({bit(B_LB) | bit(B_RB), Action::sp(S_CYCLE_SPEED)});
  return p;
}

void Profile::clampValues() {
  auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
  auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
  deadZone = clampi(deadZone, 0, 30000);
  scrollDeadZone = clampi(scrollDeadZone, 0, 30000);
  triggerThreshold = clampi(triggerThreshold, 0, 254);
  chordWindowMs = clampi(chordWindowMs, 0, 500);
  repeatDelayMs = clampi(repeatDelayMs, 100, 2000);
  repeatRateHz = clampi(repeatRateHz, 2, 60);
  curve = clampf(curve, 0.0f, 6.0f);
  precisionFactor = clampf(precisionFactor, 0.02f, 1.0f);
  scrollSpeed = clampf(scrollSpeed, 0.001f, 5.0f);
  vibrationStrength = clampf(vibrationStrength, 0.0f, 1.0f);
  if (speeds.empty()) speeds = defaultSpeeds();
  if (speeds.size() > 8) speeds.resize(8);
  for (auto& s : speeds) s.value = clampf(s.value, 0.0001f, 1.0f);
  defaultSpeed = clampi(defaultSpeed, 0, (int)speeds.size() - 1);
  if (layerBtn < -1 || layerBtn >= B_COUNT) layerBtn = -1;
  if (name.empty()) name = "Default";
}

bool bindingsEqual(const Profile& a, const Profile& b) {
  if (a.layerBtn != b.layerBtn || a.chords.size() != b.chords.size()) return false;
  for (int l = 0; l < 2; ++l)
    for (int i = 0; i < B_COUNT; ++i)
      if (a.bind[l][i] != b.bind[l][i]) return false;
  for (size_t i = 0; i < a.chords.size(); ++i)
    if (a.chords[i].mask != b.chords[i].mask || a.chords[i].action != b.chords[i].action) return false;
  return true;
}

// ------------------------------------------------------------ serialisation
static std::string chordMaskToText(uint32_t mask) {
  std::string s;
  for (int i = 0; i < B_COUNT; ++i)
    if (mask & (1u << i)) { if (!s.empty()) s += "+"; s += btnId(i); }
  return s;
}

static bool chordMaskFromText(const std::string& text, uint32_t& mask) {
  mask = 0;
  for (const auto& t : split(text, '+')) {
    int b = btnFromId(trim(t));
    if (b < 0) return false;
    mask |= 1u << b;
  }
  return mask != 0;
}

std::string profileToText(const Profile& p) {
  std::ostringstream o;
  o << "# DeskControl profile (v" << DESKCONTROL_VERSION << "). Edit in the app, or by hand if you like.\n";
  o << "name = " << p.name << "\n\n";
  o << "# --- cursor ---\n";
  o << "swap_sticks = " << p.swapSticks << "\n";
  o << "relative_mouse = " << p.relativeMouse << "\n";
  o << "invert_mouse_y = " << p.invertMouseY << "\n";
  o << "dead_zone = " << p.deadZone << "\n";
  o << "curve = " << fmtFloat(p.curve) << "\n";
  o << "precision_factor = " << fmtFloat(p.precisionFactor) << "\n";
  o << "default_speed = " << p.defaultSpeed << "\n";
  for (const auto& s : p.speeds) o << "speed = " << s.name << "=" << fmtFloat(s.value) << "\n";
  o << "\n# --- scrolling ---\n";
  o << "scroll_dead_zone = " << p.scrollDeadZone << "\n";
  o << "scroll_speed = " << fmtFloat(p.scrollSpeed) << "\n";
  o << "scroll_invert_v = " << p.scrollInvertV << "\n";
  o << "scroll_invert_h = " << p.scrollInvertH << "\n";
  o << "scroll_horizontal = " << p.scrollHorizontal << "\n";
  o << "\n# --- buttons ---\n";
  o << "trigger_threshold = " << p.triggerThreshold << "\n";
  o << "chord_window_ms = " << p.chordWindowMs << "\n";
  o << "repeat_delay_ms = " << p.repeatDelayMs << "\n";
  o << "repeat_rate_hz = " << p.repeatRateHz << "\n";
  o << "layer_button = " << (p.layerBtn >= 0 ? btnId(p.layerBtn) : "none") << "\n";
  o << "\n# --- rumble ---\n";
  o << "vibration = " << p.vibration << "\n";
  o << "vibration_strength = " << fmtFloat(p.vibrationStrength) << "\n";
  o << "\n# --- bindings: key:<vk>[+ctrl+shift+alt+win][,repeat] | mouse:<left|right|middle|x1|x2> | special:<id> | none ---\n";
  for (int i = 0; i < B_COUNT; ++i) o << "bind." << btnId(i) << " = " << actionToText(p.bind[0][i]) << "\n";
  o << "\n# shift layer (active while layer_button is held)\n";
  for (int i = 0; i < B_COUNT; ++i)
    if (p.bind[1][i].type != ActType::None) o << "shift." << btnId(i) << " = " << actionToText(p.bind[1][i]) << "\n";
  o << "\n# --- chords: BUTTON+BUTTON | action ---\n";
  for (const auto& c : p.chords) if (c.mask) o << "chord = " << chordMaskToText(c.mask) << " | " << actionToText(c.action) << "\n";
  return o.str();
}

bool profileFromText(const std::string& text, Profile& out, std::vector<std::string>* warnings) {
  Profile p;                 // blank: no bindings, no chords, no speeds
  auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };

  std::istringstream in(text);
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) { warn("line " + std::to_string(lineNo) + ": missing '='"); continue; }
    std::string key = toLower(trim(line.substr(0, eq)));
    std::string val = trim(line.substr(eq + 1));
    long n = 0; float f = 0;

    auto setInt = [&](int& dst) { if (parseLong(val, n)) dst = (int)n; else warn("line " + std::to_string(lineNo) + ": bad number"); };
    auto setFloat = [&](float& dst) { if (parseFloat(val, f)) dst = f; else warn("line " + std::to_string(lineNo) + ": bad number"); };
    auto setBool = [&](bool& dst) { dst = parseBool(val, dst); };

    if (key == "name") { if (!val.empty()) p.name = val; }
    else if (key == "swap_sticks") setBool(p.swapSticks);
    else if (key == "relative_mouse") setBool(p.relativeMouse);
    else if (key == "invert_mouse_y") setBool(p.invertMouseY);
    else if (key == "dead_zone") setInt(p.deadZone);
    else if (key == "curve") setFloat(p.curve);
    else if (key == "precision_factor") setFloat(p.precisionFactor);
    else if (key == "default_speed") setInt(p.defaultSpeed);
    else if (key == "speed") {
      size_t e2 = val.rfind('=');
      SpeedPreset sp;
      if (e2 == std::string::npos) { sp.name = std::to_string(p.speeds.size() + 1); parseFloat(val, sp.value); }
      else { sp.name = trim(val.substr(0, e2)); if (!parseFloat(val.substr(e2 + 1), sp.value)) { warn("line " + std::to_string(lineNo) + ": bad speed"); continue; } }
      p.speeds.push_back(sp);
    }
    else if (key == "scroll_dead_zone") setInt(p.scrollDeadZone);
    else if (key == "scroll_speed") setFloat(p.scrollSpeed);
    else if (key == "scroll_invert_v") setBool(p.scrollInvertV);
    else if (key == "scroll_invert_h") setBool(p.scrollInvertH);
    else if (key == "scroll_horizontal") setBool(p.scrollHorizontal);
    else if (key == "trigger_threshold") setInt(p.triggerThreshold);
    else if (key == "chord_window_ms") setInt(p.chordWindowMs);
    else if (key == "repeat_delay_ms") setInt(p.repeatDelayMs);
    else if (key == "repeat_rate_hz") setInt(p.repeatRateHz);
    else if (key == "layer_button") p.layerBtn = btnFromId(val);
    else if (key == "vibration") setBool(p.vibration);
    else if (key == "vibration_strength") setFloat(p.vibrationStrength);
    else if (key.rfind("bind.", 0) == 0 || key.rfind("shift.", 0) == 0) {
      int layer = key[0] == 'b' ? 0 : 1;
      int b = btnFromId(key.substr(key.find('.') + 1));
      Action a;
      if (b < 0) warn("line " + std::to_string(lineNo) + ": unknown button");
      else if (!actionFromText(val, a)) warn("line " + std::to_string(lineNo) + ": unreadable action '" + val + "'");
      else p.bind[layer][b] = a;
    }
    else if (key == "chord") {
      size_t bar = val.find('|');
      ChordDef c;
      if (bar == std::string::npos || !chordMaskFromText(val.substr(0, bar), c.mask) ||
          !actionFromText(val.substr(bar + 1), c.action))
        warn("line " + std::to_string(lineNo) + ": unreadable chord");
      else p.chords.push_back(c);
    }
    else warn("line " + std::to_string(lineNo) + ": unknown setting '" + key + "'");
  }
  p.clampValues();
  out = p;
  return true;
}

std::string appToText(const AppSettings& a) {
  std::ostringstream o;
  o << "# DeskControl application settings\n";
  o << "active_profile = " << a.activeProfile << "\n";
  o << "controller = " << a.controller << "\n";
  o << "poll_hz = " << a.pollHz << "\n";
  o << "start_minimized = " << a.startMinimized << "\n";
  o << "close_to_tray = " << a.closeToTray << "\n";
  o << "builtin_keyboard = " << a.builtinKeyboard << "\n";
  o << "keyboard_dpad = " << a.keyboardDpad << "\n";
  o << "auto_keyboard = " << a.autoKeyboard << "\n";
  o << "auto_hide_keyboard = " << a.autoHideKeyboard << "\n";
  o << "accent = " << a.accent << "\n";
  o << "ui_scale = " << fmtFloat(a.uiScale) << "\n";
  for (const auto& r : a.rules) o << "rule = " << r.exe << " | " << (r.profile.empty() ? "<pause>" : r.profile) << "\n";
  return o.str();
}

bool appFromText(const std::string& text, AppSettings& out) {
  AppSettings a;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = toLower(trim(line.substr(0, eq)));
    std::string val = trim(line.substr(eq + 1));
    long n = 0; float f = 0;
    if (key == "active_profile" && !val.empty()) a.activeProfile = val;
    else if (key == "controller" && parseLong(val, n)) a.controller = (n >= 0 && n <= 3) ? (int)n : -1;
    else if (key == "poll_hz" && parseLong(val, n)) a.pollHz = (int)std::min(500L, std::max(30L, n));
    else if (key == "start_minimized") a.startMinimized = parseBool(val, false);
    else if (key == "close_to_tray") a.closeToTray = parseBool(val, true);
    else if (key == "builtin_keyboard") a.builtinKeyboard = parseBool(val, true);
    else if (key == "keyboard_dpad") a.keyboardDpad = parseBool(val, true);
    else if (key == "auto_keyboard") a.autoKeyboard = parseBool(val, true);
    else if (key == "auto_hide_keyboard") a.autoHideKeyboard = parseBool(val, true);
    else if (key == "accent" && parseLong(val, n)) a.accent = (int)std::max(0L, n);
    else if (key == "ui_scale" && parseFloat(val, f)) a.uiScale = std::min(2.0f, std::max(0.7f, f));
    else if (key == "rule") {
      size_t bar = val.find('|');
      if (bar == std::string::npos) continue;
      Rule r;
      r.exe = trim(val.substr(0, bar));
      r.profile = trim(val.substr(bar + 1));
      if (r.profile == "<pause>") r.profile.clear();
      if (!r.exe.empty()) a.rules.push_back(r);
    }
  }
  out = a;
  return true;
}

// ------------------------------------------------------------- legacy import
bool importLegacyText(const std::string& text, Profile& out, std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };

  std::map<std::string, std::string> kv;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    line = trim(line);
    if (line.empty()) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  if (kv.empty()) { warn("No settings found - is this a DeskControl config.ini?"); return false; }

  Profile p;                       // start blank (everything unassigned)
  p.name = "Imported";
  p.speeds.clear();

  auto num = [&](const char* k, long& v) {
    auto it = kv.find(k);
    return it != kv.end() && parseLong(it->second, v);
  };

  // Keyboard bindings first...
  struct { const char* key; int btn; } keyMap[] = {
    {"GAMEPAD_DPAD_UP", B_UP}, {"GAMEPAD_DPAD_DOWN", B_DOWN}, {"GAMEPAD_DPAD_LEFT", B_LEFT},
    {"GAMEPAD_DPAD_RIGHT", B_RIGHT}, {"GAMEPAD_START", B_START}, {"GAMEPAD_BACK", B_BACK},
    {"GAMEPAD_LEFT_THUMB", B_LTHUMB}, {"GAMEPAD_RIGHT_THUMB", B_RTHUMB},
    {"GAMEPAD_LEFT_SHOULDER", B_LB}, {"GAMEPAD_RIGHT_SHOULDER", B_RB},
    {"GAMEPAD_A", B_A}, {"GAMEPAD_B", B_B}, {"GAMEPAD_X", B_X}, {"GAMEPAD_Y", B_Y},
    {"GAMEPAD_TRIGGER_LEFT", B_LT}, {"GAMEPAD_TRIGGER_RIGHT", B_RT},
  };
  for (const auto& m : keyMap) {
    long vk = 0;
    if (num(m.key, vk) && vk > 0 && vk < 255) p.bind[0][m.btn] = Action::key((uint16_t)vk);
  }

  // ...then the CONFIG_* functions, which are XInput masks.
  auto maskToBits = [](long mask) {
    uint32_t bits = 0;
    for (int i = 0; i < B_COUNT; ++i)
      if (kXInputMask[i] && (mask & kXInputMask[i])) bits |= 1u << i;
    return bits;
  };
  auto apply = [&](const char* key, const Action& act) {
    long mask = 0;
    if (!num(key, mask) || mask == 0) return;
    uint32_t bits = maskToBits(mask);
    int n = popcount32(bits);
    if (n == 1) {
      for (int i = 0; i < B_COUNT; ++i)
        if (bits & (1u << i)) {
          if (p.bind[0][i].type != ActType::None)
            warn(std::string(btnName(i)) + ": keyboard binding replaced by " + key);
          p.bind[0][i] = act;
        }
    } else if (n > 1) {
      p.chords.push_back({bits, act});
    }
  };
  apply("CONFIG_MOUSE_LEFT", Action::mouseBtn(M_LEFT));
  apply("CONFIG_MOUSE_RIGHT", Action::mouseBtn(M_RIGHT));
  apply("CONFIG_MOUSE_MIDDLE", Action::mouseBtn(M_MIDDLE));
  apply("CONFIG_HIDE", Action::sp(S_TOGGLE_WINDOW));
  apply("CONFIG_DISABLE", Action::sp(S_TOGGLE_ENABLED));
  apply("CONFIG_DISABLE_VIBRATION", Action::sp(S_TOGGLE_VIBRATION));
  apply("CONFIG_SPEED_CHANGE", Action::sp(S_CYCLE_SPEED));
  apply("CONFIG_OSK", Action::sp(S_TOGGLE_OSK));

  long v = 0;
  if (num("DEAD_ZONE", v) && v > 0) p.deadZone = (int)v;
  if (num("SCROLL_DEAD_ZONE", v) && v > 0) p.scrollDeadZone = (int)v;
  if (num("SWAP_THUMBSTICKS", v)) p.swapSticks = v != 0;
  float f = 0;
  if (kv.count("SCROLL_SPEED") && parseFloat(kv["SCROLL_SPEED"], f) && f > 0.00001f) p.scrollSpeed = f;
  if (kv.count("ACCELERATION_FACTOR") && parseFloat(kv["ACCELERATION_FACTOR"], f) && f > 0) p.curve = f;

  if (kv.count("CURSOR_SPEED")) {
    int idx = 1;
    for (const auto& entry : split(kv["CURSOR_SPEED"], ',')) {
      std::string e = trim(entry);
      if (e.empty()) continue;
      SpeedPreset sp;
      size_t eq = e.find('=');
      float val = 0;
      if (eq == std::string::npos) { sp.name = std::to_string(idx++); parseFloat(e, val); }
      else { sp.name = trim(e.substr(0, eq)); parseFloat(e.substr(eq + 1), val); }
      if (val > 0.0001f && val <= 1.0f) { sp.value = val; p.speeds.push_back(sp); }
    }
  }
  p.defaultSpeed = 0;                // the original always started on the first preset
  if (p.speeds.empty()) { p.speeds = Profile::defaultSpeeds(); p.defaultSpeed = 2; }

  p.clampValues();
  out = p;
  return true;
}
