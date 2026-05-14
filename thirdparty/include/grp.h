#pragma once
#include <sys/types.h>

struct group {
	char *gr_name;
	char *gr_passwd;
	gid_t gr_gid;
	char **gr_mem;
};

int getgroups(int size, gid_t list[]);
int setgroups(size_t size, const gid_t *list);
