#include "kbdlayout.h"

#include <cmath>

namespace {

constexpr float kRowGap = 0.3f;         // extra space under the function-key row
constexpr float kMainW = 15.0f;
constexpr float kNavX = 15.4f;          // navigation cluster starts here

struct Ch { uint16_t vk; const char* lo; const char* hi; };

std::vector<KbKey> build() {
  std::vector<KbKey> k;
  auto fixed = [&](uint16_t vk, const char* label, float x, float row, float w) {
    const float y = row == 0 ? 0.0f : kRowGap + row;
    k.push_back({vk, label, nullptr, nullptr, x, y, w, 1.0f});
  };
  auto chars = [&](const Ch* list, int n, float x, float row) {
    const float y = kRowGap + row;
    for (int i = 0; i < n; ++i) k.push_back({list[i].vk, nullptr, list[i].lo, list[i].hi, x + i, y, 1.0f, 1.0f});
  };

  // Row 0: Esc + function keys
  fixed(0x1B, "Esc", 0, 0, 1);
  static const char* const fn[12] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
  for (int i = 0; i < 12; ++i) fixed((uint16_t)(0x70 + i), fn[i], 1.0f + i, 0, 1);

  // Row 1: numbers
  static const Ch r1[] = {{0xC0, "`", "~"}, {0x31, "1", "!"}, {0x32, "2", "@"}, {0x33, "3", "#"}, {0x34, "4", "$"},
                          {0x35, "5", "%"}, {0x36, "6", "^"}, {0x37, "7", "&"}, {0x38, "8", "*"}, {0x39, "9", "("},
                          {0x30, "0", ")"}, {0xBD, "-", "_"}, {0xBB, "=", "+"}};
  chars(r1, 13, 0, 1);
  fixed(0x08, "Backspace", 13, 1, 2);

  // Row 2: QWERTY
  fixed(0x09, "Tab", 0, 2, 1.5f);
  static const Ch r2[] = {{0x51, "q", "Q"}, {0x57, "w", "W"}, {0x45, "e", "E"}, {0x52, "r", "R"}, {0x54, "t", "T"},
                          {0x59, "y", "Y"}, {0x55, "u", "U"}, {0x49, "i", "I"}, {0x4F, "o", "O"}, {0x50, "p", "P"},
                          {0xDB, "[", "{"}, {0xDD, "]", "}"}};
  chars(r2, 12, 1.5f, 2);
  k.push_back({0xDC, nullptr, "\\", "|", 13.5f, kRowGap + 2, 1.5f, 1.0f});

  // Row 3: home row
  fixed(0x14, "Caps", 0, 3, 1.75f);
  static const Ch r3[] = {{0x41, "a", "A"}, {0x53, "s", "S"}, {0x44, "d", "D"}, {0x46, "f", "F"}, {0x47, "g", "G"},
                          {0x48, "h", "H"}, {0x4A, "j", "J"}, {0x4B, "k", "K"}, {0x4C, "l", "L"}, {0xBA, ";", ":"},
                          {0xDE, "'", "\""}};
  chars(r3, 11, 1.75f, 3);
  fixed(0x0D, "Enter", 12.75f, 3, 2.25f);

  // Row 4: bottom letter row
  fixed(0xA0, "Shift", 0, 4, 2.25f);
  static const Ch r4[] = {{0x5A, "z", "Z"}, {0x58, "x", "X"}, {0x43, "c", "C"}, {0x56, "v", "V"}, {0x42, "b", "B"},
                          {0x4E, "n", "N"}, {0x4D, "m", "M"}, {0xBC, ",", "<"}, {0xBE, ".", ">"}, {0xBF, "/", "?"}};
  chars(r4, 10, 2.25f, 4);
  fixed(0xA1, "Shift", 12.25f, 4, 2.75f);

  // Row 5: modifiers + space
  fixed(0xA2, "Ctrl", 0, 5, 1.5f);
  fixed(0x5B, "Win", 1.5f, 5, 1.25f);
  fixed(0xA4, "Alt", 2.75f, 5, 1.5f);
  fixed(0x20, "", 4.25f, 5, 6.5f);
  fixed(0xA5, "Alt", 10.75f, 5, 1.5f);
  fixed(0x5D, "Menu", 12.25f, 5, 1.25f);
  fixed(0xA3, "Ctrl", 13.5f, 5, 1.5f);

  // Navigation cluster
  fixed(0x2D, "Ins", kNavX, 1, 1);
  fixed(0x24, "Home", kNavX + 1, 1, 1);
  fixed(0x21, "PgUp", kNavX + 2, 1, 1);
  fixed(0x2E, "Del", kNavX, 2, 1);
  fixed(0x23, "End", kNavX + 1, 2, 1);
  fixed(0x22, "PgDn", kNavX + 2, 2, 1);
  fixed(0x26, "↑", kNavX + 1, 4, 1);
  fixed(0x25, "←", kNavX, 5, 1);
  fixed(0x28, "↓", kNavX + 1, 5, 1);
  fixed(0x27, "→", kNavX + 2, 5, 1);
  return k;
}

float cx(const KbKey& k) { return k.x + k.w * 0.5f; }
float cy(const KbKey& k) { return k.y + k.h * 0.5f; }

}  // namespace

const std::vector<KbKey>& kbKeys() {
  static const std::vector<KbKey> keys = build();
  return keys;
}

float kbWidth() { return kNavX + 3.0f; }
float kbHeight() { return kRowGap + 6.0f; }

int kbNavigate(int from, int dx, int dy) {
  const auto& keys = kbKeys();
  if (from < 0 || from >= (int)keys.size()) return -1;
  if (dx == 0 && dy == 0) return from;

  const float fx = cx(keys[from]), fy = cy(keys[from]);
  int best = -1;
  float bestScore = 1e9f;
  for (int i = 0; i < (int)keys.size(); ++i) {
    if (i == from) continue;
    const float ddx = cx(keys[i]) - fx, ddy = cy(keys[i]) - fy;
    const float along = dx != 0 ? ddx * (float)dx : ddy * (float)dy;
    const float across = std::fabs(dx != 0 ? ddy : ddx);
    if (along < 0.3f) continue;                 // must really be further in that direction
    if (dx != 0 && across > 0.6f) continue;     // sideways moves stay in the row
    const float score = along + 2.5f * across;
    if (score < bestScore) { bestScore = score; best = i; }
  }
  if (best >= 0) return best;

  if (dx != 0) {                                // wrap around the row
    float edge = 0;
    for (int i = 0; i < (int)keys.size(); ++i) {
      if (i == from || std::fabs(cy(keys[i]) - fy) > 0.6f) continue;
      const float x = cx(keys[i]);
      if (best < 0 || (dx > 0 ? x < edge : x > edge)) { best = i; edge = x; }
    }
    if (best >= 0) return best;
  }
  return from;
}

int kbNearest(float x, float y) {
  const auto& keys = kbKeys();
  int best = 0;
  float bestD = 1e9f;
  for (int i = 0; i < (int)keys.size(); ++i) {
    const KbKey& k = keys[i];
    const float ex = x < k.x ? k.x - x : (x > k.x + k.w ? x - k.x - k.w : 0.0f);
    const float ey = y < k.y ? k.y - y : (y > k.y + k.h ? y - k.y - k.h : 0.0f);
    const float d = ex * ex + ey * ey;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

bool kbIsModifier(uint16_t vk) {
  return vk == 0xA0 || vk == 0xA1 || vk == 0xA2 || vk == 0xA3 || vk == 0xA4 || vk == 0xA5;
}

bool kbIsWinKey(uint16_t vk) { return vk == 0x5B || vk == 0x5C; }
