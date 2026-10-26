// Small ImGui widget helpers shared by the UI translation units.
#pragma once
#include <string>
#include <vector>

#include "ui.h"

extern ImFont* g_fontBold;
extern ImFont* g_fontTitle;
int accentCount();
const char* accentName(int i);
ImVec4 accentColor(int i);

namespace W {

inline ImU32 col(ImGuiCol c, float alpha = 1.0f) {
  ImVec4 v = ImGui::GetStyle().Colors[c];
  v.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(v);
}
inline ImU32 accent(float alpha = 1.0f) { return col(ImGuiCol_CheckMark, alpha); }

inline bool beginCard(const char* id, ImVec2 size = ImVec2(0, 0), ImU32 border = 0) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
  ImGui::PushStyleColor(ImGuiCol_Border, border ? ImGui::ColorConvertU32ToFloat4(border) : ImGui::GetStyle().Colors[ImGuiCol_Border]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(14)));
  ImGuiChildFlags f = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
  if (size.y == 0) f |= ImGuiChildFlags_AutoResizeY;
  bool r = ImGui::BeginChild(id, size, f, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
  return r;
}
inline void endCard() { ImGui::EndChild(); }

inline void title(const char* t) {
  ImGui::PushFont(g_fontBold);
  ImGui::TextUnformatted(t);
  ImGui::PopFont();
}
inline void dim(const char* t) { ImGui::TextDisabled("%s", t); }
inline void dimWrapped(const char* t) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("%s", t);
  ImGui::PopStyleColor();
}
inline void help(const char* t) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(S(340));
    ImGui::TextUnformatted(t);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// iOS-style switch with a label on its right. Returns true when toggled.
inline bool sw(const char* label, bool* v) {
  ImGui::PushID(label);
  const float fh = ImGui::GetFrameHeight();
  const float h = fh * 0.78f, w = h * 1.85f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##sw", ImVec2(w, fh));
  bool changed = false;
  if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
  const bool hov = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 a(p.x, p.y + (fh - h) * 0.5f), b(a.x + w, a.y + h);
  dl->AddRectFilled(a, b, *v ? accent(hov ? 1.0f : 0.9f) : col(ImGuiCol_Border, hov ? 1.0f : 0.85f), h * 0.5f);
  const float kx = *v ? b.x - h * 0.5f : a.x + h * 0.5f;
  dl->AddCircleFilled(ImVec2(kx, a.y + h * 0.5f), h * 0.5f - S(3), IM_COL32(245, 247, 250, 255), 24);
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::PopID();
  return changed;
}

// Wrapping row of toggle buttons; returns the clicked index or -1.
inline int segmented(const char* id, const std::vector<std::string>& labels, int sel) {
  int clicked = -1;
  const float maxX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  ImGui::PushID(id);
  for (size_t i = 0; i < labels.size(); ++i) {
    const float w = ImGui::CalcTextSize(labels[i].c_str()).x + S(26);
    if (i > 0) {
      const float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
      if (x + w <= maxX) ImGui::SameLine();
    }
    const bool on = (int)i == sel;
    if (on) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(accent()));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(accent()));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.09f, 1));
    }
    ImGui::PushID((int)i);
    if (ImGui::Button(labels[i].c_str(), ImVec2(w, 0))) clicked = (int)i;
    ImGui::PopID();
    if (on) ImGui::PopStyleColor(3);
  }
  ImGui::PopID();
  return clicked;
}

inline void pill(const char* text, ImU32 bg, ImU32 fg) {
  ImVec2 ts = ImGui::CalcTextSize(text);
  ImVec2 pad(S(10), S(3));
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, p + ts + pad * 2, bg, (ts.y + pad.y * 2) * 0.5f);
  ImGui::GetWindowDrawList()->AddText(p + pad, fg, text);
  ImGui::Dummy(ts + pad * 2);
}

inline void label(const char* t, const char* helpText = nullptr) {
  dim(t);
  if (helpText) help(helpText);
}

inline bool sliderInt(const char* lbl, int* v, int lo, int hi, const char* fmt, const char* helpText = nullptr) {
  label(lbl, helpText);
  ImGui::PushID(lbl);
  ImGui::SetNextItemWidth(-FLT_MIN);
  bool c = ImGui::SliderInt("##s", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopID();
  return c;
}

inline bool sliderFloat(const char* lbl, float* v, float lo, float hi, const char* fmt, const char* helpText = nullptr, bool log = false) {
  label(lbl, helpText);
  ImGui::PushID(lbl);
  ImGui::SetNextItemWidth(-FLT_MIN);
  bool c = ImGui::SliderFloat("##s", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp | (log ? ImGuiSliderFlags_Logarithmic : 0));
  ImGui::PopID();
  return c;
}

inline std::string maskText(uint32_t mask) {
  std::string s;
  for (int i = 0; i < B_COUNT; ++i)
    if (mask & (1u << i)) {
      if (!s.empty()) s += " + ";
      std::string id = btnId(i);
      for (auto& c : id) if (c == '_') c = ' ';
      s += id;
    }
  return s.empty() ? "(no buttons yet)" : s;
}

}  // namespace W
