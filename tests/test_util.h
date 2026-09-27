// Shared helpers for the evaluator unit tests.
//
// Each test_<evaluator>.c links its evaluator source directly and drives the
// exported Judge() entry point against fixture files in a scratch directory.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
typedef wchar_t str_t;
#define TS(x) L##x
static int test_pid(void) { return (int)GetCurrentProcessId(); }
#else
#include <sys/stat.h>
#include <unistd.h>
typedef char str_t;
#define TS(x) x
static int test_pid(void) { return (int)getpid(); }
#endif

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                     \
    }                                                                          \
  } while (0)

#ifdef _WIN32
static str_t *utf8_to_str_t(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  str_t *w = (str_t *)malloc((size_t)n * sizeof(str_t));
  if (!w)
    return NULL;
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static void str_t_to_utf8(const str_t *ws, char *out, size_t cap) {
  int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
  if (n <= 0 || (size_t)n >= cap) {
    if (cap)
      out[0] = 0;
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, n, NULL, NULL);
}
#else
static str_t *utf8_to_str_t(const char *s) { return strdup(s); }
static void str_t_to_utf8(const str_t *ws, char *out, size_t cap) {
  snprintf(out, cap, "%s", ws);
}
#endif

// Evaluator entry point (linked from the evaluator's own translation unit).
extern double Judge(str_t *contestantsDir, str_t *testsDir, str_t *testOutputs,
                    str_t *testName, str_t **comments_out);

static char g_tests_dir[1100];
static char g_work_dir[1100];
static char g_base[1024];

static void mkdir_one(const char *p) {
#ifdef _WIN32
  CreateDirectoryA(p, NULL);
#else
  mkdir(p, 0777);
#endif
}

// Creates <tmp>/oj_eval_tests_<pid>/tests and .../work.
static void setup_dirs(void) {
  const char *tmp = getenv("TEMP");
  if (!tmp)
    tmp = getenv("TMP");
  if (!tmp)
    tmp = getenv("TMPDIR");
  if (!tmp)
    tmp = "/tmp";

  snprintf(g_base, sizeof(g_base), "%s/oj_eval_tests_%d", tmp, test_pid());
  mkdir_one(g_base);

  snprintf(g_tests_dir, sizeof(g_tests_dir), "%s/tests", g_base);
  snprintf(g_work_dir, sizeof(g_work_dir), "%s/work", g_base);
  mkdir_one(g_tests_dir);
  mkdir_one(g_work_dir);
}

static void write_bytes(const char *dir, const char *name, const char *data,
                        size_t len) {
  char path[1200];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    printf("FAIL cannot write %s\n", path);
    ++g_failures;
    return;
  }
  fwrite(data, 1, len, f);
  fclose(f);
}

static void remove_tree_best_effort(const char *dir) {
#ifdef _WIN32
  char cmd[2400];
  snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" >nul 2>&1", dir);
  system(cmd);
#else
  char cmd[2400];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
  system(cmd);
#endif
}

// Runs Judge(exp=A.OUT in tests_dir vs act=A.OUT in work_dir) and hands back
// the comment text as UTF-8.
static double judge_a_out(char *comments_utf8, size_t cap) {
  str_t *a = utf8_to_str_t(g_work_dir);
  str_t *b = utf8_to_str_t(g_tests_dir);
  str_t *o = utf8_to_str_t("A.OUT");
  str_t *n = utf8_to_str_t("A");
  str_t *comments = NULL;
  double score = Judge(a, b, o, n, &comments);
  if (comments_utf8 && cap) {
    comments_utf8[0] = 0;
    if (comments)
      str_t_to_utf8(comments, comments_utf8, cap);
  }
#ifdef _WIN32
  free(comments); /* host-side copy of a shared-CRT allocation */
#endif
  free(a);
  free(b);
  free(o);
  free(n);
  return score;
}
