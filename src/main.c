/* Entry point: run a file, a -c string, standard input, or the interactive prompt. */
#include "py.h"
#include "ast.h"
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define STACK_BYTES ((size_t)1 << 30)

static int g_argc;
static char **g_argv;
static int exit_code;

static void on_sigint(int sig) { (void)sig; vm.interrupted = 1; }

static char *slurp(FILE *f) {
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap);
    for (;;) {
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0) break;
        if (n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    buf[n] = 0;
    return buf;
}

/* Handles the pending exception. Returns the process exit code. */
static int report_exception(void) {
    Obj *exc = vm.exc;
    vm.exc = NULL;
    vm.handled = NULL;
    if (isinstance(exc, E_SystemExit)) {
        Exc *e = (Exc *)exc;
        Obj *code = e->args->n ? e->args->items[0] : Py_None;
        int64_t c = 0;
        if (code == Py_None) return 0;
        if (IS_INT(code)) { int_fits(code, &c); return (int)(c & 0xFF); }
        Obj *s = obj_str(code);
        fflush(stdout);
        if (s) fprintf(stderr, "%s\n", AS_STR(s)->s);
        vm.exc = NULL;
        return 1;
    }
    print_exception(exc);
    return 1;
}

static Dict *main_globals(const char *file) {
    Obj *m = module_new("__main__");
    Dict *d = ((Module *)m)->dict;
    dict_sets(vm.modules, N_main, m);
    if (file) dict_sets(d, N_file, str_c(file));
    dict_sets(d, N_builtins, import_module(AS_STR(str_c("builtins"))));
    return d;
}

static int run_source(const char *src, const char *filename, Dict *globals, int mode) {
    register_source(filename, src);
    Code *co = compile_source(src, filename, mode);
    if (!co || !run_code(co, globals, NULL)) return report_exception();
    return 0;
}

static int repl(void) {
    Dict *g = main_globals(NULL);
    bool tty = isatty(0);
    SB buf;
    sb_init(&buf);
    char line[8192];
    for (;;) {
        bool cont = buf.n > 0;
        if (tty) { fputs(cont ? "... " : ">>> ", stdout); fflush(stdout); }
        if (!fgets(line, sizeof line, stdin)) {
            if (vm.interrupted) { vm.interrupted = 0; clearerr(stdin); buf.n = 0; fputs("\nKeyboardInterrupt\n", stdout); continue; }
            if (tty) fputc('\n', stdout);
            if (!buf.n) break;
            line[0] = '\n'; line[1] = 0;
        }
        bool blank = true;
        for (char *p = line; *p; p++) if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') blank = false;
        if (!cont && blank) continue;
        sb_puts(&buf, line);
        sb_putc(&buf, 0);
        buf.n--;
        /* A block (first line ends in ':' or is a decorator) ends at a blank line.
           Anything else runs as soon as it is complete. */
        size_t first_len = strcspn(buf.s, "\n"), e = first_len;
        while (e > 0 && (buf.s[e - 1] == ' ' || buf.s[e - 1] == '\t' || buf.s[e - 1] == '\r')) e--;
        bool block = (e > 0 && buf.s[e - 1] == ':') || buf.s[0] == '@';
        if (block && !blank) continue;
        register_source("<stdin>", buf.s);
        Code *co = compile_source(buf.s, "<stdin>", MODE_SINGLE);
        if (!co && compile_incomplete && !blank) { vm.exc = NULL; continue; }
        buf.n = 0;
        if (!co || !run_code(co, g, NULL)) {
            if (vm.exc && isinstance(vm.exc, E_SystemExit)) { sb_free(&buf); return report_exception(); }
            report_exception();
        }
        fflush(stdout);
    }
    sb_free(&buf);
    return 0;
}

static void *interp_main(void *arg) {
    (void)arg;
    gc_stack_bottom = __builtin_frame_address(0);
    int argc = g_argc;
    char **argv = g_argv;
    gc_init();
    gc_paused = 1;
    objects_init();
    ops_init();
    vm.recursion_limit = 1000;
    vm.max_recursion = (int)(STACK_BYTES / 4096);
    builtins_init();
    methods_init();
    const char *file = NULL, *cmd = NULL;
    int first = 1;
    bool only_check = false;
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: pagda [file.py | -c code | -] [args...]\n"); exit_code = 2; return NULL; }
        cmd = argv[2];
        first = 2;
    } else if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("usage: pagda [file.py | -c code | -] [args...]\n"
               "       pagda --check file.py   type-check a checked file without running it\n"
               "       pagda --agda file.py    print the Agda text a checked file is translated to\n"
               "With no arguments, starts an interactive prompt.\n");
        return NULL;
    } else if (argc > 2 && (strcmp(argv[1], "--agda") == 0 || strcmp(argv[1], "--check") == 0)) {
        /* --agda prints the Agda text a checked file turns into; --check type-checks it without running it. */
        only_check = true;
        if (strcmp(argv[1], "--agda") == 0) agda_mode = 1;
        file = argv[2];
        first = 2;
    } else if (argc > 1) file = argv[1];
    List *av = (List *)list_new(0);
    if (cmd) list_append(av, str_c("-c"));
    else if (!file) list_append(av, str_c(""));
    for (int i = first + (cmd ? 1 : 0); i < argc; i++) list_append(av, str_c(argv[i]));
    vm.argv = (Obj *)av;
    char *dir = NULL;
    if (file && strcmp(file, "-") != 0) {
        dir = strdup(file);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0; else { free(dir); dir = strdup(""); }
    }
    vm.script_dir = dir ? dir : "";
    if (!import_module(AS_STR(str_c("sys")))) { exit_code = report_exception(); return NULL; }
    gc_paused = 0;

    if (cmd) exit_code = run_source(cmd, "<string>", main_globals(NULL), MODE_FILE);
    else if (file && strcmp(file, "-") != 0) {
        FILE *f = fopen(file, "rb");
        if (!f) {
            fprintf(stderr, "pagda: can't open file '%s': %s\n", file, strerror(errno));
            exit_code = 2;
            return NULL;
        }
        char *src = slurp(f);
        fclose(f);
        if (only_check) {
            register_source(file, src);
            if (!compile_source(src, file, MODE_FILE)) exit_code = report_exception();
            else if (!agda_checked) {
                fprintf(stderr, "pagda: '%s' does not import dependent, so it is not a checked file\n", file);
                exit_code = 1;
            } else printf("%s: no type errors\n", file);
        } else exit_code = run_source(src, file, main_globals(file), MODE_FILE);
        free(src);
    } else if (file || !isatty(0)) {
        char *src = slurp(stdin);
        exit_code = run_source(src, "<stdin>", main_globals(NULL), MODE_FILE);
        free(src);
    } else exit_code = repl();
    /* Objects still alive at exit get their finalizers run, as in CPython. */
    vm.frame = NULL;
    vm.exc = NULL;
    vm.depth = 0;
    gc_find_all_finalizable();
    run_finalizers();
    fflush(stdout);
    return NULL;
}

int main(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    if (!setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "");
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    /* The interpreter recurses in C for nested Python calls, so give it a large stack. */
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_BYTES);
    if (pthread_create(&th, &attr, interp_main, NULL) != 0) {
        gc_stack_bottom = __builtin_frame_address(0);
        interp_main(NULL);
    } else pthread_join(th, NULL);
    fflush(stdout);
    return exit_code;
}
