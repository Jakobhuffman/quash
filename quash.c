/* Main loop */

#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void quash_loop(void) {
    char *line;
    Job *job;
    int status;
    
    initialize_job_control();
    
    do {
        check_jobs_status();
        
        printf("[QUASH]$ ");
        fflush(stdout);

        // read a line from stdin
        line = read_line(); 
        if (line == NULL) {
            break;
        }
        // handles comments, I/O redirection, and pipes.
        job = parse_command(line); 
        
        if (job != NULL) {
            // Execute the job 
            status = execute_job(job); 
        }

        free(line);
    } while (status != 0); 
}

int main(int argc, char **argv) {
    printf("Welcome to Quash!\n");
    quash_loop();

    return EXIT_SUCCESS;
}