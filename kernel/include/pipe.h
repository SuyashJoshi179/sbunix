#pragma once

#include <stdint.h>

struct file;

#define PIPESIZE 512

struct pipe {
    char     data[PIPESIZE];
    uint32_t nread;
    uint32_t nwrite;
    int      readopen;
    int      writeopen;
};

int  pipe_alloc(struct file **rf, struct file **wf);
int  pipe_read(struct pipe *p, char *buf, int n);
int  pipe_write(struct pipe *p, const char *buf, int n);
void pipe_close(struct pipe *p, int writable);
