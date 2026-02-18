#include <stdexcept>
#include <string>
#include <cstring>
#include <filesystem>
#include "JudgeAPI.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static std::wstring utf8_to_wide(const char* s)
{
    if (!s) return {};

    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0)
        throw std::runtime_error("UTF-8 → UTF-16 conversion failed");

    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), len);
    return w;
}

static char* wide_to_utf8_alloc(const wchar_t* ws)
{
    if (!ws) return nullptr;

    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return nullptr;

    char* out = static_cast<char*>(std::malloc(len));
    if (!out) return nullptr;

    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, len, nullptr, nullptr);
    return out;
}
#else
#include <dlfcn.h>
#endif
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#define LINUX
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
std::string describe_last_error()
{
    int errnum=errno;
    return std::string(strerror(errnum)) + " (errno " + std::to_string(errnum) + ")";
}
#elif defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::string describe_last_error()
{
    DWORD code = GetLastError();
    LPWSTR buffer = nullptr;

    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        0,                  // Default language
        (LPWSTR)&buffer,
        0,
        NULL
    );

    std::string result;
    if (buffer) {
        int size = WideCharToMultiByte(
            CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL
        );
        result.resize(size - 1);
        WideCharToMultiByte(
            CP_UTF8, 0, buffer, -1,
            result.data(), size, NULL, NULL
        );
        LocalFree(buffer);
    }
    return result + " (code " + std::to_string(code) + ")";
}
#endif
void Load(const char* path)
{
    if (!path)
        throw std::runtime_error("Load(): null path");

#if defined(_WIN32)
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (len <= 0)
        throw std::runtime_error("UTF-8 to UTF-16 conversion failed: "+describe_last_error());

    std::wstring wpath(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), len);

    HMODULE mod = LoadLibraryW(wpath.c_str());
    if (!mod){
            
        DWORD code = GetLastError();
        LPWSTR buffer = nullptr;
        DWORD_PTR args[]={(DWORD_PTR)wpath.data()};
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_ARGUMENT_ARRAY,
            NULL,
            code,
            0,                  // Default language
            (LPWSTR)&buffer,
            0,
            (va_list*)args
        );

        std::string result;
        if (buffer) {
            int size = WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL
            );
            result.resize(size - 1);
            WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1,
                result.data(), size, NULL, NULL
            );
            LocalFree(buffer);
        }
        result += " (code " + std::to_string(code) + ")";
        throw std::runtime_error("LoadLibraryW failed: "+result);
    }
    
    auto fn = reinterpret_cast<JudgeFn>(
        GetProcAddress(mod, "Judge")
    );

    if (!fn)
        throw std::runtime_error("GetProcAddress(Judge) failed: "+describe_last_error());

    _judge = fn;

#else
    // Linux / POSIX: UTF-8 is the native religion
    auto p_=std::filesystem::path(path);
    if (p_.extension()==".dll") p_.replace_extension(".so");
    if (std::string name = p_.filename().string(); !name.starts_with("lib"))
        p_ = p_.parent_path() / ("lib" + name);
    void* mod = dlopen(p_.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!mod)
        throw std::runtime_error(std::string(dlerror())+": "+describe_last_error());

    auto fn = reinterpret_cast<JudgeFn>(
        dlsym(mod, "Judge")
    );

    if (!fn)
        throw std::runtime_error(std::string(dlerror())+": "+describe_last_error());

    _judge = fn;

#endif
}
double STDCALL JudgeAPIFuncUTF8(
    char* contestantsDir,
    char* testsDir,
    char* testOutputs,
    char* testName,
    char** comments
){
    
#if defined(_WIN32)

    // Convert inputs
    std::wstring wCTestsDir    = utf8_to_wide(contestantsDir);
    std::wstring wTestsDir     = utf8_to_wide(testsDir);
    std::wstring wTestOutputs  = utf8_to_wide(testOutputs);
    std::wstring wTestName     = utf8_to_wide(testName);

    wchar_t* wComments = nullptr;

    double result = _judge(
        wCTestsDir.empty()   ? nullptr : wCTestsDir.data(),
        wTestsDir.empty()    ? nullptr : wTestsDir.data(),
        wTestOutputs.empty() ? nullptr : wTestOutputs.data(),
        wTestName.empty()    ? nullptr : wTestName.data(),
        &wComments
    );

    // Convert output comment back to UTF-8
    if (comments)
        *comments = wide_to_utf8_alloc(wComments);
    return result;
#else
    return _judge(contestantsDir, testsDir, testOutputs, testName, comments);
#endif
}
