/* pm.c - core of libpm: variables, template packs, expansion, rendering. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pm_int.h"

/* ---- small helpers ---------------------------------------------------- */

char *pm_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

void pm_seterr(pm_ctx *c, const char *fmt, ...)
{
	va_list ap;
	if (!c)
		return;
	va_start(ap, fmt);
	vsnprintf(c->err, sizeof c->err, fmt, ap);
	va_end(ap);
}

void pm_buf_init(pm_buf *b)
{
	b->p = NULL;
	b->len = b->cap = 0;
}

int pm_buf_add(pm_buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		char *np;
		while (cap < b->len + n + 1)
			cap *= 2;
		np = realloc(b->p, cap);
		if (!np)
			return -1;
		b->p = np;
		b->cap = cap;
	}
	memcpy(b->p + b->len, s, n);
	b->len += n;
	b->p[b->len] = '\0';
	return 0;
}

int pm_buf_adds(pm_buf *b, const char *s)
{
	return pm_buf_add(b, s, strlen(s));
}

char *pm_buf_take(pm_buf *b)
{
	char *p = b->p;
	if (!p)
		p = pm_strdup("");
	b->p = NULL;
	b->len = b->cap = 0;
	return p;
}

void pm_buf_free(pm_buf *b)
{
	free(b->p);
	pm_buf_init(b);
}

int pm_kv_put(pm_kv **arr, size_t *n, size_t *cap, const char *key, const char *val)
{
	size_t i;
	char *v = pm_strdup(val);
	if (!v)
		return -1;
	for (i = 0; i < *n; i++) {
		if (strcmp((*arr)[i].key, key) == 0) {
			free((*arr)[i].val);
			(*arr)[i].val = v;
			return 0;
		}
	}
	if (*n == *cap) {
		size_t nc = *cap ? *cap * 2 : 32;
		pm_kv *na = realloc(*arr, nc * sizeof **arr);
		if (!na) {
			free(v);
			return -1;
		}
		*arr = na;
		*cap = nc;
	}
	(*arr)[*n].key = pm_strdup(key);
	if (!(*arr)[*n].key) {
		free(v);
		return -1;
	}
	(*arr)[*n].val = v;
	(*n)++;
	return 0;
}

const char *pm_kv_get(const pm_kv *arr, size_t n, const char *key)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (strcmp(arr[i].key, key) == 0)
			return arr[i].val;
	return NULL;
}

static void kv_free(pm_kv *arr, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		free(arr[i].key);
		free(arr[i].val);
	}
	free(arr);
}

/* ---- filesystem ------------------------------------------------------- */

int pm_mkdirp(const char *path)
{
	char *tmp, *p;
	int rc = 0;

	if (!path || !*path)
		return 0;
	tmp = pm_strdup(path);
	if (!tmp)
		return -1;
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
			rc = -1;
			break;
		}
		*p = '/';
	}
	if (rc == 0 && mkdir(tmp, 0777) != 0 && errno != EEXIST)
		rc = -1;
	free(tmp);
	return rc;
}

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	pm_buf b;
	char tmp[4096];
	size_t n;

	if (!f)
		return NULL;
	pm_buf_init(&b);
	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) {
		if (pm_buf_add(&b, tmp, n) != 0) {
			pm_buf_free(&b);
			fclose(f);
			return NULL;
		}
	}
	fclose(f);
	if (len)
		*len = b.len;
	return pm_buf_take(&b);
}

/* ---- context ---------------------------------------------------------- */

static int pack_parse(pm_ctx *c, const char *data, size_t len)
{
	const char *p = data, *end = data + len;
	char name[256];
	pm_buf body;
	int have = 0;

	name[0] = '\0';
	pm_buf_init(&body);
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *le = nl ? nl + 1 : end;
		if (le - p >= 4 && memcmp(p, "%%% ", 4) == 0) {
			size_t n = (size_t)(le - p) - 4;
			if (have && pm_kv_put(&c->tmpl, &c->ntmpl, &c->captmpl, name,
					       body.p ? body.p : "") != 0)
				goto oom;
			pm_buf_free(&body);
			while (n > 0 && (p[4 + n - 1] == '\n' || p[4 + n - 1] == '\r' ||
					 p[4 + n - 1] == ' '))
				n--;
			if (n == 0 || n >= sizeof name) {
				pm_seterr(c, "bad section name in template pack");
				pm_buf_free(&body);
				return -1;
			}
			memcpy(name, p + 4, n);
			name[n] = '\0';
			have = 1;
		} else if (have) {
			if (pm_buf_add(&body, p, (size_t)(le - p)) != 0)
				goto oom;
		}
		p = le;
	}
	if (have && pm_kv_put(&c->tmpl, &c->ntmpl, &c->captmpl, name,
			       body.p ? body.p : "") != 0)
		goto oom;
	pm_buf_free(&body);
	return 0;
oom:
	pm_buf_free(&body);
	pm_seterr(c, "out of memory");
	return -1;
}

int pm_load_pack(pm_ctx *c, const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		pm_seterr(c, "template pack '%s': %s", path, strerror(errno));
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		size_t i, n = c->ntmpl;
		char **names = malloc((n ? n : 1) * sizeof *names);
		int rc = 0;
		if (!names) {
			pm_seterr(c, "out of memory");
			return -1;
		}
		for (i = 0; i < n; i++)
			names[i] = pm_strdup(c->tmpl[i].key);
		for (i = 0; i < n && rc == 0; i++) {
			char fp[1024];
			char *txt;
			if (!names[i])
				continue;
			snprintf(fp, sizeof fp, "%s/%s", path, names[i]);
			txt = read_file(fp, NULL);
			if (!txt)
				continue; /* not overridden */
			if (pm_kv_put(&c->tmpl, &c->ntmpl, &c->captmpl, names[i], txt) != 0) {
				pm_seterr(c, "out of memory");
				rc = -1;
			}
			free(txt);
		}
		for (i = 0; i < n; i++)
			free(names[i]);
		free(names);
		return rc;
	} else {
		size_t len = 0;
		char *txt = read_file(path, &len);
		int rc;
		if (!txt) {
			pm_seterr(c, "cannot read template pack '%s'", path);
			return -1;
		}
		rc = pack_parse(c, txt, len);
		free(txt);
		return rc;
	}
}

pm_ctx *pm_new(void)
{
	pm_ctx *c = calloc(1, sizeof *c);
	const char *env;

	if (!c)
		return NULL;
	if (pack_parse(c, (const char *)pm_default_pack, pm_default_pack_len) != 0)
		return c; /* err already set */
	env = getenv("PM_TEMPLATE");
	if (env && *env)
		pm_load_pack(c, env);
	return c;
}

void pm_free(pm_ctx *c)
{
	if (!c)
		return;
	kv_free(c->vars, c->nvars);
	kv_free(c->tmpl, c->ntmpl);
	free(c);
}

const char *pm_error(const pm_ctx *c)
{
	return c ? c->err : "";
}

void pm_set_verbose(pm_ctx *c, int on)
{
	c->verbose = on;
}

int pm_set(pm_ctx *c, const char *key, const char *val)
{
	if (pm_kv_put(&c->vars, &c->nvars, &c->capvars, key, val ? val : "") != 0) {
		pm_seterr(c, "out of memory");
		return -1;
	}
	return 0;
}

const char *pm_get(const pm_ctx *c, const char *key)
{
	return pm_kv_get(c->vars, c->nvars, key);
}

const char *pm_template(const pm_ctx *c, const char *name)
{
	return pm_kv_get(c->tmpl, c->ntmpl, name);
}

size_t pm_template_count(const pm_ctx *c)
{
	return c->ntmpl;
}

const char *pm_template_name(const pm_ctx *c, size_t i)
{
	return i < c->ntmpl ? c->tmpl[i].key : NULL;
}

int pm_dump_pack(const pm_ctx *c, FILE *out)
{
	size_t i;
	for (i = 0; i < c->ntmpl; i++)
		if (fputs("%%% ", out) < 0 || fputs(c->tmpl[i].key, out) < 0 ||
		    fputc('\n', out) == EOF || fputs(c->tmpl[i].val, out) < 0)
			return -1;
	return 0;
}

/* ---- expansion -------------------------------------------------------- */

#define PM_MAX_DEPTH 64

static int truthy(const char *v)
{
	return v && *v && strcmp(v, "0") != 0;
}

static int is_tok_char(char ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

/* "@@if X@@", "@@if !X@@", "@@else@@", "@@end@@" alone on a line.
 * Returns 1 = if, 2 = else, 3 = end, 0 = not a directive. */
static int directive(const char *s, const char *e, char *arg, size_t argn, int *neg)
{
	size_t n;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		e--;
	n = (size_t)(e - s);
	if (n >= 8 && memcmp(s, "@@else@@", 8) == 0 && n == 8)
		return 2;
	if (n == 7 && memcmp(s, "@@end@@", 7) == 0)
		return 3;
	if (n >= 7 && memcmp(s, "@@if ", 5) == 0 && memcmp(e - 2, "@@", 2) == 0) {
		const char *a = s + 5, *ae = e - 2;
		*neg = 0;
		if (a < ae && *a == '!') {
			*neg = 1;
			a++;
		}
		n = (size_t)(ae - a);
		if (n == 0 || n >= argn)
			return 0;
		memcpy(arg, a, n);
		arg[n] = '\0';
		return 1;
	}
	return 0;
}

static int expand_line(pm_ctx *c, pm_buf *out, const char *s, const char *e)
{
	while (s < e) {
		if (e - s >= 4 && s[0] == '@' && s[1] == '@') {
			const char *t = s + 2, *q = t;
			while (q < e && is_tok_char(*q))
				q++;
			if (q > t && e - q >= 2 && q[0] == '@' && q[1] == '@') {
				char key[64];
				size_t n = (size_t)(q - t);
				const char *v;
				if (n < sizeof key) {
					memcpy(key, t, n);
					key[n] = '\0';
					v = pm_get(c, key);
					if (v) {
						if (pm_buf_adds(out, v) != 0)
							return -1;
						s = q + 2;
						continue;
					}
				}
			}
		}
		if (pm_buf_add(out, s, 1) != 0)
			return -1;
		s++;
	}
	return 0;
}

char *pm_expand(pm_ctx *c, const char *text)
{
	pm_buf out;
	unsigned char cond[PM_MAX_DEPTH];
	int depth = 0, i, active;
	const char *p = text, *end = text + strlen(text);

	pm_buf_init(&out);
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *le = nl ? nl + 1 : end;
		char arg[64];
		int neg = 0, d = directive(p, le, arg, sizeof arg, &neg);

		if (d == 1) {
			if (depth == PM_MAX_DEPTH) {
				pm_seterr(c, "@@if nesting too deep");
				goto fail;
			}
			cond[depth++] = (unsigned char)(truthy(pm_get(c, arg)) != neg);
		} else if (d == 2) {
			if (depth == 0) {
				pm_seterr(c, "@@else@@ without @@if");
				goto fail;
			}
			cond[depth - 1] = (unsigned char)!cond[depth - 1];
		} else if (d == 3) {
			if (depth == 0) {
				pm_seterr(c, "@@end@@ without @@if");
				goto fail;
			}
			depth--;
		} else {
			active = 1;
			for (i = 0; i < depth; i++)
				active = active && cond[i];
			if (active && expand_line(c, &out, p, le) != 0) {
				pm_seterr(c, "out of memory");
				goto fail;
			}
		}
		p = le;
	}
	if (depth != 0) {
		pm_seterr(c, "missing @@end@@");
		goto fail;
	}
	return pm_buf_take(&out);
fail:
	pm_buf_free(&out);
	return NULL;
}

int pm_render(pm_ctx *c, const char *name, const char *dst, int exec)
{
	const char *t = pm_template(c, name);
	char *txt, *dir, *slash;
	FILE *f;

	if (!t) {
		pm_seterr(c, "no template named '%s'", name);
		return -1;
	}
	txt = pm_expand(c, t);
	if (!txt) {
		char why[sizeof c->err];
		memcpy(why, c->err, sizeof why);
		pm_seterr(c, "template '%s': %s", name, why);
		return -1;
	}
	dir = pm_strdup(dst);
	if (!dir) {
		free(txt);
		pm_seterr(c, "out of memory");
		return -1;
	}
	slash = strrchr(dir, '/');
	if (slash && slash != dir) {
		*slash = '\0';
		if (pm_mkdirp(dir) != 0) {
			pm_seterr(c, "cannot create '%s': %s", dir, strerror(errno));
			free(dir);
			free(txt);
			return -1;
		}
	}
	free(dir);
	f = fopen(dst, "wb");
	if (!f) {
		pm_seterr(c, "cannot write '%s': %s", dst, strerror(errno));
		free(txt);
		return -1;
	}
	if (fputs(txt, f) < 0 || fclose(f) != 0) {
		pm_seterr(c, "write error on '%s'", dst);
		free(txt);
		return -1;
	}
	free(txt);
	if (exec && chmod(dst, 0755) != 0) {
		pm_seterr(c, "chmod '%s': %s", dst, strerror(errno));
		return -1;
	}
	if (c->verbose)
		printf("  created %s\n", dst);
	return 0;
}
