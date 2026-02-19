#include "ProcessIO.h"
#include "JudgeAPI.h"
#include "JudgeBackend.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <optional>
#include <random>
#include <plog/Log.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace std;
namespace fs = std::filesystem;

// Function to generate a random string of a specified length
string random_string(size_t length) {
    const string characters =
        "0123456789"
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

vector<string> split_args_quoted(const string& s) {
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

optional<CompilerItem> find_compiler(
    const vector<CompilerItem>& items,
    const string& ext
) {
    for (const auto& it : items)
        if (it.ext == ext)
            return it;
    return nullopt;
}

optional<fs::path> find_source_file(
    const fs::path& submissionDir,
    const std::string& problem
){
    if (!fs::is_directory(submissionDir))
        return nullopt;

    for (const auto& entry : fs::directory_iterator(submissionDir)) {
        if (!entry.is_regular_file())
            continue;

        if (entry.path().stem().string() == problem)
            return entry.path();
    }
    return nullopt;
}

optional<fs::path> find_executable(const fs::path& workdir) {
    for (const auto& entry : fs::directory_iterator(workdir)) {
        if (!entry.is_regular_file())
            continue;

        #ifdef _WIN32
        DWORD a;
        if (GetBinaryTypeW(entry.path().wstring().c_str(), &a)) return entry.path();
        #else
        auto perms = entry.status().permissions();
        if ((perms & fs::perms::owner_exec) != fs::perms::none)
            return entry.path();
        #endif
    }
    return nullopt;
}

bool parse_compiler_cmd(
    const std::string& cmd,
    std::string &rawCmd,
    std::string &rawWorkdir
){
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

std::string load_file_to_string(const fs::path& filename) {
    std::ifstream file(fs::canonical(filename).string(), std::ios::binary); 

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename.string());
    }

    std::string content((std::istreambuf_iterator<char>(file)), 
                        std::istreambuf_iterator<char>());
    return content;
}

int idx = 0;
std::map<std::pair<string, string>, double> scores;

void judge(
    fs::path subdir,
    fs::path tdir,
    string problem,
    string user,
    const Configuration& conf,
    const unordered_map<string, Testcases>& testcases,
    fs::path& judger_path
) {
    string fn=std::to_string(++idx)+"["+user+"]["+problem+"].txt";
    fs::create_directory(subdir/"$History");
    ofstream out(subdir/"$History"/fn);
    if (!out.is_open()){
        PLOGE<< subdir/"$History"/fn<<" (" << strerror(errno) << ")";
        return;
    }
    // WARNING: NotImplemented multi input/output files
    #define _LOG(sev, msg) {PLOG(sev)<<msg; out<<msg<<'\n';}
    auto it = testcases.find(problem);
    if (it == testcases.end()) {
        PLOGE<< problem << " doesn't have tests!";
        return;
    }
    const Testcases& tests = it->second;

    fs::path sourceDir = subdir / user;
    if (!fs::is_directory(sourceDir)) {
        PLOGE<< sourceDir << " is not a directory";
        return;
    }

    auto sourceFile = find_source_file(sourceDir, problem);
    if (!sourceFile) {
        _LOG(plog::info, "[" << user << "/" << problem << "] source file not found");
        return;
    }

    string ext  = sourceFile->extension().string();
    string name = sourceFile->filename().stem().string();
    string path = sourceFile->string();

    auto compiler = find_compiler(conf.compiler.items, ext);
    if (!compiler) {
        _LOG(plog::error, "[" << user << "/" << problem << "] no compiler for " << ext);
        return;
    }

    string rawCmd, rawWorkDir;
    if (!parse_compiler_cmd(compiler->cmd, rawCmd, rawWorkDir)) {
        PLOGE << "[" << user << "/" << problem << "] malformed compiler command";
        return;
    }

    fs::path workdir = expand_percent_vars(
        rawWorkDir,
        {{"PATH", (fs::path(conf.environment.contestHouse) / "judgeWORK" / random_string(16)).string()}}
    );

    fs::create_directories(workdir);
    fs::copy_file(*sourceFile, workdir / sourceFile->filename(),
                  fs::copy_options::overwrite_existing);

    string expandedCmd = expand_percent_vars(
        rawCmd,
        {{"NAME", name}, {"EXT", ext}, {"PATH", path}}
    );

    PLOGD << "[" << user << "/" << problem << "] compiling with: [" << expandedCmd << "] at [" << workdir << "]";

    // Compile the code
    ProcessResult compileInfo;
    compileInfo = run_command(split_args_quoted(expandedCmd), workdir, "", 60.0);
    if (compileInfo.exit_code != 0) {
        _LOG(plog::error, "[" << user << "/" << problem << "] Compiling failed");
        _LOG(plog::error, "stderr:\n" << compileInfo.stderr_data);
        _LOG(plog::error, "stdout:\n" << compileInfo.stdout_data);
        return;
    }

    // Find the compiled executable
    auto exe = find_executable(workdir);
    if (!exe) {
        _LOG(plog::error, "[" << user << "/" << problem << "] executable not found");
        return;
    }

    _LOG(plog::info, "[" << user << "/" << problem << "] compiled successfully at " << *exe);

    // Load evaluator
    Load(fs::canonical(judger_path / tests.EvaluatorName).string().c_str());
    PLOGI << "[" << user << "/" << problem << "] loaded evaluator successfully";

    double points = 0.0;
    for (auto& tc : tests.subtests) {
        float timeLimit = tc.TimeLimit == -1 ? tests.TimeLimit : tc.TimeLimit;
        float memoryLimit = tc.MemoryLimit == -1 ? tests.MemoryLimit : tc.MemoryLimit;

        fs::remove(workdir / tests.InputFile);
        fs::remove(workdir / tests.OutputFile);

        PLOGI << "[" << user << "/" << problem << "/" << tc.Name << "] judging...";

        try {
            if (tests.UseStdIn) {
                std::string input = load_file_to_string(tdir / problem / tc.Name / tests.InputFile);
                ProcessResult result = run_command({fs::canonical(*exe).string()}, workdir, input, timeLimit, memoryLimit);
                if (result.exit_code != 0) throw CPError<CPErrors::IR>(result.exit_code);
                if (result.time > timeLimit) throw CPError<CPErrors::TLE>();

                _LOG(plog::info, "Time ~" << result.time << " seconds");

                if (tests.UseStdOut) {
                    ofstream output(tdir / problem / tc.Name / tests.OutputFile);
                    output << result.stdout_data;
                }
            }
            else {
                fs::copy_file(tdir / problem / tc.Name / tests.InputFile, workdir / tests.InputFile);
                if (!tests.UseStdOut) fs::copy_file(tdir / problem / tc.Name / tests.OutputFile, workdir / tests.OutputFile);

                ProcessResult result = run_command({fs::canonical(*exe).string()}, workdir, "", timeLimit, memoryLimit);
                if (result.exit_code != 0) throw CPError<CPErrors::IR>(result.exit_code);
                if (result.time > timeLimit) throw CPError<CPErrors::TLE>();

                _LOG(plog::info, "Time ~" << result.time << " seconds");

                if (tests.UseStdOut) {
                    ofstream output(tdir / problem / tc.Name / tests.OutputFile);
                    output << result.stdout_data;
                }
            }
            char* comments=nullptr;
            double _points = JudgeAPIFuncUTF8(fs::canonical(workdir).string().data(), (tdir / problem / tc.Name).string().data(), strdup(tests.OutputFile.data()), problem.data(), &comments)
                * (tc.Mark == -1 ? tests.Mark : tc.Mark);

            _LOG(plog::info, "[" << user << "/" << problem << "/" << tc.Name << "]: " << _points << '\n' << comments);
            free(comments);
            points += _points;
        }
        catch (CPError<CPErrors::TLE>& e) {
            _LOG(plog::error, "[" << user << "/" << problem << "] TLEd " << tc.Name);
        }
        catch (CPError<CPErrors::IR>& e) {
            _LOG(plog::error, "[" << user << "/" << problem << "] exited with code 0x" << std::hex << e.exit_code << std::dec);
        }
        catch (std::exception& e) {
            _LOG(plog::error, "[" << user << "/" << problem << "] critical error: " << e.what());
        }
    }

    _LOG(plog::info, "[" << user << "/" << problem << "]: " << points);
    #undef _LOG
    out.close();
    scores[std::make_pair(user, problem)] = points;
}

std::map<std::pair<string, string>, double> getScores() { return scores; }

