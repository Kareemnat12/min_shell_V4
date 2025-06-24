#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "sim_mem.h"

/////// PRE DEFINITIONS ///////

typedef struct {
    int V;
    int D;
    int P;
    int frame_swap;
} page_descriptor;


typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int timestamp;
} tlb_entry;
typedef struct sim_database {
    page_descriptor* page_table;
    int swapfile_fd;
    int program_fd;
    char* main_memory; // SIZE_MEMORY length , and this is the RAM, Evey frame is page_size bytes
    int text_size;
    int data_size;
    int bss_size;
    int heap_stack_size;

    tlb_entry* tlb;

    int page_size;
    int num_pages;
    int memory_size;
    int swap_size;
    int num_frames;
    int tlb_size;
} sim_database;


//////////////////////////////// PRINT FUNCTIONS ///////////////////////

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

    int num_swap_pages = mem_sim->swap_size / mem_sim->page_size;
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
    int bss_pages = (mem_sim->bss_size + mem_sim->page_size - 1) / mem_sim->page_size;

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
///////////////////////////////////////////////////////////




//////// SIMULATION MEMORY SYSTEM /////////

// === GLOBAL CONFIG VARIABLES ===
char exe_file[256];
char swap_file[256];
int text_size, data_size, bss_size, heap_stack_size;
int page_size, num_pages, memory_size, swap_size;
int text_pages, data_pages, bss_pages , heap_stack_pages;
int offset , page_number, frame_number;

// parse_script_header - Reads the first line of the script file
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



sim_database* init_system(char exe_file_name[],char swap_file_name[],int text_size,int data_size,int bss_size,int heap_stack_size) {
    sim_database* db = (sim_database*)malloc(sizeof(sim_database));
    if (!db) {
        perror("malloc sim_database failed");
        return NULL;
    }

    // Save basic sizes
    db->text_size = text_size;
    db->data_size = data_size;
    db->bss_size = bss_size;
    db->heap_stack_size = heap_stack_size;
    db->page_size = page_size;
    db->num_pages = num_pages;
    db->memory_size = memory_size;
    db->swap_size = swap_size;
    db->num_frames = memory_size / page_size;
    db->tlb_size = 0;   // No TLB unless implemented

    // --- Open executable file ---
    db->program_fd = open(exe_file_name, O_RDONLY);
    if (db->program_fd < 0) {
        perror("Error opening program file");
        free(db);
        return NULL;
    }

    // --- Create + open swap file ---
    db->swapfile_fd = open(swap_file_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (db->swapfile_fd < 0) {
        perror("Error creating/opening swap file");
        close(db->program_fd);
        free(db);
        return NULL;
    }

    // Fill swap file with '-'
    for (int i = 0; i < swap_size; i++) {
        if (write(db->swapfile_fd, "-", 1) != 1) {
            perror("Error writing to swap file");
            close(db->program_fd);
            close(db->swapfile_fd);
            free(db);
            return NULL;
        }
    }

    // --- Allocate memory and page table ---
    db->main_memory = (char*)malloc(memory_size);
    db->page_table = (page_descriptor*)malloc(sizeof(page_descriptor) * num_pages);
    db->tlb = NULL;

    if (!db->main_memory || !db->page_table) {
        perror("Memory allocation failed");
        close(db->program_fd);
        close(db->swapfile_fd);
        free(db);
        return NULL;
    }

    // Fill RAM with '-'
    memset(db->main_memory, '-', memory_size);

    // --- Init page descriptors ---
    int text_pages = (text_size + page_size - 1) / page_size;
    int data_pages = (data_size + page_size - 1) / page_size;
    int bss_pages  = (bss_size  + page_size - 1) / page_size;

    for (int i = 0; i < num_pages; i++) {
        db->page_table[i].V = 0;
        db->page_table[i].D = 0;
        db->page_table[i].frame_swap = -1;

        if (i < text_pages)
            db->page_table[i].P = 1;  // Read-only TEXT
        else
            db->page_table[i].P = 0;  // Writable: DATA, BSS, HEAP/STACK
    }

    return db;
}


///// The get_segment function determines which segment a given page number belongs to ///////
const char* get_segment(sim_database* mem_sim, int page_num) {
    int text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int data_pages = (mem_sim->data_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int bss_pages  = (mem_sim->bss_size  + mem_sim->page_size - 1) / mem_sim->page_size;
    int heap_stack_pages = (mem_sim->heap_stack_size + mem_sim->page_size - 1) / mem_sim->page_size;

    // Start page indices
    int text_start = 0;
    int data_start = text_start + text_pages;
    int bss_start  = data_start + data_pages;
    int hs_start   = bss_start + bss_pages;

    if (page_num < data_start) {
        return "TEXT";
    } else if (page_num < bss_start) {
        return "DATA";
    } else if (page_num < hs_start) {
        return "BSS";
    } else {
        return "Heap_Stack"; // Heap / Stack
    }
}


char load(sim_database* mem_sim, int address) {
    // Total virtual space = num_pages * page_size
    int total_size = mem_sim->num_pages * mem_sim->page_size;

    // 1. Check address validity
    if (address < 0 || address >= total_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return '-';
    }
    // 2. Compute page & offset into local vars
    int page, offset;
    calculate_page_offset(mem_sim, address, &page, &offset);


    // 3. If page is already in RAM (V == 1), just read and return:
    if (mem_sim->page_table[page].V == 1) {
        // Grab the physical frame number from the page table
        int frame = mem_sim->page_table[page].frame_swap;
        // Compute the physical address: frame × page_size + offset
        int phys_addr = frame * mem_sim->page_size + offset;
        // Return the byte stored there
        return mem_sim->main_memory[phys_addr];
    }
    // Otherwise, handle page fault…





}


// static so it’s private to sim_mem.c
static void calculate_page_offset(sim_database* mem_sim,
                                  int address,
                                  int *out_page,
                                  int *out_offset) {
    // page_size must be a power-of-two per spec
    int shift = __builtin_ctz(mem_sim->page_size);
    *out_page   = address >> shift;
    *out_offset = address & (mem_sim->page_size - 1);
}








//////////// MAIN FUNCTION ////////////
int main(){
    printf("Memory Simulation System\n");
}