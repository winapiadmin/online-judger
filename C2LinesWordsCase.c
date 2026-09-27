// C2LinesWordsCase.c
// =============================================================
//
// Windows: wchar_t / UTF-16, wide APIs
// Others : char    / UTF-8, libc
//
// BUILD
// -----
// Windows (MSVC):
//   cl /LD C2LinesWordsCase.c
//
// Windows (MinGW):
//   x86_64-w64-mingw32-gcc -shared -o C2LinesWordsCase.dll C2LinesWordsCase.c
//
// Linux:
//   gcc -shared -fPIC C2LinesWordsCase.c -o libC2LinesWordsCase.so
//
// macOS:
//   clang -shared -fPIC C2LinesWordsCase.c -o libC2LinesWordsCase.dylib
//
// (or just pick it from CMake)

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
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
#define PATH_SEP L'\\'
#else
#define DLL_EXPORT __attribute__((visibility("default")))
#define API_CALL
typedef char str;
#define STR_LIT(x) x
#define PATH_SEP '/'
#endif

// ------------------------------------------------------------
// String helpers
// ------------------------------------------------------------
#ifdef _WIN32
#define str_len wcslen
#define str_dup _wcsdup
#define str_cmp wcscmp
#define str_cat wcscat
#define str_cat_s wcscat_s
#define str_cpy_s wcscpy_s
#define str_tok wcstok_s
#define str_tolower towlower
#define str_space iswspace
#define str_fopen _wfopen
#else
#define str_len strlen
#define str_dup strdup
#define str_cmp strcmp
#define str_cat_s(b, c, s)                                                     \
  strncat(b, s, (size_t)((c) > strlen(b) + 1 ? (c) - strlen(b) - 1 : 0))
#define str_cpy_s(d, c, s) strncpy(d, s, c)
#define str_tok strtok_r
#define str_tolower tolower
#define str_space isspace
#define str_fopen(p, m) fopen(p, m)

#include <unistd.h>
#endif

// ------------------------------------------------------------
// Utility: split "a|b|c"
// ------------------------------------------------------------
static str **str_split(const str *s, str delim) {
  if (!s)
    return NULL;

  str *tmp = str_dup(s);
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

  for (str *tok = str_tok(tmp, d, &ctx); tok; tok = str_tok(NULL, d, &ctx))
    out[i++] = str_dup(tok);

  free(tmp);
  return out;
}

// ------------------------------------------------------------
// Utility: trim trailing whitespace
// ------------------------------------------------------------
static void rtrim(str *s) {
  size_t n = str_len(s);
  while (n && str_space(s[n - 1]))
    s[--n] = 0;
}

// ------------------------------------------------------------
// Utility: compare two lines word-by-word (case-sensitive).
// Both lines are tokenized in place, so they must be private mutable
// buffers. Words are compared as they are tokenized: no limit on how many
// words a line may contain.
// Returns 1 if equal, 0 if not.
// ------------------------------------------------------------
static int line_words_equal(str *la, str *lb) {
  static const str seps[] = STR_LIT(" \t");

  str *ctxa = NULL, *ctxb = NULL;
  str *ta = str_tok(la, seps, &ctxa);
  str *tb = str_tok(lb, seps, &ctxb);

  while (ta && tb) {
    if (str_cmp(ta, tb) != 0)
      return 0;
    ta = str_tok(NULL, seps, &ctxa);
    tb = str_tok(NULL, seps, &ctxb);
  }

  /* equal only when both lines ran out of words together */
  return ta == NULL && tb == NULL;
}

// ------------------------------------------------------------
// Skip UTF-8 BOM if present
// ------------------------------------------------------------
static void skip_bom(FILE *f) {
  unsigned char bom[3];
  long pos = ftell(f);

  if (fread(bom, 1, 3, f) == 3) {
    if (!(bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
      fseek(f, pos, SEEK_SET);
  } else {
    fseek(f, pos, SEEK_SET);
  }
}

// ------------------------------------------------------------
// Read one line of arbitrary length (buffer grows as needed), trimmed of
// trailing whitespace. Returns a malloc'd line, or NULL at EOF.
// Sets *ok to 0 on allocation failure (caller treats this as an error).
// The caller owns and must free the returned buffer.
// ------------------------------------------------------------
static str *read_line(FILE *f, int *ok) {
  size_t cap = 256, len = 0;
  *ok = 1;

  str *buf = (str *)malloc(cap * sizeof(str));
  if (!buf) {
    *ok = 0;
    return NULL;
  }

  for (;;) {
#ifdef _WIN32
    if (!fgetws(buf + len, (int)(cap - len), f))
      break; /* EOF */
#else
    if (!fgets(buf + len, (int)(cap - len), f))
      break; /* EOF */
#endif
    len += str_len(buf + len);

    if (len > 0 && buf[len - 1] == STR_LIT('\n'))
      break; /* complete line */

    if (len + 1 < cap)
      break; /* partial content then EOF */

    /* buffer filled without reaching end of line: grow and continue */
    cap *= 2;
    str *nb = (str *)realloc(buf, cap * sizeof(str));
    if (!nb) {
      free(buf);
      *ok = 0;
      return NULL;
    }
    buf = nb;
  }

  if (len == 0 && feof(f)) {
    free(buf);
    return NULL;
  }

  rtrim(buf);
  return buf;
}

// ------------------------------------------------------------
// Text comparison: line-by-line, word-by-word, case-sensitive.
// Line lengths are unbounded. Trailing blank lines on either side are
// ignored; anything else must match positionally.
// Returns 1 if equal, 0 if different, -1 on open error / allocation failure.
// ------------------------------------------------------------
static int compare_text_files(const str *f1, const str *f2) {
  FILE *a = str_fopen(f1, STR_LIT("r"));
  FILE *b = str_fopen(f2, STR_LIT("r"));

  if (!a || !b) {
    if (a)
      fclose(a);
    if (b)
      fclose(b);
    return -1;
  }

  skip_bom(a);
  skip_bom(b);

  int ea = 0, eb = 0;
  int result = 1;
  str *la = NULL, *lb = NULL;

  while (result == 1) {
    if (!ea) {
      int ok;
      la = read_line(a, &ok);
      if (!ok)
        result = -1;
      else if (!la)
        ea = 1;
    }
    if (result == 1 && !eb) {
      int ok;
      lb = read_line(b, &ok);
      if (!ok)
        result = -1;
      else if (!lb)
        eb = 1;
    }
    if (result != 1)
      break;

    if (ea && eb)
      break;

    if (ea) {
      /* a finished: b may only have blank lines left */
      if (lb[0] != 0)
        result = 0;
      free(lb);
      lb = NULL;
      continue;
    }
    if (eb) {
      if (la[0] != 0)
        result = 0;
      free(la);
      la = NULL;
      continue;
    }

    if (!line_words_equal(la, lb))
      result = 0;

    free(la);
    free(lb);
    la = lb = NULL;
  }

  free(la);
  free(lb);
  fclose(a);
  fclose(b);
  return result;
}

// ------------------------------------------------------------
// Path join
// ------------------------------------------------------------
static int join_path(str *out, size_t cap, const str *dir, const str *file) {
  size_t dl = str_len(dir);
  if (dl + str_len(file) + 2 > cap)
    return 0;

  str_cpy_s(out, cap, dir);
  if (dl && dir[dl - 1] != PATH_SEP) {
    out[dl++] = PATH_SEP;
    out[dl] = 0;
  }
  str_cat_s(out, cap, file);
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

  const size_t BUF = 131072;
  str *comments = calloc(BUF, sizeof(str));

  str **files = str_split(testOutputs, STR_LIT('|'));
  if (!files) {
    free(comments);
    return 0.0;
  }

   double score = 0.0;
   str exp[1024], act[1024];

#ifdef _WIN32
#define V_MISSING_STR STR_LIT("Kh\xF4ng t\xECm th\x1EA5y k\x1EBFt qu\x1EA3\n")
#define V_MATCH_STR STR_LIT("K\x1EBFt qu\x1EA3 kh\x1EDBp \x111\xE1p \xE1n!\n")
#define V_MISMATCH_STR STR_LIT("K\x1EBFt qu\x1EA3 KH\xC1\x43 \x111\xE1p \xE1n!\n")
#else
#define V_MISSING_STR "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y k\xE1\xBA\xBFt qu\xE1\xBA\xA3\n"
#define V_MATCH_STR "K\xE1\xBA\xBFt qu\xE1\xBA\xA3 kh\xE1\xBB\x9Bp \xE1\xBB\x99\xC3\xA1p \xC3\xA1n!\n"
#define V_MISMATCH_STR "K\xE1\xBA\xBFt qu\xE1\xBA\xA3 KH\xC3\x81" "C \xE1\xBB\x99\xC3\xA1p \xC3\xA1n!\n"
#endif

   for (int i = 0; files[i]; ++i) {
    if (join_path(exp, 1024, testsDir, files[i]) &&
        join_path(act, 1024, contestantsDir, files[i])) {
      if (!file_exists(act)) {
        /* Contestant produced no result file */
        str_cat_s(comments, BUF,
                  V_MISSING_STR);
      } else if (compare_text_files(exp, act) == 1) {
        str_cat_s(comments, BUF,
                  V_MATCH_STR);
        score += 1.0;
      } else
        str_cat_s(comments, BUF,
                  V_MISMATCH_STR);
    }
    free(files[i]);
  }

  free(files);
  *comments_out = comments;
  return score;
}

#ifdef __cplusplus
}
#endif
