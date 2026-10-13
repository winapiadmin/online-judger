// Unit tests for the C3WordsIgnoreCase evaluator (word stream, lines free).
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
  run("hello world\n", "hello world\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C3 identical files should score 1.0");

  // 2. line structure irrelevant
  run("a b\nc\n", "a\nb c\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C3 must ignore line structure");

  // 3. word ORDER still matters
  run("alpha beta\n", "beta alpha\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C3 word order must matter");

  // 4. case-insensitive
  run("ABC def\n", "abc def\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C3 should ignore case");

  // 5. missing/extra words -> FAIL
  run("one two three\n", "one two\n", &s, c, sizeof(c));
  CHECK(s == 0.0, "C3 word count must match");

  // 6. whitespace shape is irrelevant (tabs/newlines vs spaces)
  run("a\tb\n\nc  d\n", "a b c d\n", &s, c, sizeof(c));
  CHECK(s == 1.0, "C3 must treat all whitespace alike");

  // 7. over-long words: a single 8000-char word must compare exactly.
  // Guards the old fixed-buffer truncation (common-prefix false positive).
  {
    const size_t n = 8000;
    char *exp = malloc(n + 2), *act = malloc(n + 2);
    memset(exp, 'a', n);
    memset(act, 'a', n);
    exp[n] = '\n';
    exp[n + 1] = '\0';
    act[n] = '\n';
    act[n + 1] = '\0';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 1.0, "C3 identical over-long word should score 1.0");

    // 8. same, but differing in the last character (past any fixed buffer)
    act[n - 1] = 'b';
    run(exp, act, &s, c, sizeof(c));
    CHECK(s == 0.0, "C3 must detect a tail difference in an over-long word");
    free(exp);
    free(act);
  }

  // 9. missing result file verdict
  remove_actual();
  run("anything\n", NULL, &s, c, sizeof(c));
  CHECK(s == 0.0, "C3 missing result file scores 0");
  CHECK(strstr(c, "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y") != NULL,
        "C3 missing-file Vietnamese verdict missing");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
