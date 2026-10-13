# online-judger API documentation

This document describes the public interfaces of the judger: the evaluator
plugin ABI, the host-side loader, the judging backend, process sandboxing,
configuration parsing, and the verdict/score model.

Headers: `JudgeAPI.h`, `JudgeBackend.h`, `ProcessIO.h`, `Parsers.h`,
`Base.h`, `SubmissionWatcher.h`, `common/AppConfig.h`.

---

## 1. Evaluator plugin ABI (`JudgeAPI.h` contract)

Evaluators are shared libraries (`judge`, `C5Binary`, `C1…C4`) exporting two
C functions. The ABI is Themis-compatible.

### 1.1 `Judge`

```c
// Windows (wchar_t / UTF-16)
double __stdcall Judge(wchar_t *contestantsDir, wchar_t *testsDir,
                       wchar_t *testOutputs, wchar_t *testName,
                       wchar_t **commentsOut);
// Linux/macOS (char / UTF-8)
double Judge(char *contestantsDir, char *testsDir,
             char *testOutputs, char *testName,
             char **commentsOut);
```

| Parameter       | Meaning                                                        |
| --------------- | -------------------------------------------------------------- |
| `contestantsDir`| Working directory holding the contestant's output file(s)      |
| `testsDir`      | Directory holding the reference output file(s)                 |
| `testOutputs`   | `\|`-separated list of result file names (e.g. `"A.OUT"`)      |
| `testName`      | Problem name (informational; may be ignored)                   |
| `commentsOut`   | `[out]` verdict text, NUL-terminated, allocated by the library |

Rules:

- Arguments are **mutable scratch copies** — the callee may modify them but
  must not retain them. Never pass string literals' storage onward.
- All pointers are always non-null when called through `CallJudgeUTF8`, but
  defensive null checks are still required (unit tests drive `Judge`
  directly).
- Return value is the **sum of per-file scores**, each file contributing
  `1.0` on match and `0.0` otherwise. Multi-file `testOutputs` may therefore
  return more than `1.0`; the host (`JudgeBackend`) clamps each call into
  `[0.0, 1.0]` and multiplies by the test mark, but custom hosts must clamp
  themselves.
- `*commentsOut` must always be assigned (to a buffer or `NULL`).
  Conventional comment lines: `PASSED`, `FAILED`, `ERROR`, and the
  Vietnamese missing-file verdict (`Không tìm thấy kết quả`). An internal
  comparison failure must emit `ERROR`, never silence.
- The same library may be called from several threads, but the host
  serializes calls per library path — still, keep evaluators reentrant and
  avoid global mutable state.

### 1.2 `FreeJudgeComment`

```c
void __stdcall FreeJudgeComment(void *comment);  // (plain C call on POSIX)
```

Releases a buffer previously returned through `commentsOut`. Libraries that
predate this export must allocate comments with `malloc`/`calloc` from the
process-shared CRT so the host's `free()` stays valid; new evaluators must
export this function. The host prefers the export and falls back to `free()`.

### 1.3 Building an evaluator

Windows (MSVC): `cl /LD evaluator.c` · MinGW:
`x86_64-w64-mingw32-gcc -shared -o evaluator.dll evaluator.c` ·
Linux: `gcc -shared -fPIC evaluator.c -o libevaluator.so` (or just use the
CMake targets `judge`, `C5Binary`, `C1…C4`). `C5Binary` is the same source
as `judge` compiled with `-DC5_STRICT` (verdict only, no diff details).

### 1.4 Bundled evaluators

| Library                    | Match rule                                            |
| -------------------------- | ----------------------------------------------------- |
| `C5Binary` / `judge`       | Byte-for-byte identical (`judge` adds diff details)   |
| `C1LinesWordsIgnoreCase`   | Same lines/words in order, case-insensitive, ASCII    |
| `C2LinesWordsCase`         | Same as C1, case-sensitive                            |
| `C3WordsIgnoreCase`        | Same word stream (lines irrelevant), case-insensitive |
| `C4WordsCase`              | Same word stream, case-sensitive                      |

C1/C2 tolerate trailing blank lines (a leading blank line is significant —
documented Themis quirk). Line lengths and word lengths are unbounded.
Intended for ASCII text; non-ASCII bytes compare as-is (C1 folds ASCII case
only).

---

## 2. Host loader (`JudgeAPI.h`)

```cpp
JudgeFn Load(const char *path);              // throws std::runtime_error
void FreeJudgeComment(const char *path, void *comment);
double STDCALL CallJudgeUTF8(JudgeFn fn, const char *contestantsDir,
                             const char *testsDir, const char *testOutputs,
                             const char *testName, std::string *comments);
```

- `Load` is thread-safe and caches per path; modules stay loaded for the
  process lifetime. On 32-bit Windows it also probes the decorated
  `_Judge@20` name.
- `CallJudgeUTF8` converts UTF-8 ↔ UTF-16 on Windows, always passes valid
  pointers, converts the comment back to UTF-8 (`comments` may be null), and
  frees the native buffer before returning — callers must not free it.
- `FreeJudgeComment(path, comment)` frees one buffer with its library's own
  deallocator when exported, else `free()`; null is a no-op.

---

## 3. Judging backend (`JudgeBackend.h`)

```cpp
void judge(fs::path subdir, fs::path tdir, std::string problem,
           std::string user, const Configuration &conf,
           const std::unordered_map<std::string, Testcases> &testcases,
           const fs::path &judger_path);
std::map<std::pair<std::string,std::string>, std::pair<std::string,double>>
getScores();  // {(user, problem) -> (verdict, points)}, thread-safe
std::string resolveEvaluatorName(const std::string &configured,
                                 const fs::path &judgersDir);
```

`judge()` compiles `<user>/<problem>.<ext>` per the global compiler table in
an isolated `judgeWORK/<random>` directory (300 s cap, no memory cap — the
compiler must be free to use host memory), runs every subtest with the
configured time/memory limits, calls the problem's evaluator, and appends a
timestamped transcript to `<subdir>/$History/`.

### 3.1 Verdicts and scores

`getScores()` maps `(user, problem)` to `(verdict, points)`:

- `"V"` — judged (points may be `0` for wrong answer / TLE / MLE / OLE /
  runtime error).
- `"X"` — not judged: compile error, missing source/compiler/evaluator,
  missing test data, or internal error.
- `"-"` — source file not found.

Every failure path records a verdict — a judged pair never keeps a stale or
blank cell. Runtime failures are logged per test (`TLEd`, `MLEd`, `output
limit exceeded`, `exited with code 0x…`).

### 3.2 `resolveEvaluatorName`

Maps legacy Themis names (`C5Binary.dll`) to the platform convention
(`libC5Binary.so` on POSIX, `.dll` on Windows; `lib` prefix added only when
missing), preferring whichever variant actually exists in `judgersDir`. Used
by both the CLI and the GUI — do not duplicate the mapping.

---

## 4. Process sandbox (`ProcessIO.h`)

```cpp
struct ProcessResult { std::string stdout_data, stderr_data;
                       uint32_t exit_code; float time; /* CPU seconds */ };
ProcessResult run_command(const std::vector<std::string> &command,
                          const fs::path &cwd,
                          const std::string &stdin_data = "",
                          float time_limit_sec = 1.0,
                          int maxMemoryMB = 1024);
```

- Spawns the child with redirected stdio, feeds stdin from a helper thread,
  and enforces limits: wall **and** CPU time → `CPError<TLE>`, 32 MiB output
  cap → `CPError<OLE>`, spawn failures → `CPError<IE>`.
- Memory (`maxMemoryMB`, `0` = unlimited): Job-Object commit cap on Windows,
  `RLIMIT_AS` on POSIX. Note `RLIMIT_AS` counts address space — managed
  runtimes (JVM/Python/CLR) may need generous caps.
- The whole process tree is killed (Job kill on Windows, process group +
  `killpg` on POSIX); pipes are drained after exit so output is not
  truncated.
- POSIX exit codes `≥ 128` encode signal deaths; the backend maps `SIGXCPU`
  → TLE and `SIGKILL` (OOM/rlimit kill — the sandbox's own kills already
  threw) → `MLE`. Codes `126/127` are host spawn failures. `MLE` is declared
  in `CPErrors` for exactly this classification.
- Throws on empty command / non-positive time limit.

`expand_percent_vars(input, vars)` expands `%NAME%`-style placeholders used
by compiler templates.

---

## 5. Configuration (`Parsers.h`, `Base.h`, `common/AppConfig.h`)

```cpp
void parseGlobalSettingsFormat(std::string_view sv, Configuration &out);
void parseSettingsFormat(std::string_view sv, Testcases &out);
```

- Auto-detect among YAML / JSON / XML / TOML. The first non-whitespace byte
  orders the attempts (`<` → XML first, `{` → JSON first); each attempt
  parses into a temporary so failed formats leave no residue. Anything that
  no parser accepts throws `std::runtime_error`.
- Dialects are liberal: numbers/booleans accept numeric strings and quoted
  YAML scalars (`'false'`, `'1'`); XML booleans are case-insensitive;
  `TestCase` accepts a single object or an array.
- `AppConfig::load()` imports Themis XML (read-only) → own JSON
  (`~/.config/online-judger/oj.cfg`) → built-in defaults, returning `false`
  when a source is corrupt. `save()` writes the JSON config only — never the
  Themis path.

Compiler template (`Configuration.compiler.items[i].cmd`):

```text
g++ -std=c++14 "%NAME%%EXT%" -pipe -O2 -x c++ -o"%NAME%.exe"|@WorkDir=%PATH%
```

`%NAME%` = source stem, `%EXT%` = source extension, `%PATH%` = workdir for
`@WorkDir` (source path inside the command part). Both single- and
double-quoted arguments are supported.

`Testcases` fields: `Name`, `InputFile`, `OutputFile`, `EvaluatorName`,
`UseStdIn/UseStdOut`, `TimeLimit` (s), `MemoryLimit` (MiB), `Mark`, plus
`subtests` (`-1` = inherit the problem-level value).

---

## 6. Watch mode (`SubmissionWatcher.h`)

```cpp
SubmissionWatcher(const fs::path &dir, Callback cb);  // cb: path -> void
void start();  // idempotent; restarts after stop()
void stop();   // drains already-queued submissions, then joins
void wait();   // joins; blocks until stop() if still running
```

Watches `dir` recursively for added/modified `*.cpp` files (500 ms
per-path debounce), queuing them for a single worker thread. Callback
exceptions are contained per item (the worker survives); the queue is
drained — not dropped — on `stop()`.

---

## 7. CLI (`main_judger`)

```text
main_judger -s <submissions> -t <tests> [-c <global-config>] [-j <judgers>]
            [-w] [-n <jobs>]
```

Discovers problems from `<tests>/*/Settings.cfg`, judges every
`(user, problem)` pair on a thread pool (`-n 0` = auto, capped at 256),
prints the score table, and with `-w` keeps watching for new submissions
instead of exiting. History transcripts land in
`<submissions>/$History/<stamp>-<n>[<user>][<problem>].txt`; set
`JUDGER_KEEP_WORKDIR=1` to preserve scratch compile dirs for debugging.
