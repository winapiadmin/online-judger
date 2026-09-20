#include "JudgeBackend.h"
#include "JudgeAPI.h"
#include "ProcessIO.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <plog/Log.h>
#include <random>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace std;
namespace fs = std::filesystem;

namespace {
std::mutex g_scores_mtx;
std::map<std::pair<string, string>, std::pair<std::string, double>> g_scores;
std::atomic<int> g_idx{0};

// One mutex per evaluator path: judge DLLs are not guaranteed thread-safe,
// so the same library is never invoked concurrently, while different
// problems' evaluators can still run in parallel.
std::mutex &evaluator_mutex(const string &key) {
  static std::mutex map_mtx;
  static unordered_map<string, std::shared_ptr<std::mutex>> map;
  lock_guard<mutex> map_lock(map_mtx);
  auto &entry = map[key];
  if (!entry)
    entry = std::make_shared<std::mutex>();
  return *entry;
}
} // namespace

static void set_score(const string &user, const string &problem,
                      const string &verdict, double points) {
  lock_guard<mutex> lock(g_scores_mtx);
  g_scores[{user, problem}] = {verdict, points};
}

// Function to generate a random string of a specified length
string random_string(size_t length) {
  const string characters = "0123456789"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz";

  random_device random_device;
  mt19937 generator(random_device());

  uniform_int_distribution<size_t> distribution(0, characters.length() - 1);

  string random_string;
  random_string.reserve(length);

  for (size_t i = 0; i < length; ++i) {
    random_string += characters[distribution(generator)];
  }

  return random_string;
}

vector<string> split_args_quoted(const string &s) {
  vector<string> out;
  string cur;
  bool in_quote = false;

  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"') {
      in_quote = !in_quote;
    } else if (isspace((unsigned char)c) && !in_quote) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty())
    out.push_back(cur);

  return out;
}

optional<CompilerItem> find_compiler(const vector<CompilerItem> &items,
                                     const string &ext) {
  for (const auto &it : items) {
    if (iequals(it.ext, ext))
      return it;
  }
  return nullopt;
}

optional<fs::path> find_source_file(const fs::path &submissionDir,
                                    std::string problem,
                                    const vector<CompilerItem> &items) {
  if (!fs::is_directory(submissionDir))
    return nullopt;

  for (const auto &entry : fs::directory_iterator(submissionDir)) {
    if (!entry.is_regular_file())
      continue;
    auto name = entry.path().stem().string(),
         ext = entry.path().extension().string();
    if (find_compiler(items, ext) != nullopt && iequals(name, problem))
      return entry.path();
  }
  return nullopt;
}

optional<fs::path> find_executable(const fs::path &workdir) {
  for (const auto &entry : fs::directory_iterator(workdir)) {
    if (!entry.is_regular_file())
      continue;

#ifdef _WIN32
    DWORD a;
    if (GetBinaryTypeW(entry.path().wstring().c_str(), &a))
      return entry.path();
#else
    auto perms = entry.status().permissions();
    if ((perms & fs::perms::owner_exec) != fs::perms::none)
      return entry.path();
#endif
  }
  return nullopt;
}

bool parse_compiler_cmd(const std::string &cmd, std::string &rawCmd,
                        std::string &rawWorkdir) {
  auto sep = cmd.find('|');
  if (sep == std::string::npos)
    return false;

  rawCmd = cmd.substr(0, sep);
  std::string tail = cmd.substr(sep + 1);

  constexpr std::string_view key = "@WorkDir=";
  if (!tail.starts_with(key))
    return false;

  rawWorkdir = tail.substr(key.size());
  if (rawWorkdir.empty())
    return false;

  return true;
}

std::string load_file_to_string(const fs::path &filename) {
  std::ifstream file(fs::canonical(filename).string(), std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filename.string());
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  return content;
}

// Timestamp prefix so history files from different runs never collide.
// Millisecond precision matters: several short-lived processes can judge
// within the same second, and the per-process counter restarts at 1.
// Lexicographic order == chronological order.
static string history_stamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  char frac[8];
  snprintf(frac, sizeof(frac), ".%03d", (int)ms.count());
  return string(stamp) + frac;
}

void judge(fs::path subdir, fs::path tdir, string problem, string user,
           const Configuration &conf,
           const unordered_map<string, Testcases> &testcases,
           const fs::path &judger_path) {
  string fn = history_stamp() + "-" + std::to_string(++g_idx) + "[" + user +
              "][" + problem + "].txt";
  fs::create_directory(subdir / "$History");
  ofstream out(subdir / "$History" / fn);
  if (!out.is_open()) {
    PLOGE << subdir / "$History" / fn << " (" << strerror(errno) << ")";
    return;
  }
// WARNING: NotImplemented multi input/output files
#define _LOG(sev, msg)                                                         \
  {                                                                            \
    PLOG(sev) << msg;                                                          \
    out << msg << '\n';                                                        \
  }
  auto it = testcases.find(problem);
  if (it == testcases.end()) {
    PLOGE << problem << " doesn't have tests!";
    return;
  }
  const Testcases &tests = it->second;

  fs::path sourceDir = subdir / user;
  if (!fs::is_directory(sourceDir)) {
    PLOGE << sourceDir << " is not a directory";
    return;
  }

  auto sourceFile = find_source_file(sourceDir, problem, conf.compiler.items);
  if (!sourceFile) {
    _LOG(plog::info,
         "[" << user << "/" << problem << "] source file not found");
    set_score(user, problem, "-", 0.0);
    return;
  }

  string ext = sourceFile->extension().string();
  string name = sourceFile->filename().stem().string();
  string path = sourceFile->string();
  auto compiler = find_compiler(conf.compiler.items, ext);
  if (!compiler) {
    _LOG(plog::error,
         "[" << user << "/" << problem << "] no compiler for " << ext);
    return;
  }

  string rawCmd, rawWorkDir;
  if (!parse_compiler_cmd(compiler->cmd, rawCmd, rawWorkDir)) {
    PLOGE << "[" << user << "/" << problem << "] malformed compiler command ("
          << compiler->cmd << ')';
    return;
  }

  fs::path workdir = expand_percent_vars(
      rawWorkDir, {{"PATH", (fs::path(conf.environment.contestHouse) /
                             "judgeWORK" / random_string(16))
                                .string()}});

  fs::create_directories(workdir);
  // Removes the scratch dir on every exit path (early returns included).
  // JUDGER_KEEP_WORKDIR disables this for debugging.
  struct WorkdirCleanup {
    const fs::path &p;
    ~WorkdirCleanup() {
      if (std::getenv("JUDGER_KEEP_WORKDIR"))
        return;
      std::error_code ec;
      fs::remove_all(p, ec);
    }
  } workdirCleanup{workdir};
  fs::copy_file(*sourceFile, workdir / sourceFile->filename(),
                fs::copy_options::overwrite_existing);

  string expandedCmd = expand_percent_vars(
      rawCmd, {{"NAME", name}, {"EXT", ext}, {"PATH", path}});

  PLOGD << "[" << user << "/" << problem << "] compiling with: [" << expandedCmd
        << "] at [" << workdir << "]";

  // Compile the code (5-minute cap; slow toolchains / cold AV scans happen)
  constexpr float kCompileTimeoutSec = 300.0f;
  ProcessResult compileInfo;
  try {
    compileInfo = run_command(split_args_quoted(expandedCmd), workdir, "",
                              kCompileTimeoutSec);
  } catch (const CPError<CPErrors::TLE> &) {
    _LOG(plog::error, "[" << user << "/" << problem
                          << "] compilation timed out ("
                          << (int)kCompileTimeoutSec << "s)");
    set_score(user, problem, "X", 0.0);
    return;
  } catch (const CPErrorBase &) {
    // spawn/pipe failures must still yield a verdict, not a blank cell
    _LOG(plog::error,
         "[" << user << "/" << problem << "] internal error while compiling");
    set_score(user, problem, "X", 0.0);
    return;
  }
  if (compileInfo.exit_code != 0) {
    _LOG(plog::error, "[" << user << "/" << problem << "] Compiling failed");
    _LOG(plog::error, "stderr:\n" << compileInfo.stderr_data);
    _LOG(plog::error, "stdout:\n" << compileInfo.stdout_data);
    set_score(user, problem, "X", 0.0);
    return;
  }

  // Find the compiled executable
  auto exe = find_executable(workdir);
  if (!exe) {
    _LOG(plog::error,
         "[" << user << "/" << problem << "] executable not found");
    return;
  }

  _LOG(plog::info,
       "[" << user << "/" << problem << "] compiled successfully at " << *exe);

  // Load evaluator (cached by path; safe to call from any thread)
  JudgeFn judgeFn = nullptr;
  string evaluatorKey;
  try {
    evaluatorKey = fs::canonical(judger_path / tests.EvaluatorName).string();
    judgeFn = Load(evaluatorKey.c_str());
    PLOGI << "[" << user << "/" << problem << "] loaded evaluator successfully";
  } catch (const std::exception &e) {
    _LOG(plog::error, "[" << user << "/" << problem
                          << "] failed to load evaluator: " << e.what());
    set_score(user, problem, "X", 0.0);
    return;
  }

  double points = 0.0;
  for (auto &tc : tests.subtests) {
    float timeLimit = tc.TimeLimit == -1 ? tests.TimeLimit : tc.TimeLimit;
    float memoryLimit =
        tc.MemoryLimit == -1 ? tests.MemoryLimit : tc.MemoryLimit;

    fs::remove(workdir / tests.InputFile);
    fs::remove(workdir / tests.OutputFile);

    PLOGI << "[" << user << "/" << problem << "/" << tc.Name << "] judging...";

    try {
      if (tests.UseStdIn) {
        std::string input =
            load_file_to_string(tdir / problem / tc.Name / tests.InputFile);
        ProcessResult result =
            run_command({fs::canonical(*exe).string()}, workdir, input,
                        timeLimit, memoryLimit);
        if (result.exit_code != 0)
          throw CPError<CPErrors::IR>(result.exit_code);
        if (result.time > timeLimit)
          throw CPError<CPErrors::TLE>();

        _LOG(plog::info, "Time ~" << result.time << " seconds");

        if (tests.UseStdOut) {
          // Contestant stdout belongs in the contestant's own workdir; it must
          // never be written into the test data directory.
          ofstream output(workdir / tests.OutputFile, ios::binary | ios::trunc);
          output << result.stdout_data;
        }
      } else {
        fs::copy_file(tdir / problem / tc.Name / tests.InputFile,
                      workdir / tests.InputFile,
                      fs::copy_options::overwrite_existing);

        ProcessResult result = run_command({fs::canonical(*exe).string()},
                                           workdir, "", timeLimit, memoryLimit);
        if (result.exit_code != 0)
          throw CPError<CPErrors::IR>(result.exit_code);
        if (result.time > timeLimit)
          throw CPError<CPErrors::TLE>();

        _LOG(plog::info, "Time ~" << result.time << " seconds");

        if (tests.UseStdOut) {
          ofstream output(workdir / tests.OutputFile, ios::binary | ios::trunc);
          output << result.stdout_data;
        }
      }
      std::string comments;
      double _points;
      {
        std::lock_guard<std::mutex> eval_lock(evaluator_mutex(evaluatorKey));
        _points = CallJudgeUTF8(
                      judgeFn, fs::canonical(workdir).string().c_str(),
                      (tdir / problem / tc.Name).string().c_str(),
                      tests.OutputFile.c_str(), problem.c_str(), &comments) *
                  (tc.Mark == -1 ? tests.Mark : tc.Mark);
      }

      _LOG(plog::info, "[" << user << "/" << problem << "/" << tc.Name
                           << "]: " << _points << '\n'
                           << comments);
      points += _points;
    } catch (CPError<CPErrors::TLE> &e) {
      _LOG(plog::error, "[" << user << "/" << problem << "] TLEd " << tc.Name);
    } catch (CPError<CPErrors::IR> &e) {
      _LOG(plog::error, "[" << user << "/" << problem << "] exited with code 0x"
                            << std::hex << e.exit_code << std::dec);
    } catch (std::exception &e) {
      _LOG(plog::error,
           "[" << user << "/" << problem << "] critical error: " << e.what());
    }
  }

  _LOG(plog::info, "[" << user << "/" << problem << "]: " << points);
#undef _LOG
  out.close();
  set_score(user, problem, "V", points);
}

std::map<std::pair<string, string>, std::pair<std::string, double>>
getScores() {
  lock_guard<mutex> lock(g_scores_mtx);
  return g_scores;
}
