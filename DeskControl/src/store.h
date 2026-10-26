// DeskControl - persistence of profiles and application settings (plain text files).
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "model.h"

namespace Store {

void init(const std::filesystem::path& dataDir);   // creates <dir>/profiles
const std::filesystem::path& dir();

bool loadApp(AppSettings& out);                    // false => file missing (defaults returned)
bool saveApp(const AppSettings& a);

std::vector<std::string> listProfiles();           // sorted, case-insensitive
bool profileExists(const std::string& name);
bool loadProfile(const std::string& name, Profile& out);
bool saveProfile(const Profile& p);
bool deleteProfile(const std::string& name);
bool renameProfile(const std::string& from, const std::string& to);

std::string sanitizeName(const std::string& s);    // safe file-name stem (may return "")
std::string uniqueName(const std::string& base);   // "Name", "Name 2", ...

bool readFile(const std::filesystem::path& p, std::string& out);
bool writeFileAtomic(const std::filesystem::path& p, const std::string& text);

}  // namespace Store
