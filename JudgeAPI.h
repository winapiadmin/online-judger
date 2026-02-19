#pragma once
#if defined(_WIN32) && !defined(_WIN64)
#define STDCALL __stdcall
#else
#define STDCALL
#endif
// [0.0, 1.0]
// 0.0 means no points and 1.0 means full points
// in (0.0, 1.0): scaled
extern "C" double STDCALL JudgeAPIFunc(
    wchar_t *contestantsDir,
    wchar_t *testsDir,    // __In__
    wchar_t *testOutputs, // __In__
    wchar_t *testName,    // __In__
    wchar_t **comments    // __Out__, __Freed_by_callee__
);
// always UTF-8
extern "C" double STDCALL JudgeAPIFuncUTF8(
    char *contestantsDir,
    char *testsDir,    // __In__
    char *testOutputs, // __In__
    char *testName,    // __In__
    char **comments    // __Out__, __Freed_by_callee__
); // will be a port to the non-utf8 version on windows or a stub for _judge
using JudgeFn =
#ifdef _WIN32
    decltype(&JudgeAPIFunc);
#else
    decltype(&JudgeAPIFuncUTF8);
#endif
inline JudgeFn _judge = nullptr;
void Load(const char *path);