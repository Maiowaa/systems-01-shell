#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
int main()
{
    char input[1024];
    

    while (1)
    {
        printf("myshell> ");

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        char *token = strtok(input, " ");
        char *argv[64];
        int argc = 0;
        while (token != NULL && argc < (int)(sizeof(argv) / sizeof(argv[0]) - 1))
        {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }
        argv[argc] = NULL;
        if (argc == 0)
        {
            continue;
        }
        
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            continue;
        }
        if (pid == 0)
        {
            if (execvp(argv[0], argv) < 0)
            {
                perror("execvp");
                return 1;
            }
            
        }
        else
        {
            waitpid(pid, NULL, 0);
            
        }
        if (strcmp(argv[0], "exit") == 0)
        {
            break;
        }
        
    }

    return 0;
}
