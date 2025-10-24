#ifndef QUASH_H
#define QUASH_H

#include <sys/types.h> 
#include <stdbool.h>


typedef struct Process {
    pid_t pid;
    char **argv;        // Argument list
    char *input_file;     
    char *output_file;   
    bool append_output;  
} Process;


typedef struct Job {
    int job_id; //Quash job identifier
    pid_t pgid; // Process Group ID 
    char *command_line; //Command line string
    Process *processes; //Array of processes in the pipeline
    int num_processes;
    bool is_background;
} Job;


Job **job_list; 
int max_job_id;


// Main loop
void quash_loop(void);

// Comand parsing
char *read_line(void);
struct Job *parse_command(char *line);

// Execution
int execute_job(struct Job *job);
int execute_builtin(char **args);

// Builtin commands
int quash_cd(char **args);
int quash_echo(char **args);
int quash_pwd(char **args);    
int quash_export(char **args); 
int quash_jobs(char **args);  
int quash_kill(char **args);

// Job Control
void initialize_job_control(void);
void track_job(struct Job *job);
void check_jobs_status(void);
void free_job(Job *job);

#endif