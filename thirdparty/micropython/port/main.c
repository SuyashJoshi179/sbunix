// SBUnix MicroPython entry: argv[1] is a script path; run it; exit.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mperrno.h"
#include "py/mphal.h"

static char *stack_top;
static char heap[MICROPY_GC_HEAP_SIZE];

static int run_file(const char *path) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(path));
        qstr source_name = lex->source_name;
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t mod = mp_compile(&pt, source_name, false);
        mp_call_function_0(mod);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return 1;
    }
}

int main(int argc, char **argv) {
    int stack_dummy;
    stack_top = (char *)&stack_dummy;
    mp_stack_ctrl_init();
    mp_stack_set_limit(64 * 1024);

    gc_init(heap, heap + sizeof(heap));
    mp_init();

    int rc = 0;
    if (argc < 2) {
        const char msg[] = "usage: micropython <script.py>\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        rc = 2;
    } else {
        rc = run_file(argv[1]);
    }

    mp_deinit();
    return rc;
}

void gc_collect(void) {
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

void nlr_jump_fail(void *val) {
    (void)val;
    const char msg[] = "FATAL: nlr_jump_fail\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(1);
}

void NORETURN __fatal_error(const char *msg) {
    write(STDERR_FILENO, "FATAL: ", 7);
    if (msg) {
        write(STDERR_FILENO, msg, strlen(msg));
    }
    write(STDERR_FILENO, "\n", 1);
    _exit(1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file; (void)line; (void)func;
    write(STDERR_FILENO, "assertion failed: ", 18);
    write(STDERR_FILENO, expr, strlen(expr));
    write(STDERR_FILENO, "\n", 1);
    __fatal_error("assertion");
}
#endif
