#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "store.h"
#include "ui.h"
#include "ui_widgets.h"

// -------------------------------------------------------------------- sticks
void UI::pageSticks() {
  bool ch = false;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float leftW = std::floor(avail * 0.60f);

  ImGui::BeginChild("##leftcol", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY);

  W::beginCard("##cursor");
  W::title("Cursor");
  ch |= W::sliderInt("Dead zone", &prof_.deadZone, 0, 20000, "%d",
                     "How far the stick must move before the cursor reacts. Raise it if the cursor drifts on its own. Use the live view on the right to tune it.");
  ch |= W::sliderFloat("Response curve", &prof_.curve, 0.0f, 4.0f, prof_.curve < 0.01f ? "Linear" : "%.2f",
                       "0 = linear. Higher values make small movements gentler and big movements faster, for precise control near the centre.");
  ch |= W::sliderFloat("Precision mode speed", &prof_.precisionFactor, 0.05f, 1.0f, "%.2fx",
                       "Cursor speed multiplier while a button assigned to \"Precision mode\" is held. Assign it under Buttons.");
  ImGui::Spacing();
  ch |= W::sw("Swap sticks (right stick = cursor)", &prof_.swapSticks);
  ch |= W::sw("Invert vertical cursor movement", &prof_.invertMouseY);
  ch |= W::sw("Relative mouse movement", &prof_.relativeMouse);
  W::help("Sends mouse movement instead of placing the cursor. Try this in games and remote-desktop windows that ignore normal cursor placement.");
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##scroll");
  W::title("Scrolling");
  ch |= W::sliderInt("Scroll dead zone", &prof_.scrollDeadZone, 0, 20000, "%d");
  ch |= W::sliderFloat("Scroll speed", &prof_.scrollSpeed, 0.01f, 1.0f, "%.3f", nullptr, true);
  ImGui::Spacing();
  ch |= W::sw("Invert vertical scrolling", &prof_.scrollInvertV);
  ch |= W::sw("Invert horizontal scrolling", &prof_.scrollInvertH);
  ch |= W::sw("Horizontal scrolling", &prof_.scrollHorizontal);
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##trig");
  W::title("Triggers");
  ch |= W::sliderInt("Trigger threshold", &prof_.triggerThreshold, 0, 250, "%d / 255",
                     "How far a trigger must be pulled to count as a button press.");
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##speeds");
  W::title("Cursor speed presets");
  W::dimWrapped("Switch between these with the speed combo (default LB + RB). The selected radio button is the speed used when this profile loads.");
  int del = -1;
  for (size_t i = 0; i < prof_.speeds.size(); ++i) {
    ImGui::PushID((int)i);
    if (ImGui::RadioButton("##def", prof_.defaultSpeed == (int)i)) { prof_.defaultSpeed = (int)i; ch = true; }
    ImGui::SameLine();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s", prof_.speeds[i].name.c_str());
    ImGui::SetNextItemWidth(S(130));
    if (ImGui::InputText("##name", buf, sizeof(buf))) { prof_.speeds[i].name = buf; ch = true; }
    ImGui::SameLine();
    const bool canRemove = prof_.speeds.size() > 1;
    ImGui::SetNextItemWidth(-(canRemove ? S(96) : 0.0f) - (canRemove ? ImGui::GetStyle().ItemSpacing.x : 0.0f));
    ch |= ImGui::SliderFloat("##val", &prof_.speeds[i].value, 0.002f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    if (canRemove) {
      ImGui::SameLine();
      if (ImGui::Button("Remove", ImVec2(S(88), 0))) del = (int)i;
    }
    ImGui::PopID();
  }
  if (del >= 0) {
    prof_.speeds.erase(prof_.speeds.begin() + del);
    if (prof_.defaultSpeed >= del && prof_.defaultSpeed > 0) --prof_.defaultSpeed;
    ch = true;
  }
  if (prof_.speeds.size() < 8 && ImGui::Button("Add speed")) {
    prof_.speeds.push_back({"Custom", 0.03f});
    ch = true;
  }
  W::endCard();

  ImGui::EndChild();
  ImGui::SameLine();

  W::beginCard("##live", ImVec2(0, 0));
  W::title("Live view");
  W::dimWrapped("The grey disc is the dead zone. Move your sticks and keep the dot inside it while they rest.");
  const float d = std::min(S(170), (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
  const bool sw = prof_.swapSticks;
  drawStick(sw ? "Left (scroll)" : "Left (cursor)", d, st_.pad.lx, st_.pad.ly, sw ? prof_.scrollDeadZone : prof_.deadZone, (st_.pad.bits >> B_LTHUMB) & 1u);
  ImGui::SameLine();
  drawStick(sw ? "Right (cursor)" : "Right (scroll)", d, st_.pad.rx, st_.pad.ry, sw ? prof_.deadZone : prof_.scrollDeadZone, (st_.pad.bits >> B_RTHUMB) & 1u);
  if (!st_.pad.connected) W::dim("No controller connected.");
  W::endCard();

  if (ch) commit();
}

// ------------------------------------------------------------------ profiles
void UI::pageProfiles() {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float leftW = std::floor(avail * 0.42f);
  namespace fs = std::filesystem;

  ImVec2 ctr = ImGui::GetMainViewport()->GetCenter();
  W::beginCard("##profiles", ImVec2(leftW, 0));
  W::title("Profiles");
  W::dimWrapped("A profile stores bindings, combos and stick settings. Click one to edit it and use it.");
  ImGui::Spacing();
  for (const auto& n : profiles_) {
    const bool cur = iequals(n, prof_.name);
    if (ImGui::Selectable((n + (cur ? "   (in use)" : "")).c_str(), cur) && !cur) selectProfile(n);
  }
  ImGui::Spacing();

  auto flow = [&](const char* label, bool first) {          // buttons that wrap instead of clipping
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
    if (!first) {
      const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right) ImGui::SameLine();
    }
    return ImGui::Button(label);
  };
  if (flow("New", true)) { nameBuf_[0] = 0; ImGui::OpenPopup("New profile"); }
  if (flow("Duplicate", false)) { std::snprintf(nameBuf_, sizeof(nameBuf_), "%s copy", prof_.name.c_str()); ImGui::OpenPopup("Duplicate profile"); }
  if (flow("Rename", false)) { std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", prof_.name.c_str()); ImGui::OpenPopup("Rename profile"); }
  ImGui::BeginDisabled(profiles_.size() <= 1);
  if (flow("Delete", false)) ImGui::OpenPopup("Delete profile");
  ImGui::EndDisabled();
  ImGui::Spacing();
  if (ImGui::Button("Import Gopher 1.x config.ini...")) {
    std::wstring path;
    if (Platform::openFileDialog(hwnd_, L"Import a Gopher 1.x config.ini", L"INI files\0*.ini\0All files\0*.*\0", path)) {
      std::string text;
      Profile p;
      std::vector<std::string> warn;
      if (Store::readFile(fs::path(path), text) && importLegacyText(text, p, &warn)) {
        flushPending();
        p.name = Store::uniqueName("Imported");
        Store::saveProfile(p);
        refreshProfiles();
        selectProfile(p.name);
        status_ = "Imported as \"" + p.name + "\"" + (warn.empty() ? "." : " (" + std::to_string(warn.size()) + " note(s): " + warn[0] + ")");
      } else {
        status_ = warn.empty() ? "Could not read that file." : warn[0];
      }
    }
  }
  if (!status_.empty()) W::dimWrapped(status_.c_str());

  auto nameField = [&](const char* hint) {
    ImGui::SetNextItemWidth(S(280));
    const bool enter = ImGui::InputTextWithHint("##pname", hint, nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    return enter;
  };
  const std::string wanted = Store::sanitizeName(nameBuf_);
  const bool taken = !wanted.empty() && Store::profileExists(wanted);

  ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("New profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool enter = nameField("Profile name");
    if (taken) W::dim("A profile with that name already exists.");
    ImGui::BeginDisabled(wanted.empty() || taken);
    if (ImGui::Button("Create", ImVec2(S(130), 0)) || (enter && !wanted.empty() && !taken)) {
      flushPending();
      Profile p = Profile::makeDefault();
      p.name = wanted;
      Store::saveProfile(p);
      refreshProfiles();
      selectProfile(p.name);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Duplicate profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    nameField("New profile name");
    if (taken) W::dim("A profile with that name already exists.");
    ImGui::BeginDisabled(wanted.empty() || taken);
    if (ImGui::Button("Duplicate", ImVec2(S(130), 0))) {
      flushPending();
      Profile p = prof_;
      p.name = wanted;
      Store::saveProfile(p);
      refreshProfiles();
      selectProfile(p.name);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Rename profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    nameField("New name");
    const bool clash = taken && !iequals(wanted, prof_.name);
    if (clash) W::dim("A profile with that name already exists.");
    ImGui::BeginDisabled(wanted.empty() || clash);
    if (ImGui::Button("Rename", ImVec2(S(130), 0))) {
      flushPending();
      const std::string old = prof_.name;
      if (Store::renameProfile(old, wanted)) {
        for (auto& r : app_.rules) if (iequals(r.profile, old)) r.profile = wanted;
        refreshProfiles();
        appDirty_ = true;
        selectProfile(wanted);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete the profile \"%s\"? This cannot be undone.", prof_.name.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Delete", ImVec2(S(130), 0))) {
      const std::string old = prof_.name;
      dirty_ = false;
      Store::deleteProfile(old);
      app_.rules.erase(std::remove_if(app_.rules.begin(), app_.rules.end(), [&](const Rule& r) { return iequals(r.profile, old); }), app_.rules.end());
      refreshProfiles();
      appDirty_ = true;
      if (!profiles_.empty()) selectProfile(profiles_[0]);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  W::endCard();
  ImGui::SameLine();

  // ---- rules
  W::beginCard("##rules", ImVec2(0, 0));
  W::title("App rules");
  W::dimWrapped("Pause DeskControl or switch profile automatically while a program is in front - for instance pause it in a game that has its own controller support.");
  ImGui::Spacing();

  int del = -1;
  bool rch = false;
  if (!app_.rules.empty() && ImGui::BeginTable("rules", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Program", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("When it is in front", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(80));
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < app_.rules.size(); ++i) {
      Rule& r = app_.rules[i];
      ImGui::PushID((int)i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.exe.c_str());
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      const std::string prev = r.profile.empty() ? "Pause DeskControl" : "Use profile: " + r.profile;
      if (ImGui::BeginCombo("##act", prev.c_str())) {
        if (ImGui::Selectable("Pause DeskControl", r.profile.empty())) { r.profile.clear(); rch = true; }
        for (const auto& n : profiles_)
          if (ImGui::Selectable(("Use profile: " + n).c_str(), iequals(n, r.profile))) { r.profile = n; rch = true; }
        ImGui::EndCombo();
      }
      ImGui::TableNextColumn();
      if (ImGui::Button("Remove")) del = (int)i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  } else if (app_.rules.empty()) {
    W::dim("No rules yet.");
  }
  if (del >= 0) { app_.rules.erase(app_.rules.begin() + del); rch = true; }

  ImGui::Spacing();
  if (ImGui::Button("Add from running programs...")) {
    runningExes_ = Platform::visibleWindowExes();
    ImGui::OpenPopup("Pick program");
  }
  ImGui::SetNextWindowPos(ctr, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(S(320), 0), ImVec2(S(420), S(420)));
  if (ImGui::BeginPopup("Pick program")) {
    W::dim("Programs with a visible window");
    for (const auto& exe : runningExes_) {
      bool exists = false;
      for (const auto& r : app_.rules) if (iequals(r.exe, exe)) exists = true;
      if (exists) continue;
      if (ImGui::Selectable(exe.c_str())) { app_.rules.push_back({exe, ""}); rch = true; ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndPopup();
  }
  ImGui::SetNextItemWidth(S(220));
  ImGui::InputTextWithHint("##manual", "or type a name, e.g. game.exe", ruleBuf_, sizeof(ruleBuf_));
  ImGui::SameLine();
  if (ImGui::Button("Add") && trim(ruleBuf_).size()) {
    app_.rules.push_back({toLower(trim(ruleBuf_)), ""});
    ruleBuf_[0] = 0;
    rch = true;
  }
  W::endCard();

  if (rch) { appDirty_ = true; ruleDirty_ = true; }
}

// ------------------------------------------------------------------ settings
void UI::pageSettings() {
  bool ch = false;
  bool ach = false;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float leftW = std::floor(avail * 0.5f - S(5));

  ImGui::BeginChild("##setL", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY);
  W::beginCard("##app");
  W::title("Application");
  if (W::sw("Start with Windows", &autostart_)) {
    if (!Platform::setAutostart(autostart_)) autostart_ = Platform::getAutostart();
  }
  ach |= W::sw("Start minimized to the tray", &app_.startMinimized);
  ach |= W::sw("Close button hides to the tray", &app_.closeToTray);
  ImGui::Spacing();
  if (ImGui::Button("Open settings folder")) Platform::openFolder(Platform::dataDir());
  if (!elevated_) {
    ImGui::SameLine();
    if (ImGui::Button("Restart as administrator")) {
      saveNow();
      if (Platform::relaunchElevated()) requestQuit();
    }
  }
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##osk");
  W::title("On-screen keyboard");
  ach |= W::sw("Use the DeskControl keyboard", &app_.builtinKeyboard);
  W::help("DeskControl's own keyboard: it never steals focus from the program you are typing into. Move the cursor over a key and click (A), or step between keys with the D-pad. Tap Shift, Ctrl or Alt to hold it for the next key. Turn this off to use the Windows on-screen keyboard instead.");
  if (app_.builtinKeyboard) {
    ach |= W::sw("D-pad steps between keys", &app_.keyboardDpad);
    W::help("While the cursor is on the keyboard the D-pad jumps from key to key instead of sending arrow keys.");
  }
  if (ImGui::Button("Show / hide keyboard now")) Osk::toggle();
  ImGui::Spacing();
  ach |= W::sw("Show when I click a text box", &app_.autoKeyboard);
  W::help("After a controller click, DeskControl checks whether a text box has focus (search bars, address bars, chat boxes, documents) and opens the on-screen keyboard for you.");
  ach |= W::sw("Hide when I click elsewhere", &app_.autoHideKeyboard);
  W::help("Only closes a keyboard that DeskControl opened itself. One you opened yourself stays put.");
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##dev");
  W::title("Controller");
  W::label("Controller to use");
  ImGui::SetNextItemWidth(-FLT_MIN);
  const std::string prev = app_.controller < 0 ? "Automatic (first one found)" : "Controller " + std::to_string(app_.controller + 1);
  if (ImGui::BeginCombo("##ctl", prev.c_str())) {
    if (ImGui::Selectable("Automatic (first one found)", app_.controller < 0)) { app_.controller = -1; ach = true; }
    for (int i = 0; i < 4; ++i)
      if (ImGui::Selectable(("Controller " + std::to_string(i + 1)).c_str(), app_.controller == i)) { app_.controller = i; ach = true; }
    ImGui::EndCombo();
  }
  ach |= W::sliderInt("Update rate", &app_.pollHz, 60, 500, "%d Hz",
                      "How often the controller is read. 150 Hz feels smooth; higher values use a little more CPU.");
  W::endCard();
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("##setR", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
  W::beginCard("##rumble");
  W::title("Rumble (this profile)");
  ch |= W::sw("Rumble feedback on by default", &prof_.vibration);
  ch |= W::sliderFloat("Rumble strength", &prof_.vibrationStrength, 0.0f, 1.0f, "%.2f");
  if (ImGui::Button("Test rumble")) engine_.testRumble();
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##keys");
  W::title("Key repeat (this profile)");
  ch |= W::sliderInt("Repeat delay", &prof_.repeatDelayMs, 100, 1000, "%d ms", "How long a repeating key is held before it starts repeating.");
  ch |= W::sliderInt("Repeat rate", &prof_.repeatRateHz, 2, 60, "%d per second");
  W::endCard();
  ImGui::Spacing();

  W::beginCard("##look");
  W::title("Appearance");
  W::label("Accent colour");
  for (int i = 0; i < accentCount(); ++i) {
    ImGui::PushID(i);
    ImGui::PushStyleColor(ImGuiCol_Button, accentColor(i));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentColor(i));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentColor(i));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##acc", ImVec2(S(30), S(30)))) { app_.accent = i; ach = true; rebuildStyle_ = true; }
    ImGui::PopStyleColor(3);
    if (app_.accent == i) ImGui::GetWindowDrawList()->AddRect(p - ImVec2(3, 3), p + ImVec2(S(30), S(30)) + ImVec2(3, 3), IM_COL32(240, 243, 250, 255), S(9), 0, 2.0f);
    ImGui::PopID();
    if (i + 1 < accentCount()) ImGui::SameLine();
  }
  W::label("Interface size");
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::SliderFloat("##scale", &app_.uiScale, 0.8f, 1.6f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::IsItemDeactivatedAfterEdit()) { ach = true; rebuildStyle_ = true; }
  W::endCard();
  ImGui::EndChild();

  if (ch) commit();
  if (ach) { appDirty_ = true; pushDevice(); }
}

// ----------------------------------------------------------------------- log
void UI::pageLog() {
  static bool autoScroll = true;
  if (ImGui::Button("Clear")) logLines_.clear();
  ImGui::SameLine();
  ImGui::Checkbox("Follow newest", &autoScroll);
  ImGui::Spacing();

  W::beginCard("##logcard", ImVec2(0, std::max(S(200), ImGui::GetContentRegionAvail().y - S(12))));
  if (logLines_.empty()) W::dim("Nothing yet. Connection changes, speed changes and toggles appear here.");
  for (const auto& l : logLines_) {
    const int t = (int)l.time;
    ImGui::TextDisabled("%02d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    ImGui::SameLine();
    ImGui::TextUnformatted(l.text.c_str());
  }
  if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
  W::endCard();
}

// --------------------------------------------------------------------- about
void UI::pageAbout() {
  W::beginCard("##about");
  ImGui::PushFont(g_fontTitle);
  ImGui::TextUnformatted("DeskControl");
  ImGui::PopFont();
  W::dim("Version " DESKCONTROL_VERSION);
  ImGui::Spacing();
  ImGui::TextWrapped("Control your whole PC from the couch: the sticks drive the mouse, the buttons become keys and clicks. DeskControl works with any application, no game support needed.");
  ImGui::Spacing();
  W::title("What is new in 2.0");
  ImGui::BulletText("A real interface with a live controller view and click-to-edit bindings");
  ImGui::BulletText("Profiles, a shift layer, custom combos and per-app rules");
  ImGui::BulletText("Key capture, modifier shortcuts, mouse X buttons and key repeat");
  ImGui::BulletText("Tray icon, start with Windows, battery and hot-plug detection");
  ImGui::BulletText("Smoother, non-blocking rumble and fixes for scrolling and arrow keys");
  ImGui::Spacing();
  W::dimWrapped("Based on Gopher360 by Tylemagne (GPL v3). This version is free software under the same licence. Interface by Dear ImGui (MIT).");
  W::endCard();
}
