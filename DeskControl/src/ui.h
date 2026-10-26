// DeskControl - Dear ImGui user interface.
#pragma once
#include <string>
#include <vector>

#include "engine.h"
#include "imgui.h"
#include "model.h"
#include "platform.h"

extern float g_uiScale;
inline float S(float v) { return v * g_uiScale; }

class UI {
 public:
  UI();
  bool init(HWND hwnd, float dpiScale);   // call after ImGui::CreateContext, before the first NewFrame
  void shutdown();
  void tick();                            // logic; call every loop iteration, visible or not
  void frame();                           // widgets; call between NewFrame and Render

  // Used by the window / tray code
  void onEvent(int coreEvent);
  void requestQuit() { quit_ = true; }
  bool wantQuit() const { return quit_; }
  bool startMinimized() const { return app_.startMinimized; }
  bool closeToTray() const { return app_.closeToTray; }
  void setWindowVisible(bool v);
  bool enabled() const { return st_.enabled; }
  void setEnabled(bool e) { engine_.setEnabled(e); }
  std::string trayTooltip() const;
  const std::vector<std::string>& profileNames() const { return profiles_; }
  const std::string& activeProfile() const { return prof_.name; }
  void selectProfile(const std::string& name);
  void nextProfile();
  void saveNow();

 private:
  // pages
  void pageDashboard();
  void pageButtons();
  void pageCombos();
  void pageSticks();
  void pageProfiles();
  void pageSettings();
  void pageLog();
  void pageAbout();

  // widgets
  bool actionEditor(const char* id, Action& a, bool allowRepeat);
  bool keyPicker(const char* id, uint16_t& vk);
  int drawController(float width, bool interactive, int selected);
  void drawStick(const char* label, float diameter, float x, float y, int deadZone, bool pressed);
  bool banner(ImU32 color, const char* title, const std::string& body, const char* button = nullptr);
  void drawBattery();

  // logic
  void applyStyle();
  void commit();                          // profile edited: push to engine, schedule save
  void saveProfileNow();
  void refreshProfiles();
  void evaluateRules();
  void pushDevice();
  void flushPending();
  void setPage(int p) { page_ = p; }

  HWND hwnd_ = nullptr;
  Engine engine_;
  AppSettings app_;
  Profile prof_;                          // the profile being edited (the user's selection)
  Profile eff_;                           // what the engine runs (may differ via app rules)
  std::string effName_;
  std::vector<std::string> profiles_;
  EngineStatus st_;
  std::vector<LogLine> logLines_;
  uint32_t prevBits_ = 0;

  ImFont* font_ = nullptr;
  ImFont* fontBold_ = nullptr;
  ImFont* fontTitle_ = nullptr;
  ImVec4 acc_;
  ImU32 accU_ = 0;

  int page_ = 0;
  int selBtn_ = B_A;
  int layerTab_ = 0;
  bool padPick_ = false;
  ImGuiID capId_ = 0;                     // key capture in progress (0 = none)
  bool capArmed_ = false;
  uint16_t capLastMod_ = 0;
  int chordCap_ = -1;                     // chord being captured from the controller
  uint32_t chordPeak_ = 0;
  bool suspendSent_ = false;

  bool dirty_ = false;
  double dirtyAt_ = 0;
  bool appDirty_ = false;
  bool pausedByRule_ = false;
  bool ruleDirty_ = true;
  std::string lastFg_;
  bool elevated_ = false;
  bool autostart_ = false;
  bool winVisible_ = true;
  bool quit_ = false;
  bool rebuildStyle_ = false;
  std::string status_;                    // short feedback line (import results etc.)
  char nameBuf_[64] = {0};
  char ruleBuf_[64] = {0};
  std::vector<std::string> runningExes_;
};
