// judge.c
// =============================================================
//
// Themis "C5Binary" compatible judge:
// each result file listed in testOutputs is compared BYTE BY BYTE against the
// same-named file produced by the contestant's program. Works for any file
// type.
//
// Windows: wchar_t / UTF-16, wide APIs
// Others : char    / UTF-8, libc
//
// BUILD
// -----
// Windows (MSVC):
//   cl /LD judge.c
//
// Windows (MinGW):
//   x86_64-w64-mingw32-gcc -shared -o judge.dll judge.c
//
// Linux:
//   gcc -shared -fPIC judge.c -o libjudge.so
//
// macOS:
//   clang -shared -fPIC judge.c -o libjudge.dylib
//
// (or just pick it from CMake)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Diff details are emitted only by the judge.dll build. The C5Binary target
// compiles this file with -DC5_STRICT and stays Themis-spec compliant
// (verdict only, no extra output).
#ifndef C5_STRICT
struct binary_diff {
  long offset;         /* 0-based byte offset of first difference */
  int expected_byte;   /* value from the test's answer file (EOF at end) */
  int actual_byte;     /* value from the contestant's file (EOF at end) */
  long expected_size;
  long actual_size;
};
#endif

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Platform abstraction
// ------------------------------------------------------------
#ifdef _WIN32
#define DLL_EXPORT __declspec(dllexport)
#define API_CALL __cdecl

typedef wchar_t str;

#define STR_LIT(x) L##x
#define str_len wcslen
#define str_dup _wcsdup
#define str_tok wcstok_s
#define str_fopen(p, m) _wfopen(p, m)

#define PATH_SEP L'\\'

#else
#define DLL_EXPORT __attribute__((visibility("default")))
#define API_CALL

typedef char str;

#define STR_LIT(x) x
#define str_len strlen
#define str_dup strdup
#define str_tok strtok_r
#define str_cat_s(b, c, s)                                                                            \
    strncat(b, s, (size_t)((c) > strlen(b) + 1 ? (c) - strlen(b) - 1 : 0))
#define str_fopen(p, m) fopen(p, m)

#define PATH_SEP '/'

#include <unistd.h>
#endif

// ------------------------------------------------------------
// Utility: split "a|b|c"
// ------------------------------------------------------------
static str **str_split(const str *s, str delim) {
  if (!s)
    return NULL;

  str *tmp = str_dup(s);
  if (!tmp)
    return NULL;

  int count = 1;
  for (str *p = tmp; *p; ++p)
    if (*p == delim)
      count++;

  str **out = calloc((size_t)count + 1, sizeof(str *));
  if (!out) {
    free(tmp);
    return NULL;
  }

  str d[2] = {delim, 0};
  str *ctx = NULL;
  int i = 0;

  for (str *tok = str_tok(tmp, d, &ctx); tok; tok = str_tok(NULL, d, &ctx)) {
    out[i++] = str_dup(tok);
  }

  free(tmp);
  return out;
}

// ------------------------------------------------------------
// Binary comparison: byte by byte
// Returns 1 if identical, 0 if different, -1 on open error.
// When diff is non-NULL it receives diagnostic details about the first
// difference (used by the judge.dll build; the strict C5Binary build passes
// NULL).
// ------------------------------------------------------------
#ifndef C5_STRICT
static int compare_binary_files_ex(const str *f1, const str *f2,
                                   struct binary_diff *diff) {
  FILE *a = str_fopen(f1, STR_LIT("rb"));
  FILE *b = str_fopen(f2, STR_LIT("rb"));

  if (!a || !b) {
    if (a)
      fclose(a);
    if (b)
      fclose(b);
    return -1;
  }

  long len1 = 0, len2 = 0;
  if (diff) {
    long cur = ftell(a);
    fseek(a, 0, SEEK_END);
    len1 = ftell(a);
    fseek(a, cur, SEEK_SET);

    cur = ftell(b);
    fseek(b, 0, SEEK_END);
    len2 = ftell(b);
    fseek(b, cur, SEEK_SET);
  }

  int ca, cb;
  long offset = 0;
  do {
    ca = fgetc(a);
    cb = fgetc(b);
    if (ca != cb)
      break;
    ++offset;
  } while (ca != EOF && cb != EOF);

  fclose(a);
  fclose(b);

  if (ca == cb) {
    /* Both hit EOF on the same step => identical */
    return 1;
  }

  if (diff) {
    diff->offset = offset;
    diff->expected_byte = ca;
    diff->actual_byte = cb;
    diff->expected_size = len1;
    diff->actual_size = len2;
  }
  return 0;
}

static int compare_binary_files(const str *f1, const str *f2) {
  return compare_binary_files_ex(f1, f2, NULL);
}
#else
/* Strict Themis C5Binary: verdict only */
static int compare_binary_files(const str *f1, const str *f2) {
  FILE *a = str_fopen(f1, STR_LIT("rb"));
  FILE *b = str_fopen(f2, STR_LIT("rb"));

  if (!a || !b) {
    if (a)
      fclose(a);
    if (b)
      fclose(b);
    return -1;
  }

  int ca, cb;
  do {
    ca = fgetc(a);
    cb = fgetc(b);
  } while (ca != EOF && cb != EOF && ca == cb);

  fclose(a);
  fclose(b);

  /* Both hit EOF on the same step => identical */
  return ca == cb ? 1 : 0;
}
#endif /* C5_STRICT */

// ------------------------------------------------------------
// Path join
// ------------------------------------------------------------
static int join_path(str *out, size_t cap, const str *dir, const str *file) {
  size_t dl = str_len(dir);
  size_t fl = str_len(file);

  if (dl + fl + 2 > cap)
    return 0;

#ifdef _WIN32
  wcscpy_s(out, cap, dir);
  if (dl && dir[dl - 1] != PATH_SEP) {
    out[dl++] = PATH_SEP;
    out[dl] = 0;
  }
  wcscat_s(out, cap, file);
#else
  strcpy(out, dir);
  if (dl && dir[dl - 1] != PATH_SEP) {
    out[dl++] = PATH_SEP;
    out[dl] = 0;
  }
  strcat(out, file);
#endif
  return 1;
}

// ------------------------------------------------------------
// File existence check
// ------------------------------------------------------------
static int file_exists(const str *path) {
#ifdef _WIN32
  DWORD attrs = GetFileAttributesW(path);
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
  return access(path, F_OK) == 0;
#endif
}

#ifndef C5_STRICT
// Append an ASCII string to the (wide or narrow) comment buffer.
static void cat_ascii(str *comments, size_t cap, const char *s) {
#ifdef _WIN32
  size_t n = strlen(s);
  wchar_t *tmp = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  if (!tmp)
    return;
  if (mbstowcs(tmp, s, n + 1) != (size_t)-1)
    wcscat_s(comments, cap, tmp);
  free(tmp);
#else
  strncat(comments, s, cap - strlen(comments) - 1);
#endif
}

static void format_byte(char *out, size_t cap, int v) {
  if (v == EOF)
    snprintf(out, cap, "EOF");
  else
    snprintf(out, cap, "0x%02X", (unsigned)v);
}

static void append_diff_details(str *comments, size_t cap,
                                const struct binary_diff *d) {
  char line[192];
  if (d->expected_size != d->actual_size) {
    snprintf(line, sizeof(line),
             "  size mismatch: expected %ld bytes, actual %ld bytes\n",
             d->expected_size, d->actual_size);
    cat_ascii(comments, cap, line);
  }
  char eb[16], ab[16];
  format_byte(eb, sizeof(eb), d->expected_byte);
  format_byte(ab, sizeof(ab), d->actual_byte);
  snprintf(line, sizeof(line),
           "  first difference at byte offset %ld: expected %s, actual %s\n",
           d->offset, eb, ab);
  cat_ascii(comments, cap, line);
}
#endif /* !C5_STRICT */

// ------------------------------------------------------------
// Exported entry
// ------------------------------------------------------------
DLL_EXPORT
double API_CALL Judge(str *contestantsDir, str *testsDir, str *testOutputs,
                      str *testName, str **comments_out) {
  (void)testName;

  if (!comments_out)
    return 0.0;
  *comments_out = NULL;

  const size_t BUF_CCH = 131072;
  str *comments = calloc(BUF_CCH, sizeof(str));
  if (!comments)
    return 0.0;

  str **files = str_split(testOutputs, STR_LIT('|'));
  if (!files) {
    free(comments);
    return 0.0;
  }

  double score = 0.0;
  str exp[1024], act[1024];

  for (int i = 0; files[i]; ++i) {
    if (!join_path(exp, 1024, testsDir, files[i]) ||
        !join_path(act, 1024, contestantsDir, files[i])) {
      free(files[i]);
      continue;
    }

#ifdef _WIN32
    wcscat_s(comments, BUF_CCH, files[i]);
    wcscat_s(comments, BUF_CCH, STR_LIT(": "));
#else
    str_cat_s(comments, BUF_CCH, files[i]);
    str_cat_s(comments, BUF_CCH, ": ");
#endif

    if (!file_exists(act)) {
      /* Contestant produced no result file */
#ifdef _WIN32
      wcscat_s(comments, BUF_CCH,
               STR_LIT("Kh\xF4ng t\xECm th\x1EA5y k\x1EBFt qu\x1EA3\n"));
#else
      str_cat_s(comments, BUF_CCH,
                "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y "
                "k\xE1\xBA\xBFt qu\x1EA3\n");
#endif
      free(files[i]);
      continue;
    }

#ifndef C5_STRICT
    struct binary_diff diff_info;
    int cmp = compare_binary_files_ex(exp, act, &diff_info);
#else
    int cmp = compare_binary_files(exp, act);
#endif

    if (cmp == 1) {
#ifdef _WIN32
      wcscat_s(comments, BUF_CCH, STR_LIT("PASSED\n"));
#else
      str_cat_s(comments, BUF_CCH, "PASSED\n");
#endif
      score += 1.0;
    } else if (cmp == 0) {
#ifdef _WIN32
      wcscat_s(comments, BUF_CCH, STR_LIT("FAILED\n"));
#else
      str_cat_s(comments, BUF_CCH, "FAILED\n");
#endif
#ifndef C5_STRICT
      append_diff_details(comments, BUF_CCH, &diff_info);
#endif
    } else {
#ifdef _WIN32
      wcscat_s(comments, BUF_CCH, STR_LIT("ERROR\n"));
#else
      str_cat_s(comments, BUF_CCH, "ERROR\n");
#endif
    }

    free(files[i]);
  }

  free(files);

#ifdef _WIN32
  *comments_out = (str *)malloc((str_len(comments) + 1) * sizeof(str));
  if (*comments_out)
    wcscpy_s(*comments_out, str_len(comments) + 1, comments);
#else
  *comments_out = comments;
  comments = NULL;
#endif

  free(comments);
  return score;
}

#ifdef __cplusplus
}
#endif
