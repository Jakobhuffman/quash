/* Parsing comands in the Inteface */
/* command.c */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>

// Delimiters for command arguments (already defined in your file)
#define QUASH_TOK_DELIM " \t\r\n\a" 
#define MAX_PIPES 10 // Define a constant limit

/**
 * Parses a single command segment (one side of a pipe),
 * extracting I/O redirection and tokenizing arguments.
 */
static void parse_process_segment(char *segment, Process *p) {
    // 1. Initialize process redirection fields
    p->input_file = NULL;
    p->output_file = NULL;
    p->append_output = false;
    
    // We build a new argument list that excludes the redirection tokens
    int bufsize = 64;
    int position = 0;
    // Note: The tokens in temp_tokens will be pointers to newly allocated memory (strdup)
    char **temp_tokens = (char **)malloc(bufsize * sizeof(char*));
    if (!temp_tokens) { perror("malloc"); exit(EXIT_FAILURE); }

    char *token;
    char *saveptr;
    
    // We use strtok_r to safely tokenize and allow nested parsing if needed.
    token = strtok_r(segment, QUASH_TOK_DELIM, &saveptr);
    // 0=none, 1=input pending, 2=output pending
    int redirection_pending = 0; 

    while (token != NULL) {
        if (redirection_pending) {
            // The previous token was a redirection symbol, this token is the filename
            if (redirection_pending == 1) { 
                p->input_file = strdup(token);
            } else { // redirection_pending == 2
                p->output_file = strdup(token);
            }
            redirection_pending = 0;
        } else if (strcmp(token, "<") == 0) {
            redirection_pending = 1; // Input file pending
        } else if (strcmp(token, ">>") == 0) {
            p->append_output = true;
            redirection_pending = 2; // Output file pending
        } else if (strcmp(token, ">") == 0) {
            p->append_output = false;
            redirection_pending = 2; // Output file pending
        } else {
            // Regular argument
            temp_tokens[position++] = strdup(token);

            if (position >= bufsize) {
                bufsize += 64;
                temp_tokens = (char**)realloc(temp_tokens, bufsize * sizeof(char*));
                if (!temp_tokens) { perror("realloc"); exit(EXIT_FAILURE); }
            }
        }
        token = strtok_r(NULL, QUASH_TOK_DELIM, &saveptr);
    }
    
    temp_tokens[position] = NULL; // NULL-terminate the argument list
    p->argv = temp_tokens;
}
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

// Helper to trim leading and trailing whitespace
char *trim_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) // All spaces
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = 0;

    return str;
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
    char *pipe_segments[MAX_PIPES]; // Temporary array for segments
    int num_pipes = 0;
    
    // We use a copy of the string to tokenize for pipes
    char *pipe_input = strdup(line_copy);
    char *token;

    token = strtok(pipe_input, "|");
    while (token != NULL) {
        if (num_pipes >= MAX_PIPES) {
            fprintf(stderr, "quash: too many pipes\n");
            // NOTE: Add cleanup logic here
            free(pipe_input);
            // ... cleanup job struct and return NULL ...
            return NULL; 
        }
        // Store a copy of the segment string for later parsing
        pipe_segments[num_pipes++] = strdup(token); 
        token = strtok(NULL, "|");
    }
    free(pipe_input); // Free the string used for pipe tokenizing

    if (num_pipes == 0) {
        // Empty command after background check and comment removal
        free(job->command_line);
        free(job);
        return NULL;
    }

    job->num_processes = num_pipes;
    job->processes = (Process *)calloc(num_pipes, sizeof(Process));
    if (!job->processes) { 
        perror("calloc"); 
        // ... cleanup job struct and return NULL ...
        return NULL;
    }
    
    // --- 4. Parse Each Process/Segment (Redirection and Arguments) ---
    for (int i = 0; i < num_pipes; i++) {
        // TRIMMING THE SEGMENT IS CRUCIAL
        char *trimmed_segment = trim_whitespace(pipe_segments[i]); 
        parse_process_segment(trimmed_segment, &job->processes[i]);
        
        // NOTE: The previous code freed pipe_segments[i] which was strdup'd. 
        // Ensure you free the *original pointer* to prevent memory leaks.
        free(pipe_segments[i]);
    }
    
    return job; // Return the fully parsed job structure
}
// Conceptual helper function to tokenize arguments


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