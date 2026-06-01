#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#define MAX_ARGS 64

int parse_input(char *input, char *argv[]);
int execute_builtin(char *argv[], int argc);
void execute_external(char *argv[], int argc);


int main()
{
    char input[1024];
    char *argv[64];
    int argc;
    
    while (1)
    {
        printf("myshell> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        
        input[strcspn(input, "\n")] = '\0';
        
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
            exit(0);  // or return special code to break from main loop
        return 1;  // was a builtin
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
        return 1;  // was a builtin
    }
    
    return 0;  // NOT a builtin, needs fork/exec
}

// Execute external command via fork/exec
void execute_external(char *argv[], int argc)
{
    char *input_file = NULL;
    char *output_file = NULL;
    int new_argc = argc;

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
        waitpid(pid, NULL, 0);
    }
}
