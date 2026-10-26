// DeskControl - on-screen keyboard layout and key-to-key navigation (platform independent).
#pragma once
#include <cstdint>
#include <vector>

struct KbKey {
  uint16_t vk;          // Windows virtual-key code
  const char* label;    // fixed caption (UTF-8), or nullptr for character keys
  const char* lo;       // character keys: US caption without Shift (fallback; the real caption comes from the active layout)
  const char* hi;       // character keys: US caption with Shift
  float x, y, w, h;     // position and size in key units (1.0 = one standard key)
};

const std::vector<KbKey>& kbKeys();
float kbWidth();        // total layout size in key units
float kbHeight();

// Moves the selection one key in a direction (dx/dy in {-1,0,1}). Left/right wrap around the
// row. Returns `from` when there is nowhere to go and -1 when `from` is not a valid key.
int kbNavigate(int from, int dx, int dy);

// Key whose rectangle is closest to a point given in key units.
int kbNearest(float x, float y);

bool kbIsModifier(uint16_t vk);     // Shift / Ctrl / Alt: latch until the next key is typed
bool kbIsWinKey(uint16_t vk);       // tapped, not latched (a lone Win press opens the Start menu)
