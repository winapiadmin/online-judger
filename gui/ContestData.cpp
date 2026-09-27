#include "ContestData.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <fstream>
#include <sstream>
#include <tinyxml2.h>
#include <zlib.h>
#include <zip.h>
#include <unzip.h>
#include <ioapi.h>

namespace fs = std::filesystem;

static std::string zlib_compress(const std::string &in) {
  uLongf dest = compressBound(static_cast<uLong>(in.size()));
  std::string out(dest, '\0');
  if (compress2(reinterpret_cast<Bytef *>(out.data()), &dest,
                reinterpret_cast<const Bytef *>(in.data()),
                static_cast<uLong>(in.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
    return {};
  out.resize(dest);
  return out;
}

static std::string zlib_decompress(const std::string &in) {
  std::string out(in.size() * 4, '\0');
  uLongf dest = static_cast<uLongf>(out.size());
  int rc;
  while ((rc = uncompress(reinterpret_cast<Bytef *>(out.data()), &dest,
                          reinterpret_cast<const Bytef *>(in.data()),
                          static_cast<uLong>(in.size()))) == Z_BUF_ERROR) {
    out.resize(out.size() * 2);
    dest = static_cast<uLongf>(out.size());
  }
  if (rc != Z_OK)
    return {};
  out.resize(dest);
  return out;
}

static std::string readFile(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return {};
  return {std::istreambuf_iterator<char>(f),
          std::istreambuf_iterator<char>()};
}

static bool writeFile(const std::filesystem::path &p, const std::string &s) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  if (!f)
    return false;
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
  return f.good();
}

// Read a file from a minizip archive, decompressing if zlib-compressed.
// method 8 = DEFLATE. We try decompress; if it fails, return raw.
static std::string readZipFile(unzFile uf) {
  unz_file_info64 fi;
  if (unzGetCurrentFileInfo64(uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) !=
      UNZ_OK)
    return {};

  std::string raw(fi.uncompressed_size, '\0');
  if (unzReadCurrentFile(uf, raw.data(),
                         static_cast<unsigned>(fi.uncompressed_size)) !=
      static_cast<int>(fi.uncompressed_size))
    return {};

  // If the file was DEFLATE-compressed in the ZIP, try zlib decompress
  if (fi.compression_method == 8) {
    std::string decompressed = zlib_decompress(raw);
    if (!decompressed.empty())
      return decompressed;
  }
  return raw;
}

// ============================================================
// Settings.cfg read/write (zlib-compressed XML)
// ============================================================
bool readProblemConfig(const std::filesystem::path &testsDir,
                       const std::string &problemName, GuiProblemConfig &out) {
  auto cfgPath = testsDir / problemName / "Settings.cfg";
  std::string raw;
  if (std::filesystem::exists(cfgPath))
    raw = readFile(cfgPath);

  if (raw.empty()) {
    // No Settings.cfg — create defaults from directory scan
    out.name = QString::fromStdString(problemName);
    out.inputFile = QString::fromStdString(problemName + ".INP");
    out.outputFile = QString::fromStdString(problemName + ".OUT");
    out.evaluatorName = "C1LinesWordsIgnoreCase";
    out.useStdIn = false;
    out.useStdOut = false;
    out.memoryLimit = 1024;
    out.timeLimit = 1.0f;
    out.mark = 1.0f;

    auto testDir = testsDir / problemName;
    if (std::filesystem::is_directory(testDir)) {
      for (auto &entry : std::filesystem::directory_iterator(testDir)) {
        if (entry.is_directory()) {
          GuiSubtest st;
          st.name = QString::fromStdString(
              entry.path().relative_path().stem().string());
          st.mark = 1.0f;
          st.timeLimit = -1.0f;
          st.memoryLimit = -1;
          out.subtests.push_back(st);
        }
      }
    }
    return true;
  }

  std::string content = zlib_decompress(raw);
  if (content.empty())
    content = raw;

  tinyxml2::XMLDocument doc;
  if (doc.Parse(content.c_str(), content.size()) != tinyxml2::XML_SUCCESS)
    return false;

  auto *info = doc.FirstChildElement("ExamInformation");
  if (!info)
    return false;

  auto attrS = [](const tinyxml2::XMLElement *e, const char *n) -> std::string {
    const char *v = e->Attribute(n);
    return v ? v : "";
  };

  out.name = QString::fromStdString(attrS(info, "Name"));
  out.inputFile = QString::fromStdString(attrS(info, "InputFile"));
  out.outputFile = QString::fromStdString(attrS(info, "OutputFile"));
  out.evaluatorName = QString::fromStdString(attrS(info, "EvaluatorName"));
  out.useStdIn = strcmp(attrS(info, "UseStdIn").c_str(), "true") == 0;
  out.useStdOut = strcmp(attrS(info, "UseStdOut").c_str(), "true") == 0;
  float mark = 0;
  info->QueryFloatAttribute("Mark", &mark);
  out.mark = mark;
  float tl = 0;
  info->QueryFloatAttribute("TimeLimit", &tl);
  out.timeLimit = tl;
  int ml = 1024;
  info->QueryIntAttribute("MemoryLimit", &ml);
  out.memoryLimit = static_cast<size_t>(ml);

  for (auto *e = info->FirstChildElement("TestCase"); e;
       e = e->NextSiblingElement("TestCase")) {
    GuiSubtest st;
    st.name = QString::fromStdString(attrS(e, "Name"));
    float stm = 0;
    e->QueryFloatAttribute("Mark", &stm);
    st.mark = stm;
    float sttl = 0;
    e->QueryFloatAttribute("TimeLimit", &sttl);
    st.timeLimit = sttl;
    int stml = 0;
    e->QueryIntAttribute("MemoryLimit", &stml);
    st.memoryLimit = stml;
    out.subtests.push_back(st);
  }
  return true;
}

bool writeProblemConfig(const std::filesystem::path &testsDir,
                        const std::string &problemName,
                        const GuiProblemConfig &cfg) {
  tinyxml2::XMLDocument doc;
  auto *info = doc.NewElement("ExamInformation");
  info->SetAttribute("Name", cfg.name.toStdString().c_str());
  info->SetAttribute("InputFile", cfg.inputFile.toStdString().c_str());
  info->SetAttribute("OutputFile", cfg.outputFile.toStdString().c_str());
  info->SetAttribute("UseStdIn", cfg.useStdIn ? "true" : "false");
  info->SetAttribute("UseStdOut", cfg.useStdOut ? "true" : "false");
  info->SetAttribute("EvaluatorName", cfg.evaluatorName.toStdString().c_str());
  info->SetAttribute("Mark", cfg.mark);
  info->SetAttribute("TimeLimit", cfg.timeLimit);
  info->SetAttribute("MemoryLimit", static_cast<int>(cfg.memoryLimit));

  for (auto &st : cfg.subtests) {
    auto *se = doc.NewElement("TestCase");
    se->SetAttribute("Name", st.name.toStdString().c_str());
    se->SetAttribute("Mark", st.mark);
    se->SetAttribute("TimeLimit", st.timeLimit);
    se->SetAttribute("MemoryLimit", st.memoryLimit);
    info->InsertEndChild(se);
  }
  doc.InsertEndChild(info);

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  std::string xml = printer.CStr();
  std::string compressed = zlib_compress(xml);

  auto cfgPath = testsDir / problemName / "Settings.cfg";
  std::filesystem::create_directories(testsDir / problemName);
  return writeFile(cfgPath, compressed.empty() ? xml : compressed);
}

// ============================================================
// Import .contest (Themis ZIP format)
// ============================================================
bool importContestZip(const QString &contestPath, const QString &targetDir,
                      GuiContestData &data) {
  unzFile uf = unzOpen64(contestPath.toStdString().c_str());
  if (!uf)
    return false;

  fs::path tgt(targetDir.toStdString());
  std::filesystem::create_directories(tgt);

  // Extract all files
  if (unzGoToFirstFile(uf) != UNZ_OK) {
    unzClose(uf);
    return false;
  }

  do {
    unz_file_info64 fi;
    char filename[512];
    if (unzGetCurrentFileInfo64(uf, &fi, filename, sizeof(filename), nullptr, 0,
                               nullptr, 0) != UNZ_OK)
      continue;

    std::string name(filename);
    if (name.empty() || name.back() == '/')
      continue; // skip directories

    fs::path outPath = tgt / name;
    std::string content = readZipFile(uf);
    writeFile(outPath, content);
  } while (unzGoToNextFile(uf) == UNZ_OK);

  unzClose(uf);

  // Now load from the extracted folder structure
  data.testsPath = QString::fromStdString((tgt / "Tasks").string());
  data.submissionsPath = QString::fromStdString((tgt / "Contestants").string());
  return loadFromFolders(data);
}

// ============================================================
// Load from folder structure (Tasks/ + Contestants/)
// ============================================================
bool loadFromFolders(GuiContestData &data) {
  data.problems.clear();
  data.contestants.clear();
  data.scores.clear();
  data.verdicts.clear();

  fs::path tdir(data.testsPath.toStdString());
  fs::path sdir(data.submissionsPath.toStdString());

  if (fs::is_directory(tdir)) {
    for (auto &entry : fs::directory_iterator(tdir)) {
      if (entry.is_directory()) {
        auto name = entry.path().stem().string();
        if (name == "$History")
          continue;
        data.problems.append(QString::fromStdString(name));
      }
    }
    std::sort(data.problems.begin(), data.problems.end());
  }

  if (fs::is_directory(sdir)) {
    for (auto &entry : fs::directory_iterator(sdir)) {
      if (entry.is_directory()) {
        auto name = entry.path().stem().string();
        if (name == "$History" || name == "Logs")
          continue;
        data.contestants.append(QString::fromStdString(name));
      }
    }
    std::sort(data.contestants.begin(), data.contestants.end());
  }

  // Try to load Contest.result if present
  auto resultPath = sdir.parent_path() / "Contest.result";
  if (std::filesystem::exists(resultPath)) {
    std::string raw = readFile(resultPath);
    std::string content = zlib_decompress(raw);
    if (content.empty())
      content = raw;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(content.c_str(), content.size()) == tinyxml2::XML_SUCCESS) {
      auto *root = doc.FirstChildElement("ContestResult");
      if (root) {
        for (auto *cr = root->FirstChildElement("ContestantResult"); cr;
             cr = cr->NextSiblingElement("ContestantResult")) {
          const char *cName = cr->Attribute("ContestantName");
          if (!cName)
            continue;
          QString contestant = QString::fromUtf8(cName);

          for (auto *er = cr->FirstChildElement("ExamResult"); er;
               er = er->NextSiblingElement("ExamResult")) {
            const char *eName = er->Attribute("ExamName");
            if (!eName)
              continue;
            QString problem = QString::fromUtf8(eName);

            float eval = 0;
            er->QueryFloatAttribute("Evaluation", &eval);

            bool hasFailed = false;
            for (auto *tr = er->FirstChildElement("TestResult"); tr;
                 tr = tr->NextSiblingElement("TestResult")) {
              float tEval = 0;
              tr->QueryFloatAttribute("Evaluation", &tEval);
              if (tEval == 0.0f) {
                hasFailed = true;
                break;
              }
            }

            data.scores[contestant][problem] = eval;
            data.verdicts[contestant + "/" + problem] =
                hasFailed ? "X" : "V";
          }
        }
      }
    }
  }

  return true;
}

// ============================================================
// Export .contest (Themis ZIP format)
// ============================================================
bool exportContestZip(const QString &path, const GuiContestData &data) {
  zipFile zf = zipOpen64(path.toStdString().c_str(), 0);
  if (!zf)
    return false;

  auto addCompressed = [&](const std::string &name, const std::string &data) {
    std::string compressed = zlib_compress(data);
    const std::string &toWrite = compressed.empty() ? data : compressed;
    zip_fileinfo zi{};
    if (zipOpenNewFileInZip64(zf, name.c_str(), &zi, nullptr, 0, nullptr, 0,
                              nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) !=
        ZIP_OK)
      return false;
    zipWriteInFileInZip(zf, toWrite.c_str(),
                        static_cast<unsigned>(toWrite.size()));
    zipCloseFileInZip(zf);
    return true;
  };

  auto addRaw = [&](const std::string &name, const std::string &raw) {
    zip_fileinfo zi{};
    if (zipOpenNewFileInZip64(zf, name.c_str(), &zi, nullptr, 0, nullptr, 0,
                              nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) !=
        ZIP_OK)
      return false;
    zipWriteInFileInZip(zf, raw.c_str(), static_cast<unsigned>(raw.size()));
    zipCloseFileInZip(zf);
    return true;
  };

  fs::path tdir(data.testsPath.toStdString());
  fs::path sdir(data.submissionsPath.toStdString());

  // 1. Tasks.config — zlib-compressed XML listing all problems
  {
    tinyxml2::XMLDocument doc;
    auto *tasks = doc.NewElement("Tasks");
    for (auto &prob : data.problems) {
      GuiProblemConfig cfg;
      readProblemConfig(tdir, prob.toStdString(), cfg);

      auto *exam = doc.NewElement("Exam");
      exam->SetAttribute("Name", cfg.name.toStdString().c_str());
      exam->SetAttribute("InputFile", cfg.inputFile.toStdString().c_str());
      exam->SetAttribute("OutputFile", cfg.outputFile.toStdString().c_str());
      exam->SetAttribute("UseStdIn", cfg.useStdIn ? "true" : "false");
      exam->SetAttribute("UseStdOut", cfg.useStdOut ? "true" : "false");
      exam->SetAttribute("EvaluatorName",
                         cfg.evaluatorName.toStdString().c_str());
      exam->SetAttribute("Mark", cfg.mark);
      exam->SetAttribute("TimeLimit", cfg.timeLimit);
      exam->SetAttribute("MemoryLimit", static_cast<int>(cfg.memoryLimit));

      for (auto &st : cfg.subtests) {
        auto *tc = doc.NewElement("TestCase");
        tc->SetAttribute("Name", st.name.toStdString().c_str());
        tc->SetAttribute("Mark", st.mark);
        tc->SetAttribute("TimeLimit", st.timeLimit);
        tc->SetAttribute("MemoryLimit", st.memoryLimit);
        exam->InsertEndChild(tc);
      }
      tasks->InsertEndChild(exam);
    }
    doc.InsertEndChild(tasks);
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    addCompressed("Tasks.config", printer.CStr());
  }

  // 2. TaskDirectories.txt
  {
    std::ostringstream ss;
    for (auto &prob : data.problems) {
      ss << prob.toStdString() << "\n";
      auto probDir = tdir / prob.toStdString();
      if (fs::is_directory(probDir)) {
        for (auto &sub : fs::directory_iterator(probDir)) {
          if (sub.is_directory())
            ss << prob.toStdString() << "\\"
               << sub.path().stem().string() << "\n";
        }
      }
    }
    addCompressed("TaskDirectories.txt", ss.str());
  }

  // 3. ContestantDirectories.txt
  {
    std::ostringstream ss;
    for (auto &cont : data.contestants) {
      ss << cont.toStdString() << "\n";
      ss << cont.toStdString() << "\\$History\n";
    }
    addCompressed("ContestantDirectories.txt", ss.str());
  }

  // 4. Tasks/<problem>/Settings.cfg + test data files
  for (auto &prob : data.problems) {
    GuiProblemConfig cfg;
    readProblemConfig(tdir, prob.toStdString(), cfg);

    // Write Settings.cfg (compressed XML)
    tinyxml2::XMLDocument doc;
    auto *info = doc.NewElement("ExamInformation");
    info->SetAttribute("Name", cfg.name.toStdString().c_str());
    info->SetAttribute("InputFile", cfg.inputFile.toStdString().c_str());
    info->SetAttribute("OutputFile", cfg.outputFile.toStdString().c_str());
    info->SetAttribute("UseStdIn", cfg.useStdIn ? "true" : "false");
    info->SetAttribute("UseStdOut", cfg.useStdOut ? "true" : "false");
    info->SetAttribute("EvaluatorName",
                       cfg.evaluatorName.toStdString().c_str());
    info->SetAttribute("Mark", cfg.mark);
    info->SetAttribute("TimeLimit", cfg.timeLimit);
    info->SetAttribute("MemoryLimit", static_cast<int>(cfg.memoryLimit));
    for (auto &st : cfg.subtests) {
      auto *se = doc.NewElement("TestCase");
      se->SetAttribute("Name", st.name.toStdString().c_str());
      se->SetAttribute("Mark", st.mark);
      se->SetAttribute("TimeLimit", st.timeLimit);
      se->SetAttribute("MemoryLimit", st.memoryLimit);
      info->InsertEndChild(se);
    }
    doc.InsertEndChild(info);
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    addCompressed("Tasks/" + prob.toStdString() + "/Settings.cfg",
                  printer.CStr());

    // Add test data files
    auto probDir = tdir / prob.toStdString();
    if (fs::is_directory(probDir)) {
      for (auto &sub : fs::directory_iterator(probDir)) {
        if (!sub.is_directory())
          continue;
        std::string testDir = sub.path().stem().string();
        for (auto &file : fs::directory_iterator(sub.path())) {
          if (!file.is_regular_file())
            continue;
          std::string fname = file.path().filename().string();
          std::string zipPath = "Tasks/" + prob.toStdString() + "/" +
                                testDir + "/" + fname;
          std::string content = readFile(file.path());
          addRaw(zipPath, content);
        }
      }
    }
  }

  // 5. Contestants/<name>/<PROBLEM>.<ext> + $History
  for (auto &cont : data.contestants) {
    auto contDir = sdir / cont.toStdString();
    if (!fs::is_directory(contDir))
      continue;

    for (auto &entry : fs::directory_iterator(contDir)) {
      if (!entry.is_regular_file())
        continue;
      std::string fname = entry.path().filename().string();
      std::string zipPath =
          "Contestants/" + cont.toStdString() + "/" + fname;
      std::string content = readFile(entry.path());
      addRaw(zipPath, content);
    }

    // $History
    auto histDir = contDir / "$History";
    if (fs::is_directory(histDir)) {
      for (auto &entry : fs::directory_iterator(histDir)) {
        if (!entry.is_regular_file())
          continue;
        std::string fname = entry.path().filename().string();
        std::string zipPath = "Contestants/" + cont.toStdString() +
                              "/$History/" + fname;
        std::string content = readFile(entry.path());
        addRaw(zipPath, content);
      }
    }
  }

  // 6. Contest.result — zlib-compressed XML with per-contestant scores
  {
    tinyxml2::XMLDocument doc;
    auto *root = doc.NewElement("ContestResult");
    for (auto &cont : data.contestants) {
      double totalScore = 0;
      for (auto &prob : data.problems)
        totalScore += data.scores.value(cont).value(prob, 0.0);

      auto *cr = doc.NewElement("ContestantResult");
      cr->SetAttribute("ContestantName", cont.toStdString().c_str());
      cr->SetAttribute("Evaluation", totalScore);

      for (auto &prob : data.problems) {
        double score = data.scores.value(cont).value(prob, 0.0);
        QString verdict = data.verdicts.value(cont + "/" + prob, "-");

        auto *er = doc.NewElement("ExamResult");
        er->SetAttribute("ExamName", prob.toStdString().c_str());
        er->SetAttribute("State", verdict == "V" ? "1" : "0");
        er->SetAttribute("Evaluation", score);

        GuiProblemConfig cfg;
        readProblemConfig(tdir, prob.toStdString(), cfg);
        for (auto &st : cfg.subtests) {
          auto *tr = doc.NewElement("TestResult");
          tr->SetAttribute("TestName", st.name.toStdString().c_str());
          tr->SetAttribute("Evaluation",
                           score > 0 ? st.mark : 0.0f);
          er->InsertEndChild(tr);
        }
        cr->InsertEndChild(er);
      }
      root->InsertEndChild(cr);
    }
    doc.InsertEndChild(root);
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    addCompressed("Contest.result", printer.CStr());
  }

  zipClose(zf, nullptr);
  return true;
}

// ============================================================
// Export CSV
// ============================================================
bool exportCSV(const QString &path, const GuiContestData &data) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;

  QTextStream ts(&file);
  ts << "Contestant";
  for (auto &p : data.problems)
    ts << "," << p;
  ts << ",Total\n";

  for (auto &c : data.contestants) {
    ts << c;
    double total = 0;
    for (auto &p : data.problems) {
      double s = data.scores.value(c).value(p, 0.0);
      total += s;
      ts << "," << s;
    }
    ts << "," << total << "\n";
  }
  return true;
}

// ============================================================
// Conversion helpers between AppConfig and GUI types
// ============================================================
std::vector<GuiCompilerItem>
toGuiCompilers(const std::vector<CompilerItem> &items) {
  std::vector<GuiCompilerItem> out;
  out.reserve(items.size());
  for (auto &ci : items)
    out.push_back({QString::fromStdString(ci.ext),
                   QString::fromStdString(ci.cmd)});
  return out;
}

std::vector<CompilerItem>
fromGuiCompilers(const std::vector<GuiCompilerItem> &items) {
  std::vector<CompilerItem> out;
  out.reserve(items.size());
  for (auto &gi : items)
    out.push_back({gi.ext.toStdString(), gi.cmd.toStdString()});
  return out;
}

// ============================================================
// Build Testcases map from tests directory (for judge())
// ============================================================
std::unordered_map<std::string, Testcases>
buildTestcasesMap(const fs::path &testsDir) {
  std::unordered_map<std::string, Testcases> result;

  for (auto &fd : fs::directory_iterator(testsDir)) {
    if (!fd.is_directory())
      continue;
    std::string name = fd.path().filename().string();
    auto settings_path = fd.path() / "Settings.cfg";

    if (fs::exists(settings_path)) {
      GuiProblemConfig guiCfg;
      readProblemConfig(testsDir, name, guiCfg);

      Testcases &tc = result[name];
      tc.Name = guiCfg.name.toStdString();
      tc.InputFile = guiCfg.inputFile.toStdString();
      tc.OutputFile = guiCfg.outputFile.toStdString();
      tc.EvaluatorName = guiCfg.evaluatorName.toStdString();
      tc.UseStdIn = guiCfg.useStdIn;
      tc.UseStdOut = guiCfg.useStdOut;
      tc.MemoryLimit = guiCfg.memoryLimit;
      tc.TimeLimit = guiCfg.timeLimit;
      tc.Mark = guiCfg.mark;

      for (auto &gs : guiCfg.subtests) {
        tc.subtests.push_back({gs.name.toStdString(), gs.memoryLimit,
                               gs.timeLimit, gs.mark});
      }

      // Fix evaluator path: lib prefix + platform extension
      fs::path f(tc.EvaluatorName);
      f.replace_filename(
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__) ||           \
    defined(__MSYS__)
          "lib" +
#endif
          f.filename().string());
      f.replace_extension(
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
          ".so"
#elif defined(_WIN32)
          ".dll"
#else
          f.extension()
#endif
      );
      tc.EvaluatorName = f.string();
    } else {
      // No Settings.cfg — create default test case
      Testcases &tc = result[name];
      tc.InputFile = name + ".INP";
      tc.OutputFile = name + ".OUT";
      tc.EvaluatorName =
#ifdef _WIN32
#ifdef __MSYS__
          "lib"
#endif
          "C1LinesWordsIgnoreCase.dll";
#else
          "libC1LinesWordsIgnoreCase.so";
#endif
      tc.MemoryLimit = 1024;
      tc.TimeLimit = 1.0;
      tc.Mark = 1.0;

      for (auto &test : fs::directory_iterator(fd.path())) {
        if (!test.is_directory())
          continue;
        tc.subtests.push_back(Subtest{
            test.path().filename().string(), -1, -1, 1.0});
      }
    }
  }
  return result;
}
