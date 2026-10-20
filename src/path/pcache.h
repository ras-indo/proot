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

#ifndef PCACHE_H
#define PCACHE_H

#include <stdbool.h>
#include <limits.h>

#include "tracee/tracee.h"
#include "syscall/sysnum.h"

/**
 * Drop every cached path translation.  Must be called whenever the
 * way a guest path resolves to a host path can change: creation,
 * removal or renaming of symlinks, directory tree changes, chdir(2),
 * mount(2)/umount(2), or any change to the bindings of a file-system
 * name-space.
 */
extern void path_cache_reset(void);

/**
 * Return true and fill @result when a previously computed translation
 * of @guest_path (with the given @deref_final) is still valid for the
 * current file-system name-space of @tracee.  @guest_path is expected
 * to be an absolute guest path, as produced by translate_path().
 */
extern bool path_cache_get(const Tracee *tracee, const char *guest_path,
			   bool deref_final, char result[PATH_MAX]);

/**
 * Remember that @guest_path (with the given @deref_final) translates
 * to @result within the current file-system name-space of @tracee.
 */
extern void path_cache_put(const Tracee *tracee, const char *guest_path,
			   bool deref_final, const char result[PATH_MAX]);

/**
 * Return true when @sysnum is able to change how guest paths resolve
 * (create/remove/rename a name, change the cwd, mount, ...), i.e. when
 * the cache must be invalidated before it is handled.
 */
extern bool path_cache_sysnum_invalidates(Sysnum sysnum);

#endif /* PCACHE_H */
