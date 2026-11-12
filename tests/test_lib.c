#define _POSIX_C_SOURCE 200809L
/* test_lib.c - exercises libpm through its public header only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pm/pm.h"

static int failures;

#define CHECK(x)                                                              \
	do {                                                                  \
		if (!(x)) {                                                   \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);   \
			failures++;                                           \
		}                                                             \
	} while (0)

static void test_expand(void)
{
	pm_ctx *c = pm_new();
	char *s;

	pm_set(c, "A", "alpha");
	pm_set(c, "ON", "1");
	pm_set(c, "OFF", "");

	s = pm_expand(c, "x @@A@@ y @@UNKNOWN@@ z\n");
	CHECK(s && strcmp(s, "x alpha y @@UNKNOWN@@ z\n") == 0);
	free(s);

	s = pm_expand(c, "@@if ON@@\nyes\n@@else@@\nno\n@@end@@\n");
	CHECK(s && strcmp(s, "yes\n") == 0);
	free(s);

	s = pm_expand(c, "@@if OFF@@\nyes\n@@else@@\nno\n@@end@@\n");
	CHECK(s && strcmp(s, "no\n") == 0);
	free(s);

	s = pm_expand(c, "@@if !OFF@@\nnot off\n@@end@@\n");
	CHECK(s && strcmp(s, "not off\n") == 0);
	free(s);

	/* nesting: inner else must not leak out of a false outer branch */
	s = pm_expand(c, "@@if OFF@@\n@@if ON@@\na\n@@else@@\nb\n@@end@@\n@@end@@\nz\n");
	CHECK(s && strcmp(s, "z\n") == 0);
	free(s);

	CHECK(pm_expand(c, "@@if ON@@\nno end\n") == NULL);
	CHECK(pm_expand(c, "@@end@@\n") == NULL);
	pm_free(c);
}

static void test_pack_override(void)
{
	const char *path = "build/override.pack";
	FILE *f = fopen(path, "w");
	pm_ctx *c = pm_new();

	CHECK(f != NULL);
	fputs("%%% main.c\ncustom @@NAME@@\n%%% brand-new\nhello\n", f);
	fclose(f);

	CHECK(strstr(pm_template(c, "main.c"), "#include <stdio.h>") != NULL);
	CHECK(pm_load_pack(c, path) == 0);
	CHECK(strcmp(pm_template(c, "main.c"), "custom @@NAME@@\n") == 0);
	CHECK(pm_template(c, "brand-new") != NULL);
	CHECK(pm_template_count(c) > 10);
	CHECK(pm_template_name(c, 0) != NULL);
	CHECK(pm_template_name(c, pm_template_count(c)) == NULL);
	CHECK(pm_render(c, "brand-new", "build/rendered.txt", 0) == 0);
	{
		FILE *r = fopen("build/rendered.txt", "r");
		char line[64] = "";
		CHECK(r != NULL);
		if (r) { fgets(line, sizeof line, r); fclose(r); }
		CHECK(strcmp(line, "hello\n") == 0);
	}
	CHECK(pm_render(c, "not-a-template", "build/nope", 0) != 0);
	CHECK(pm_load_pack(c, "build/does-not-exist.pack") != 0);
	CHECK(pm_error(c)[0] != '\0');
	pm_free(c);
}

static void test_pack_directory(void)
{
	pm_ctx *c = pm_new();
	FILE *f;
	CHECK(pm_mkdirp("build/packdir") == 0);
	f = fopen("build/packdir/main.c", "w");
	CHECK(f != NULL);
	if (f) { fputs("directory override @@NAME@@\n", f); fclose(f); }
	CHECK(pm_load_pack(c, "build/packdir") == 0);
	CHECK(strcmp(pm_template(c, "main.c"), "directory override @@NAME@@\n") == 0);
	pm_free(c);
}

static void test_render_executable(void)
{
	pm_ctx *c = pm_new();
	FILE *f;
	CHECK(pm_set(c, "NAME", "render-test") == 0);
	CHECK(pm_render(c, "build.sh", "build/rendered-build.sh", 1) == 0);
	CHECK(access("build/rendered-build.sh", X_OK) == 0);
	f = fopen("build/rendered-build.sh", "r");
	CHECK(f != NULL);
	if (f) fclose(f);
	pm_free(c);
}

static void test_env_pack(void)
{
	pm_ctx *c;
	FILE *f = fopen("build/env.pack", "w");

	CHECK(f != NULL);
	fputs("%%% main.c\nfrom env\n", f);
	fclose(f);
	setenv("PM_TEMPLATE", "build/env.pack", 1);
	c = pm_new();
	CHECK(strcmp(pm_template(c, "main.c"), "from env\n") == 0);
	pm_free(c);
	unsetenv("PM_TEMPLATE");
}

static void test_config(void)
{
	pm_ctx *c = pm_new();
	pm_config cfg;

	pm_config_default(&cfg);
	CHECK(pm_config_apply(c, &cfg) != 0); /* no name */
	strcpy(cfg.name, "bad name");
	CHECK(pm_config_apply(c, &cfg) != 0);
	strcpy(cfg.name, "ok-name");
	strcpy(cfg.cc, "cc; touch /tmp/pm-injected");
	CHECK(pm_config_apply(c, &cfg) != 0); /* reject shell metacharacters */
	strcpy(cfg.cc, "cc");
	CHECK(pm_config_apply(c, &cfg) == 0);
	CHECK(strcmp(pm_get(c, "NAME_CANON"), "ok_name") == 0);
	CHECK(strcmp(pm_get(c, "NAME_UPPER"), "OK_NAME") == 0);
	CHECK(strcmp(pm_get(c, "CC"), "cc") == 0);
	CHECK(strcmp(pm_get(c, "STD_FLAG"), "-std=c99") == 0);
	CHECK(strcmp(pm_get(c, "HAS_DEPS"), "1") == 0);
	strcpy(cfg.cc, "clang");
	CHECK(pm_config_apply(c, &cfg) == 0);
	CHECK(strcmp(pm_get(c, "CC"), "clang") == 0);

	strcpy(cfg.std, "c++17");
	CHECK(pm_config_apply(c, &cfg) != 0); /* std/lang mismatch */
	strcpy(cfg.std, "c99");
	strcpy(cfg.libc, "system");
	strcpy(cfg.build, "autoconf");
	CHECK(pm_config_apply(c, &cfg) == 0); /* system libc works with autoconf */
	strcpy(cfg.license, "not-a-license");
	CHECK(pm_config_apply(c, &cfg) != 0);
	strcpy(cfg.license, "MIT");
	strcpy(cfg.libc, "other");
	CHECK(pm_config_apply(c, &cfg) != 0);
	strcpy(cfg.libc, "system");
	strcpy(cfg.kind, "shared");
	CHECK(pm_config_apply(c, &cfg) != 0);
	pm_free(c);
}

static void test_flavor(void)
{
	CHECK(pm_cc_detect("cc") == PM_CC_CC);
	CHECK(pm_cc_detect("clang") == PM_CC_CC);
	CHECK(pm_cc_detect("/opt/toolchain/bin/gcc") == PM_CC_CC);
	CHECK(pm_cc_detect("cc; rm -rf /") == PM_CC_UNKNOWN); /* injection guard */
	CHECK(strcmp(pm_cc_flavor_name(PM_CC_CC), "cc") == 0);
}

static void test_prefix(void)
{
	char buf[256];
	setenv("PM_PREFIX", "/tmp/pmtest", 1);
	CHECK(strcmp(pm_prefix(buf, sizeof buf), "/tmp/pmtest") == 0);
	unsetenv("PM_PREFIX");
	setenv("HOME", "/home/x", 1);
	CHECK(strcmp(pm_prefix(buf, sizeof buf), "/usr/local") == 0);
}

int main(void)
{
	pm_mkdirp("build");
	test_expand();
	test_pack_override();
	test_pack_directory();
	test_render_executable();
	test_env_pack();
	test_config();
	test_flavor();
	test_prefix();
	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("libpm: all unit checks passed\n");
	return 0;
}
