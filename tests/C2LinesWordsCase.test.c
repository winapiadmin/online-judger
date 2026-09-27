// Unit tests for the C2LinesWordsCase evaluator (case-sensitive variant).
#include "test_util.h"

static void run(const char *expected, const char *actual, double *score) {
  write_bytes(g_tests_dir, "A.OUT", expected, strlen(expected));
  if (actual)
    write_bytes(g_work_dir, "A.OUT", actual, strlen(actual));
  *score = judge_a_out(NULL, 0);
}

static void remove_actual(void) {
  char path[1200];
  snprintf(path, sizeof(path), "%s/A.OUT", g_work_dir);
  remove(path);
}

int main(void) {
  setup_dirs();
  double s;

  // 1. identical
  run("ABC def\n", "ABC def\n", &s);
  CHECK(s == 1.0, "C2 identical files should score 1.0");

  // 2. case difference -> FAIL (case-sensitive)
  run("ABC def\n", "abc def\n", &s);
  CHECK(s == 0.0, "C2 must be case-sensitive");

  // 3. line/word order matters
  run("a b\nc\n", "a\nb c\n", &s);
  CHECK(s == 0.0, "C2 line/word order must matter");

  // 4. trailing blank lines tolerated
  run("x\n", "x\n\n", &s);
  CHECK(s == 1.0, "C2 should tolerate trailing blank lines");

  // 5. extra non-blank line -> FAIL
  run("x\n", "x\ny\n", &s);
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
    act[ao++] = '\n';
    run(exp, act, &s);
    CHECK(s == 1.0, "C2 long-line identical content should score 1.0");
    free(exp);
    free(act);
  }

  // 7. missing result file verdict
  remove_actual();
  run("anything\n", NULL, &s);
  CHECK(s == 0.0, "C2 missing result file scores 0");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
