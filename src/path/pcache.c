/* -*- c-set-style: "K&R"; c-basic-offset: 8 -*-
 *
 * This file is part of PRoot.
 *
 * Copyright (C) 2015 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "path/pcache.h"

/**
 * A small direct-mapped memo of fully translated host paths.
 *
 * PRoot recomputes the host path of every argument of every
 * path-related syscall from scratch: canonicalize() walks the whole
 * guest path component by component and lstat(2)s each of them, then
 * the bindings are substituted.  Programs issue the very same path
 * syscalls again and again (an interpreter dlopen()ing the same
 * libraries, a package manager stat()ing the same directories, a shell
 * hashing $PATH, ...), so remembering the result removes a large part
 * of PRoot's per-syscall overhead for those workloads.
 *
 * Correctness rules:
 *
 *  - Only absolute guest paths are cached.  Relative paths depend on
 *    the current working directory and on AT_FDCWD/dir_fd, which are
 *    handled by the caller, not here.
 *
 *  - Paths under "/proc" are never cached: their translation can
 *    depend on the tracee itself (e.g. "/proc/self/exe").
 *
 *  - The host path is a pure function of the absolute guest path, the
 *    "deref_final" flag and the tracee's file-system name-space (root
 *    + bindings), so tracee->fs is part of the key.  Two tracees with
 *    different name-spaces can therefore never share a wrong entry.
 *
 *  - Any event able to change how a name resolves (see
 *    path_cache_reset() callers) invalidates the whole table; a stale
 *    entry is therefore never used.
 *
 * The cache can be disabled at runtime by setting PROOT_NO_PATH_CACHE
 * (in the same spirit as PROOT_NO_SECCOMP), and statistics can be
 * printed on exit by setting PROOT_PATH_CACHE_STATS.
 */

#define PCACHE_SLOTS	256		/* must be a power of two */
#define PCACHE_MASK	(PCACHE_SLOTS - 1)
#define PCACHE_MAXLEN	768		/* guest paths this long are not cached */

typedef struct {
    const void *fs;			/* Tracee.fs identity */
    unsigned long generation;		/* cache generation this entry belongs to */
    bool deref_final;
    bool used;
    char guest[PCACHE_MAXLEN];
    char host[PATH_MAX];
} PathCacheEntry;

static PathCacheEntry pcache[PCACHE_SLOTS];

/* Bumped on every invalidation so that stale entries are ignored
 * without having to walk the whole table.  Starts at 1 so that an
 * untouched (all-zero) entry never matches.  */
static unsigned long pcache_generation = 1;

static bool pcache_off_checked = false;
static bool pcache_off = false;

static unsigned long pcache_hits = 0;
static unsigned long pcache_misses = 0;

static void pcache_report(void)
{
    if (getenv("PROOT_PATH_CACHE_STATS") != NULL)
	fprintf(stderr, "[pcache] hits=%lu misses=%lu\n",
		pcache_hits, pcache_misses);
}

static bool pcache_disabled(void)
{
    if (!pcache_off_checked) {
	pcache_off = (getenv("PROOT_NO_PATH_CACHE") != NULL);
	if (getenv("PROOT_PATH_CACHE_STATS") != NULL)
	    atexit(pcache_report);
	pcache_off_checked = true;
    }
    return pcache_off;
}

void path_cache_reset(void)
{
    pcache_generation++;

    /* Only a full wrap-around requires an explicit wipe; the
     * generation check in path_cache_get() ignores older entries.  */
    if (pcache_generation == 0) {
	memset(pcache, 0, sizeof(pcache));
	pcache_generation = 1;
    }
}

static uint64_t hash_update(const char *string, uint64_t hash)
{
    const unsigned char *cursor = (const unsigned char *) string;

    while (*cursor != '\0') {
	hash ^= *cursor++;
	hash *= 1099511628211ULL;	/* FNV-1a prime */
    }
    return hash;
}

static unsigned int slot_of(const Tracee *tracee, const char *guest_path,
			    bool deref_final)
{
    uint64_t hash = 1469598103934665603ULL;	/* FNV-1a offset basis */

    hash = hash_update(guest_path, hash);
    hash ^= (uint64_t) (uintptr_t) tracee->fs;
    hash *= 1099511628211ULL;
    hash ^= deref_final ? 0x9e3779b97f4a7c15ULL : 0ULL;
    hash *= 1099511628211ULL;

    return (unsigned int) (hash & PCACHE_MASK);
}

static bool cacheable(const Tracee *tracee, const char *guest_path)
{
    size_t length;

    if (pcache_disabled())
	return false;
    if (tracee == NULL || tracee->fs == NULL)
	return false;
    if (guest_path == NULL || guest_path[0] != '/')
	return false;

    /* "/proc", "/proc/..." : resolution may depend on the tracee.  */
    if (strncmp(guest_path, "/proc", 5) == 0
	&& (guest_path[5] == '\0' || guest_path[5] == '/'))
	return false;

    length = strlen(guest_path);
    return length > 0 && length < PCACHE_MAXLEN;
}

bool path_cache_get(const Tracee *tracee, const char *guest_path,
		    bool deref_final, char result[PATH_MAX])
{
    PathCacheEntry *entry;

    if (!cacheable(tracee, guest_path))
	return false;

    entry = &pcache[slot_of(tracee, guest_path, deref_final)];

    if (!entry->used
	|| entry->generation != pcache_generation
	|| entry->fs != tracee->fs
	|| entry->deref_final != deref_final
	|| strcmp(entry->guest, guest_path) != 0) {
	pcache_misses++;
	return false;
    }

    pcache_hits++;
    strcpy(result, entry->host);
    return true;
}

void path_cache_put(const Tracee *tracee, const char *guest_path,
		    bool deref_final, const char result[PATH_MAX])
{
    PathCacheEntry *entry;

    if (!cacheable(tracee, guest_path))
	return;
    if (result == NULL || strlen(result) >= PATH_MAX)
	return;

    entry = &pcache[slot_of(tracee, guest_path, deref_final)];

    entry->fs = tracee->fs;
    entry->generation = pcache_generation;
    entry->deref_final = deref_final;
    entry->used = true;
    strcpy(entry->guest, guest_path);
    strcpy(entry->host, result);
}

bool path_cache_sysnum_invalidates(Sysnum sysnum)
{
    switch (sysnum) {
    /* Create/remove/rename a name: the way other paths resolve (in
     * particular through symlinks) may change.  */
    case PR_symlink:
    case PR_symlinkat:
    case PR_rename:
    case PR_renameat:
    case PR_renameat2:
    case PR_unlink:
    case PR_unlinkat:
    case PR_rmdir:
    case PR_mkdir:
    case PR_mkdirat:
    case PR_mknod:
    case PR_mknodat:
    case PR_link:
    case PR_linkat:

    /* Change the reference used to resolve relative paths.  */
    case PR_chdir:
    case PR_fchdir:
    case PR_chroot:

    /* Change the name-space itself.  */
    case PR_mount:
    case PR_umount:
    case PR_umount2:
    case PR_pivot_root:
	return true;

    default:
	return false;
    }
}
