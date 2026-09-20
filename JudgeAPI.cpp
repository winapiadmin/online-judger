#include "JudgeAPI.h"
#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static std::wstring utf8_to_wide(const char *s) {
  if (!s)
    return {};

  int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (len <= 0)
    throw std::runtime_error("UTF-8 -> UTF-16 conversion failed");

  std::wstring w(len - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), len);
  return w;
}

static std::string describe_last_error() {
  DWORD code = GetLastError();
  LPWSTR buffer = nullptr;

  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, code, 0, (LPWSTR)&buffer, 0, NULL);

  std::string result;
  if (buffer) {
    int size = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL);
    result.resize(size > 0 ? size - 1 : 0);
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, result.data(), size, NULL,
                        NULL);
    LocalFree(buffer);
  }
  return result + " (code " + std::to_string(code) + ")";
}
#else
#include <cerrno>
#include <dlfcn.h>

static std::string describe_last_error() {
  int errnum = errno;
  return std::string(strerror(errnum)) + " (errno " + std::to_string(errnum) +
         ")";
}
#endif

namespace {
std::mutex g_mutex;
std::unordered_map<std::string, JudgeFn> g_cache;
} // namespace

JudgeFn Load(const char *path) {
  if (!path || !*path)
    throw std::runtime_error("Load(): null path");

  std::lock_guard<std::mutex> lock(g_mutex);
  auto cached = g_cache.find(path);
  if (cached != g_cache.end())
    return cached->second;

#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);

  // The module is intentionally kept loaded for the process lifetime; handles
  // are cached so each evaluator DLL is loaded at most once.
  HMODULE mod = LoadLibraryW(wpath.c_str());
  if (!mod)
    throw std::runtime_error(std::string("LoadLibraryW failed for ") + path +
                             ": " + describe_last_error());

  auto fn = reinterpret_cast<JudgeFn>(GetProcAddress(mod, "Judge"));
  if (!fn)
    throw std::runtime_error(std::string("GetProcAddress(Judge) failed for ") +
                             path + ": " + describe_last_error());
#else
  void *mod = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!mod)
    throw std::runtime_error(std::string("dlopen failed for ") + path + ": " +
                             dlerror());

  auto fn = reinterpret_cast<JudgeFn>(dlsym(mod, "Judge"));
  if (!fn) {
    const char *err = dlerror();
    throw std::runtime_error(std::string("dlsym(Judge) failed for ") + path +
                             ": " + (err ? err : "unknown error"));
  }
#endif

  g_cache.emplace(path, fn);
  return fn;
}

double STDCALL CallJudgeUTF8(JudgeFn fn, const char *contestantsDir,
                             const char *testsDir, const char *testOutputs,
                             const char *testName, std::string *comments) {
  if (!fn)
    throw std::runtime_error("CallJudgeUTF8(): no judge function loaded");

#if defined(_WIN32)
  std::wstring wContestantsDir = utf8_to_wide(contestantsDir);
  std::wstring wTestsDir = utf8_to_wide(testsDir);
  std::wstring wTestOutputs = utf8_to_wide(testOutputs ? testOutputs : "");
  std::wstring wTestName = utf8_to_wide(testName ? testName : "");

  wchar_t *wComments = nullptr;

  // Always pass valid pointers; third-party judge DLLs do not check for null.
  double result = fn(const_cast<wchar_t *>(wContestantsDir.c_str()),
                     const_cast<wchar_t *>(wTestsDir.c_str()),
                     const_cast<wchar_t *>(wTestOutputs.c_str()),
                     const_cast<wchar_t *>(wTestName.c_str()), &wComments);

  if (comments) {
    comments->clear();
    if (wComments) {
      int len = WideCharToMultiByte(CP_UTF8, 0, wComments, -1, nullptr, 0,
                                    nullptr, nullptr);
      if (len > 1) {
        comments->resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, wComments, -1, comments->data(), len,
                            nullptr, nullptr);
      }
    }
  }
  /* The comment buffer is freed here once per call so it cannot accumulate
     across a whole contest. This requires judges to allocate it with
     malloc/calloc from the process-shared CRT (true for every evaluator
     built by this project; see JudgeAPI.h). */
  free(wComments);
  return result;
#else
  char *cComments = nullptr;
  double result =
      fn(const_cast<char *>(contestantsDir), const_cast<char *>(testsDir),
         const_cast<char *>(testOutputs ? testOutputs : ""),
         const_cast<char *>(testName ? testName : ""), &cComments);
  if (comments) {
    comments->clear();
    if (cComments)
      *comments = cComments;
  }
  free(cComments); /* same rationale as above */
  return result;
#endif
}
