#pragma once
#include "Base.h"
#include <optional>
#include <string>
#include <vector>

// Unified configuration for CLI and GUI.
// Loads from: Themis XML (%APPDATA%/Themis.cfg) → JSON (~/.config/online-judger/oj.cfg) → defaults.
// Saves to: JSON only.
class AppConfig {
public:
  AppConfig() = default;

  // Load config from Themis XML → our JSON → defaults.
  bool load();

  // Save config to ~/.config/online-judger/oj.cfg (JSON).
  bool save();

  // The config file path (~/.config/online-judger/oj.cfg).
  std::string configPath() const;

  // Direct access to the underlying Configuration.
  Configuration &configuration();
  const Configuration &configuration() const;

  // Compiler convenience accessors.
  std::vector<CompilerItem> &compilers();
  const std::vector<CompilerItem> &compilers() const;

  // Tests/submissions path (stored in JSON, not in Themis XML).
  std::string testsPath() const;
  std::string submissionsPath() const;
  void setTestsPath(const std::string &path);
  void setSubmissionsPath(const std::string &path);

private:
  Configuration m_cfg;
  std::string m_testsPath;
  std::string m_submissionsPath;

  static std::string defaultConfigPath();
  static std::string themisConfigPath();
  static std::vector<CompilerItem> defaultCompilers();
  bool loadFromThemis(const std::string &path);
  bool loadFromJson(const std::string &path);
};
