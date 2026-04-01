#pragma once

typedef struct {
    int fd;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *, ...);
