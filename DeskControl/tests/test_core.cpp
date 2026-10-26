// Native unit tests for the platform independent parts (model, core, store).
// Build: cmake -DDESKCONTROL_BUILD_TESTS=ON .. && ctest   (Linux/macOS/Windows)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/core.h"
#include "../src/kbdlayout.h"
#include "../src/model.h"
#include "../src/store.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!(cond)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define SECTION(name) std::printf("[%s]\n", name)

struct Ev { enum K { Key, Mouse, Wheel, Move, Rumble, Event, Log } k; int a = 0, b = 0; bool down = false; bool flag = false; std::string s; float f = 0; };

struct Rec : IOutput {
  std::vector<Ev> ev;
  void key(uint16_t vk, bool down) override { Ev e; e.k = Ev::Key; e.a = vk; e.down = down; ev.push_back(e); }
  void mouseButton(int m, bool down) override { Ev e; e.k = Ev::Mouse; e.a = m; e.down = down; ev.push_back(e); }
  void wheel(int amt, bool h) override { Ev e; e.k = Ev::Wheel; e.a = amt; e.flag = h; ev.push_back(e); }
  void moveCursor(int dx, int dy, bool rel) override { Ev e; e.k = Ev::Move; e.a = dx; e.b = dy; e.flag = rel; ev.push_back(e); }
  void rumble(int ms, float s) override { Ev e; e.k = Ev::Rumble; e.a = ms; e.f = s; ev.push_back(e); }
  void event(int c) override { Ev e; e.k = Ev::Event; e.a = c; ev.push_back(e); }
  void log(const std::string& s) override { Ev e; e.k = Ev::Log; e.s = s; ev.push_back(e); }

  int count(Ev::K k, int a = -1, int down = -1) const {
    int n = 0;
    for (const auto& e : ev) if (e.k == k && (a < 0 || e.a == a) && (down < 0 || e.down == (down != 0))) ++n;
    return n;
  }
  void clear() { ev.clear(); }
};

static uint32_t bit(int b) { return 1u << b; }

// Runs the core for `seconds` at 150 Hz with the given pad state.
struct Sim {
  Rec rec;
  Core core;
  double now = 100.0;
  PadState pad;
  Sim() : core(&rec) { pad.connected = true; }
  void run(double seconds, uint32_t bits, int lx = 0, int ly = 0, int rx = 0, int ry = 0) {
    pad.bits = bits; pad.lx = (int16_t)lx; pad.ly = (int16_t)ly; pad.rx = (int16_t)rx; pad.ry = (int16_t)ry;
    const double dt = 1.0 / 150.0;
    for (double t = 0; t < seconds - 1e-9; t += dt) { now += dt; core.step(pad, now, dt); }
  }
};

static void testBasicButtons() {
  SECTION("basic buttons");
  Sim s;
  s.run(0.05, bit(B_A));
  CHECK(s.rec.count(Ev::Mouse, M_LEFT, 1) == 1);          // A -> left mouse down immediately
  s.run(0.05, 0);
  CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 1);          // released
  s.rec.clear();
  s.run(0.05, bit(B_B));
  CHECK(s.rec.count(Ev::Key, 0x0D, 1) == 1);              // B -> Enter
  s.run(0.05, 0);
  CHECK(s.rec.count(Ev::Key, 0x0D, 0) == 1);
}

static void testChordsAndTaps() {
  SECTION("chords");
  {   // Start+Back (pressed 30ms apart) disables DeskControl and must NOT type the Windows key.
    Sim s;
    s.run(0.03, bit(B_START));
    s.run(0.30, bit(B_START) | bit(B_BACK));
    CHECK(!s.core.enabled());
    CHECK(s.rec.count(Ev::Key, 0x5B) == 0);
    CHECK(s.rec.count(Ev::Key, 0xA8) == 0);               // and no browser-refresh either
    s.run(0.1, 0);
    CHECK(s.rec.count(Ev::Key, 0x5B) == 0);
    // while disabled, buttons do nothing...
    s.rec.clear();
    s.run(0.2, bit(B_A));
    s.run(0.1, 0);
    CHECK(s.rec.count(Ev::Mouse) == 0);
    // ...but the chord re-enables.
    s.run(0.03, bit(B_START));
    s.run(0.2, bit(B_START) | bit(B_BACK));
    CHECK(s.core.enabled());
    s.run(0.1, 0);
    s.rec.clear();
    s.run(0.05, bit(B_A));
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 1) == 1);
  }
  {   // Quick tap on a chord member still fires its own action (press+release).
    Sim s;
    s.run(0.05, bit(B_LB));                               // 50 ms < 120 ms window
    s.run(0.05, 0);
    CHECK(s.rec.count(Ev::Key, 0xA6, 1) == 1);
    CHECK(s.rec.count(Ev::Key, 0xA6, 0) == 1);
  }
  {   // Holding a chord member longer than the window acts as a normal hold.
    Sim s;
    s.run(0.30, bit(B_START));
    CHECK(s.rec.count(Ev::Key, 0x5B, 1) == 1);
    CHECK(s.rec.count(Ev::Key, 0x5B, 0) == 0);
    s.run(0.05, 0);
    CHECK(s.rec.count(Ev::Key, 0x5B, 0) == 1);
  }
  {   // LB+RB cycles speed; repeated presses wrap around.
    Sim s;
    CHECK(s.core.speedIndex() == 2);
    s.run(0.2, bit(B_LB) | bit(B_RB));
    CHECK(s.core.speedIndex() == 3);
    s.run(0.1, 0);
    s.run(0.2, bit(B_LB) | bit(B_RB));
    CHECK(s.core.speedIndex() == 0);                      // wrapped
    CHECK(s.rec.count(Ev::Key, 0xA6) == 0 && s.rec.count(Ev::Key, 0xA7) == 0);
  }
  {   // Back+LB toggles vibration, without stray Back / LB actions.
    Sim s;
    CHECK(s.core.vibration());
    s.run(0.03, bit(B_BACK));
    s.run(0.2, bit(B_BACK) | bit(B_LB));
    CHECK(!s.core.vibration());
    CHECK(s.rec.count(Ev::Key, 0xA8) == 0 && s.rec.count(Ev::Key, 0xA6) == 0);
  }
  {   // D-pad is NOT delayed by the chord grace period (it is in no default chord).
    Sim s;
    s.run(0.01, bit(B_UP));
    CHECK(s.rec.count(Ev::Key, 0x26, 1) == 1);
  }
}

static void testLayer() {
  SECTION("shift layer");
  Sim s;
  Profile p = Profile::makeDefault();
  p.layerBtn = B_LB;
  p.chords.clear();
  p.bind[1][B_A] = Action::key(0x41, GM_CTRL);            // Ctrl+A on shift layer
  s.core.setProfile(p, true);

  s.run(0.05, bit(B_LB));                                  // holding the layer button does nothing itself
  CHECK(s.rec.count(Ev::Key, 0xA6) == 0);
  s.run(0.05, bit(B_LB) | bit(B_A));
  CHECK(s.rec.count(Ev::Key, 0xA2, 1) == 1);               // Ctrl down
  CHECK(s.rec.count(Ev::Key, 0x41, 1) == 1);               // A down
  CHECK(s.rec.count(Ev::Mouse) == 0);                      // layer 0 action (left click) not used
  s.run(0.05, bit(B_LB));
  CHECK(s.rec.count(Ev::Key, 0x41, 0) == 1);
  CHECK(s.rec.count(Ev::Key, 0xA2, 0) == 1);
  s.run(0.05, 0);
  s.rec.clear();
  s.run(0.05, bit(B_A));                                   // without the layer: normal left click
  CHECK(s.rec.count(Ev::Mouse, M_LEFT, 1) == 1);
}

static void testRepeat() {
  SECTION("key repeat");
  Sim s;
  s.run(1.0, bit(B_UP));
  // initial down + repeats after 400ms at 25Hz for the remaining 0.6s ~ 15
  int downs = s.rec.count(Ev::Key, 0x26, 1);
  CHECK(downs >= 12 && downs <= 18);
  CHECK(s.rec.count(Ev::Key, 0x26, 0) == 0);               // no key-ups while held
  s.run(0.05, 0);
  CHECK(s.rec.count(Ev::Key, 0x26, 0) == 1);
  // A non-repeating key must not repeat.
  s.rec.clear();
  s.run(1.0, bit(B_B));
  CHECK(s.rec.count(Ev::Key, 0x0D, 1) == 1);
}

static void testCursor() {
  SECTION("cursor");
  {   // inside the dead zone: nothing
    Sim s;
    s.run(0.5, 0, 3000, 3000);
    CHECK(s.rec.count(Ev::Move) == 0);
  }
  {   // full right at Medium (0.025): 32767*0.025*1 = ~819 px/s
    Sim s;
    s.run(1.0, 0, 32767, 0);
    long dx = 0, dy = 0;
    for (auto& e : s.rec.ev) if (e.k == Ev::Move) { dx += e.a; dy += e.b; }
    CHECK(dx > 780 && dx < 840);
    CHECK(dy == 0);
  }
  {   // stick up moves cursor up (negative y); invertMouseY flips it
    Sim s;
    s.run(0.5, 0, 0, 32767);
    long dy = 0; for (auto& e : s.rec.ev) if (e.k == Ev::Move) dy += e.b;
    CHECK(dy < -300);
    Sim t; Profile p = Profile::makeDefault(); p.invertMouseY = true; t.core.setProfile(p, true);
    t.run(0.5, 0, 0, 32767);
    long dy2 = 0; for (auto& e : t.rec.ev) if (e.k == Ev::Move) dy2 += e.b;
    CHECK(dy2 > 300);
  }
  {   // precision mode slows the cursor
    Sim fast, slow;
    Profile p = Profile::makeDefault();
    p.precisionFactor = 0.25f;
    p.bind[0][B_X] = Action::sp(S_PRECISION);
    slow.core.setProfile(p, true);
    fast.core.setProfile(p, true);
    fast.run(1.0, 0, 32767, 0);
    slow.run(1.0, bit(B_X), 32767, 0);
    long f = 0, sl = 0;
    for (auto& e : fast.rec.ev) if (e.k == Ev::Move) f += e.a;
    for (auto& e : slow.rec.ev) if (e.k == Ev::Move) sl += e.a;
    CHECK(sl > 0 && std::fabs((double)sl / (double)f - 0.25) < 0.04);
    CHECK(slow.core.precision());
    slow.run(0.05, 0, 0, 0);
    CHECK(!slow.core.precision());
  }
  {   // speed presets change the distance, swap sticks moves control to the right stick
    Sim s;
    Profile p = Profile::makeDefault(); p.swapSticks = true; s.core.setProfile(p, true);
    s.run(0.5, 0, 32767, 0, 0, 0);
    CHECK(s.rec.count(Ev::Move) == 0);
    s.run(0.5, 0, 0, 0, 32767, 0);
    CHECK(s.rec.count(Ev::Move) > 0);
  }
  {   // curve makes half-tilt slower than linear
    Sim lin, cur;
    Profile p = Profile::makeDefault(); p.curve = 3.0f; cur.core.setProfile(p, true);
    lin.run(1.0, 0, 20000, 0); cur.run(1.0, 0, 20000, 0);
    long a = 0, b = 0;
    for (auto& e : lin.rec.ev) if (e.k == Ev::Move) a += e.a;
    for (auto& e : cur.rec.ev) if (e.k == Ev::Move) b += e.a;
    CHECK(b > 0 && b < a / 2);
  }
  {   // relative flag is forwarded
    Sim s; Profile p = Profile::makeDefault(); p.relativeMouse = true; s.core.setProfile(p, true);
    s.run(0.2, 0, 32767, 0);
    bool allRel = true; int n = 0;
    for (auto& e : s.rec.ev) if (e.k == Ev::Move) { ++n; allRel &= e.flag; }
    CHECK(n > 0 && allRel);
  }
  {   // disabled => no movement
    Sim s; s.core.setEnabled(false); s.rec.clear();
    s.run(0.5, 0, 32767, 0);
    CHECK(s.rec.count(Ev::Move) == 0);
  }
}

static void testScroll() {
  SECTION("scroll");
  {   // stick up => positive vertical wheel
    Sim s;
    s.run(0.5, 0, 0, 0, 0, 32767);
    long v = 0; bool anyH = false;
    for (auto& e : s.rec.ev) if (e.k == Ev::Wheel) { if (e.flag) anyH = true; else v += e.a; }
    CHECK(v > 1000);
    CHECK(!anyH);
  }
  {   // slightly off-axis (old code reversed the weak axis): horizontal must stay silent
    Sim s;
    s.run(0.5, 0, 0, 0, 1500, 30000);
    bool anyH = false; long v = 0;
    for (auto& e : s.rec.ev) if (e.k == Ev::Wheel) { if (e.flag) anyH = true; else v += e.a; }
    CHECK(!anyH && v > 0);
  }
  {   // stick down => negative; invert flips it
    Sim s;
    s.run(0.3, 0, 0, 0, 0, -32767);
    long v = 0; for (auto& e : s.rec.ev) if (e.k == Ev::Wheel && !e.flag) v += e.a;
    CHECK(v < -500);
    Sim t; Profile p = Profile::makeDefault(); p.scrollInvertV = true; t.core.setProfile(p, true);
    t.run(0.3, 0, 0, 0, 0, -32767);
    long w = 0; for (auto& e : t.rec.ev) if (e.k == Ev::Wheel && !e.flag) w += e.a;
    CHECK(w > 500);
  }
  {   // horizontal scroll can be disabled
    Sim s; Profile p = Profile::makeDefault(); p.scrollHorizontal = false; s.core.setProfile(p, true);
    s.run(0.3, 0, 0, 0, 32767, 0);
    CHECK(s.rec.count(Ev::Wheel) == 0);
    Sim t; t.run(0.3, 0, 0, 0, 32767, 0);
    CHECK(t.rec.count(Ev::Wheel) > 0);
  }
}

static void testStateHandling() {
  SECTION("state handling");
  {   // disabling releases everything held (no stuck mouse button)
    Sim s;
    s.run(0.1, bit(B_A));
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 1) == 1);
    s.core.setEnabled(false);
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 1);
  }
  {   // controller disconnect releases everything
    Sim s;
    s.run(0.1, bit(B_A) | bit(B_UP));
    s.pad.connected = false;
    s.core.step(s.pad, s.now + 0.01, 0.01);
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 1);
    CHECK(s.rec.count(Ev::Key, 0x26, 0) == 1);
  }
  {   // editing a non-binding setting must not release held buttons
    Sim s;
    s.run(0.1, bit(B_A));
    Profile p = s.core.profile(); p.deadZone = 7000; p.scrollSpeed = 0.2f;
    s.core.setProfile(p, false);
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 0);
    s.run(0.1, bit(B_A));
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 0);
    // editing a binding releases and does not re-fire until re-pressed
    p.bind[0][B_A] = Action::key(0x41);
    s.core.setProfile(p, false);
    CHECK(s.rec.count(Ev::Mouse, M_LEFT, 0) == 1);
    s.run(0.1, bit(B_A));
    CHECK(s.rec.count(Ev::Key, 0x41, 1) == 0);
    s.run(0.05, 0);
    s.run(0.05, bit(B_A));
    CHECK(s.rec.count(Ev::Key, 0x41, 1) == 1);
  }
  {   // suspended => absolutely no output, even for a held button
    Sim s;
    s.core.setSuspended(true);
    s.run(0.3, bit(B_A) | bit(B_UP), 32767, 0, 0, 32767);
    CHECK(s.rec.ev.empty());
    s.core.setSuspended(false);
    s.run(0.1, bit(B_A));                                  // still held from before: no fire
    CHECK(s.rec.count(Ev::Mouse) == 0);
  }
  {   // modifiers wrap the key press in the right order
    Sim s;
    Profile p = Profile::makeDefault(); p.bind[0][B_Y] = Action::key(0x09, GM_ALT);
    s.core.setProfile(p, true);
    s.run(0.05, bit(B_Y)); s.run(0.05, 0);
    int ia = -1, it = -1, ta = -1, tt = -1, i = 0;
    for (auto& e : s.rec.ev) {
      if (e.k == Ev::Key && e.a == 0xA4 && e.down) ia = i;
      if (e.k == Ev::Key && e.a == 0x09 && e.down) it = i;
      if (e.k == Ev::Key && e.a == 0x09 && !e.down) tt = i;
      if (e.k == Ev::Key && e.a == 0xA4 && !e.down) ta = i;
      ++i;
    }
    CHECK(ia >= 0 && ia < it && it < tt && tt < ta);
  }
  {   // trigger-as-button + special events
    Sim s;
    s.run(0.05, bit(B_LT)); s.run(0.05, 0);
    CHECK(s.rec.count(Ev::Key, 0x20, 1) == 1 && s.rec.count(Ev::Key, 0x20, 0) == 1);
    s.rec.clear();
    s.run(0.05, bit(B_Y));
    CHECK(s.rec.count(Ev::Event, EV_TOGGLE_WINDOW) == 1);
  }
}

static void testModel() {
  SECTION("model / serialisation");
  Profile p = Profile::makeDefault();
  p.name = "Test profile";
  p.layerBtn = B_RB;
  p.bind[1][B_A] = Action::key(0x43, GM_CTRL | GM_SHIFT, true);
  p.deadZone = 7123; p.curve = 1.75f; p.scrollSpeed = 0.2f; p.swapSticks = true; p.relativeMouse = true;
  p.speeds = { {"Slow", 0.01f}, {"Fast", 0.1f} }; p.defaultSpeed = 1;
  p.chords.push_back({bit(B_LB) | bit(B_X), Action::key(0x73, GM_ALT)});

  std::string text = profileToText(p);
  Profile q; std::vector<std::string> warn;
  CHECK(profileFromText(text, q, &warn));
  CHECK(warn.empty());
  CHECK(bindingsEqual(p, q));
  CHECK(q.deadZone == 7123 && std::fabs(q.curve - 1.75f) < 1e-4f && std::fabs(q.scrollSpeed - 0.2f) < 1e-4f);
  CHECK(q.swapSticks && q.relativeMouse && q.speeds.size() == 2 && q.speeds[1].name == "Fast" && q.defaultSpeed == 1);
  CHECK(q.layerBtn == B_RB);

  // Garbage in => no crash, warnings out, sane values.
  Profile g; std::vector<std::string> gw;
  profileFromText("dead_zone = banana\nbind.A = key:zzz\nthis is not a setting\nchord = FOO+BAR | none\nscroll_speed = -9\n", g, &gw);
  CHECK(gw.size() >= 3);
  CHECK(g.scrollSpeed > 0 && g.deadZone > 0 && !g.speeds.empty());

  // actions
  Action a; CHECK(actionFromText("key:0x41+ctrl+alt,repeat", a));
  CHECK(a.type == ActType::Key && a.vk == 0x41 && a.mods == (GM_CTRL | GM_ALT) && a.repeat);
  CHECK(!actionFromText("key:300", a));
  CHECK(actionToString(Action::key(0x41, GM_CTRL | GM_SHIFT)) == "Ctrl+Shift+A");

  // app settings
  AppSettings app; app.activeProfile = "Couch"; app.controller = 2; app.pollHz = 240; app.closeToTray = false; app.autoKeyboard = false; app.autoHideKeyboard = false;
  app.rules.push_back({"steam.exe", "Couch"}); app.rules.push_back({"game.exe", ""});
  AppSettings app2; appFromText(appToText(app), app2);
  CHECK(app2.activeProfile == "Couch" && app2.controller == 2 && app2.pollHz == 240 && !app2.closeToTray && !app2.autoKeyboard && !app2.autoHideKeyboard);
  AppSettings fresh; appFromText("", fresh);
  CHECK(fresh.autoKeyboard && fresh.autoHideKeyboard);   // on by default
  CHECK(app2.rules.size() == 2 && app2.rules[1].profile.empty() && app2.rules[0].exe == "steam.exe");
}

static void testLegacyImport(const std::string& cfgPath) {
  SECTION("legacy config.ini import");
  std::string text;
  CHECK(Store::readFile(cfgPath, text));
  Profile p; std::vector<std::string> w;
  CHECK(importLegacyText(text, p, &w));
  CHECK(p.bind[0][B_A].type == ActType::Mouse && p.bind[0][B_A].mouse == M_LEFT);        // 0x1000
  CHECK(p.bind[0][B_X].type == ActType::Mouse && p.bind[0][B_X].mouse == M_RIGHT);       // 0x4000
  CHECK(p.bind[0][B_LTHUMB].type == ActType::Mouse && p.bind[0][B_LTHUMB].mouse == M_MIDDLE); // 0x0040
  CHECK(p.bind[0][B_Y].type == ActType::Special && p.bind[0][B_Y].special == S_TOGGLE_WINDOW);   // 0x8000 hide
  CHECK(p.bind[0][B_B].type == ActType::Key && p.bind[0][B_B].vk == 0x0D);
  CHECK(p.bind[0][B_UP].vk == 0x26 && p.bind[0][B_RTHUMB].vk == 0x71);
  CHECK(p.bind[0][B_LT].vk == 0x20 && p.bind[0][B_RT].vk == 0x08);
  CHECK(p.bind[0][B_LB].vk == 0xA6 && p.bind[0][B_RB].vk == 0xA7);
  bool disable = false, vib = false, speed = false;
  for (auto& c : p.chords) {
    if (c.mask == (bit(B_START) | bit(B_BACK)) && c.action.special == S_TOGGLE_ENABLED) disable = true;
    if (c.mask == (bit(B_START) | bit(B_UP)) && c.action.special == S_TOGGLE_VIBRATION) vib = true;
    if (c.mask == (bit(B_LB) | bit(B_RB)) && c.action.special == S_CYCLE_SPEED) speed = true;
  }
  CHECK(disable && vib && speed);
  CHECK(p.speeds.size() == 4 && std::fabs(p.speeds[3].value - 0.04f) < 1e-5f && p.defaultSpeed == 2);  // no CURSOR_SPEED => defaults
  CHECK(!importLegacyText("hello world", p, nullptr));

  // Custom speeds, named and unnamed, with the legacy rule "first preset is the default".
  Profile q;
  CHECK(importLegacyText("CURSOR_SPEED = SLOW=0.01,0.05,TURBO=0.2,BAD=5.0\nDEAD_ZONE = 8000 # c\nSWAP_THUMBSTICKS = 1\nACCELERATION_FACTOR = 2.5\nGAMEPAD_A = 0x41\nCONFIG_MOUSE_LEFT = 0x1000\n", q, &w));
  CHECK(q.speeds.size() == 3 && q.speeds[0].name == "SLOW" && q.speeds[1].name == "1" && q.speeds[2].name == "TURBO");  // 5.0 rejected
  CHECK(q.defaultSpeed == 0 && q.deadZone == 8000 && q.swapSticks && std::fabs(q.curve - 2.5f) < 1e-5f);
  CHECK(q.bind[0][B_A].type == ActType::Mouse);                       // CONFIG_* wins over the keyboard binding
}

static void testStore() {
  SECTION("store");
  namespace fs = std::filesystem;
  fs::path tmp = fs::temp_directory_path() / "deskcontrol_test_store";
  std::error_code ec; fs::remove_all(tmp, ec);
  Store::init(tmp);
  CHECK(Store::listProfiles().empty());
  Profile p = Profile::makeDefault(); p.name = "Couch: mode?";
  CHECK(Store::saveProfile(p));
  CHECK(Store::listProfiles().size() == 1 && Store::listProfiles()[0] == "Couch mode");
  Profile r; CHECK(Store::loadProfile("Couch mode", r) && bindingsEqual(p, r) && r.name == "Couch mode");
  CHECK(Store::uniqueName("Couch mode") == "Couch mode 2");
  CHECK(Store::renameProfile("Couch mode", "Desk"));
  CHECK(Store::listProfiles().size() == 1 && Store::listProfiles()[0] == "Desk");
  CHECK(Store::deleteProfile("Desk") && Store::listProfiles().empty());
  CHECK(!Store::loadProfile("nope", r));
  AppSettings a; a.activeProfile = "X"; CHECK(Store::saveApp(a));
  AppSettings b; CHECK(Store::loadApp(b) && b.activeProfile == "X");
  CHECK(Store::sanitizeName("..") == "" && Store::sanitizeName("  a/b\\c  ") == "abc");
  fs::remove_all(tmp, ec);
}


static int keyIndex(uint16_t vk) {
  const auto& k = kbKeys();
  for (int i = 0; i < (int)k.size(); ++i) if (k[i].vk == vk) return i;
  return -1;
}

static void testKeyboardLayout() {
  SECTION("keyboard layout");
  const auto& keys = kbKeys();
  CHECK(keys.size() > 80);

  // Every key exists once, fits inside the layout and does not overlap another key.
  bool unique = true, inside = true, overlap = false;
  for (size_t i = 0; i < keys.size(); ++i) {
    const KbKey& a = keys[i];
    if (a.x < 0 || a.y < 0 || a.x + a.w > kbWidth() + 0.001f || a.y + a.h > kbHeight() + 0.001f) inside = false;
    if (!a.label && (!a.lo || !a.hi)) inside = false;
    for (size_t j = i + 1; j < keys.size(); ++j) {
      const KbKey& b = keys[j];
      if (a.vk == b.vk) unique = false;
      if (a.x < b.x + b.w - 0.001f && b.x < a.x + a.w - 0.001f && a.y < b.y + b.h - 0.001f && b.y < a.y + a.h - 0.001f) overlap = true;
    }
  }
  CHECK(unique); CHECK(inside); CHECK(!overlap);

  // The main block rows are exactly 15 units wide, like a real keyboard.
  const float rowY[5] = {1.3f, 2.3f, 3.3f, 4.3f, 5.3f};
  for (float y : rowY) {
    float lo = 100, hi = 0;
    for (const auto& k : keys) if (std::fabs(k.y - y) < 0.01f && k.x < 15.0f) { lo = std::min(lo, k.x); hi = std::max(hi, k.x + k.w); }
    CHECK(std::fabs(lo) < 0.001f && std::fabs(hi - 15.0f) < 0.001f);
  }

  // Navigation: neighbours, rows, wrap-around.
  const int q = keyIndex('Q'), w = keyIndex('W'), a = keyIndex('A'), z = keyIndex('Z');
  CHECK(kbNavigate(q, 1, 0) == w);
  CHECK(kbNavigate(w, -1, 0) == q);
  CHECK(kbNavigate(q, 0, 1) == a || kbNavigate(q, 0, 1) == keyIndex('S'));
  CHECK(kbNavigate(a, 0, 1) == z || kbNavigate(a, 0, 1) == keyIndex('X'));
  CHECK(kbNavigate(keyIndex(0x1B), -1, 0) == keyIndex(0x7B));                 // Esc wraps to F12
  CHECK(kbNavigate(keyIndex(0x1B), 0, -1) == keyIndex(0x1B));                 // nowhere to go
  CHECK(kbNavigate(keyIndex(0x20), 0, -1) >= 0 && keys[kbNavigate(keyIndex(0x20), 0, -1)].y < keys[keyIndex(0x20)].y);
  CHECK(kbNavigate(keyIndex(0x0D), 1, 0) == keyIndex(0x14));                  // Enter wraps to Caps (no nav keys on that row)
  CHECK(kbNavigate(keyIndex(0x08), 1, 0) == keyIndex(0x2D));                  // Backspace -> Insert
  CHECK(kbNavigate(-1, 1, 0) == -1 && kbNavigate(9999, 1, 0) == -1);

  // Every key can be reached from Esc by D-pad steps (nothing is unreachable).
  std::vector<char> seen(keys.size(), 0);
  std::vector<int> todo{keyIndex(0x1B)};
  seen[todo[0]] = 1;
  while (!todo.empty()) {
    int cur = todo.back(); todo.pop_back();
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& d : dirs) {
      int n = kbNavigate(cur, d[0], d[1]);
      if (n >= 0 && !seen[n]) { seen[n] = 1; todo.push_back(n); }
    }
  }
  int reached = 0; for (char c : seen) reached += c;
  CHECK(reached == (int)keys.size());

  CHECK(kbNearest(keys[q].x + 0.5f, keys[q].y + 0.5f) == q);
  CHECK(kbNearest(-3.0f, -3.0f) == keyIndex(0x1B));
  CHECK(kbIsModifier(0xA0) && kbIsModifier(0xA5) && !kbIsModifier('A') && !kbIsModifier(0x5B));
  CHECK(kbIsWinKey(0x5B) && !kbIsWinKey(0x14));

  AppSettings s; s.builtinKeyboard = false; s.keyboardDpad = false;
  AppSettings r; CHECK(appFromText(appToText(s), r) && !r.builtinKeyboard && !r.keyboardDpad);
  AppSettings d; CHECK(appFromText("", d) && d.builtinKeyboard && d.keyboardDpad);   // on by default
}

int main(int argc, char** argv) {
  std::string cfg = argc > 1 ? argv[1] : "../Configs/config_default.ini";
  testBasicButtons();
  testChordsAndTaps();
  testLayer();
  testRepeat();
  testCursor();
  testScroll();
  testStateHandling();
  testModel();
  testLegacyImport(cfg);
  testStore();
  testKeyboardLayout();
  std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
