#include "core.h"

#include <algorithm>
#include <cmath>

namespace {
const uint16_t kVkLShift = 0xA0, kVkLCtrl = 0xA2, kVkLAlt = 0xA4, kVkLWin = 0x5B;
const float kStickMax = 32767.0f;
}

Core::Core(IOutput* out) : out_(out) {
  prof_ = Profile::makeDefault();
  rebuildMembers();
  chordRt_.assign(prof_.chords.size(), ChordRt());
  speedIdx_ = prof_.defaultSpeed;
  vibOn_ = prof_.vibration;
}

void Core::rebuildMembers() {
  memberMask_ = 0;
  for (const auto& c : prof_.chords)
    if (popcount32(c.mask) >= 2 && c.action.type != ActType::None) memberMask_ |= c.mask;
}

void Core::setProfile(const Profile& p, bool reset) {
  const bool changed = reset || !bindingsEqual(prof_, p);
  if (changed) releaseAll();

  prof_ = p;
  prof_.clampValues();
  rebuildMembers();

  if (changed) {
    chordRt_.assign(prof_.chords.size(), ChordRt());
    uint32_t downBits = 0;
    for (int b = 0; b < B_COUNT; ++b) {
      if (br_[b].down) downBits |= 1u << b;
      br_[b].consumed = br_[b].down;       // a button held across an edit must be re-pressed
      br_[b].pending = false;
    }
    for (size_t i = 0; i < prof_.chords.size(); ++i)
      chordRt_[i].on = prof_.chords[i].mask && (downBits & prof_.chords[i].mask) == prof_.chords[i].mask;
  }
  if (reset) {
    speedIdx_ = prof_.defaultSpeed;
    vibOn_ = prof_.vibration;
    precisionCount_ = 0;
  }
  speedIdx_ = std::max(0, std::min(speedIdx_, (int)prof_.speeds.size() - 1));
}

float Core::currentSpeed() const {
  if (prof_.speeds.empty()) return 0.025f;
  return prof_.speeds[std::max(0, std::min(speedIdx_, (int)prof_.speeds.size() - 1))].value;
}

std::string Core::currentSpeedName() const {
  if (prof_.speeds.empty()) return "";
  return prof_.speeds[std::max(0, std::min(speedIdx_, (int)prof_.speeds.size() - 1))].name;
}

void Core::setSpeedIndex(int i) {
  speedIdx_ = std::max(0, std::min(i, (int)prof_.speeds.size() - 1));
}

void Core::setEnabled(bool e) {
  if (e == enabled_) return;
  enabled_ = e;
  if (!e) releaseAll();
  out_->log(e ? "DeskControl enabled" : "DeskControl disabled");
  pulse(400, e ? 1.0f : 0.15f);
}

void Core::pulse(int ms, float strength) {
  if (vibOn_) out_->rumble(ms, strength * prof_.vibrationStrength);
}

bool Core::allowed(const Action& a) const {
  return enabled_ || (a.type == ActType::Special && a.special == S_TOGGLE_ENABLED);
}

void Core::sendMods(uint8_t mods, bool down) {
  if (down) {
    if (mods & GM_CTRL) out_->key(kVkLCtrl, true);
    if (mods & GM_SHIFT) out_->key(kVkLShift, true);
    if (mods & GM_ALT) out_->key(kVkLAlt, true);
    if (mods & GM_WIN) out_->key(kVkLWin, true);
  } else {
    if (mods & GM_WIN) out_->key(kVkLWin, false);
    if (mods & GM_ALT) out_->key(kVkLAlt, false);
    if (mods & GM_SHIFT) out_->key(kVkLShift, false);
    if (mods & GM_CTRL) out_->key(kVkLCtrl, false);
  }
}

void Core::pressSlot(Slot& s, double now) {
  const Action& a = s.act;
  switch (a.type) {
    case ActType::Key: sendMods(a.mods, true); out_->key(a.vk, true); break;
    case ActType::Mouse: out_->mouseButton(a.mouse, true); break;
    case ActType::Special: execSpecial(a.special, true); break;
    default: return;
  }
  s.held = true;
  s.nextRepeat = now + prof_.repeatDelayMs / 1000.0;
}

void Core::releaseSlot(Slot& s) {
  if (!s.held) return;
  s.held = false;
  const Action& a = s.act;
  switch (a.type) {
    case ActType::Key: out_->key(a.vk, false); sendMods(a.mods, false); break;
    case ActType::Mouse: out_->mouseButton(a.mouse, false); break;
    case ActType::Special: execSpecial(a.special, false); break;
    default: break;
  }
}

void Core::repeatSlot(Slot& s, double now) {
  if (!s.held || s.act.type != ActType::Key || !s.act.repeat) return;
  if (now >= s.nextRepeat) {
    out_->key(s.act.vk, true);        // repeated key-down, like typematic repeat
    s.nextRepeat = now + 1.0 / std::max(1, prof_.repeatRateHz);
  }
}

void Core::execSpecial(int sp, bool press) {
  if (sp == S_PRECISION) {
    precisionCount_ += press ? 1 : -1;
    if (precisionCount_ < 0) precisionCount_ = 0;
    return;
  }
  if (!press) return;

  const int n = (int)prof_.speeds.size();
  auto announceSpeed = [&]() {
    out_->log("Cursor speed: " + currentSpeedName());
    pulse(180, 0.8f);
  };

  switch (sp) {
    case S_TOGGLE_ENABLED: setEnabled(!enabled_); break;
    case S_TOGGLE_VIBRATION:
      vibOn_ = !vibOn_;
      out_->log(vibOn_ ? "Vibration on" : "Vibration off");
      pulse(120, 0.6f);
      break;
    case S_CYCLE_SPEED:
      if (n > 0) { speedIdx_ = (speedIdx_ + 1) % n; announceSpeed(); }
      break;
    case S_SPEED_UP:
      if (speedIdx_ < n - 1) { ++speedIdx_; announceSpeed(); }
      break;
    case S_SPEED_DOWN:
      if (speedIdx_ > 0) { --speedIdx_; announceSpeed(); }
      break;
    case S_TOGGLE_WINDOW: out_->event(EV_TOGGLE_WINDOW); break;
    case S_TOGGLE_OSK: out_->event(EV_TOGGLE_OSK); break;
    case S_NEXT_PROFILE: out_->event(EV_NEXT_PROFILE); break;
    default: break;
  }
}

void Core::releaseAll() {
  for (int b = 0; b < B_COUNT; ++b) { releaseSlot(br_[b].slot); br_[b].pending = false; }
  for (auto& c : chordRt_) releaseSlot(c.slot);
  precisionCount_ = 0;
  accX_ = accY_ = scrollAccV_ = scrollAccH_ = 0;
}

void Core::step(const PadState& pad, double now, double dt) {
  dt = std::max(0.0, std::min(dt, 0.1));

  if (!pad.connected) {
    if (wasConnected_) {
      releaseAll();
      for (int b = 0; b < B_COUNT; ++b) br_[b] = BtnRt();
      chordRt_.assign(prof_.chords.size(), ChordRt());
    }
    wasConnected_ = false;
    return;
  }
  wasConnected_ = true;

  if (suspended_) {
    releaseAll();
    for (int b = 0; b < B_COUNT; ++b) {
      const bool d = (pad.bits >> b) & 1u;
      br_[b].down = d; br_[b].consumed = d; br_[b].pending = false;
    }
    for (size_t i = 0; i < prof_.chords.size(); ++i)
      chordRt_[i].on = prof_.chords[i].mask && (pad.bits & prof_.chords[i].mask) == prof_.chords[i].mask;
    return;
  }

  // ---- chords: fire when every member is down, release when any lets go.
  for (size_t i = 0; i < prof_.chords.size(); ++i) {
    const ChordDef& c = prof_.chords[i];
    ChordRt& r = chordRt_[i];
    if (popcount32(c.mask) < 2 || c.action.type == ActType::None) continue;

    const bool all = (pad.bits & c.mask) == c.mask;
    if (all && !r.on) {
      r.on = true;
      for (int b = 0; b < B_COUNT; ++b) {
        if (!(c.mask & (1u << b))) continue;
        releaseSlot(br_[b].slot);          // cancel a single-button action that already started
        br_[b].pending = false;
        br_[b].consumed = true;
      }
      if (allowed(c.action)) { r.slot.act = c.action; pressSlot(r.slot, now); }
    } else if (!all && r.on) {
      r.on = false;
      releaseSlot(r.slot);
    }
    repeatSlot(r.slot, now);
  }

  // ---- single buttons
  const bool layerDown = prof_.layerBtn >= 0 && ((pad.bits >> prof_.layerBtn) & 1u);
  const int layer = layerDown ? 1 : 0;

  for (int b = 0; b < B_COUNT; ++b) {
    BtnRt& s = br_[b];
    const bool d = (pad.bits >> b) & 1u;

    if (d && !s.down) {                                   // pressed
      s.down = true; s.downT = now; s.pending = false;
      if (b == prof_.layerBtn) { s.slot.act = Action(); continue; }   // pure modifier
      s.slot.act = prof_.bind[layer][b];
      if (s.slot.act.type != ActType::None && !s.consumed) {
        if (((memberMask_ >> b) & 1u) && prof_.chordWindowMs > 0) s.pending = true;   // wait for a possible chord
        else if (allowed(s.slot.act)) pressSlot(s.slot, now);
      }
    } else if (d && s.down) {                             // held
      if (s.pending && !s.consumed && (now - s.downT) * 1000.0 >= prof_.chordWindowMs) {
        s.pending = false;
        if (allowed(s.slot.act)) pressSlot(s.slot, now);
      }
      repeatSlot(s.slot, now);
    } else if (!d && s.down) {                            // released
      s.down = false;
      if (s.pending && !s.consumed && allowed(s.slot.act)) {   // quick tap on a chord member
        pressSlot(s.slot, now);
      }
      releaseSlot(s.slot);
      s.pending = false;
      s.consumed = false;
    }
  }

  if (enabled_) {
    moveCursor(pad, dt);
    scroll(pad, dt);
  }
}

void Core::moveCursor(const PadState& pad, double dt) {
  const float tx = (float)(prof_.swapSticks ? pad.rx : pad.lx);
  const float ty = (float)(prof_.swapSticks ? pad.ry : pad.ly);
  const float len = std::sqrt(tx * tx + ty * ty);
  const float dz = (float)prof_.deadZone;
  if (len <= dz || len <= 0.0f) { accX_ = accY_ = 0; return; }

  const float norm = std::min(1.0f, (len - dz) / (kStickMax - dz));
  const float mult = prof_.curve > 0.0001f ? std::pow(norm, prof_.curve) : norm;
  const float spd = currentSpeed() * (precisionCount_ > 0 ? prof_.precisionFactor : 1.0f);

  const float dx = tx * spd * mult * (float)dt;
  const float dy = ty * spd * mult * (float)dt * (prof_.invertMouseY ? 1.0f : -1.0f);  // stick up => screen up

  accX_ += dx;
  accY_ += dy;
  const int ix = (int)accX_;                 // truncate toward zero, keep the remainder
  const int iy = (int)accY_;
  if (ix || iy) {
    out_->moveCursor(ix, iy, prof_.relativeMouse);
    accX_ -= (float)ix;
    accY_ -= (float)iy;
  }
}

void Core::scroll(const PadState& pad, double dt) {
  float tx = (float)(prof_.swapSticks ? pad.lx : pad.rx);
  float ty = (float)(prof_.swapSticks ? pad.ly : pad.ry);

  // Axis snapping: if the stick is clearly pushed along one axis, ignore the other.
  const float ax = std::fabs(tx), ay = std::fabs(ty);
  if (ay > ax * 2.0f) tx = 0; else if (ax > ay * 2.0f) ty = 0;

  const float dz = (float)prof_.scrollDeadZone;
  auto axis = [&](float t, float& acc, bool invert) -> int {
    const float a = std::fabs(t);
    if (a <= dz) { acc = 0; return 0; }
    const float norm = std::min(1.0f, (a - dz) / (kStickMax - dz));
    float v = a * norm * prof_.scrollSpeed * (float)dt;     // wheel units (120 = one notch)
    if ((t < 0) != invert) v = -v;
    acc += v;
    const int iv = (int)acc;
    acc -= (float)iv;
    return iv;
  };

  const int v = axis(ty, scrollAccV_, prof_.scrollInvertV);
  if (v) out_->wheel(v, false);
  if (prof_.scrollHorizontal) {
    const int h = axis(tx, scrollAccH_, prof_.scrollInvertH);
    if (h) out_->wheel(h, true);
  } else {
    scrollAccH_ = 0;
  }
}
