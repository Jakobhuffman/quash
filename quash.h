#ifndef QUASH_H
#define QUASH_H

#include <sys/types.h> // for pid_t
#include <stdbool.h>

// --- Job Management Structures ---

// A single process within a job (e.g., in a pipe)
typedef struct Process {
    pid_t pid;
    char **argv; // Command and arguments
    // maybe status/exit code
} Process;

// A job can be a single command or a pipeline
typedef struct Job {
    int job_id; // Unique Quash job identifier
    pid_t pgid; // Process Group ID (usually the PID of the first process in the pipeline)
    char *command_line; // Original command line string
    Process *processes; // Array of processes in the pipeline
    int num_processes;
    bool is_background;
    // Status (e.g., RUNNING, COMPLETED, STOPPED)
} Job;

// Global list to track background jobs
Job *job_list;
int max_job_id;

// --- Function Prototypes ---

// Main loop
void quash_loop(void);

// Parsing
char *read_line(void);
struct Job *parse_command(char *line);

// Execution
int execute_job(struct Job *job);
int execute_builtin(char **args);

// Built-ins
int quash_cd(char **args);
int quash_echo(char **args);
// ... others ...

// Job Control
void initialize_job_control(void);
void track_job(struct Job *job);
void check_jobs_status(void);

#endif