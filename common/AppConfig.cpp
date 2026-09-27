#include "AppConfig.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <tinyxml2.h>
#include <zlib.h>

namespace fs = std::filesystem;

// ============================================================
// Helpers
// ============================================================

static std::string readFileBytes(const fs::path &p) {
  std::ifstream f(p, std::ios::in | std::ios::binary);
  if (!f.is_open())
    return {};
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

// Transparently decompress ZLIB data. Returns raw content if not compressed.
static std::string loadMaybeZlib(const fs::path &p) {
  std::vector<char> data = [&]() {
    std::ifstream f(p, std::ios::in | std::ios::binary);
    if (!f.is_open())
      return std::vector<char>{};
    return std::vector<char>((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
  }();
  constexpr size_t kMaxRaw = 1 << 24;
  constexpr size_t kMaxInflated = 128 << 20;
  if (data.size() > kMaxRaw || data.empty())
    return std::string(data.begin(), data.end());

  uLongf destSize = compressBound((uLong)data.size());
  std::vector<char> buf(destSize);
  int rc;
  while ((rc = uncompress((Bytef *)buf.data(), &destSize,
                          (const Bytef *)data.data(), (uLong)data.size())) ==
         Z_BUF_ERROR) {
    if (buf.size() >= kMaxInflated)
      return std::string(data.begin(), data.end());
    destSize = buf.size() * 2;
    buf.resize(destSize);
  }
  if (rc == Z_OK)
    return std::string(buf.data(), destSize);
  return std::string(data.begin(), data.end());
}

// ============================================================
// Platform paths
// ============================================================

std::string AppConfig::configPath() const { return defaultConfigPath(); }

std::string AppConfig::defaultConfigPath() {
#ifdef _WIN32
  // Check %APPDATA%/Themis.cfg first (backward compat with Themis)
  const char *appdata = std::getenv("APPDATA");
  if (appdata) {
    fs::path themis = fs::path(appdata) / "Themis.cfg";
    if (fs::exists(themis))
      return themis.string();
  }
#endif
  const char *home = std::getenv("HOME");
  if (!home)
    home = std::getenv("USERPROFILE");
  if (!home)
    return "oj.cfg";
  return (fs::path(home) / ".config" / "online-judger" / "oj.cfg").string();
}

std::string AppConfig::themisConfigPath() {
#ifdef _WIN32
  const char *appdata = std::getenv("APPDATA");
  if (appdata) {
    fs::path p = fs::path(appdata) / "Themis.cfg";
    if (fs::exists(p))
      return p.string();
  }
#endif
  return {};
}

// ============================================================
// Default compilers (platform-specific, matches CLI defaults)
// ============================================================

std::vector<CompilerItem> AppConfig::defaultCompilers() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  return {
      {".cpp", "g++ -std=c++14 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ "
               "-o\"%NAME%.exe\"|@WorkDir=%PATH%"},
      {".c", "gcc -std=c11 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c "
             "-o\"%NAME%.exe\"|@WorkDir=%PATH%"},
      {".pas",
       "fpc -o\"%NAME%.exe\" -O2 -XS -Sg \"%NAME%%EXT%\"|@WorkDir=%PATH%"},
      {".pp",
       "fpc -o\"%NAME%.exe\" -O2 -XS -Sg \"%NAME%%EXT%\"|@WorkDir=%PATH%"},
      {".java", "\"javac\" \"%NAME%%EXT%\"|@WorkDir=%PATH%"},
      {".exe", ";No recompile if .exe already exists"},
      {".class", ";No recompile if .class already exists"},
      {".py", ";Python source interpreted"},
  };
#else
  return {
      {".cpp", "g++ -std=c++14 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c++ "
               "-o\"%NAME%.exe\" -Wl,--stack,66060288|@WorkDir=%PATH%"},
      {".c", "gcc -std=c11 \"%NAME%%EXT%\" -pipe -O2 -s -static -lm -x c "
             "-o\"%NAME%.exe\" -Wl,--stack,66060288|@WorkDir=%PATH%"},
      {".pas", "fpc -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\""
               "|@WorkDir=%PATH%"},
      {".pp", "fpc -o\"%NAME%.exe\" -O2 -XS -Sg -Cs66060288 \"%NAME%%EXT%\""
              "|@WorkDir=%PATH%"},
      {".java", "\"javac\" \"%NAME%%EXT%\"|@WorkDir=%PATH%"},
      {".exe", ";No recompile if .exe already exists"},
      {".class", ";No recompile if .class already exists"},
      {".py", ";Python source interpreted"},
  };
#endif
}

// ============================================================
// Themis XML loader (tinyxml2)
// ============================================================

bool AppConfig::loadFromThemis(const std::string &path) {
  std::string raw = readFileBytes(path);
  if (raw.empty())
    return false;

  tinyxml2::XMLDocument doc;
  if (doc.Parse(raw.c_str(), raw.size()) != tinyxml2::XML_SUCCESS)
    return false;

  auto *root = doc.FirstChildElement("ThemisConfiguration");
  if (!root)
    root = doc.FirstChildElement("Configuration");
  if (!root)
    return false;

  auto *cc = root->FirstChildElement("CompilerConfigurations");
  if (!cc)
    return false;

  m_cfg.compiler.items.clear();
  for (auto *item = cc->FirstChildElement("Item"); item;
       item = item->NextSiblingElement("Item")) {
    const char *ext = item->Attribute("ext");
    const char *cmd = item->Attribute("cmd");
    if (ext && cmd)
      m_cfg.compiler.items.push_back({ext, cmd});
  }
  return !m_cfg.compiler.items.empty();
}

// ============================================================
// JSON loader/saver (nlohmann/json)
// ============================================================

bool AppConfig::loadFromJson(const std::string &path) {
  std::string raw = readFileBytes(path);
  if (raw.empty())
    return false;

  auto j = nlohmann::json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    return false;

  if (j.contains("testsPath"))
    m_testsPath = j["testsPath"].get<std::string>();
  if (j.contains("submissionsPath"))
    m_submissionsPath = j["submissionsPath"].get<std::string>();

  if (j.contains("compilers") && j["compilers"].is_array()) {
    m_cfg.compiler.items.clear();
    for (auto &c : j["compilers"]) {
      CompilerItem ci;
      if (c.contains("ext"))
        ci.ext = c["ext"].get<std::string>();
      if (c.contains("cmd"))
        ci.cmd = c["cmd"].get<std::string>();
      m_cfg.compiler.items.push_back(std::move(ci));
    }
  }
  return true;
}

bool AppConfig::save() {
  std::string path = configPath();
  fs::path p(path);
  if (p.has_parent_path())
    fs::create_directories(p.parent_path());

  nlohmann::json root;
  root["testsPath"] = m_testsPath;
  root["submissionsPath"] = m_submissionsPath;

  nlohmann::json compArr = nlohmann::json::array();
  for (auto &ci : m_cfg.compiler.items) {
    compArr.push_back({{"ext", ci.ext}, {"cmd", ci.cmd}});
  }
  root["compilers"] = compArr;

  std::ofstream f(path, std::ios::out | std::ios::trunc);
  if (!f.is_open())
    return false;
  f << root.dump(2) << "\n";
  return true;
}

// ============================================================
// Public API
// ============================================================

bool AppConfig::load() {
  // 1. Try Themis config (Windows: %APPDATA%/Themis.cfg)
  std::string themis = themisConfigPath();
  if (!themis.empty())
    loadFromThemis(themis);

  // 2. Our JSON config (overrides compilers + provides paths)
  std::string cfgPath = configPath();
  if (fs::exists(cfgPath))
    loadFromJson(cfgPath);

  // 3. Fill defaults if no compilers loaded
  if (m_cfg.compiler.items.empty())
    m_cfg.compiler.items = defaultCompilers();

  return true;
}

Configuration &AppConfig::configuration() { return m_cfg; }
const Configuration &AppConfig::configuration() const { return m_cfg; }

std::vector<CompilerItem> &AppConfig::compilers() {
  return m_cfg.compiler.items;
}
const std::vector<CompilerItem> &AppConfig::compilers() const {
  return m_cfg.compiler.items;
}

std::string AppConfig::testsPath() const { return m_testsPath; }
std::string AppConfig::submissionsPath() const { return m_submissionsPath; }
void AppConfig::setTestsPath(const std::string &path) { m_testsPath = path; }
void AppConfig::setSubmissionsPath(const std::string &path) {
  m_submissionsPath = path;
}
