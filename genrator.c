#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *filename = "program_large.bin";
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    // Write 32 bytes of "text" segment (0x01 .. 0x20)
    for (int i = 1; i <= 32; ++i) {
        unsigned char byte = (unsigned char)i;
        if (fwrite(&byte, 1, 1, f) != 1) {
            perror("fwrite text");
            fclose(f);
            return EXIT_FAILURE;
        }
    }

    // Write 32 bytes of "data" segment (0xA1 .. 0xC0)
    for (int i = 0; i < 32; ++i) {
        unsigned char byte = (unsigned char)(0xA1 + i);
        if (fwrite(&byte, 1, 1, f) != 1) {
            perror("fwrite data");
            fclose(f);
            return EXIT_FAILURE;
        }
    }

    fclose(f);
    printf("Wrote %s (64 bytes total: 32B text + 32B data)\n", filename);
    return EXIT_SUCCESS;
}
