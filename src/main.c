#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#define MAX_ARGS 64

int parse_input(char *input, char *argv[]);
int execute_builtin(char *argv[], int argc);
void execute_external(char *argv[]);


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
        
        execute_external(argv);
    }
    
    return 0;
}

// Parse input into argc/argv
int parse_input(char *input, char *argv[])
{
    // Current strtok logic
    char *token = strtok(input, " ");
    int argc = 0;
    while (token != NULL && argc < MAX_ARGS - 1)
    {
        argv[argc++] = token;
        token = strtok(NULL, " ");
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
void execute_external(char *argv[])
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return;
    }
    
    if (pid == 0)  // child
    {
        if (execvp(argv[0], argv) < 0)
        {
            perror("execvp");
            exit(1);
        }
    }
    else  // parent
    {
        waitpid(pid, NULL, 0);
    }
}
