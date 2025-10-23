/* Main loop */

#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void quash_loop(void) {
    char *line;
    Job *job;
    int status;
    
    // Initial setup (signal handlers, job list, etc.)
    initialize_job_control();
    
    do {
        // 1. Check for finished background jobs and print status
        check_jobs_status();
        
        // 2. Print Prompt
        printf("[QUASH]$ ");
        fflush(stdout);

        // 3. Read Line (Implementation in command.c/quash.h)
        line = read_line(); 
        if (line == NULL) {
            // EOF (Ctrl+D)
            break;
        }

        // 4. Parse Command (Implementation in command.c)
        // This function handles comments, I/O redirection, and pipes.
        job = parse_command(line); 
        
        // 5. Execute Command
        if (job != NULL) {
            // Execute the job (builtin, foreground, or background external command)
            status = execute_job(job); 
            // Handle job cleanup if necessary
            // free_job(job);
        }

        free(line);
    } while (status != 0); // Assuming 0 means 'quit' or 'exit' for the loop
}

int main(int argc, char **argv) {
    // Initial banner or welcome message
    printf("Welcome to Quash!\n");

    quash_loop();

    return EXIT_SUCCESS;
}