/* toolchain.c - system prefix (/usr/local), compiler discovery. */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pm_int.h"

const char *pm_prefix(char *buf, size_t n)
{
	const char *e = getenv("PM_PREFIX");

	if (e && *e)
		snprintf(buf, n, "%s", e);
	else
		snprintf(buf, n, "/usr/local");
	return buf;
}

static int is_exec(const char *path)
{
	return access(path, X_OK) == 0;
}

int pm_which(const char *prog, char *out, size_t n)
{
	char pm[512], cand[1024];
	const char *path, *s;

	if (!prog || !*prog)
		return 0;
	if (strchr(prog, '/')) {
		if (!is_exec(prog))
			return 0;
		snprintf(out, n, "%s", prog);
		return 1;
	}
	pm_prefix(pm, sizeof pm);
	snprintf(cand, sizeof cand, "%s/bin/%s", pm, prog);
	if (is_exec(cand)) {
		snprintf(out, n, "%s", cand);
		return 1;
	}
	path = getenv("PATH");
	if (!path)
		return 0;
	for (s = path; *s;) {
		const char *e = strchr(s, ':');
		size_t len = e ? (size_t)(e - s) : strlen(s);
		if (len == 0)
			snprintf(cand, sizeof cand, "./%s", prog);
		else
			snprintf(cand, sizeof cand, "%.*s/%s", (int)len, s, prog);
		if (is_exec(cand)) {
			snprintf(out, n, "%s", cand);
			return 1;
		}
		if (!e)
			break;
		s = e + 1;
	}
	return 0;
}

/* Runs a command and captures the first bytes of stdout+stderr. */
int pm_capture(const char *cmd, char *out, size_t n)
{
	FILE *p;
	size_t got;

	out[0] = '\0';
	p = popen(cmd, "r");
	if (!p)
		return -1;
	got = fread(out, 1, n - 1, p);
	out[got] = '\0';
	return pclose(p) == 0 ? 0 : -1;
}

static int safe_cmd(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++)
		if (!(isalnum((unsigned char)*s) || strchr("_+./-", *s)))
			return 0;
	return 1;
}

pm_cc_flavor pm_cc_detect(const char *cc)
{
	return cc && safe_cmd(cc) ? PM_CC_CC : PM_CC_UNKNOWN;
}

const char *pm_cc_flavor_name(pm_cc_flavor f)
{
	return f == PM_CC_CC ? "cc" : "unknown";
}

size_t pm_find_compilers(const char *lang, char cmds[][128], size_t max)
{
	static const char *const c_names[] = {"cc", NULL};
	static const char *const cxx_names[] = {"c++", NULL};
	const char *const *names = strcmp(lang, "c++") == 0 ? cxx_names : c_names;
	char pm[512], pmbin[600], found[1024];
	size_t cnt = 0, i;

	pm_prefix(pm, sizeof pm);
	snprintf(pmbin, sizeof pmbin, "%s/bin/", pm);
	for (i = 0; names[i] && cnt < max; i++) {
		if (!pm_which(names[i], found, sizeof found))
			continue;
		if (strncmp(found, pmbin, strlen(pmbin)) == 0)
			snprintf(cmds[cnt], 128, "%.127s", found); /* local only: absolute */
		else
			snprintf(cmds[cnt], 128, "%.127s", names[i]);
		cnt++;
	}
	return cnt;
}

