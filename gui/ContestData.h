#pragma once
#include "Base.h"
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <filesystem>
#include <unordered_map>
#include <vector>

struct GuiSubtest {
  QString name;
  int memoryLimit = -1;
  float timeLimit = -1;
  float mark = 1.0f;
};

struct GuiProblemConfig {
  QString name;
  QString inputFile;
  QString outputFile;
  QString evaluatorName;
  bool useStdIn = false;
  bool useStdOut = false;
  size_t memoryLimit = 1024;
  float timeLimit = 1.0f;
  float mark = 1.0f;
  std::vector<GuiSubtest> subtests;
};

struct GuiCompilerItem {
  QString ext;
  QString cmd;
};

struct GuiContestData {
  QString testsPath;
  QString submissionsPath;
  QStringList problems;
  QStringList contestants;
  QMap<QString, QMap<QString, double>> scores;
  QMap<QString, QString> verdicts;
  std::vector<GuiCompilerItem> compilers;
};

// Read/write Settings.cfg (zlib-compressed XML) for a problem
bool readProblemConfig(const std::filesystem::path &testsDir,
                       const std::string &problemName, GuiProblemConfig &out);
bool writeProblemConfig(const std::filesystem::path &testsDir,
                        const std::string &problemName,
                        const GuiProblemConfig &cfg);

// Import .contest (Themis ZIP format) into a target directory,
// then populate GuiContestData from the extracted folder structure.
bool importContestZip(const QString &contestPath, const QString &targetDir,
                      GuiContestData &data);

// Export .contest (Themis ZIP format) from loaded data.
// Creates Tasks.config, TaskDirectories.txt, ContestantDirectories.txt,
// Contest.result, Tasks/<problem>/Settings.cfg + test files,
// Contestants/<name>/<PROBLEM>.<ext> + $History.
bool exportContestZip(const QString &path, const GuiContestData &data);

// Load GuiContestData from an extracted folder structure on disk.
bool loadFromFolders(GuiContestData &data);

// Export CSV
bool exportCSV(const QString &path, const GuiContestData &data);

// Conversion helpers between AppConfig (std::string) and GUI (QString) types.
std::vector<GuiCompilerItem>
toGuiCompilers(const std::vector<CompilerItem> &items);
std::vector<CompilerItem>
fromGuiCompilers(const std::vector<GuiCompilerItem> &items);

// Build a Testcases map from the tests directory (for judge()).
std::unordered_map<std::string, Testcases>
buildTestcasesMap(const std::filesystem::path &testsDir);
