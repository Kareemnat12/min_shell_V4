#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
//#include "sim_mem.h"

/////// PRE DEFINITIONS ///////

//////////////////////////////
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
////////////////////////////////FUNCTION DECLARATIONS ///////////////////////
static void calculate_page_offset(sim_database* mem_sim,
                                  int address,
                                  int *out_page,
                                  int *out_offset);

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

// sim_mem.c — memory‑simulation core for Mini‑Shell (vmem)
// -------------------------------------------------------------

#include <errno.h>

/////// GLOBAL CONFIG VARIABLES /////////
char exe_file[256];
char swap_file[256];
int text_size, data_size, bss_size, heap_stack_size;
int page_size, num_pages, memory_size, swap_size;
int text_pages_count,data_pages_count,bss_pages_count,heap_stack_pages_count;
int total_size; // Total virtual memory size in bytes
int* frame_time;
int timestamp = 1;
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

// init_system - Initializes the simulation from a script path
sim_database* init_system(const char* script_path) {
    // 1. Open script
    FILE* script = fopen(script_path, "r");
    if (!script) {
        perror("Error opening script file");
        return NULL;
    }
    // 2. Parse header
    if (!parse_script_header(script)) {
        fclose(script);
        return NULL;
    }

    // Compute page counts
     text_pages_count       = (text_size      + page_size - 1) / page_size;
     data_pages_count       = (data_size      + page_size - 1) / page_size;
     bss_pages_count        = (bss_size       + page_size - 1) / page_size;
     heap_stack_pages_count = (heap_stack_size+ page_size - 1) / page_size;

    // 3. Allocate sim_database
    sim_database* db = malloc(sizeof(sim_database));
    if (!db) {
        perror("malloc sim_database failed");
        fclose(script);
        return NULL;
    }
    // Save basic sizes
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

    // 4. Open executable file
    db->program_fd = open(exe_file, O_RDONLY);
    if (db->program_fd < 0) {
        perror("Error opening program file");
        free(db);
        fclose(script);
        return NULL;
    }

    // 5. Create/resize swap file
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

    // 6. Allocate memory and page table
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
    // Fill RAM with '-'
    memset(db->main_memory, '-', memory_size);

    // 7. Init page descriptors
    for (int i = 0; i < num_pages; i++) {
        db->page_table[i].V          = 0;
        db->page_table[i].D          = 0;
        db->page_table[i].frame_swap = -1;
        db->page_table[i].P          = (i < text_pages_count) ? 1 : 0;
    }

    // 8. Success banner
    printf("Loaded program \"%s\" with text=%d, data=%d, bss=%d, heap_stack=%d.\n",
           exe_file, text_size, data_size, bss_size, heap_stack_size);

    fclose(script);
    // Total virtual space = num_pages * page_size
     total_size = db->num_pages * db->page_size;
    frame_time = malloc(db->num_frames * sizeof(int));// make the allocation if its not worked
    if (!frame_time) {
        perror("Error allocating frame time array");
        close(db->program_fd);
        close(db->swapfile_fd);
        free(db->main_memory);
        free(db->page_table);
        free(db);
        return NULL;
    }
    return db;
}

// clear_system - Frees resources allocated by init_system
void clear_system(sim_database* db) {
    if (!db) return;
    close(db->program_fd);
    close(db->swapfile_fd);
    free(db->main_memory);
    free(db->page_table);
    free(db->tlb);
    free(db);
}


///// The get_segment function determines which segment a given page number belongs to ///////
//const char* get_segment(sim_database* mem_sim, int page_num) {
//     text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
//     data_pages = (mem_sim->data_size + mem_sim->page_size - 1) / mem_sim->page_size;
//     bss_pages  = (mem_sim->bss_size  + mem_sim->page_size - 1) / mem_sim->page_size;
//     heap_stack_pages = (mem_sim->heap_stack_size + mem_sim->page_size - 1) / mem_sim->page_size;
//
//    // Start page indices
//    int text_start = 0;
//    int data_start = text_start + text_pages;
//    int bss_start  = data_start + data_pages;
//    int hs_start   = bss_start + bss_pages;
//
//    if (page_num < data_start) {
//        return "TEXT";
//    } else if (page_num < bss_start) {
//        return "DATA";
//    } else if (page_num < hs_start) {
//        return "BSS";
//    } else {
//        return "Heap_Stack"; // Heap / Stack
//    }
//}
//////////////////////////////////////////////////////////////

/// power of two function ///
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


char load(sim_database* mem_sim, int address) {

    // 1. Check address validity
    if (address < 0 || address >= total_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return '-';
    }
    // 2. Compute page & offset into local vars
    int page, offset;
   // calculate_page_offset(mem_sim, address, &page, &offset);
    /// calculate offset and page number ///
    int shift = pow_of_two(mem_sim->page_size);
    page   = address >> shift;
    offset= address & (mem_sim->page_size - 1);


    // 3. Check if page number is valid or not
    // I. If page is already in RAM (V == 1), just read and return:
    if (mem_sim->page_table[page].V == 1) {
        // Grab the physical frame number from the page table
        int frame = mem_sim->page_table[page].frame_swap;
        // Compute the physical address: frame × page_size + offset
        int phys_addr = frame * mem_sim->page_size + offset;
        // Read the byte from memory
        char val = mem_sim->main_memory[phys_addr];
        // **Success message** per spec:
        printf("Value at address %d = %c\n", address, val);
        return val;
    }
        /// II.  Otherwise, handle page fault loading the stuff to main memory V==0
    else {
        // Load in the follwing steps:
        //1. check permeation  of the page :
        // V==0
        if (mem_sim->page_table[page].P == 1) {// this means its a text page thus read only
            // 1. Report page fault for text
            printf("Page fault: Loading page %d from %s\n", page, exe_file);

            // 2. Seek to the start of that page in the executable
            off_t offset_bytes = (off_t)page * mem_sim->page_size; /* BE aware about the off_t) */
            if (lseek(mem_sim->program_fd, offset_bytes, SEEK_SET) == -1) {
                perror("Error seeking in file");
                return '-';
            }

            // 3. Read the full page into a free frame in RAM
            int frame =0 /* find a free frame index here */; // 1 for test purpose
            char *dest = mem_sim->main_memory + frame * mem_sim->page_size;
            if (read(mem_sim->program_fd, dest, mem_sim->page_size)
                != mem_sim->page_size) {
                perror("Error reading from file");
                return '-';
            }

            // 4. Update the page‐table entry
            mem_sim->page_table[page].V = 1;
            mem_sim->page_table[page].D = 0;
            mem_sim->page_table[page].frame_swap = frame;

            // 5. Finally, return the requested byte with success banner
            char val = dest[offset];
            printf("Value at address %d = %c\n", address, val);
            return val;
        }
        else {/* V==0, P==1 */
            //2.1 if its not dirty, read from programe  file
                if(mem_sim->page_table[page].D==0){
                    //D==0
                    //if() if the page is from data or not
                    if(page<text_pages_count+data_pages_count)/* If the page from data  */{
                        // 1. Report page fault for text
                        printf("Page fault: Loading page %d from %s\n", page, exe_file);

                        // 2. Seek to the start of that page in the executable
                        off_t offset_bytes = (off_t)page * mem_sim->page_size; /* BE aware about the off_t) */
                        if (lseek(mem_sim->program_fd, offset_bytes, SEEK_SET) == -1) {
                            perror("Error seeking in file");
                            return '-';
                        }

                        // 3. Read the full page into a free frame in RAM
                        int frame =0 /* find a free frame index here */; // 1 for test purpose
                        char *dest = mem_sim->main_memory + frame * mem_sim->page_size;
                        if (read(mem_sim->program_fd, dest, mem_sim->page_size)
                            != mem_sim->page_size) {
                            perror("Error reading from file");
                            return '-';
                        }

                        // 4. Update the page‐table entry
                        mem_sim->page_table[page].V = 1;
                        mem_sim->page_table[page].frame_swap = frame;

                        // 5. Finally, return the requested byte with success banner
                        char val = dest[offset];
                        printf("Value at address %d = %c\n", address, val);
                        return val;
                    }

                    // else if tha page from heap_stack bss we initialize  stuff with zeros and update the table

                    else{
                        // 1. Report page fault for heap/stack/bss
                        printf("Page fault: Loading page %d with zeros\n", page);

                        // 2. Find a free frame index (for simplicity, we assume one is available)
                        int frame = 0; /* find a free frame index here */ // 1 for test purpose
                        char *dest = mem_sim->main_memory + frame * mem_sim->page_size;

                        // 3. Fill the page with zeros
                        memset(dest, 0, mem_sim->page_size);

                        // 4. Update the page‐table entry
                        mem_sim->page_table[page].V = 1;
                        mem_sim->page_table[page].frame_swap = frame;

                        // 5. Finally, return the requested byte with success banner
                        char val = dest[offset];
                        printf("Value at address %d = %c\n", address, val);
                        return val;




                    }
                }
                // D==1 ,V==0, P==0
                    //if its from data or bss, we need to swap it out first ithink ther is no diffrance
                else{
                    // 1. Report page fault for text
                    printf("Page fault: Loading page %d from %s\n", page, swap_file);

                    // 2. Seek to the start of that page in the swap file
                    off_t offset_bytes = (off_t)page * mem_sim->page_size; /* BE aware about the off_t) */
                    if (lseek(mem_sim->swapfile_fd, offset_bytes, SEEK_SET) == -1) {
                        perror("Error seeking in file");
                        return '-';
                    }

                    // 3. Read the full page into a free frame in RAM
                    int frame =0 /* find a free frame index here */; // 1 for test purpose
                    char *dest = mem_sim->main_memory + frame * mem_sim->page_size;
                    if (read(mem_sim->swapfile_fd, dest, mem_sim->page_size)
                        != mem_sim->page_size) {
                        perror("Error reading from file");
                        return '-';
                    }

                    // 4. Update the page‐table entry
                    mem_sim->page_table[page].V = 1;
                    mem_sim->page_table[page].frame_swap = frame;

                    // 5. Finally, return the requested byte with success banner
                    char val = dest[offset];
                    printf("Value at address %d = %c\n", address, val);
                    return val;


                }

        }

    }

}

////////////////////////////STORE FUCNTIONS//////////////////////
void store(sim_database* mem_sim, int address, char value){
    // 1. Check address validity
    int page, offset;
    if (address < 0 || address >= total_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return;
    }
    int shift = pow_of_two(mem_sim->page_size);
    page   = address >> shift;
    offset= address & (mem_sim->page_size - 1);

    //2. check if the page is text or not
    if (mem_sim->page_table[page].P==1) {
        fprintf(stderr,"Error: Invalid write operation to\n""read-only segment at address %d\n",address);
        return;
    }

    if(mem_sim->page_table[page].V == 0) {
        // Page fault: load the page first
        char loaded_value = load(mem_sim, address);
        if (loaded_value == '-') {
            return;
        }
    }
        // Now we can store the value
            int frame = mem_sim->page_table[page].frame_swap;
            int phys_addr = frame * mem_sim->page_size + offset;
            mem_sim->main_memory[phys_addr] = value;
            mem_sim->page_table[page].D = 1; // Mark as dirty
            printf("Stored value '%c' at address %d\n", value, address);
}
int zero_flag = 0;

int frame_evic(sim_database* mem_sim){
    int min = frame_time[0];
    int min_index = 0;
    int lim= mem_sim->num_frames;

    for(int i=0;i<lim;i++){// handle if there is no empty frame
        if(frame_time[i]==0)
            zero_flag = 0;
        else
            zero_flag = 1;
    }


    //to handle the empty place frames
    if (zero_flag==0){
    for (int i =0; i<lim;i++){
        if(frame_time[i]==0){
            frame_time[i] = timestamp;
            timestamp++;
            return i; // Return the first free frame
        }}}
    for (int i = 0; i < lim; i++) {//handle the minimum stuff
        if (frame_time[i] < min) {
            min = frame_time[i];
            min_index = i;
        }
    }


}




void printAll(sim_database* mem_sim) {
    print_page_table(mem_sim);
    print_memory(mem_sim);
    print_swap(mem_sim);
    //print_tlb(mem_sim); // Bonus function, if TLB is implemented
}


//////////// MAIN FUNCTION ////////////
int main(){
    printf("Memory Simulation System\n");
    sim_database *db = init_system("script.txt");
    printAll(db);
    printf("///////////////Loading value at address 64:///////////////////\n");
    load(db, 64);
    printAll(db);
    printf("///////////////Storing value 'K' at address 0://///////////////\n");
    store(db, 64, 'K');
    printAll(db);
}