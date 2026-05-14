#pragma once
#include <sys/types.h>

struct statfs {
	unsigned long f_type;
	unsigned long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	unsigned long f_namelen;
};

