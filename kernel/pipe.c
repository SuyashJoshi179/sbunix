#include <pipe.h>
#include <file.h>
#include <proc.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define NPIPE 8

static struct pipe pipes[NPIPE];

static struct pipe *pipe_pool_alloc(void) {
    for (int i = 0; i < NPIPE; i++) {
        if (!pipes[i].readopen && !pipes[i].writeopen) {
            memset(&pipes[i], 0, sizeof(struct pipe));
            return &pipes[i];
        }
    }
    return 0;
}

int pipe_alloc(struct file **rf, struct file **wf) {
    struct pipe *p = pipe_pool_alloc();
    if (!p) return -ENOMEM;

    *rf = filealloc();
    if (!*rf) return -ENOMEM;

    *wf = filealloc();
    if (!*wf) {
        fileclose(*rf);
        *rf = 0;
        return -ENOMEM;
    }

    p->readopen  = 1;
    p->writeopen = 1;
    p->nread     = 0;
    p->nwrite    = 0;

    (*rf)->type     = FD_PIPE;
    (*rf)->readable = 1;
    (*rf)->writable = 0;
    (*rf)->pipe     = p;
    (*rf)->ip       = 0;

    (*wf)->type     = FD_PIPE;
    (*wf)->readable = 0;
    (*wf)->writable = 1;
    (*wf)->pipe     = p;
    (*wf)->ip       = 0;

    return 0;
}

int pipe_read(struct pipe *p, char *buf, int n) {
    while (p->nread == p->nwrite && p->writeopen) {
        proc_sleep_chan(p);
        if (sig_has_actionable(current_proc()))
            return -EINTR;
    }

    int i;
    for (i = 0; i < n; i++) {
        if (p->nread == p->nwrite)
            break;
        buf[i] = p->data[p->nread % PIPESIZE];
        p->nread++;
    }

    proc_wakeup_chan(p);
    return i;
}

int pipe_write(struct pipe *p, const char *buf, int n) {
    int i;
    for (i = 0; i < n; i++) {
        while (p->nwrite == p->nread + PIPESIZE) {
            if (!p->readopen) {
                send_signal(current_proc(), SIGPIPE);
                return -EPIPE;
            }
            proc_wakeup_chan(p);
            proc_sleep_chan(p);
            if (sig_has_actionable(current_proc()))
                return i > 0 ? i : -EINTR;
        }
        if (!p->readopen) {
            send_signal(current_proc(), SIGPIPE);
            return -EPIPE;
        }
        p->data[p->nwrite % PIPESIZE] = buf[i];
        p->nwrite++;
    }

    proc_wakeup_chan(p);
    return i;
}

void pipe_close(struct pipe *p, int writable) {
    if (writable)
        p->writeopen = 0;
    else
        p->readopen = 0;
    proc_wakeup_chan(p);
}
