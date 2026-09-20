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
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>
std::function<void()> fn;
#include "Base.h"
#include "JudgeBackend.h"
#include "Parsers.h"
#include "SubmissionWatcher.h"
#include "common/AppConfig.h"
using namespace std;
namespace fs = filesystem;
plog::ColorConsoleAppender<plog::TxtFormatter> appender;
#ifdef _WIN32
BOOL WINAPI SignalHandler(DWORD) {
  fn();
  return TRUE;
}
#else
static volatile sig_atomic_t g_sigint_flag = 0;
void SignalHandler(int v) {
  if (v == SIGINT)
    g_sigint_flag = 1; /* async-signal-safe: set a flag only */
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

static std::string load_maybe_zlib(const fs::path &p) {
  std::ifstream binary(p, ios::in | ios::binary);
  if (!binary.is_open())
    throw std::runtime_error("Failed to open file: " + p.string());

  std::vector<char> data((std::istreambuf_iterator<char>(binary)),
                         std::istreambuf_iterator<char>());
  constexpr size_t kMaxRaw = 1 << 24;
  constexpr size_t kMaxInflated = 128 << 20;
  if (data.size() > kMaxRaw)
    throw std::runtime_error("Possibly crafted input; re-check config");
  if (data.empty())
    return {};

  uLongf destSize = compressBound((uLong)data.size());
  std::vector<char> processed(destSize);

  int rc;
  while ((rc = uncompress((Bytef *)processed.data(), &destSize,
                          (const Bytef *)data.data(), (uLong)data.size())) ==
         Z_BUF_ERROR) {
    if (processed.size() >= kMaxInflated)
      throw std::runtime_error("Decompressed size exceeds limit (zip bomb?)");
    destSize = processed.size() * 2;
    processed.resize(destSize);
  }
  if (rc == Z_OK) {
    PLOGD << p.string() << " is ZLIB compressed";
    return std::string(processed.data(), destSize);
  }
  PLOGD << p.string() << " is not ZLIB compressed, error=" << rc;
  return std::string(data.data(), data.size());
}

int main(int argc, char **argv) {
#ifdef _WIN32
  // Set output code page to UTF-8
  SetConsoleOutputCP(CP_UTF8);
#endif
  std::set_terminate(termination);
  plog::init(plog::verbose, &appender);
  fs::path subdir, tdir, compfile, judgers = "judgers";
  bool waitSubmittorMode = false;
  int jobs = 0; // 0 = auto (hardware concurrency)
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
  mode->add_option("-n,--jobs", jobs, "Parallel judge threads (0 = auto)")
      ->check(CLI::NonNegativeNumber);

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

  AppConfig appConfig;
  Configuration &globalInfo = appConfig.configuration();
  if (!compfile.empty()) {
    // -c flag: full format detection (YAML/JSON/XML/TOML) via parsers
    std::string content = load_maybe_zlib(compfile);
    parseGlobalSettingsFormat(content, globalInfo);
  } else {
    // No -c flag: use AppConfig (Themis XML → our JSON → defaults)
    appConfig.load();
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
      std::string content = load_maybe_zlib(settings_path);
      parseSettingsFormat(content, testcases[name]);
      fs::path f = testcases[name].EvaluatorName;
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
      testcases[name].EvaluatorName = f.string();
    } else {
      testcases[name].InputFile = name + ".INP";
      testcases[name].OutputFile = name + ".OUT";
      testcases[name].EvaluatorName =
#ifdef _WIN32
#ifdef __MSYS__
          "lib"
#endif
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
  // Collect (user, problem) jobs, then judge them on a worker pool
  struct Job {
    std::string user;
    std::string problem;
  };
  std::vector<Job> jobList;
  for (auto &user : fs::directory_iterator(subdir)) {
    if (!user.is_directory())
      continue;
    if (user.path().filename() == "$History")
      continue;
    for (auto &problem : testcases)
      jobList.push_back({user.path().stem().string(), problem.first});
  }

  unsigned maxThreads = jobs < 0 ? 0 : (unsigned)jobs;
  if (maxThreads == 0) {
    maxThreads = std::thread::hardware_concurrency();
    if (maxThreads == 0)
      maxThreads = 1;
  }
  size_t threadCount =
      std::min<size_t>(maxThreads, std::max<size_t>(jobList.size(), 1));

  PLOGI << "Judging " << jobList.size() << " submission(s) with " << threadCount
        << " thread(s)";

  std::atomic<size_t> nextJob{0};
  auto worker = [&]() -> void {
    for (;;) {
      size_t i = nextJob++;
      if (i >= jobList.size())
        return;
      const auto &[u, p] = jobList[i];
      try {
        judge(subdir, tdir, p, u, globalInfo, testcases, judgers);
      } catch (const std::exception &e) {
        PLOGE << "judge(" << u << "/" << p << ") crashed: " << e.what();
      } catch (...) {
        PLOGE << "judge(" << u << "/" << p << ") crashed: unknown exception";
      }
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(threadCount);
  try {
    for (size_t i = 0; i < threadCount; ++i)
      pool.emplace_back(worker);
  } catch (...) {
    for (auto &t : pool)
      if (t.joinable())
        t.join();
    throw;
  }
  for (auto &t : pool)
    t.join();
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
        auto v = (it != scores.end()) ? it->second : std::make_pair("", 0.0);
        total += v.second;

        std::ostringstream oss;
        oss << v.first << " " << v.second;
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

    auto print_num = [](double v, size_t w, string pp) {
      std::ostringstream oss;
      oss << pp << ' ' << v;
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
        auto v = (it != scores.end()) ? it->second : std::make_pair("", 0.0);
        total += v.second;
        print_num(v.second, width[p], v.first);
      }

      std::cout << " | ";
      print_num(total, width["Total"], "");
      std::cout << "\n";
    }
  };
  if (!waitSubmittorMode) {
    print_stats();
  } else {
    fn = []() {};
    print_stats();
    auto callback_judge = [&](fs::path path) -> void {
      // An escaping exception here would terminate the whole judger process.
      try {
        judge(subdir, tdir, path.filename().stem().string(),
              path.parent_path().filename().string(), globalInfo, testcases,
              judgers);
      } catch (const std::exception &e) {
        PLOGE << "judge(" << path.string() << ") crashed: " << e.what();
      } catch (...) {
        PLOGE << "judge(" << path.string() << ") crashed: unknown exception";
      }
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
    watcher.wait();
#else
    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
      PLOGD << "Failed to set handler";
    }
    /* The handler only sets a flag; stop() runs on this thread. */
    while (!g_sigint_flag)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.stop();
#endif
  }
}
