#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <charconv>
#include <filesystem>

namespace fs = std::filesystem;
std::string expand_percent_vars(
    std::string_view input,
    const std::unordered_map<std::string, std::string>& vars);

struct ProcessResult {
    std::string stdout_data;
    std::string stderr_data;
    uint32_t exit_code;
    float time;
};
// takes input as-is, e.g. "g++ %PATH%" will run "g++ %PATH%" without the expand_percent_vars
ProcessResult run_command(
    const std::vector<std::string>& command,
    const fs::path& cwd,
    const std::string& stdin_data = "",
    const float time=1.0,
    const int maxMemory=1024);
enum class CPErrors{
    TLE,
    OLE,
    IR,
    IE,
    MLE
};

class CPErrorBase : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    CPErrorBase()
        : std::runtime_error("CP error") {}
};

template <CPErrors E>
class CPError : public CPErrorBase {
public:
    CPError()
        : CPErrorBase(message()) {}

private:
    static const char* message() {
        if constexpr (E == CPErrors::TLE) return "Time limit exceeded";
        else if constexpr (E == CPErrors::OLE) return "Output limit exceeded";
        else if constexpr (E == CPErrors::IR)  return "Invalid result";
        else if constexpr (E == CPErrors::MLE)  return "Program exceeded memory usage";
        else return "Internal error";
    }
};

template <>
class CPError<CPErrors::IE> : public CPErrorBase {
public:
    CPError(std::string error)
        : CPErrorBase("Internal error: "+error) {}
};

template <>
class CPError<CPErrors::IR> : public CPErrorBase {
public:
    CPError(uint32_t error){
        std::string s;
        s.resize(8);  // max hex digits for 32-bit unsigned

        auto [ptr, ec] = std::to_chars(s.data(), s.data() + s.size(), error, 16);
        CPErrorBase("Invalid return: "+s);
    }
};
