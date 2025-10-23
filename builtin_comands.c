/* Built in comands ex echo, export, cd, pwd, quit, exit, jobs, kill */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h> // for get_current_dir_name
#include <unistd.h>
#include <errno.h>
#include <ctype.h>


int quash_cd(char **args) {
    char *path = args[1];
    char cwd[4096]; // Use a larger buffer for safety

    // If no argument, go to HOME
    if (path == NULL || strcmp(path, "~") == 0) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, "quash: cd: HOME not set\n");
            return -1;
        }
    }
    
    // Attempt to change directory
    if (chdir(path) != 0) {
        perror("quash: cd");
        return -1;
    }

    // Update PWD environment variable (critical step)
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

int quash_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        char *to_print = args[i];
        
        // Check for variable expansion: $VAR
        if (to_print[0] == '$') {
            // This is the variable expansion logic you implemented in the last step
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
            // --- Quoting Fix ---
            // If the argument starts and ends with a single or double quote, strip them.
            char start_char = to_print[0];
            size_t len = strlen(to_print);

            if (len > 1 && (start_char == '\'' || start_char == '\"') && to_print[len - 1] == start_char) {
                // If quoted, print the substring between the quotes
                // +1 skips the opening quote, len - 2 is the new length
                printf("%.*s", (int)(len - 2), to_print + 1);
            } else {
                // Not quoted or improperly quoted, print as is
                printf("%s", to_print);
            }
        }

        // Print space if it's not the last argument
        if (args[i+1] != NULL) {
            printf(" ");
        }
    }
    printf("\n");
    return 0;
}

int quash_pwd(char **args) {
    // 1. Use a fixed size buffer for the current working directory
    //    (Path length usually limited to 4096 bytes on modern systems)
    char cwd[4096]; 
    
    // 2. Use getcwd to retrieve the path into the buffer
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        // NOTE: No free(cwd) needed here because 'cwd' is a stack variable.
    } else {
        perror("quash: pwd error retrieving path");
        return -1;
    }
    return 0;
}

int quash_export(char **args) {
    if (args[1] == NULL) {
        // Just 'export' - usually lists all environment variables, 
        // but for this project, checking for error is fine.
        fprintf(stderr, "quash: expected argument NAME=VALUE to \"export\"\n");
        return -1;
    }
    
    // IMPORTANT: Operating on a copy, as strtok modifies the string.
    char *arg_copy = strdup(args[1]);
    if (!arg_copy) { perror("strdup"); return -1; }

    char *var = strtok(arg_copy, "=");
    char *value = strtok(NULL, "=");
    
    if (var == NULL) {
        free(arg_copy);
        fprintf(stderr, "quash: export: invalid format\n");
        return -1;
    }
    
    // Handle variable expansion in the value (e.g., export VAR=$HOME)
    char *final_value;
    if (value != NULL && value[0] == '$') {
        char *env_val = getenv(value + 1);
        final_value = (env_val != NULL) ? env_val : "";
    } else {
        final_value = (value != NULL) ? value : "";
    }

    // Set the environment variable. The '1' ensures it overwrites existing variable.
    if (setenv(var, final_value, 1) == -1) {
        perror("quash: setenv");
        free(arg_copy);
        return -1;
    }

    free(arg_copy);
    return 0;
}



// Assume the Job struct now looks like:
// typedef struct Job {
//     int job_id;
//     pid_t pgid; // Use this as the primary PID to report
//     char *command_line;
//     // ...
// } Job;

// quash_jobs
int quash_jobs(char **args) {
    // This loop logic is highly dependent on how job_list is implemented (array vs. list).
    // Assuming 'max_job_id' is the current highest ID and the array/list is indexed.
    for (int i = 0; i < max_job_id; i++) { 
        // Check if the slot is occupied by an active job (pointer is not NULL)
        if (job_list[i] != NULL) {
            // REQUIRED FORMAT: "[JOBID] PID COMMAND"
            printf("[%d] %d %s\n", job_list[i]->job_id, (int)job_list[i]->pgid, job_list[i]->command_line);
        }
    }
    return 0;
}

// Corrected `quash_kill`
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
        // If errno is ESRCH (No such process), handle it specifically
        perror("quash: kill");
        return -1;
    }

    printf("Signal %d sent to PID %d\n", signum, (int)pid);

    // NOTE: If you decide to support killing by Job ID (as your original attempt did),
    // you must clearly define the required syntax: kill SIGNUM PID or kill SIGNUM [JOBID].
    return 0;
}