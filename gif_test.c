#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"gif_decode.h"

int main(int argc, char * argv[]) {
    FILE * fptr;
    int width, height;
    unsigned char * data;
    int frame=-1;

    if (argc == 1) {
        fptr = stdin;
    }

    if (argc >= 2) {
        fptr = fopen(argv[1], "rb");
        if (!fptr) {
            fprintf(stderr, "Error opening file %s\n", argv[1]);
            return 1;
        }
    }

    if (argc >= 3) {
        frame = strtol(argv[2], NULL, 10);
    }

    gif_decode(fptr, frame, &data, &width, &height);
    if (argc != 1) {
        fclose(fptr);
    }

    unsigned char * data2 = (unsigned char *)malloc(width * height * 3);
    unsigned char *ptr1, *ptr2;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            ptr1 = data + 4 * (y * width + x);
            ptr2 = data2 + 3 * (y * width + x);
            for (int i = 0; i < 3; i++) ptr2[i] = ptr1[i];
        }
    }

    char buffer[1000]; int buffer_write;
    fopen_s(&fptr, "test_output.ppm", "wb+");
    buffer_write = sprintf(buffer, "P6\n");
    fwrite(buffer, 1, buffer_write, fptr);
    buffer_write = sprintf(buffer, "%d %d\n", width, height);
    fwrite(buffer, 1, buffer_write, fptr);
    buffer_write = sprintf(buffer, "255\n");
    fwrite(buffer, 1, buffer_write, fptr);
    buffer_write = fwrite(data2, 1, 3 * width * height, fptr);

    fclose(fptr);

    free(data);
    free(data2);

    return 0;
}