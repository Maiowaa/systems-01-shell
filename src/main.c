#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_JOBS 64

int parse_input(char *input, char *argv[]);
int execute_builtin(char *argv[], int argc);
void execute_external(char *argv[], int argc);
int split_pipeline(char *input, char *cmds[]);
void execute_pipeline(char *cmds[], int ncmds);
void exec_command(char *argv[]);
char *trim_whitespace(char *str);


/* job table for background processes */
struct job {
    int id;
    pid_t pid;
    pid_t pgid;
    char cmd[256];
    int running;
};

static struct job jobs[MAX_JOBS];
static int next_job_id = 1;
static pid_t shell_pgid;
static int shell_terminal;
static int shell_is_interactive;
static struct termios shell_tmodes;

int add_job(pid_t pid, pid_t pgid, char *argv[])
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (!jobs[i].running)
        {
            jobs[i].running = 1;
            jobs[i].pid = pid;
            jobs[i].pgid = pgid;
            jobs[i].id = next_job_id++;
            jobs[i].cmd[0] = '\0';
            int pos = 0;
            for (int j = 0; argv[j] != NULL && pos + 1 < (int)sizeof(jobs[i].cmd); j++)
            {
                int r = snprintf(jobs[i].cmd + pos, sizeof(jobs[i].cmd) - pos, "%s%s",
                                 (pos==0)?"":" ", argv[j]);
                if (r < 0) break;
                pos += r;
            }
            return jobs[i].id;
        }
    }
    return -1;
}

int remove_job_by_pid(pid_t pid)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs[i].running && jobs[i].pid == pid)
        {
            jobs[i].running = 0;
            return jobs[i].id;
        }
    }
    return -1;
}

void sigchld_handler(int sig)
{
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int jid = remove_job_by_pid(pid);
        if (jid > 0)
        {
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "\n[%d] Done (pid %d)\n", jid, pid);
            if (len > 0)
                write(STDOUT_FILENO, buf, len);
        }
    }
}

void init_shell(void)
{
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpid();
    shell_is_interactive = isatty(shell_terminal);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    if (setpgid(shell_pgid, shell_pgid) < 0)
        perror("setpgid");

    if (shell_is_interactive)
    {
        if (tcsetpgrp(shell_terminal, shell_pgid) < 0)
            perror("tcsetpgrp");

        tcgetattr(shell_terminal, &shell_tmodes);
    }
}

int main()
{
    char input[1024];
    char *argv[64];
    char *cmds[MAX_CMDS];
    int argc;
    int ncmds;
    init_shell();

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    while (1)
    {
        printf("myshell> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        
        input[strcspn(input, "\n")] = '\0';
        
        ncmds = split_pipeline(input, cmds);
        if (ncmds == 0)
            continue;
        if (ncmds > 1)
        {
            execute_pipeline(cmds, ncmds);
            continue;
        }
        
        argc = parse_input(input, argv);
        if (argc == 0)
            continue;
        
        if (execute_builtin(argv, argc) == 1)
            continue;
        
        execute_external(argv, argc);
    }
    
    return 0;
}

// Parse input into argc/argv
int parse_input(char *input, char *argv[])
{
    // Current strtok logic
    char *token = strtok(input, " \t");
    int argc = 0;
    
    while (token != NULL && argc < MAX_ARGS - 1)
    {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    argv[argc] = NULL;
    return argc;  // return argc so caller knows how many args
}

char *trim_whitespace(char *str)
{
    char *end;
    while (*str == ' ' || *str == '\t')
        str++;
    if (*str == '\0')
        return str;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t'))
        end--;
    end[1] = '\0';
    return str;
}

int split_pipeline(char *input, char *cmds[])
{
    int count = 0;
    char *token = strtok(input, "|");

    while (token != NULL && count < MAX_CMDS)
    {
        cmds[count++] = trim_whitespace(token);
        token = strtok(NULL, "|");
    }
    return count;
}

void exec_command(char *argv[])
{
    char *input_file = NULL;
    char *output_file = NULL;
    int new_argc = 0;

    while (argv[new_argc] != NULL)
        new_argc++;

    for (int i = 0; i < new_argc; i++)
    {
        if (strcmp(argv[i], ">") == 0)
        {
            if (output_file != NULL)
            {
                fprintf(stderr, "Multiple output redirections not supported\n");
                exit(1);
            }
            if (i + 1 >= new_argc)
            {
                fprintf(stderr, "Missing output file\n");
                exit(1);
            }
            output_file = argv[i + 1];
            for (int j = i; j + 2 < new_argc; j++)
                argv[j] = argv[j + 2];
            new_argc -= 2;
            argv[new_argc] = NULL;
            i--;
            continue;
        }

        if (strcmp(argv[i], "<") == 0)
        {
            if (input_file != NULL)
            {
                fprintf(stderr, "Multiple input redirections not supported\n");
                exit(1);
            }
            if (i + 1 >= new_argc)
            {
                fprintf(stderr, "Missing input file\n");
                exit(1);
            }
            input_file = argv[i + 1];
            for (int j = i; j + 2 < new_argc; j++)
                argv[j] = argv[j + 2];
            new_argc -= 2;
            argv[new_argc] = NULL;
            i--;
            continue;
        }
    }

    if (input_file != NULL)
    {
        int fd = open(input_file, O_RDONLY);
        if (fd < 0)
        {
            perror("open");
            exit(1);
        }
        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            exit(1);
        }
        close(fd);
    }

    if (output_file != NULL)
    {
        int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            perror("open");
            exit(1);
        }
        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            close(fd);
            exit(1);
        }
        close(fd);
    }

    execvp(argv[0], argv);
    perror("execvp");
    exit(1);
}

void execute_pipeline(char *cmds[], int ncmds)
{
    int pipefd[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    int prev_fd = -1;
    pid_t pipeline_pgid = 0;

    for (int i = 0; i < ncmds; i++)
    {
        char *argv[MAX_ARGS];
        int argc = parse_input(cmds[i], argv);
        if (argc == 0)
        {
            fprintf(stderr, "Invalid null command in pipeline\n");
            return;
        }
        if (strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "exit") == 0)
        {
            fprintf(stderr, "Built-in commands are not supported in pipelines\n");
            return;
        }

        if (i < ncmds - 1)
        {
            if (pipe(pipefd[i]) < 0)
            {
                perror("pipe");
                return;
            }
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            return;
        }

        if (pid == 0)
        {
            if (pipeline_pgid == 0)
                setpgid(0, 0);
            else
                setpgid(0, pipeline_pgid);
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            if (prev_fd != -1)
            {
                if (dup2(prev_fd, STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    exit(1);
                }
            }
            if (i < ncmds - 1)
            {
                if (dup2(pipefd[i][1], STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    exit(1);
                }
            }

            for (int j = 0; j < ncmds - 1; j++)
            {
                close(pipefd[j][0]);
                close(pipefd[j][1]);
            }
            if (prev_fd != -1)
                close(prev_fd);

            exec_command(argv);
        }
        else
        {
            if (pipeline_pgid == 0)
                pipeline_pgid = pid;
            if (setpgid(pid, pipeline_pgid) < 0)
                perror("setpgid");

            pids[i] = pid;
            if (prev_fd != -1)
                close(prev_fd);
            if (i < ncmds - 1)
            {
                close(pipefd[i][1]);
                prev_fd = pipefd[i][0];
            }
        }
    }

    if (shell_is_interactive)
    {
        if (tcsetpgrp(shell_terminal, pipeline_pgid) < 0)
            perror("tcsetpgrp");
    }

    for (int i = 0; i < ncmds; i++)
        waitpid(pids[i], NULL, 0);

    if (shell_is_interactive)
    {
        if (tcsetpgrp(shell_terminal, shell_pgid) < 0)
            perror("tcsetpgrp");
    }
}

// Execute built-in commands; return 1 if executed, 0 if not
int execute_builtin(char *argv[], int argc)
{
    // Returns 1 if command was a builtin (executed)
    // Returns 0 if command was NOT a builtin (needs fork/exec)

    if (strcmp(argv[0], "exit") == 0)
    {
        if (argc != 1)
            printf("Usage: exit\n");
        else
            exit(0);
        return 1;
    }

    if (strcmp(argv[0], "cd") == 0)
    {
        if (argc == 1)
        {
            char *home = getenv("HOME");
            if (home == NULL || chdir(home) < 0)
                perror("cd");
        }
        else
        {
            if (chdir(argv[1]) < 0)
                perror("cd");
        }
        return 1;
    }

    if (strcmp(argv[0], "jobs") == 0)
    {
        for (int i = 0; i < MAX_JOBS; i++)
        {
            if (jobs[i].running)
            {
                printf("[%d] %d running %s\n", jobs[i].id, jobs[i].pid, jobs[i].cmd);
            }
        }
        return 1;
    }

    if (strcmp(argv[0], "fg") == 0)
    {
        if (argc != 2)
        {
            printf("Usage: fg %%jobid\n");
            return 1;
        }
        int jobid = atoi(argv[1][0] == '%' ? argv[1] + 1 : argv[1]);
        for (int i = 0; i < MAX_JOBS; i++)
        {
            if (jobs[i].running && jobs[i].id == jobid)
            {
                pid_t pgid = jobs[i].pgid;
                pid_t pid = jobs[i].pid;
                jobs[i].running = 0;
                if (kill(-pgid, SIGCONT) < 0)
                    perror("kill");
                if (shell_is_interactive)
                {
                    if (tcsetpgrp(shell_terminal, pgid) < 0)
                        perror("tcsetpgrp");
                }
                waitpid(pid, NULL, 0);
                if (shell_is_interactive)
                {
                    if (tcsetpgrp(shell_terminal, shell_pgid) < 0)
                        perror("tcsetpgrp");
                }
                return 1;
            }
        }
        printf("fg: job not found: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

// Execute external command via fork/exec
void execute_external(char *argv[], int argc)
{
    char *input_file = NULL;
    char *output_file = NULL;
    int new_argc = argc;
    int background = 0;

    if (new_argc > 0 && strcmp(argv[new_argc - 1], "&") == 0)
    {
        background = 1;
        argv[new_argc - 1] = NULL;
        new_argc--;
        if (new_argc == 0)
            return; // nothing to run
    }

    for (int i = 0; i < new_argc; i++)
    {
        if (strcmp(argv[i], ">") == 0)
        {
            if (output_file != NULL)
            {
                fprintf(stderr, "Multiple output redirections not supported\n");
                return;
            }
            if (i + 1 >= new_argc)
            {
                fprintf(stderr, "Missing output file\n");
                return;
            }
            output_file = argv[i + 1];
            for (int j = i; j + 2 < new_argc; j++)
                argv[j] = argv[j + 2];
            new_argc -= 2;
            argv[new_argc] = NULL;
            i--;
            continue;
        }
        if (strcmp(argv[i], "<") == 0)
        {
            if (input_file != NULL)
            {
                fprintf(stderr, "Multiple input redirections not supported\n");
                return;
            }
            if (i + 1 >= new_argc)
            {
                fprintf(stderr, "Missing input file\n");
                return;
            }
            input_file = argv[i + 1];
            for (int j = i; j + 2 < new_argc; j++)
                argv[j] = argv[j + 2];
            new_argc -= 2;
            argv[new_argc] = NULL;
            i--;
            continue;
        }
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return;
    }

    if (pid == 0)
    {
        if (setpgid(0, 0) < 0)
            perror("setpgid");
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        if (input_file != NULL)
        {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0)
            {
                perror("open");
                exit(1);
            }
            if (dup2(fd, STDIN_FILENO) < 0)
            {
                perror("dup2");
                close(fd);
                exit(1);
            }
            close(fd);
        }

        if (output_file != NULL)
        {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                perror("open");
                exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) < 0)
            {
                perror("dup2");
                close(fd);
                exit(1);
            }
            close(fd);
        }

        if (execvp(argv[0], argv) < 0)
        {
            perror("execvp");
            exit(1);
        }
    }
    else
    {
        if (setpgid(pid, pid) < 0)
            perror("setpgid");

        if (background)
        {
            int jid = add_job(pid, pid, argv);
            if (jid > 0)
                printf("[%d] %d\n", jid, pid);
            else
                printf("[?] %d\n", pid);
        }
        else
        {
            if (shell_is_interactive)
        {
            if (tcsetpgrp(shell_terminal, pid) < 0)
                perror("tcsetpgrp");
        }
        waitpid(pid, NULL, 0);
        if (shell_is_interactive)
        {
            if (tcsetpgrp(shell_terminal, shell_pgid) < 0)
                perror("tcsetpgrp");
        }
        }
    }
}
