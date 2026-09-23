#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum { INPUT_CAP = 4096, ARG_CAP = 256, BG_CAP = 128 };

typedef struct {
    char *argv[ARG_CAP + 1];
    size_t argc;
    char *input_path;
    char *output_path;
    bool background;
} command_t;

typedef struct {
    bool signaled;
    int value;
} status_t;

static volatile sig_atomic_t foreground_only = 0;
static pid_t background_pids[BG_CAP];
static size_t background_count = 0;

static void on_sigtstp(int signo) {
    (void)signo;
    foreground_only = !foreground_only;

    const char enabled[] = "\nforeground-only mode enabled\n";
    const char disabled[] = "\nforeground-only mode disabled\n";
    const char *message = foreground_only ? enabled : disabled;
    size_t length = foreground_only ? sizeof(enabled) - 1 : sizeof(disabled) - 1;

    (void)write(STDOUT_FILENO, message, length);
}

static void install_shell_signals(void) {
    struct sigaction interrupt_action = {0};
    interrupt_action.sa_handler = SIG_IGN;
    sigemptyset(&interrupt_action.sa_mask);
    sigaction(SIGINT, &interrupt_action, NULL);

    struct sigaction stop_action = {0};
    stop_action.sa_handler = on_sigtstp;
    sigemptyset(&stop_action.sa_mask);
    stop_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &stop_action, NULL);
}

static void command_init(command_t *command) {
    memset(command, 0, sizeof(*command));
}

static void command_free(command_t *command) {
    for (size_t i = 0; i < command->argc; ++i) {
        free(command->argv[i]);
    }
    free(command->input_path);
    free(command->output_path);
    command_init(command);
}

static char *expand_pid_token(const char *token) {
    char pid_text[32];
    snprintf(pid_text, sizeof(pid_text), "%ld", (long)getpid());

    size_t pid_len = strlen(pid_text);
    size_t needed = 1;

    for (size_t i = 0; token[i] != '\0'; ++i) {
        if (token[i] == '$' && token[i + 1] == '$') {
            needed += pid_len;
            ++i;
        } else {
            ++needed;
        }
    }

    char *expanded = malloc(needed);
    if (!expanded) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; token[i] != '\0'; ++i) {
        if (token[i] == '$' && token[i + 1] == '$') {
            memcpy(expanded + out, pid_text, pid_len);
            out += pid_len;
            ++i;
        } else {
            expanded[out++] = token[i];
        }
    }
    expanded[out] = '\0';
    return expanded;
}

static bool assign_path(char **destination, const char *token) {
    char *expanded = expand_pid_token(token);
    if (!expanded) {
        return false;
    }

    free(*destination);
    *destination = expanded;
    return true;
}

static bool parse_line(char *line, command_t *command) {
    char *save = NULL;
    char *token = strtok_r(line, " \t\r\n", &save);

    if (!token || token[0] == '#') {
        return true;
    }

    while (token) {
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            bool input = token[0] == '<';
            token = strtok_r(NULL, " \t\r\n", &save);
            if (!token) {
                fprintf(stderr, "minish: redirection requires a file path\n");
                return false;
            }

            if (!assign_path(input ? &command->input_path : &command->output_path, token)) {
                perror("malloc");
                return false;
            }
        } else {
            char *expanded = expand_pid_token(token);
            if (!expanded) {
                perror("malloc");
                return false;
            }

            if (command->argc >= ARG_CAP) {
                free(expanded);
                fprintf(stderr, "minish: too many arguments\n");
                return false;
            }

            command->argv[command->argc++] = expanded;
        }

        token = strtok_r(NULL, " \t\r\n", &save);
    }

    if (command->argc > 0 && strcmp(command->argv[command->argc - 1], "&") == 0) {
        free(command->argv[command->argc - 1]);
        command->argv[--command->argc] = NULL;
        command->background = true;
    }

    command->argv[command->argc] = NULL;
    return true;
}

static void background_add(pid_t pid) {
    if (background_count < BG_CAP) {
        background_pids[background_count++] = pid;
    }
}

static void background_remove(pid_t pid) {
    for (size_t i = 0; i < background_count; ++i) {
        if (background_pids[i] == pid) {
            background_pids[i] = background_pids[background_count - 1];
            --background_count;
            return;
        }
    }
}

static void reap_background(void) {
    int child_status = 0;
    pid_t pid = 0;

    while ((pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
        background_remove(pid);

        if (WIFEXITED(child_status)) {
            printf("background pid %ld completed: exit %d\n",
                   (long)pid, WEXITSTATUS(child_status));
        } else if (WIFSIGNALED(child_status)) {
            printf("background pid %ld completed: signal %d\n",
                   (long)pid, WTERMSIG(child_status));
        }
        fflush(stdout);
    }
}

static void terminate_background(void) {
    for (size_t i = 0; i < background_count; ++i) {
        kill(background_pids[i], SIGTERM);
    }

    while (background_count > 0) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            background_remove(pid);
        } else if (errno != EINTR) {
            break;
        }
    }
}

static bool redirect_fd(const char *path, int target_fd, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd == -1) {
        fprintf(stderr, "minish: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }

    if (dup2(fd, target_fd) == -1) {
        fprintf(stderr, "minish: dup2 failed for %s: %s\n", path, strerror(errno));
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

static void configure_child_signals(bool background) {
    struct sigaction interrupt_action = {0};
    interrupt_action.sa_handler = background ? SIG_IGN : SIG_DFL;
    sigemptyset(&interrupt_action.sa_mask);
    sigaction(SIGINT, &interrupt_action, NULL);

    struct sigaction stop_action = {0};
    stop_action.sa_handler = SIG_IGN;
    sigemptyset(&stop_action.sa_mask);
    sigaction(SIGTSTP, &stop_action, NULL);
}

static void execute_external(command_t *command, status_t *last_status) {
    bool run_background = command->background && !foreground_only;

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return;
    }

    if (child == 0) {
        configure_child_signals(run_background);

        if (command->input_path &&
            !redirect_fd(command->input_path, STDIN_FILENO, O_RDONLY, 0)) {
            _exit(1);
        }

        if (command->output_path &&
            !redirect_fd(command->output_path, STDOUT_FILENO,
                         O_WRONLY | O_CREAT | O_TRUNC, 0644)) {
            _exit(1);
        }

        if (run_background && !command->input_path) {
            if (!redirect_fd("/dev/null", STDIN_FILENO, O_RDONLY, 0)) {
                _exit(1);
            }
        }

        if (run_background && !command->output_path) {
            if (!redirect_fd("/dev/null", STDOUT_FILENO, O_WRONLY, 0)) {
                _exit(1);
            }
        }

        execvp(command->argv[0], command->argv);
        fprintf(stderr, "minish: %s: %s\n", command->argv[0], strerror(errno));
        _exit(127);
    }

    if (run_background) {
        background_add(child);
        printf("background pid %ld\n", (long)child);
        fflush(stdout);
        return;
    }

    int child_status = 0;
    while (waitpid(child, &child_status, 0) == -1 && errno == EINTR) {
    }

    if (WIFEXITED(child_status)) {
        last_status->signaled = false;
        last_status->value = WEXITSTATUS(child_status);
    } else if (WIFSIGNALED(child_status)) {
        last_status->signaled = true;
        last_status->value = WTERMSIG(child_status);
        printf("terminated by signal %d\n", last_status->value);
        fflush(stdout);
    }
}

static bool run_builtin(command_t *command, status_t *last_status, bool *should_exit) {
    if (command->argc == 0) {
        return true;
    }

    const char *name = command->argv[0];

    if (strcmp(name, "exit") == 0) {
        *should_exit = true;
        return true;
    }

    if (strcmp(name, "cd") == 0) {
        const char *path = command->argc > 1 ? command->argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "minish: HOME is not set\n");
        } else if (chdir(path) == -1) {
            fprintf(stderr, "minish: cd: %s\n", strerror(errno));
        }
        return true;
    }

    if (strcmp(name, "status") == 0) {
        if (last_status->signaled) {
            printf("terminated by signal %d\n", last_status->value);
        } else {
            printf("exit value %d\n", last_status->value);
        }
        fflush(stdout);
        return true;
    }

    return false;
}

int main(void) {
    install_shell_signals();

    status_t last_status = { .signaled = false, .value = 0 };
    char input[INPUT_CAP];

    for (;;) {
        reap_background();

        fputs("minish$ ", stdout);
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            clearerr(stdin);
            continue;
        }

        command_t command;
        command_init(&command);

        if (!parse_line(input, &command)) {
            command_free(&command);
            continue;
        }

        bool should_exit = false;

        if (!run_builtin(&command, &last_status, &should_exit) && command.argc > 0) {
            execute_external(&command, &last_status);
        }

        command_free(&command);

        if (should_exit) {
            break;
        }
    }

    terminate_background();
    return EXIT_SUCCESS;
}
