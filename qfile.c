/*
 * Copyright 2005-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.51 2010/01/16 23:45:02 vapier Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qfile

#define QFILE_MAX_MAX_ARGS 5000000
#define QFILE_DEFAULT_MAX_ARGS 5000
#define QFILE_DEFAULT_MAX_ARGS_STR "5000"

#define QFILE_FLAGS "ef:m:oRx:S" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	{"from",        a_argument,  NULL, 'f'},
	{"max-args",    a_argument,  NULL, 'm'},
	{"orphans",     no_argument, NULL, 'o'},
	{"root-prefix", no_argument, NULL, 'R'},
	{"exclude",     a_argument,  NULL, 'x'},
	{"slots",       no_argument, NULL, 'S'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	"Read arguments from file <arg> (\"-\" for stdin)",
	"Treat from file arguments by groups of <arg> (defaults to " QFILE_DEFAULT_MAX_ARGS_STR ")",
	"List orphan files",
	"Assume arguments are already prefixed by $ROOT",
	"Don't look in package <arg>",
	"Display installed packages with slots",
	COMMON_OPTS_HELP
};
static char qfile_rcsid[] = "$Id: qfile.c,v 1.51 2010/01/16 23:45:02 vapier Exp $";
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, lookup_applet_idx("qfile"))

#define qfile_is_prefix(path, prefix, prefix_length) \
	(!prefix_length \
		|| (strlen(path) >= prefix_length \
			&& (path[prefix_length] == '/' || path[prefix_length] == '\0') \
			&& !strncmp(path, prefix, prefix_length)))

typedef struct {
	int length;
	char **basenames;
	char **dirnames;
	char **realdirnames;
	char *bn_firstchars;
	short *non_orphans;
	char *real_root;
	char *exclude_pkg;
	char *exclude_slot;
} qfile_args_t;

char slotted = 0;

/*
 * We assume the people calling us have chdir(/var/db/pkg) and so
 * we use relative paths throughout here.
 */
void qfile(char *, const char *, qfile_args_t *);
void qfile(char *path, const char *root, qfile_args_t *args)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *entry_basename;
	char *p;
	char buf[1024];
	char pkg[150];
	depend_atom *atom = NULL;
	int i, path_ok;
	char bn_firstchar;
	char *real_root = args->real_root;
	char **base_names = args->basenames;
	char **dir_names = args->dirnames;
	char **real_dir_names = args->realdirnames;
	char *bn_firstchars = args->bn_firstchars;
	short *non_orphans = args->non_orphans;

	if ((dir = opendir(path)) == NULL) {
		warnp("opendir(%s) failed", path);
		return;
	}

	while ((dentry = readdir(dir))) {
		if (dentry->d_name[0] == '.')
			continue;

		/* We reuse pkg for reading files in the subdir */
		p = basename(path);
		i = snprintf(pkg, sizeof(pkg), "%s/%s", p, dentry->d_name);
		if (i + 20 >= sizeof(pkg)) {
			warn("skipping long pkg name: %s/%s", p, dentry->d_name);
			continue;
		}
		p = pkg + i;

		/* Will be exploded once at most, as needed. */
		if (atom) {
			atom_implode(atom);
			atom = NULL;
		}

		/* If exclude_pkg is not NULL, check it.  We are looking for files
		 * collisions, and must exclude one package.
		 */
		if (args->exclude_pkg != NULL) {
			if (strncmp(args->exclude_pkg, pkg, sizeof(pkg)) == 0)
				goto check_pkg_slot; /* CAT/PF matches */
			if (strcmp(args->exclude_pkg, dentry->d_name) == 0)
				goto check_pkg_slot; /* PF matches */
			if ((atom = atom_explode(pkg)) == NULL
					|| atom->PN == NULL || atom->CATEGORY == NULL) {
				warn("invalid atom %s", pkg);
				goto dont_skip_pkg;
			}
			snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			if (strncmp(args->exclude_pkg, buf, sizeof(buf)) != 0
					&& strcmp(args->exclude_pkg, atom->PN) != 0)
				goto dont_skip_pkg; /* "(CAT/)?PN" doesn't match */
check_pkg_slot: /* Also compare slots, if any was specified */
			if (args->exclude_slot == NULL)
				continue; /* "(CAT/)?(PN|PF)" matches, and no SLOT specified */
			buf[0] = '0'; buf[1] = '\0';
			strcpy(p, "/SLOT");
			eat_file(pkg, buf, sizeof(buf));
			rmspace(buf);
			*p = '\0';
			if (strncmp(args->exclude_slot, buf, sizeof(buf)) == 0)
				continue; /* "(CAT/)?(PN|PF):SLOT" matches */
		}
dont_skip_pkg: /* End of the package exclusion tests. */

		strcpy(p, "/CONTENTS");
		fp = fopen(pkg, "r");
		*p = '\0';
		if (fp == NULL)
			continue;

		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			contents_entry *e;
			e = contents_parse_line(buf);
			if (!e)
				continue;

			/* assume sane basename() -- doesnt modify argument */
			if ((entry_basename = basename(e->name)) == NULL)
				continue;

			/* used to cut the number of strcmp() calls */
			bn_firstchar = entry_basename[0];

			for (i = 0; i < args->length; i++) {
				if (base_names[i] == NULL)
					continue;
				if (non_orphans != NULL && non_orphans[i])
					continue;
				path_ok = (dir_names[i] == NULL && real_dir_names[i] == NULL);

				if (bn_firstchar != bn_firstchars[i]
						|| strcmp(entry_basename, base_names[i]))
					continue;

				if (!path_ok) {
					/* check the full filepath ... */
					size_t dirname_len = (entry_basename - e->name - 1);
					/* basename(/usr)     = usr, dirname(/usr)     = /
					 * basename(/usr/bin) = bin, dirname(/usr/bin) = /usr
					 */
					if (dirname_len == 0)
						dirname_len = 1;

					if (dir_names[i] &&
					    strncmp(e->name, dir_names[i], dirname_len) == 0 &&
					    dir_names[i][dirname_len] == '\0')
						/* dir_name == dirname(CONTENTS) */
						path_ok = 1;

					else if (real_dir_names[i] &&
					         strncmp(e->name, real_dir_names[i], dirname_len) == 0 &&
					         real_dir_names[i][dirname_len] == '\0')
						/* real_dir_name == dirname(CONTENTS) */
						path_ok = 1;

					else if (real_root[0]) {
						char rpath[_Q_PATH_MAX + 1], *_rpath;
						char *fullpath;
						size_t real_root_len = strlen(real_root);

						xasprintf(&fullpath, "%s%s", real_root, e->name);
						fullpath[real_root_len + dirname_len] = '\0';
						_rpath = rpath + real_root_len;
						if (realpath(fullpath, rpath) == NULL) {
							if (verbose) {
								warnp("Could not read real path of \"%s\" (from %s)", fullpath, pkg);
								warn("We'll never know whether \"%s\" was a result for your query...",
										e->name);
							}
						} else if (!qfile_is_prefix(rpath, real_root, real_root_len)) {
							if (verbose)
								warn("Real path of \"%s\" is not under ROOT: %s", fullpath, rpath);
						} else if (dir_names[i] &&
						           strcmp(_rpath, dir_names[i]) == 0) {
							/* dir_name == realpath(dirname(CONTENTS)) */
							path_ok = 1;
						} else if (real_dir_names[i] &&
						           strcmp(_rpath, real_dir_names[i]) == 0) {
							/* real_dir_name == realpath(dirname(CONTENTS)) */
							path_ok = 1;
						}
						free(fullpath);
					}
				}
				if (!path_ok)
					continue;

				if (non_orphans == NULL) {
					char pkgslot[126];

					if (atom == NULL && (atom = atom_explode(pkg)) == NULL) {
						warn("invalid atom %s", pkg);
						continue;
					}
					if (slotted) {
						strcpy(p, "/SLOT");
						eat_file(pkg, pkgslot+1, sizeof(pkgslot)-1);
						rmspace(pkgslot+1);
						*pkgslot = ':';
						*p = '\0';
					} else
						*pkgslot = '\0';
					printf("%s%s/%s%s%s%s", BOLD, atom->CATEGORY, BLUE,
						(exact ? dentry->d_name : atom->PN),
						pkgslot, NORM);
					if (quiet)
						puts("");
					else
						printf(" (%s%s)\n", root ? : "", e->name);

				} else {
					non_orphans[i] = 1;
				}
				found++;
			}
		}
		fclose(fp);
	}
	closedir(dir);

	return;
}

qfile_args_t *create_qfile_args();
qfile_args_t *create_qfile_args()
{
	qfile_args_t *qfile_args;

	qfile_args = xmalloc(sizeof(qfile_args_t));

	memset(qfile_args, 0, sizeof(qfile_args_t));
	return qfile_args;
}

void destroy_qfile_args(qfile_args_t *);
void destroy_qfile_args(qfile_args_t *qfile_args)
{
	int i;

	for (i = 0; i < qfile_args->length; ++i) {
		if (qfile_args->basenames != NULL && qfile_args->basenames[i] != NULL)
			free(qfile_args->basenames[i]);
		if (qfile_args->dirnames != NULL && qfile_args->dirnames[i] != NULL)
			free(qfile_args->dirnames[i]);
		if (qfile_args->realdirnames != NULL && qfile_args->realdirnames[i] != NULL)
			free(qfile_args->realdirnames[i]);
	}

	if (qfile_args->basenames != NULL)
		free(qfile_args->basenames);
	if (qfile_args->dirnames != NULL)
		free(qfile_args->dirnames);
	if (qfile_args->realdirnames != NULL)
		free(qfile_args->realdirnames);

	if (qfile_args->bn_firstchars != NULL)
		free(qfile_args->bn_firstchars);

	if (qfile_args->non_orphans != NULL)
		free(qfile_args->non_orphans);

	if (qfile_args->real_root != NULL)
		free(qfile_args->real_root);

	if (qfile_args->exclude_pkg != NULL)
		free(qfile_args->exclude_pkg);
	/* don't free qfile_args->exclude_slot, it's the same chunk */

	memset(qfile_args, 0, sizeof(qfile_args_t));
}

int prepare_qfile_args(const int, const char **,
		const short, const short, const char *, qfile_args_t *);
int prepare_qfile_args(const int argc, const char **argv,
		const short assume_root_prefix, const short search_orphans,
		const char *exclude_pkg_arg, qfile_args_t *qfile_args)
{
	int i;
	int nb_of_queries = argc;
	char *pwd = NULL;
	int real_root_length;
	char *real_root = NULL;
	char **basenames = NULL;
	char **dirnames = NULL;
	char **realdirnames = NULL;
	char *basenames_firstchars = NULL;
	char tmppath[_Q_PATH_MAX+1];
	char abspath[_Q_PATH_MAX+1];

	/* Try to get $PWD. Must be absolute, with no trailing slash. */
	if ((pwd = getenv("PWD")) != NULL && pwd[0] == '/') {
		pwd = xstrdup(pwd);
		if ((pwd[strlen(pwd) - 1] == '/'))
			pwd[strlen(pwd) - 1] = '\0';
	} else
		pwd = NULL;

	/* Get realpath of $ROOT, with no trailing slash */
	if (portroot[0] == '/')
		strncpy(tmppath, portroot, _Q_PATH_MAX);
	else if (pwd != NULL)
		snprintf(tmppath, _Q_PATH_MAX, "%s/%s", pwd, portroot);
	else {
		free(pwd);
		warn("Could not get absolute path for ROOT (\"%s\"), because of missing or not absolute $PWD", tmppath);
		return -1;
	}
	if (realpath(tmppath, abspath) == NULL) {
		free(pwd);
		warnp("Could not read real path of ROOT (\"%s\")", tmppath);
		return -1;
	}
	if (strlen(abspath) == 1)
		abspath[0] = '\0';
	real_root = xstrdup(abspath);
	real_root_length = strlen(real_root);

	/* For each argument, we store its basename, its absolute dirname,
	 * and the realpath of its dirname.  Dirnames and their realpaths
	 * are stored without their $ROOT prefix, but $ROOT is used when
	 * checking realpaths.
	 */
	basenames = xcalloc(argc, sizeof(char*));
	dirnames = xcalloc(argc, sizeof(char*));
	realdirnames = xcalloc(argc, sizeof(char*));
	/* For optimization of qfile(), we also give it an array of the first char
	 * of each basename.  This way we avoid numerous strcmp() calls.
	 */
	basenames_firstchars = xcalloc(argc, sizeof(char));

	for (i = 0; i < argc; ++i) {
		/* Record basename, but if it is ".", ".." or "/" */
		strncpy(abspath, argv[i], _Q_PATH_MAX); /* strncopy so that "argv" can be "const" */
		strncpy(tmppath, basename(abspath), _Q_PATH_MAX);
		if ((strlen(tmppath) > 2) ||
		    (strncmp(tmppath, "..", strlen(tmppath))
		     && strncmp(tmppath, "/", strlen(tmppath))))
		{
			basenames[i] = xstrdup(tmppath);
			basenames_firstchars[i] = basenames[i][0];
			/* If there is no "/" in the argument, then it's over.
			 * (we are searching a simple file name)
			 */
			if (strchr(argv[i], '/') == NULL)
				continue;
		}

		/* Make sure we have an absolute path available (with "realpath(ROOT)" prefix) */
		if (argv[i][0] == '/') {
			if (assume_root_prefix)
				strncpy(abspath, argv[i], _Q_PATH_MAX);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s", real_root, argv[i]);
		} else if (pwd != NULL) {
			if (assume_root_prefix)
				snprintf(abspath, _Q_PATH_MAX, "%s/%s", pwd, argv[i]);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s/%s", real_root, pwd, argv[i]);
		} else {
			warn("$PWD was not found in environment, or is not an absolute path");
			goto skip_query_item;
		}

		if (basenames[i] != NULL) {
			/* Get both the dirname and its realpath.  This paths will
			 * have no trailing slash, but if it is the only char (ie.,
			 * when searching for "/foobar").
			 */
			strncpy(tmppath, abspath, _Q_PATH_MAX);
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (abspath[real_root_length] == '\0')
				strncat(abspath, "/", 1);
			dirnames[i] = xstrdup(abspath + real_root_length);
			if (realpath(abspath, tmppath) == NULL) {
				if (verbose) {
					warnp("Could not read real path of \"%s\"", abspath);
					warn("Results for query item \"%s\" may be inaccurate.", argv[i]);
				}
				continue;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_length)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			if (tmppath[real_root_length] == '\0')
				strncat(tmppath, "/", 1);
			if (strcmp(dirnames[i], tmppath + real_root_length))
				realdirnames[i] = xstrdup(tmppath + real_root_length);
		} else {
			/* No basename means we are looking for something like "/foo/bar/.."
			 * Dirname is meaningless here, we can only get realpath of the full
			 * path and then split it.
			 */
			if (realpath(abspath, tmppath) == NULL) {
				warnp("Could not read real path of \"%s\"", abspath);
				goto skip_query_item;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_length)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			strncpy(abspath, tmppath, _Q_PATH_MAX);
			basenames[i] = xstrdup(basename(abspath));
			basenames_firstchars[i] = basenames[i][0];
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (tmppath[real_root_length] == '\0')
				strncat(tmppath, "/", 1);
			realdirnames[i] = xstrdup(abspath + real_root_length);
		}
		continue;

		skip_query_item:
			--nb_of_queries;
			warn("Skipping query item \"%s\".", argv[i]);
			if (basenames[i] != NULL) free(basenames[i]);
			if (dirnames[i] != NULL) free(dirnames[i]);
			if (realdirnames[i] != NULL) free(realdirnames[i]);
			basenames[i] = dirnames[i] = realdirnames[i] = NULL;
	}

	if (pwd != NULL)
		free(pwd);

	qfile_args->real_root = real_root;
	qfile_args->basenames = basenames;
	qfile_args->dirnames = dirnames;
	qfile_args->realdirnames = realdirnames;
	qfile_args->bn_firstchars = basenames_firstchars;
	qfile_args->length = argc;

	if (search_orphans) {
		qfile_args->non_orphans = xcalloc(argc, sizeof(short));
		memset(qfile_args->non_orphans, 0, argc);
	}

	if (exclude_pkg_arg != NULL) {
		qfile_args->exclude_pkg = xstrdup(exclude_pkg_arg);
		if ((qfile_args->exclude_slot = strchr(qfile_args->exclude_pkg, ':')) != NULL)
			*qfile_args->exclude_slot++ = '\0';
		/* Maybe this should be atom-exploded instead (to check syntax, etc.) */
	}

	return nb_of_queries;
}

int qfile_main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *dentry;
	int i, nb_of_queries;
	char *p;
	short search_orphans = 0;
	short assume_root_prefix = 0;
	char *root_prefix = NULL;
	char *exclude_pkg_arg = NULL;
	qfile_args_t *qfile_args = NULL;
	int qargc = 0;
	char **qargv = NULL;
	short done = 0;
	FILE *args_file = NULL;
	int max_args = QFILE_DEFAULT_MAX_ARGS;
	char path[_Q_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'S': slotted = 1; break;
			case 'e': exact = 1; break;
			case 'f':
				if (args_file != NULL) {
					warn("Don't use -f twice!");
					goto exit;
				}
				if (strcmp(optarg, "-") == 0)
					args_file = stdin;
				else if ((args_file = fopen(optarg, "r")) == NULL) {
					warnp("%s", optarg);
					goto exit;
				}
				break;
			case 'm':
				errno = 0;
				max_args = strtol(optarg, &p, 10);
				if (errno != 0) {
					warnp("%s: not a valid integer", optarg);
					goto exit;
				} else if (p == optarg || *p != '\0') {
					warn("%s: not a valid integer", optarg);
					goto exit;
				}
				if (max_args <= 0 || max_args > QFILE_MAX_MAX_ARGS) {
					warn("%s: silly value!", optarg);
					goto exit;
				}
				break;
			case 'o': search_orphans = 1; break;
			case 'R': assume_root_prefix = 1; break;
			case 'x':
				if (exclude_pkg_arg != NULL) {
					warn("--exclude can only be used once.");
					goto exit;
				}
				exclude_pkg_arg = optarg;
				break;
		}
	}
	if (!exact && verbose) exact++;
	if ((argc == optind) && (args_file == NULL))
		qfile_usage(EXIT_FAILURE);

	if ((args_file == NULL) && (max_args != QFILE_DEFAULT_MAX_ARGS))
		warn("--max-args is only used when reading arguments from a file (with -f)");

	xchdir(portroot);
	xchdir(portvdb);

	/* Get a copy of $ROOT, with no trailing slash
	 * (this one is just for qfile(...) output)
	 */
	root_prefix = xstrdup(portroot);
	if (root_prefix[strlen(root_prefix) - 1] == '/')
		root_prefix[strlen(root_prefix) - 1] = '\0';

	/* Are we using --from ? */
	if (args_file == NULL) {
		qargc = argc - optind;
		qargv = argv + optind;
		done = 1;
	} else {
		qargv = xcalloc(max_args, sizeof(char*));
	}

	do { /* This block may be repeated if using --from with a big files list */
		if (args_file != NULL) {
			qargc = 0;
			/* Read up to max_args files from the input file */
			while ((fgets(path, _Q_PATH_MAX, args_file)) != NULL) {
				if ((p = strchr(path, '\n')) != NULL)
					*p = '\0';
				if (path == p) continue;
				qargv[qargc] = xstrdup(path);
				qargc++;
				if (qargc >= max_args) break;
			}
		}

		if (qargc == 0) break;

		if (qfile_args == NULL) { /* qfile_args is allocated only once */
			if ((qfile_args = create_qfile_args()) == NULL) {
				warn("Out of memory");
				goto exit;
			}
		} else {
			destroy_qfile_args(qfile_args);
		}

		/* Prepare the qfile(...) arguments structure */
		nb_of_queries = prepare_qfile_args(qargc, (const char **) qargv,
				assume_root_prefix, search_orphans, exclude_pkg_arg, qfile_args);
		if (nb_of_queries < 0)
			goto exit;

		if ((dir = opendir(".")) == NULL) {
			warnp("could not read(ROOT/%s) for installed packages database", portvdb);
			goto exit;
		}

		/* Iteration over VDB categories */
		while (nb_of_queries && (dentry = q_vdb_get_next_dir(dir))) {
			snprintf(path, _Q_PATH_MAX, "%s/%s/%s", qfile_args->real_root, portvdb, dentry->d_name);
			qfile(path, (assume_root_prefix ? root_prefix : NULL), qfile_args);
		}

		if (qfile_args->non_orphans != NULL) {
			/* display orphan files */
			for (i = 0; i < qfile_args->length; i++) {
				if (qfile_args->non_orphans[i])
					continue;
				if (qfile_args->basenames[i] != NULL) {
					found = 0; /* ~inverse return code (as soon as an orphan is found, return non-zero) */
					if (!quiet)
						printf("%s\n", qargv[i]);
					else break;
				}
			}
		}

		if (args_file != NULL && qargv != NULL) {
			for (i = 0; i < qargc; i++) {
				if (qargv[i] != NULL) free(qargv[i]);
				qargv[i] = NULL;
			}
		}
	} while (args_file != NULL && qargc == max_args);

exit:

	if (args_file != NULL && qargv != NULL) {
		for (i = 0; i < qargc; i++)
			if (qargv[i] != NULL) free(qargv[i]);
		free(qargv);
	}

	if (args_file != NULL && args_file != stdin)
		fclose(args_file);

	if (qfile_args != NULL) {
		destroy_qfile_args(qfile_args);
		free(qfile_args);
	}

	if (root_prefix != NULL)
		free(root_prefix);

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}

#else
DEFINE_APPLET_STUB(qfile)
#endif
