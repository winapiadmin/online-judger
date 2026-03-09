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
           std::filesystem::path &judger_path);
std::map<std::pair<std::string, std::string>, std::pair<std::string, double>>
getScores();
