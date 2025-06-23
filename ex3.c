#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <bits/time.h>
#include <sys/resource.h>
#include <ctype.h>
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <pthread.h>

/**** CONSTANTS ****/
#define MAX_INPUT_LENGTH 1024
#define MAX_INPUT_LENGTHH 1025    // Fixed buffer size with null terminator
#define MAX_ARGC 7
const char delim[] = " ";
#define MAX_MATRICES 1024



/**** CUSTOM COMMANDS STRUCTURE ****/
typedef struct {
    const char *name;
    int (*handler)(void);
    int requires_pipe;
    int supports_append;
    int min_args;
} CustomCommand;


typedef struct {
    int rows;
    int cols;
    int* data; // 1D array storing matrix elements row-wise
} Matrix;
/**** FUNCTION PROTOTYPES ****/
// Input handling
void get_string(char* buffer, size_t buffer_size);
char** split_to_args(const char *string, const char *delimiter, int *count);
int checkMultipleSpaces(const char* input);
char* trim_inplace(char* str);
void free_args(char **args);
int pipe_split(char *input, char *left_cmd, char *right_cmd);
void strip_crlf(char *str);

// File operations
char** read_file_lines(const char* filename, int* num_lines);
void append_to_log(const char *filename, char* val1, float val2);
void write_to_file(const char *filename, const char *content, int append);

// Command processing
int is_dangerous_command(char **user_args, int user_args_len);
float time_diff(struct timespec start, struct timespec end);
void update_min_max_time(double current_time, double *min_time, double *max_time);
void prompt(void);
void check_append_flag(char **args, int args_len, int *append_flg);
void redirect_stderr_to_file(const char *filename);
void check_and_redirect_stderr(char **l_args);

// Signal handlers
void sigchld_handler(int sig);
void sigxcpu_handler(int sig);
void sigxfsz_handler(int sig);

// Resource limiting
int get_resource_type(const char *res_name);
unsigned long long parse_value_with_unit(const char *str);
char **check_rsc_lmt(char **argu, int *args_len);
void show_resource_limit(const char *name, int resource_type);
void show_all_resource_limits(void);

// Error handling
void handle_execvp_errors_in_child(char **args);
void* safe_malloc(size_t size);
void restore_stderr(void);

// Custom commands
int my_tee_handler(void);
// matrix handler
void mcalc_handler(char* input);
int parse_input(const char* input, Matrix* matrices, int* matrix_count, char* operation_out);
int check_same_dimensions(Matrix* matrices, int count);
void free_matrices(Matrix* matrices, int count);
int parse_matrix(const char* token, Matrix* matrix);
int is_uppercase(const char* str);
void* matrix_thread_operation(void* arg);
Matrix copy_matrix(Matrix* original);
Matrix hierarchical_matrix_calculation(Matrix* matrices, int matrix_count, char* operation);//


/////MONITORING
// Add these to your global variables
typedef struct {
    int operation_count;
    int error_count;
    int total_matrices_processed;
    int max_matrix_size;  // largest matrix by element count
    int add_operations;
    int sub_operations;
} MatrixStats;

MatrixStats matrix_stats = {0, 0, 0, 0, 0, 0};

// Add this function to log matrix operations
void log_matrix_operation(Matrix* matrices, int count, const char* operation, int success) {
    FILE* log = fopen("matrix_operations.log", "a");
    if (!log) return;

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(log, "[%s] Operation: %s, Matrices: %d, Success: %s\n",
            timestamp, operation, count, success ? "YES" : "NO");

    if (success) {
        fprintf(log, "  Dimensions: (%d,%d)\n", matrices[0].rows, matrices[0].cols);

        for (int i = 0; i < count; i++) {
            fprintf(log, "  Matrix #%d: (", i+1);
            for (int j = 0; j < matrices[i].rows * matrices[i].cols; j++) {
                fprintf(log, "%d", matrices[i].data[j]);
                if (j < matrices[i].rows * matrices[i].cols - 1)
                    fprintf(log, ",");
            }
            fprintf(log, ")\n");
        }
    } else {
        fprintf(log, "  ERROR: Operation failed\n");
    }

    fprintf(log, "  Stats: Total Ops=%d, Errors=%d, ADD=%d, SUB=%d\n",
            matrix_stats.operation_count, matrix_stats.error_count,
            matrix_stats.add_operations, matrix_stats.sub_operations);
    fprintf(log, "--------------------------------------------------\n");

    fclose(log);
}
//////////////////////////////////////////////////////////////////////
/**** GLOBAL VARIABLES ****/
// Custom commands table
CustomCommand custom_commands[] = {
        {"my_tee", my_tee_handler, 1, 1, 1}, // my_tee requires pipe, supports append, needs at least 1 arg
        {NULL, NULL, 0, 0, 0}                // Terminator entry
};

// Command handling
char **Danger_CMD = NULL;      // List of dangerous commands loaded from file
int l_args_len = 0;            // Arguments count in left command
int r_args_len = 0;            // Arguments count in right command
int numLines = 0;              // Number of dangerous commands
char **l_args = NULL;          // Arguments array for left command
char **r_args = NULL;          // Arguments array for right command
struct timespec start, end;    // Timestamps for timing command execution
int flag_semi_dangerous = 0;   // Flag for semi-dangerous commands

// Statistics tracking
int total_cmd_count = 0;              // Total successful commands
int dangerous_cmd_blocked_count = 0;  // Dangerous commands blocked
double last_cmd_time = 0;             // Last command execution time
double average_time = 0;              // Average execution time
double total_time_all = 0;            // Sum of execution times
double min_time = 0;                  // Minimum command time
double max_time = 0;                  // Maximum command time
int semi_dangerous_cmd_count = 0;     // Similar-but-allowed commands count

// Pipe and command state
int pip_flag = 0;              // Flag for pipe existence
int pipefd[2];                 // For pipe communication
int append_flg = 0;            // Flag for append mode
int left_status;               // Exit status of left command
int right_status;              // Exit status of right command
char userInput[MAX_INPUT_LENGTHH]; // Buffer for user input
int background_flag = 0;       // Flag for background execution
char current_command[MAX_INPUT_LENGTHH]; // Current command for logging
const char *output_file = NULL;   // Path to output log file
int original_stderr_fd = -1;      // Original stderr for restoration
int stderr_redirected = 0;        // Flag if stderr was redirected
pid_t left_pid;                   // PID of left command process


/**** UTILITY FUNCTIONS ****/

// Safe memory allocation with error handling
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }
    return ptr;
}

// Error handling for child processes when exec fails
void handle_execvp_errors_in_child(char **args) {
    if (!args || !args[0]) {
        fprintf(stderr, "ERR\n");
        exit(1);
    }

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = sigxfsz_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGXFSZ, &sa, NULL);
    sigaction(SIGXCPU, &sa, NULL);

    // Try to execute the command
    execvp(args[0], args);

    // If execvp returns, there was an error
    if (errno == EMFILE) {
        fprintf(stderr, "Too many open files!\n");
    } else if (errno == ENOMEM) {
        fprintf(stderr, "Memory allocation failed!\n");
    } else {
        perror("exec failed");
    }
    exit(127);
}

// Handler for SIGXCPU signal (CPU time limit exceeded)
void sigxcpu_handler(int sig) {
    fprintf(stderr, "CPU time limit exceeded!\n");
    fflush(stderr);

    exit(1);
}

// Handler for SIGXFSZ signal (File size limit exceeded)
void sigxfsz_handler(int sig) {
    fprintf(stderr, "File size limit exceeded!\n");
    fflush(stderr);
    exit(1);
}

// Find a custom command by name
const CustomCommand* find_custom_command(const char *cmd_name) {
    if (!cmd_name) return NULL;

    for (int i = 0; custom_commands[i].name != NULL; i++) {
        if (strcmp(custom_commands[i].name, cmd_name) == 0) {
            return &custom_commands[i];
        }
    }
    return NULL;
}

// Handler for SIGCHLD signal - updates statistics when child processes terminate
void sigchld_handler(int sig) {
    int cmd_succeeded;
    pid_t pid;
    int status;

    if (pip_flag) {
        cmd_succeeded = WIFEXITED(left_status) && WEXITSTATUS(left_status) == 0 &&
                        WIFEXITED(right_status) && WEXITSTATUS(right_status) == 0;
    } else {
        cmd_succeeded = WIFEXITED(left_status) && WEXITSTATUS(left_status) == 0;
    }

    if (cmd_succeeded && !background_flag) {
        clock_gettime(CLOCK_MONOTONIC, &end);
        float total_time = time_diff(start, end);

        total_cmd_count += 1;
        last_cmd_time = total_time;
        total_time_all += total_time;
        average_time = total_time_all / total_cmd_count;
        update_min_max_time(total_time, &min_time, &max_time);

        if (current_command[0] != '\0') {
            append_to_log(output_file, current_command, total_time);
        }
    } else {
        // Check for signal termination
        if (pip_flag) {
            if (WIFSIGNALED(left_status)) {
                printf("Terminated by signal: %s\n", strsignal(WTERMSIG(left_status)));
                if (WTERMSIG(left_status) == SIGXFSZ) {
                    printf("File size limit exceeded!\n");
                }
            } else if (!WIFEXITED(left_status) || WEXITSTATUS(left_status) != 0) {
                printf("Process exited with error code: %d\n", WEXITSTATUS(left_status));
            } else if (WIFSIGNALED(right_status)) {
                printf("Terminated by signal: %s\n", strsignal(WTERMSIG(right_status)));
                if (WTERMSIG(right_status) == SIGXFSZ) {
                    printf("File size limit exceeded!\n");
                }
            } else {
                printf("Process exited with error code: %d\n", WEXITSTATUS(right_status));
            }
        } else {
            if (WIFSIGNALED(left_status)) {
                printf("Terminated by signal: %s\n", strsignal(WTERMSIG(left_status)));
                if (WTERMSIG(left_status) == SIGXFSZ) {
                    printf("File size limit exceeded!\n");
                }
            } else {
                printf("Process exited with error code: %d\n", WEXITSTATUS(left_status));
            }
        }

        if (flag_semi_dangerous) {
            semi_dangerous_cmd_count -= 1;
            flag_semi_dangerous = 0;
        }
    }

    // Clean up zombie processes and handle background process success
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (background_flag && pid == left_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            total_cmd_count += 1;
            if (current_command[0] != '\0') {
                append_to_log(output_file, current_command, 0.0);
            }
        }
    }
}

// Redirect stderr to a file
void redirect_stderr_to_file(const char *filename) {
    if (original_stderr_fd == -1) {
        original_stderr_fd = dup(STDERR_FILENO);
    }

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open for stderr redirection");
        return;
    }

    if (dup2(fd, STDERR_FILENO) < 0) {
        perror("dup2 for stderr redirection");
        close(fd);
        return;
    }

    close(fd);
    stderr_redirected = 1;
}

// Restore stderr to its original file descriptor
void restore_stderr(void) {
    if (stderr_redirected && original_stderr_fd != -1) {
        dup2(original_stderr_fd, STDERR_FILENO);
        close(original_stderr_fd);
        original_stderr_fd = -1;
        stderr_redirected = 0;
    }
}

// Check command arguments for 2> redirection
void check_and_redirect_stderr(char **args) {
    if (!args) return;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "2>") == 0 && args[i+1] != NULL) {
            redirect_stderr_to_file(args[i+1]);

            // Remove the redirection operator and filename from arguments
            int j;
            for (j = i; args[j+2] != NULL; j++) {
                args[j] = args[j+2];
            }
            args[j] = NULL; // Properly terminate the array
            break;
        }
    }
}

// Check if input contains the -a (append) flag
void check_append_flag(char **args, int args_len, int *flg) {
    *flg = 0;
    if (args == NULL) return;

    for (int i = 0; i < args_len && args[i]; i++) {
        if (strcmp(args[i], "-a") == 0) {
            *flg = 1;
            break;
        }
    }
}

// Write content to a file, either appending or truncating
void write_to_file(const char *filename, const char *content, int append) {
    int fd;

    if (append) {
        fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    } else {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    if (fd == -1) {
        perror("open failed");
        return;
    }

    if (write(fd, content, strlen(content)) == -1) {
        perror("write failed");
    }

    close(fd);
}

// Get the resource ID for a named resource
int get_resource_type(const char *res_name) {
    if (strcmp(res_name, "cpu") == 0) return RLIMIT_CPU;
    if (strcmp(res_name, "fsize") == 0) return RLIMIT_FSIZE;
    if (strcmp(res_name, "as") == 0 || strcmp(res_name, "mem") == 0) return RLIMIT_AS;
    if (strcmp(res_name, "nofile") == 0) return RLIMIT_NOFILE;
    if (strcmp(res_name, "nproc") == 0) return RLIMIT_NPROC;
    return -1;
}

// Display a resource limit in a human-readable format
void show_resource_limit(const char *name, int resource_type) {
    struct rlimit limit;
    if (getrlimit(resource_type, &limit) != 0) {
        perror("getrlimit");
        return;
    }

    const char* res_name;
    if (resource_type == RLIMIT_CPU) res_name = "CPU time";
    else if (resource_type == RLIMIT_AS) res_name = "Memory";
    else if (resource_type == RLIMIT_FSIZE) res_name = "File size";
    else if (resource_type == RLIMIT_NOFILE) res_name = "Open files";
    else if (resource_type == RLIMIT_NPROC) res_name = "Process count";
    else res_name = name;

    printf("%s: ", res_name);

    // Format soft limit
    if (limit.rlim_cur == RLIM_INFINITY) {
        printf("soft=unlimited, ");
    } else {
        if (resource_type == RLIMIT_CPU) {
            printf("soft=%lus, ", (unsigned long)limit.rlim_cur);
        } else if (resource_type == RLIMIT_AS || resource_type == RLIMIT_FSIZE) {
            if (limit.rlim_cur >= 1024*1024*1024) {
                printf("soft=%.1fG, ", (double)limit.rlim_cur / (1024*1024*1024));
            } else if (limit.rlim_cur >= 1024*1024) {
                printf("soft=%.1fM, ", (double)limit.rlim_cur / (1024*1024));
            } else if (limit.rlim_cur >= 1024) {
                printf("soft=%.1fK, ", (double)limit.rlim_cur / 1024);
            } else {
                printf("soft=%luB, ", (unsigned long)limit.rlim_cur);
            }
        } else {
            printf("soft=%lu, ", (unsigned long)limit.rlim_cur);
        }
    }

    // Format hard limit
    if (limit.rlim_max == RLIM_INFINITY) {
        printf("hard=unlimited\n");
    } else {
        if (resource_type == RLIMIT_CPU) {
            printf("hard=%lus\n", (unsigned long)limit.rlim_max);
        } else if (resource_type == RLIMIT_AS || resource_type == RLIMIT_FSIZE) {
            if (limit.rlim_max >= 1024*1024*1024) {
                printf("hard=%.1fG\n", (double)limit.rlim_max / (1024*1024*1024));
            } else if (limit.rlim_max >= 1024*1024) {
                printf("hard=%.1fM\n", (double)limit.rlim_max / (1024*1024));
            } else if (limit.rlim_max >= 1024) {
                printf("hard=%.1fK\n", (double)limit.rlim_max / 1024);
            } else {
                printf("hard=%luB\n", (unsigned long)limit.rlim_max);
            }
        } else {
            printf("hard=%lu\n", (unsigned long)limit.rlim_max);
        }
    }
}

// Show all resource limits
void show_all_resource_limits(void) {
    show_resource_limit("cpu", RLIMIT_CPU);
    show_resource_limit("mem", RLIMIT_AS);
    show_resource_limit("fsize", RLIMIT_FSIZE);
    show_resource_limit("nofile", RLIMIT_NOFILE);
    show_resource_limit("nproc", RLIMIT_NPROC);
}

// Parse a value with optional unit (B, K/KB, M/MB, G/GB)
unsigned long long parse_value_with_unit(const char *str) {
    char *endptr;
    double value = strtod(str, &endptr);
    while (isspace(*endptr)) endptr++;

    if (*endptr == '\0') return (unsigned long long)value;
    if (strcasecmp(endptr, "B") == 0) return (unsigned long long)(value);
    if (strcasecmp(endptr, "K") == 0 || strcasecmp(endptr, "KB") == 0)
        return (unsigned long long)(value * 1024);
    if (strcasecmp(endptr, "M") == 0 || strcasecmp(endptr, "MB") == 0)
        return (unsigned long long)(value * 1024 * 1024);
    if (strcasecmp(endptr, "G") == 0 || strcasecmp(endptr, "GB") == 0)
        return (unsigned long long)(value * 1024 * 1024 * 1024);

    fprintf(stderr, "Unknown unit: %s\n", endptr);
    return (unsigned long long)value;
}

// Check and apply resource limits specified in command arguments
char **check_rsc_lmt(char **argu, int *args_len) {
    // Basic validation
    if (!argu || !argu[0]) {
        return NULL;
    }

    // Check if this is a rlimit command
    if (strcmp(argu[0], "rlimit") != 0) {
        // Not a rlimit command - make a DEEP COPY of the arguments
        int len = 0;
        while (argu[len]) len++;

        char **new_args = malloc((len + 1) * sizeof(char *));
        if (!new_args) {
            perror("malloc");
            return NULL;
        }

        for (int i = 0; i < len; i++) {
            new_args[i] = strdup(argu[i]);
            if (!new_args[i]) {
                // Clean up if strdup fails
                for (int j = 0; j < i; j++) {
                    free(new_args[j]);
                }
                free(new_args);
                return NULL;
            }
        }
        new_args[len] = NULL;

        if (args_len) *args_len = len;
        return new_args;
    }

    // Handle 'rlimit show' command
    if (argu[1] && strcmp(argu[1], "show") == 0) {
        if (argu[2] == NULL) {
            // Show all resource limits
            show_all_resource_limits();
        } else {
            // Show limit for a specific resource
            int rtype = get_resource_type(argu[2]);
            if (rtype == -1) {
                printf("ERR_RESOURCE: Unknown resource '%s'\n", argu[2]);
            } else {
                show_resource_limit(argu[2], rtype);
            }
        }

        // Return empty command array
        char **empty_cmd = malloc(sizeof(char*));
        if (!empty_cmd) {
            perror("malloc");
            return NULL;
        }
        empty_cmd[0] = NULL;

        if (args_len) *args_len = 0;
        return empty_cmd;
    }

    // Handle 'rlimit set' command
    if (!argu[1] || strcmp(argu[1], "set") != 0) {
        printf("ERR: Unknown rlimit command. Use 'rlimit set' or 'rlimit show'\n");
        return NULL;
    }

    // Process resource limit settings
    int i = 2;
    for (; argu[i]; i++) {
        if (!strchr(argu[i], '=')) break;

        char resource[MAX_INPUT_LENGTH];
        char soft_str[MAX_INPUT_LENGTH], hard_str[MAX_INPUT_LENGTH];

        hard_str[0] = '\0';

        if (sscanf(argu[i], "%[^=]=%[^:]:%s", resource, soft_str, hard_str) < 2) {
            printf("ERR_FORMAT in: %s\n", argu[i]);
            return NULL;
        }

        rlim_t soft = parse_value_with_unit(soft_str);
        rlim_t hard = strlen(hard_str) ? parse_value_with_unit(hard_str) : soft;

        int rtype = get_resource_type(resource);
        if (rtype == -1) {
            printf("ERR_RESOURCE in: %s\n", resource);
            return NULL;
        }

        // Set the resource limit
        struct rlimit lim = { .rlim_cur = soft, .rlim_max = hard };
        if (setrlimit(rtype, &lim) != 0) {
            switch (errno) {
                case EPERM:
                    printf("ERR: Permission denied setting %s limit\n", resource);
                    break;
                case EINVAL:
                    printf("ERR: Invalid value for %s limit\n", resource);
                    break;
                default:
                    perror("setrlimit");
            }
            return NULL;
        }
    }

    // Count remaining arguments for new array
    int remaining = 0;
    for (int j = i; argu[j]; j++) {
        remaining++;
    }

    // Create a new array with DEEP COPIES of the remaining arguments
    char **new_args = malloc((remaining + 1) * sizeof(char *));
    if (!new_args) {
        perror("malloc");
        return NULL;
    }

    int j = 0;
    for (; argu[i]; i++, j++) {
        new_args[j] = strdup(argu[i]);
        if (!new_args[j]) {
            for (int k = 0; k < j; k++) {
                free(new_args[k]);
            }
            free(new_args);
            return NULL;
        }
    }
    new_args[j] = NULL;

    if (args_len) *args_len = remaining;
    return new_args;
}


// Implementation of my_tee command handler
int my_tee_handler(void) {
    close(pipefd[1]);  // Close write end (only reading)

    int bytes;
    char buffer[MAX_INPUT_LENGTHH];
    char result[MAX_INPUT_LENGTHH];
    result[0] = '\0';

    // Read all data from pipe
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        strcat(result, buffer);
    }

    close(pipefd[0]);

    // Output to stdout
    write(STDOUT_FILENO, result, strlen(result));

    // Write to all specified files (skip the first arg which is the command name)
    for (int i = 1; i < r_args_len; i++) {
        write_to_file(r_args[i], result, append_flg);
    }

    return 0;
}
// Gets user input using a static buffer
void get_string(char* buffer, size_t buffer_size) {
    buffer[0] = '\0';
    size_t length = 0;
    int c;

    while ((c = getchar()) != EOF && c != '\n') {
        if (length < buffer_size - 1) {
            buffer[length++] = (char)c;
        } else {
            while ((c = getchar()) != EOF && c != '\n');
            printf("ERR\n");
            buffer[0] = '\0';
            return;
        }
    }

    buffer[length] = '\0';

    if (length > MAX_INPUT_LENGTH) {
        printf("ERR\n");
        buffer[0] = '\0';
    }
}

// Split string into an array of arguments (argv style)
// Split string into an array of arguments (argv style)
char **split_to_args(const char *string, const char *delimiter, int *count) {
    // Handle empty string case first
    if (!string || string[0] == '\0') {
        *count = 0;
        return NULL;
    }

    // Make a copy to avoid modifying the original string
    char *input_copy = strdup(string);
    if (!input_copy) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }

    char **argf = NULL;
    *count = 0;

    // Tokenize the string
    char *token = strtok(input_copy, delimiter);
    while (token != NULL) {
        // Trim whitespace from the token
        char *trimmed_token = trim_inplace(token);
        if (strlen(trimmed_token) == 0) {
            token = strtok(NULL, delimiter);
            continue; // Skip empty tokens after trimming
        }

        // Expand the arguments array
        char **temp = realloc(argf, (*count + 2) * sizeof(char *));
        if (!temp) {
            fprintf(stderr, "Memory allocation failed!\n");
            free_args(argf);
            free(input_copy);
            exit(1);
        }
        argf = temp;

        // Store a copy of the trimmed token
        argf[*count] = strdup(trimmed_token);
        if (!argf[*count]) {
            fprintf(stderr, "Memory allocation failed!\n");
            free(input_copy);
            free_args(argf);
            exit(1);
        }

        (*count)++;
        token = strtok(NULL, delimiter);
    }

    // Null-terminate the array if any tokens were added
    if (argf != NULL) {
        argf[*count] = NULL;
    } else {
        // No valid tokens, ensure count is 0
        *count = 0;
    }

    free(input_copy);
    return argf;
}

// Check for multiple consecutive spaces in a string
int checkMultipleSpaces(const char* input) {
    int i = 0;
    int prevWasSpace = 0;
    int onlySpaces = 1;

    while (input[i] != '\0') {
        if (input[i] != ' ' && input[i] != '\n' && input[i] != '\t') {
            onlySpaces = 0;
        }

        if (input[i] == ' ') {
            if (prevWasSpace && !onlySpaces) {
                if (strncmp(input, "mcalc ", 6) == 0){
                    fprintf(stderr,"ERR_MAT_INPUT\n");
                    return 1; // Error for multiple spaces in mcalc command
                }
                else {
                fprintf(stderr,"ERR_SPACE\n");
                return 1;
            }}

            prevWasSpace = 1;
        } else {
            prevWasSpace = 0;
        }

        i++;
    }

    return 0;
}

// Free memory allocated for arguments array
void free_args(char **args) {
    if (args != NULL) {
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
            args[i] = NULL;
        }
        free(args);
    }
}

// Trims leading and trailing whitespace from a string in-place
char* trim_inplace(char* str) {
    if (str == NULL) {
        return NULL;
    }

    if (*str == '\0') {
        return str;
    }

    char* start = str;
    while (*start && (*start == ' ' || *start == '\t')) {
        start++;
    }

    if (*start == '\0') {
        *str = '\0';
        return str;
    }

    char* end = str + strlen(str) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) {
        end--;
    }

    end[1] = '\0';

    if (start != str) {
        size_t len = (end - start) + 1;
        memmove(str, start, len + 1);
    }

    return str;
}

// Split input string into left and right commands based on pipe symbol
int pipe_split(char *input, char *left_cmd, char *right_cmd) {
    char *input_copy = strdup(input);
    if (!input_copy) {
        return -1; // Memory allocation failed
    }

    char *token = strtok(input_copy, "|");
    int result;

    if (token) {
        strcpy(left_cmd, token);

        token = strtok(NULL, "|");
        if (token) {
            strcpy(right_cmd, token);
            result = 1; // pipe exists
        } else {
            right_cmd[0] = '\0';
            result = 0; // no second command after pipe
        }
    } else {
        strcpy(left_cmd, input);
        right_cmd[0] = '\0';
        result = 0; // no pipe at all
    }

    free(input_copy);
    return result;
}

// Utility to strip trailing \r or \n
void strip_crlf(char *str) {
    if (str == NULL) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[len - 1] = '\0';
        len--;
    }
}
// Read lines from a file into a string array
char** read_file_lines(const char* filename, int* num_lines) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file for reading");
        *num_lines = 0;
        return NULL;
    }

    int capacity = 64;
    char** lines = safe_malloc(capacity * sizeof(char*));

    char buffer[MAX_INPUT_LENGTH];
    int count = 0;

    while (fgets(buffer, sizeof(buffer), file)) {
        if (count >= capacity - 1) {
            capacity *= 2;
            char** new_lines = realloc(lines, capacity * sizeof(char*));
            if (!new_lines) {
                fprintf(stderr, "Memory allocation failed!\n");
                for (int i = 0; i < count; i++) {
                    free(lines[i]);
                }
                free(lines);
                fclose(file);
                *num_lines = 0;
                return NULL;
            }
            lines = new_lines;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        trim_inplace(buffer);

        if (buffer[0] == '\0') {
            continue;
        }

        lines[count] = strdup(buffer);
        if (!lines[count]) {
            fprintf(stderr, "Memory allocation failed!\n");
            for (int i = 0; i < count; i++) {
                free(lines[i]);
            }
            free(lines);
            fclose(file);
            *num_lines = 0;
            return NULL;
        }

        count++;
    }

    lines[count] = NULL;
    fclose(file);
    *num_lines = count;

    if (count > 0 && count < capacity - 1) {
        char** new_lines = realloc(lines, (count + 1) * sizeof(char*));
        if (new_lines) {
            lines = new_lines;
        }
    }

    return lines;
}

// Check if a command is in the list of dangerous commands
int is_dangerous_command(char **user_args, int user_args_len) {
    if (user_args == NULL || user_args_len == 0) {
        return 0;
    }

    int is_exact_match = 0;
    int is_semi_dangerous = 0;
    char *similar_command = NULL;

    for (int i = 0; i < numLines; i++) {
        int temp_count = 0;
        char **dangerous_args = split_to_args(Danger_CMD[i], delim, &temp_count);
        if (dangerous_args == NULL) continue;

        // Sanitize each token
        for (int k = 0; k < temp_count; k++) {
            strip_crlf(dangerous_args[k]);
        }

        for (int k = 0; k < user_args_len; k++) {
            strip_crlf(user_args[k]);
        }

        // Check if the command name matches
        if (strcmp(user_args[0], dangerous_args[0]) == 0) {
            // Check for full exact match
            if (user_args_len == temp_count) {
                is_exact_match = 1;
                for (int j = 0; j < user_args_len; j++) {
                    if (strcmp(user_args[j], dangerous_args[j]) != 0) {
                        is_exact_match = 0;
                        break;
                    }
                }
            }

            if (is_exact_match) {
                fprintf(stderr,"ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", Danger_CMD[i]);
                fflush(stdout);
                dangerous_cmd_blocked_count++;
                free_args(dangerous_args);
                return 1; // BLOCK execution
            }

            // Not exact, but same base command = semi-dangerous
            is_semi_dangerous = 1;
            similar_command = Danger_CMD[i];
        }

        free_args(dangerous_args);
    }

    if (is_semi_dangerous && similar_command) {
        fprintf(stderr,"WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", similar_command);
        fflush(stdout);
        semi_dangerous_cmd_count++;
        flag_semi_dangerous = 1;
    }

    return 0; // ALLOW execution
}

// Calculate time difference between two timespec structs
float time_diff(struct timespec start, struct timespec end) {
    long sec_diff = end.tv_sec - start.tv_sec;
    long nsec_diff = end.tv_nsec - start.tv_nsec;

    if (nsec_diff < 0) {
        nsec_diff += 1000000000;
        sec_diff -= 1;
    }

    float total_time = (float)((double)sec_diff + (double)nsec_diff / 1000000000.0);
    return total_time;
}

// Append command and execution time to log file
void append_to_log(const char *filename, char* val1, float val2) {
    FILE *file = fopen(filename, "a");
    if (!file) {
        perror("Error opening log file");
        return;
    }

    fprintf(file, "%s : %.5f sec\n", val1, val2);
    fclose(file);
}

// Display the shell prompt with current statistics
void prompt(void) {
    printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
           total_cmd_count,
           dangerous_cmd_blocked_count,
           last_cmd_time,
           average_time,
           min_time,
           max_time);
    fflush(stdout);
}

// Update minimum and maximum execution times
void update_min_max_time(double current_time, double *min_time, double *max_time) {
    if (*min_time <= 0 || current_time < *min_time) {
        *min_time = current_time;
    }

    if (current_time > *max_time) {
        *max_time = current_time;
    }
}

// Main function - Shell implementation
int main(int argc, char* argv[]) {
    // Validate command line arguments
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <dangerous_commands_file> <log_file>\n", argv[0]);
        exit(1);
    }
    current_command[0] = '\0';

    // Setup file paths
    output_file = argv[2];
    const char *input_file = argv[1];
    char left_cmd[MAX_INPUT_LENGTH];
    char right_cmd[MAX_INPUT_LENGTH];
    pid_t right_pid = 0;

    // Load dangerous commands list
    Danger_CMD = read_file_lines(input_file, &numLines);
    if (Danger_CMD == NULL) {
        fprintf(stderr, "Failed to load dangerous commands\n");
        exit(1);
    }

    // Clear the log file
    {
        FILE *clear = fopen(argv[2], "w");
        if (clear) fclose(clear);
    }

    // Set up signal handlers
    signal(SIGCHLD, sigchld_handler);
    signal(SIGXCPU, sigxcpu_handler);
    signal(SIGXFSZ, sigxfsz_handler);

    // Main command processing loop
    while (1) {
        // Reset state for new command
        l_args = NULL;
        r_args = NULL;
        pip_flag = 0;

        prompt();

        // Get user input
        get_string(userInput, sizeof(userInput));
        clock_gettime(CLOCK_MONOTONIC, &start);

        // Skip empty input
        if (userInput[0] == '\0') {
            continue;
        }
        strcpy(current_command, userInput);

        // Clean up input
        trim_inplace(userInput);

        // Check for multiple spaces
        int spaceCheck = checkMultipleSpaces(userInput);
        if (spaceCheck == 1) {
            continue;
        }

        // Split input for pipe
        pip_flag = pipe_split(userInput, left_cmd, right_cmd);
        trim_inplace(left_cmd);
        trim_inplace(right_cmd);
        //check if the command is mcalc
        if (strncmp(left_cmd, "mcalc ", 6) == 0){
            mcalc_handler(left_cmd);

            continue;
        }
        // Split into arguments
        l_args = split_to_args(left_cmd, delim, &l_args_len);
        r_args = split_to_args(right_cmd, delim, &r_args_len);

        // Validate arguments
        if (l_args == NULL || (r_args == NULL && pip_flag)) {
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            continue;
        }

        // Handle exit command
        if (l_args_len > 0 && l_args[0] && strcmp(l_args[0], "done") == 0) {
            free_args(l_args);
            free_args(r_args);
            free_args(Danger_CMD);
            printf("%d\n", dangerous_cmd_blocked_count + semi_dangerous_cmd_count);
            return 0;
        }

        // Handle resource limits
        if (l_args_len > 0 && l_args[0] && strcmp(l_args[0], "rlimit") == 0) {
            char **new_cmd = check_rsc_lmt(l_args, &l_args_len);
            if (new_cmd != NULL) {
                free_args(l_args);
                l_args = new_cmd;
            } else {
                free_args(l_args);
                free_args(r_args);
                l_args = NULL;
                r_args = NULL;
                continue;
            }
        }

        if (pip_flag && r_args_len > 0 && r_args[0] && strcmp(r_args[0], "rlimit") == 0) {
            char **new_cmd = check_rsc_lmt(r_args, &r_args_len);
            if (new_cmd != NULL) {
                free_args(r_args);
                r_args = new_cmd;
            } else {
                free_args(l_args);
                free_args(r_args);
                l_args = NULL;
                r_args = NULL;
                continue;
            }
        }

        // Check argument count
        if (l_args_len > MAX_ARGC || r_args_len > MAX_ARGC) {
            printf("ERR_ARGS\n");
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            continue;
        }

        // Security check
        if (is_dangerous_command(l_args, l_args_len)) {
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            continue;
        }

        if (r_args && is_dangerous_command(r_args, r_args_len)) {
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            continue;
        }

        // Check for background execution
        // Check if command has background flag
        if (l_args_len > 0 && l_args[l_args_len - 1] && strcmp(l_args[l_args_len - 1], "&") == 0) {
            background_flag = 1;
            free(l_args[l_args_len - 1]);  // Free the "&" string
            l_args[l_args_len - 1] = NULL; // Remove "&"
            l_args_len--;                  // Decrease arg count
        }

        // Create pipe
        if (pipe(pipefd) == -1) {
            perror("pipe creation failed");
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            continue;
        }

        // Execute left command
        left_pid = fork();
        if (left_pid < 0) {
            if (errno == EAGAIN) {
                fprintf(stderr, "Process creation limit exceeded!\n");
            } else {
                perror("Fork Failed");
            }
            free_args(l_args);
            free_args(r_args);
            l_args = NULL;
            r_args = NULL;
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (left_pid == 0) {
            // Child process for left command
            check_and_redirect_stderr(l_args);

            // Set up signal handlers
            signal(SIGXCPU, sigxcpu_handler);
            signal(SIGXFSZ, sigxfsz_handler);

            char **cmd = check_rsc_lmt(l_args, &l_args_len);
            if (cmd != NULL) {
                l_args = cmd;
            }

            if (pip_flag == 0) {
                // No pipe, just execute the command
                close(pipefd[0]);
                close(pipefd[1]);
                handle_execvp_errors_in_child(l_args);
            }

            // Set up pipe for output redirection
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                if (errno == EMFILE) {
                    fprintf(stderr, "Too many open files!\n");
                    exit(1);
                }
                perror("dup2");
                exit(1);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            handle_execvp_errors_in_child(l_args);
        }

        // Execute right command if pipe exists
        if (pip_flag && r_args) {
            check_append_flag(r_args, r_args_len, &append_flg);

            // Check for custom command
            if (r_args[0]) {
                const CustomCommand* cmd = find_custom_command(r_args[0]);

                if (cmd != NULL) {
                    // It's a custom command
                    if (r_args_len - 1 < cmd->min_args) {
                        printf("ERR: Not enough arguments for %s\n", cmd->name);
                    } else {
                        // Execute the custom command handler
                        cmd->handler();
                    }
                } else {
                    // Standard pipe to external command
                    right_pid = fork();
                    if (right_pid < 0) {
                        if (errno == EAGAIN) {
                            fprintf(stderr, "Process creation limit exceeded!\n");
                        } else {
                            perror("Fork Failed");
                        }
                        free_args(l_args);
                        free_args(r_args);
                        l_args = NULL;
                        r_args = NULL;
                        close(pipefd[0]);
                        close(pipefd[1]);
                        continue;
                    }

                    if (right_pid == 0) {
                        signal(SIGCHLD, sigchld_handler);

                        char **cmd2 = check_rsc_lmt(r_args, &r_args_len);
                        if (cmd2 != NULL) {
                            r_args = cmd2;
                        }

                        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
                            if (errno == EMFILE) {
                                fprintf(stderr, "Too many open files!\n");
                                exit(1);
                            }
                            perror("dup2");
                            exit(1);
                        }

                        close(pipefd[1]);
                        close(pipefd[0]);
                        handle_execvp_errors_in_child(r_args);
                    }
                }
            }
        }

        // Parent process: Close pipe ends
        close(pipefd[0]);
        close(pipefd[1]);

        // Wait for child processes to complete
        if (pip_flag) {
            waitpid(left_pid, &left_status, 0);
            if (right_pid > 0) {
                waitpid(right_pid, &right_status, 0);
            }
        } else {
            if (!background_flag)
                waitpid(left_pid, &left_status, 0);

            background_flag = 0; // Reset background flag
        }

        // Clean up argument arrays
        free_args(l_args);
        free_args(r_args);
        l_args = NULL;
        r_args = NULL;
    }
}



/// EX3 SOLUITONS STUFF


Matrix matrix;
int matrix_num=0;
char *opration ;



int is_uppercase(const char* str) {
    for (; *str; str++) {
        if (!isupper(*str)) return 0;
    }
    return 1;
}

int parse_matrix(const char* token, Matrix* matrix) {
    // Format: (R,C:a1,a2,...,aR*C)
    int r, c;
    const char* ptr = token;
    // Check for spaces in the input (new validation)
    for (const char* p = token; *p; p++) {
        if (*p == ' ') {
            return 0; // Reject matrices with spaces
        }
    }
    if (*ptr != '(') return 0;
    ptr++;
    if (sscanf(ptr, "%d,%d", &r, &c) != 2) return 0;

    // Move ptr to after 'R,C'
    while (*ptr && *ptr != ':') ptr++;
    if (*ptr != ':') return 0;
    ptr++;

    // Count expected number of elements
    int expected = r * c;
    int* data = malloc(sizeof(int) * expected);
    if (!data) return 0;

    // Parse all elements separated by ','
    char* endptr;
    for (int i = 0; i < expected; i++) {
        data[i] = (int)strtol(ptr, &endptr, 10);
        if (ptr == endptr) {
            free(data);
            return 0; // parse error or missing number
        }
        ptr = endptr;
        if (i < expected - 1) {
            if (*ptr != ',') {
                free(data);
                return 0; // missing comma
            }
            ptr++;
        }
    }

    // After last element, expect ')'
    if (*ptr != ')') {
        free(data);
        return 0;
    }

    matrix->rows = r;
    matrix->cols = c;
    matrix->data = data;
    return 1;
}

void free_matrices(Matrix* matrices, int count) {
    for (int i = 0; i < count; i++) {
        free(matrices[i].data);
    }
}

int check_same_dimensions(Matrix* matrices, int count) {
    if (count < 1) return 1;

    int base_rows = matrices[0].rows;
    int base_cols = matrices[0].cols;

    for (int i = 1; i < count; i++) {
        if (matrices[i].rows != base_rows || matrices[i].cols != base_cols) {
            printf("Error: Matrix #%d dimensions (%d,%d) differ from Matrix #1 (%d,%d)\n",
                   i+1, matrices[i].rows, matrices[i].cols, base_rows, base_cols);
            return 0;
        }
    }
    return 1;
}

int parse_input(const char* input, Matrix* matrices, int* matrix_count, char* operation_out) {
    if (strncmp(input, "mcalc ", 6) != 0) {
        ///printf("Error: Input must start with 'mcalc'\n");
        return 0;
    }

    const char* ptr = input + 6; // skip "mcalc "
    char tokens[MAX_MATRICES + 1][MAX_INPUT_LENGTH];
    int token_index = 0;

    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (*ptr != '"') {
            //printf("Error: Expected '\"' at token #%d\n", token_index + 1);
            return 0;
        }
        ptr++; // skip opening quote

        const char* end_quote = strchr(ptr, '"');
        if (!end_quote) {
            //printf("Error: Missing closing '\"' at token #%d\n", token_index + 1);
            return 0;
        }

        int len = end_quote - ptr;
        if (len <= 0) {
           // printf("Error: Empty token at #%d\n", token_index + 1);
            return 0;
        }
        if (len >= sizeof(tokens[token_index])) {
           // printf("Error: Token too long at #%d\n", token_index + 1);
            return 0;
        }

        strncpy(tokens[token_index], ptr, len);
        tokens[token_index][len] = '\0';

        token_index++;
        if (token_index > MAX_MATRICES + 1) {
           // printf("Error: Too many tokens\n");
            return 0;
        }

        ptr = end_quote + 1;
    }

    if (token_index < 3) {
       // printf("Error: Must provide at least two matrices and one operation\n");
        return 0;
    }

    char* operation = tokens[token_index - 1];
    if (!is_uppercase(operation) || (strcmp(operation, "ADD") != 0 && strcmp(operation, "SUB") != 0)) {
       // printf("Error: Invalid operation '%s'\n", operation);
        return 0;
    }
    strcpy(operation_out, operation);

    int matrices_count = token_index - 1;

    for (int i = 0; i < matrices_count; i++) {
        if (!parse_matrix(tokens[i], &matrices[i])) {
            //printf("Error: Invalid matrix format at #%d\n", i + 1);
            for (int j = 0; j < i; j++) free(matrices[j].data);
            return 0;
        }
    }

    if (!check_same_dimensions(matrices, matrices_count)) {
        for (int i = 0; i < matrices_count; i++) free(matrices[i].data);
        return 0;
    }

    *matrix_count = matrices_count;
    return 1;
}

void mcalc_handler(char *input) {
    // Allocate memory for matrices and operation
    Matrix matrices[MAX_MATRICES];
    char operation[16];
    int matrix_count = 0;

    matrix_stats.operation_count++;

    // Parse the input
    if (!parse_input(input, matrices, &matrix_count, operation)) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        matrix_stats.error_count++;
        return;
    }

    matrix_stats.total_matrices_processed += matrix_count;

    // Update max matrix size if needed
    int matrix_size = matrices[0].rows * matrices[0].cols;
    if (matrix_size > matrix_stats.max_matrix_size) {
        matrix_stats.max_matrix_size = matrix_size;
    }

    // Update operation statistics
    if (strcmp(operation, "ADD") == 0) {
        matrix_stats.add_operations++;
    } else if (strcmp(operation, "SUB") == 0) {
        matrix_stats.sub_operations++;
    }

    // Use hierarchical calculation with threads
    Matrix result = hierarchical_matrix_calculation(matrices, matrix_count, operation);

    // Check if calculation succeeded
    if (!result.data) {
        fprintf(stderr, "Matrix calculation failed\n");
        matrix_stats.error_count++;
        free_matrices(matrices, matrix_count);
        return;
    }

    // Print result in format (rows,cols:val1,val2,...)
    printf("(");
    printf("%d,%d:", result.rows, result.cols);
    for (int i = 0; i < result.rows * result.cols; i++) {
        printf("%d", result.data[i]);
        if (i < result.rows * result.cols - 1)
            printf(",");
    }
    printf(")\n");

    // Log the operation
    log_matrix_operation(matrices, matrix_count, operation, 1);

    // Clean up
    free(result.data);
    free_matrices(matrices, matrix_count);
}
typedef struct {
    Matrix* matrix1;
    Matrix* matrix2;
    Matrix* result;
    char operation[16];
} ThreadData;

// Thread function for matrix operations
void* matrix_thread_operation(void* arg) {
    ThreadData* data = (ThreadData*)arg;

    // Allocate memory for result matrix
    data->result->rows = data->matrix1->rows;
    data->result->cols = data->matrix1->cols;
    data->result->data = malloc(sizeof(int) * data->result->rows * data->result->cols);

    if (!data->result->data) {
        fprintf(stderr, "Memory allocation failed in thread\n");
        pthread_exit(NULL);
    }

    // Copy first matrix to result
    for (int i = 0; i < data->result->rows * data->result->cols; i++) {
        data->result->data[i] = data->matrix1->data[i];
    }

    // Perform operation based on operation type
    if (strcmp(data->operation, "ADD") == 0) {
        for (int i = 0; i < data->result->rows * data->result->cols; i++) {
            data->result->data[i] += data->matrix2->data[i];
        }
    } else if (strcmp(data->operation, "SUB") == 0) {
        for (int i = 0; i < data->result->rows * data->result->cols; i++) {
            data->result->data[i] -= data->matrix2->data[i];
        }
    }

    pthread_exit(NULL);
}

// Function to create a deep copy of a matrix
Matrix copy_matrix(Matrix* original) {
    Matrix copy;
    copy.rows = original->rows;
    copy.cols = original->cols;
    copy.data = malloc(sizeof(int) * copy.rows * copy.cols);

    if (!copy.data) {
        fprintf(stderr, "Memory allocation failed\n");
        copy.rows = 0;
        copy.cols = 0;
        return copy;
    }

    for (int i = 0; i < copy.rows * copy.cols; i++) {
        copy.data[i] = original->data[i];
    }

    return copy;
}

// Function to perform hierarchical matrix calculation
Matrix hierarchical_matrix_calculation(Matrix* matrices, int matrix_count, char* operation) {
    if (matrix_count == 0) {
        fprintf(stderr, "No matrices to process\n");
        Matrix empty = {0, 0, NULL};
        return empty;
    }

    if (matrix_count == 1) {
        // Only one matrix, create a deep copy and return it
        return copy_matrix(&matrices[0]);
    }

    // Make deep copies of input matrices to avoid modifying originals
    Matrix* working_matrices = malloc(sizeof(Matrix) * matrix_count);
    if (!working_matrices) {
        fprintf(stderr, "Memory allocation failed\n");
        Matrix empty = {0, 0, NULL};
        return empty;
    }

    for (int i = 0; i < matrix_count; i++) {
        working_matrices[i] = copy_matrix(&matrices[i]);
        if (!working_matrices[i].data) {
            // Clean up previously allocated memory
            for (int j = 0; j < i; j++) {
                free(working_matrices[j].data);
            }
            free(working_matrices);
            Matrix empty = {0, 0, NULL};
            return empty;
        }
    }

    // Start hierarchical processing
    int current_count = matrix_count;

    while (current_count > 1) {
        int pairs = current_count / 2;
        int next_count = pairs + (current_count % 2);
        Matrix* next_level = malloc(sizeof(Matrix) * next_count);

        if (!next_level) {
            // Clean up
            for (int i = 0; i < current_count; i++) {
                free(working_matrices[i].data);
            }
            free(working_matrices);
            Matrix empty = {0, 0, NULL};
            return empty;
        }

        // Allocate thread data and thread objects
        ThreadData* thread_data = malloc(sizeof(ThreadData) * pairs);
        pthread_t* threads = malloc(sizeof(pthread_t) * pairs);

        if (!thread_data || !threads) {
            // Clean up
            for (int i = 0; i < current_count; i++) {
                free(working_matrices[i].data);
            }
            free(working_matrices);
            free(next_level);
            if (thread_data) free(thread_data);
            if (threads) free(threads);
            Matrix empty = {0, 0, NULL};
            return empty;
        }

        // Create and start threads for each pair
        for (int i = 0; i < pairs; i++) {
            thread_data[i].matrix1 = &working_matrices[i*2];
            thread_data[i].matrix2 = &working_matrices[i*2 + 1];
            thread_data[i].result = &next_level[i];
            strcpy(thread_data[i].operation, operation);

            if (pthread_create(&threads[i], NULL, matrix_thread_operation, &thread_data[i]) != 0) {
                fprintf(stderr, "Failed to create thread\n");
                // Clean up
                for (int j = 0; j < i; j++) {
                    pthread_join(threads[j], NULL);
                    free(next_level[j].data);
                }
                for (int j = 0; j < current_count; j++) {
                    free(working_matrices[j].data);
                }
                free(working_matrices);
                free(next_level);
                free(thread_data);
                free(threads);
                Matrix empty = {0, 0, NULL};
                return empty;
            }
        }

        // If odd number of matrices, move the last one to next level
        if (current_count % 2 == 1) {
            next_level[next_count-1] = working_matrices[current_count-1];
            // Prevent this memory from being freed later
            working_matrices[current_count-1].data = NULL;
        }

        // Wait for all threads to complete
        for (int i = 0; i < pairs; i++) {
            pthread_join(threads[i], NULL);
        }

        // Clean up memory from current level
        for (int i = 0; i < current_count; i++) {
            if (working_matrices[i].data) {
                free(working_matrices[i].data);
            }
        }
        free(working_matrices);

        // Move to next level
        working_matrices = next_level;
        current_count = next_count;

        // Free thread resources
        free(thread_data);
        free(threads);
    }

    // At this point, working_matrices has only one matrix - the final result
    Matrix result = working_matrices[0];
    free(working_matrices); // Just free the array, not the data inside

    return result;
}
