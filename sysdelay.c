#include "sysdelay.h"

void *xmalloc(size_t s) {
    void *data = malloc(s);
    if (data == NULL) {
        err(1, "failed to malloc");
    }
    return data;
}

void * handle_ptrace_loop(void *arg) {
    thread *t = (thread *)arg;
    ptrace_loop(t);
    return NULL;
}

void create_trace_thread(pid_t tid) {
    thread *t     = xmalloc(sizeof(thread));
    t->tid        = tid;
    t->in_syscall = false;
    t->pthread    = xmalloc(sizeof(pthread_t));
    pthread_create(t->pthread, NULL, handle_ptrace_loop, t);
}

void attach(pid_t pid) {
    int rc = ptrace(PTRACE_ATTACH, pid, 0, 0);
    if (rc == -1) {
        err(1, "failed to ptrace(PTRACE_ATTACH, %d, ..)", pid);
    }
}

void detach(pid_t pid) {
    int rc = ptrace(PTRACE_DETACH, pid, 0, 0);
    if (rc == -1) {
        err(1, "failed to ptrace(PTRACE_DETACH, %d, ..)", pid);
    }
}

void attach_all_threads(pid_t pid) {
    char procdir[sizeof("/proc/%d/task") + sizeof(int) * 3];
    DIR *dir;

    sprintf(procdir, "/proc/%d/task", pid);
    dir = opendir(procdir);
    if (dir != NULL) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            int tid;

            if (de->d_fileno == 0)
                continue;

            tid = atoi(de->d_name);
            if (tid <= 0)
                continue;
            create_trace_thread(tid);
        }
        closedir(dir);
    }
}

bool ignored_syscall(unsigned int syscall) {
    switch(syscall) {
    case SYS_futex:
    case SYS_nanosleep:
    case SYS_gettimeofday:
    case SYS_clock_gettime:
    case SYS_pselect6:
    case SYS_epoll_wait:
    case SYS_sched_yield:
    case SYS_restart_syscall:
    case SYS_rt_sigaction:
    case SYS_rt_sigprocmask:
        return true;
    default:
        return false;
    }
}

void init_signal_handler() {
    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_handler = signal_handler;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        err(1, "failed to sigaction(SIGINT, ...)");
    }

    if (sigaction(SIGTERM, &act, NULL) == -1) {
        err(1, "failed to sigaction(SIGTERM, ...");
    }
}

void
ptrace_loop(thread *t)
{
    attach(t->tid);

    while(!got_signal()){
        int status;
        waitpid(t->tid, &status, __WALL);
        if (WIFEXITED(status)) {
            warn("tid WIFEXITED: %d", t->tid);
            break;
        }

        errno = 0;
        long orig_rax = ptrace(PTRACE_PEEKUSER, t->tid, 8 * ORIG_RAX, NULL);
        if (orig_rax == -1 && errno) {
            warn("failed to ptrace(PTRACE_PEEKUSER,...), tid = %d", t->tid);
            continue;
        }

        if (ignored_syscall(orig_rax)) {
            fprintf(stderr, "%d [%d] %s(2)\n", gettid(), t->tid, syscall_name(orig_rax));
        }
        else {
            if (t->in_syscall == true) {
                int delay = get_delay_time();
                t->in_syscall = false;
                fprintf(stderr, "%d [%d] %s(2), usleep = %d\n", gettid(), t->tid, syscall_name(orig_rax), delay);
                usleep(delay);
            }
            else {
                t->in_syscall = true;
            }
        }
        ptrace(PTRACE_SYSCALL, t->tid, NULL, NULL);
    }
    detach(t->tid);
}

int main (int argc, char **argv)
{
    pid_t pid;
    int delay;
    int long_opt, option_index;

    struct option long_options[] = {
        {"pid",     required_argument, &long_opt, 'p' },
        {"delay",   required_argument, &long_opt, 'd' },
        {"verbose", no_argument,       &long_opt,  0  },
        {0,         0,                 0,  0 }
    };

    while (1) {
        int c = getopt_long(argc, argv, "p:d:v", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 0:
            switch (long_opt) {
            case 'p':
                pid = atoi(optarg);
                break;
            case 'd':
                delay = atoi(optarg);
                break;
            }
            break;
        case 'p':
            pid = atoi(optarg);
            break;
 
        case 'd':
            delay = atoi(optarg);
            break;
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
            break;
        }
    }

    if (pid == 0) {
        errx(1, "--pid should be > 0");
    }

    if (delay <= 0) {
        errx(1, "--delay should be > 0");
    }

    set_delay_time(delay);
    init_signal_handler();
    attach_all_threads(pid);
    /* oh */
    pause();
    exit(0);
}
