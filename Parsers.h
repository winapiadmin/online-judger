#pragma once
#include "Base.h"
#include <string_view>
enum class ParseType{
    JSON,
    YAML,
    TOML,
    XML,
};
template <ParseType T>
void ParseTestSettings(const std::string_view &sv, Testcases &tc);
template <ParseType T>
void ParseGlobalOptions(const std::string_view &sv, Configuration &tc);