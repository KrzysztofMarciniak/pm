/* gen.c - project configuration and file generation. */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pm_int.h"

static int in_list(const char *v, const char *const *list)
{
	for (; *list; list++)
		if (strcmp(v, *list) == 0)
			return 1;
	return 0;
}

static void copy(char *dst, size_t n, const char *src)
{
	snprintf(dst, n, "%s", src);
}

void pm_config_default(pm_config *cfg)
{
	memset(cfg, 0, sizeof *cfg);
	copy(cfg->lang, sizeof cfg->lang, "c");
	copy(cfg->std, sizeof cfg->std, "c99");
	copy(cfg->build, sizeof cfg->build, "make");
	copy(cfg->license, sizeof cfg->license, "MIT");
	copy(cfg->test_fw, sizeof cfg->test_fw, "none");
	copy(cfg->kind, sizeof cfg->kind, "exe");
	copy(cfg->libc, sizeof cfg->libc, "system");
	cfg->ci = 1;
	cfg->lint = 1;
}

static int set_bool(pm_ctx *c, const char *key, int on)
{
	return pm_set(c, key, on ? "1" : "");
}

/* Fills toolchain-dependent variables. */
static int apply_toolchain(pm_ctx *c, const pm_config *cfg, const char *cc)
{
	char std_flag[40];
	const char *warn = "-Wall -Wextra -Wpedantic";
	const char *debug = "-g -O0";
	const char *asan = "-fsanitize=address,undefined -g -O0";
	const char *asan_ld = "-fsanitize=address,undefined";
	const char *dep = "-MMD -MP";

	snprintf(std_flag, sizeof std_flag, "-std=%s", cfg->std);
	if (pm_set(c, "LIBC_CFLAGS", "") || pm_set(c, "LIBC_LDFLAGS", "") ||
	    pm_set(c, "LIBC_LIBS", "") || pm_set(c, "LIBC_CRT", "") ||
	    pm_set(c, "CC", cc) || pm_set(c, "FLAVOR", "cc") ||
	    pm_set(c, "STD_FLAG", std_flag) || pm_set(c, "WARN_FLAGS", warn) ||
	    pm_set(c, "DEBUG_FLAGS", debug) || pm_set(c, "ASAN_FLAGS", asan) ||
	    pm_set(c, "ASAN_LDFLAGS", asan_ld) || pm_set(c, "DEP_FLAGS", dep) ||
	    set_bool(c, "HAS_ASAN", 1) || set_bool(c, "HAS_DEPS", 1))
		return -1;
	return 0;
}

int pm_config_apply(pm_ctx *c, const pm_config *cfg)
{
	static const char *const langs[] = {"c", "c++", NULL};
	static const char *const c_std[] = {"c99", "c11", "c17", "c23", NULL};
	static const char *const cxx_std[] = {"c++11", "c++14", "c++17", "c++20", "c++23", NULL};
	static const char *const builds[] = {"make", "cmake", "sh", "configure", "autoconf", NULL};
	static const char *const licenses[] = {
		"MIT", "Apache-2.0", "GPL-2.0-only", "GPL-3.0", "GPL-3.0-only",
		"LGPL-2.1-only", "LGPL-3.0-only", "BSD-2-Clause", "BSD-3-Clause",
		"ISC", "MPL-2.0", "CC0-1.0", "Unlicense", "Zlib", "None", NULL
	};
	static const char *const kinds[] = {"exe", "lib", "both", NULL};
		static const char *const c_fw[] = {"none", "Unity", "cmocka", "criterion", NULL};
	static const char *const cxx_fw[] = {"none", "Catch2", "GoogleTest", "doctest", NULL};

	char canon[160], upper[160], year[16], cc[128], build[16];
	char found[4][128];
	const char *p, *test_libs = "";
		int is_c, i;
	time_t now;

	c->err[0] = '\0';
	if (!cfg->name[0]) {
		pm_seterr(c, "project name is empty");
		return -1;
	}
	for (p = cfg->name; *p; p++)
		if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.')) {
			pm_seterr(c, "project name '%s': only letters, digits, _ - . allowed", cfg->name);
			return -1;
		}
	if (!in_list(cfg->lang, langs)) {
		pm_seterr(c, "unknown language '%s' (c, c++)", cfg->lang);
		return -1;
	}
	is_c = strcmp(cfg->lang, "c") == 0;
	if (!in_list(cfg->std, is_c ? c_std : cxx_std)) {
		pm_seterr(c, "standard '%s' is not valid for %s", cfg->std, cfg->lang);
		return -1;
	}
	copy(build, sizeof build, strcmp(cfg->build, "posix-build.sh") == 0 ? "sh" : cfg->build);
	if (!in_list(build, builds)) {
		pm_seterr(c, "unknown build system '%s' (make cmake sh configure autoconf)", cfg->build);
		return -1;
	}
	if (!in_list(cfg->license, licenses)) {
		pm_seterr(c, "unknown license '%s'", cfg->license);
		return -1;
	}
	if (!in_list(cfg->test_fw, is_c ? c_fw : cxx_fw)) {
		pm_seterr(c, "test framework '%s' is not valid for %s", cfg->test_fw, cfg->lang);
		return -1;
	}
	if (!in_list(cfg->kind, kinds)) {
		pm_seterr(c, "unknown kind '%s' (exe lib both)", cfg->kind);
		return -1;
	}
	if (strcmp(cfg->libc, "system") != 0) {
		pm_seterr(c, "only the system C library is supported");
		return -1;
	}

	/* compiler: explicit, else first one found (local prefix first) */
	if (cfg->cc[0]) {
		copy(cc, sizeof cc, cfg->cc);
	} else if (pm_find_compilers(cfg->lang, found, 4) > 0) {
		copy(cc, sizeof cc, found[0]);
	} else {
		copy(cc, sizeof cc, is_c ? "cc" : "c++");
	}
	if (pm_cc_detect(cc) == PM_CC_UNKNOWN) {
		pm_seterr(c, "invalid compiler command '%s'", cc);
		return -1;
	}

	/* derived names */
	for (i = 0; cfg->name[i] && i < (int)sizeof canon - 2; i++) {
		unsigned char ch = (unsigned char)cfg->name[i];
		canon[i] = isalnum(ch) ? (char)ch : '_';
		upper[i] = (char)toupper((unsigned char)canon[i]);
	}
	canon[i] = upper[i] = '\0';
	if (isdigit((unsigned char)canon[0])) {
		memmove(canon + 1, canon, strlen(canon) + 1);
		memmove(upper + 1, upper, strlen(upper) + 1);
		canon[0] = upper[0] = '_';
	}
	now = time(NULL);
	strftime(year, sizeof year, "%Y", localtime(&now));

	if (pm_set(c, "NAME", cfg->name) || pm_set(c, "NAME_CANON", canon) ||
	    pm_set(c, "NAME_UPPER", upper) ||
	    pm_set(c, "AUTHOR", cfg->author[0] ? cfg->author : "Your Name") ||
	    pm_set(c, "YEAR", year) || pm_set(c, "LANG", cfg->lang) ||
	    pm_set(c, "STD", cfg->std) || pm_set(c, "SRC_EXT", is_c ? "c" : "cpp") ||
	    pm_set(c, "HDR_EXT", is_c ? "h" : "hpp") ||
	    pm_set(c, "FLAGS_VAR", is_c ? "CFLAGS" : "CXXFLAGS") ||
	    pm_set(c, "CMAKE_LANG", is_c ? "C" : "CXX") ||
	    pm_set(c, "CMAKE_STD_VAR", is_c ? "C" : "CXX") ||
	    pm_set(c, "CMAKE_STD_NUM", strpbrk(cfg->std, "0123456789")) ||
	    pm_set(c, "BUILD", build) || pm_set(c, "LICENSE", cfg->license) ||
	    pm_set(c, "TEST_FW", cfg->test_fw) || pm_set(c, "KIND", cfg->kind) ||
	    pm_set(c, "LIBC", cfg->libc) || pm_set(c, "TOOL_VERSION", PM_VERSION) ||
	    set_bool(c, "IS_C", is_c) || set_bool(c, "IS_CXX", !is_c) ||
	    set_bool(c, "HAS_EXE", strcmp(cfg->kind, "lib") != 0) ||
	    set_bool(c, "HAS_LIB", strcmp(cfg->kind, "exe") != 0) ||
	    set_bool(c, "HAS_TESTS", strcmp(cfg->test_fw, "none") != 0) ||
	    set_bool(c, "USE_CI", cfg->ci) || set_bool(c, "USE_LINT", cfg->lint) ||
	    set_bool(c, "HAS_LICENSE", strcmp(cfg->license, "None") != 0) ||
	    set_bool(c, "BUILD_MAKE", strcmp(build, "make") == 0) ||
	    set_bool(c, "BUILD_CMAKE", strcmp(build, "cmake") == 0) ||
	    set_bool(c, "BUILD_SH", strcmp(build, "sh") == 0) ||
	    set_bool(c, "BUILD_CONFIGURE", strcmp(build, "configure") == 0) ||
	    set_bool(c, "BUILD_AUTOCONF", strcmp(build, "autoconf") == 0) ||
	    pm_set(c, "PM_PREFIX", "/usr/local"))
		return -1;

	if (strcmp(cfg->test_fw, "cmocka") == 0)
		test_libs = "-lcmocka";
	else if (strcmp(cfg->test_fw, "criterion") == 0)
		test_libs = "-lcriterion";
	else if (strcmp(cfg->test_fw, "Catch2") == 0)
		test_libs = "-lCatch2Main -lCatch2";
	else if (strcmp(cfg->test_fw, "GoogleTest") == 0)
		test_libs = "-lgtest -lpthread";
	if (pm_set(c, "TEST_LIBS", test_libs))
		return -1;

	if (strcmp(build, "sh") == 0 || strcmp(build, "configure") == 0)
		p = strcmp(build, "sh") == 0 ? "./build.sh" : "make";
	else if (strcmp(build, "cmake") == 0)
		p = "cmake --build build";
	else
		p = "make";
	if (pm_set(c, "BUILD_CMD", p))
		return -1;

	/* editors want both std fields filled in */
	if (pm_set(c, "VSCODE_C_STD", is_c ? cfg->std : "c17") ||
	    pm_set(c, "VSCODE_CPP_STD", is_c ? "c++17" : cfg->std) ||
	    pm_set(c, "VSCODE_LANG_ID", is_c ? "c" : "cpp"))
		return -1;

	if (apply_toolchain(c, cfg, cc) != 0) {
		pm_seterr(c, "out of memory");
		return -1;
	}
	return 0;
}

/* ---- generation ------------------------------------------------------- */

typedef struct {
	pm_ctx *c;
	const char *dir;
} gen;

static int emit(gen *g, const char *tmpl, const char *rel, int exec)
{
	char dst[1100];
	char *exp;
	int n;

	/* destination paths may contain tokens (e.g. @@NAME@@.sublime-project) */
	exp = pm_expand(g->c, rel);
	if (!exp)
		return -1;
	n = snprintf(dst, sizeof dst, "%s/%s", g->dir, exp);
	free(exp);
	if (n < 0 || (size_t)n >= sizeof dst) {
		pm_seterr(g->c, "path too long");
		return -1;
	}
	return pm_render(g->c, tmpl, dst, exec);
}

static int emit_dir(gen *g, const char *rel)
{
	char dst[1100];
	snprintf(dst, sizeof dst, "%s/%s", g->dir, rel);
	if (pm_mkdirp(dst) != 0) {
		pm_seterr(g->c, "cannot create directory '%s'", dst);
		return -1;
	}
	return 0;
}

#define TRY(x)                                                                 \
	do {                                                                   \
		if ((x) != 0)                                                  \
			return -1;                                             \
	} while (0)

int pm_generate(pm_ctx *c, const pm_config *cfg, const char *dir)
{
	gen g;
	char nm[64];
	const char *ext, *hext, *kind, *build, *fw;
	int is_c;

	if (pm_config_apply(c, cfg) != 0)
		return -1;
	g.c = c;
	g.dir = dir && *dir ? dir : ".";
	is_c = strcmp(cfg->lang, "c") == 0;
	ext = is_c ? "c" : "cpp";
	hext = is_c ? "h" : "hpp";
	kind = cfg->kind;
	build = pm_get(c, "BUILD");
	fw = cfg->test_fw;

	TRY(pm_mkdirp(g.dir));
	TRY(emit_dir(&g, "src"));
	{
		char *inc = pm_expand(c, "include/@@NAME@@");
		int rc;
		if (!inc)
			return -1;
		rc = emit_dir(&g, inc);
		free(inc);
		TRY(rc);
	}
	TRY(emit_dir(&g, "examples"));
	TRY(emit_dir(&g, "docs"));
	TRY(emit_dir(&g, "build"));

	/* sources by kind */
	if (strcmp(kind, "exe") == 0) {
		snprintf(nm, sizeof nm, "main.%s", ext);
		TRY(emit(&g, nm, "src/main.@@SRC_EXT@@", 0));
	} else {
		snprintf(nm, sizeof nm, "lib.%s", ext);
		TRY(emit(&g, nm, "src/@@NAME@@.@@SRC_EXT@@", 0));
		snprintf(nm, sizeof nm, "lib.%s", hext);
		TRY(emit(&g, nm, "include/@@NAME@@/@@NAME@@.@@HDR_EXT@@", 0));
		if (strcmp(kind, "both") == 0) {
			snprintf(nm, sizeof nm, "mainlib.%s", ext);
			TRY(emit(&g, nm, "src/main.@@SRC_EXT@@", 0));
		}
	}

	TRY(emit(&g, "format.sh", "scripts/format.sh", 1));
	TRY(emit(&g, "lint.sh", "scripts/lint.sh", 1));

	if (strcmp(build, "make") == 0) {
		TRY(emit(&g, "Makefile", "Makefile", 0));
	} else if (strcmp(build, "cmake") == 0) {
		TRY(emit(&g, "CMakeLists.txt", "CMakeLists.txt", 0));
	} else if (strcmp(build, "sh") == 0) {
		TRY(emit(&g, "build.sh", "build.sh", 1));
	} else if (strcmp(build, "configure") == 0) {
		TRY(emit(&g, "configure", "configure", 1));
		TRY(emit(&g, "Makefile", "Makefile", 0));
	} else {
		TRY(emit(&g, "configure.ac", "configure.ac", 0));
		TRY(emit(&g, "Makefile.am", "Makefile.am", 0));
		TRY(emit(&g, "bootstrap", "bootstrap", 1));
	}

	TRY(emit(&g, "gitignore", ".gitignore", 0));
	TRY(emit(&g, "README.md", "README.md", 0));

	/* editor / LSP files */
	TRY(emit(&g, "exrc", ".exrc", 0));
	TRY(emit(&g, "vimrc", ".vimrc", 0));
	TRY(emit(&g, "dir-locals.el", ".dir-locals.el", 0));
	TRY(emit(&g, "vscode/settings.json", ".vscode/settings.json", 0));
	TRY(emit(&g, "vscode/c_cpp_properties.json", ".vscode/c_cpp_properties.json", 0));
	TRY(emit(&g, "vscode/tasks.json", ".vscode/tasks.json", 0));
	TRY(emit(&g, "sublime-project", "@@NAME@@.sublime-project", 0));
	TRY(emit(&g, "compile_flags.txt", "compile_flags.txt", 0));

	if (strcmp(cfg->license, "None") != 0) {
		snprintf(nm, sizeof nm, "licenses/%s", cfg->license);
		TRY(emit(&g, nm, "LICENSE", 0));
	}
	if (strcmp(fw, "none") != 0) {
		/* template names are lower-case: tests/unity.c, tests/catch2.cpp ... */
		char low[32];
		size_t i;
		for (i = 0; fw[i] && i < sizeof low - 1; i++)
			low[i] = (char)tolower((unsigned char)fw[i]);
		low[i] = '\0';
		snprintf(nm, sizeof nm, "tests/%s.%s", low, ext);
		TRY(emit(&g, nm, "tests/test_main.@@SRC_EXT@@", 0));
	}
	if (cfg->ci)
		TRY(emit(&g, "workflow.yml", ".github/workflows/ci.yml", 0));
	if (cfg->lint) {
		TRY(emit(&g, "clang-format", ".clang-format", 0));
		TRY(emit(&g, "clang-tidy", ".clang-tidy", 0));
		TRY(emit(&g, "editorconfig", ".editorconfig", 0));
	}
	return 0;
}
