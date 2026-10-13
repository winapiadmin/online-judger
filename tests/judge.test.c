// Unit tests for judge.c (C5Binary comparison + diff reporting).
// Built WITHOUT -DC5_STRICT, so diff details are expected on mismatch.
#include "test_util.h"

static void run(const char *expected, size_t elen, const char *actual,
                size_t alen, double *score, char *comments, size_t cap) {
  write_bytes(g_tests_dir, "A.OUT", expected, elen);
  if (actual)
    write_bytes(g_work_dir, "A.OUT", actual, alen);
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

  // 1. identical bytes (incl. an embedded NUL) -> PASSED.
  // NOTE: octal escapes, not \x00: hex escapes greedily consume following
  // hex digits ("\x00da" is U+00DA, not NUL + "da").
  run("binary\0data\1\2", 13, "binary\0data\1\2", 13, &s, c, sizeof(c));
  CHECK(s == 1.0, "identical binary content should score 1.0");
  CHECK(strstr(c, "PASSED") != NULL, "PASSED verdict missing");

  // 2. single byte difference -> FAILED with diff details
  run("\x00\x01\x02\x03", 4, "\x00\x01\x02\x04", 4, &s, c, sizeof(c));
  CHECK(s == 0.0, "byte difference must score 0");
  CHECK(strstr(c, "FAILED") != NULL, "FAILED verdict missing");
  CHECK(strstr(c, "first difference at byte offset 3") != NULL,
        "diff offset missing from comments");
  CHECK(strstr(c, "expected 0x03") != NULL && strstr(c, "actual 0x04") != NULL,
        "diff byte values missing from comments");

  // 3. actual is a strict prefix (EOF vs byte) -> size mismatch reported
  run("abc", 3, "ab", 2, &s, c, sizeof(c));
  CHECK(s == 0.0, "truncated file must score 0");
  CHECK(strstr(c, "size mismatch: expected 3 bytes, actual 2 bytes") != NULL,
        "size mismatch line missing");

  // 4. actual longer than expected
  run("ab", 2, "abcd", 4, &s, c, sizeof(c));
  CHECK(s == 0.0, "extra bytes must fail the byte-exact comparison");

  // 5. missing result file -> Vietnamese verdict, no diff details
  remove_actual();
  run("anything", 8, NULL, 0, &s, c, sizeof(c));
  CHECK(s == 0.0, "missing result file scores 0");
  CHECK(strstr(c, "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y") != NULL,
        "missing-file Vietnamese verdict missing");
  CHECK(strstr(c, "k\xE1\xBA\xBFt qu\xE1\xBA\xA3") != NULL,
        "missing-file Vietnamese verdict (full) missing");

  remove_tree_best_effort(g_base);
  printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
