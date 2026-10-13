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
// a: contestant working dir for one test (mutable scratch copy; the callee
//    may modify the buffer but must not retain it)
// b: test data dir (same mutability note)
// c: '|' separated list of result files (same mutability note)
// d: problem name (same mutability note)
// e: [out] NUL-terminated comment string allocated by the judge DLL
// return: per-file score in [0.0, 1.0]; the total is the sum over all files
// listed in c (so multi-file outputs may exceed 1.0; callers clamp).
//
// Every evaluator must additionally export:
//   void __stdcall FreeJudgeComment(void *comment);
// which releases a buffer returned through e. Libraries that predate this
// contract (no such export) must allocate the comment with malloc/calloc
// from the process-shared CRT so the host's free() stays valid.
#ifdef _WIN32
using JudgeFn = double(STDCALL *)(wchar_t *, wchar_t *, wchar_t *, wchar_t *,
                                  wchar_t **);
using FreeJudgeCommentFn = void(STDCALL *)(void *);
#else
using JudgeFn = double (*)(char *, char *, char *, char *, char **);
using FreeJudgeCommentFn = void (*)(void *);
#endif

// Loads (and caches) a judge library. Thread-safe; repeated calls with the
// same path return the cached function without reloading.
// Throws std::runtime_error on failure.
JudgeFn Load(const char *path);

// Releases a comment buffer obtained from the library at path, using that
// library's FreeJudgeComment export when present, plain free() otherwise.
// Passing a null comment is a no-op.
void FreeJudgeComment(const char *path, void *comment);

// UTF-8 convenience wrapper around a loaded Judge function.
// Converts inputs to wchar_t on Windows, converts the comment output back to
// UTF-8. The comment buffer is freed before returning (see above); callers
// must not free it. comments may be nullptr.
double STDCALL CallJudgeUTF8(JudgeFn fn, const char *contestantsDir,
                             const char *testsDir, const char *testOutputs,
                             const char *testName, std::string *comments);
