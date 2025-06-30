Mini Shell Implementation (v4)

OVERVIEW
========
This project implements a simple shell (command-line interface) in C under Linux. The shell reads user commands, executes them using system calls, and provides additional features including command timing, dangerous command detection, detailed statistics tracking, piping, background processes, error redirection, resource limits, parallel matrix calculations, and virtual memory simulation.

FEATURES
========

Core Features from v1
----------------------

1. Basic Command Execution
    - Reads and executes system commands using fork() and execvp()
    - Supports command input of up to 1024 characters
    - Handles up to 6 command-line arguments
    - Terminates on the "done" command with statistics output
    - Provides error handling for failed commands with ERR_CMD message

2. Informative Prompt
   The shell displays a detailed prompt with the following information:
   #cmd:<count>|#dangerous_cmd_blocked:<count>|last_cmd_time:<time>|avg_time:<time>|min_time:<time>|max_time:<time>>

   Where:
    - #cmd - Number of successfully executed commands
    - #dangerous_cmd_blocked - Number of dangerous commands that were blocked
    - last_cmd_time - Execution time of the last successful command (in seconds)
    - avg_time - Average execution time of all successful commands
    - min_time - Minimum command execution time observed
    - max_time - Maximum command execution time observed

3. Command Timing
    - Measures execution time for each command using clock_gettime(CLOCK_MONOTONIC)
    - Logs detailed timing data to a specified output file in format: <command> : <time> sec
    - Uses a high-precision timer with results in 5 decimal places

4. Dangerous Commands Protection
    - Reads potential dangerous commands from a specified input file
    - Blocks execution of exact matches to dangerous commands with message:
      "ERR: Dangerous command detected ("<command>"). Execution prevented."
    - Warns about similar commands (same command name but different arguments) with:
      "WARNING: Command similar to dangerous command ("<command>"). Proceed with caution."
    - Tracks statistics on blocked and similar commands

New Features in v2
-------------------

1. Pipe Mechanism (|)
    - Supports piping output from one command to another using the | character
    - Requires exactly one space before and after the pipe character
    - Supports single pipe operations (command1 | command2)

2. Internal my_tee Command
    - Custom implementation of the tee command as an internal shell command
    - Reads from standard input and writes to both standard output and specified files
    - Supports the -a (append) option to add content to the end of files rather than overwriting
    - Can write to multiple files simultaneously
    - Example usage:
      command | my_tee file.txt          # Write output to screen and file.txt
      command | my_tee -a file.txt       # Append output to file.txt
      command | my_tee file1.txt file2.txt  # Write to multiple files

3. Resource Limit Mechanism
    - Implements a resource limitation system via the internal rlimit command
    - Allows setting and displaying resource limits for processes
    - Supports four types of resources:
        - cpu: CPU time in seconds
        - mem: Memory size in B, KB, MB, GB
        - fsize: Maximum file size
        - nofile: Maximum number of open files
    - Syntax:
        - rlimit set resource=soft_value[:hard_value] command [args...]
        - rlimit show [resource]
    - Example usage:
      rlimit set cpu=2:3 sleep 10           # Limit CPU time to 2s (soft) and 3s (hard)
      rlimit set mem=50M ./memory_program   # Limit memory to 50MB
      rlimit show                           # Show all current resource limits
      rlimit show cpu                       # Show only CPU resource limits

4. Background Process Support (&)
    - Executes commands in the background when followed by &
    - Returns the prompt immediately, allowing the user to execute other commands while the background process runs
    - Example usage:
      sleep 5 &    # Runs sleep in the background

5. Error Output Redirection (2>)
    - Redirects standard error output to a file using 2>
    - Example usage:
      command 2> error.log    # Redirects error messages to error.log

6. Enhanced Error Handling
    - Comprehensive error detection and reporting based on process exit status
    - Distinguishes between normal termination and signal-based termination
    - Reports specific error signals when processes are terminated abnormally
    - Error messages for different termination scenarios:
        - Normal termination with non-zero exit code
        - Termination due to signals (with signal name displayed)

New Features in v3
-------------------

Matrix Calculator (mcalc)
- Internal command that performs parallel matrix operations
- Supports addition (ADD) and subtraction (SUB) of matrices
- Uses parallel computation with pthreads arranged in a hierarchical tree structure
- Syntax:
  mcalc "matrix1" "matrix2" ... "matrixN" "OPERATION"
- Each matrix is specified in format "(N,N:a1,a2,...,aN²)" where:
    - N,N: Dimensions of the matrix (rows, columns)
    - a1,a2,...: Comma-separated matrix elements
- Operations:
    - ADD: Performs addition of all matrices
    - SUB: Performs subtraction in the order given (order matters)
- Example usage:
  mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"  # Results in (2,2:6,8,10,12)
  mcalc "(2,2:9,8,7,6)" "(2,2:1,2,3,4)" "SUB"  # Results in (2,2:8,6,4,2)
  mcalc "(2,2:1,1,1,1)" "(2,2:2,2,2,2)" "(2,2:3,3,3,3)" "ADD"  # Results in (2,2:6,6,6,6)
- Input validation:
    - Matrices must follow the exact format specified
    - At least two matrices must be provided
    - All matrices must have the same dimensions
    - Operation must be either "ADD" or "SUB" in uppercase
    - Invalid input format results in ERR_MAT_INPUT error

Parallel Computation Model
- Calculations are performed using a hierarchical tree of pthread workers
- For N matrices, the computation forms a tree where:
    - Each level combines pairs of matrices in parallel
    - If there's an odd number of matrices at any level, the last one passes through to the next level
    - Processing continues until a single result matrix remains
- For example, with 4 matrices and SUB operation:
  Final result = ((M1 - M2) - (M3 - M4))
- The tree structure maintains the order of operations, which is critical for subtraction

New Features in v4
-------------------

Virtual Memory Simulation System
- Complete virtual memory management simulation with paging and swapping
- Simulates a virtual memory system with configurable memory sizes, page sizes, and swap space
- Implements demand paging with LRU (Least Recently Used) page replacement algorithm
- Supports different memory segments: TEXT (read-only), DATA, BSS, and HEAP/STACK
- Provides comprehensive visualization of memory state through various print commands

Key Components:

1. Memory Segments
    - TEXT: Read-only executable code segment (P=1)
    - DATA: Initialized data segment loaded from executable file
    - BSS: Uninitialized data segment (zero-filled)
    - HEAP/STACK: Dynamic memory regions (zero-filled)

2. Page Table Management
    - Each page has flags: Valid (V), Dirty (D), Permission (P)
    - V=1: Page is in physical memory, V=0: Page is not in memory
    - D=1: Page has been modified, D=0: Page is clean
    - P=1: Read-only (TEXT), P=0: Read/write (DATA/BSS/HEAP/STACK)

3. Memory Operations
    - load <address>: Read a byte from virtual address
    - store <address> <value>: Write a byte to virtual address
    - Automatic page fault handling when accessing non-resident pages
    - Permission checking for write operations to read-only segments

4. Page Replacement Algorithm
    - LRU (Least Recently Used) eviction policy
    - Timestamp-based tracking of frame usage
    - Automatic eviction of least recently used pages when memory is full
    - Dirty pages are written to swap file before eviction

5. Swap File Management
    - First-fit allocation for swap space
    - Automatic cleanup of swap blocks when pages return to memory
    - Support for loading pages back from swap file

6. Visualization Commands
    - print table: Display complete page table with all flags and locations
    - print ram: Show contents of physical memory (RAM) in hex and ASCII
    - print swap: Display contents of swap file in hex and ASCII

Script File Format:
The simulation uses a script file with the following format:
```
<exe_file> <swap_file> <text_size> <data_size> <bss_size> <heap_stack_size> <page_size> <num_pages> <memory_size> <swap_size>
load <address>
store <address> <value>
print table
print ram
print swap
```

Where:
- exe_file: Path to executable file containing program data
- swap_file: Path to swap file (created automatically)
- text_size, data_size, bss_size, heap_stack_size: Sizes of memory segments in bytes
- page_size: Size of each page in bytes (must be power of 2)
- num_pages: Total number of virtual pages
- memory_size: Size of physical memory (RAM) in bytes
- swap_size: Size of swap file in bytes

Example Script:
```
program.exe swap.dat 1024 512 256 1024 256 16 1024 2048
load 100
store 200 A
load 1500
print table
print ram
print swap
```

Error Handling:
- Invalid memory addresses are rejected with error messages
- Write attempts to read-only TEXT segment are blocked
- Comprehensive error reporting for file operations and memory allocation failures
- Automatic validation of page sizes (must be powers of 2)

Technical Implementation:
- Uses system calls (open, read, write, lseek) for file operations
- Implements bit manipulation for efficient address translation
- Memory-mapped approach for swap file management
- Proper cleanup of all allocated resources

USAGE
=====

./ex4 <dangerous_commands_file> <log_file>

Parameters:
- dangerous_commands_file: Text file containing dangerous commands (one per line)
- log_file: File where command execution times will be logged

Example:
./ex4 dangerous_commands.txt exec_times.log

For Virtual Memory Simulation:
The program includes a built-in virtual memory simulator that can be accessed through the vmem_do() function. The simulator reads configuration and commands from a script file and executes the virtual memory operations.

INPUT VALIDATION
================
- Maximum command length: 1024 characters (defined by MAX_INPUT_LENGTH)
- Maximum number of arguments: 6 (defined by MAX_ARGC)
- Multiple consecutive spaces between arguments are not allowed
- Empty inputs and input containing only spaces are handled properly
- Virtual memory addresses must be within valid range (0 to total_virtual_size-1)
- Page sizes must be powers of 2 for proper address translation

ERROR MESSAGES
==============
- ERR_ARGS: Displayed when more than 6 arguments are provided
- ERR_SPACE: Displayed when multiple spaces are found between arguments
- ERR_MAX_CHAR: Displayed when input exceeds maximum length
- ERR_CMD: Displayed when a command cannot be executed
- ERR: Dangerous command detected ("<command>"): Displayed when a dangerous command is blocked
- Signal-specific messages for abnormal process termination (e.g., "Terminated by signal: SIGSEGV")
- ERR_MAT_INPUT: Displayed when matrix calculator input format is invalid
- Virtual memory specific errors:
    - "Error: Invalid address <addr> (out of range)"
    - "Error: Invalid write operation to read-only segment at address <addr>"
    - "Error: Page size must be a power of two"
    - "Error: Swap file is full, cannot evict page <page>"

IMPLEMENTATION DETAILS
======================

Key Functions
-------------

Core Functions (from v1)
- get_string(): Reads user input with dynamic memory allocation
- split_to_args(): Splits command string into argument array
- checkMultipleSpaces(): Validates spacing between arguments
- read_file_lines(): Reads dangerous commands from file
- is_dangerous_command(): Checks if a command is dangerous
- time_diff(): Calculates execution time difference
- prompt(): Displays the detailed shell prompt
- append_to_log(): Records command execution time to log file
- update_min_max_time(): Updates minimum and maximum execution times

Core Functions (v2)
- handle_pipe(): Processes commands containing pipes
- my_tee(): Implements the internal tee command
- setup_resource_limits(): Configures resource limits for processes
- parse_rlimit_command(): Parses resource limit specifications
- handle_background(): Manages background process execution
- redirect_stderr(): Handles redirection of standard error
- check_process_status(): Enhanced error checking for process termination

Core Functions (v3)
- mcalc_handler(): Main handler for the mcalc command
- parse_matrix(): Parses matrix input in the specified format
- parse_input(): Validates and processes the entire mcalc command
- hierarchical_matrix_calculation(): Orchestrates parallel computation
- matrix_thread_operation(): Thread function for matrix operations
- copy_matrix(): Creates deep copies of matrices for processing
- free_matrices(): Cleans up allocated matrix memory

New Functions (v4) - Virtual Memory System
- vmem_do(): Main entry point for virtual memory simulation
- init_system(): Initializes the virtual memory simulation from script file
- clear_system(): Cleans up all allocated resources
- load(): Loads a byte from virtual address with automatic page fault handling
- store(): Stores a byte to virtual address with permission checking
- frame_evic(): Implements LRU page replacement algorithm
- moveToSwap(): Moves pages from memory to swap file
- parse_script_header(): Parses simulation configuration from script file
- execute_script(): Processes and executes script commands
- pow_of_two(): Utility function for address translation calculations

Print Functions (v4)
- print_memory(): Displays physical memory contents in hex and ASCII
- print_swap(): Shows swap file contents in hex and ASCII
- print_page_table(): Displays complete page table with flags and locations
- print_tlb(): Displays TLB contents (bonus feature)
- printAll(): Convenience function to print all memory state information

Memory Management
-----------------
- Dynamic memory allocation for input and command arguments
- Proper cleanup using free_args() to prevent memory leaks
- Error handling for memory allocation failures
- Proper handling of file descriptors for pipes and redirections
- Virtual memory simulation with proper page table management
- Automatic cleanup of swap space and memory frames
- Resource management for executable and swap files

COMPILATION
===========

gcc -g -Wall -pthread shell.c sim_mem.c -o ex4 && valgrind --leak-check=full --track-origins=yes ./ex4 f.txt log.txt
NOTES
=====
- The shell clears the log file at the start of each execution
- Initial minimum time is set to -1 to ensure it gets properly updated on first command
- Both dangerous and semi-dangerous commands are tracked separately
- The program uses fork() and waitpid() to ensure proper process management
- The my_tee implementation uses basic system calls (read(), write()) rather than stdio functions
- Resource limits are implemented using the setrlimit() and getrlimit() system calls
- Matrix calculator uses pthread library for parallel computation
- The hierarchical tree structure ensures proper order of operations for matrix subtraction
- Virtual memory simulation uses demand paging with realistic page fault handling
- LRU page replacement algorithm ensures efficient memory utilization
- Swap file management uses first-fit allocation for optimal space usage
- All memory operations are validated for proper permissions and address ranges
- The virtual memory simulator properly handles different memory segments (TEXT, DATA, BSS, HEAP/STACK)
- Page sizes must be powers of 2 for efficient bitwise address translation
- Timestamp-based LRU tracking ensures accurate page replacement decisions

Author: Kareem Natsheh