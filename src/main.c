/* main.c - the `pm` command line tool. Thin wrapper around libpm. */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pm/pm.h"

enum {
	G_NAME = 1 << 0, G_AUTHOR = 1 << 1, G_LANG = 1 << 2, G_STD = 1 << 3,
	G_BUILD = 1 << 4, G_LICENSE = 1 << 5, G_TEST = 1 << 6, G_KIND = 1 << 7,
	G_CC = 1 << 8, G_CI = 1 << 10, G_LINT = 1 << 11
};

static void usage(FILE *o)
{
	fputs("Usage: pm [options] [dir]\n"
	      "\n"
	      "Scaffolds a C/C++ project in dir (default: current directory).\n"
	      "Anything not given as an option is asked interactively, unless -y.\n"
	      "\n"
	      "  -n, --name NAME        project name (default: directory name)\n"
	      "  -a, --author NAME      author for the license\n"
	      "  -l, --lang c|c++\n"
	      "  -s, --std STD          c99 c11 c17 c23 | c++11 c++14 c++17 c++20 c++23\n"
	      "  -k, --kind exe|lib|both  executable, static library, or library + thin main\n"
	      "  -b, --build make|cmake|sh|configure|autoconf\n"
	      "  -c, --cc CMD           compiler (cc or a path)\n"
	      ""
	      "  -L, --license ID       SPDX license identifier (see README)\n"
	      "  -t, --test FW          none Unity cmocka criterion | Catch2 GoogleTest doctest\n"
	      "      --ci / --no-ci     GitHub Actions workflow\n"
	      "      --lint / --no-lint clang-format, clang-tidy, .editorconfig\n"
	      "  -T, --template FILE    template pack (same as PM_TEMPLATE=FILE)\n"
	      "  -y, --yes              no questions, use defaults for the rest\n"
	      "      --dump-templates   print the active template pack and exit\n"
	      "      --list-templates   print template names and exit\n"
	      "      --list-compilers   print compilers found and exit\n"
	      "  -V, --version\n"
	      "  -h, --help\n"
	      "\n"
	      "Environment:\n"
	      "  PM_TEMPLATE   template pack (file) or directory overriding built-ins\n"
	      "  PM_PREFIX     compiler lookup prefix, default /usr/local\n"
	      "",
	      o);
}

/* ---- prompts ---------------------------------------------------------- */

static int read_line(char *buf, size_t n)
{
	size_t len;
	if (!fgets(buf, (int)n, stdin))
		return 0;
	len = strlen(buf);
	while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	return 1;
}

static void ask(const char *prompt, const char *def, char *out, size_t n)
{
	char line[256];
	printf("%s [%s]: ", prompt, def);
	fflush(stdout);
	if (!read_line(line, sizeof line) || !line[0])
		snprintf(out, n, "%s", def);
	else
		snprintf(out, n, "%.*s", (int)n - 1, line);
}

/* Returns the chosen index. Empty / bad / EOF input picks entry 0. */
static size_t choose(const char *prompt, const char *const *opts,
		     const char *const *desc, size_t n)
{
	char line[64];
	size_t i;
	long sel;

	printf("%s\n", prompt);
	for (i = 0; i < n; i++) {
		if (desc && desc[i])
			printf("  %lu) %-12s %s\n", (unsigned long)i + 1, opts[i], desc[i]);
		else
			printf("  %lu) %s\n", (unsigned long)i + 1, opts[i]);
	}
	printf("Choice [1]: ");
	fflush(stdout);
	if (!read_line(line, sizeof line) || !line[0])
		return 0;
	sel = strtol(line, NULL, 10);
	return sel >= 1 && (size_t)sel <= n ? (size_t)sel - 1 : 0;
}

static int header_exists(const char *h)
{
	static const char *const dirs[] = {"/usr/include", "/usr/local/include",
					   "/opt/homebrew/include", NULL};
	char pm[512], p[1100];
	size_t i;

	for (i = 0; dirs[i]; i++) {
		snprintf(p, sizeof p, "%s/%s", dirs[i], h);
		if (access(p, R_OK) == 0)
			return 1;
	}
	snprintf(p, sizeof p, "%s/include/%s", pm_prefix(pm, sizeof pm), h);
	return access(p, R_OK) == 0;
}

static int fw_available(const char *fw)
{
	if (strcmp(fw, "none") == 0)
		return 1;
	if (strcmp(fw, "Unity") == 0)
		return header_exists("unity.h") || header_exists("unity/unity.h");
	if (strcmp(fw, "cmocka") == 0)
		return header_exists("cmocka.h");
	if (strcmp(fw, "criterion") == 0)
		return header_exists("criterion/criterion.h");
	if (strcmp(fw, "Catch2") == 0)
		return header_exists("catch2/catch_all.hpp");
	if (strcmp(fw, "GoogleTest") == 0)
		return header_exists("gtest/gtest.h");
	if (strcmp(fw, "doctest") == 0)
		return header_exists("doctest/doctest.h") || header_exists("doctest.h");
	return 1;
}

/* ---- main ------------------------------------------------------------- */

static int want_value(int argc, char **argv, int *i, const char *arg, const char *opt,
		      const char **val)
{
	size_t ol = strlen(opt);
	if (strncmp(arg, opt, ol) != 0)
		return 0;
	if (arg[ol] == '=') {
		*val = arg + ol + 1;
		return 1;
	}
	if (arg[ol] == '\0') {
		if (*i + 1 >= argc) {
			fprintf(stderr, "pm: option %s needs a value\n", opt);
			exit(2);
		}
		*val = argv[++*i];
		return 1;
	}
	return 0;
}

static void setstr(char *dst, size_t n, const char *v, unsigned *given, unsigned bit)
{
	snprintf(dst, n, "%s", v);
	*given |= bit;
}

int main(int argc, char **argv)
{
	pm_ctx *c;
	pm_config cfg;
	unsigned given = 0;
	int yes = 0, i;
	int do_dump = 0, do_list = 0, do_cc = 0;
	const char *dir = ".", *tmpl = NULL, *v;
	char defname[128], cwd[1024];
	const char *base;

	pm_config_default(&cfg);

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("pm %s\n", PM_VERSION);
			return 0;
		} else if (!strcmp(a, "-y") || !strcmp(a, "--yes")) {
			yes = 1;
		} else if (!strcmp(a, "--ci")) {
			cfg.ci = 1; given |= G_CI;
		} else if (!strcmp(a, "--no-ci")) {
			cfg.ci = 0; given |= G_CI;
		} else if (!strcmp(a, "--lint")) {
			cfg.lint = 1; given |= G_LINT;
		} else if (!strcmp(a, "--no-lint")) {
			cfg.lint = 0; given |= G_LINT;
		} else if (!strcmp(a, "--dump-templates")) {
			do_dump = 1;
		} else if (!strcmp(a, "--list-templates")) {
			do_list = 1;
		} else if (!strcmp(a, "--list-compilers")) {
			do_cc = 1;
		} else if (want_value(argc, argv, &i, a, "--name", &v) || want_value(argc, argv, &i, a, "-n", &v)) {
			setstr(cfg.name, sizeof cfg.name, v, &given, G_NAME);
		} else if (want_value(argc, argv, &i, a, "--author", &v) || want_value(argc, argv, &i, a, "-a", &v)) {
			setstr(cfg.author, sizeof cfg.author, v, &given, G_AUTHOR);
		} else if (want_value(argc, argv, &i, a, "--lang", &v) || want_value(argc, argv, &i, a, "-l", &v)) {
			setstr(cfg.lang, sizeof cfg.lang, v, &given, G_LANG);
		} else if (want_value(argc, argv, &i, a, "--std", &v) || want_value(argc, argv, &i, a, "-s", &v)) {
			setstr(cfg.std, sizeof cfg.std, v, &given, G_STD);
		} else if (want_value(argc, argv, &i, a, "--kind", &v) || want_value(argc, argv, &i, a, "-k", &v)) {
			setstr(cfg.kind, sizeof cfg.kind, v, &given, G_KIND);
		} else if (want_value(argc, argv, &i, a, "--build", &v) || want_value(argc, argv, &i, a, "-b", &v)) {
			setstr(cfg.build, sizeof cfg.build, v, &given, G_BUILD);
		} else if (want_value(argc, argv, &i, a, "--cc", &v) || want_value(argc, argv, &i, a, "-c", &v)) {
			setstr(cfg.cc, sizeof cfg.cc, v, &given, G_CC);
		} else if (want_value(argc, argv, &i, a, "--license", &v) || want_value(argc, argv, &i, a, "-L", &v)) {
			setstr(cfg.license, sizeof cfg.license, v, &given, G_LICENSE);
		} else if (want_value(argc, argv, &i, a, "--test", &v) || want_value(argc, argv, &i, a, "-t", &v)) {
			setstr(cfg.test_fw, sizeof cfg.test_fw, v, &given, G_TEST);
		} else if (want_value(argc, argv, &i, a, "--template", &v) || want_value(argc, argv, &i, a, "-T", &v)) {
			tmpl = v;
		} else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "pm: unknown option '%s' (try --help)\n", a);
			return 2;
		} else {
			dir = a;
		}
	}

	c = pm_new();
	if (!c) {
		fprintf(stderr, "pm: out of memory\n");
		return 1;
	}
	if (pm_error(c)[0]) {
		fprintf(stderr, "pm: %s\n", pm_error(c));
		pm_free(c);
		return 1;
	}
	if (tmpl && pm_load_pack(c, tmpl) != 0) {
		fprintf(stderr, "pm: %s\n", pm_error(c));
		pm_free(c);
		return 1;
	}

	if (do_dump) {
		int rc = pm_dump_pack(c, stdout);
		pm_free(c);
		return rc == 0 ? 0 : 1;
	}
	if (do_list) {
		size_t k;
		for (k = 0; k < pm_template_count(c); k++)
			puts(pm_template_name(c, k));
		pm_free(c);
		return 0;
	}
	if (do_cc) {
		char cmds[8][128], pm[512];
		size_t k, n;
		printf("prefix: %s\n", pm_prefix(pm, sizeof pm));
		n = pm_find_compilers("c", cmds, 8);
		for (k = 0; k < n; k++)
			printf("c    %-28s %s\n", cmds[k],
			       strcmp(cmds[k], "cc") == 0 ? "recommended" : "compiler");
		n = pm_find_compilers("c++", cmds, 8);
		for (k = 0; k < n; k++)
			printf("c++  %-28s %s\n", cmds[k], "compiler");
		pm_free(c);
		return 0;
	}

	/* default project name = last path component of dir (or cwd) */
	if (strcmp(dir, ".") == 0) {
		if (!getcwd(cwd, sizeof cwd))
			snprintf(cwd, sizeof cwd, "project");
	} else {
		snprintf(cwd, sizeof cwd, "%s", dir);
	}
	{
		size_t len = strlen(cwd);
		while (len > 1 && cwd[len - 1] == '/')
			cwd[--len] = '\0';
	}
	base = strrchr(cwd, '/');
	snprintf(defname, sizeof defname, "%.127s", base && base[1] ? base + 1 : cwd);

	if (!(given & G_NAME)) {
		if (yes)
			snprintf(cfg.name, sizeof cfg.name, "%s", defname);
		else
			ask("Project name", defname, cfg.name, sizeof cfg.name);
	}
	if (!(given & G_AUTHOR)) {
		char who[128] = "";
		if (pm_capture("git config user.name 2>/dev/null", who, sizeof who) == 0) {
			size_t n = strlen(who);
			while (n && (who[n - 1] == '\n' || who[n - 1] == '\r'))
				who[--n] = '\0';
		} else {
			who[0] = '\0';
		}
		if (!who[0])
			snprintf(who, sizeof who, "Your Name");
		if (yes)
			snprintf(cfg.author, sizeof cfg.author, "%s", who);
		else
			ask("Author name", who, cfg.author, sizeof cfg.author);
	}
	if (!(given & G_LANG) && !yes) {
		static const char *const o[] = {"c", "c++"};
		snprintf(cfg.lang, sizeof cfg.lang, "%s", o[choose("Language:", o, NULL, 2)]);
	}
	if (!(given & G_STD)) {
		static const char *const cs[] = {"c99", "c11", "c17", "c23"};
		static const char *const xs[] = {"c++11", "c++14", "c++17", "c++20", "c++23"};
		int is_c = strcmp(cfg.lang, "c") == 0;
		if (yes)
			snprintf(cfg.std, sizeof cfg.std, "%s", is_c ? "c99" : "c++17");
		else if (is_c)
			snprintf(cfg.std, sizeof cfg.std, "%s", cs[choose("C standard:", cs, NULL, 4)]);
		else
			snprintf(cfg.std, sizeof cfg.std, "%s", xs[choose("C++ standard:", xs, NULL, 5)]);
	}
	if (!(given & G_KIND) && !yes) {
		static const char *const o[] = {"exe", "lib", "both"};
		static const char *const d[] = {"program only",
						"static library, internals exposed via include/<name>/",
						"library + thin main (tests link the library)"};
		snprintf(cfg.kind, sizeof cfg.kind, "%s", o[choose("Project kind:", o, d, 3)]);
	}
	if (!(given & G_BUILD) && !yes) {
		static const char *const o[] = {"make", "cmake", "sh", "configure", "autoconf"};
		static const char *const d[] = {NULL, NULL, "POSIX build.sh, no make needed",
						"hand-written ./configure + make", "autotools"};
		snprintf(cfg.build, sizeof cfg.build, "%s", o[choose("Build system:", o, d, 5)]);
	}
	if (!(given & G_CC) && !yes) {
		char found[8][128];
		const char *labels[8];
		char lab[8][40];
		size_t n = pm_find_compilers(cfg.lang, found, 8), k;
		if (n > 1) {
			for (k = 0; k < n; k++) {
				snprintf(lab[k], sizeof lab[k], "%s", "compiler");
				labels[k] = strcmp(found[k], "cc") == 0 ? "recommended" : lab[k];
			}
			{
				const char *opts[8];
				for (k = 0; k < n; k++)
					opts[k] = found[k];
				k = choose("Compiler:", opts, labels, n);
				snprintf(cfg.cc, sizeof cfg.cc, "%s", found[k]);
			}
		}
	}

	if (!(given & G_LICENSE) && !yes) {
		static const char *const o[] = {
			"MIT", "Apache-2.0", "GPL-2.0-only", "GPL-3.0-only",
			"LGPL-2.1-only", "LGPL-3.0-only", "BSD-2-Clause",
			"BSD-3-Clause", "ISC", "MPL-2.0", "CC0-1.0",
			"Unlicense", "Zlib", "None"
		};
		static const char *const d[] = {
			"permissive", "permissive + patent grant", "strong copyleft",
			"strong copyleft", "weak copyleft (libraries)",
			"weak copyleft", "permissive", "permissive", "permissive",
			"file-level copyleft", "public-domain dedication",
			"public domain", "permissive", "no license file"
		};
		snprintf(cfg.license, sizeof cfg.license, "%s", o[choose("License:", o, d, sizeof o / sizeof o[0])]);
	}
	if (!(given & G_TEST) && !yes) {
		static const char *const cf[] = {"none", "Unity", "cmocka", "criterion"};
		static const char *const xf[] = {"none", "Catch2", "GoogleTest", "doctest"};
		int is_c = strcmp(cfg.lang, "c") == 0;
		const char *const *o = is_c ? cf : xf;
		snprintf(cfg.test_fw, sizeof cfg.test_fw, "%s", o[choose("Test framework:", o, NULL, 4)]);
	}
	if (!(given & G_CI) && !yes) {
		static const char *const o[] = {"yes", "no"};
		cfg.ci = choose("Add GitHub Actions CI?", o, NULL, 2) == 0;
	}
	if (!(given & G_LINT) && !yes) {
		static const char *const o[] = {"yes", "no"};
		cfg.lint = choose("Add clang-format/clang-tidy/.editorconfig?", o, NULL, 2) == 0;
	}

	if (!fw_available(cfg.test_fw))
		printf("\n  note: %s headers not found in the usual places; tests/ is still\n"
		       "        generated but will not build until %s is installed.\n",
		       cfg.test_fw, cfg.test_fw);

	pm_set_verbose(c, 1);
	printf("\n==> Scaffolding '%s' in %s\n", cfg.name, dir);
	if (pm_generate(c, &cfg, dir) != 0) {
		fprintf(stderr, "pm: %s\n", pm_error(c));
		pm_free(c);
		return 1;
	}
	printf("\n==> Done. %s, %s (%s), %s, compiler %s\n", cfg.kind, cfg.lang, cfg.std,
	       cfg.build, pm_get(c, "CC"));
	if (strcmp(cfg.build, "autoconf") == 0)
		printf("    next: ./bootstrap && ./configure && make\n");
	else if (strcmp(cfg.build, "configure") == 0)
		printf("    next: ./configure && make\n");
	pm_free(c);
	return 0;
}
