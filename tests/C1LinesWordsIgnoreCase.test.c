// Unit tests for the C1LinesWordsIgnoreCase evaluator.
#include "test_util.h"

static void run(const char *expected, const char *actual, double *score,
                char *comments, size_t cap) {
  write_bytes(g_tests_dir, "A.OUT", expected, strlen(expected));
  if (actual)
    write_bytes(g_work_dir, "A.OUT", actual, strlen(actual));
  *score = judge_a_out(comments, cap);
}

static void remove_actual(void) {
  char path[1200];
  snprintf(path, sizeof(path), "%s/A.OUT", g_work_dir);
  remove(path);
}

int main(void) {
  setup_dirs();
  double s;
  char c[8192];

  // 1. identical content
  run("hello world\n", "hello world\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C1 identical files should score 1.0");
  CHECK(strstr(c, "kh\xE1\xBB\x9Bp") != NULL, "C1 match comment missing");

  // 2. words reordered across lines -> FAIL
  run("a b\nc\n", "a\nb c\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C1 line/word order must matter");

  // 3. case-insensitive
  run("ABC def\n", "abc def\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C1 should ignore case");

  // 4. trailing blank lines tolerated
  run("x\n", "x\n\n\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C1 should tolerate trailing blank lines");

  // 5. extra NON-blank line -> FAIL
  run("x\n", "x\ny\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C1 must not tolerate extra non-blank lines");

  // 6. leading blank line matters (documented Themis quirk)
  run("\nx\n", "x\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C1 leading blank line is significant (spec quirk)");

  // 7. unbounded line length: 20000 words on ONE line (~60KB)
  {
    size_t need = 20000 * 8 + 16;
    char *exp = malloc(need), *act = malloc(need);
    size_t eo = 0, ao = 0;
    for (int i = 0; i < 20000; ++i) {
      eo += (size_t)sprintf(exp + eo, i ? " w%d" : "w%d", i);
      ao += (size_t)sprintf(act + ao, i ? " w%d" : "w%d", i);
    }
    exp[eo++] = '\n';
    act[ao++] = '\n';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 1.0, "C1 long-line identical content should score 1.0");

    // 8. same long line, differing deep inside (word #15000)
    free(exp);
    exp = malloc(need);
    eo = 0;
    for (int i = 0; i < 20000; ++i) {
      if (i == 15000)
        eo += (size_t)sprintf(exp + eo, i ? " CHANGED" : "CHANGED");
      else
        eo += (size_t)sprintf(exp + eo, i ? " w%d" : "w%d", i);
    }
    exp[eo++] = '\n';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 0.0,
          "C1 must detect a difference deep inside a very long line");
    free(exp);
    free(act);
  }

  // 9. missing result file verdict
  remove_actual();
  run("anything\n", NULL, &s, c, sizeof(c));
  CHECK(s == 0.0, "C1 missing result file scores 0");
  CHECK(strstr(c, "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y") != NULL,
        "C1 missing-file Vietnamese verdict missing");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n",
         g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
