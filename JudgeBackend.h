#pragma once
#include "Base.h"
#include <filesystem>
#include <map>
#include <string>
#include <utility>
void judge(std::filesystem::path subdir, std::filesystem::path tdir,
           std::string problem, std::string user, const Configuration &conf,
           const std::unordered_map<std::string, Testcases> &testcases,
           std::filesystem::path &judger_path);
std::map<std::pair<std::string, std::string>, double> getScores();