#include "ui.h"

#include "keyboard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "store.h"
#include "ui_widgets.h"

float g_uiScale = 1.0f;
ImFont* g_fontBold = nullptr;
ImFont* g_fontTitle = nullptr;

namespace {

struct AccentDef { const char* name; int r, g, b; };
const AccentDef kAccents[] = {
  {"Emerald", 52, 199, 107}, {"Azure", 76, 141, 255}, {"Violet", 155, 107, 255},
  {"Amber", 255, 162, 58},   {"Rose", 255, 92, 138},  {"Cyan", 46, 196, 214},
};
const int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

float s_dpi = 1.0f;

double nowSeconds() { return (double)GetTickCount64() / 1000.0; }

ImVec4 vec(int r, int g, int b, int a = 255) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f); }

const char* kPageNames[] = {"Dashboard", "Buttons", "Combos", "Sticks & Speed", "Profiles & Apps", "Settings", "Log", "About"};
const int kPageCount = 8;

// Key capture: finishes with a key (plus modifiers held), or a lone modifier key.
struct CapResult { bool done = false, cancelled = false; uint16_t vk = 0; uint8_t mods = 0; };

CapResult pollKeyCapture(bool& armed, uint16_t& lastMod) {
  auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
  uint8_t mods = 0;
  uint16_t modVk = 0;
  if (down(VK_LCONTROL) || down(VK_RCONTROL)) { mods |= GM_CTRL; modVk = down(VK_LCONTROL) ? VK_LCONTROL : VK_RCONTROL; }
  if (down(VK_LSHIFT) || down(VK_RSHIFT)) { mods |= GM_SHIFT; modVk = down(VK_LSHIFT) ? VK_LSHIFT : VK_RSHIFT; }
  if (down(VK_LMENU) || down(VK_RMENU)) { mods |= GM_ALT; modVk = down(VK_LMENU) ? VK_LMENU : VK_RMENU; }
  if (down(VK_LWIN) || down(VK_RWIN)) { mods |= GM_WIN; modVk = down(VK_LWIN) ? VK_LWIN : VK_RWIN; }

  uint16_t key = 0;
  for (int vk = 0x08; vk <= 0xFE; ++vk) {
    if (vk == 0x10 || vk == 0x11 || vk == 0x12) continue;          // generic modifiers
    if (vk >= 0xA0 && vk <= 0xA5) continue;                        // L/R modifiers
    if (vk == 0x5B || vk == 0x5C) continue;                        // Windows keys
    if (vk >= 0x15 && vk <= 0x1A) continue;                        // IME keys
    if (vk >= 0xE0) continue;
    if (down(vk)) { key = (uint16_t)vk; break; }
  }

  CapResult r;
  if (!armed) { if (!key && !mods) armed = true; return r; }       // wait until everything was released once
  if (key == VK_ESCAPE && !mods) { r.cancelled = true; r.done = true; return r; }
  if (key) { r.done = true; r.vk = key; r.mods = mods; return r; }
  if (mods) { lastMod = modVk; return r; }
  if (lastMod) { r.done = true; r.vk = lastMod; r.mods = 0; return r; }
  return r;
}

struct Elem { int btn; int kind; float a, b, c, d; float round; const char* label; };
// kind 0 = circle (a,b = centre, c = radius), kind 1 = rect (a,b)-(c,d)
const Elem kElems[] = {
  {B_LT, 1, 62, 2, 132, 30, 8, "LT"},     {B_RT, 1, 288, 2, 358, 30, 8, "RT"},
  {B_LB, 1, 52, 34, 140, 52, 9, "LB"},    {B_RB, 1, 280, 34, 368, 52, 9, "RB"},
  {B_LTHUMB, 0, 112, 112, 32, 0, 0, ""},  {B_RTHUMB, 0, 268, 184, 32, 0, 0, ""},
  {B_UP, 1, 144, 158, 160, 178, 4, ""},   {B_DOWN, 1, 144, 190, 160, 210, 4, ""},
  {B_LEFT, 1, 126, 176, 146, 192, 4, ""}, {B_RIGHT, 1, 158, 176, 178, 192, 4, ""},
  {B_A, 0, 318, 134, 14, 0, 0, "A"},      {B_B, 0, 340, 112, 14, 0, 0, "B"},
  {B_X, 0, 296, 112, 14, 0, 0, "X"},      {B_Y, 0, 318, 90, 14, 0, 0, "Y"},
  {B_BACK, 0, 186, 112, 9, 0, 0, ""},     {B_START, 0, 234, 112, 9, 0, 0, ""},
};

ImU32 faceColor(int btn) {
  switch (btn) {
    case B_A: return IM_COL32(70, 200, 110, 255);
    case B_B: return IM_COL32(235, 85, 85, 255);
    case B_X: return IM_COL32(75, 145, 255, 255);
    case B_Y: return IM_COL32(245, 200, 60, 255);
    default: return 0;
  }
}

}  // namespace

int accentCount() { return kAccentCount; }
const char* accentName(int i) { return kAccents[i].name; }
ImVec4 accentColor(int i) { return vec(kAccents[i].r, kAccents[i].g, kAccents[i].b); }

UI::UI() {}

// ---------------------------------------------------------------- lifecycle
bool UI::init(HWND hwnd, float dpiScale) {
  hwnd_ = hwnd;
  s_dpi = dpiScale;

  Store::init(Platform::dataDir());
  Store::loadApp(app_);
  refreshProfiles();
  if (profiles_.empty()) {
    Store::saveProfile(Profile::makeDefault());
    refreshProfiles();
  }
  if (!Store::loadProfile(app_.activeProfile, prof_)) Store::loadProfile(profiles_[0], prof_);
  app_.activeProfile = prof_.name;
  eff_ = prof_;
  effName_ = prof_.name;
  elevated_ = Platform::isElevated();
  autostart_ = Platform::getAutostart();

  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;

  char winDir[MAX_PATH] = {0};
  GetWindowsDirectoryA(winDir, MAX_PATH);
  const std::string fonts = std::string(winDir) + "\\Fonts\\";
  const float base = 17.0f * dpiScale;
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  auto exists = [](const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
  const ImWchar* ranges = io.Fonts->GetGlyphRangesDefault();
  if (exists(fonts + "segoeui.ttf")) font_ = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), base, &cfg, ranges);
  if (font_ && exists(fonts + "segoeuib.ttf")) {
    fontBold_ = io.Fonts->AddFontFromFileTTF((fonts + "segoeuib.ttf").c_str(), base, &cfg, ranges);
    fontTitle_ = io.Fonts->AddFontFromFileTTF((fonts + "segoeuib.ttf").c_str(), base * 1.45f, &cfg, ranges);
  }
  if (!font_) {                                   // Segoe UI missing (stripped Windows, Wine): built-in font
    ImFontConfig dc;
    dc.SizePixels = base;
    font_ = io.Fonts->AddFontDefault(&dc);
  }
  if (!fontBold_) fontBold_ = font_;
  if (!fontTitle_) {
    ImFontConfig tc;
    tc.SizePixels = base * 1.45f;
    fontTitle_ = io.Fonts->AddFontDefault(&tc);
  }
  g_fontBold = fontBold_;
  g_fontTitle = fontTitle_;
  io.FontDefault = font_;

  applyStyle();

  engine_.setProfile(eff_, true);
  pushDevice();
  engine_.start(hwnd);
  return true;
}

void UI::shutdown() {
  flushPending();
  engine_.stop();
}

void UI::applyStyle() {
  const AccentDef& a = kAccents[std::max(0, std::min(app_.accent, kAccentCount - 1))];
  acc_ = vec(a.r, a.g, a.b);
  accU_ = ImGui::ColorConvertFloat4ToU32(acc_);
  Keyboard::get().setAccent(RGB(a.r, a.g, a.b));
  g_uiScale = s_dpi * app_.uiScale;
  ImGui::GetIO().FontGlobalScale = app_.uiScale;

  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  const ImVec4 bg = vec(17, 19, 24), side = vec(12, 14, 18), panel = vec(26, 29, 37), panel2 = vec(36, 40, 52),
               border = vec(48, 53, 67), text = vec(232, 235, 242), dimc = vec(135, 143, 162);
  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = dimc;
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = panel;
  c[ImGuiCol_PopupBg] = panel;
  c[ImGuiCol_MenuBarBg] = side;
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_FrameBg] = panel2;
  c[ImGuiCol_FrameBgHovered] = vec(46, 51, 66);
  c[ImGuiCol_FrameBgActive] = vec(54, 60, 78);
  c[ImGuiCol_Button] = panel2;
  c[ImGuiCol_ButtonHovered] = ImVec4(acc_.x, acc_.y, acc_.z, 0.35f);
  c[ImGuiCol_ButtonActive] = ImVec4(acc_.x, acc_.y, acc_.z, 0.55f);
  c[ImGuiCol_Header] = ImVec4(acc_.x, acc_.y, acc_.z, 0.22f);
  c[ImGuiCol_HeaderHovered] = ImVec4(acc_.x, acc_.y, acc_.z, 0.35f);
  c[ImGuiCol_HeaderActive] = ImVec4(acc_.x, acc_.y, acc_.z, 0.5f);
  c[ImGuiCol_SliderGrab] = acc_;
  c[ImGuiCol_SliderGrabActive] = ImVec4(std::min(1.f, acc_.x + .15f), std::min(1.f, acc_.y + .15f), std::min(1.f, acc_.z + .15f), 1);
  c[ImGuiCol_CheckMark] = acc_;
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = vec(58, 64, 82);
  c[ImGuiCol_ScrollbarGrabHovered] = vec(78, 86, 108);
  c[ImGuiCol_ScrollbarGrabActive] = vec(98, 108, 134);
  c[ImGuiCol_TextSelectedBg] = ImVec4(acc_.x, acc_.y, acc_.z, 0.35f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
  c[ImGuiCol_TableBorderLight] = border;
  c[ImGuiCol_TableHeaderBg] = panel2;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

  st.WindowRounding = 0;
  st.ChildRounding = 10;
  st.FrameRounding = 7;
  st.PopupRounding = 8;
  st.GrabRounding = 7;
  st.ScrollbarRounding = 8;
  st.FramePadding = ImVec2(10, 6);
  st.ItemSpacing = ImVec2(10, 9);
  st.WindowPadding = ImVec2(16, 14);
  st.ScrollbarSize = 12;
  st.ChildBorderSize = 1;
  st.FrameBorderSize = 0;
  st.PopupBorderSize = 1;
  st.GrabMinSize = 14;
  st.ScaleAllSizes(s_dpi * app_.uiScale);
}

void UI::setWindowVisible(bool v) {
  winVisible_ = v;
  if (!v) { capId_ = 0; chordCap_ = -1; padPick_ = false; }
}

// -------------------------------------------------------------------- logic
void UI::refreshProfiles() { profiles_ = Store::listProfiles(); }
void UI::pushDevice() {
  engine_.setDevice(app_.controller, app_.pollHz);
  engine_.setKeyboardAuto(app_.autoKeyboard, app_.autoHideKeyboard);
  engine_.setKeyboardMode(app_.builtinKeyboard, app_.keyboardDpad);
}

void UI::saveProfileNow() {
  Store::saveProfile(prof_);
  dirty_ = false;
}

void UI::flushPending() {
  if (dirty_) saveProfileNow();
  if (appDirty_) { Store::saveApp(app_); appDirty_ = false; }
}

void UI::saveNow() { flushPending(); }

void UI::commit() {
  prof_.clampValues();
  dirty_ = true;
  dirtyAt_ = nowSeconds();
  if (iequals(effName_, prof_.name)) {
    eff_ = prof_;
    engine_.setProfile(eff_, false);
  }
}

void UI::selectProfile(const std::string& name) {
  flushPending();
  Profile p;
  if (!Store::loadProfile(name, p)) return;
  prof_ = p;
  app_.activeProfile = p.name;
  appDirty_ = true;
  ruleDirty_ = true;
  effName_.clear();                       // force the engine to reload
  status_.clear();
}

void UI::nextProfile() {
  if (profiles_.size() < 2) return;
  size_t i = 0;
  for (; i < profiles_.size(); ++i) if (iequals(profiles_[i], prof_.name)) break;
  selectProfile(profiles_[(i + 1) % profiles_.size()]);
  engine_.log("Profile: " + prof_.name);
}

void UI::evaluateRules() {
  ruleDirty_ = false;
  const Rule* hit = nullptr;
  for (const auto& r : app_.rules)
    if (!st_.fgExe.empty() && iequals(r.exe, st_.fgExe)) { hit = &r; break; }

  const bool pause = hit && hit->profile.empty();
  if (pause != pausedByRule_) { pausedByRule_ = pause; engine_.setPaused(pause); }

  std::string want = prof_.name;
  if (hit && !hit->profile.empty() && Store::profileExists(hit->profile)) want = hit->profile;
  if (want != effName_) {
    if (iequals(want, prof_.name)) { eff_ = prof_; want = prof_.name; }
    else if (!Store::loadProfile(want, eff_)) { eff_ = prof_; want = prof_.name; }
    effName_ = want;
    engine_.setProfile(eff_, true);
  }
}

void UI::onEvent(int ev) {
  if (ev == EV_NEXT_PROFILE) nextProfile();
}

std::string UI::trayTooltip() const {
  std::string s = "DeskControl - ";
  s += st_.paused ? "paused for this app" : (st_.enabled ? "ON" : "OFF");
  s += "\nProfile: " + effName_;
  if (!st_.pad.connected) s += "\nNo controller";
  return s;
}

void UI::tick() {
  st_ = engine_.status();
  for (auto& l : engine_.takeLog()) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    l.time = t.wHour * 3600.0 + t.wMinute * 60.0 + t.wSecond;     // wall clock, seconds since midnight
    logLines_.push_back(l);
  }
  if (logLines_.size() > 300) logLines_.erase(logLines_.begin(), logLines_.begin() + (logLines_.size() - 300));

  if (st_.fgExe != lastFg_) { lastFg_ = st_.fgExe; ruleDirty_ = true; }
  if (ruleDirty_) evaluateRules();

  const bool want = capId_ != 0 || padPick_ || chordCap_ >= 0;
  if (want != suspendSent_) { engine_.setSuspended(want); suspendSent_ = want; }

  const double now = nowSeconds();
  if ((dirty_ && now - dirtyAt_ > 0.5) || appDirty_) flushPending();
}

// ------------------------------------------------------------------- widgets
bool UI::keyPicker(const char* id, uint16_t& vk) {
  bool changed = false;
  const std::string preview = vk ? keyName(vk) : "Choose a key...";
  if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
    static ImGuiTextFilter filter;
    if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); filter.Clear(); }
    filter.Draw("##filter", -FLT_MIN);
    for (const auto& k : allKeys()) {
      if (!filter.PassFilter(k.name.c_str())) continue;
      if (ImGui::Selectable(k.name.c_str(), vk == k.vk)) { vk = k.vk; changed = true; }
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool UI::actionEditor(const char* id, Action& a, bool allowRepeat) {
  bool ch = false;
  ImGui::PushID(id);

  static const char* types[] = {"Not assigned", "Keyboard key", "Mouse button", "Special function"};
  int type = (int)a.type;
  W::label("Action");
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::Combo("##type", &type, types, 4)) {
    Action n;
    n.type = (ActType)type;
    if (n.type == ActType::Mouse) n.mouse = M_LEFT;
    if (n.type == ActType::Special) n.special = S_CYCLE_SPEED;
    a = n;
    ch = true;
  }

  if (a.type == ActType::Key) {
    W::label("Key");
    const ImGuiID capHere = ImGui::GetID("##capture");
    const bool capturing = capId_ == capHere;
    const float btnW = S(150);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
    ch |= keyPicker("##key", a.vk);
    ImGui::SameLine();
    if (ImGui::Button(capturing ? "Press a key..." : "Capture key", ImVec2(btnW, 0))) {
      capId_ = capturing ? 0 : capHere;
      capArmed_ = false;
      capLastMod_ = 0;
    }
    if (capturing) {
      CapResult r = pollKeyCapture(capArmed_, capLastMod_);
      if (r.done) {
        if (!r.cancelled) { a.vk = r.vk; a.mods = r.mods; ch = true; }
        capId_ = 0;
      }
      W::dimWrapped("Press the key or shortcut you want (Esc cancels). Controller input is paused meanwhile.");
    }

    W::label("Hold with the key");
    bool ctrl = a.mods & GM_CTRL, shift = a.mods & GM_SHIFT, alt = a.mods & GM_ALT, win = a.mods & GM_WIN;
    bool m = false;
    m |= ImGui::Checkbox("Ctrl", &ctrl); ImGui::SameLine();
    m |= ImGui::Checkbox("Shift", &shift); ImGui::SameLine();
    m |= ImGui::Checkbox("Alt", &alt); ImGui::SameLine();
    m |= ImGui::Checkbox("Win", &win);
    if (m) {
      a.mods = (uint8_t)((ctrl ? GM_CTRL : 0) | (shift ? GM_SHIFT : 0) | (alt ? GM_ALT : 0) | (win ? GM_WIN : 0));
      ch = true;
    }
    if (allowRepeat) ch |= ImGui::Checkbox("Repeat while held", &a.repeat);
  } else if (a.type == ActType::Mouse) {
    W::label("Mouse button");
    int m = a.mouse;
    ImGui::SetNextItemWidth(-FLT_MIN);
    static const char* names[] = {"Left click", "Right click", "Middle click", "Back button (X1)", "Forward button (X2)"};
    if (ImGui::Combo("##mouse", &m, names, M_COUNT)) { a.mouse = (uint8_t)m; ch = true; }
  } else if (a.type == ActType::Special) {
    W::label("Function");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##special", specialName(a.special))) {
      for (int s = 1; s < S_COUNT; ++s)
        if (ImGui::Selectable(specialName(s), a.special == s)) { a.special = (uint8_t)s; ch = true; }
      ImGui::EndCombo();
    }
    W::dimWrapped(specialHelp(a.special));
  }
  ImGui::PopID();
  return ch;
}

int UI::drawController(float width, bool interactive, int selected) {
  const float dw = 420.f, dh = 272.f;
  const float s = width / dw;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(width, dh * s));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto P = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };

  const ImU32 body = W::col(ImGuiCol_FrameBg);
  const ImU32 face = W::col(ImGuiCol_Border);
  const ImU32 faceHot = IM_COL32(88, 96, 120, 255);
  const ImU32 ink = W::col(ImGuiCol_Text);

  dl->AddCircleFilled(P(96, 206), 54 * s, body, 48);
  dl->AddCircleFilled(P(324, 206), 54 * s, body, 48);
  dl->AddRectFilled(P(40, 56), P(380, 214), body, 70 * s);
  dl->AddRectFilled(P(144, 176), P(160, 192), face, 2 * s);       // d-pad centre

  // hit test
  const ImVec2 mp = ImGui::GetMousePos();
  const bool canHover = interactive && ImGui::IsWindowHovered();
  int hovered = -1;
  for (const auto& e : kElems) {
    bool hit;
    if (e.kind == 0) {
      const float dx = mp.x - (o.x + e.a * s), dy = mp.y - (o.y + e.b * s), r = e.c * s;
      hit = dx * dx + dy * dy <= r * r;
    } else {
      hit = mp.x >= o.x + e.a * s && mp.x <= o.x + e.c * s && mp.y >= o.y + e.b * s && mp.y <= o.y + e.d * s;
    }
    if (canHover && hit) hovered = e.btn;
  }

  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize() * 0.9f;

  for (const auto& e : kElems) {
    const bool pressed = (st_.pad.bits >> e.btn) & 1u;
    const bool hot = hovered == e.btn;
    const bool sel = selected == e.btn;
    const ImU32 fc = faceColor(e.btn);
    ImU32 fill = pressed ? (fc ? fc : accU_) : (hot ? faceHot : face);

    if (e.kind == 0) {
      const ImVec2 c = P(e.a, e.b);
      const float r = e.c * s;
      dl->AddCircleFilled(c, r, fill, 40);
      if (e.btn == B_LTHUMB || e.btn == B_RTHUMB) {
        const bool left = e.btn == B_LTHUMB;
        const float x = (left ? st_.pad.lx : st_.pad.rx) / 32767.f;
        const float y = (left ? st_.pad.ly : st_.pad.ry) / 32767.f;
        dl->AddCircleFilled(ImVec2(c.x + x * r * 0.42f, c.y - y * r * 0.42f), r * 0.62f, pressed ? IM_COL32(255, 255, 255, 235) : IM_COL32(24, 27, 35, 255), 32);
        dl->AddCircle(ImVec2(c.x + x * r * 0.42f, c.y - y * r * 0.42f), r * 0.62f, accU_, 32, 1.5f * s);
      } else if (e.label[0]) {
        const ImVec2 ts = ImGui::CalcTextSize(e.label);
        const ImVec2 tp(c.x - ts.x * 0.45f, c.y - ts.y * 0.45f);
        dl->AddText(font, fs, tp, pressed ? IM_COL32(15, 17, 22, 255) : (fc ? fc : ink), e.label);
      }
      if (sel) dl->AddCircle(c, r + 3 * s, accU_, 40, 2.5f * s);
    } else {
      const ImVec2 a = P(e.a, e.b), b = P(e.c, e.d);
      dl->AddRectFilled(a, b, (pressed && e.label[0] == '\0') ? accU_ : (hot ? faceHot : face), e.round * s);
      if (e.btn == B_LT || e.btn == B_RT) {
        const float frac = (e.btn == B_LT ? st_.lt : st_.rt) / 255.f;
        if (frac > 0.01f) dl->AddRectFilled(a, ImVec2(b.x, a.y + (b.y - a.y) * frac), accU_, e.round * s);
      } else if (e.btn == B_LB || e.btn == B_RB) {
        if (pressed) dl->AddRectFilled(a, b, accU_, e.round * s);
      }
      if (e.label[0]) {
        const ImVec2 ts = ImGui::CalcTextSize(e.label);
        dl->AddText(font, fs, ImVec2((a.x + b.x) * 0.5f - ts.x * 0.45f, (a.y + b.y) * 0.5f - ts.y * 0.45f), pressed ? IM_COL32(15, 17, 22, 255) : ink, e.label);
      } else {                                                      // d-pad arrow
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float q = 4 * s;
        const ImU32 ac = pressed ? IM_COL32(15, 17, 22, 255) : ink;
        if (e.btn == B_UP) dl->AddTriangleFilled(ImVec2(c.x, c.y - q), ImVec2(c.x - q, c.y + q * 0.7f), ImVec2(c.x + q, c.y + q * 0.7f), ac);
        if (e.btn == B_DOWN) dl->AddTriangleFilled(ImVec2(c.x, c.y + q), ImVec2(c.x - q, c.y - q * 0.7f), ImVec2(c.x + q, c.y - q * 0.7f), ac);
        if (e.btn == B_LEFT) dl->AddTriangleFilled(ImVec2(c.x - q, c.y), ImVec2(c.x + q * 0.7f, c.y - q), ImVec2(c.x + q * 0.7f, c.y + q), ac);
        if (e.btn == B_RIGHT) dl->AddTriangleFilled(ImVec2(c.x + q, c.y), ImVec2(c.x - q * 0.7f, c.y - q), ImVec2(c.x - q * 0.7f, c.y + q), ac);
      }
      if (sel) dl->AddRect(a - ImVec2(2 * s, 2 * s), b + ImVec2(2 * s, 2 * s), accU_, e.round * s, 0, 2.5f * s);
    }
  }

  if (hovered >= 0) {
    ImGui::SetTooltip("%s", btnName(hovered));
    if (ImGui::IsMouseClicked(0)) return hovered;
  }
  return -1;
}

void UI::drawStick(const char* label, float diameter, float x, float y, int deadZone, bool pressed) {
  const float r = diameter * 0.5f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 c(p.x + r, p.y + r);
  ImGui::Dummy(ImVec2(diameter, diameter + ImGui::GetTextLineHeightWithSpacing()));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddCircleFilled(c, r, W::col(ImGuiCol_FrameBg), 64);
  dl->AddCircleFilled(c, r * (float)deadZone / 32767.f, W::col(ImGuiCol_Border, 0.8f), 48);
  dl->AddCircle(c, r, W::col(ImGuiCol_Border), 64, 2.0f);
  dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), W::col(ImGuiCol_Border, 0.6f));
  dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), W::col(ImGuiCol_Border, 0.6f));
  dl->AddCircleFilled(ImVec2(c.x + x / 32767.f * r, c.y - y / 32767.f * r), S(7), pressed ? IM_COL32(255, 255, 255, 255) : accU_, 24);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(c.x - ts.x * 0.5f, p.y + diameter + S(2)), W::col(ImGuiCol_TextDisabled), label);
}

bool UI::banner(ImU32 color, const char* title, const std::string& body, const char* button) {
  bool clicked = false;
  if (W::beginCard((std::string("##banner") + title).c_str(), ImVec2(0, 0), color)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    W::title(title);
    ImGui::PopStyleColor();
    W::dimWrapped(body.c_str());
    if (button && ImGui::Button(button)) clicked = true;
  }
  W::endCard();
  return clicked;
}

void UI::drawBattery() {
  if (st_.wired) { W::dim("Wired"); return; }
  if (st_.battery < 0) return;
  static const int bars[] = {0, 1, 3, 4};
  const int n = bars[std::max(0, std::min(st_.battery, 3))];
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float bw = S(6), bh = S(14), gap = S(3);
  ImGui::Dummy(ImVec2(4 * (bw + gap) + S(6), bh));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 on = st_.battery <= 1 ? IM_COL32(235, 85, 85, 255) : accU_;
  for (int i = 0; i < 4; ++i)
    dl->AddRectFilled(ImVec2(p.x + i * (bw + gap), p.y), ImVec2(p.x + i * (bw + gap) + bw, p.y + bh), i < n ? on : W::col(ImGuiCol_Border), 2);
}

// -------------------------------------------------------------------- frame
void UI::frame() {
  if (rebuildStyle_) { applyStyle(); rebuildStyle_ = false; }

  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  // ---- sidebar
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
  ImGui::BeginChild("##side", ImVec2(S(206), 0), 0, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SetCursorPos(ImVec2(S(22), S(20)));
  ImGui::PushFont(fontTitle_);
  ImGui::TextUnformatted("DeskControl");
  ImGui::PopFont();
  ImGui::SetCursorPosX(S(22));
  W::dim("Couch control");
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(16));
  for (int i = 0; i < kPageCount; ++i) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = S(40);
    ImGui::PushID(i);
    const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool on = page_ == i;
    if (on) {
      dl->AddRectFilled(ImVec2(p.x + S(10), p.y + S(2)), ImVec2(p.x + w - S(10), p.y + h - S(2)), W::accent(0.16f), S(8));
      dl->AddRectFilled(ImVec2(p.x + S(10), p.y + S(10)), ImVec2(p.x + S(13), p.y + h - S(10)), accU_, S(2));
    } else if (hov) {
      dl->AddRectFilled(ImVec2(p.x + S(10), p.y + S(2)), ImVec2(p.x + w - S(10), p.y + h - S(2)), W::col(ImGuiCol_FrameBg, 0.8f), S(8));
    }
    dl->AddText(ImVec2(p.x + S(26), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), on ? W::col(ImGuiCol_Text) : W::col(ImGuiCol_TextDisabled), kPageNames[i]);
    if (clicked) page_ = i;
  }
  {
    ImGui::SetCursorPos(ImVec2(S(22), ImGui::GetWindowHeight() - S(54)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool ok = st_.pad.connected;
    dl->AddCircleFilled(ImVec2(p.x + S(5), p.y + S(10)), S(5), ok ? accU_ : IM_COL32(235, 85, 85, 255));
    ImGui::SetCursorPosX(S(40));
    ImGui::TextDisabled("%s", ok ? ("Controller " + std::to_string(st_.slot + 1)).c_str() : "No controller");
  }
  ImGui::EndChild();
  ImGui::SameLine(0, 0);

  // ---- content
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28), S(22)));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
  ImGui::BeginChild("##main", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  {
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(fontTitle_);
    ImGui::TextUnformatted(kPageNames[page_]);
    ImGui::PopFont();
    bool en = st_.enabled;
    const float w = ImGui::GetFrameHeight() * 0.78f * 1.85f + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("DeskControl enabled").x;
    ImGui::SameLine(availW - w);
    if (W::sw("DeskControl enabled", &en)) engine_.setEnabled(en);
    ImGui::Spacing();
  }

  switch (page_) {
    case 0: pageDashboard(); break;
    case 1: pageButtons(); break;
    case 2: pageCombos(); break;
    case 3: pageSticks(); break;
    case 4: pageProfiles(); break;
    case 5: pageSettings(); break;
    case 6: pageLog(); break;
    default: pageAbout(); break;
  }
  ImGui::Dummy(ImVec2(0, S(12)));
  ImGui::EndChild();
  ImGui::End();

  prevBits_ = st_.pad.bits;
}

// ---------------------------------------------------------------- dashboard
void UI::pageDashboard() {
  bool ch = false;
  const ImU32 warn = IM_COL32(245, 190, 70, 255), info = IM_COL32(96, 165, 250, 255);

  if (!st_.pad.connected)
    banner(warn, "No controller detected",
           "Connect an Xbox 360 / One / Series controller by USB, Bluetooth or wireless adapter. PlayStation controllers work through DS4Windows or Steam Input, which make them look like Xbox pads.");
  if (!elevated_) {
    if (banner(info, "Not running as administrator",
               "Windows blocks injected input to programs that run as administrator (Task Manager, some games and installers). Restart DeskControl as administrator if you need that.",
               "Restart as administrator")) {
      saveNow();
      if (Platform::relaunchElevated()) requestQuit();
    }
  }
  if (st_.paused)
    banner(info, "Paused for this application", "An app rule pauses DeskControl while " + st_.fgExe + " is in front. Manage rules under Profiles & Apps.");
  else if (!iequals(effName_, prof_.name))
    banner(info, "Profile switched automatically", "\"" + effName_ + "\" is active because " + st_.fgExe + " is in front. You are editing \"" + prof_.name + "\".");

  const float avail = ImGui::GetContentRegionAvail().x;
  const float leftW = std::floor(avail * 0.44f);

  // ---- control card
  W::beginCard("##ctl", ImVec2(leftW, 0));
  {
    const bool on = st_.enabled && !st_.paused;
    ImGui::PushFont(fontTitle_);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? acc_ : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(on ? "Input is ON" : (st_.paused ? "Paused" : "Input is OFF"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    W::dimWrapped(prof_.swapSticks ? "Right stick moves the mouse, left stick scrolls." : "Left stick moves the mouse, right stick scrolls.");
    ImGui::Spacing();
    if (ImGui::Button(st_.enabled ? "Pause controller input" : "Resume controller input", ImVec2(-FLT_MIN, S(42)))) engine_.setEnabled(!st_.enabled);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    W::label("Profile");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##prof", prof_.name.c_str())) {
      for (const auto& n : profiles_)
        if (ImGui::Selectable(n.c_str(), iequals(n, prof_.name)) && !iequals(n, prof_.name)) selectProfile(n);
      ImGui::EndCombo();
    }

    W::label("Cursor speed", "Cycle with the speed combo (default LB + RB) or pick one here. Edit presets under Sticks & Speed.");
    std::vector<std::string> names;
    for (const auto& sp : eff_.speeds) names.push_back(sp.name);
    const int c = W::segmented("speed", names, st_.speedIdx);
    if (c >= 0) engine_.setSpeedIndex(c);

    ImGui::Spacing();
    bool vib = st_.vibration;
    if (W::sw("Rumble feedback", &vib)) engine_.setVibration(vib);
    bool swap = prof_.swapSticks;
    if (W::sw("Swap sticks", &swap)) { prof_.swapSticks = swap; ch = true; }
  }
  W::endCard();
  ImGui::SameLine();

  // ---- controller card
  W::beginCard("##pad", ImVec2(0, 0));
  {
    W::title(st_.pad.connected ? ("Controller " + std::to_string(st_.slot + 1)).c_str() : "Controller");
    if (st_.pad.connected && (st_.wired || st_.battery >= 0)) {
      ImGui::SameLine();
      drawBattery();
    }
    const float w = std::min(ImGui::GetContentRegionAvail().x, S(520));
    drawController(w, false, -1);
    if (st_.pad.connected) {
      ImGui::TextDisabled("L %6d, %6d   R %6d, %6d", st_.pad.lx, st_.pad.ly, st_.pad.rx, st_.pad.ry);
    } else {
      W::dimWrapped("Press any button once your controller is connected.");
    }
  }
  W::endCard();

  if (ch) commit();
}

// ------------------------------------------------------------------ buttons
void UI::pageButtons() {
  bool ch = false;

  {
    std::vector<std::string> tabs = {"Normal layer", "Shift layer"};
    const int c = W::segmented("layer", tabs, layerTab_);
    if (c >= 0) layerTab_ = c;
    ImGui::SameLine(0, S(24));
    ImGui::AlignTextToFramePadding();
    W::dim("Shift button");
    ImGui::SameLine();
    int cur = prof_.layerBtn + 1;
    ImGui::SetNextItemWidth(S(190));
    if (ImGui::BeginCombo("##layerbtn", cur == 0 ? "None" : btnName(prof_.layerBtn))) {
      if (ImGui::Selectable("None", cur == 0)) { prof_.layerBtn = -1; ch = true; }
      for (int b = 0; b < B_COUNT; ++b)
        if (ImGui::Selectable(btnName(b), prof_.layerBtn == b)) { prof_.layerBtn = b; layerTab_ = 1; ch = true; }
      ImGui::EndCombo();
    }
    W::help("Hold the shift button to switch every other button to its Shift-layer action - a second set of bindings on the same controller.");
  }
  if (padPick_) {
    const uint32_t fresh = st_.pad.bits & ~prevBits_;
    for (int b = 0; b < B_COUNT && fresh; ++b)
      if ((fresh >> b) & 1u) { selBtn_ = b; padPick_ = false; break; }
  }
  ImGui::Spacing();

  const float avail = ImGui::GetContentRegionAvail().x;
  const float lw = std::min(S(540), std::floor(avail * 0.56f));

  W::beginCard("##diag", ImVec2(lw, 0));
  {
    const int c = drawController(ImGui::GetContentRegionAvail().x, true, selBtn_);
    if (c >= 0) selBtn_ = c;
    W::dimWrapped(padPick_ ? "Press the button on your controller that you want to edit..." : "Click a button to edit it. Buttons light up as you press them.");
    ImGui::Checkbox("Pick by pressing on controller", &padPick_);
  }
  W::endCard();
  ImGui::SameLine();

  W::beginCard("##edit", ImVec2(0, 0));
  {
    ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted(btnName(selBtn_));
    ImGui::PopFont();
    W::dim(layerTab_ ? "Shift layer" : "Normal layer");
    ImGui::Spacing();

    if (selBtn_ == prof_.layerBtn) {
      ImGui::TextWrapped("This is your shift button. It switches layers while held, so it has no action of its own.");
      if (ImGui::Button("Remove shift button")) { prof_.layerBtn = -1; ch = true; }
    } else if (layerTab_ == 1 && prof_.layerBtn < 0) {
      ImGui::TextWrapped("Pick a shift button above to start using the Shift layer.");
    } else {
      Action& a = prof_.bind[layerTab_][selBtn_];
      ch |= actionEditor("act", a, true);
      ImGui::Spacing();
      if (ImGui::Button("Clear")) { a = Action(); ch = true; }
      ImGui::SameLine();
      if (ImGui::Button("Reset to default")) { a = Profile::makeDefault().bind[layerTab_][selBtn_]; ch = true; }

      bool member = false;
      for (const auto& c : prof_.chords)
        if (((c.mask >> selBtn_) & 1u) && popcount32(c.mask) >= 2 && c.action.type != ActType::None) member = true;
      if (member && prof_.chordWindowMs > 0)
        W::dimWrapped("This button is part of a combo, so it reacts a fraction of a second late (the combo window) to leave room for the combo. Change it under Combos.");
    }
  }
  W::endCard();

  ImGui::Spacing();
  W::beginCard("##table", ImVec2(0, 0));
  {
    W::title("All buttons");
    if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
      ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Normal", ImGuiTableColumnFlags_WidthStretch, 1.4f);
      ImGui::TableSetupColumn("Shift layer", ImGuiTableColumnFlags_WidthStretch, 1.4f);
      ImGui::TableHeadersRow();
      for (int i = 0; i < B_COUNT; ++i) {
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(btnName(i), selBtn_ == i, ImGuiSelectableFlags_SpanAllColumns)) selBtn_ = i;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(i == prof_.layerBtn ? "(shift button)" : actionToString(prof_.bind[0][i]).c_str());
        ImGui::TableNextColumn();
        if (i == prof_.layerBtn || prof_.layerBtn < 0) ImGui::TextDisabled("-");
        else ImGui::TextUnformatted(actionToString(prof_.bind[1][i]).c_str());
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Reset all bindings to defaults")) ImGui::OpenPopup("Reset bindings");
    ImVec2 ctr = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reset bindings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Reset every button, shift layer and combo in this profile\nto the DeskControl defaults?");
      ImGui::Spacing();
      if (ImGui::Button("Reset", ImVec2(S(110), 0))) {
        const Profile d = Profile::makeDefault();
        memcpy(prof_.bind, d.bind, sizeof(prof_.bind));
        prof_.chords = d.chords;
        prof_.layerBtn = -1;
        ch = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(S(110), 0))) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  W::endCard();

  if (ch) commit();
}

// ------------------------------------------------------------------- combos
void UI::pageCombos() {
  bool ch = false;
  W::dimWrapped("A combo fires when several buttons are held together - for example Start + Back to pause DeskControl. While a combo is possible, its buttons wait a short moment before acting on their own.");
  ImGui::Spacing();

  if (chordCap_ >= 0) {
    const uint32_t b = st_.pad.bits;
    if (b) {
      if (popcount32(b) >= popcount32(chordPeak_)) chordPeak_ = b;
    } else if (chordPeak_) {
      if (chordCap_ < (int)prof_.chords.size() && popcount32(chordPeak_) >= 2) {
        prof_.chords[chordCap_].mask = chordPeak_;
        ch = true;
        status_.clear();
      } else {
        status_ = "Hold two or more buttons at the same time, then let go.";
      }
      chordCap_ = -1;
      chordPeak_ = 0;
    }
  }

  int del = -1;
  for (size_t i = 0; i < prof_.chords.size(); ++i) {
    ChordDef& c = prof_.chords[i];
    ImGui::PushID((int)i);
    W::beginCard("##chord", ImVec2(0, 0));
    const bool capturing = chordCap_ == (int)i;
    W::dim("Buttons");
    ImGui::SameLine();
    const std::string mt = capturing ? (chordPeak_ ? W::maskText(chordPeak_) : "Hold your buttons now...") : W::maskText(c.mask);
    ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted(mt.c_str());
    ImGui::PopFont();
    if (ImGui::Button(capturing ? "Cancel" : "Set from controller")) {
      chordCap_ = capturing ? -1 : (int)i;
      chordPeak_ = 0;
      status_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove combo")) del = (int)i;
    ImGui::Spacing();
    ch |= actionEditor("act", c.action, true);
    W::endCard();
    ImGui::PopID();
    ImGui::Spacing();
  }
  if (del >= 0) {
    prof_.chords.erase(prof_.chords.begin() + del);
    if (chordCap_ == del) chordCap_ = -1;
    else if (chordCap_ > del) --chordCap_;
    ch = true;
  }
  if (prof_.chords.empty()) { W::dim("No combos yet."); ImGui::Spacing(); }

  if (ImGui::Button("Add combo")) {
    prof_.chords.push_back(ChordDef());
    chordCap_ = (int)prof_.chords.size() - 1;
    chordPeak_ = 0;
    ch = true;
  }
  if (!status_.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.96f, 0.75f, 0.27f, 1), "%s", status_.c_str()); }

  ImGui::Spacing();
  W::beginCard("##chordwin", ImVec2(0, 0));
  ch |= W::sliderInt("Combo window", &prof_.chordWindowMs, 0, 400, "%d ms",
                     "How long a button that belongs to a combo waits for its partner before acting alone. 0 turns the wait off (combos then may trigger the single-button actions too).");
  W::endCard();

  if (ch) commit();
}
