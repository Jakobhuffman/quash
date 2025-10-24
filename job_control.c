/* Handling using fork, exec */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h> /
#include <string.h>

int max_job_id; 

#define MAX_JOBS 1024 

void initialize_job_control(void) {
    job_list = (Job **)calloc(MAX_JOBS, sizeof(Job *)); //allocate for pointers to Job structs
    if (job_list == NULL) {
        perror("quash: Failed to initialize job list");
        exit(EXIT_FAILURE);
    }

    max_job_id = 0;
}


//find the next available job ID
int get_next_job_id() {
    return ++max_job_id; 
}

int launch_process(Process *p, pid_t pgid, int fdin, int fdout, bool is_background);
int launch_job(Job *job); // initial fork for the pipeline
void wait_for_job(Job *job); // Wait for a foreground job

/**
 * checks for built-in vs. external.
 */
int execute_job(Job *job) {
    char *command = job->processes[0].argv[0];
    char **args = job->processes[0].argv;
    Process *p = &job->processes[0]; 
    
    if (command == NULL) {
        // Handle job cleanup if no command is found
        return 1;
    }
    bool is_simple_builtin = (job->num_processes == 1 &&
                              p->input_file == NULL &&
                              p->output_file == NULL);

    if (is_simple_builtin) { 
        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            free_job(job);
            exit(0);
        }
        else if (strcmp(command, "cd") == 0) {
            quash_cd(args);
            free_job(job);
            return 1;
        }
        else if (strcmp(command, "echo") == 0) {
            quash_echo(args);
            free_job(job);
            return 1;
        }
        else if (strcmp(command, "export") == 0) {
            quash_export(args);
            free_job(job);
            return 1;
        }
        else if (strcmp(command, "pwd") == 0) {
            quash_pwd(args);
            free_job(job);
            return 1;
        }
        else if (strcmp(command, "jobs") == 0) {
            quash_jobs(args);
            free_job(job);
            return 1;
        }
        else if (strcmp(command, "kill") == 0) {
            quash_kill(args);
            free_job(job);
            return 1;
        }
    }
    return launch_job(job);
}

/**
 * Handles the forking and waiting for a single or piped job
 */
int launch_job(Job *job) {

    int pipe_fds[2]; 
    int fdin = STDIN_FILENO; 
    pid_t pid;

    for (int i = 0; i < job->num_processes; i++) {
        Process *p = &job->processes[i];
        
        if (i < job->num_processes - 1) {
            if (pipe(pipe_fds) < 0) {
                perror("quash: pipe error");
                // Handle job failure and cleanup
                return -1;
            }
            // read from pipe_fds[0]
            // write to pipe_fds[1]
        }
        
        int fdout = (i < job->num_processes - 1) ? pipe_fds[1] : STDOUT_FILENO;
        pid = launch_process(p, job->pgid, fdin, fdout, job->is_background);

        if (pid < 0) {
            // Error handling
            return -1;
        }

        if (i == 0) {
            job->pgid = pid;
            setpgid(pid, pid); 
        } else {
            setpgid(pid, job->pgid);
        }
        if (fdin != STDIN_FILENO) {
            close(fdin);
        }
        if (i < job->num_processes - 1) {
            close(pipe_fds[1]); // Close the write end in the parent
            fdin = pipe_fds[0];  
        }
    }
    if (job->is_background) {
        track_job(job); 
        return 1;
    } else {
        wait_for_job(job);
        return 1;
    }
}

/**
 * Forks and executes a single command, handling redirection.
 */
int launch_process(Process *p, pid_t pgid, int fdin, int fdout, bool is_background) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child Process 
        setpgid(0, (pgid == 0) ? 0 : pgid); 
        if (fdin != STDIN_FILENO) {
            dup2(fdin, STDIN_FILENO);
            close(fdin);
        }
        if (fdout != STDOUT_FILENO) {
            dup2(fdout, STDOUT_FILENO);
            close(fdout);
        }
        if (p->input_file) {
            int fd = open(p->input_file, O_RDONLY);
            if (fd < 0) { 
                perror("quash: open input file"); 
                exit(1); 
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        if (p->output_file) {
            int flags = O_WRONLY | O_CREAT;
            if (p->append_output) {
                flags |= O_APPEND; // >>
            } else {
                flags |= O_TRUNC;  // >
            }
            int fd = open(p->output_file, flags, 0666); 
            if (fd < 0) { 
                perror("quash: open output file"); 
                exit(1); 
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (execvp(p->argv[0], p->argv) == -1) {
            perror("quash: command not found");
            exit(EXIT_FAILURE); 
        }

    } else if (pid < 0) {
        // Error forking
        perror("quash: fork error");
        return -1;
    } 

    // Parent Process 
    return pid;
}
/**
 * Waits for a foreground job's process group to finish.
 */
void wait_for_job(Job *job) {
    pid_t wpid;
    int status;
    do {

        wpid = waitpid(-job->pgid, &status, WUNTRACED); 
        if (wpid == -1 && errno != ECHILD) {
             perror("quash: waitpid error");
             break;
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    free_job(job); 
}

/**
 * Adds a background job to the global list and prints the startup message.
 */
void track_job(Job *job) {
    job->job_id = get_next_job_id();
    if (job->job_id <= MAX_JOBS) {
        job_list[job->job_id - 1] = job;
        printf("Background job started: [%d] %d %s &\n",
               job->job_id, (int)job->pgid, job->command_line);
    } else {
        fprintf(stderr, "quash: too many jobs\n");
        free_job(job);
    }
}

/**
 * Checks for completed background jobs (called at the start of the main loop).
 */
void check_jobs_status() {
    pid_t wpid;
    int status;
    
    while ((wpid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < max_job_id; i++) {
            if (job_list[i] != NULL && job_list[i]->pgid == wpid) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    printf("Completed: [%d] %d %s\n",
                           job_list[i]->job_id, (int)wpid, job_list[i]->command_line);
                    free_job(job_list[i]);
                    job_list[i] = NULL;
                }

                break; 
            }
        }
    }
}

void free_job(Job *job);


void free_job(Job *job) {
    if (job == NULL){
        return;
    }

    if (job->command_line) {
        free(job->command_line);
    }
    for (int i = 0; i < job->num_processes; i++) {
        Process *p = &job->processes[i];
        if (p->input_file) {
            free(p->input_file);
        }
        if (p->output_file) {
            free(p->output_file);
        }

        if (p->argv) {
            for (int j = 0; p->argv[j] != NULL; j++) {
                free(p->argv[j]); 
            }
            free(p->argv); 
        }
    }
    if (job->processes) {
        free(job->processes);
    }
    free(job);
}