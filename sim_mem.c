#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

//=============================================================================
//                              DATA STRUCTURES
//=============================================================================

typedef struct {
    int V;          // Valid bit (1 = in memory, 0 = not in memory)
    int D;          // Dirty bit (1 = modified, 0 = clean)
    int P;          // Permission bit (1 = read-only, 0 = read/write)
    int frame_swap; // Frame number (if V=1) or swap page (if V=0, D=1)
} page_descriptor;

typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int timestamp;
} tlb_entry;

typedef struct sim_database {
    page_descriptor* page_table;
    int              swapfile_fd;
    int              program_fd;
    char*            main_memory;      // RAM - each frame is page_size bytes
    int              text_size;
    int              data_size;
    int              bss_size;
    int              heap_stack_size;
    tlb_entry*       tlb;
    int              page_size;
    int              num_pages;
    int              memory_size;
    int              swap_size;
    int              num_frames;
    int              tlb_size;
} sim_database;

//=============================================================================
//                            GLOBAL VARIABLES
//=============================================================================

char exe_file[256];
char swap_file[256];
int  text_size, data_size, bss_size, heap_stack_size;
int  page_size, num_pages, memory_size, swap_size;
int  text_pages_count, data_pages_count, bss_pages_count, heap_stack_pages_count;
int  total_size;        // Total virtual memory size in bytes
int* frame_time;        // Timestamp for each frame (for LRU eviction)
int  timestamp;         // Global timestamp counter
static int* swap_map = NULL;    // Swap allocation map: 0 = free, 1 = used
int vmem_do(const char* script_path);
//=============================================================================
//                             PRINT FUNCTIONS
//=============================================================================

/**
 * print_memory - Prints the contents of the main memory (RAM)
 * Shows each frame with its contents in both hex and character format
 */
void print_memory(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->main_memory) {
        printf("Error: Invalid memory simulation structure\n");
        return;
    }

    printf("=== MAIN MEMORY CONTENTS ===\n");
    printf("Memory size: %d bytes, Page size: %d bytes, Number of frames: %d\n",
           mem_sim->memory_size, mem_sim->page_size, mem_sim->num_frames);

    for (int frame = 0; frame < mem_sim->num_frames; frame++) {
        printf("Frame %d: ", frame);

        // Print hex values
        for (int i = 0; i < mem_sim->page_size; i++) {
            int addr = frame * mem_sim->page_size + i;
            printf("%02X ", (unsigned char)mem_sim->main_memory[addr]);
        }

        printf("| ");

        // Print character representation
        for (int i = 0; i < mem_sim->page_size; i++) {
            int addr = frame * mem_sim->page_size + i;
            char c = mem_sim->main_memory[addr];
            if (c >= 32 && c <= 126) {  // Printable ASCII
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
    printf("=============================\n\n");
}

/**
 * print_swap - Prints the contents of the swap file
 * Shows each page slot in the swap file
 */
void print_swap(sim_database* mem_sim) {
    if (!mem_sim || mem_sim->swapfile_fd < 0) {
        printf("Error: Invalid swap file\n");
        return;
    }

    printf("=== SWAP FILE CONTENTS ===\n");
    printf("Swap size: %d bytes, Page size: %d bytes, Number of swap pages: %d\n",
           mem_sim->swap_size, mem_sim->page_size, mem_sim->swap_size / mem_sim->page_size);

    int   num_swap_pages = mem_sim->swap_size / mem_sim->page_size;
    char* buffer = malloc(mem_sim->page_size);

    if (!buffer) {
        perror("Error allocating buffer for swap reading");
        return;
    }

    for (int page = 0; page < num_swap_pages; page++) {
        // Seek to the beginning of this page in swap file
        if (lseek(mem_sim->swapfile_fd, page * mem_sim->page_size, SEEK_SET) == -1) {
            perror("Error seeking in swap file");
            free(buffer);
            return;
        }

        // Read the page
        ssize_t bytes_read = read(mem_sim->swapfile_fd, buffer, mem_sim->page_size);
        if (bytes_read != mem_sim->page_size) {
            printf("Swap Page %d: [Error reading]\n", page);
            continue;
        }

        printf("Swap Page %d: ", page);

        // Print hex values
        for (int i = 0; i < mem_sim->page_size; i++) {
            printf("%02X ", (unsigned char)buffer[i]);
        }

        printf("| ");

        // Print character representation
        for (int i = 0; i < mem_sim->page_size; i++) {
            char c = buffer[i];
            if (c >= 32 && c <= 126) {  // Printable ASCII
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }

    free(buffer);
    printf("===========================\n\n");
}

/**
 * print_page_table - Prints the page table contents
 * Shows all page descriptors with their flags and frame/swap locations
 */
void print_page_table(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->page_table) {
        printf("Error: Invalid page table\n");
        return;
    }

    printf("=== PAGE TABLE ===\n");
    printf("Number of pages: %d\n", mem_sim->num_pages);
    printf("Page | V | D | P | Frame/Swap | Segment\n");
    printf("-----|---|---|---|------------|--------\n");

    // Calculate segment boundaries in pages
    int text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int data_pages = (mem_sim->data_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int bss_pages  = (mem_sim->bss_size + mem_sim->page_size - 1) / mem_sim->page_size;

    for (int page = 0; page < mem_sim->num_pages; page++) {
        page_descriptor* pd = &mem_sim->page_table[page];

        // Determine segment type
        const char* segment;
        if (page < text_pages) {
            segment = "TEXT";
        } else if (page < text_pages + data_pages) {
            segment = "DATA";
        } else if (page < text_pages + data_pages + bss_pages) {
            segment = "BSS";
        } else {
            segment = "H/S";  // Heap/Stack
        }

        printf("%4d | %d | %d | %d |", page, pd->V, pd->D, pd->P);

        if (pd->frame_swap == -1) {
            printf("      -    |");
        } else {
            printf("    %4d   |", pd->frame_swap);
        }

        printf(" %s\n", segment);
    }
    printf("==================\n");
    printf("Legend: V=Valid, D=Dirty, P=Permission (1=Read-Only, 0=Read/Write)\n");
    printf("        Frame/Swap: Frame number if in memory (V=1), Swap page if swapped out\n\n");
}

/**
 * print_tlb - Prints the TLB contents (bonus function)
 * Shows all TLB entries with their page->frame mappings
 */
void print_tlb(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->tlb) {
        printf("TLB not implemented or invalid\n");
        return;
    }

    printf("=== TLB CONTENTS ===\n");
    printf("TLB size: %d entries\n", mem_sim->tlb_size);
    printf("Entry | Valid | Page | Frame | Timestamp\n");
    printf("------|-------|------|-------|----------\n");

    for (int i = 0; i < mem_sim->tlb_size; i++) {
        tlb_entry* entry = &mem_sim->tlb[i];
        printf("  %d   |   %d   |", i, entry->valid);

        if (entry->valid) {
            printf(" %4d | %5d |  %8d\n",
                   entry->page_number, entry->frame_number, entry->timestamp);
        } else {
            printf("   -  |   -   |     -\n");
        }
    }
    printf("====================\n\n");
}

void printAll(sim_database* mem_sim) {
    print_page_table(mem_sim);
    print_memory(mem_sim);
    print_swap(mem_sim);
    //print_tlb(mem_sim); // Bonus function, if TLB is implemented
}

//=============================================================================
//                           UTILITY FUNCTIONS
//=============================================================================

/**
 * pow_of_two - Calculate log2 of a power of two
 * Used to determine bit shift amount for page/offset calculation
 */
int pow_of_two(int n) {
    if (n <= 0 || (n & (n - 1)) != 0) {
        fprintf(stderr, "Error: Page size must be a power of two\n");
        exit(EXIT_FAILURE);
    }
    int power = 0;
    while (n > 1) {
        n >>= 1;
        power++;
    }
    return power;
}

//=============================================================================
//                          SYSTEM INITIALIZATION
//=============================================================================

/**
 * parse_script_header - Reads the first line of the script file
 * Format: exe_file swap_file text_size data_size bss_size heap_stack_size 
 *         page_size num_pages memory_size swap_size
 */
int parse_script_header(FILE* script) {
    char line[512];
    if (!fgets(line, sizeof(line), script)) {
        fprintf(stderr, "Error: Script file is empty or missing configuration line\n");
        return 0;
    }

    int matched = sscanf(line, "%s %s %d %d %d %d %d %d %d %d",
                         exe_file, swap_file,
                         &text_size, &data_size, &bss_size, &heap_stack_size,
                         &page_size, &num_pages, &memory_size, &swap_size);
    if (matched != 10) {
        fprintf(stderr, "Error: Invalid script format. Got %d fields\n", matched);
        return 0;
    }
    return 1;
}

/**
 * init_system - Initializes the simulation from a script path
 * Creates all necessary data structures and opens required files
 */
sim_database* init_system(const char* script_path) {
    // 1. Open and parse script file
    FILE* script = fopen(script_path, "r");
    if (!script) {
        perror("Error opening script file");
        return NULL;
    }

    if (!parse_script_header(script)) {
        fclose(script);
        return NULL;
    }

    // Calculate page counts for each segment
    text_pages_count       = (text_size + page_size - 1) / page_size;
    data_pages_count       = (data_size + page_size - 1) / page_size;
    bss_pages_count        = (bss_size + page_size - 1) / page_size;
    heap_stack_pages_count = (heap_stack_size + page_size - 1) / page_size;

    // 2. Allocate main simulation structure
    sim_database* db = malloc(sizeof(sim_database));
    if (!db) {
        perror("malloc sim_database failed");
        fclose(script);
        return NULL;
    }

    // Initialize basic parameters
    db->text_size       = text_size;
    db->data_size       = data_size;
    db->bss_size        = bss_size;
    db->heap_stack_size = heap_stack_size;
    db->page_size       = page_size;
    db->num_pages       = num_pages;
    db->memory_size     = memory_size;
    db->swap_size       = swap_size;
    db->num_frames      = memory_size / page_size;
    db->tlb_size        = 0;

    // 3. Open executable file for reading
    db->program_fd = open(exe_file, O_RDONLY);
    if (db->program_fd < 0) {
        perror("Error opening program file");
        free(db);
        fclose(script);
        return NULL;
    }

    // 4. Create/resize swap file
    db->swapfile_fd = open(swap_file, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (db->swapfile_fd < 0) {
        perror("Error creating/opening swap file");
        close(db->program_fd);
        free(db);
        fclose(script);
        return NULL;
    }
    if (ftruncate(db->swapfile_fd, swap_size) == -1) {
        perror("Error sizing swap file");
        close(db->program_fd);
        close(db->swapfile_fd);
        free(db);
        fclose(script);
        return NULL;
    }

    // 5. Allocate memory structures
    db->main_memory = malloc(memory_size);
    db->page_table  = malloc(sizeof(page_descriptor) * num_pages);
    db->tlb         = NULL;

    if (!db->main_memory || !db->page_table) {
        perror("Memory allocation failed");
        close(db->program_fd);
        close(db->swapfile_fd);
        free(db->main_memory);
        free(db->page_table);
        free(db);
        fclose(script);
        return NULL;
    }

    // Initialize RAM with '-' characters
    memset(db->main_memory, '-', memory_size);

    // 6. Initialize page table entries
    for (int i = 0; i < num_pages; i++) {
        db->page_table[i].V          = 0;  // Not in memory initially
        db->page_table[i].D          = 0;  // Clean initially
        db->page_table[i].frame_swap = -1; // No frame/swap assignment
        // Text pages are read-only (P=1), others are read/write (P=0)
        db->page_table[i].P          = (i < text_pages_count) ? 1 : 0;
    }

    // 7. Initialize global variables
    total_size = db->num_pages * db->page_size;
    timestamp  = 1;

    frame_time = malloc(db->num_frames * sizeof(*frame_time));
    if (!frame_time) {
        perror("Error allocating frame_time array");
        exit(1);
    }
    for (int i = 0; i < db->num_frames; i++) {
        frame_time[i] = 0;  // 0 means frame is free
    }

    // Initialize swap allocation map
    int num_swap_pages = db->swap_size / db->page_size;
    swap_map = calloc(num_swap_pages, sizeof(*swap_map));
    if (!swap_map) {
        perror("Error allocating swap map");
        exit(EXIT_FAILURE);
    }

    printf("Loaded program \"%s\" with text=%d, data=%d, bss=%d, heap_stack=%d.\n",
           exe_file, text_size, data_size, bss_size, heap_stack_size);

    fclose(script);
    return db;
}

/**
 * clear_system - Frees resources allocated by init_system
 */
void clear_system(sim_database* db) {
    if (!db) return;

    close(db->program_fd);
    close(db->swapfile_fd);
    free(db->main_memory);
    free(db->page_table);
    free(db->tlb);
    free(frame_time);
    free(swap_map);
    free(db);
}

//=============================================================================
//                           MEMORY MANAGEMENT
//=============================================================================

// Forward declarations
int frame_evic(sim_database* db);
int moveToSwap(sim_database* db, int p);

/**
 * load - Load a byte from virtual address
 * Handles page faults and memory management automatically
 */
char load(sim_database* mem_sim, int address) {
    // 1. Validate address range
    if (address < 0 || address >= total_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return '-';
    }

    // 2. Calculate page number and offset within page
    int shift  = pow_of_two(mem_sim->page_size);
    int page   = address >> shift;                          // Page number
    int offset = address & (mem_sim->page_size - 1);        // Offset within page

    // 3. Check if page is already in memory (V == 1)
    if (mem_sim->page_table[page].V == 1) {
        //=== PAGE HIT: Page is in memory ===
        int  frame     = mem_sim->page_table[page].frame_swap;
        int  phys_addr = frame * mem_sim->page_size + offset;
        char val       = mem_sim->main_memory[phys_addr];

        printf("Value at address %d = %c\n", address, val);
        return val;
    }

    //=== PAGE FAULT: Page is not in memory (V == 0) ===
    else{   //modifed V==0
    // 4. Handle different page types based on permission and dirty bits
    if (mem_sim->page_table[page].P == 1) {
        //--- TEXT PAGE (Read-only, P=1) ---
        // Text pages are always loaded from executable file
        printf("Page fault: Loading page %d from %s\n", page, exe_file);

        // Seek to page location in executable
        off_t offset_bytes = (off_t)page * mem_sim->page_size;
        if (lseek(mem_sim->program_fd, offset_bytes, SEEK_SET) == -1) {
            perror("Error seeking in file");
            return '-';
        }

        // Get a frame and load the page
        int   frame = frame_evic(mem_sim);
        char* dest  = mem_sim->main_memory + frame * mem_sim->page_size;
        if (read(mem_sim->program_fd, dest, mem_sim->page_size) != mem_sim->page_size) {
            perror("Error reading from file");
            return '-';
        }

        // Update page table and timestamp
        frame_time[frame] = timestamp++;
        mem_sim->page_table[page].V          = 1;
        mem_sim->page_table[page].D          = 0;
        mem_sim->page_table[page].frame_swap = frame;

        char val = dest[offset];
        printf("Value at address %d = %c\n", address, val);
        return val;

    } else {
        //--- DATA/BSS/HEAP/STACK PAGE (Read-write, P=0, V==0) ---

        if (mem_sim->page_table[page].D == 0) {
            //--- CLEAN PAGE (D=0): Load from file or initialize ---

            if (page < text_pages_count + data_pages_count) {
                //... DATA PAGE: Load from executable file
                printf("Page fault: Loading page %d from %s\n", page, exe_file);

                off_t offset_bytes = (off_t)page * mem_sim->page_size;
                if (lseek(mem_sim->program_fd, offset_bytes, SEEK_SET) == -1) {
                    perror("Error seeking in file");
                    return '-';
                }

                int   frame = frame_evic(mem_sim);
                char* dest  = mem_sim->main_memory + frame * mem_sim->page_size;
                if (read(mem_sim->program_fd, dest, mem_sim->page_size) != mem_sim->page_size) {
                    perror("Error reading from file");
                    return '-';
                }

                frame_time[frame] = timestamp++;
                mem_sim->page_table[page].V          = 1;
                mem_sim->page_table[page].frame_swap = frame;

                char val = dest[offset];
                printf("Value at address %d = %c\n", address, val);
                return val;

            } else {
                //... BSS/HEAP/STACK PAGE: Initialize with zeros
                printf("Page fault: Loading page %d with zeros\n", page);

                int   frame = frame_evic(mem_sim);
                char* dest  = mem_sim->main_memory + frame * mem_sim->page_size;
                memset(dest, 0, mem_sim->page_size);

                frame_time[frame] = timestamp++;
                mem_sim->page_table[page].V          = 1;
                mem_sim->page_table[page].frame_swap = frame;

                char val = dest[offset];
                printf("Value at address %d = %c\n", address, val);
                return val;
            }

        } else {
            //--- DIRTY PAGE (D=1): Load from swap file
            printf("Page fault: Loading page %d from %s\n", page, swap_file);

            int swap_block = mem_sim->page_table[page].frame_swap;
            off_t offset_bytes = (off_t)swap_block * mem_sim->page_size;
            if (lseek(mem_sim->swapfile_fd, offset_bytes, SEEK_SET) == -1) {
                perror("Error seeking in swap file");
                return '-';
            }

            int   frame = frame_evic(mem_sim);
            char* dest  = mem_sim->main_memory + frame * mem_sim->page_size;
            if (read(mem_sim->swapfile_fd, dest, mem_sim->page_size) != mem_sim->page_size) {
                perror("Error reading from file");
                return '-';
            }
// Free the swap block since page is now back in memory
            swap_map[swap_block] = 0;
            // Clear the swap block with '-' characters
            char* clear_buffer = malloc(mem_sim->page_size);
            if (clear_buffer) {
                memset(clear_buffer, '-', mem_sim->page_size);
                if (lseek(mem_sim->swapfile_fd, (off_t)swap_block * mem_sim->page_size, SEEK_SET) != -1) {
                    write(mem_sim->swapfile_fd, clear_buffer, mem_sim->page_size);
                }
                free(clear_buffer);
            }
            frame_time[frame] = timestamp++;
            mem_sim->page_table[page].V          = 1;
            mem_sim->page_table[page].frame_swap = frame;

            char val = dest[offset];
            printf("Value at address %d = %c\n", address, val);
            return val;
        }
    }
}}

/**
 * store - Store a byte to virtual address
 * Handles page faults, permission checks, and dirty bit management
 */
void store(sim_database* mem_sim, int address, char value) {
    // 1. Validate address range
    if (address < 0 || address >= total_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return;
    }

    // 2. Calculate page number and offset
    int shift  = pow_of_two(mem_sim->page_size);
    int page   = address >> shift;
    int offset = address & (mem_sim->page_size - 1);

    // 3. Check write permission (text pages are read-only)
    if (mem_sim->page_table[page].P == 1) {
        fprintf(stderr, "Error: Invalid write operation to read-only segment at address %d\n", address);
        return;
    }

    // 4. Ensure page is loaded in memory
    if (mem_sim->page_table[page].V == 0) {
        // Page fault: load the page first using load function
        char loaded_value = load(mem_sim, address);
        if (loaded_value == '-') {
            return;  // Load failed
        }
    }

    // 5. Perform the store operation
    int frame     = mem_sim->page_table[page].frame_swap;
    int phys_addr = frame * mem_sim->page_size + offset;

    mem_sim->main_memory[phys_addr] = value;
    mem_sim->page_table[page].D     = 1;  // Mark page as dirty
    frame_time[frame]               = timestamp++;

    printf("Stored value '%c' at address %d\n", value, address);
}

/**
 * frame_evic - Find a frame to use (LRU eviction policy)
 * Returns frame number, handling eviction if necessary
 */
int frame_evic(sim_database* db) {
    int nf = db->num_frames;

    // 1. Try to find a free frame (timestamp == 0)
    for (int f = 0; f < nf; f++) {
        if (frame_time[f] == 0) {
            frame_time[f] = timestamp++;
            return f;
        }
    }

    // 2. No free frame: find LRU victim (smallest timestamp)
    int victim = 0;
    int min_t  = frame_time[0];
    for (int f = 1; f < nf; f++) {
        if (frame_time[f] < min_t) {
            min_t  = frame_time[f];
            victim = f;
        }
    }

    // 3. Find and evict the page currently in victim frame
    for (int p = 0; p < db->num_pages; p++) {
        page_descriptor* pd = &db->page_table[p];
        if (pd->V == 1 && pd->frame_swap == victim) {
            // If dirty and writable, save to swap
            if (pd->P == 0 && pd->D == 1) {
                printf("Page replacement: Evicting page %d to swap\n", p);
                pd->frame_swap=moveToSwap(db, p);
            }
            // Evict from memory
            pd->V = 0;
            break;
        }
    }

    // 4. Return the now-available frame
    frame_time[victim] = timestamp++;
    return victim;
}

/**
 * moveToSwap - Write a page from memory to swap file
 * Uses first-fit allocation for swap space
 */
int moveToSwap(sim_database* db, int page_num) {
    int              num_swap_pages = db->swap_size / db->page_size;
    page_descriptor* pd             = &db->page_table[page_num];

    // Find first free swap page (first-fit allocation)
    int block;
    for (block = 0; block < num_swap_pages; block++) {
        if (swap_map[block] == 0) {
            swap_map[block] = 1;  // Mark as used
            break;
        }
    }
    if (block == num_swap_pages) {
        fprintf(stderr, "Error: Swap file is full, cannot evict page %d\n", page_num);
        return -1;
    }

    // Write page data to swap file
    off_t offset_bytes = (off_t)block * db->page_size;
    if (lseek(db->swapfile_fd, offset_bytes, SEEK_SET) == -1) {
        perror("Error seeking in swap file");
        return -1;
    }

    int   frame = pd->frame_swap;
    char* src   = db->main_memory + frame * db->page_size;
    if (write(db->swapfile_fd, src, db->page_size) != db->page_size) {
        perror("Error writing to swap file");
        return -1;
    }

    // Update page table: page now lives in swap
    pd->frame_swap = block;  // Now points to swap block
    pd->V          = 0;      // No longer in RAM

    return block;
}

//=============================================================================
//                           SCRIPT EXECUTION
//=============================================================================

/**
 * execute_script - Execute commands from script file
 * Processes load, store, and print commands
 */
void execute_script(sim_database* db, const char* script_path) {
    FILE* script = fopen(script_path, "r");
    if (!script) {
        perror("Error opening script for commands");
        return;
    }

    char line[256];

    // Skip header line
    if (!fgets(line, sizeof(line), script)) {
        fclose(script);
        return;
    }

    // Process each command line
    while (fgets(line, sizeof(line), script)) {
        // Trim leading whitespace
        char* p = line;
        while (isspace((unsigned char)*p)) p++;

        int  addr;
        char value;

        // Parse and execute commands
        if (sscanf(p, "load %d", &addr) == 1) {
            load(db, addr);
        }
        else if (sscanf(p, "store %d %c", &addr, &value) == 2) {
            store(db, addr, value);
        }
        else if (strncmp(p, "print table", 11) == 0) {
            print_page_table(db);
        }
        else if (strncmp(p, "print ram", 9) == 0) {
            print_memory(db);
        }
        else if (strncmp(p, "print swap", 10) == 0) {
            print_swap(db);
        }
        else {
            fprintf(stderr, "Error: Invalid script format\n");
        }
    }

    fclose(script);
}

//=============================================================================
//                              MAIN FUNCTION
//=============================================================================

int main() {
    vmem_do("scriptt");
    return 0;
}
//////////////////////////////////////////////////////////////////////////////////////

//=============================================================================
//                             Prosedure to run the simulation
//=============================================================================

/**
 * vmem_do - initialize, execute, and tear down the VM simulator
 * @script_path: path to the script file
 *
 * Returns 1 on success, 0 on any error.
 */
int vmem_do(const char *script_path) {
    // 1) set up the VM simulator
    sim_database *db = init_system(script_path);
    if (!db) {
        // init_system already printed an error
        return 0;
    }

    // 2) execute all the commands in the script
    execute_script(db, script_path);

    // 3) tear everything down
    clear_system(db);

    return 1;
}
//=============================================================================