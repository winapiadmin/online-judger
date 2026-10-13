// Unit tests for the C2LinesWordsCase evaluator (case-sensitive variant).
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

  // 1. identical
  run("ABC def\n", "ABC def\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C2 identical files should score 1.0");
  CHECK(strstr(c, "kh\xE1\xBB\x9Bp") != NULL, "C2 match comment missing");

  // 2. case difference -> FAIL (case-sensitive)
  run("ABC def\n", "abc def\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C2 must be case-sensitive");

  // 3. line/word order matters
  run("a b\nc\n", "a\nb c\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C2 line/word order must matter");

  // 4. trailing blank lines tolerated
  run("x\n", "x\n\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C2 should tolerate trailing blank lines");

  // 5. extra non-blank line -> FAIL
  run("x\n", "x\ny\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C2 must not tolerate extra non-blank lines");

  // 6. long line (beyond the historical caps)
  {
    size_t need = 20000 * 8 + 16;
    char *exp = malloc(need), *act = malloc(need);
    size_t eo = 0, ao = 0;
    for (int i = 0; i < 20000; ++i) {
      eo += (size_t)sprintf(exp + eo, i ? " W%d" : "W%d", i);
      ao += (size_t)sprintf(act + ao, i ? " W%d" : "W%d", i);
    }
    exp[eo++] = '\n';
    exp[eo] = '\0';
    act[ao++] = '\n';
    act[ao] = '\0';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 1.0, "C2 long-line identical content should score 1.0");

    // 7. same long line, differing deep inside (word #15000)
    free(exp);
    exp = malloc(need);
    eo = 0;
    for (int i = 0; i < 20000; ++i) {
      if (i == 15000)
        eo += (size_t)sprintf(exp + eo, i ? " CHANGED" : "CHANGED");
      else
        eo += (size_t)sprintf(exp + eo, i ? " W%d" : "W%d", i);
    }
    exp[eo++] = '\n';
    exp[eo] = '\0';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 0.0, "C2 must detect a difference deep inside a very long line");
    free(exp);
    free(act);
  }

  // 8. missing result file verdict
  remove_actual();
  run("anything\n", NULL, &s, c, sizeof(c));
  CHECK(s == 0.0, "C2 missing result file scores 0");
  CHECK(strstr(c, "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y") != NULL,
        "C2 missing-file Vietnamese verdict missing");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
