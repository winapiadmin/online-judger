// Unit tests for the C4WordsCase evaluator (word stream, case-sensitive).
#include "test_util.h"

static void run(const char *expected, const char *actual, double *score) {
  write_bytes(g_tests_dir, "A.OUT", expected, strlen(expected));
  if (actual)
    write_bytes(g_work_dir, "A.OUT", actual, strlen(actual));
  *score = judge_a_out(NULL, 0);
}

int main(void) {
  setup_dirs();
  double s;

  // 1. identical
  run("hello world\n", "hello world\n", &s);
  CHECK(s == 1.0, "C4 identical files should score 1.0");

  // 2. line structure irrelevant
  run("a b\nc\n", "a\nb c\n", &s);
  CHECK(s == 1.0, "C4 must ignore line structure");

  // 3. case difference -> FAIL (case-sensitive)
  run("ABC def\n", "abc def\n", &s);
  CHECK(s == 0.0, "C4 must be case-sensitive");

  // 4. word order matters
  run("alpha beta\n", "beta alpha\n", &s);
  CHECK(s == 0.0, "C4 word order must matter");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
