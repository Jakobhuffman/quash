/* Parsing comands in the Inteface */
/* command.c */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/**
 * Reads a line of input from standard input.
 * The memory for the line is allocated by getline.
 */
char *read_line(void) {
    char *line = NULL;
    size_t bufsize = 0; // getline manages the buffer size
    ssize_t chars_read;

    // Getline will allocate memory for 'line'.
    chars_read = getline(&line, &bufsize, stdin);

    if (chars_read == -1) {
        // EOF (Ctrl+D) or error
        if (feof(stdin)) {
            // End of File (EOF)
            free(line);
            return NULL;
        } else {
            perror("quash: getline error");
            free(line);
            exit(EXIT_FAILURE); // Fatal error
        }
    }

    // Remove trailing newline character
    if (line[chars_read - 1] == '\n') {
        line[chars_read - 1] = '\0';
    }

    return line;
}

/* command.c (Snippet for parse_command) */

Job *parse_command(char *line) {
    if (line == NULL || *line == '\0') {
        return NULL; // Empty input
    }
    
    // Create a copy of the line to tokenize and store in the Job struct
    char *line_copy = strdup(line);
    if (!line_copy) { perror("strdup"); return NULL; }

    Job *job = (Job *)malloc(sizeof(Job));
    if (!job) { perror("malloc"); free(line_copy); return NULL; }
    memset(job, 0, sizeof(Job));
    job->command_line = line_copy;

    // --- 1. Handle Comments ---
    char *comment_pos = strchr(line_copy, '#');
    if (comment_pos) {
        *comment_pos = '\0'; // Terminate the string at the comment
    }
    
    // --- 2. Check for Background Job (&) ---
    size_t len = strlen(line_copy);
    if (len > 0 && line_copy[len - 1] == '&') {
        job->is_background = true;
        line_copy[len - 1] = '\0'; // Remove '&'
        // Trim any preceding whitespace left after removing '&'
        while (len > 0 && (line_copy[len - 2] == ' ' || line_copy[len - 2] == '\t')) {
            line_copy[len - 2] = '\0';
            len--;
        }
    } else {
        job->is_background = false;
    }
    
    // --- 3. Split by Pipe (|) ---
    // You'll need a dedicated function (e.g., split_pipes) here.
    // Use strtok or a loop with strchr to split the line_copy by '|'.
    // Each resulting segment is a separate Process in the pipeline.
    
    // Example conceptual structure:
    /*
    char *pipe_segments[MAX_PIPES];
    int num_pipes = 0;
    
    char *token = strtok(line_copy, "|");
    while (token != NULL) {
        pipe_segments[num_pipes++] = strdup(token); // Store a copy
        token = strtok(NULL, "|");
    }
    
    job->num_processes = num_pipes;
    job->processes = (Process *)calloc(num_pipes, sizeof(Process));
    
    // --- 4. Parse Each Process/Segment (Redirection and Arguments) ---
    for (int i = 0; i < num_pipes; i++) {
        // Call a function like parse_process_segment(pipe_segments[i], &job->processes[i])
        // This function handles:
        //    a. Identifying '<', '>', '>>' and extracting file paths.
        //    b. Tokenizing the remaining command/arguments into char** argv.
    }
    */
    
    return job; // Return the fully parsed job structure
}
// Conceptual helper function to tokenize arguments
#define QUASH_TOK_DELIM " \t\r\n\a" // Delimiters for command arguments

char **split_line_to_args(char *line) {
    int bufsize = 64;
    int position = 0;
    char **tokens = (char **)malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        perror("quash: allocation error");
        exit(EXIT_FAILURE);
    }

    // Use strtok to split the command string by spaces/tabs
    // NOTE: This simple strtok will break if you have quoted arguments!
    token = strtok(line, QUASH_TOK_DELIM);
    while (token != NULL) {
        // You should handle variable expansion ($VAR) here or later
        tokens[position] = token; 
        position++;

        if (position >= bufsize) {
            // Reallocate memory if needed
            bufsize += 64;
            tokens = (char**)realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                perror("quash: allocation error");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, QUASH_TOK_DELIM);
    }
    tokens[position] = NULL; // NULL-terminate the argument list
    return tokens;
}