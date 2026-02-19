#pragma once
#include <string>
#include <vector>

struct Subtest{
  std::string Name;
  int MemoryLimit; // or -1 to inherit
  float TimeLimit;
  float Mark;
};
struct Testcases{
  std::string Name;
  std::string InputFile;
  std::string OutputFile;
  std::string EvaluatorName; // paths
  bool UseStdIn=false, UseStdOut=false;
  size_t MemoryLimit; // MiB
  float TimeLimit; // seconds
  float Mark;
  std::vector<Subtest> subtests;
};

struct CompilerItem {
    std::string ext;
    std::string cmd;
};

struct CompilerConfiguration {
    std::string identifier;
    std::vector<CompilerItem> items;
};

struct Environment {
    std::string identifier;
    std::string submitDir;
    std::string decompressDir;
    bool activeSecurity = false;
    std::string contestHouse;
    std::string adminUserName;
    std::string adminPassword;
    std::string adminDomain;
    std::string lastExamDir; // unused
    std::string lastContestantDir; // unused
    int examEditAction = 0; // unused
    bool toolBarVisible = false; // unused
};
struct Configuration {
    CompilerConfiguration compiler;
    Environment environment;
};
