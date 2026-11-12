#!/bin/sh
# tests/scaffold.sh - scaffolds throwaway projects for (language x build system
# x kind), builds them, runs them, and checks the output. Also covers install,
# the generated test wiring, PM_TEMPLATE, and pack/license handling.
#
# usage: tests/scaffold.sh path/to/pm [--keep]

set -u

PM="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
KEEP=0
[ "${2:-}" = "--keep" ] && KEEP=1

TMP="$(mktemp -d "${TMPDIR:-/tmp}/pm_test.XXXXXX")"
PASS=0
FAIL=0
SKIP=0

have() { command -v "$1" >/dev/null 2>&1; }

report() { # status name [detail-file]
  printf '%-5s %s\n' "$1" "$2"
  if [ "$1" = FAIL ] && [ -n "${3:-}" ]; then tail -15 "$3" | sed 's/^/        /'; fi
  case "$1" in PASS) PASS=$((PASS + 1)) ;; FAIL) FAIL=$((FAIL + 1)) ;; SKIP) SKIP=$((SKIP + 1)) ;; esac
}

# build_cmd BUILD -> shell command that builds the project in cwd
build_cmd() {
  case "$1" in
    make) echo "make" ;;
    sh) echo "sh build.sh" ;;
    configure) echo "sh ./configure && make" ;;
    cmake) echo "cmake -S . -B build && cmake --build build" ;;
    autoconf) echo "./bootstrap && ./configure && make" ;;
  esac
}

needs() { # BUILD -> missing tools
  case "$1" in
    make | configure) have make || echo make ;;
    cmake) have cmake || echo cmake ;;
    autoconf) have autoreconf || echo autoreconf ;;
  esac
}

# run_case LANG BUILD KIND [extra pm args...]
run_case() {
  lang="$1"; build="$2"; kind="$3"; shift 3
  tag=$(echo "$lang" | sed 's/c++/cxx/')
  name="t_${build}_${tag}_${kind}"
  label="$name $*"
  dir="$TMP/$name"
  log="$TMP/$name.log"

  cc=cc; [ "$lang" = "c++" ] && cc=c++
  miss="$(needs "$build")"; have "$cc" || miss="$miss $cc"
  if [ -n "$miss" ]; then report SKIP "$label (missing:$miss)"; return; fi

  if ! "$PM" -y --name="$name" --lang="$lang" --build="$build" --kind="$kind" \
      --no-ci --no-lint "$@" "$dir" >"$log" 2>&1; then
    report FAIL "$label (scaffold)" "$log"; return
  fi
  if ! (cd "$dir" && eval "$(build_cmd "$build")") >>"$log" 2>&1; then
    report FAIL "$label (build)" "$log"; return
  fi
  case "$kind" in
    lib)
      if [ ! -f "$dir/build/lib$name.a" ]; then report FAIL "$label (no lib$name.a)" "$log"; return; fi
      ;;
    *)
      out="$("$dir/build/$name" 2>&1)"
      case "$out" in
        *"Hello from $name"*) ;;
        *) echo "$out" >>"$log"; report FAIL "$label (wrong output)" "$log"; return ;;
      esac
      [ "$kind" = both ] && [ ! -f "$dir/build/lib$name.a" ] &&
        { report FAIL "$label (no lib$name.a)" "$log"; return; }
      ;;
  esac
  report PASS "$label"
}

echo "==> pm: $PM"
# ---- CLI smoke tests ----------------------------------------------------
if "$PM" --version | grep -q '2.0.1'; then
  report PASS "--version"
else
  report FAIL "--version"
fi
if "$PM" --help | grep -q -- '--template'; then
  report PASS "--help"
else
  report FAIL "--help"
fi
if "$PM" --list-templates | grep -q '^main.c$'; then
  report PASS "--list-templates"
else
  report FAIL "--list-templates"
fi
for lang in c c++; do
  for build in make sh configure cmake autoconf; do
    for kind in exe lib both; do
      run_case "$lang" "$build" "$kind"
    done
  done
done

# ---- install into a local prefix --------------------------------------
for build in make sh; do
  name="inst_$build"
  dir="$TMP/$name"; log="$TMP/$name.log"
  "$PM" -y --name="$name" --build="$build" --kind=both --no-ci --no-lint "$dir" >"$log" 2>&1
  if [ "$build" = make ]; then
    (cd "$dir" && make >/dev/null 2>&1 && make install PREFIX="$TMP/pm" >>"$log" 2>&1)
  else
    (cd "$dir" && PREFIX="$TMP/pm" sh build.sh install >>"$log" 2>&1)
  fi
  if [ -x "$TMP/pm/bin/$name" ] && [ -f "$TMP/pm/lib/lib$name.a" ] &&
     [ -f "$TMP/pm/include/$name/$name.h" ]; then
    report PASS "install $build -> prefix (bin, lib, include)"
  else
    report FAIL "install $build" "$log"
  fi
done

# ---- all bundled license templates ------------------------------------
license_log="$TMP/licenses.log"
license_dir="$TMP/licenses"
mkdir -p "$license_dir"
license_ok=1
for license_id in MIT Apache-2.0 GPL-2.0-only GPL-3.0 GPL-3.0-only \
    LGPL-2.1-only LGPL-3.0-only BSD-2-Clause BSD-3-Clause ISC MPL-2.0 \
    CC0-1.0 Unlicense Zlib None; do
  dir="$license_dir/$license_id"
  if ! "$PM" -y --name=license_check --license="$license_id" --no-ci --no-lint \
      "$dir" >>"$license_log" 2>&1; then
    license_ok=0
    break
  fi
  if [ "$license_id" = None ]; then
    [ ! -e "$dir/LICENSE" ] || license_ok=0
  else
    [ -s "$dir/LICENSE" ] || license_ok=0
  fi
done
if [ "$license_ok" -eq 1 ]; then
  report PASS "all bundled license templates"
else
  report FAIL "license template generation" "$license_log"
fi

# ---- generated test wiring (fake Unity so no framework is needed) ------
for build in make sh cmake; do
  have "$build" || [ "$build" = sh ] || { report SKIP "tests wiring $build (missing $build)"; continue; }
  name="tw_$build"
  dir="$TMP/$name"; log="$TMP/$name.log"
  "$PM" -y --name="$name" --build="$build" --kind=both --test=Unity --no-ci --no-lint "$dir" >"$log" 2>&1
  mkdir -p "$dir/tests/vendor"
  cat >"$dir/tests/vendor/unity.h" <<'EOF'
#ifndef UNITY_H
#define UNITY_H
extern int unity_fail;
void setUp(void);
void tearDown(void);
#define UNITY_BEGIN() 0
#define UNITY_END() (unity_fail)
#define RUN_TEST(f) do { setUp(); f(); tearDown(); } while (0)
#define TEST_ASSERT_EQUAL_INT(e, a) do { if ((e) != (a)) unity_fail = 1; } while (0)
#endif
EOF
  printf 'int unity_fail = 0;\n' >"$dir/tests/vendor/unity.c"
  case "$build" in
    make) cmd="make test" ;;
    sh) cmd="sh build.sh test" ;;
    cmake) cmd="cmake -S . -B build && cmake --build build && ctest --test-dir build" ;;
  esac
  if (cd "$dir" && eval "$cmd") >>"$log" 2>&1; then
    report PASS "tests wiring $build (tests link the library)"
  else
    report FAIL "tests wiring $build" "$log"
  fi
done

# ---- PM_TEMPLATE override ------------------------------------------
cat >"$TMP/custom.pack" <<'EOF'
%%% main.c
#include <stdio.h>
int main(void) { puts("custom-main @@NAME@@"); return 0; }
EOF
dir="$TMP/custom"; log="$TMP/custom.log"
PM_TEMPLATE="$TMP/custom.pack" "$PM" -y --name=custom --no-ci --no-lint "$dir" >"$log" 2>&1
if (cd "$dir" && make >/dev/null 2>&1) && [ "$("$dir/build/custom")" = "custom-main custom" ]; then
  report PASS "PM_TEMPLATE overrides a built-in template"
else
  report FAIL "PM_TEMPLATE override" "$log"
fi

# a broken pack must be reported, not ignored
if PM_TEMPLATE="$TMP/nope.pack" "$PM" -y --name=x "$TMP/x" >"$log" 2>&1; then
  report FAIL "missing PM_TEMPLATE file should be an error" "$log"
else
  report PASS "missing PM_TEMPLATE file is an error"
fi

# dump -> reload round trip must be byte-identical
"$PM" --dump-templates >"$TMP/dump.pack" 2>/dev/null
PM_TEMPLATE="$TMP/dump.pack" "$PM" --dump-templates >"$TMP/dump2.pack" 2>/dev/null
if cmp -s "$TMP/dump.pack" "$TMP/dump2.pack" && [ -s "$TMP/dump.pack" ]; then
  report PASS "--dump-templates round trip"
else
  report FAIL "--dump-templates round trip"
fi


