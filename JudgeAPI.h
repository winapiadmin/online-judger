#pragma once
#include <string>
#if defined(_WIN32) && !defined(_WIN64)
#define STDCALL __stdcall
#else
#define STDCALL
#endif
// Judge DLL ABI (Themis-compatible):
//   double __stdcall Judge(wchar_t *a, wchar_t *b, wchar_t *c, wchar_t *d,
//                          wchar_t **e);
// a: contestant working dir for one test
// b: test data dir
// c: '|' separated list of result files
// d: problem name
// e: [out] NUL-terminated comment string allocated by the judge DLL
// return: score in [0.0, 1.0]
#ifdef _WIN32
using JudgeFn = double(STDCALL *)(wchar_t *, wchar_t *, wchar_t *, wchar_t *,
                                  wchar_t **);
#else
using JudgeFn = double (*)(char *, char *, char *, char *, char **);
#endif

// Loads (and caches) a judge library. Thread-safe; repeated calls with the
// same path return the cached function without reloading.
// Throws std::runtime_error on failure.
JudgeFn Load(const char *path);

// UTF-8 convenience wrapper around a loaded Judge function.
// Converts inputs to wchar_t on Windows, converts the comment output back to
// UTF-8. The comment buffer is owned by the judge DLL; callers must not free
// it. comments may be nullptr.
double STDCALL CallJudgeUTF8(JudgeFn fn, const char *contestantsDir,
                             const char *testsDir, const char *testOutputs,
                             const char *testName, std::string *comments);
