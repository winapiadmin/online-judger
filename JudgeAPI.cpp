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
#include <dlfcn.h>
#endif

namespace {
std::mutex g_mutex;
struct LoadedJudge {
  JudgeFn fn = nullptr;
  // Optional per-library deallocator for comment buffers (see
  // FreeJudgeComment below); null when the library does not export one.
  FreeJudgeCommentFn freeFn = nullptr;
};
std::unordered_map<std::string, LoadedJudge> g_cache;
} // namespace

#if defined(_WIN32)
// 32-bit __stdcall decorates exports (_Judge@20); x64 does not. Try both.
template <typename T>
static T getProc(HMODULE mod, const char *plain, const char *decorated) {
  auto p = reinterpret_cast<T>(GetProcAddress(mod, plain));
  if (!p && decorated)
    p = reinterpret_cast<T>(GetProcAddress(mod, decorated));
  return p;
}
#endif

JudgeFn Load(const char *path) {
  if (!path || !*path)
    throw std::runtime_error("Load(): null path");

  std::lock_guard<std::mutex> lock(g_mutex);
  auto cached = g_cache.find(path);
  if (cached != g_cache.end())
    return cached->second.fn;

#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);

  // The module is intentionally kept loaded for the process lifetime; handles
  // are cached so each evaluator DLL is loaded at most once.
  HMODULE mod = LoadLibraryW(wpath.c_str());
  if (!mod)
    throw std::runtime_error(std::string("LoadLibraryW failed for ") + path +
                             ": " + describe_last_error());

  auto fn = getProc<JudgeFn>(mod, "Judge", "_Judge@20");
  if (!fn)
    throw std::runtime_error(std::string("GetProcAddress(Judge) failed for ") +
                             path + ": " + describe_last_error());
  auto freeFn = getProc<FreeJudgeCommentFn>(mod, "FreeJudgeComment",
                                            "_FreeJudgeComment@4");
#else
  void *mod = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!mod)
    throw std::runtime_error(std::string("dlopen failed for ") + path + ": " +
                             dlerror());

  dlerror(); // clear stale errors before dlsym
  auto fn = reinterpret_cast<JudgeFn>(dlsym(mod, "Judge"));
  if (!fn) {
    const char *err = dlerror();
    throw std::runtime_error(std::string("dlsym(Judge) failed for ") + path +
                             ": " + (err ? err : "unknown error"));
  }
  dlerror();
  auto freeFn =
      reinterpret_cast<FreeJudgeCommentFn>(dlsym(mod, "FreeJudgeComment"));
#endif

  g_cache.emplace(path, LoadedJudge{fn, freeFn});
  return fn;
}

void FreeJudgeComment(const char *path, void *comment) {
  if (!comment)
    return;
  FreeJudgeCommentFn freeFn = nullptr;
  if (path && *path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(path);
    if (it != g_cache.end())
      freeFn = it->second.freeFn;
  }
  if (freeFn)
    freeFn(comment);
  else
    free(comment); // legacy evaluator without the export: shared-CRT malloc
}

// Find the deallocator for a loaded Judge function (null when the library
// exports no FreeJudgeComment or the function is unknown to the cache).
static FreeJudgeCommentFn freeFor(JudgeFn fn) {
  if (!fn)
    return nullptr;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const auto &[path, loaded] : g_cache)
    if (loaded.fn == fn)
      return loaded.freeFn;
  return nullptr;
}

double STDCALL CallJudgeUTF8(JudgeFn fn, const char *contestantsDir,
                             const char *testsDir, const char *testOutputs,
                             const char *testName, std::string *comments) {
  if (!fn)
    throw std::runtime_error("CallJudgeUTF8(): no judge function loaded");

#if defined(_WIN32)
  // Mutable copies: the ABI takes non-const pointers, so a writing callee
  // must land in our own buffer, never in shared/immutable storage.
  std::wstring wContestantsDir = utf8_to_wide(contestantsDir);
  std::wstring wTestsDir = utf8_to_wide(testsDir);
  std::wstring wTestOutputs = utf8_to_wide(testOutputs ? testOutputs : "");
  std::wstring wTestName = utf8_to_wide(testName ? testName : "");

  wchar_t *wComments = nullptr;

  // Always pass valid pointers; third-party judge DLLs do not check for null.
  double result = fn(wContestantsDir.data(), wTestsDir.data(),
                     wTestOutputs.data(), wTestName.data(), &wComments);

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
     across a whole contest. Prefer the library's own deallocator (see
     FreeJudgeComment); fall back to free() for legacy evaluators that
     allocate with malloc/calloc from the process-shared CRT. */
  {
    FreeJudgeCommentFn ff = freeFor(fn);
    if (ff)
      ff(wComments);
    else
      free(wComments);
  }
  return result;
#else
  // Mutable copies for the same reason as above; the old code handed the
  // callee the caller's own (possibly read-only) bytes.
  std::string cContestantsDir(contestantsDir ? contestantsDir : "");
  std::string cTestsDir(testsDir ? testsDir : "");
  std::string cTestOutputs(testOutputs ? testOutputs : "");
  std::string cTestName(testName ? testName : "");
  char *cComments = nullptr;
  double result = fn(cContestantsDir.data(), cTestsDir.data(),
                     cTestOutputs.data(), cTestName.data(), &cComments);
  if (comments) {
    comments->clear();
    if (cComments)
      *comments = cComments;
  }
  {
    FreeJudgeCommentFn ff = freeFor(fn);
    if (ff)
      ff(cComments);
    else
      free(cComments); /* same rationale as above */
  }
  return result;
#endif
}
