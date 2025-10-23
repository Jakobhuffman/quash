/* Handling using fork, exec */
/* job_control.c */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

// Global variables defined in quash.h but implemented here (or in quash.c)
// Job *job_list; 
// int max_job_id; 

#define MAX_JOBS 1024 // Define a limit for the job list size (if using a fixed array)

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

/**
 * Main execution handler: checks for built-in vs. external.
 */
int execute_job(Job *job) {
    // Assume job->processes[0].argv[0] is the command name
    char *command = job->processes[0].argv[0];
    
    // 1. Check if it's a built-in command (e.g., 'cd', 'exit')
    // NOTE: Built-ins should be checked BEFORE the pipeline/redirection logic.
    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        // Execute the built-in function
        // quash_exit(job->processes[0].argv); 
        return 0; // Signal main loop to exit
    } 
    // ... check other built-ins (cd, echo, export, pwd, jobs, kill) ...
    if (strcmp(command, "cd") == 0) {
        // execute_builtin(job->processes[0].argv); 
        return 1; // Signal success (stay in loop)
    }

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

        // 2. Handle File I/O Redirection (file_in, file_out, file_append)
        // (This code depends on your Process struct parsing)
        // NOTE: The original code referenced p->input_file / p->output_file / p->append_output,
        // but those fields are not present in your Process struct; avoid referencing them here.
        // If your Process struct provides different field names for redirection (for example
        // p->infile, p->outfile, p->append), implement the open/dup2 logic using those names.
        //
        // Example (uncomment and adjust field names if your struct provides them):
        // if (p->infile) {
        //     int fd = open(p->infile, O_RDONLY);
        //     if (fd < 0) { perror("quash: open input"); exit(1); }
        //     dup2(fd, STDIN_FILENO);
        //     close(fd);
        // }
        // if (p->outfile) {
        //     int flags = O_WRONLY | O_CREAT;
        //     if (p->append) {
        //         flags |= O_APPEND; // >>
        //     } else {
        //         flags |= O_TRUNC;  // >
        //     }
        //     int fd = open(p->outfile, flags, 0666);
        //     if (fd < 0) { perror("quash: open output"); exit(1); }
        //     dup2(fd, STDOUT_FILENO);
        //     close(fd);
        // }

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
    // Add job to job_list array/linked list (Need your specific job_list logic here)
    // job_list[job->job_id] = *job; 

    // Print the required startup message
    printf("Background job started: [%d] %d %s &\n", 
           job->job_id, (int)job->pgid, job->command_line);
}

/**
 * Checks for completed background jobs (called at the start of the main loop).
 */
void check_jobs_status() {
    pid_t wpid;
    int status;
    
    // Loop through the active job list (depends on your job_list structure)
    // For simplicity, let's assume we loop MAX_JOBS times or through the list.
    
    // Use WNOHANG: returns 0 if no child has exited, or the PID of the exited child.
    while ((wpid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        
        // Find the job associated with this wpid (by checking job->processes[] PIDs or PGID)
        Job *completed_job = NULL; 
        
        // --- Loop to find the job associated with wpid ---
        
        if (completed_job != NULL) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                // Job truly finished
                printf("Completed: [%d] %d %s\n", 
                       completed_job->job_id, (int)wpid, completed_job->command_line);
                // Remove the job from the job_list (cleanup)
                // free_job(completed_job); 
            }
            // Add logic for WIFSTOPPED (Ctrl+Z) or WIFCONTINUED if handling those signals.
        }
    }
}