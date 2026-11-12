/* pm.h - public API of libpm (C99 + POSIX.1 for mkdir/stat/chmod).
 *
 * Everything the `pm` CLI does goes through this header, so any program can
 * scaffold projects, load template packs, or query toolchains the same way.
 */
#ifndef PM_H
#define PM_H

#include <stddef.h>
#include <stdio.h>

#define PM_VERSION "2.0.1"

#if defined(__GNUC__) || defined(__clang__)
#define PM_API __attribute__((visibility("default")))
#else
#define PM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_ctx pm_ctx;

/* ---- context ---------------------------------------------------------- */

/* Loads the built-in template pack, then the pack named by the
 * PM_TEMPLATE environment variable (file or directory), if set.
 * Never returns NULL except on OOM; if PM_TEMPLATE could not be loaded,
 * pm_error() is non-empty right after this call. */
PM_API pm_ctx *pm_new(void);
PM_API void pm_free(pm_ctx *c);
PM_API const char *pm_error(const pm_ctx *c);
PM_API void pm_set_verbose(pm_ctx *c, int on);

/* ---- variables (@@NAME@@ tokens) -------------------------------------- */

PM_API int pm_set(pm_ctx *c, const char *key, const char *val);
PM_API const char *pm_get(const pm_ctx *c, const char *key); /* NULL: unset */

/* ---- templates -------------------------------------------------------- */

/* Pack file: sections start with a line "%%% name". A directory is also
 * accepted: each known template name is looked up as a file inside it.
 * Loaded templates replace same-named ones. Returns 0 / -1. */
PM_API int pm_load_pack(pm_ctx *c, const char *path);
PM_API const char *pm_template(const pm_ctx *c, const char *name);
PM_API size_t pm_template_count(const pm_ctx *c);
PM_API const char *pm_template_name(const pm_ctx *c, size_t i);
PM_API int pm_dump_pack(const pm_ctx *c, FILE *out);

/* Expands tokens and "@@if X@@ / @@else@@ / @@end@@" lines. malloc'd result. */
PM_API char *pm_expand(pm_ctx *c, const char *text);
PM_API int pm_render(pm_ctx *c, const char *name, const char *dst, int exec);

/* ---- project configuration -------------------------------------------- */

typedef struct pm_config {
	char name[128];
	char author[128];
	char lang[8];     /* "c" | "c++" */
	char std[16];     /* "c99", "c++17", ... */
	char build[16];   /* "make" | "cmake" | "sh" | "configure" | "autoconf" */
	char license[24]; /* SPDX ID: MIT, Apache-2.0, GPL-*, LGPL-*, BSD-*, ISC, MPL-2.0, CC0-1.0, Unlicense, Zlib, None */
	char test_fw[16]; /* "none" | "Unity" | "cmocka" | "criterion" | "Catch2" | "GoogleTest" | "doctest" */
	char kind[8];     /* "exe" | "lib" | "both" (library + thin executable) */
	char cc[128];     /* compiler command; "" = auto */
	char libc[8];     /* "system" (only supported libc) */
	int ci;
	int lint;
} pm_config;

PM_API void pm_config_default(pm_config *cfg);
/* Validates cfg and fills every derived variable. Returns 0 / -1. */
PM_API int pm_config_apply(pm_ctx *c, const pm_config *cfg);
/* pm_config_apply + write all files below dir (created if needed). */
PM_API int pm_generate(pm_ctx *c, const pm_config *cfg, const char *dir);

/* ---- toolchain / local prefix ----------------------------------------- */

typedef enum {
	PM_CC_UNKNOWN = 0,
	PM_CC_CC
} pm_cc_flavor;

/* $PM_PREFIX, else /usr/local. Returns buf. */
PM_API const char *pm_prefix(char *buf, size_t n);
/* Looks in <pm>/bin first, then $PATH. 1 = found (path in out), 0 = no. */
PM_API int pm_which(const char *prog, char *out, size_t n);
PM_API pm_cc_flavor pm_cc_detect(const char *cc);
PM_API const char *pm_cc_flavor_name(pm_cc_flavor f);
/* Fills cmds[] with compilers found for "c" or "c++": a bare name if it is on
 * $PATH, an absolute path if it only exists in <pm>/bin (local-first). */
PM_API size_t pm_find_compilers(const char *lang, char cmds[][128], size_t max);

/* ---- filesystem / process --------------------------------------------- */

/* Runs a shell command, captures its stdout+stderr (truncated to n-1 bytes).
 * Returns 0 if the command exited with status 0. */
PM_API int pm_capture(const char *cmd, char *out, size_t n);

PM_API int pm_mkdirp(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* PM_H */
