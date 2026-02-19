// stl
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <CLI/CLI.hpp>
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/exceptions.hpp>
#include <cpptrace/from_current_macros.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Helpers/HexDump.h>
#include <plog/Init.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Log.h>
#include <signal.h>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>
std::function<void()> fn;
#include "Base.h"
#include "JudgeBackend.h"
#include "Parsers.h"
#include "SubmissionWatcher.h"
using namespace std;
namespace fs = filesystem;
plog::ColorConsoleAppender<plog::TxtFormatter> appender;
#ifdef _WIN32
BOOL WINAPI SignalHandler(DWORD) {
  fn();
  return TRUE;
}
#else
void SignalHandler(int v) {
  if (v == SIGINT) {
    fn();
    exit(0);
  }
}
#endif
void termination() {
  if (auto eptr = std::current_exception()) { // get the active exception
    try {
      std::rethrow_exception(eptr);
    } catch (const std::exception &e) {
      PLOGE << "Unhandled std::exception: " << typeid(e).name()
            << " what(): " << e.what() << "\n";
      cpptrace::generate_trace().print();
    } catch (...) {
      PLOGE << "Unhandled non-std exception\n";
      cpptrace::generate_trace().print();
    }
  } else {
    cpptrace::generate_trace().print();
    PLOGE << "terminate() called without an active exception\n";
  }
  exit(-1);
}

void parseGlobalSettingsFormat(const std::string_view sv, Configuration &tc) {
  try {
    ParseGlobalOptions<ParseType::YAML>(sv, tc);
    PLOGI << "parsed as YAML (success)";
    return;
  } catch (...) {
  }
  try {
    ParseGlobalOptions<ParseType::JSON>(sv, tc);
    PLOGI << "parsed as JSON (success)";
    return;
  } catch (...) {
  }
  try {
    ParseGlobalOptions<ParseType::XML>(sv, tc);
    PLOGI << "parsed as XML (success)";
    return;
  } catch (...) {
  }
  try {
    ParseGlobalOptions<ParseType::TOML>(sv, tc);
    PLOGI << "parsed as TOML (success)";
    return;
  } catch (...) {
  }
  throw std::runtime_error("File format not implemented");
}
void parseSettingsFormat(const std::string_view sv, Testcases &tc) {
  try {
    ParseTestSettings<ParseType::YAML>(sv, tc);
    PLOGI << "Problem: " << tc.Name << " parsed as YAML (success)";
    return;
  } catch (...) {
  }
  try {
    ParseTestSettings<ParseType::JSON>(sv, tc);
    PLOGI << "Problem: " << tc.Name << " parsed as JSON (success)";
    return;
  } catch (...) {
  }
  try {
    ParseTestSettings<ParseType::XML>(sv, tc);
    PLOGI << "Problem: " << tc.Name << " parsed as XML (success)";
    return;
  } catch (...) {
  }
  try {
    ParseTestSettings<ParseType::TOML>(sv, tc);
    PLOGI << "Problem: " << tc.Name << " parsed as TOML (success)";
    return;
  } catch (...) {
  }
  throw std::runtime_error("File format not implemented");
}
int main(int argc, char **argv) {
  std::set_terminate(termination);
  plog::init(plog::verbose, &appender);
  fs::path subdir, tdir, compfile, judgers = "judgers";
  bool waitSubmittorMode = false;
  CLI::App app{"competitive programming judger"};
  argv = app.ensure_utf8(argv);

  auto *io = app.add_option_group("Input");
  io->add_option("-s,--submissions", subdir)
      ->required()
      ->option_text("PATH")
      ->check(CLI::ExistingDirectory);
  io->add_option("-t,--tests", tdir)
      ->required()
      ->option_text("PATH")
      ->check(CLI::ExistingDirectory);

  auto *cfg = app.add_option_group("Configuration");
  cfg->add_option("-c,--settings", compfile)
      ->option_text("FILE")
      ->check(CLI::ExistingFile);
  cfg->add_option("-j,--judge-paths", judgers)
      ->option_text("PATH")
      ->check(CLI::ExistingDirectory);

  auto *mode = app.add_option_group("Mode");
  mode->add_flag("-w,--wait-submittor-mode", waitSubmittorMode,
                 "Wait for new submissions instead of exiting");

  app.get_formatter()->column_width(32);
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    PLOGE << "Failed to parse arguments - see help below";
    return app.exit(e);
  }

  subdir = fs::canonical(subdir);
  tdir = fs::canonical(tdir);
  if (!compfile.empty())
    compfile = fs::canonical(compfile);

  Configuration globalInfo;
  if (!compfile.empty()) {
    std::fstream binary(compfile.string(), ios::in | ios::binary);
    binary.seekg(0, ios::end);
    size_t size = binary.tellg();
    binary.seekg(0, ios::beg);
    if (size > (1 << 24)) // 16MiB of testcases even if uncompressed?
    {
      throw std::runtime_error("Is this excessive for compiler info? Possibly "
                               "specifically crafted input.");
    }
    std::vector<char> data(size);

    binary.read(data.data(), size);
    uLongf destSize = size * 1032; // starting guess
    std::vector<char> processed(destSize);

    int rc;
    while ((rc = uncompress((Bytef *)processed.data(), &destSize,
                            (const Bytef *)data.data(), size)) == Z_BUF_ERROR) {
      destSize *= 2;
      processed.resize(destSize);
    }
    if (rc == Z_OK) {
      PLOGD << compfile.string() << " is ZLIB compressed";
      parseGlobalSettingsFormat(std::string_view(processed.data(), destSize),
                                globalInfo);
    } else {
      PLOGD << compfile.string() << " is not ZLIB compressed, error=" << rc;
      parseGlobalSettingsFormat(std::string_view(data.data(), data.size()),
                                globalInfo);
    }
  } else {
    PLOGD << "Using default options";
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    parseGlobalSettingsFormat(
        R"(<ThemisConfiguration><CompilerConfigurations Identifier="THEMISCompiler"><Item ext=".cpp" cmd="g++ -std=c++14 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c++ -o&quot;%NAME%.exe&quot;|@WorkDir=%PATH%"/><Item ext=".c" cmd="gcc -std=c11 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c -o&quot;%NAME%.exe&quot;|@WorkDir=%PATH%" /><Item ext=".pas" cmd="fpc -o&quot;%NAME%.exe&quot; -O2 -XS -Sg &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".pp" cmd="fpc -o&quot;%NAME%.exe&quot; -O2 -XS -Sg &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".java" cmd="&quot;javac&quot; &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".exe" cmd=";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu" /><Item ext=".class" cmd=";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này lên đầu" /><Item ext=".py" cmd=";Mã nguồn Python được thông dịch!" /></CompilerConfigurations><Environment Identifier="THEMISEnvironment" SubmitDir="" DecompressDir="C:\ProgramData\" ActiveSecurity="false" ContestHouse="/tmp" AdminUserName="" AdminPassword="" AdminDomain="WINDOWS" LastExamDir="" LastContestantDir="" ExamEditAction="0" ToolBarVisible="true"/></ThemisConfiguration>)"sv,
        globalInfo);
#else
    parseGlobalSettingsFormat(
        R"(<ThemisConfiguration><CompilerConfigurations Identifier="THEMISCompiler"><Item ext=".cpp" cmd="g++ -std=c++14 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c++ -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288|@WorkDir=%PATH%"/><Item ext=".c" cmd="gcc -std=c11 &quot;%NAME%%EXT%&quot; -pipe -O2 -s -static -lm -x c -o&quot;%NAME%.exe&quot; -Wl,--stack,66060288|@WorkDir=%PATH%" /><Item ext=".pas" cmd="fpc -o&quot;%NAME%.exe&quot; -O2 -XS -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".pp" cmd="fpc -o&quot;%NAME%.exe&quot; -O2 -XS -Sg -Cs66060288 &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".java" cmd="&quot;javac&quot; &quot;%NAME%%EXT%&quot;|@WorkDir=%PATH%" /><Item ext=".exe" cmd=";Nếu không muốn dịch lại khi đã có file .exe, chuyển loại file này lên đầu" /><Item ext=".class" cmd=";Nếu không muốn dịch lại khi đã có file .class, chuyển loại file này lên đầu" /><Item ext=".py" cmd=";Mã nguồn Python được thông dịch!" /></CompilerConfigurations><Environment Identifier="THEMISEnvironment" SubmitDir="" DecompressDir="C:\ProgramData\" ActiveSecurity="false" ContestHouse="C:\ProgramData\" AdminUserName="" AdminPassword="" AdminDomain="WINDOWS" LastExamDir="" LastContestantDir="" ExamEditAction="0" ToolBarVisible="true"/></ThemisConfiguration>)"sv,
        globalInfo);
#endif
  }
  // discover TCs
  unordered_map<string, Testcases> testcases;
  for (auto &fd : fs::directory_iterator(tdir)) {
    if (!fd.is_directory())
      continue;
    string name = fd.path().relative_path().stem().string();
    auto settings_path = fd.path() / "Settings.cfg";
    string inpf = name + ".INP", outf = name + ".OUT";
    if (fs::exists(settings_path)) {
      std::fstream binary(settings_path.string(), ios::in | ios::binary);
      binary.seekg(0, ios::end);
      size_t size = binary.tellg();
      binary.seekg(0, ios::beg);
      if (size > (1 << 24)) // 16MiB of testcases even if uncompressed?
        throw std::runtime_error(
            "Is this excessive for test cases? Possibly specifically crafted "
            "input. please re-check config");
      std::vector<char> data(size);
      binary.read(data.data(), size);
      uLongf destSize = size * 1032; // starting guess
      std::vector<char> processed(destSize);

      int rc;
      while ((rc = uncompress((Bytef *)processed.data(), &destSize,
                              (const Bytef *)data.data(), size)) ==
             Z_BUF_ERROR) {
        destSize *= 2;
        processed.resize(destSize);
      }
      if (rc == Z_OK) {
        PLOGD << settings_path.string() << " is ZLIB compressed";
        parseSettingsFormat(std::string_view(processed.data(), destSize),
                            testcases[name]);
      } else {
        PLOGD << settings_path.string()
              << " is not ZLIB compressed, error=" << rc;
        parseSettingsFormat(std::string_view(data.data(), data.size()),
                            testcases[name]);
      }
    } else {
      testcases[name].InputFile = name + ".INP";
      testcases[name].OutputFile = name + ".OUT";
      testcases[name].EvaluatorName =
#ifdef _WIN32
          "C1LinesWordsIgnoreCase.dll";
#else
          "libC1LinesWordsIgnoreCase.so";
#endif
      testcases[name].MemoryLimit = 1024;
      testcases[name].TimeLimit = 1.0;
      testcases[name].Mark = 1.0;
      for (auto &test : fs::directory_iterator(fd)) {
        // tests.path().relative_path().stem().string()
        // problem file i/o=name+".INP/OUT"
        if (!test.is_directory())
          continue;
        testcases[name].subtests.push_back(
            Subtest{test.path().relative_path().stem().string(), -1, -1, 1.0});
      }
    }
  }
  for (auto &user : fs::directory_iterator(subdir)) {
    if (!user.is_directory())
      continue;
    for (auto &problem : testcases) {
      judge(subdir, tdir, problem.first, user.path().stem().string(),
            globalInfo, testcases, judgers);
    }
  }
  auto print_stats = [&]() {
    auto scores = getScores();

    // -------------------------------
    // Collect users and problems
    // -------------------------------
    std::set<std::string> users;
    std::set<std::string> problems;

    for (const auto &[k, v] : scores) {
      users.insert(k.first);
      problems.insert(k.second);
    }

    // -------------------------------
    // Compute column widths
    // -------------------------------
    std::map<std::string, size_t> width;

    width["User/Problem"] = std::string("User/Problem").size();
    width["Total"] = std::string("Total").size();

    for (const auto &p : problems)
      width[p] = p.size();

    for (const auto &u : users) {
      width["User/Problem"] = std::max(width["User/Problem"], u.size());

      double total = 0.0;
      for (const auto &p : problems) {
        auto it = scores.find({u, p});
        double v = (it != scores.end()) ? it->second : 0.0;
        total += v;

        std::ostringstream oss;
        oss << v;
        width[p] = std::max(width[p], oss.str().size());
      }

      std::ostringstream oss;
      oss << total;
      width["Total"] = std::max(width["Total"], oss.str().size());
    }

    // -------------------------------
    // Printing helpers
    // -------------------------------
    auto print_text = [](const std::string &s, size_t w) {
      std::cout << std::left << std::setw(w) << s;
    };

    auto print_num = [](double v, size_t w) {
      std::ostringstream oss;
      oss << v;
      std::cout << std::right << std::setw(w) << oss.str();
    };

    // -------------------------------
    // Header
    // -------------------------------
    print_text("User/Problem", width["User/Problem"]);
    for (const auto &p : problems) {
      std::cout << " | ";
      print_text(p, width[p]);
    }
    std::cout << " | ";
    print_text("Total", width["Total"]);
    std::cout << "\n";

    // Separator
    size_t line = width["User/Problem"];
    for (const auto &p : problems)
      line += 3 + width[p];
    line += 3 + width["Total"];

    std::cout << std::string(line, '-') << "\n";

    // -------------------------------
    // Rows
    // -------------------------------
    for (const auto &u : users) {
      print_text(u, width["User/Problem"]);

      double total = 0.0;
      for (const auto &p : problems) {
        std::cout << " | ";
        auto it = scores.find({u, p});
        double v = (it != scores.end()) ? it->second : 0.0;
        total += v;
        print_num(v, width[p]);
      }

      std::cout << " | ";
      print_num(total, width["Total"]);
      std::cout << "\n";
    }
  };
  if (!waitSubmittorMode) {
    print_stats();
  } else {
    fn = []() {};
    print_stats();
    auto callback_judge = [&](fs::path path) -> void {
      judge(subdir, tdir, path.filename().stem().string(),
            path.parent_path().filename().string(), globalInfo, testcases,
            judgers);
      print_stats();
    };
    SubmissionWatcher watcher(subdir, callback_judge);
    watcher.start();
    PLOGI << "Watching...";
#ifdef _WIN32
    fn = [&]() -> void { watcher.stop(); };
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)SignalHandler, TRUE)) {
      PLOGD << "Failed to set handler";
    }
#else
    struct sigaction sa;
    fn = [&]() { watcher.stop(); };
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
      PLOGD << "Failed to set handler";
    }
#endif
    watcher.wait();
  }
}
