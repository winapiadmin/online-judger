#pragma once
#include "Base.h"
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
void judge(std::filesystem::path subdir, std::filesystem::path tdir,
           std::string problem, std::string user, const Configuration &conf,
           const std::unordered_map<std::string, Testcases> &testcases,
           const std::filesystem::path &judger_path);
std::map<std::pair<std::string, std::string>, std::pair<std::string, double>>
getScores();

// Resolve a configured evaluator file name to this platform's file name,
// preferring whichever variant actually exists in judgersDir: legacy Themis
// configs carry "C5Binary.dll" while Linux builds ship "libC5Binary.so"
// (and MSVC builds ship "C5Binary.dll" with no lib prefix, even when
// __MSYS__ is defined). Falls back to the platform-mapped name when neither
// exists so the loader can report the missing file.
std::string
resolveEvaluatorName(const std::string &configured,
                     const std::filesystem::path &judgersDir);
