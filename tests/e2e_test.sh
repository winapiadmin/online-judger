#!/usr/bin/env bash
# End-to-end test for the judger.
#
# Usage: e2e_test.sh <path-to-main_judger> <judgers-dir>
# Requires g++ on PATH (the compiler used by the generated config).
#
# Covers: correct/wrong answers, stdin/stdout mode, file-I/O mode, TLE,
# runtime error, compile error, memory-limit kill, TOML settings parsing,
# and the "no result file" verdict. Runs on Linux runners and Windows
# runners under Git Bash; expected-output line endings adapt to the
# platform's CRT text mode.
set -u

BIN="$1"
JUDGERS="$2"

PASS=0
FAIL=0
ok() { echo "  ok  : $1"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

WIN=0
case "$(uname)" in MINGW* | MSYS* | CYGWIN*) WIN=1 ;; esac
EOL=''
[ "$WIN" = 1 ] && EOL=$'\r'

WORK="$(mktemp -d)"
SUBS="$WORK/subs"
TESTS="$WORK/tests"
JDIR="$WORK/judgers"
HOUSE="$WORK/house"
mkdir -p "$SUBS" "$TESTS/A/1" "$JDIR" "$HOUSE"
cp "$JUDGERS"/*.dll "$JDIR/" 2>/dev/null || true
cp "$JUDGERS"/*.so "$JDIR/" 2>/dev/null || true

CFG="$WORK/cfg.xml"
cat >"$CFG" <<EOF
<ThemisConfiguration><CompilerConfigurations Identifier="THEMISCompiler"><Item ext=".cpp" cmd="g++ -std=c++14 &quot;%NAME%%EXT%&quot; -pipe -O2 -x c++ -o&quot;%NAME%.exe&quot;|@WorkDir=%PATH%" /></CompilerConfigurations><Environment Identifier="E" ContestHouse="$HOUSE" /></ThemisConfiguration>
EOF

# settings template; placeholders replaced per scenario
XML='<ExamInformation Name="A" InputFile="A.INP" OutputFile="A.OUT" UseStdIn="%USESTDIN%" UseStdOut="%USESTDOUT%" EvaluatorName="%EVAL%" Mark="1" TimeLimit="%TL%" MemoryLimit="%MEM%"><TestCase Name="1" Mark="-1" TimeLimit="-1" MemoryLimit="-1"/></ExamInformation>'

OUT=""
settings() { # $1..: USESTDIN USESTDOUT EVAL TL MEM
  echo "$XML" | sed -e "s/%USESTDIN%/$1/" -e "s/%USESTDOUT%/$2/" \
    -e "s/%EVAL%/$3/" -e "s/%TL%/$4/" -e "s/%MEM%/$5/"
}

set_expected() { printf '3%s\n' "$EOL" >"$TESTS/A/1/A.OUT"; }

debug_bytes() {
  [ "${E2E_DEBUG:-0}" = 1 ] || return 0
  echo "    [dbg] expected A.OUT:"; od -c "$TESTS/A/1/A.OUT" | head -2
}

submit() { # $1=user ($2=source on stdin)
  mkdir -p "$SUBS/$1"
  cat >"$SUBS/$1/a.cpp"
}

run() { # $1=settings-file-content (optional; keeps existing file if omitted)
  if [ "$#" -gt 0 ] && [ -n "$1" ]; then
    printf '%s' "$1" >"$TESTS/A/Settings.cfg"
  fi
  OUT=$("$BIN" -s "$SUBS" -t "$TESTS" -j "$JDIR" -c "$CFG" -n 4 2>&1)
  if [ "${E2E_DEBUG:-0}" = 1 ]; then
    echo "$OUT" | grep -aE "\[dbg\]" | head -8
  fi
}

score_of() { # $1=user -> "<verdict> <score>"
  echo "$OUT" | grep -E "^$1 " | awk -F'|' '{n=split($2,a," "); print a[1] " " a[2]}' | head -1
}
check_score() {
  local got
  got=$(score_of "$1")
  if [ "$got" = "$2 $3" ]; then ok "$1 => $got"; else bad "$1 => '$got', want '$2 $3'"; fi
}

history_grep() { # $1=user  $2=extended-regex over history file bytes
  local hist="$SUBS/\$History"
  [ -d "$hist" ] || return 1
  local latest
  latest=$(ls -t "$hist" 2>/dev/null | grep "\[$1\]\[" | head -1)
  [ -n "$latest" ] || return 1
  grep -qE "$2" "$hist/$latest" 2>/dev/null
}

NO_RESULT_UTF8=$'Kh\xc3\xb4ng t\xc3\xacm th\xe1\xba\xa5y k\xe1\xba\xbft qu\xe1\xba\xa3'

C5=C5Binary.dll
[ "$WIN" = 0 ] && C5=libC5Binary.so
C1L=C1LinesWordsIgnoreCase.dll
[ "$WIN" = 0 ] && C1L=libC1LinesWordsIgnoreCase.so

printf '1 2\n' >"$TESTS/A/1/A.INP"
CORRECT='#include <cstdio>
int main(){int a,b;if(scanf("%d %d",&a,&b)!=2)return 1;printf("%d\n",a+b);return 0;}'

echo "== scenario 1: correct submission, stdin/stdout, C5Binary =="
for u in u1 u2 u3; do submit "$u" <<<"$CORRECT"; done
set_expected
debug_bytes
run "$(settings true true "$C5" 10 512)"
check_score u1 V 1
check_score u2 V 1
check_score u3 V 1

echo "== scenario 2: wrong answer =="
submit u1 <<'EOF'
#include <cstdio>
int main(){int a,b;if(scanf("%d %d",&a,&b)!=2)return 1;printf("%d\n",a+b+1);return 0;}
EOF
set_expected
run "$(settings true true "$C5" 10 512)"
check_score u1 V 0

echo "== scenario 3: time limit exceeded (1s) =="
submit u1 <<'EOF'
int main(){for(;;){}return 0;}
EOF
set_expected
run "$(settings true true "$C5" 1 512)"
check_score u1 V 0
if history_grep u1 'TLE'; then ok "TLE recorded in history"; else bad "TLE not recorded in history"; fi

echo "== scenario 4: runtime error (exit code 7) =="
submit u1 <<'EOF'
#include <cstdio>
int main(){int a,b;if(scanf("%d %d",&a,&b)!=2)return 1;return 7;}
EOF
set_expected
run "$(settings true true "$C5" 10 512)"
check_score u1 V 0
if history_grep u1 'exited with code 0x7'; then ok "exit code 0x7 recorded"; else bad "exit code not recorded"; fi

echo "== scenario 5: compile error => X =="
submit u1 <<'EOF'
this is not C++
EOF
set_expected
run "$(settings true true "$C5" 300 512)"
check_score u1 X 0

echo "== scenario 6: memory limit (256MB cap vs 1GB hog) =="
submit u1 <<'EOF'
#include <cstdlib>
int main(){volatile char*p=(char*)malloc(1024ull*1024*1024);if(!p)return 3;for(long i=0;i<1024ll*1024*1024;i+=4096)p[i]=1;return 0;}
EOF
set_expected
run "$(settings true true "$C5" 30 256)"
check_score u1 V 0

echo "== scenario 7: file I/O mode, missing result file =="
submit u1 <<'EOF'
#include <cstdio>
int main(){FILE*f=fopen("A.INP","r");int a,b;if(!f||fscanf(f,"%d %d",&a,&b)!=2)return 1;fclose(f);return 0;}
EOF
set_expected
run "$(settings false false "$C1L" 10 512)"
check_score u1 V 0
if history_grep u1 "$NO_RESULT_UTF8"; then
  ok "missing-file Vietnamese verdict recorded"
elif history_grep u1 'FAILED'; then
  ok "missing file reported as FAILED"
else
  bad "no verdict recorded for missing result file"
fi

echo "== scenario 8: file I/O correct answer via TOML settings =="
submit u1 <<EOF
#include <stdio.h>
int main(){FILE*f=fopen("A.INP","r");int a,b;if(!f||fscanf(f,"%d %d",&a,&b)!=2)return 1;fclose(f);FILE*g=fopen("A.OUT","w");fprintf(g,"3\\n");fclose(g);return 0;}
EOF
cat >"$TESTS/A/Settings.cfg" <<EOF
[ExamInformation]
Name = "A"
InputFile = "A.INP"
OutputFile = "A.OUT"
UseStdIn = false
UseStdOut = false
EvaluatorName = "$C1L"
Mark = 1
TimeLimit = 10
MemoryLimit = 512

[[ExamInformation.TestCase]]
Name = "1"
Mark = -1
TimeLimit = -1
MemoryLimit = -1
EOF
set_expected
run
check_score u1 V 1

if [ "${E2E_KEEP:-0}" = 1 ]; then
  echo "workdir kept: $WORK"
else
  rm -rf "$WORK"
fi

echo
echo "e2e: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
