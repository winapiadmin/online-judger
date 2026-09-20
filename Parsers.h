#pragma once
#include "Base.h"
#include <string_view>
enum class ParseType {
  JSON,
  YAML,
  TOML,
  XML,
};
template <ParseType T>
void ParseTestSettings(const std::string_view &sv, Testcases &tc);
template <ParseType T>
void ParseGlobalOptions(const std::string_view &sv, Configuration &tc);

// Auto-detect format and parse global configuration (YAML > JSON > XML > TOML).
void parseGlobalSettingsFormat(const std::string_view sv, Configuration &tc);
// Auto-detect format and parse test settings.
void parseSettingsFormat(const std::string_view sv, Testcases &tc);