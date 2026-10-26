#include "store.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace Store {

static fs::path g_dir;

static fs::path profilesDir() { return g_dir / "profiles"; }

static fs::path profilePath(const std::string& name) {
  return profilesDir() / fs::u8path(sanitizeName(name) + ".ini");
}

void init(const fs::path& dataDir) {
  g_dir = dataDir;
  std::error_code ec;
  fs::create_directories(profilesDir(), ec);
}

const fs::path& dir() { return g_dir; }

bool readFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  // strip UTF-8 BOM if some editor added one
  if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
    out.erase(0, 3);
  return true;
}

// Write to a temp file and rename, so a crash never leaves a half-written profile.
bool writeFileAtomic(const fs::path& p, const std::string& text) {
  fs::path tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    if (!f) return false;
  }
  std::error_code ec;
  fs::rename(tmp, p, ec);
  if (ec) {                       // rename over an existing file can fail on some systems
    fs::remove(p, ec);
    ec.clear();
    fs::rename(tmp, p, ec);
  }
  return !ec;
}

bool loadApp(AppSettings& out) {
  std::string text;
  if (!readFile(g_dir / "settings.ini", text)) { out = AppSettings(); return false; }
  return appFromText(text, out);
}

bool saveApp(const AppSettings& a) { return writeFileAtomic(g_dir / "settings.ini", appToText(a)); }

std::string sanitizeName(const std::string& s) {
  std::string out;
  for (char c : s) {
    if ((unsigned char)c < 32 || std::string("\\/:*?\"<>|#").find(c) != std::string::npos) continue;
    out.push_back(c);
  }
  out = trim(out);
  while (!out.empty() && out.back() == '.') out.pop_back();
  if (out.size() > 48) out.resize(48);
  return trim(out);
}

std::vector<std::string> listProfiles() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(profilesDir(), ec)) {
    if (!e.is_regular_file()) continue;
    if (toLower(e.path().extension().string()) != ".ini") continue;
    names.push_back(e.path().stem().u8string().c_str());
  }
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
  return names;
}

bool profileExists(const std::string& name) {
  std::error_code ec;
  return fs::exists(profilePath(name), ec);
}

bool loadProfile(const std::string& name, Profile& out) {
  std::string text;
  if (!readFile(profilePath(name), text)) return false;
  bool ok = profileFromText(text, out);
  out.name = sanitizeName(name).empty() ? out.name : sanitizeName(name);   // file name is the truth
  return ok;
}

bool saveProfile(const Profile& p) {
  if (sanitizeName(p.name).empty()) return false;
  Profile copy = p;
  copy.name = sanitizeName(p.name);
  return writeFileAtomic(profilePath(copy.name), profileToText(copy));
}

bool deleteProfile(const std::string& name) {
  std::error_code ec;
  return fs::remove(profilePath(name), ec);
}

bool renameProfile(const std::string& from, const std::string& to) {
  if (sanitizeName(to).empty()) return false;
  Profile p;
  if (!loadProfile(from, p)) return false;
  p.name = sanitizeName(to);
  if (!saveProfile(p)) return false;
  if (!iequals(sanitizeName(from), sanitizeName(to))) deleteProfile(from);
  return true;
}

std::string uniqueName(const std::string& base) {
  std::string b = sanitizeName(base);
  if (b.empty()) b = "Profile";
  if (!profileExists(b)) return b;
  for (int i = 2; i < 1000; ++i) {
    std::string cand = b + " " + std::to_string(i);
    if (!profileExists(cand)) return cand;
  }
  return b + " copy";
}

}  // namespace Store
