/* abuild-sudo.c - limited root privileges for users in "abuild" group
 *
 * Copyright (C) 2012 Natanael Copa <ncopa@alpinelinux.org>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See http://www.gnu.org/ for details.
 */

#include <sys/types.h>

#include <err.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ABUILD_GROUP
#define ABUILD_GROUP "abuild"
#endif

static const char* valid_cmds[] = {
	"/bin/adduser",
	"/usr/sbin/adduser",
	"/bin/addgroup",
	"/usr/sbin/addgroup",
	"/sbin/apk",
	NULL
};

const char *get_command_path(const char *cmd)
{
	const char *p;
	int i;
	for (i = 0; valid_cmds[i] != NULL; i++) {
		if (access(valid_cmds[i], F_OK) == -1)
			continue;
		p = strrchr(valid_cmds[i], '/') + 1;
		if (strcmp(p, cmd) == 0)
			return valid_cmds[i];
	}
	return NULL;
}

int is_in_group(gid_t group)
{
	int ngroups_max = sysconf(_SC_NGROUPS_MAX) + 1;
	gid_t *buf = malloc(ngroups_max * sizeof(gid_t));
	int ngroups, ret = 0;
	int i;
	if (buf == NULL) {
		perror("malloc");
		return 0;
	}
	ngroups = getgroups(ngroups_max, buf);
	for (i = 0; i < ngroups; i++) {
		if (buf[i] == group)
			break;
	}
	free(buf);
	return i < ngroups;
}

int main(int argc, const char *argv[])
{
	struct group *grent;
	const char *cmd;
	const char *path;

	grent = getgrnam(ABUILD_GROUP);
	if (grent == NULL)
		errx(1, "%s: Group not found", ABUILD_GROUP);

	if (!is_in_group(grent->gr_gid))
		errx(1, "Not a member of group %s\n", ABUILD_GROUP);

	cmd = strrchr(argv[0], '-');
	if (cmd == NULL)
		errx(1, "Calling command has no '-'");
	cmd++;

	path = get_command_path(cmd);
	if (path == NULL)
		errx(1, "%s: Not a valid subcommand", cmd);

	argv[0] = path;
	/* set our uid to root so bbsuid --install works */
	setuid(0);
	execv(path, (char * const*)argv);
	perror(path);
	return 1;
}
