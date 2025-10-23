/* Handling using fork, exec */
/* job_control.c */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h> // For O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND

// Global variables defined in quash.h but implemented here (or in quash.c)
// Job *job_list; 
// int max_job_id; 

#define MAX_JOBS 1024 // Define a limit for the job list size (if using a fixed array)

void initialize_job_control(void) {
    // 1. Allocate memory for the global job list array
    job_list = (Job **)calloc(MAX_JOBS, sizeof(Job *)); // Allocate for pointers to Job structs
    if (job_list == NULL) {
        perror("quash: Failed to initialize job list");
        exit(EXIT_FAILURE);
    }
    
    // 2. Initialize the max job ID counter
    max_job_id = 0;
}


// Helper to find the next available job ID
int get_next_job_id() {
    // Logic to find the next job ID (max_job_id + 1, and wrap around/reuse if needed)
    // For simplicity, let's assume max_job_id increments and we reuse slots later.
    return ++max_job_id; 
}

/* job_control.c (or common launcher called by quash.c) */

// Forward declarations for pipeline stages and external execution
int launch_process(Process *p, pid_t pgid, int fdin, int fdout, bool is_background);
int launch_job(Job *job); // Handles the initial fork for the pipeline
void wait_for_job(Job *job); // Wait for a foreground job

/**
 * Main execution handler: checks for built-in vs. external.
 */
int execute_job(Job *job) {
    char *command = job->processes[0].argv[0];
    char **args = job->processes[0].argv; 
    
    if (command == NULL) {
        // Handle job cleanup here
        return 1;
    }

    // 1. Check if it's a built-in command AND not part of a pipe
    if (job->num_processes == 1) { // <-- CRITICAL CHECK
        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            free_job(job); // Cleanup before exiting
            exit(0);
        }
        else if (strcmp(command, "cd") == 0) {
            quash_cd(args);
            free_job(job); // Cleanup
            return 1;
        }
        // ... (Repeat for echo, export, pwd, jobs, kill) ...
        else if (strcmp(command, "echo") == 0) {
            quash_echo(args);
            free_job(job); // Cleanup
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
    // NOTE: Built-ins should not be run in a subshell, so they should return immediately.

    // 2. External Command (or pipeline)
    return launch_job(job);
}

/**
 * Handles the forking and waiting for a single or piped job.
 */
int launch_job(Job *job) {
    // This is the most complex part: setting up pipes, forking, and I/O redirection.

    int pipe_fds[2]; // For pipe(2)
    int fdin = STDIN_FILENO; // Initial input is standard input
    pid_t pid;

    // 1. Loop through all processes in the job (pipeline stages)
    for (int i = 0; i < job->num_processes; i++) {
        Process *p = &job->processes[i];
        
        // If it's not the last command in the pipe, create a pipe
        if (i < job->num_processes - 1) {
            if (pipe(pipe_fds) < 0) {
                perror("quash: pipe error");
                // Handle job failure and cleanup
                return -1;
            }
            // The next process will read from pipe_fds[0]
            // The current process will write to pipe_fds[1]
        }
        
        // 2. Launch the current process (handles fork, exec, redirect)
        // fdout is the write end of the current pipe, or STDOUT_FILENO for the last command.
        int fdout = (i < job->num_processes - 1) ? pipe_fds[1] : STDOUT_FILENO;
        pid = launch_process(p, job->pgid, fdin, fdout, job->is_background);

        // 3. Parent Cleanup and Setup for Next Stage
        if (pid < 0) {
            // Error handling
            // Clean up any open pipes
            return -1;
        }

        // Set the Process Group ID (PGID) for the entire job
        if (i == 0) {
            job->pgid = pid; // First PID is the PGID for the job
            setpgid(pid, pid); 
        } else {
            setpgid(pid, job->pgid);
        }

        // Close the read end of the pipe we just finished with (if one was used)
        if (fdin != STDIN_FILENO) {
            close(fdin);
        }
        
        // Set up the input for the next process
        if (i < job->num_processes - 1) {
            close(pipe_fds[1]); // Close the write end in the parent
            fdin = pipe_fds[0];  // The next process will read from the pipe's read end
        }
    }
    
    // 4. Job Control (Foreground vs. Background)
    if (job->is_background) {
        track_job(job); // Add to job_list and print "Background job started..."
        return 1;
    } else {
        // Foreground: Wait for the entire process group to finish
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
        // --- Child Process ---
        
        // Set up process group ID
        setpgid(0, (pgid == 0) ? 0 : pgid); 

        // 1. Handle Pipe I/O Redirection (dup2)
        if (fdin != STDIN_FILENO) {
            dup2(fdin, STDIN_FILENO);
            close(fdin);
        }
        if (fdout != STDOUT_FILENO) {
            dup2(fdout, STDOUT_FILENO);
            close(fdout);
        }
        // NOTE: All pipe FDs must be closed after dup2 in the child process.

        // 2. Handle File I/O Redirection (using the fields added to the Process struct)
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
            
            // 0666 is the permission for the new file
            int fd = open(p->output_file, flags, 0666); 
            if (fd < 0) { 
                perror("quash: open output file"); 
                exit(1); 
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        // 3. Execute the command
        if (execvp(p->argv[0], p->argv) == -1) {
            // execvp only returns on failure
            perror("quash: command not found");
            exit(EXIT_FAILURE); 
        }

    } else if (pid < 0) {
        // Error forking
        perror("quash: fork error");
        return -1;
    } 

    // --- Parent Process ---
    return pid; // Return PID to the launcher
}

/* job_control.c */

/**
 * Waits for a foreground job's process group to finish.
 */
void wait_for_job(Job *job) {
    pid_t wpid;
    int status;
    
    // Wait for any process in the job's group (-pgid) to change state (WUNTRACED)
    do {
        // The negative PGID means "wait for any child whose PGID is |job->pgid|"
        wpid = waitpid(-job->pgid, &status, WUNTRACED); 
        if (wpid == -1 && errno != ECHILD) {
             perror("quash: waitpid error");
             break;
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    // Cleanup the job structure after it finishes
    // free_job(job); 
}

/* job_control.c */

/**
 * Adds a background job to the global list and prints the startup message.
 */
void track_job(Job *job) {
    job->job_id = get_next_job_id();
    if (job->job_id <= MAX_JOBS) {
        // Store the pointer to the job, not a copy of the struct
        // The job ID is 1-based, array is 0-based
        job_list[job->job_id - 1] = job;

        // Print the required startup message
        printf("Background job started: [%d] %d %s &\n",
               job->job_id, (int)job->pgid, job->command_line);
    } else {
        fprintf(stderr, "quash: too many jobs\n");
        // We are not tracking this job, so it should be freed.
        free_job(job);
    }
}

/**
 * Checks for completed background jobs (called at the start of the main loop).
 */
void check_jobs_status() {
    pid_t wpid;
    int status;
    
    // Use WNOHANG: returns 0 if no child has exited, or the PID of the exited child.
    while ((wpid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        // Find the job associated with this wpid by checking its PGID
        for (int i = 0; i < max_job_id; i++) {
            if (job_list[i] != NULL && job_list[i]->pgid == wpid) {
                // Check if the process group has terminated
                // A simple check is to see if the leader of the group (wpid) has exited/been signaled
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    printf("Completed: [%d] %d %s\n",
                           job_list[i]->job_id, (int)wpid, job_list[i]->command_line);
                    
                    // Free the job resources and remove it from the list
                    free_job(job_list[i]);
                    job_list[i] = NULL;
                }
                // You could add logic here for WIFSTOPPED (Ctrl+Z) if you implement that feature

                break; // Found the job, no need to search further
            }
        }
    }
}

 // Add this prototype to quash.h
void free_job(Job *job);

/* job_control.c */
void free_job(Job *job) {
    if (job == NULL) return;

    // 1. Free the original command line string
    if (job->command_line) {
        free(job->command_line);
    }
    
    // 2. Loop through all processes in the job
    for (int i = 0; i < job->num_processes; i++) {
        Process *p = &job->processes[i];
        
        // 3. Free redirection strings
        if (p->input_file) {
            free(p->input_file);
        }
        if (p->output_file) {
            free(p->output_file);
        }

        // 4. Free the argument list (p->argv) AND the argument strings inside it.
        // NOTE: If you used strdup for arguments in parse_process_segment, 
        // you MUST free each argument string before freeing the array.
        if (p->argv) {
            for (int j = 0; p->argv[j] != NULL; j++) {
                free(p->argv[j]); // Free each strdup'd argument string
            }
            free(p->argv); // Free the array itself
        }
    }
    
    // 5. Free the array of processes
    if (job->processes) {
        free(job->processes);
    }
    
    // 6. Free the job structure
    free(job);
}