// DeskControl - input core (platform independent).
//
// Turns a stream of controller snapshots into keyboard / mouse / special
// actions according to a Profile. All side effects go through IOutput so the
// logic can be unit tested without Windows.
#pragma once
#include <cstdint>
#include <string>
#include "model.h"

struct PadState {
  bool connected = false;
  uint32_t bits = 0;                 // bit i set => Btn i is down (triggers included)
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

enum CoreEvent { EV_TOGGLE_WINDOW = 1, EV_TOGGLE_OSK = 2, EV_NEXT_PROFILE = 3 };

class IOutput {
 public:
  virtual ~IOutput() {}
  virtual void key(uint16_t vk, bool down) = 0;
  virtual void mouseButton(int mouseBtn, bool down) = 0;
  virtual void wheel(int amount, bool horizontal) = 0;
  virtual void moveCursor(int dx, int dy, bool relative) = 0;   // dy: screen space (down is +)
  virtual void rumble(int durationMs, float strength) = 0;      // must not block
  virtual void event(int coreEvent) = 0;
  virtual void log(const std::string& line) = 0;
};

class Core {
 public:
  explicit Core(IOutput* out);

  // reset = true when switching to a different profile (restores default
  // speed / vibration). Changing bindings releases anything currently held.
  void setProfile(const Profile& p, bool reset);
  const Profile& profile() const { return prof_; }

  // now / dt are in seconds.
  void step(const PadState& pad, double now, double dt);
  void releaseAll();

  bool enabled() const { return enabled_; }
  void setEnabled(bool e);
  bool suspended() const { return suspended_; }
  void setSuspended(bool s) { suspended_ = s; }     // no output at all (used while capturing keys)
  int speedIndex() const { return speedIdx_; }
  void setSpeedIndex(int i);
  bool vibration() const { return vibOn_; }
  void setVibration(bool v) { vibOn_ = v; }
  bool precision() const { return precisionCount_ > 0; }
  float currentSpeed() const;
  std::string currentSpeedName() const;

 private:
  struct Slot { Action act; bool held = false; double nextRepeat = 0; };
  struct BtnRt { bool down = false, pending = false, consumed = false; double downT = 0; Slot slot; };
  struct ChordRt { bool on = false; Slot slot; };

  void pressSlot(Slot& s, double now);
  void releaseSlot(Slot& s);
  void repeatSlot(Slot& s, double now);
  void execSpecial(int special, bool press);
  void sendMods(uint8_t mods, bool down);
  bool allowed(const Action& a) const;
  void rebuildMembers();
  void moveCursor(const PadState& pad, double dt);
  void scroll(const PadState& pad, double dt);
  void pulse(int ms, float strength);

  IOutput* out_;
  Profile prof_;
  uint32_t memberMask_ = 0;          // buttons that take part in at least one chord
  BtnRt br_[B_COUNT];
  std::vector<ChordRt> chordRt_;

  bool enabled_ = true;
  bool suspended_ = false;
  bool wasConnected_ = false;
  bool vibOn_ = true;
  int speedIdx_ = 0;
  int precisionCount_ = 0;

  float accX_ = 0, accY_ = 0;        // sub-pixel remainders
  float scrollAccV_ = 0, scrollAccH_ = 0;
};
