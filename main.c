/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/main.c,v 1.112 2006/03/18 01:58:55 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>

/* make sure our buffers are as big as they can be */
#if PATH_MAX > _POSIX_PATH_MAX
# define _Q_PATH_MAX PATH_MAX
#else
# define _Q_PATH_MAX _POSIX_PATH_MAX
#endif

#if defined(__i386__) && defined(__linux__) && !defined(__UCLIBC__)
# define PORTAGE_BINHOST "ftp://tinderbox.x86.dev.gentoo.org/default-linux/x86/2005.1/All"
#else
# define PORTAGE_BINHOST ""
#endif

/* prototypes and such */
static char eat_file(const char *file, char *buf, const size_t bufsize);
int rematch(const char *, const char *, int);
static char *rmspace(char *);

void initialize_portage_env(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);
void cleanup(void);
int lookup_applet_idx(const char *);

/* variables to control runtime behavior */
static char exact = 0;
static int found = 0;
static int verbose = 0;
static int quiet = 0;
static char pretend = 0;
static char reinitialize = 0;
static char reinitialize_metacache = 0;
static char portdir[_Q_PATH_MAX] = "/usr/portage";
static char portarch[20] = "";
static char portvdb[] = "var/db/pkg";
static char portcachedir[] = "metadata/cache";
static char portroot[_Q_PATH_MAX] = "/";
static char config_protect[_Q_PATH_MAX] = "/etc/";

char pkgdir[512] = "/usr/portage/packages/";
char port_tmpdir[512] = "/var/tmp/portage/";

char binhost[512] = PORTAGE_BINHOST;
char features[512] = "noman noinfo nodoc";
char accept_license[512] = "*";
char install_mask[1024] = "";

const char *err_noapplet = "Sorry this applet was disabled at compile time";

FILE *saved_stderr;

#define qfprintf(stream, fmt, args...) do { if (!quiet) fprintf(stream, _( fmt ), ## args); } while (0)
#define qprintf(fmt, args...) qfprintf(stdout, _( fmt ), ## args)

#if defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
# define SET_STDERR(fp) err("Darwin/NetBSD/OpenBSD have stupid stdout handling")
#else
# define SET_STDERR(fp) stderr = fp
#endif


#define _q_unused_ __attribute__((__unused__))

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif

/* helper functions for showing errors */
static const char *argv0;

#ifdef EBUG
# include <sys/resource.h>

# define DBG(fmt, args...) warnf(fmt , ## args)
# define IF_DEBUG(x) x
void init_coredumps(void);
void init_coredumps(void)
{
	struct rlimit rl;
	rl.rlim_cur = RLIM_INFINITY;
	rl.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rl);
}
#else
# define DBG(fmt, args...)
# define IF_DEBUG(x)
#endif

/* include common library code */
#include "libq/libq.c"

void no_colors(void);
void no_colors() {
	// echo $(awk '{print $4,"="}' libq/colors.c  | grep ^* |cut -c 2-| grep ^[A-Z] |tr '\n' ' ') = \"\"\;
	BOLD = NORM = BLUE = DKBLUE = CYAN = GREEN = DKGREEN = MAGENTA = RED = YELLOW = BRYELLOW = WHITE = "";
	setenv("NOCOLOR", "true", 1);
}

/* include common applet defs */
#include "applets.h"

/* Common usage for all applets */
#define COMMON_FLAGS "vqChV"
#define a_argument required_argument
#define COMMON_LONG_OPTS \
	{"verbose",   no_argument, NULL, 'v'}, \
	{"quiet",     no_argument, NULL, 'q'}, \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Make a lot of noise", \
	"Tighter output; suppress warnings", \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 'v': ++verbose; break; \
	case 'q': ++quiet; \
		if (stderr == saved_stderr) { \
			FILE *stderr_fp = NULL; \
			stderr_fp = fopen("/dev/null", "w"); \
			SET_STDERR(stderr_fp); \
		} \
		break; \
	case 'V': version_barf( applet ## _rcsid ); break; \
	case 'h': applet ## _usage(EXIT_SUCCESS); break; \
	case 'C': no_colors(); break; \
	default: applet ## _usage(EXIT_FAILURE); break;

#define GETOPT_LONG(A, a, ex) \
	getopt_long(argc, argv, ex A ## _FLAGS, a ## _long_opts, NULL)
/* display usage and exit */
static void usage(int status, const char *flags, struct option const opts[], 
                  const char *help[], int blabber)
{
	unsigned long i;
	if (blabber == 0) {
		printf("%sUsage:%s %sq%s %s<applet> [arguments]...%s\n\n", GREEN, 
			NORM, YELLOW, NORM, DKBLUE, NORM);
		printf("%sCurrently defined applets:%s\n", GREEN, NORM);
		for (i = 0; applets[i].desc; ++i)
			if (applets[i].func)
				printf(" %s%8s%s %s%-16s%s%s:%s %s\n",
					YELLOW, applets[i].name, NORM, 
					DKBLUE, applets[i].opts, NORM,
					RED, NORM, _(applets[i].desc));
	} else {
		printf("%sUsage:%s %s%s%s %s%s%s %s:%s %s\n", GREEN, NORM,
			YELLOW, applets[blabber].name, NORM,
			DKBLUE, applets[blabber].opts, NORM,
			RED, NORM, _(applets[blabber].desc));
	}
	printf("\n%sOptions:%s -[%s]\n", GREEN, NORM, flags);
	for (i = 0; opts[i].name; ++i) {
		assert(help[i] != NULL); /* this assert is a life saver when adding new applets. */
		if (opts[i].has_arg == no_argument)
			printf("  -%c, --%-15s%s*%s %s\n", opts[i].val,
				opts[i].name, RED, NORM, _(help[i]));
		else
			printf("  -%c, --%-8s %s<arg>%s %s*%s %s\n", opts[i].val,
				opts[i].name, DKBLUE, NORM, RED, NORM, _(help[i]));
	}
	exit(status);
}
static void version_barf(const char *Id)
{
#ifndef VERSION
# define VERSION "cvs"
#endif
	printf("portage-utils-%s: compiled on %s\n%s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       VERSION, __DATE__, Id, argv0);
	exit(EXIT_SUCCESS);
}

static char eat_file(const char *file, char *buf, const size_t bufsize)
{
	FILE *f;
	struct stat s;
	char ret = 0;

	if ((f = fopen(file, "r")) == NULL)
		return ret;

	memset(buf, 0x00, bufsize);
	if (fstat(fileno(f), &s) != 0)
		goto close_and_ret;
	if (s.st_size) {
		if (bufsize < (size_t)s.st_size)
			goto close_and_ret;
		if (fread(buf, 1, s.st_size, f) != (size_t)s.st_size)
			goto close_and_ret;
	} else {
		if (fread(buf, 1, bufsize, f) == 0)
			goto close_and_ret;
	}

	ret = 1;
close_and_ret:
	fclose(f);
	return ret;
}

int rematch(const char *regex, const char *match, int cflags)
{
	regex_t preg;
	int ret;

	if ((match == NULL) || (regex == NULL))
		return EXIT_FAILURE;

	if ((ret = regcomp(&preg, regex, cflags))) {
		char err[256];
		if (regerror(ret, &preg, err, sizeof(err)))
			warnf("regcomp failed: %s", err);
		else
			warnf("regcomp failed");
		return EXIT_FAILURE;
	}
	ret = regexec(&preg, match, 0, NULL, 0);
	regfree(&preg);

	return ret;
}

/* removes adjacent extraneous white space */
static char *remove_extra_space(char *str);
static char *remove_extra_space(char *str)
{
	char *p, c = ' ';
	size_t len, pos = 0;
	char *buf;

	if (str == NULL)
		return NULL;
	len = strlen(str);
	buf = (char*)xmalloc(len+1);
	memset(buf, 0, len+1);
	for (p = str; *p != 0; ++p) {
		if (!isspace(*p)) c = *p; else {
			if (c == ' ') continue;
			c = ' ';
		}
		buf[pos] = c;
		pos++;
	}
	if (pos > 0 && buf[pos-1] == ' ') buf[pos-1] = '\0';
	strcpy(str, buf);
	free(buf);
	return str;
}

void freeargv(int, char **);
void freeargv(int argc, char **argv) {
	int i;
        if (argc > 0) {
                for (i = 0; i < argc; i++)
                        free(argv[i]);
                free(argv);
        }
}

void makeargv(char *string, int *argc, char ***argv);
void makeargv(char *string, int *argc, char ***argv) {
	int curc = 2;
	char *q, *p, *str;        
	(*argv) = (char **) malloc(sizeof(char **) * curc);

	*argc = 1;        
	(*argv)[0] = xstrdup(argv0);
	q = xstrdup(string);        
	str = q;

	remove_extra_space(str);        
	rmspace(str);

	while (str) {
		if ((p = strchr(str, ' ')) != NULL)
			*(p++) = '\0';

                if (*argc == curc) {
			curc *= 2;
			(*argv) = (char **) realloc(*argv, sizeof(char **) * curc);
                }
                (*argv)[*argc] = xstrdup(str);
                (*argc)++;
                str = p;
        }
	free(q);
}

/* 
 * Parse a line of CONTENTS file and provide access to the individual fields
 */
typedef enum {
	CONTENTS_DIR, CONTENTS_OBJ, CONTENTS_SYM
} contents_type;
typedef struct {
	contents_type type;
	char *_data;
	char *name;
	char *sym_target;
	char *digest;
	char *mtime_str;
	long mtime;
} contents_entry;

contents_entry *contents_parse_line(char *line);
contents_entry *contents_parse_line(char *line)
{
	static contents_entry e;
	char *p;

	if (!line || !*line || *line == '\n')
		return NULL;

	/* chop trailing newline */
	if ((p = strrchr(line, '\n')) != NULL)
		*p = '\0';

	memset(&e, 0x00, sizeof(e));
	e._data = line;

	if (!strncmp(e._data, "obj ", 4))
		e.type = CONTENTS_OBJ;
	else if (!strncmp(e._data, "dir ", 4))
		e.type = CONTENTS_DIR;
	else if (!strncmp(e._data, "sym ", 4))
		e.type = CONTENTS_SYM;
	else
		return NULL;

	e.name = e._data + 4;

	switch (e.type) {
		/* dir /bin */
		case CONTENTS_DIR:
			break;

		/* obj /bin/bash 62ed51c8b23866777552643ec57614b0 1120707577 */
		case CONTENTS_OBJ:
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.digest = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.digest++ = '\0';
			break;

		/* sym /bin/sh -> bash 1120707577 */
		case CONTENTS_SYM:
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.sym_target = strstr(e.name, " -> ")) == NULL)
				return NULL;
			*e.sym_target = '\0';
			e.sym_target += 4;
			break;
	}

	if (e.mtime_str) {
		e.mtime = strtol(e.mtime_str, NULL, 10);
		if (e.mtime == LONG_MAX) {
			e.mtime = 0;
			e.mtime_str = NULL;
		}
	}

	return &e;
}

char *strincr_var(const char *, char *, char *, const size_t );
char *strincr_var(const char *name, char *s, char *value, const size_t value_len) {
	char buf[BUFSIZ];
	char *p;

	strncat(value, " ", value_len);
	strncat(value, s, value_len);

	while ((p = strstr(value, "-*")) != NULL)
		memset(value, ' ', (strlen(value)-strlen(p))+2);

	/* This function is mainly used by the startup code for parsing 
		make.conf and stacking variables remove.
		variables can be in the form of ${v} or $v
		works:
			FEATURES="${FEATURES} foo"
			FEATURES="$FEATURES foo"
			FEATURES="baz bar -* foo"

		wont work:
			FEATURES="${OTHERVAR} foo"
			FEATURES="-nls nls -nls"
	*/

	snprintf(buf, sizeof(buf), "${%s}", name);
	if ((p = strstr(value, buf)) != NULL)
		memset(p, ' ', strlen(name)+3);

	snprintf(buf, sizeof(buf), "$%s", name);
	if ((p = strstr(value, buf)) != NULL)
		memset(p, ' ', strlen(name)+1);

	remove_extra_space(value);
	/* we should sort here */

	return (char *) value;
}

void initialize_portage_env(void)
{
	char nocolor = 0;
	int i, f;
	struct stat st;
	FILE *fp;
	char buf[BUFSIZE], *s, *p;

	char profile[_Q_PATH_MAX], portage_file[_Q_PATH_MAX];
	const char *files[] = {portage_file, "/etc/make.globals", "/etc/make.conf"};
	typedef enum { _Q_BOOL, _Q_STR, _Q_ISTR } var_types;
	struct {
		const char *name;
		const size_t name_len;
		const var_types type;
		char *value;
		const size_t value_len;
	} vars_to_read[] = {
		{"ACCEPT_LICENSE", 14, _Q_STR,  accept_license, sizeof(accept_license)},
		{"INSTALL_MASK", 12, _Q_ISTR,  install_mask, sizeof(install_mask)},
		{"ARCH",    4, _Q_STR,  portarch, sizeof(portarch)},
		{"CONFIG_PROTECT",    14, _Q_STR,  config_protect, sizeof(config_protect)},
		{"NOCOLOR", 7, _Q_BOOL, &nocolor, 1},
		{"FEATURES",8, _Q_ISTR,  features, sizeof(features)},
		{"PORTDIR", 7, _Q_STR,  portdir, sizeof(portdir)},
		{"PORTAGE_BINHOST",   15, _Q_STR,  binhost, sizeof(binhost)},
		{"PORTAGE_TMPDIR",    14, _Q_STR,  port_tmpdir, sizeof(port_tmpdir)},
		{"PKGDIR",  6, _Q_STR,  pkgdir, sizeof(pkgdir)},
		{"ROOT",    4, _Q_STR,  portroot, sizeof(portroot)}
	};

	f = 0;
	if (readlink("/etc/make.profile", profile, sizeof(profile)) == -1)
		strcpy(profile, "/etc/make.profile");
	do {
		if (f == 0)
			snprintf(portage_file, sizeof(portage_file), "%s/make.defaults", profile);
		IF_DEBUG(fprintf(stderr, "profile %s\n", files[f]));

		if ((fp=fopen(files[f], "r")) != NULL) {
			while (fgets(buf, sizeof(buf), fp) != NULL) {
				rmspace(buf);
				if (*buf == '#' || *buf == '\0')
					continue;
				for (i=0; i<ARR_SIZE(vars_to_read); ++i) {
					if (buf[vars_to_read[i].name_len] != '=' && buf[vars_to_read[i].name_len] != ' ')
						continue;
					if (strncmp(buf, vars_to_read[i].name, vars_to_read[i].name_len))
						continue;

					/* make sure we handle spaces between the varname, the =, and the value:
					 * VAR=val   VAR = val   VAR="val"
					 */
					s = buf + vars_to_read[i].name_len;
					if ((p = strchr(s, '=')) != NULL)
						s = p + 1;
					while (isspace(*s))
						++s;
					if (*s == '"' || *s == '\'') {
						++s;
						s[strlen(s)-1] = '\0';
					}

					switch (vars_to_read[i].type) {
					case _Q_BOOL: *vars_to_read[i].value = 1; break;
					case _Q_STR: strncpy(vars_to_read[i].value, s, vars_to_read[i].value_len); break;
					case _Q_ISTR: strincr_var(vars_to_read[i].name, s, vars_to_read[i].value, vars_to_read[i].value_len); break;
					}
				}
			}
			fclose(fp);
		}

		if (f > 0) {
			if (++f < ARR_SIZE(files))
				continue;
			else
				break;
		}

		/* everything below here is to figure out what the next parent is */
		snprintf(portage_file, sizeof(portage_file), "%s/parent", profile);
		if (stat(portage_file, &st) == 0) {
			eat_file(portage_file, buf, sizeof(buf));
			rmspace(buf);
			portage_file[strlen(portage_file)-7] = '\0';
			snprintf(profile, sizeof(profile), "%s/%s", portage_file, buf);
		} else {
			f = 1;
		}
	} while (1);

	/* finally, check the env */
	for (i=0; i<ARR_SIZE(vars_to_read); ++i) {
		s = getenv(vars_to_read[i].name);
		if (s != NULL) {
			switch (vars_to_read[i].type) {
			case _Q_BOOL: *vars_to_read[i].value = 1; break;
			case _Q_STR: strncpy(vars_to_read[i].value, s, vars_to_read[i].value_len); break;
			case _Q_ISTR: strincr_var(vars_to_read[i].name, s, vars_to_read[i].value, vars_to_read[i].value_len); break;
			}
		}
		if (getenv("DEBUG") IF_DEBUG(|| 1)) {
			fprintf(stderr, "%s = ", vars_to_read[i].name);
			switch (vars_to_read[i].type) {
			case _Q_BOOL: fprintf(stderr, "%i\n", *vars_to_read[i].value); break;
			case _Q_STR:
			case _Q_ISTR: fprintf(stderr, "%s\n", vars_to_read[i].value); break;
			}
		}
	}

	if (nocolor)
		no_colors();
	else
		color_remap();
}

enum {
	CACHE_EBUILD = 1,
	CACHE_METADATA = 2
};

int filter_hidden(const struct dirent *dentry);
int filter_hidden(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	return 1;
}

#define CACHE_EBUILD_FILE (getenv("CACHE_EBUILD_FILE") ? getenv("CACHE_EBUILD_FILE") : ".ebuild.x")
#define CACHE_METADATA_FILE ".metadata.x"
const char *initialize_flat(int cache_type);
const char *initialize_flat(int cache_type)
{
	struct dirent **category, **pn, **eb;
	struct stat st;
	struct timeval start, finish;
	static const char *cache_file;
	char *p;
	int a, b, c, d, e, i;
	int frac, secs, count;
	FILE *fp;

	a = b = c = d = e = i = 0;
	count = frac = secs = 0;

	cache_file = (cache_type == CACHE_EBUILD ? CACHE_EBUILD_FILE : CACHE_METADATA_FILE);

	if (chdir(portdir) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		goto ret;
	}

	if (cache_type == CACHE_METADATA && chdir(portcachedir) != 0) {
		warnp("chdir to portage cache '%s/%s' failed", portdir, portcachedir);
		goto ret;
	}

	if ((stat(cache_file, &st)) != (-1))
		if (st.st_size == 0)
			unlink(cache_file);

	/* assuming --sync is used with --delete this will get recreated after every merge */
	if (access(cache_file, R_OK) == 0)
		goto ret;

	warn("Updating ebuild %scache ... ", cache_type == CACHE_EBUILD ? "" : "meta" );

	unlink(cache_file);
	if (errno != ENOENT) {
		warnfp("unlinking '%s/%s' failed", portdir, cache_file);
		goto ret;
	}

	if ((fp = fopen(cache_file, "w")) == NULL) {
		warnfp("opening '%s/%s' failed", portdir, cache_file);
		goto ret;
	}

	gettimeofday(&start, NULL);

	if ((a = scandir(".", &category, filter_hidden, alphasort)) < 0)
		goto ret;

	for (i = 0 ; i < a; i++) {
		stat(category[i]->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (strchr(category[i]->d_name, '-') == NULL)
			if ((strncmp(category[i]->d_name, "virtual", 7)) != 0)
				continue;

		if ((b = scandir(category[i]->d_name, &pn, filter_hidden, alphasort)) < 0)
			continue;
		for (c = 0; c < b; c++) {
			char de[_Q_PATH_MAX];

			snprintf(de, sizeof(de), "%s/%s", category[i]->d_name, pn[c]->d_name);

			if (stat(de, &st) < 0)
				continue;

			switch (cache_type) {
			case CACHE_EBUILD:
				if (!S_ISDIR(st.st_mode))
					continue;
				break;
			case CACHE_METADATA:
				if (S_ISREG(st.st_mode))
					fprintf(fp, "%s\n", de);
				continue;
				break;
			}
			if ((e = scandir(de, &eb, filter_hidden, alphasort)) < 0)
				continue;
			for (d = 0 ; d < e; d++) {
				if ((p = rindex(eb[d]->d_name, '.')) != NULL)
					if (strcmp(p, ".ebuild") == 0) {
						count++;
						fprintf(fp, "%s/%s/%s\n", category[i]->d_name, pn[c]->d_name, eb[d]->d_name);
					}
			}
			while(d--) free(eb[d]);
			free(eb);
		}
		while(b--) free(pn[b]);
		free(pn);
	}
	fclose(fp);
	while(a--) free(category[a]);
	free(category);
        
	gettimeofday(&finish, NULL);
	if (start.tv_usec > finish.tv_usec) {
		finish.tv_usec += 1000000;
		finish.tv_sec--;
        
	}
	frac = (finish.tv_usec - start.tv_usec);
	secs = (finish.tv_sec - start.tv_sec);
	if (secs < 0) secs = 0;
	if (frac < 0) frac = 0;
	warn("Finished %u entries in %d.%06d seconds", count, secs, frac);
ret:
	return cache_file;
}
#define initialize_ebuild_flat() initialize_flat(CACHE_EBUILD)
#define initialize_metadata_flat() initialize_flat(CACHE_METADATA)

void reinitialize_ebuild_flat(void)
{
	if ((chdir(portdir)) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		return;
	}
	unlink(CACHE_EBUILD_FILE);
	initialize_ebuild_flat();
}

void reinitialize_as_needed(void)
{
	if (reinitialize)
		reinitialize_ebuild_flat();
	if (reinitialize_metacache)
		initialize_metadata_flat();
}

typedef struct {
	char *_data;
	char *DEPEND;        /* line 1 */
	char *RDEPEND;
	char *SLOT;
	char *SRC_URI;
	char *RESTRICT;      /* line 5 */
	char *HOMEPAGE;
	char *LICENSE;
	char *DESCRIPTION;
	char *KEYWORDS;
	char *INHERITED;     /* line 10 */
	char *IUSE;
	char *CDEPEND;
	char *PDEPEND;
	char *PROVIDE;       /* line 14 */
	depend_atom *atom;
} portage_cache;

void cache_free(portage_cache *cache);
portage_cache *cache_read_file(const char *file);
portage_cache *cache_read_file(const char *file)
{
	struct stat s;
	char *ptr;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len;

	if ((f = fopen(file, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0)
		goto err;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xmalloc(len);
	memset(ret, 0x00, len);
#if 0
{	/* This is the expected format of portage-2.0 cache code NAME=var */
	/* at any time ferringb might decide to add new entries. CDEPEND= will be no more */
	char buf[BUFSIZ];
	while ((fgets(buf, sizeof(buf), f)) != NULL) {
		if ((ptr = strrchr(buf, '\n')) != NULL)
			*ptr = 0;

		if ((strncmp(buf, "DEPEND=", 7)) == 0)
			ret->DEPEND = xstrdup(buf + 7);

		if ((strncmp(buf, "DESCRIPTION=", 12)) == 0)
			ret->DESCRIPTION = xstrdup(buf + 12);
#if 0
		if ((strncmp(buf, "EAPI=", 5)) == 0)
			warnf("EAPI=%s", buf + 5);
#endif
		if ((strncmp(buf, "HOMEPAGE=", 9)) == 0)
			ret->HOMEPAGE = xstrdup(buf + 9);

		if ((strncmp(buf, "INHERITED=", 10)) == 0)
			ret->INHERITED = xstrdup(buf + 10);

		if ((strncmp(buf, "IUSE=", 4)) == 0)
			ret->IUSE = xstrdup(buf + 4);

		if ((strncmp(buf, "KEYWORDS=", 9)) == 0)
			ret->KEYWORDS = xstrdup(buf + 9);

		if ((strncmp(buf, "LICENSE=", 8)) == 0)
			ret->LICENSE = xstrdup(buf + 8);

		if ((strncmp(buf, "PDEPEND=", 8)) == 0)
			ret->PDEPEND = xstrdup(buf + 8);

		if ((strncmp(buf, "PROVIDE=", 8)) == 0)
			ret->PROVIDE = xstrdup(buf + 8);

		if ((strncmp(buf, "RDEPEND=", 8)) == 0)
			ret->RDEPEND = xstrdup(buf + 8);

		if ((strncmp(buf, "RESTRICT=", 9)) == 0)
			ret->RESTRICT = xstrdup(buf + 9);

		if ((strncmp(buf, "SLOT=", 5)) == 0)
			ret->SLOT = xstrdup(buf + 5);

		if ((strncmp(buf, "SRC_URI=", 8)) == 0)
			ret->SRC_URI = xstrdup(buf + 8);
	}
	rewind(f);
}
#endif
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->atom = atom_explode(file);
	ret->DEPEND = ret->_data;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->curr, '\n')) == NULL) { \
		warn("Invalid cache file '%s'", file); \
		goto err; \
	} \
	ret->next = ptr+1; \
	*ptr = '\0';
	next_line(DEPEND, RDEPEND)
	next_line(RDEPEND, SLOT)
	next_line(SLOT, SRC_URI)
	next_line(SRC_URI, RESTRICT)
	next_line(RESTRICT, HOMEPAGE)
	next_line(HOMEPAGE, LICENSE)
	next_line(LICENSE, DESCRIPTION)
	next_line(DESCRIPTION, KEYWORDS)
	next_line(KEYWORDS, INHERITED)
	next_line(INHERITED, IUSE)
	next_line(IUSE, CDEPEND)
	next_line(CDEPEND, PDEPEND)
	next_line(PDEPEND, PROVIDE)
#undef next_line
	ptr = strchr(ptr+1, '\n');
	*ptr = '\0';

	fclose(f);

	return ret;

err:
	if (ret) cache_free(ret);
	return NULL;
}

void cache_dump(portage_cache *cache);
void cache_dump(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");

	printf("DEPEND     : %s\n", cache->DEPEND);
	printf("RDEPEND    : %s\n", cache->RDEPEND);
	printf("SLOT       : %s\n", cache->SLOT);
	printf("SRC_URI    : %s\n", cache->SRC_URI);
	printf("RESTRICT   : %s\n", cache->RESTRICT);
	printf("HOMEPAGE   : %s\n", cache->HOMEPAGE);
	printf("LICENSE    : %s\n", cache->LICENSE);
	printf("DESCRIPTION: %s\n", cache->DESCRIPTION);
	printf("KEYWORDS   : %s\n", cache->KEYWORDS);
	printf("INHERITED  : %s\n", cache->INHERITED);
	printf("IUSE       : %s\n", cache->IUSE);
	printf("CDEPEND    : %s\n", cache->CDEPEND);
	printf("PDEPEND    : %s\n", cache->PDEPEND);
	printf("PROVIDE    : %s\n", cache->PROVIDE);
	if (!cache->atom) return;
	printf("CATEGORY   : %s\n", cache->atom->CATEGORY);
	printf("PN         : %s\n", cache->atom->PN);
	printf("PV         : %s\n", cache->atom->PV);
	printf("PVR        : %s\n", cache->atom->PVR);
}

void cache_free(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");
	atom_implode(cache->atom);
	free(cache);
}

char *grab_vdb_item(const char *, const char *, const char *);
char *grab_vdb_item(const char *item, const char *CATEGORY, const char *PF) {
        static char buf[_Q_PATH_MAX];
        char *p;
        FILE *fp;

	snprintf(buf, sizeof(buf), "%s%s/%s/%s/%s", portroot, portvdb, CATEGORY, PF, item);
	if ((fp = fopen(buf, "r")) == NULL)
		return NULL;        
	fgets(buf, sizeof(buf), fp);
	if ((p = strchr(buf, '\n')) != NULL)                
		*p = 0;
	fclose(fp);
	rmspace(buf);
        
	return buf;
}

queue *get_vdb_atoms(void);
queue *get_vdb_atoms(void) {
	int cfd, j;
	int dfd, i;

	char buf[_Q_PATH_MAX];
        static char savecwd[_POSIX_PATH_MAX];

	struct dirent **cat;
	struct dirent **pf;

	depend_atom *atom = NULL;
	queue *cpf = NULL;

        getcwd(savecwd, sizeof(savecwd));

	assert(chdir(savecwd) == 0);

	if (chdir(portroot) != 0)
		goto fuckit;

	if (chdir(portvdb) != 0)
		goto fuckit;

	memset(buf, 0, sizeof(buf));
	/* scan the cat first */
	if ((cfd = scandir(".", &cat, filter_hidden, alphasort)) < 0)
		goto fuckit;

	for (j = 0; j < cfd ; j++) {
		if (cat[j]->d_name[0] == '-')
			continue;
		if (chdir(cat[j]->d_name) != 0)
			continue;
		if ((dfd = scandir(".", &pf, filter_hidden, alphasort)) < 0) {
			chdir("..");
			continue;
		}
		for (i = 0; i < dfd; i++) {
			if (pf[i]->d_name[0] == '-')
				continue;
			snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name, pf[i]->d_name);
			if ((atom = atom_explode(buf)) == NULL)
				continue;

			strncat(buf, "/SLOT", sizeof(buf));
			if (access(buf, R_OK) == 0) {
				eat_file(buf, buf, sizeof(buf));
				rmspace(buf);
				if (strcmp(buf, "0") != 0);
			}

			if (atom->PR_int)
				snprintf(buf, sizeof(buf), "%s/%s-%s-r%i", atom->CATEGORY, atom->PN, atom->PV , atom->PR_int);
			else
				snprintf(buf, sizeof(buf), "%s/%s-%s", atom->CATEGORY, atom->PN, atom->PV);
			atom_implode(atom);
			cpf = add_set(buf, "0", cpf);
			
		}
		chdir("..");
		while(dfd--) free(pf[dfd]);
	}

	/* cleanup */
	if (cfd) while(cfd--) free(cat[cfd]);

fuckit:
	assert(chdir(savecwd) == 0);
	return cpf;
}

void cleanup() {
	reinitialize_as_needed();
	free_sets(virtuals);
	if (saved_stderr != stderr)
		fclose(stderr);
}

int main(int argc, char **argv)
{
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];
	saved_stderr = stderr;

#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(argv0, "/usr/share/locale");
	textdomain(argv0);
#endif

#if 0
	if (ttyname(1) == NULL)
		no_colors();
#endif
	if (getenv("TERM") == NULL)
		no_colors();

	initialize_portage_env();
	atexit(cleanup);
	optind = 0;
	return q_main(argc, argv);
}

#include "include_applets.h"
