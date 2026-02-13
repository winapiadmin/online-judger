// C1LinesWordsIgnoreCase.c
// =============================================================
//
// Windows: wchar_t / UTF-16, wide APIs
// Others : char    / UTF-8, libc
//
// BUILD
// -----
// Windows (MSVC):
//   cl /LD C1LinesWordsIgnoreCase.c
//
// Windows (MinGW):
//   x86_64-w64-mingw32-gcc -shared -o C1LinesWordsIgnoreCase.dll C1LinesWordsIgnoreCase.c
//
// Linux:
//   gcc -shared -fPIC C1LinesWordsIgnoreCase.c -o libC1LinesWordsIgnoreCase.so
//
// macOS:
//   clang -shared -fPIC C1LinesWordsIgnoreCase.c -o libC1LinesWordsIgnoreCase.dylib
// 
// (or just pick it from CMake)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
  #define API_CALL   __cdecl
  typedef wchar_t    str;
  #define STR_LIT(x) L##x
  #define PATH_SEP   L'\\'
#else
  #define DLL_EXPORT __attribute__((visibility("default")))
  #define API_CALL
  typedef char       str;
  #define STR_LIT(x) x
  #define PATH_SEP   '/'
#endif

// ------------------------------------------------------------
// String helpers
// ------------------------------------------------------------
#ifdef _WIN32
  #define str_len    wcslen
  #define str_dup    _wcsdup
  #define str_cmp    wcscmp
  #define str_cat    wcscat
  #define str_cat_s  wcscat_s
  #define str_cpy_s  wcscpy_s
  #define str_tok    wcstok_s
  #define str_tolower towlower
  #define str_space  iswspace
  #define str_fopen  _wfopen
#else
  #define str_len    strlen
  #define str_dup    strdup
  #define str_cmp    strcmp
  #define str_cat_s(b,c,s) strncat(b,s,(c)-strlen(b)-1)
  #define str_cpy_s(d,c,s) strncpy(d,s,c)
  #define str_tok    strtok_r
  #define str_tolower tolower
  #define str_space  isspace
  #define str_fopen(p,m) fopen(p,m)
#endif

// ------------------------------------------------------------
// Utility: split "a|b|c"
// ------------------------------------------------------------
static str **str_split(const str *s, str delim)
{
    if (!s) return NULL;

    str *tmp = str_dup(s);
    int count = 1;

    for (str *p = tmp; *p; ++p)
        if (*p == delim) count++;

    str **out = calloc((size_t)count + 1, sizeof(str *));
    if (!out) { free(tmp); return NULL; }

    str d[2] = { delim, 0 };
    str *ctx = NULL;
    int i = 0;

    for (str *tok = str_tok(tmp, d, &ctx);
         tok;
         tok = str_tok(NULL, d, &ctx))
        out[i++] = str_dup(tok);

    free(tmp);
    return out;
}

// ------------------------------------------------------------
// Utility: trim trailing whitespace
// ------------------------------------------------------------
static void rtrim(str *s)
{
    size_t n = str_len(s);
    while (n && str_space(s[n - 1]))
        s[--n] = 0;
}

// ------------------------------------------------------------
// Utility: lowercase in-place
// ------------------------------------------------------------
static void str_lower(str *s)
{
    for (; *s; ++s)
        *s = (str)str_tolower(*s);
}

// ------------------------------------------------------------
// Utility: split line into words
// ------------------------------------------------------------
static int split_words(str *buf, str **out, int max)
{
    int n = 0;
    str *ctx = NULL;

    for (str *tok = str_tok(buf, STR_LIT(" \t\r\n"), &ctx);
         tok && n < max;
         tok = str_tok(NULL, STR_LIT(" \t\r\n"), &ctx))
        out[n++] = tok;

    return n;
}

// ------------------------------------------------------------
// Text comparison
// ------------------------------------------------------------
static int compare_text_files(const str *f1, const str *f2)
{
    FILE *a = str_fopen(f1, STR_LIT("r"));
    FILE *b = str_fopen(f2, STR_LIT("r"));

    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return -1;
    }

    str la[1024], lb[1024];

#ifdef _WIN32
    while (fgetws(la, 1024, a) && fgetws(lb, 1024, b)) {
#else
    while (fgets(la, 1024, a) && fgets(lb, 1024, b)) {
#endif
        rtrim(la);
        rtrim(lb);

        str ca[1024], cb[1024];
        str_cpy_s(ca, 1024, la);
        str_cpy_s(cb, 1024, lb);

        str_lower(ca);
        str_lower(cb);

        str *wa[256], *wb[256];
        int na = split_words(ca, wa, 256);
        int nb = split_words(cb, wb, 256);

        if (na != nb) { fclose(a); fclose(b); return 0; }

        for (int i = 0; i < na; ++i)
            if (str_cmp(wa[i], wb[i]) != 0) {
                fclose(a); fclose(b); return 0;
            }
    }

    fclose(a);
    fclose(b);
    return 1;
}

// ------------------------------------------------------------
// Path join
// ------------------------------------------------------------
static int join_path(str *out, size_t cap, const str *dir, const str *file)
{
    size_t dl = str_len(dir);
    if (dl + str_len(file) + 2 > cap) return 0;

    str_cpy_s(out, cap, dir);
    if (dir[dl - 1] != PATH_SEP) {
        out[dl++] = PATH_SEP;
        out[dl] = 0;
    }
    str_cat_s(out, cap, file);
    return 1;
}

// ------------------------------------------------------------
// Exported entry
// ------------------------------------------------------------
DLL_EXPORT
double API_CALL Judge(
    str *contestantsDir,
    str *testsDir,
    str *testOutputs,
    str *testName,
    str **comments_out)
{
    (void)testName;

    if (!comments_out) return 0.0;
    *comments_out = NULL;

    const size_t BUF = 131072;
    str *comments = calloc(BUF, sizeof(str));

    str **files = str_split(testOutputs, STR_LIT('|'));
    if (!files) { free(comments); return 0.0; }

    double score = 0.0;
    str exp[1024], act[1024];

    for (int i = 0; files[i]; ++i) {
        if (join_path(exp, 1024, testsDir, files[i]) &&
            join_path(act, 1024, contestantsDir, files[i]))
        {
            if (compare_text_files(exp, act) == 1) {
                str_cat_s(comments, BUF,
                    STR_LIT("K\x1EBFt qu\x1EA3 kh\x1EDBp \x111\xE1p \xE1n!\n"));
                score += 1.0;
            }
            else str_cat_s(comments, BUF,
                    STR_LIT("K\x1EBFt qu\x1EA3 KH\xC1\x43 \x111\xE1p \xE1n!\n"));
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
