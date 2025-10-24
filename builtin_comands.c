/* Built in comands ex echo, export, cd, pwd, quit, exit, jobs, kill */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h> 
#include <unistd.h>
#include <errno.h>
#include <ctype.h>


int quash_cd(char **args) {
    char *path = args[1];
    char cwd[4096]; 
    // If no argument, go to HOME
    if (path == NULL || strcmp(path, "~") == 0) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, "quash: cd: HOME not set\n");
            return -1;
        }
    }
    
    //Change directory
    if (chdir(path) != 0) {
        perror("quash: cd");
        return -1;
    }

    // Update PWD environment variable
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        if (setenv("PWD", cwd, 1) != 0) {
             perror("quash: setenv PWD");
             return -1;
        }
    } else {
        perror("quash: getcwd after chdir");
        return -1;
    }
    return 0;
}

// echo
int quash_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        char *to_print = args[i];
        
        // Check for $VAR
        if (to_print[0] == '$') {
            char *var_name_start = to_print + 1;
            char *var_name_end = var_name_start;
            
            while (*var_name_end != '\0' && (isalnum(*var_name_end) || *var_name_end == '_')) {
                var_name_end++;
            }
            
            char original_char = *var_name_end;
            *var_name_end = '\0';
            
            char *value = getenv(var_name_start);
            
            if (value != NULL) {
                printf("%s", value);
            }
            
            *var_name_end = original_char;
            printf("%s", var_name_end);
            
        } else {
            char start_char = to_print[0];
            size_t len = strlen(to_print);

            if (len > 1 && (start_char == '\'' || start_char == '\"') && to_print[len - 1] == start_char) {
                printf("%.*s", (int)(len - 2), to_print + 1);  // +1 skips the opening quote, len - 2 is the new length
            } else {
                printf("%s", to_print);
            }
        }
        if (args[i+1] != NULL) {
            printf(" ");
        }
    }
    printf("\n");
    return 0;
}

// pwd
int quash_pwd(char **args) {
    char cwd[4096]; 

    // retrieve the path into the buffer
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("quash: pwd error retrieving path");
        return -1;
    }
    return 0;
}

// export
int quash_export(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "quash: expected argument NAME=VALUE to \"export\"\n");
        return -1;
    }
    
    char *arg_copy = strdup(args[1]);
    if (!arg_copy) { perror("strdup"); return -1; }

    char *var = strtok(arg_copy, "=");
    char *value = strtok(NULL, "=");
    
    if (var == NULL) {
        free(arg_copy);
        fprintf(stderr, "quash: export: invalid format\n");
        return -1;
    }
    
    // Handle export VAR=$HOME
    char *final_value;
    if (value != NULL && value[0] == '$') {
        char *env_val = getenv(value + 1);
        final_value = (env_val != NULL) ? env_val : "";
    } else {
        final_value = (value != NULL) ? value : "";
    }

    if (setenv(var, final_value, 1) == -1) {
        perror("quash: setenv");
        free(arg_copy);
        return -1;
    }

    free(arg_copy);
    return 0;
}



// Job struct 
// typedef struct Job {
//     int job_id;
//     pid_t pgid; 
//     char *command_line;
//     Process *processes;
//     int num_processes;
//     bool is_background;
// } Job;

// jobs
int quash_jobs(char **args) {
    for (int i = 0; i < max_job_id; i++) { 
        // Check if the slot is taken by an active job 
        if (job_list[i] != NULL) {
            printf("[%d] %d %s\n", job_list[i]->job_id, (int)job_list[i]->pgid, job_list[i]->command_line);
        }
    }
    return 0;
}

// kill
int quash_kill(char **args) {
    if (args[1] == NULL || args[2] == NULL) {
        fprintf(stderr, "Usage: kill SIGNUM PID\n");
        return -1;
    }
    
    int signum = atoi(args[1]);
    pid_t pid = (pid_t)atoi(args[2]);

    if (signum <= 0 || pid <= 0) {
        fprintf(stderr, "quash: kill: invalid signal number or PID\n");
        return -1;
    }

    // Send the user-specified signal to the specified PID
    if (kill(pid, signum) == -1) {
        perror("quash: kill");
        return -1;
    }

    printf("Signal %d sent to PID %d\n", signum, (int)pid);
    return 0;
}