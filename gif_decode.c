#include"gif_decode.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>

/* #define GIFDEBUG */

#ifdef GIFDEBUG
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#else
#define LOG(...)
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

const uint32_t QUICK_MASK[] = {
    (1 <<  0) - 1,
    (1 <<  1) - 1,
    (1 <<  2) - 1,
    (1 <<  3) - 1,
    (1 <<  4) - 1,
    (1 <<  5) - 1,
    (1 <<  6) - 1,
    (1 <<  7) - 1,
    (1 <<  8) - 1,
    (1 <<  9) - 1,
    (1 << 10) - 1,
    (1 << 11) - 1,
    (1 << 12) - 1,
    (1 << 13) - 1,
    (1 << 14) - 1,
    (1 << 15) - 1
};

typedef struct _rgb_col {
    unsigned char rgb[3];
} rgb_col;

#define BIT(l, n) ((uint32_t)(QUICK_MASK[l] << n))
#define BITx(x, l, n) ((x & BIT(l, n)) >> n)

#define FIXED_BUFFER_SIZE 256
unsigned char fixed_buffer[FIXED_BUFFER_SIZE];
int buffer_read;

uint32_t code_table[4096];
unsigned char codestream[4096];
unsigned char code_used[4096];

int WIDTH,
    HEIGHT,
    GLOBAL_COLOR_TABLE_FLAG, 
    COLOR_RESOLUTION, 
    SORT_FLAG, 
    SIZE_OF_GLOBAL_COLOR_TABLE,
    
    BACKGROUND_COLOR_INDEX,
    PIXEL_ASPECT_RATIO;

rgb_col GLOBAL_COLOR_TABLE[256];
rgb_col LOCAL_COLOR_TABLE[256];

/* Frame specific */
int FRAME_HAS_GRAPHIC_CONTROL;
int FRAME_DELAY_TIME; /* not used */
int FRAME_DISPOSAL_METHOD;
int FRAME_USER_INPUT_FLAG; /* not used */
int FRAME_TRANSPARENT_COLOR_FLAG;
int FRAME_TRANSPARENT_COLOR_INDEX;

int FRAME_LEFT_POSITION;
int FRAME_TOP_POSITION;
int FRAME_WIDTH;
int FRAME_HEIGHT;
int FRAME_LOCAL_COLOR_TABLE_FLAG;
int FRAME_INTERLACE_FLAG;
int FRAME_SORT_FLAG;
int FRAME_SIZE_OF_LOCAL_COLOR_TABLE;

void xy_roll(int *x, int *y, int width, int height, int interlace) {
    int add_y = 0;
    *x += 1;
    if (*x == width) {
        *x = 0;
        add_y = 1;
    }
    if (add_y) {
        if (interlace) {
            if (*y % 8 == 0) {
                *y += 8;
                goto xy_roll_update1;
            }
            else if (*y % 8 == 4) {
                *y += 8;
                goto xy_roll_update2;
            }
            else if (*y % 4 == 2) {
                *x += 4;
                goto xy_roll_update3;
            }
            else if (*y % 2 == 1) {
                *y += 2;
                goto xy_roll_update4;
            }
            else {
                xy_roll_update1:
                if (*y >= height) *y = 4;
                xy_roll_update2:
                if (*y >= height) *y = 2;
                xy_roll_update3:
                if (*y >= height) *y = 1;
                xy_roll_update4:
                if (*y >= height) *x = *y = -1;
            }
        }
        else {
            *y += 1;
            if (*y >= height) {
                *x = *y = -1;
            }
        }
    }
}

int gif_decode(FILE * input, int frame, unsigned char ** output_array, int * width, int * height) {

    LOG("Setting Up Data\n");
    int frame_counter = 0;
    FRAME_HAS_GRAPHIC_CONTROL = 0;
    FRAME_DELAY_TIME = 0;
    FRAME_DISPOSAL_METHOD = 0;
    FRAME_USER_INPUT_FLAG = 0;
    FRAME_TRANSPARENT_COLOR_FLAG = 0;
    FRAME_TRANSPARENT_COLOR_INDEX = 0;

    FRAME_LEFT_POSITION = 0;
    FRAME_TOP_POSITION = 0;
    FRAME_WIDTH = 0;
    FRAME_HEIGHT = 0;
    FRAME_LOCAL_COLOR_TABLE_FLAG = 0;
    FRAME_INTERLACE_FLAG = 0;
    FRAME_SORT_FLAG = 0;
    FRAME_SIZE_OF_LOCAL_COLOR_TABLE = 0;

    memset(GLOBAL_COLOR_TABLE, 0, sizeof(GLOBAL_COLOR_TABLE));
    memset(LOCAL_COLOR_TABLE, 0, sizeof(LOCAL_COLOR_TABLE));
    GLOBAL_COLOR_TABLE_FLAG = 0; 
    COLOR_RESOLUTION = 0; 
    SORT_FLAG = 0;
    SIZE_OF_GLOBAL_COLOR_TABLE = 0;
    BACKGROUND_COLOR_INDEX = 0;
    PIXEL_ASPECT_RATIO = 0;

    LOG("Header\n");
    buffer_read = fread(fixed_buffer, 1, 3, input);
    if (buffer_read != 3) return 0;
    if (memcmp(fixed_buffer, "GIF", 3) != 0) return 0;

    char VERSION[4];
    VERSION[3] = '\0';
    buffer_read = fread(VERSION, 1, 3, input);
    if (buffer_read != 3) return 0;
    LOG("Version: %s\n", VERSION);

    LOG("Logical Screen Descriptor\n");
    *width = 0;
    buffer_read = fread(width, 1, 2, input);
    if (buffer_read != 2) return 0;
    *height = 0;
    buffer_read = fread(height, 1, 2, input);
    if (buffer_read != 2) return 0;
    WIDTH = *width; HEIGHT = *height;
    LOG("Width x Height: %d x %d \n", WIDTH, HEIGHT);

    buffer_read = fread(fixed_buffer, 1, 1, input);
    if (buffer_read != 1) return 0;
    GLOBAL_COLOR_TABLE_FLAG = BITx(fixed_buffer[0], 1, 7);
    COLOR_RESOLUTION = BITx(fixed_buffer[0], 3, 4);
    SORT_FLAG = BITx(fixed_buffer[0], 1, 3);
    SIZE_OF_GLOBAL_COLOR_TABLE = BITx(fixed_buffer[0], 3, 0);
    LOG("Global Color Table Flag: %d\n", GLOBAL_COLOR_TABLE_FLAG);
    LOG("Color Resolution: %d\n", COLOR_RESOLUTION);
    LOG("Sort Flag: %d\n", SORT_FLAG);
    LOG("Size of Global Color Table: %d\n", SIZE_OF_GLOBAL_COLOR_TABLE);

    buffer_read = fread(&BACKGROUND_COLOR_INDEX, 1, 1, input);
    if (buffer_read != 1) return 0;
    buffer_read = fread(&PIXEL_ASPECT_RATIO, 1, 1, input);
    if (buffer_read != 1) return 0;
    LOG("Background Color Index: %d\n", BACKGROUND_COLOR_INDEX);
    LOG("Pixel Aspect Ratio: %d\n", PIXEL_ASPECT_RATIO);

    if (GLOBAL_COLOR_TABLE_FLAG) {
        LOG("Global Color Table\n");
        for (int i = 0; i < (int)(1 << (SIZE_OF_GLOBAL_COLOR_TABLE + 1)); i++) {
            buffer_read = fread(GLOBAL_COLOR_TABLE[i].rgb, 1, 3, input);
            if (buffer_read != 3) return 0;
        }
    }

    (*output_array) = (unsigned char *)malloc(sizeof(unsigned char) * (HEIGHT) * (WIDTH) * 4);
    if (*output_array == NULL) {
        goto gif_decode_cleanup2;
    }
    memset(*output_array, 0, sizeof(unsigned char) * (HEIGHT) * (WIDTH) * 4);

    unsigned char * back_frame = (unsigned char *)malloc(sizeof(unsigned char) * (HEIGHT) * (WIDTH));
    if (back_frame == NULL) {
        goto gif_decode_cleanup1;
    }
    memset(back_frame, 0, sizeof(unsigned char) * (HEIGHT) * (WIDTH));

    LOG("Rendering Frames\n");
    while (!feof(input)) {
        int block_size; /* read block or sub-blocks */

        buffer_read = fread(fixed_buffer, 1, 1, input);
        if (!buffer_read) continue;
        if (fixed_buffer[0] == 0x3B) {
            LOG("Trailer\n");
            break;
        }
        else if (fixed_buffer[0] == 0x2C) {
            LOG("Image Descriptor\n");

            buffer_read = fread(&FRAME_LEFT_POSITION, 1, 2, input);
            if (buffer_read != 2) goto gif_decode_cleanup1;
            buffer_read = fread(&FRAME_TOP_POSITION, 1, 2, input);
            if (buffer_read != 2) goto gif_decode_cleanup1;
            buffer_read = fread(&FRAME_WIDTH, 1, 2, input);
            if (buffer_read != 2) goto gif_decode_cleanup1;
            buffer_read = fread(&FRAME_HEIGHT, 1, 2, input);
            if (buffer_read != 2) goto gif_decode_cleanup1;

            buffer_read = fread(fixed_buffer, 1, 1, input);
            if (buffer_read != 1) goto gif_decode_cleanup1;
            FRAME_LOCAL_COLOR_TABLE_FLAG = BITx(fixed_buffer[0], 1, 7);
            FRAME_INTERLACE_FLAG = BITx(fixed_buffer[0], 1, 6);
            FRAME_SORT_FLAG = BITx(fixed_buffer[0], 1, 5);
            /* FRAME_RESERVED = BITx(fixed_buffer[0], 2, 3); */
            FRAME_SIZE_OF_LOCAL_COLOR_TABLE = BITx(fixed_buffer[0], 3, 0);

            LOG("FRAME LEFT POSITION: %d\n", FRAME_LEFT_POSITION);
            LOG("FRAME TOP POSITION: %d\n", FRAME_TOP_POSITION);
            LOG("FRAME WIDTH: %d\n", FRAME_WIDTH);
            LOG("FRAME HEIGHT: %d\n", FRAME_HEIGHT);

            LOG("FRAME LOCAL COLOR TABLE FLAG: %d\n", FRAME_LOCAL_COLOR_TABLE_FLAG);
            LOG("FRAME INTERLACE FLAG: %d\n", FRAME_INTERLACE_FLAG);
            LOG("FRAME SORT FLAG: %d\n", FRAME_SORT_FLAG);
            LOG("FRAME SIZE OF LOCAL COLOR TABLE: %d\n", FRAME_SIZE_OF_LOCAL_COLOR_TABLE);

            if (FRAME_LOCAL_COLOR_TABLE_FLAG) {
                LOG("Local Color Table\n");
                
                for (int i = 0; i < (int)(1 << (FRAME_SIZE_OF_LOCAL_COLOR_TABLE + 1)); i++) {
                    buffer_read = fread(LOCAL_COLOR_TABLE[i].rgb, 1, 3, input);
                    if (buffer_read != 3) goto gif_decode_cleanup1;
                }
            }

            LOG("Table Based Image Data\n");
            memset(back_frame, 0, sizeof(unsigned char) * (HEIGHT) * (WIDTH));

            int lzw_minimum_code_size = 0;
            buffer_read = fread(&lzw_minimum_code_size, 1, 1, input);
            lzw_minimum_code_size = MAX(2, lzw_minimum_code_size);
            if (buffer_read != 1) goto gif_decode_cleanup1;
            
            int x, y, real_x, real_y;
            x = y = 0;

            if (frame != -1 && frame_counter > frame) {
                /* skip frame */
                while (1) {
                    block_size = 0;
                    buffer_read = fread(&block_size, 1, 1, input);
                    if (buffer_read != 1) goto gif_decode_cleanup1;
                    if (block_size == 0) break;
                    if (fseek(input, block_size, SEEK_CUR)) goto gif_decode_cleanup1;
                }
            }
            else {
                rgb_col * color_table;
                if (FRAME_LOCAL_COLOR_TABLE_FLAG) {
                    color_table = LOCAL_COLOR_TABLE;
                }
                else {
                    color_table = GLOBAL_COLOR_TABLE;
                }
                
                int lzw_code_size = lzw_minimum_code_size+1;
                int CC = 1 << lzw_minimum_code_size; /* also root size */
                int EOI = CC + 1;
                int buf_size = 0;
                int init = 1;
                uint32_t read_code, buf, last_code;
                buf = 0;

                int block_idx;
                int codelen;

                for (int i = 0; i < CC; i++) {
                    unsigned char c = i;
                    code_table[i] = (c << 12) | c;
                    code_used[i] = 1;
                }
                for (int i = CC+2; i < 4096; i++) {
                    code_used[i] = 0;
                }
                int update_code = CC+2;

                while (1) {
                    block_size = 0;
                    block_idx = 0;

                    buffer_read = fread(&block_size, 1, 1, input);
                    if (buffer_read != 1) goto gif_decode_cleanup1;
                    if (block_size == 0) break;

                    buffer_read = fread(fixed_buffer, 1, block_size, input);
                    if (buffer_read != block_size) goto gif_decode_cleanup1;

                    while ((block_idx < block_size) || buf_size >= lzw_code_size) {
                        while (block_idx < block_size && buf_size < lzw_code_size) {
                            buf |= (uint32_t)(fixed_buffer[block_idx]) << buf_size;
                            buf_size += 8;
                            block_idx++;
                        }
                        if (buf_size < lzw_code_size) break;
                        
                        read_code = BITx(buf, lzw_code_size, 0);
                        buf >>= lzw_code_size;
                        buf_size -= lzw_code_size;

                        if (read_code == CC) {
                            lzw_code_size = lzw_minimum_code_size + 1;
                            update_code = CC+2;
                            init = 1;
                            for (int i = CC+2; i < 4096; i++) {
                                code_used[i] = 0;
                            }
                        }
                        else if (read_code == EOI) {
                            frame_counter += 1;
                            break;
                        }
                        else {
                            if (init) {
                                init = 0;
                                real_x = x + FRAME_LEFT_POSITION;
                                real_y = y + FRAME_TOP_POSITION;
                                back_frame[real_y * WIDTH + real_x] = read_code;
                                
                                codestream[0] = read_code;
                                codelen=1;
                            }
                            else {
                                uint32_t obj;

                                if (!code_used[read_code]) {
                                    /* NOT FOUND */
                                    codelen = 1;
                                    if (last_code >= CC) {
                                        obj = last_code;

                                        while (!(BITx(code_table[obj], 12, 0) < CC)) {
                                            codestream[codelen] = BITx(code_table[obj], 8, 12);
                                            codelen++;
                                            obj = BITx(code_table[obj], 12, 0);
                                        }
                                        codestream[codelen] = BITx(code_table[obj], 8, 12);
                                        codelen++;
                                        codestream[0] = codestream[codelen] = BITx(code_table[obj], 12, 0);
                                        codelen++;
                                    }
                                    else {
                                        codestream[0] = codestream[codelen] = last_code;
                                        codelen++;
                                    }
                                }
                                else {
                                    /* FOUND */
                                    codelen = 0;
                                    if (read_code >= CC) {
                                        obj = read_code;

                                        while (!(BITx(code_table[obj], 12, 0) < CC)) {
                                            codestream[codelen] = BITx(code_table[obj], 8, 12);
                                            codelen++;
                                            obj = BITx(code_table[obj], 12, 0);
                                        }
                                        codestream[codelen] = BITx(code_table[obj], 8, 12);
                                        codelen++;
                                        codestream[codelen] = BITx(code_table[obj], 12, 0);
                                        codelen++;
                                    }
                                    else {
                                        codestream[codelen] = read_code;
                                        codelen++;
                                    }
                                }

                                code_table[update_code] = (codestream[codelen-1] << 12) | last_code;
                                code_used[update_code] = 1;
                                if (update_code == (unsigned int)(1 << lzw_code_size) - 1) {
                                    lzw_code_size += 1;
                                    lzw_code_size = MIN(lzw_code_size, 12);
                                }
                                update_code += 1;
                            }

                            for (int i = codelen-1; i >= 0; i--) {
                                real_x = x + FRAME_LEFT_POSITION;
                                real_y = y + FRAME_TOP_POSITION;
                                back_frame[real_y * WIDTH + real_x] = codestream[i];

                                xy_roll(&x, &y, FRAME_WIDTH, FRAME_HEIGHT, FRAME_INTERLACE_FLAG);
                            }
                        }
                        last_code = read_code;
                    }
                }

                if (FRAME_HAS_GRAPHIC_CONTROL && frame_counter != frame-1) {
                    if (FRAME_DISPOSAL_METHOD == 0 || FRAME_DISPOSAL_METHOD == 1) {
                        for (real_y = FRAME_TOP_POSITION; real_y < FRAME_TOP_POSITION + FRAME_HEIGHT; real_y++) {
                            for (real_x = FRAME_LEFT_POSITION; real_x < FRAME_LEFT_POSITION + FRAME_WIDTH; real_x++) {
                                unsigned char color_idx = back_frame[real_y * WIDTH + real_x];
                                rgb_col col = color_table[color_idx];
                                if (FRAME_TRANSPARENT_COLOR_FLAG && 
                                    FRAME_TRANSPARENT_COLOR_INDEX == color_idx) continue;
                                memcpy((*output_array) + 4 * (real_y * WIDTH + real_x), col.rgb, 3);
                                (*output_array)[4 * (real_y * WIDTH + real_x) + 3] = 0xFF;
                            }
                        }
                    }
                    else if (FRAME_DISPOSAL_METHOD == 2) {
                        /* Restore to Background; not render instead of switch to background */
                    }
                    else if (FRAME_DISPOSAL_METHOD == 3) {
                        /* Restore To Previous; not render */
                    }
                }
                else {
                    for (real_y = FRAME_TOP_POSITION; real_y < FRAME_TOP_POSITION + FRAME_HEIGHT; real_y++) {
                        for (real_x = FRAME_LEFT_POSITION; real_x < FRAME_LEFT_POSITION + FRAME_WIDTH; real_x++) {
                            unsigned char color_idx = back_frame[real_y * WIDTH + real_x];
                            rgb_col col = color_table[color_idx];
                            if (FRAME_HAS_GRAPHIC_CONTROL && 
                                FRAME_TRANSPARENT_COLOR_FLAG && 
                                FRAME_TRANSPARENT_COLOR_INDEX == color_idx) continue;
                            memcpy((*output_array) + 4 * (real_y * WIDTH + real_x), col.rgb, 3);
                            (*output_array)[4 * (real_y * WIDTH + real_x) + 3] = 0xFF;
                        }
                    }
                }
            }
            
            FRAME_HAS_GRAPHIC_CONTROL = 0;
            FRAME_DELAY_TIME = 0;
            FRAME_DISPOSAL_METHOD = 0;
            FRAME_USER_INPUT_FLAG = 0;
            FRAME_TRANSPARENT_COLOR_FLAG = 0;
            FRAME_TRANSPARENT_COLOR_INDEX = 0;

            FRAME_LEFT_POSITION = 0;
            FRAME_TOP_POSITION = 0;
            FRAME_WIDTH = 0;
            FRAME_HEIGHT = 0;
            FRAME_LOCAL_COLOR_TABLE_FLAG = 0;
            FRAME_INTERLACE_FLAG = 0;
            FRAME_SORT_FLAG = 0;
            FRAME_SIZE_OF_LOCAL_COLOR_TABLE = 0;
        }
        else if (fixed_buffer[0] == 0x21) {
            buffer_read = fread(fixed_buffer, 1, 1, input);
            if (fixed_buffer[0] == 0x01) {
                LOG("Plain Text Extension\n");

                block_size = 0;
                buffer_read = fread(&block_size, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;
                if (block_size != 12) goto gif_decode_cleanup1;

                /* Skip a lot of data */
                buffer_read = fread(fixed_buffer, 1, 12, input);
                if (buffer_read != 12) goto gif_decode_cleanup1;

                block_size = 0;
                while (1) {
                    buffer_read = fread(&block_size, 1, 1, input);
                    if (buffer_read != 1) goto gif_decode_cleanup1;
                    if (block_size == 0) break;
                    if (fseek(input, block_size, SEEK_CUR)) goto gif_decode_cleanup1;
                }
            }
            else if (fixed_buffer[0] == 0xF9) {
                LOG("Graphic Control Extension\n");

                block_size = 0;
                buffer_read = fread(&block_size, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;
                if (block_size != 4) goto gif_decode_cleanup1;

                FRAME_HAS_GRAPHIC_CONTROL = 1;

                buffer_read = fread(fixed_buffer, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;
                FRAME_TRANSPARENT_COLOR_FLAG = BITx(fixed_buffer[0], 1, 0);
                FRAME_USER_INPUT_FLAG = BITx(fixed_buffer[0], 1, 1);
                FRAME_DISPOSAL_METHOD = BITx(fixed_buffer[0], 3, 2);

                buffer_read = fread(&FRAME_DELAY_TIME, 1, 2, input);
                if (buffer_read != 2) goto gif_decode_cleanup1;

                buffer_read = fread(&FRAME_TRANSPARENT_COLOR_INDEX, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;

                buffer_read = fread(fixed_buffer, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;
                if (fixed_buffer[0] != 0) goto gif_decode_cleanup1;

                LOG("FRAME TRANSPARENT COLOR FLAG: %d\n", FRAME_TRANSPARENT_COLOR_FLAG);
                LOG("FRAME USER INPUT FLAG: %d\n", FRAME_USER_INPUT_FLAG);
                LOG("FRAME DISPOSAL METHOD: %d\n", FRAME_DISPOSAL_METHOD);
                LOG("FRAME DELAY TIME: %d\n", FRAME_DELAY_TIME);
                LOG("FRAME TRANSPARENT COLOR INDEX: %d\n", FRAME_TRANSPARENT_COLOR_INDEX);
            }
            else if (fixed_buffer[0] == 0xFE) {
                LOG("Comment Extension\n");

                block_size = 0;
                while (1) {
                    buffer_read = fread(&block_size, 1, 1, input);
                    if (buffer_read != 1) goto gif_decode_cleanup1;
                    if (block_size == 0) break;
                    if (fseek(input, block_size, SEEK_CUR)) goto gif_decode_cleanup1;
                }
            }
            else if (fixed_buffer[0] == 0xFF) {
                LOG("Application Extension\n");

                block_size = 0;
                buffer_read = fread(&block_size, 1, 1, input);
                if (buffer_read != 1) goto gif_decode_cleanup1;
                if (block_size != 11) goto gif_decode_cleanup1;

                /* Skip a lot of data */
                buffer_read = fread(fixed_buffer, 1, 11, input);
                if (buffer_read != 11) goto gif_decode_cleanup1;

                block_size = 0;
                while (1) {
                    buffer_read = fread(&block_size, 1, 1, input);
                    if (buffer_read != 1) goto gif_decode_cleanup1;
                    if (block_size == 0) break;
                    if (fseek(input, block_size, SEEK_CUR)) goto gif_decode_cleanup1;
                }
            }
            else {
                fprintf(stderr, "Error: Not supported extension with label %X, aborting\n", fixed_buffer[0]);
                goto gif_decode_cleanup1;
            }

        }
        else {
            fprintf(stderr, "Error: Not supported block with separator %X, aborting\n", fixed_buffer[0]);
            goto gif_decode_cleanup1;
        }
    }

    LOG("GIF DECODE SUCCESS!\n");
    goto gif_decode_success;

/* Primitive error handling */
gif_decode_cleanup1:
    free(back_frame);

gif_decode_cleanup2:
    free(*output_array);
    *output_array = NULL;
    return 0;

/* End of primitive error handling */

gif_decode_success:
    free(back_frame);
    return 1;
}