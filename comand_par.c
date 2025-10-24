/* Parsing comands in the Inteface */
#include "quash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>

#define QUASH_TOK_DELIM " \t\r\n\a" 
#define MAX_PIPES 10 // Maximum number of pipes supported

static void parse_process_segment(char *segment, Process *p) {
    p->input_file = NULL;
    p->output_file = NULL;
    p->append_output = false;
    
    int bufsize = 64;
    int position = 0;

    char **temp_tokens = (char **)malloc(bufsize * sizeof(char*));
    if (!temp_tokens) { 
        perror("malloc"); 
        exit(EXIT_FAILURE); 
    }

    char *token;
    char *saveptr;
    
    token = strtok_r(segment, QUASH_TOK_DELIM, &saveptr);
    // 0=none, 1=input pending, 2=output pending
    int redirection_pending = 0; 

    while (token != NULL) {
        if (redirection_pending) {
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
            temp_tokens[position++] = strdup(token);

            if (position >= bufsize) {
                bufsize += 64;
                temp_tokens = (char**)realloc(temp_tokens, bufsize * sizeof(char*));
                if (!temp_tokens) { 
                    perror("realloc"); 
                    exit(EXIT_FAILURE); 
                }
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
    size_t bufsize = 0; // manages the buffer size
    ssize_t chars_read;
    chars_read = getline(&line, &bufsize, stdin);

    if (chars_read == -1) {
      
        if (feof(stdin)) {
            free(line);
            return NULL;
        } else {
            perror("quash: getline error");
            free(line);
            exit(EXIT_FAILURE);
        }
    }

    // Remove trailing newline character
    if (line[chars_read - 1] == '\n') {
        line[chars_read - 1] = '\0';
    }

    return line;
}

// trim leading and trailing whitespace
char *trim_whitespace(char *str) {
    char *end;

    if (str == NULL || *str == 0){
        return str;
    }

    // Trim leading space
    while(isspace((unsigned char)*str)) {
        str++;
    }

    if(*str == 0) {// All spaces
        return str;
    }

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = 0;

    return str;
}


Job *parse_command(char *line) {
    if (line == NULL || *line == '\0') {
        return NULL; // Empty input
    }

    char *line_copy = strdup(line);
    if (!line_copy) { 
        perror("strdup"); return NULL; 
    }

    Job *job = (Job *)malloc(sizeof(Job));
    if (!job) { 
        perror("malloc"); free(line_copy); 
        return NULL; 
    }
    memset(job, 0, sizeof(Job));
    job->command_line = line_copy;

    char *comment_pos = strchr(line_copy, '#');
    if (comment_pos) {
        *comment_pos = '\0'; // terminate the string at the comment
    }
    
    
    size_t len = strlen(line_copy);
    if (len > 0 && line_copy[len - 1] == '&') {
        job->is_background = true;
        line_copy[len - 1] = '\0'; // Remove '&'
        while (len > 0 && (line_copy[len - 2] == ' ' || line_copy[len - 2] == '\t')) {
            line_copy[len - 2] = '\0';
            len--;
        }
    } else {
        job->is_background = false;
    }
    
    char *pipe_segments[MAX_PIPES]; 
    int num_pipes = 0;
    char *pipe_input = strdup(line_copy);
    char *token;

    token = strtok(pipe_input, "|");
    while (token != NULL) {
        if (num_pipes >= MAX_PIPES) {
            fprintf(stderr, "quash: too many pipes\n");
            free(pipe_input);
            return NULL; 
        }
        pipe_segments[num_pipes++] = strdup(token); 
        token = strtok(NULL, "|");
    }
    free(pipe_input);

    if (num_pipes == 0) {
        free(job->command_line);
        free(job);
        return NULL;
    }

    job->num_processes = num_pipes;
    job->processes = (Process *)calloc(num_pipes, sizeof(Process));
    if (!job->processes) { 
        perror("calloc"); 
        return NULL;
    }
    
    for (int i = 0; i < num_pipes; i++) {
        char *trimmed_segment = trim_whitespace(pipe_segments[i]); 
        parse_process_segment(trimmed_segment, &job->processes[i]);
        free(pipe_segments[i]);
    }
    
    return job;
}

char **split_line_to_args(char *line) {
    int bufsize = 64;
    int position = 0;
    char **tokens = (char **)malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        perror("quash: allocation error");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, QUASH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token; 
        position++;

        if (position >= bufsize) {  
            bufsize += 64;
            tokens = (char**)realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                perror("quash: allocation error");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, QUASH_TOK_DELIM);
    }
    tokens[position] = NULL; 
    return tokens;
}