// DeskControl - data model (platform independent, no Windows headers).
// Profiles, bindings, chords and their text serialisation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#define DESKCONTROL_VERSION "2.1.0"

// Physical controller inputs. The order is also the bit index used in chord
// masks and in PadState::bits.
enum Btn : int {
  B_UP, B_DOWN, B_LEFT, B_RIGHT, B_START, B_BACK, B_LTHUMB, B_RTHUMB,
  B_LB, B_RB, B_A, B_B, B_X, B_Y, B_LT, B_RT,
  B_COUNT
};

const char* btnName(int b);                 // "D-pad Up", "Left bumper", ...
const char* btnId(int b);                   // "DPAD_UP", "LB", ... (file format)
int btnFromId(const std::string& id);       // -1 if unknown
extern const uint16_t kXInputMask[B_COUNT]; // XINPUT_GAMEPAD_* bit for each button (0 for triggers)

// ---------------------------------------------------------------- actions
enum class ActType : uint8_t { None, Key, Mouse, Special };

enum MouseBtn : uint8_t { M_LEFT, M_RIGHT, M_MIDDLE, M_X1, M_X2, M_COUNT };

enum Special : uint8_t {
  S_NONE, S_TOGGLE_ENABLED, S_TOGGLE_VIBRATION, S_CYCLE_SPEED, S_SPEED_UP,
  S_SPEED_DOWN, S_TOGGLE_WINDOW, S_TOGGLE_OSK, S_PRECISION, S_NEXT_PROFILE,
  S_COUNT
};

// Modifier bits for key actions (not the Win32 MOD_* values on purpose).
enum : uint8_t { GM_CTRL = 1, GM_SHIFT = 2, GM_ALT = 4, GM_WIN = 8 };

struct Action {
  ActType type = ActType::None;
  uint16_t vk = 0;       // Key: Windows virtual-key code
  uint8_t mods = 0;      // Key: GM_* modifiers held while the key is pressed
  uint8_t mouse = 0;     // Mouse: MouseBtn
  uint8_t special = 0;   // Special: Special enum
  bool repeat = false;   // Key: auto-repeat while held

  static Action none() { return Action(); }
  static Action key(uint16_t vk, uint8_t mods = 0, bool repeat = false) {
    Action a; a.type = ActType::Key; a.vk = vk; a.mods = mods; a.repeat = repeat; return a;
  }
  static Action mouseBtn(uint8_t m) { Action a; a.type = ActType::Mouse; a.mouse = m; return a; }
  static Action sp(uint8_t s) { Action a; a.type = ActType::Special; a.special = s; return a; }

  bool operator==(const Action& o) const {
    return type == o.type && vk == o.vk && mods == o.mods && mouse == o.mouse &&
           special == o.special && repeat == o.repeat;
  }
  bool operator!=(const Action& o) const { return !(*this == o); }
};

const char* mouseName(int m);
const char* specialName(int s);
const char* specialHelp(int s);
std::string actionToString(const Action& a);     // human readable ("Ctrl+Shift+A")
std::string actionToText(const Action& a);       // file format
bool actionFromText(const std::string& s, Action& out);

struct KeyInfo { uint16_t vk; std::string name; };
const std::vector<KeyInfo>& allKeys();           // picker list
std::string keyName(uint16_t vk);

// ---------------------------------------------------------------- profile
struct ChordDef {
  uint32_t mask = 0;     // bit i set => Btn i must be down
  Action action;
};

struct SpeedPreset { std::string name; float value = 0.025f; };

struct Profile {
  std::string name = "Default";

  // [0] = normal layer, [1] = shift layer (active while layerBtn is held)
  Action bind[2][B_COUNT];
  int layerBtn = -1;                 // Btn index or -1 (no shift layer)
  std::vector<ChordDef> chords;

  // Cursor
  bool swapSticks = false;           // cursor on right stick, scroll on left
  bool relativeMouse = false;        // SendInput relative moves instead of SetCursorPos
  bool invertMouseY = false;
  int deadZone = 6000;               // 0..32767
  float curve = 0.0f;                // 0 = linear, >0 exponent (original ACCELERATION_FACTOR)
  float precisionFactor = 0.3f;      // cursor speed multiplier while Precision is held
  std::vector<SpeedPreset> speeds;
  int defaultSpeed = 2;

  // Scrolling
  int scrollDeadZone = 5000;
  float scrollSpeed = 0.1f;
  bool scrollInvertV = false;
  bool scrollInvertH = false;
  bool scrollHorizontal = true;

  // Buttons / triggers
  int triggerThreshold = 30;         // 0..255
  int chordWindowMs = 120;           // grace period for multi-button chords
  int repeatDelayMs = 400;
  int repeatRateHz = 25;

  // Rumble
  bool vibration = true;
  float vibrationStrength = 1.0f;

  static Profile makeDefault();
  static std::vector<SpeedPreset> defaultSpeeds();
  void clampValues();
};

bool bindingsEqual(const Profile& a, const Profile& b);

// ------------------------------------------------------------ app settings
struct Rule {                         // per-application automation
  std::string exe;                    // e.g. "steam.exe" (case-insensitive)
  std::string profile;                // empty => pause DeskControl in this app
};

struct AppSettings {
  std::string activeProfile = "Default";
  int controller = -1;                // -1 auto, else 0..3
  int pollHz = 150;
  bool startMinimized = false;
  bool closeToTray = true;
  bool builtinKeyboard = true;        // DeskControl's own on-screen keyboard (false: the Windows one)
  bool keyboardDpad = true;           // D-pad steps between keys while the cursor is on the keyboard
  bool autoKeyboard = true;           // show the on-screen keyboard when a text field is clicked
  bool autoHideKeyboard = true;       // ...and hide it again when clicking elsewhere
  int accent = 0;                     // index into the UI accent palette
  float uiScale = 1.0f;
  std::vector<Rule> rules;
};

// -------------------------------------------------------- (de)serialisation
std::string profileToText(const Profile& p);
// Unknown / malformed lines are skipped and reported in warnings; never throws.
bool profileFromText(const std::string& text, Profile& out, std::vector<std::string>* warnings = nullptr);
std::string appToText(const AppSettings& a);
bool appFromText(const std::string& text, AppSettings& out);

// Imports a Gopher360 1.x config.ini.
bool importLegacyText(const std::string& text, Profile& out, std::vector<std::string>* warnings = nullptr);

// Small helpers shared by the modules.
std::string trim(const std::string& s);
int popcount32(uint32_t v);
bool iequals(const std::string& a, const std::string& b);
std::string toLower(std::string s);
