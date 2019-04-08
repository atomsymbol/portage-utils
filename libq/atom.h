/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _ATOM_COMPARE_H
#define _ATOM_COMPARE_H 1

typedef enum {
	VER_ALPHA=0, VER_BETA, VER_PRE, VER_RC, VER_NORM, VER_P
} atom_suffixes;
extern const char * const atom_suffixes_str[];

/* slotdeps, := :* :SLOT= */
typedef enum {
	/*   */ ATOM_SD_NONE = 0,
	/* = */ ATOM_SD_ANY_REBUILD,
	/* * */ ATOM_SD_ANY_IGNORE
} atom_slotdep;
extern const char * const atom_slotdep_str[];

typedef enum {
	/*     */ ATOM_UC_NONE = 0,
	/* !   */ ATOM_UC_NOT,
	/* -   */ ATOM_UC_NEG,
	/* ?   */ ATOM_UC_COND,
	/* =   */ ATOM_UC_EQUAL,
	/* (+) */ ATOM_UC_PREV_ENABLED,
	/* (-) */ ATOM_UC_PREV_DISABLED,
} atom_usecond;
extern const char * const atom_usecond_str[];

typedef enum {
	/*    */ ATOM_BL_NONE = 0,
	/* !  */ ATOM_BL_BLOCK,
	/* !! */ ATOM_BL_BLOCK_HARD,
} atom_blocker;
extern const char * const atom_blocker_str[];

typedef enum {
	/*    */ ATOM_OP_NONE = 0,
	/* =  */ ATOM_OP_EQUAL,
	/* >  */ ATOM_OP_NEWER,
	/* >= */ ATOM_OP_NEWER_EQUAL,
	/* <  */ ATOM_OP_OLDER,
	/* <= */ ATOM_OP_OLDER_EQUAL,
	/* ~  */ ATOM_OP_PV_EQUAL,
	/* *  */ ATOM_OP_STAR,
	/*    */ ATOM_OP_NEQUAL,
} atom_operator;
extern const char * const atom_op_str[];

typedef struct {
	atom_suffixes suffix;
	uint64_t sint;
} atom_suffix;

typedef struct _atom_usedep {
	struct _atom_usedep *next;
	char *use;
	atom_usecond pfx_cond;
	atom_usecond sfx_cond;
} atom_usedep;

typedef struct {
	atom_blocker blocker;
	atom_operator pfx_op;
	atom_operator sfx_op;
	char *CATEGORY;
	char *PN;
	char *PV;
	unsigned int PR_int;
	char letter;
	atom_suffix *suffixes;
	char *PVR;
	char *P;
	atom_usedep *usedeps;
	char *SLOT;
	atom_slotdep slotdep;
	char *REPO;
} depend_atom;

extern const char * const booga[];
enum { ERROR=0, NOT_EQUAL, EQUAL, NEWER, OLDER };

depend_atom *atom_explode(const char *atom);
void atom_implode(depend_atom *atom);
int atom_compare(const depend_atom *a1, const depend_atom *a2);
int atom_compare_str(const char * const s1, const char * const s2);
char *atom_to_string(depend_atom *a);

#endif