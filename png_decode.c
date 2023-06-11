#include"png_decode.h"

#define DEBUG
#include"util_macros.h"
#include"util_stream.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>

#define FIXED_BUFFER_SIZE 4096
unsigned char fixed_buffer[FIXED_BUFFER_SIZE];
int buffer_read;

typedef struct _rgb_col {
    unsigned char rgb[3];
} rgb_col;
rgb_col PLTE[256];

unsigned char BITSCALE1[] = {0, 255};
unsigned char BITSCALE2[] = {0, 85, 170, 255};
unsigned char BITSCALE4[] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};

/* FRAME DATA */
struct _FRAME {
    int WIDTH,
        HEIGHT,
        BIT_DEPTH,
        COLOR_TYPE,
        COMPRESSION_METHOD,
        FILTER_METHOD,
        INTERLACE_METHOD;
    unsigned char * ptr;

    int y, x;
    int bpp;
    int interlace;
    int has_alpha;
    int cur_col; /* gray, r, g, b or alpha */
    int cur_col_sub; /* MSB or LSB for 16 bit sample */

    int width_interlace[8];
    int height_interlace[8];
    
    unsigned char * prev_scanline;
    unsigned char * scanline;
    unsigned char * dummy_scanline; /* swap */
    int scanline_sz;
    int scanline_i;
    int scanline_idx;
    int in_scanline;
    int scanline_filter_type;
    int end_of_scanline;
} FRAME;
/* END OF FRAME DATA */

unsigned char FRAME_bitscale(int depth, unsigned char v) {
    if (depth == 8) {
        return v;
    }
    else if (depth == 4) {
        return BITSCALE4[v];
    }
    else if (depth == 2) {
        return BITSCALE2[v];
    }
    else {
        return BITSCALE1[v];
    }
    
}

void FRAME_real_xy(int * real_y, int * real_x) {
    if (FRAME.interlace == 0) {
        *real_y = FRAME.y;
        *real_x = FRAME.x;
    }
    else if (FRAME.interlace == 1) {
        *real_y = FRAME.y * 8;
        *real_x = FRAME.x * 8;
    }
    else if (FRAME.interlace == 2) {
        *real_y = FRAME.y * 8;
        *real_x = 4 + FRAME.x * 8;
    }
    else if (FRAME.interlace == 3) {
        *real_y = 4 + FRAME.y * 8;
        *real_x = FRAME.x * 4;
    }
    else if (FRAME.interlace == 4) {
        *real_y = FRAME.y * 4;
        *real_x = 2 + FRAME.x * 4;
    }
    else if (FRAME.interlace == 5) {
        *real_y = 2 + FRAME.y * 4;
        *real_x = FRAME.x * 2;
    }
    else if (FRAME.interlace == 6) {
        *real_y = FRAME.y * 2;
        *real_x = 1 + FRAME.x * 2;
    }
    else if (FRAME.interlace == 7) {
        *real_y = 1 + FRAME.y * 2;
        *real_x = FRAME.x;
    }
}
void FRAME_step_xy() {
    if (FRAME.interlace == -1) return;
    FRAME.x += 1;
    if (FRAME.x >= FRAME.width_interlace[FRAME.interlace]) {
        FRAME.end_of_scanline = 1;
        FRAME.y += 1;
        FRAME.x = 0;
        while (FRAME.y >= FRAME.height_interlace[FRAME.interlace]) {
            if (0 < FRAME.interlace && FRAME.interlace < 7) FRAME.interlace++;
            else FRAME.interlace = -1;
            FRAME.y = 0;

            while ((FRAME.interlace != -1) && FRAME.x >= FRAME.width_interlace[FRAME.interlace]) {
                if (0 < FRAME.interlace && FRAME.interlace < 7) FRAME.interlace++;
                else FRAME.interlace = -1;
            }
        }
    }
}


int chunk_length;
unsigned char chunk_type[4];

/* HUFFMAN */
typedef struct _huffman_t {
    int val;
    int lr[2];
} huffman_t;
#define HUFFMAN_SAFESZ1 (1 << 12)
#define HUFFMAN_SAFESZ2 (1 << 10)
#define HUFFMAN_SZ 288
huffman_t huffman[HUFFMAN_SAFESZ1];
int huffman_codelen[HUFFMAN_SZ];
struct _huffman_ctrl {
    int idx, next;
} huffman_ctrl;
#define HUFFMAN_DIST_SZ 32
huffman_t huffman_dist[HUFFMAN_SAFESZ2];
int huffman_dist_codelen[HUFFMAN_DIST_SZ];
struct _huffman_ctrl huffman_dist_ctrl;
#define HUFFMAN_TABLE_SZ 19
huffman_t huffman_table[HUFFMAN_SAFESZ2];
int huffman_table_codelen[HUFFMAN_TABLE_SZ];
struct _huffman_ctrl huffman_table_ctrl;
char huffman_table_codelen_value[] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

void huffman_reset(int flag) {
    if (flag == 0) {
        for (int i = 0; i < HUFFMAN_SAFESZ1; i++) {
            huffman[i].val = huffman[i].lr[0] = huffman[i].lr[1] = -1;
        }
        for (int i = 0; i < HUFFMAN_SZ; i++) {
            huffman_codelen[i] = 0;
        }
        huffman_ctrl.idx = 0;
        huffman_ctrl.next = 1;
    }
    else if (flag == 1) {
        for (int i = 0; i < HUFFMAN_SAFESZ2; i++) {
            huffman_dist[i].val = huffman_dist[i].lr[0] = huffman_dist[i].lr[1] = -1;
        }
        for (int i = 0; i < HUFFMAN_DIST_SZ; i++) {
            huffman_dist_codelen[i] = 0;
        }
        huffman_dist_ctrl.idx = 0;
        huffman_dist_ctrl.next = 1;
    }
    else if (flag == 2) {
        for (int i = 0; i < HUFFMAN_SAFESZ2; i++) {
            huffman_table[i].val = huffman_table[i].lr[0] = huffman_table[i].lr[1] = -1;
        }
        for (int i = 0; i < HUFFMAN_TABLE_SZ; i++) {
            huffman_table_codelen[i] = 0;
        }
        huffman_table_ctrl.idx = 0;
        huffman_table_ctrl.next = 1;
    }
}

void huffman_build(int flag) {
    LOG("--zlib: huffman_build(%d)\n", flag);
    /* codelen has been filled */
    int bl_count[16];
    int next_code[16];
    
    int SZ = HUFFMAN_SZ;
    int *codelen_p = huffman_codelen;
    huffman_t * huffman_p = huffman;
    struct _huffman_ctrl * ctrl = &huffman_ctrl;
    
    if (flag == 1) {
        SZ = HUFFMAN_DIST_SZ;
        codelen_p = huffman_dist_codelen;
        huffman_p = huffman_dist;
        ctrl = &huffman_dist_ctrl;
    }
    else if (flag == 2) {
        SZ = HUFFMAN_TABLE_SZ;
        codelen_p = huffman_table_codelen;
        huffman_p = huffman_table;
        ctrl = &huffman_table_ctrl;
    }

    for (int i = 0; i < 16; i++) {
        bl_count[i] = 0;
    }
    next_code[0] = 0;

    for (int i = 0; i < SZ; i++) {
        bl_count[codelen_p[i]]++;
    }
    bl_count[0] = 0;

    for (int bits = 1, code = 0; bits < 16; bits++) {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }

    for (int i = 0, len = 0, code = 0, _c = 0, idx = 0; i < SZ; i++) {
        len = codelen_p[i];
        if (len == 0) continue;

        idx = 0;
        code = next_code[len]++;
        BIT_REFLECT(code, len);

        for (int j = 0; j < len; j++) {
            _c = code & 1;
            if (huffman_p[idx].lr[_c] == -1) {
                huffman_p[idx].lr[_c] = ctrl->next;
                idx = ctrl->next;
                ctrl->next += 1;
            }
            else {
                idx = huffman_p[idx].lr[_c];
            }
            code >>= 1;
        }

        huffman_p[idx].val = i;
    }

}

int huffman_trav(int b, int flag) {
    huffman_t * huff;
    struct _huffman_ctrl * ctrl;
    int val;

    if (flag == 0) {
        ctrl = &huffman_ctrl;
        huff = huffman;
    }
    else if (flag == 1) {
        ctrl = &huffman_dist_ctrl;
        huff = huffman_dist;
    }
    else if (flag == 2) {
        ctrl = &huffman_table_ctrl;
        huff = huffman_table;
    }

    ctrl->idx = huff[ctrl->idx].lr[b];
    val = huff[ctrl->idx].val;
    if (val != -1) {
        ctrl->idx = 0;
    }
    return val;
}
/* End of HUFFMAN */

/* crc32 routines */
uint32_t crc, computed_crc;
int crc_table_computed = 0;
uint32_t crc_table[256];
void crc_table_make() {
    uint32_t c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (uint32_t) n;
        for (k = 0; k < 8; k++) {
            if (c & 1) {
                c = 0xEDB88320 ^ (c >> 1);
            }
            else {
                c = c >> 1;
            }
        }
        crc_table[n] = c;
    }

    crc_table_computed = 1;
}

void crc_init() {
    if (!crc_table_computed) {
        crc_table_make();
    }

    computed_crc = (uint32_t)(~(uint32_t)0);
}
void crc_update(unsigned char b) {
    uint32_t c = computed_crc;
    computed_crc = crc_table[(c ^ b) & 0xFF] ^ (c >> 8);
}
uint32_t crc_current() {
    return computed_crc ^ (uint32_t)(~(uint32_t)0);
}
void crc_update_chunk(void * src, size_t len) {
    unsigned char * ptr = (unsigned char *)src;
    for (size_t i = 0; i < len; i++) {
        crc_update(*ptr++);
    }
}
/* End of crc32 routines */


/* zlib routines */
#define LZ77_WINDOW_SIZE 32768
unsigned char LZ77[LZ77_WINDOW_SIZE];
int LZ77_i;

enum ZLIB_STATE {
    WAIT_FOR_BITS,
    DECODE_NOCOMPRESS,
    DECODE
};
struct _zlib_struct {
    int state;
    unsigned char * buf;

    /* WAIT_FOR_BITS */
    int w4b_bit;
    int *w4b_p;
    int w4b_res;

    /* table parse */
    int tp;
    int tp_res;

    void (* update_func)();
    void (* post_update_func)();
    void (* rawdata_func)();
    unsigned char rawdat_input;
    
    /* ZLIB */
    int CMF, FLG;
    int DICTID, ADLER32;
    int sz, idx;
    int bitbuf, bitbuf_sz;
    int len, dist;

    /* DEFLATE */
    int BTYPE;
    int BFINAL;
    int LEN, NLEN, _LEN;

    /* HUFFMAN */
    int HLIT, HDIST, HCLEN;
    int _HLIT, _HDIST, _HCLEN;
    int table_i;
    int lit_i, dist_i;
    
    int extra, extra2, extra_flag;
} zlib_struct;
#define zlib_struct_szl (zlib_struct.sz - zlib_struct.idx)

void zlib_func_null() {
    return;
}

void zlib_func_IDAT() {
    int real_y, real_x;
    unsigned char dat = zlib_struct.rawdat_input;
    int _real_dat;
    unsigned char real_dat;
    int a, b, c, pp, p, pa, pb, pc;

    if (FRAME.in_scanline) {
        if (FRAME.scanline_filter_type == 0) {
            real_dat = dat;
        }
        else if (FRAME.scanline_filter_type == 1) {
            if (FRAME.scanline_idx < FRAME.bpp) {
                a = 0;
            }
            else {
                a = FRAME.scanline[FRAME.scanline_idx - FRAME.bpp];
            }
            _real_dat = (a + dat);
            real_dat = _real_dat % 256;
        }
        else if (FRAME.scanline_filter_type == 2) {
            if (FRAME.scanline_i == 0) {
                b = 0;
            }
            else {
                b = FRAME.prev_scanline[FRAME.scanline_idx];
            }
            _real_dat = (b + dat);
            real_dat = _real_dat % 256;
        }
        else if (FRAME.scanline_filter_type == 3) {
            if (FRAME.scanline_idx < FRAME.bpp) {
                a = 0;
            }
            else {
                a = FRAME.scanline[FRAME.scanline_idx - FRAME.bpp];
            }
            if (FRAME.scanline_i == 0) {
                b = 0;
            }
            else {
                b = FRAME.prev_scanline[FRAME.scanline_idx];
            }
            _real_dat = dat + (a + b) / 2;
            real_dat = _real_dat % 256;
        }
        else if (FRAME.scanline_filter_type == 4) {
            if (FRAME.scanline_idx < FRAME.bpp) {
                a = 0;
                c = 0;
            }
            else {
                a = FRAME.scanline[FRAME.scanline_idx - FRAME.bpp];
            }
            if (FRAME.scanline_i == 0) {
                b = 0;
                c = 0;
            }
            else {
                b = FRAME.prev_scanline[FRAME.scanline_idx];
            }

            if (FRAME.scanline_idx >= FRAME.bpp && FRAME.scanline_i > 0) {
                c = FRAME.prev_scanline[FRAME.scanline_idx - FRAME.bpp];
            }

            /* Paeth */
            p = a + b - c;
            pa = ABS(p-a);
            pb = ABS(p-b);
            pc = ABS(p-c);
            if (pa <= pb && pa <= pc) pp = a;
            else if (pb <= pc) pp = b;
            else pp = c;

            _real_dat = dat + pp;
            real_dat = _real_dat % 256;
        }
    }

    FRAME.scanline[FRAME.scanline_idx] = real_dat;

    if (FRAME.in_scanline == 0) {
        FRAME.in_scanline = 1;
        FRAME.scanline_filter_type = dat;
    }
    else {
        int step, len, mask, shift;
        if (FRAME.BIT_DEPTH == 1) {
            step = 8 / 1;
            len = 1;
            shift = 7;
            mask = QUICK_MASK[1] << 7;
        }
        else if (FRAME.BIT_DEPTH == 2) {
            step = 8 / 2;
            len = 2;
            shift = 6;
            mask = QUICK_MASK[2] << 6;
        }
        else if (FRAME.BIT_DEPTH == 4) {
            step = 8 / 4;
            len = 4;
            shift = 4;
            mask = QUICK_MASK[4] << 4;
        }
        else if (FRAME.BIT_DEPTH == 8) {
            step = 8 / 8;
            len = 8;
            shift = 0;
            mask = QUICK_MASK[8];
        }

        if (FRAME.BIT_DEPTH != 16) {
            for (int i = 0; i < step; i++) {
                FRAME_real_xy(&real_y, &real_x);

                if (FRAME.COLOR_TYPE == 3) { /* indexed image */
                    int color_idx = (real_dat & mask) >> shift;
                    for (int j = 0; j < 3; j++) {
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + j] = PLTE[color_idx].rgb[j];
                    }
                    FRAME_step_xy();
                    if (FRAME.end_of_scanline) break;
                }
                else if (BITx(FRAME.COLOR_TYPE, 2, 0) == 0) { /* grayscale */

                    unsigned char color_value = ((real_dat & mask) >> shift);
                    color_value = FRAME_bitscale(FRAME.BIT_DEPTH, color_value);
                    if (FRAME.cur_col == 1) {
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = color_value;
                        FRAME.cur_col = 0;
                        FRAME_step_xy();
                        if (FRAME.end_of_scanline) break;
                    }
                    else {
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 0] = color_value;
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 1] = color_value;
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 2] = color_value;

                        if (FRAME.has_alpha) {
                            FRAME.cur_col = 1;
                        }
                        else {
                            FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = 255;
                            FRAME_step_xy();
                            if (FRAME.end_of_scanline) break;
                        }
                    }
                }
                else {
                    unsigned char color_value = ((real_dat & mask) >> shift);
                    color_value = FRAME_bitscale(FRAME.BIT_DEPTH, color_value);
                    FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + FRAME.cur_col] = color_value;
                    
                    if (FRAME.cur_col == 3) {
                        FRAME.cur_col = 0;
                        FRAME_step_xy();
                        if (FRAME.end_of_scanline) break;
                    }
                    else if (FRAME.cur_col == 2) {
                        if (FRAME.has_alpha) {
                            FRAME.cur_col = 3;
                        }
                        else {
                            FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = 255;
                            FRAME.cur_col = 0;
                            FRAME_step_xy();
                            if (FRAME.end_of_scanline) break;
                        }
                    }
                    else {
                        FRAME.cur_col += 1;
                    }
                }
                mask >>= len;
                shift -= len;
            }
        }
        else { /* CANNOT BE INDEXED IMAGE */
            if (BITx(FRAME.COLOR_TYPE, 2, 0) == 0) { /* grayscale */
                if (FRAME.cur_col_sub == 0) {
                    FRAME.cur_col_sub = 1;
                    FRAME_real_xy(&real_y, &real_x);
                    if (FRAME.cur_col == 0) {
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 0] = real_dat;
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 1] = real_dat;
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 2] = real_dat;
                    }
                    else {
                        FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = real_dat;
                    }
                }
                else {
                    FRAME.cur_col_sub = 0;
                    if (FRAME.cur_col == 1) {
                        FRAME.cur_col = 0;
                        FRAME_step_xy();
                    }
                    else {
                        if (FRAME.has_alpha) {
                            FRAME.cur_col = 1;
                        }
                        else {
                            FRAME_real_xy(&real_y, &real_x);
                            FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = 255;
                            FRAME.cur_col = 0;
                            FRAME_step_xy();
                        }
                    }
                }
            }
            else {
                if (FRAME.cur_col_sub == 0) {
                    FRAME.cur_col_sub = 1;
                    FRAME_real_xy(&real_y, &real_x);
                    FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + FRAME.cur_col] = real_dat;
                }
                else {
                    FRAME.cur_col_sub = 0;
                    if (FRAME.cur_col == 3) {
                        FRAME.cur_col = 0;
                        FRAME_step_xy();
                    }
                    else if (FRAME.cur_col == 2) {
                        if (FRAME.has_alpha) {
                            FRAME.cur_col = 3;
                        }
                        else {
                            FRAME_real_xy(&real_y, &real_x);
                            FRAME.ptr[4 * (FRAME.WIDTH * real_y + real_x) + 3] = 255;
                            FRAME.cur_col = 0;
                            FRAME_step_xy();
                        }
                    }
                    else {
                        FRAME.cur_col += 1;
                    }
                }
            }
        }
    }

    if (FRAME.end_of_scanline) {
        FRAME.end_of_scanline = 0;
        FRAME.in_scanline = 0;

        FRAME.dummy_scanline = FRAME.scanline;
        FRAME.scanline = FRAME.prev_scanline;
        FRAME.prev_scanline = FRAME.dummy_scanline;
        FRAME.scanline_i += 1;
        FRAME.scanline_idx = 0;
    }
    else {
        FRAME.scanline_idx += 1;
    }
    
}

void zlib_setup_rawdata_func(unsigned char b) {
    zlib_struct.rawdat_input = b;
}

void zlib_func_getbit() {
    while (zlib_struct.bitbuf_sz < zlib_struct.w4b_bit) {
        if (zlib_struct_szl >= 1) {
            zlib_struct.bitbuf |= zlib_struct.buf[zlib_struct.idx++] << zlib_struct.bitbuf_sz;
            zlib_struct.bitbuf_sz += 8;
        }
        else break;
    }
    if (zlib_struct.bitbuf_sz < zlib_struct.w4b_bit) {
        zlib_struct.w4b_res = 0;   
        return;
    }

    *zlib_struct.w4b_p = BITx(zlib_struct.bitbuf, zlib_struct.w4b_bit, 0);
    zlib_struct.bitbuf >>= zlib_struct.w4b_bit;
    zlib_struct.bitbuf_sz -= zlib_struct.w4b_bit;
    zlib_struct.w4b_res = 1;
}

void zlib_setup_getbit(int bit, int * dst) {
    zlib_struct.w4b_p = dst;
    zlib_struct.w4b_bit = bit;
}

void zlib_func_huffman_trav() {
    while (zlib_struct.bitbuf_sz < 1) {
        if (zlib_struct_szl >= 1) {
            zlib_struct.bitbuf |= zlib_struct.buf[zlib_struct.idx++] << zlib_struct.bitbuf_sz;
            zlib_struct.bitbuf_sz += 8;
        }
    }
    if (zlib_struct.bitbuf_sz < 1) {
        zlib_struct.tp_res = -1;
        return;
    }

    zlib_struct.tp_res = huffman_trav(zlib_struct.bitbuf & 1, zlib_struct.tp);
    zlib_struct.bitbuf >>= 1;
    zlib_struct.bitbuf_sz -= 1;
}

void zlib_setup_huffman_trav(int flag) {
    zlib_struct.tp = flag;
}

void zlib_init() {
    zlib_struct.CMF = zlib_struct.FLG = 0;
    zlib_struct.DICTID = 0;
    zlib_struct.ADLER32 = 1;
    zlib_struct.sz = zlib_struct.idx = 0;
    zlib_struct.bitbuf = zlib_struct.bitbuf_sz = 0;

    zlib_struct.BTYPE = 4;
    zlib_struct.BFINAL = 0;
    zlib_struct.LEN = zlib_struct.NLEN = 0;
    zlib_struct.HLIT = 0;
    zlib_struct.HDIST = 0;
    zlib_struct.HCLEN = 0;

    zlib_struct.update_func = &zlib_func_null;
    zlib_struct.post_update_func = &zlib_func_null;
    zlib_struct.rawdata_func = &zlib_func_null;

    LZ77_i = 0;
    zlib_struct.state = 0;
}

void zlib_adler32_u(unsigned char b) {
    unsigned long s1 = zlib_struct.ADLER32 & 0xffff;
    unsigned long s2 = (zlib_struct.ADLER32 >> 16) & 0xffff;

    s1 = (s1 + b) % 65521;
    s2 = (s2 + s1) % 65521;

    zlib_struct.ADLER32 = (s2 << 16) + s1;
}

void zlib_check32() {
    if (zlib_struct.w4b_res) {
        _revBytes32(zlib_struct.extra);
        LOG("--zlib: ADLER32: %08X - %08X\n", zlib_struct.ADLER32, zlib_struct.extra);
        LOG("--zlib: byte_left: %d\n", zlib_struct_szl);
        zlib_struct.state = 1;
        zlib_struct.update_func = &zlib_func_null;
        zlib_struct.post_update_func = &zlib_func_null;
    }
}

void zlib_get32() {
    int trail = zlib_struct.bitbuf_sz % 8; /* flush byte */
    zlib_struct.bitbuf >>= trail;
    zlib_struct.bitbuf_sz -= trail;
    zlib_setup_getbit(32, &zlib_struct.extra);
    zlib_struct.update_func = &zlib_func_getbit;
    zlib_struct.post_update_func = &zlib_check32;
}

void zlib_get_bfinal(); /* declaration */

void zlib_check_end_block() {
    if (zlib_struct.BFINAL) {
        zlib_get32();
    }
    else {
        zlib_get_bfinal();
    }
}

void zlib_get_rawdata_post() {
    if (zlib_struct.w4b_res) {
        LZ77[LZ77_i] = zlib_struct.extra;
        zlib_adler32_u(zlib_struct.extra);
        zlib_setup_rawdata_func(zlib_struct.extra);
        zlib_struct.rawdata_func();
        LZ77_i = (LZ77_i + 1) % LZ77_WINDOW_SIZE;
        zlib_struct._LEN++;
    }

    if (zlib_struct._LEN == zlib_struct.LEN) {
        zlib_check_end_block();
    }
}

void zlib_get_rawdata() {
    zlib_struct._LEN = 0;
    zlib_setup_getbit(8, &zlib_struct.extra);
    zlib_struct.post_update_func = &zlib_get_rawdata_post;
}

void zlib_get_NLEN_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: LEN: %X / NLEN: %X\n", zlib_struct.LEN, zlib_struct.NLEN);
        zlib_get_rawdata();
    }
}

void zlib_get_NLEN() {
    zlib_setup_getbit(16, &zlib_struct.NLEN);
    zlib_struct.post_update_func = &zlib_get_NLEN_post;
}

void zlib_get_LEN_post() {
    if (zlib_struct.w4b_res) {
        zlib_get_NLEN();
    }
}

void zlib_get_LEN() {
    int trail = zlib_struct.bitbuf_sz % 8; /* flush byte */
    zlib_struct.bitbuf >>= trail;
    zlib_struct.bitbuf_sz -= trail;
    zlib_setup_getbit(16, &zlib_struct.LEN);
    zlib_struct.update_func = &zlib_func_getbit;
    zlib_struct.post_update_func = &zlib_get_LEN_post;
}

void zlib_get_compress_data_post() {
    if (zlib_struct.extra_flag == 0) {
        if (zlib_struct.tp_res != -1) {
            int code = zlib_struct.tp_res;
            if (0 <= code && code <= 255) {
                zlib_struct.len = 0;
                zlib_struct.dist = code;
                zlib_struct.update_func = &zlib_func_null;
                zlib_struct.extra_flag = 4;
            }
            else if (code == 256) {
                zlib_struct.update_func = &zlib_func_null;
                zlib_struct.post_update_func = &zlib_func_null;
                zlib_check_end_block();
            }
            else if (code <= 264) {
                zlib_struct.len = code - 257 + 3;
                zlib_setup_huffman_trav(1);
                zlib_struct.update_func = &zlib_func_huffman_trav;
                zlib_struct.extra_flag = 2;
            }
            else if (code <= 268) {
                zlib_struct.extra = 11 + 2 * (code - 265);
                zlib_setup_getbit(1, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code <= 272) {
                zlib_struct.extra = 19 + 4 * (code - 269);
                zlib_setup_getbit(2, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code <= 276) {
                zlib_struct.extra = 35 + 8 * (code - 273);
                zlib_setup_getbit(3, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code <= 280) {
                zlib_struct.extra = 67 + 16 * (code - 277);
                zlib_setup_getbit(4, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code <= 284) {
                zlib_struct.extra = 131 + 32 * (code - 281);
                zlib_setup_getbit(5, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code == 285) {
                zlib_struct.len = 258;
                zlib_setup_huffman_trav(1);
                zlib_struct.update_func = &zlib_func_huffman_trav;
                zlib_struct.extra_flag = 2;
            }
        }
    }
    else if (zlib_struct.extra_flag == 1) {
        if (zlib_struct.w4b_res) {
            zlib_struct.len = zlib_struct.extra + zlib_struct.extra2;
            zlib_setup_huffman_trav(1);
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 2;
        }
    }
    else if (zlib_struct.extra_flag == 2) {
        if (zlib_struct.tp_res != -1) {
            int code = zlib_struct.tp_res;
            if (0 <= code && code <= 3) {
                zlib_struct.dist = code+1;
                zlib_struct.update_func = &zlib_func_null;
                zlib_struct.extra_flag = 4;
            }
            else if (code <= 5) {
                zlib_struct.extra = 5 + 2 * (code - 4);
                zlib_setup_getbit(1, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 7) {
                zlib_struct.extra = 9 + 4 * (code - 6);
                zlib_setup_getbit(2, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 9) {
                zlib_struct.extra = 17 + 8 * (code - 8);
                zlib_setup_getbit(3, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 11) {
                zlib_struct.extra = 33 + 16 * (code - 10);
                zlib_setup_getbit(4, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 13) {
                zlib_struct.extra = 65 + 32 * (code - 12);
                zlib_setup_getbit(5, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 15) {
                zlib_struct.extra = 129 + 64 * (code - 14);
                zlib_setup_getbit(6, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 17) {
                zlib_struct.extra = 257 + 128 * (code - 16);
                zlib_setup_getbit(7, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 19) {
                zlib_struct.extra = 513 + 256 * (code - 18);
                zlib_setup_getbit(8, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 21) {
                zlib_struct.extra = 1025 + 512 * (code - 20);
                zlib_setup_getbit(9, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 23) {
                zlib_struct.extra = 2049 + 1024 * (code - 22);
                zlib_setup_getbit(10, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 25) {
                zlib_struct.extra = 4097 + 2048 * (code - 24);
                zlib_setup_getbit(11, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 27) {
                zlib_struct.extra = 8193 + 4096 * (code - 26);
                zlib_setup_getbit(12, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
            else if (code <= 29) {
                zlib_struct.extra = 16385 + 8192 * (code - 28);
                zlib_setup_getbit(13, &zlib_struct.extra2);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
        }
    }
    else if (zlib_struct.extra_flag == 3) {
        if (zlib_struct.w4b_res) {
            zlib_struct.dist = zlib_struct.extra + zlib_struct.extra2;
            zlib_struct.update_func = &zlib_func_null;
            zlib_struct.extra_flag = 4;
        }
    }
    else if (zlib_struct.extra_flag == 4) {
        if (zlib_struct.len == 0) {
            LZ77[LZ77_i] = zlib_struct.dist;
            zlib_adler32_u(LZ77[LZ77_i]);
            zlib_setup_rawdata_func(LZ77[LZ77_i]);
            zlib_struct.rawdata_func();
            LZ77_i = (LZ77_i + 1) % LZ77_WINDOW_SIZE;
        }
        else {
            int o = (LZ77_i + LZ77_WINDOW_SIZE - zlib_struct.dist) % LZ77_WINDOW_SIZE;
            for (int i = 0; i < zlib_struct.len; i++) {
                LZ77[LZ77_i] = LZ77[o];
                zlib_adler32_u(LZ77[o]);
                zlib_setup_rawdata_func(LZ77[o]);
                zlib_struct.rawdata_func();
                LZ77_i = (LZ77_i + 1) % LZ77_WINDOW_SIZE;
                o = (o + 1) % LZ77_WINDOW_SIZE;
            }
        }

        zlib_setup_huffman_trav(0);
        zlib_struct.update_func = &zlib_func_huffman_trav;
        zlib_struct.extra_flag = 0;
    }
}

void zlib_get_compress_data() {
    zlib_setup_huffman_trav(0);
    zlib_struct.extra_flag = 0;
    zlib_struct.update_func = &zlib_func_huffman_trav;
    zlib_struct.post_update_func = &zlib_get_compress_data_post;
}

void zlib_get_HDIST_codelen_post() {
    if (zlib_struct.extra_flag == 0) {
        if (zlib_struct.tp_res != -1) {
            int code = zlib_struct.tp_res;

            if (0 <= code && code <= 15) {
                huffman_dist_codelen[zlib_struct.dist_i++] = code;
                zlib_struct._HDIST++;
            }
            else if (code == 16) {
                zlib_setup_getbit(2, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code == 17) {
                zlib_setup_getbit(3, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 2;
            }
            else if (code == 18) {
                zlib_setup_getbit(7, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
        }
    }
    else if (zlib_struct.extra_flag == 1) {
        if (zlib_struct.w4b_res) {
            int last;
            if (zlib_struct.dist_i == 0) {
                last = zlib_struct.lit_i - 1;
            }
            else {
                last = zlib_struct.dist_i - 1;
            }
            for (int i = 0; i < zlib_struct.extra + 3; i++) {
                huffman_dist_codelen[zlib_struct.dist_i++] = huffman_dist_codelen[last];
                zlib_struct._HDIST++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }
    else if (zlib_struct.extra_flag == 2) {
        if (zlib_struct.w4b_res) {
            for (int i = 0; i < zlib_struct.extra + 3; i++) {
                huffman_dist_codelen[zlib_struct.dist_i++] = 0;
                zlib_struct._HDIST++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }
    else if (zlib_struct.extra_flag == 3) {
        if (zlib_struct.w4b_res) {
            for (int i = 0; i < zlib_struct.extra + 11; i++) {
                huffman_dist_codelen[zlib_struct.dist_i++] = 0;
                zlib_struct._HDIST++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }

    if (zlib_struct._HDIST == zlib_struct.HDIST + 1) {
        huffman_build(1);
        zlib_get_compress_data();
    }
}

void zlib_get_HDIST_codelen() {
    zlib_setup_huffman_trav(2);
    zlib_struct.dist_i = 0;
    zlib_struct._HDIST = 0;
    zlib_struct.update_func = &zlib_func_huffman_trav;
    zlib_struct.extra_flag = 0;
    zlib_struct.post_update_func = &zlib_get_HDIST_codelen_post;
}

void zlib_get_HLIT_codelen_post() {
    if (zlib_struct.extra_flag == 0) {
        if (zlib_struct.tp_res != -1) {
            int code = zlib_struct.tp_res;

            if (0 <= code && code <= 15) {
                huffman_codelen[zlib_struct.lit_i++] = code;
                zlib_struct._HLIT++;
            }
            else if (code == 16) {
                zlib_setup_getbit(2, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 1;
            }
            else if (code == 17) {
                zlib_setup_getbit(3, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 2;
            }
            else if (code == 18) {
                zlib_setup_getbit(7, &zlib_struct.extra);
                zlib_struct.update_func = &zlib_func_getbit;
                zlib_struct.extra_flag = 3;
            }
        }
    }
    else if (zlib_struct.extra_flag == 1) {
        if (zlib_struct.w4b_res) {
            int last = zlib_struct.lit_i - 1;
            for (int i = 0; i < zlib_struct.extra + 3; i++) {
                huffman_codelen[zlib_struct.lit_i++] = huffman_codelen[last];
                zlib_struct._HLIT++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }
    else if (zlib_struct.extra_flag == 2) {
        if (zlib_struct.w4b_res) {
            for (int i = 0; i < zlib_struct.extra + 3; i++) {
                huffman_codelen[zlib_struct.lit_i++] = 0;
                zlib_struct._HLIT++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }
    else if (zlib_struct.extra_flag == 3) {
        if (zlib_struct.w4b_res) {
            for (int i = 0; i < zlib_struct.extra + 11; i++) {
                huffman_codelen[zlib_struct.lit_i++] = 0;
                zlib_struct._HLIT++;
            }
            zlib_struct.update_func = &zlib_func_huffman_trav;
            zlib_struct.extra_flag = 0;
        }
    }

    if (zlib_struct._HLIT == zlib_struct.HLIT + 257) {
        huffman_build(0);
        zlib_get_HDIST_codelen();
    }
}

void zlib_get_HLIT_codelen() {
    zlib_setup_huffman_trav(2);
    zlib_struct.lit_i = 0;
    zlib_struct._HLIT = 0;
    zlib_struct.update_func = &zlib_func_huffman_trav;
    zlib_struct.extra_flag = 0;
    zlib_struct.post_update_func = &zlib_get_HLIT_codelen_post;
}

void zlib_get_table_codelen_post() {
    if (zlib_struct.w4b_res) {
        zlib_struct.table_i += 1;
        zlib_struct._HCLEN += 1;

        if (zlib_struct.table_i < HUFFMAN_TABLE_SZ) {
            zlib_setup_getbit(3, &huffman_table_codelen[
                huffman_table_codelen_value[zlib_struct.table_i]
            ]);
        }
    }

    if (zlib_struct._HCLEN == zlib_struct.HCLEN + 4) {
        huffman_build(2);
        zlib_get_HLIT_codelen();
    }
}

void zlib_get_table_codelen() {
    zlib_setup_getbit(3, &huffman_table_codelen[
        huffman_table_codelen_value[zlib_struct.table_i]
    ]);
    zlib_struct.post_update_func = &zlib_get_table_codelen_post;
}

void zlib_get_HCLEN_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: HCLEN: %d\n", zlib_struct.HCLEN);
        zlib_struct.table_i = 0;
        zlib_struct._HCLEN = 0;
        zlib_get_table_codelen();
    }
}

void zlib_get_HCLEN() {
    zlib_setup_getbit(4, &zlib_struct.HCLEN);
    zlib_struct.post_update_func = &zlib_get_HCLEN_post;
}

void zlib_get_HDIST_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: HDIST: %d\n", zlib_struct.HDIST);
        zlib_get_HCLEN();
    }
}

void zlib_get_HDIST() {
    zlib_setup_getbit(5, &zlib_struct.HDIST);
    zlib_struct.post_update_func = &zlib_get_HDIST_post;
}

void zlib_get_HLIT_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: HLIT: %d\n", zlib_struct.HLIT);
        zlib_get_HDIST();
    }
}

void zlib_get_HLIT() {
    zlib_setup_getbit(5, &zlib_struct.HLIT);
    zlib_struct.post_update_func = &zlib_get_HLIT_post;
}

void zlib_get_btype_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: BTYPE: %d\n", zlib_struct.BTYPE);
        if (zlib_struct.BTYPE == 0) {
            /* parse raw data */
            zlib_get_LEN();
        }
        else if (zlib_struct.BTYPE == 1) {
            huffman_reset(0);
            huffman_reset(1);
            
            for (int i = 0; i <= 143; i++) huffman_codelen[i] = 8;
            for (int i = 144; i <= 255; i++) huffman_codelen[i] = 9;
            for (int i = 256; i <= 279; i++) huffman_codelen[i] = 7;
            for (int i = 280; i <= 287; i++) huffman_codelen[i] = 8;

            for (int i = 0; i <= 31; i++) huffman_dist_codelen[i] = 5;

            huffman_build(0);
            huffman_build(1);

            zlib_get_compress_data();
        }
        else if (zlib_struct.BTYPE == 2) {
            huffman_reset(0);
            huffman_reset(1);
            huffman_reset(2);

            /* parse table */
            zlib_struct._HLIT = 0;
            zlib_get_HLIT();
        }
    }
}

void zlib_get_btype() {
    zlib_setup_getbit(2, &zlib_struct.BTYPE);
    zlib_struct.post_update_func = &zlib_get_btype_post;
}

void zlib_get_bfinal_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: BFINAL: %d\n", zlib_struct.BFINAL);
        zlib_get_btype();
    }
}

void zlib_get_bfinal() {
    zlib_setup_getbit(1, &zlib_struct.BFINAL);
    zlib_struct.update_func = &zlib_func_getbit;
    zlib_struct.post_update_func = &zlib_get_bfinal_post;
}

void zlib_get_DICTID_post() {
    if (zlib_struct.w4b_res) {
        zlib_get_bfinal();
    }
}

void zlib_get_DICTID() {
    zlib_setup_getbit(32, &zlib_struct.DICTID);
    zlib_struct.post_update_func = &zlib_get_DICTID_post;
}

void zlib_get_FLG_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: FLG: %X\n", zlib_struct.FLG);

        if (BITx(zlib_struct.FLG, 1, 5)) {
            zlib_get_DICTID();
        }
        else {
            zlib_get_bfinal();
        }
    }
}

void zlib_get_FLG() {
    zlib_setup_getbit(8, &zlib_struct.FLG);
    zlib_struct.post_update_func = &zlib_get_FLG_post;
}

void zlib_get_CMF_post() {
    if (zlib_struct.w4b_res) {
        LOG("--zlib: CMF: %X\n", zlib_struct.CMF);
        zlib_get_FLG();
    }
}

void zlib_get_CMF() {
    zlib_setup_getbit(8, &zlib_struct.CMF);
    zlib_struct.update_func = &zlib_func_getbit;
    zlib_struct.post_update_func = &zlib_get_CMF_post;
}

void zlib_read(unsigned char * buf, int sz) {
    zlib_struct.buf = buf;
    zlib_struct.idx = 0;
    zlib_struct.sz = sz;

    while ((zlib_struct.state != 1) && (zlib_struct_szl > 0)) {
        zlib_struct.update_func();
        zlib_struct.post_update_func();
    }
}

/* End of zlib routines */

int png_decode(FILE * input, int frame, unsigned char ** output_array, int * width, int * height) {
    stream_init(FIXED_BUFFER_SIZE, input);
    zlib_init();
    zlib_get_CMF();

    LOG("Setting up Data\n");
    FRAME.WIDTH = 0;
    FRAME.HEIGHT = 0;
    FRAME.BIT_DEPTH = 0;
    FRAME.COLOR_TYPE = 0;
    FRAME.COMPRESSION_METHOD = 0;
    FRAME.FILTER_METHOD = 0;
    FRAME.INTERLACE_METHOD = 0;

    FRAME.scanline = NULL;
    FRAME.prev_scanline = NULL;
    *output_array = NULL;

    LOG("Header\n");
    buffer_read = stream_cpy(fixed_buffer, 8);
    if (buffer_read != 8) return 0;
    if (memcmp(fixed_buffer, "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a", 8) != 0) return 0;

    while (1) {
        chunk_length = 0;
        buffer_read = stream_cpy(&chunk_length, 4);
        if (buffer_read != 4) return 0;
        _revBytes32(chunk_length);
        LOG("Chunk Length: %d\n", chunk_length);

        crc_init();
        buffer_read = stream_cpy(chunk_type, 4);
        if (buffer_read != 4) return 0;
        LOG("Chunk Type: %.*s\n", 4, chunk_type);
        LOG("Ancillary bit: %d\n", BITx(chunk_type[0], 1, 5));
        LOG("Private bit: %d\n", BITx(chunk_type[1], 1, 5));
        LOG("Reserved bit: %d\n", BITx(chunk_type[2], 1, 5));
        LOG("Safe-to-copy bit: %d\n", BITx(chunk_type[3], 1, 5));
        for (int i = 0; i < 4; i++) {
            crc_update(chunk_type[i]);
        }

        int bytes_to_read = chunk_length;
        if (memcmp(chunk_type, "IHDR", 4) == 0) {
            buffer_read = stream_cpy(fixed_buffer, 13);
            if (buffer_read != 13) return 0;
            crc_update_chunk(fixed_buffer, 13);

            memcpy(&FRAME.WIDTH, fixed_buffer, 4);
            _revBytes32(FRAME.WIDTH);
            memcpy(&FRAME.HEIGHT, fixed_buffer + 4, 4);
            _revBytes32(FRAME.HEIGHT);
            FRAME.BIT_DEPTH = fixed_buffer[8];
            FRAME.COLOR_TYPE = fixed_buffer[9];
            FRAME.COMPRESSION_METHOD = fixed_buffer[10];
            FRAME.FILTER_METHOD = fixed_buffer[11];
            FRAME.INTERLACE_METHOD = fixed_buffer[12];

            LOG("WIDTH: %d\n", FRAME.WIDTH);
            LOG("HEIGHT: %d\n", FRAME.HEIGHT);
            LOG("FRAME.BIT_DEPTH: %d\n", FRAME.BIT_DEPTH);
            LOG("FRAME.COLOR_TYPE: %d\n", FRAME.COLOR_TYPE);
            LOG("FRAME.COMPRESSION_METHOD: %d\n", FRAME.COMPRESSION_METHOD);
            LOG("FRAME.FILTER_METHOD: %d\n", FRAME.FILTER_METHOD);
            LOG("FRAME.INTERLACE_METHOD: %d\n", FRAME.INTERLACE_METHOD);

            FRAME.y = FRAME.x = FRAME.in_scanline = FRAME.scanline_i = 0;
            FRAME.has_alpha = BITx(FRAME.COLOR_TYPE, 1, 2); 
            int color_type = BITx(FRAME.COLOR_TYPE, 2, 0); /* 0: greyscale, 2: true, 3: indexed */
            int bit_per_pix;
            bit_per_pix = (color_type == 0) ? FRAME.BIT_DEPTH : 
                ((color_type == 2) ? FRAME.BIT_DEPTH * 3 : 8);
            bit_per_pix += FRAME.has_alpha * FRAME.BIT_DEPTH;
            FRAME.bpp = MAX(1, bit_per_pix / 8);
            LOG("bpp: %d\n", FRAME.bpp);
            
            FRAME.cur_col = FRAME.cur_col_sub = 0;

            FRAME.width_interlace[0] = FRAME.WIDTH;
            FRAME.height_interlace[0] = FRAME.HEIGHT;
            FRAME.width_interlace[1] = (FRAME.WIDTH + 7) / 8;
            FRAME.height_interlace[1] = (FRAME.HEIGHT + 7) / 8;
            FRAME.width_interlace[2] = (FRAME.WIDTH - 4 + 7) / 8;
            FRAME.height_interlace[2] = (FRAME.HEIGHT + 7) / 8;
            FRAME.width_interlace[3] = (FRAME.WIDTH + 3) / 4;
            FRAME.height_interlace[3] = (FRAME.HEIGHT - 4 + 7) / 8;
            FRAME.width_interlace[4] = (FRAME.WIDTH - 2 + 3) / 4;
            FRAME.height_interlace[4] = (FRAME.HEIGHT + 3) / 4;
            FRAME.width_interlace[5] = (FRAME.WIDTH + 1) / 2;
            FRAME.height_interlace[5] = (FRAME.HEIGHT - 2 + 3) / 4;
            FRAME.width_interlace[6] = (FRAME.WIDTH - 1 + 1) / 2;
            FRAME.height_interlace[6] = (FRAME.HEIGHT + 1) / 2;
            FRAME.width_interlace[7] = FRAME.WIDTH;
            FRAME.height_interlace[7] = (FRAME.HEIGHT - 1 + 1) / 2;
            
            FRAME.scanline_sz = 1 + ((bit_per_pix * FRAME.WIDTH) + 7) / 8;
            FRAME.end_of_scanline = 0;
            
            if (FRAME.INTERLACE_METHOD) {
                FRAME.interlace = 1; /* 1 to 7 */   
            }
            else {
                FRAME.interlace = 0;
            }

            FRAME.scanline_idx = 0;

            FRAME.scanline = (unsigned char *)malloc(FRAME.scanline_sz);
            FRAME.prev_scanline = (unsigned char *)malloc(FRAME.scanline_sz);

            *output_array = (unsigned char *)malloc(4 * FRAME.WIDTH * FRAME.HEIGHT); /* 32 bit RGB, always*/
            if ((*output_array == NULL) || (FRAME.scanline == NULL) || (FRAME.prev_scanline == NULL)) {
                goto png_decode_cleanup2;
            }
            *width = FRAME.WIDTH;
            *height = FRAME.HEIGHT;

            FRAME.ptr = *output_array;
        }
        else if (memcmp(chunk_type, "PLTE", 4) == 0) {
            stream_cpy(PLTE, chunk_length);
            crc_update_chunk(PLTE, chunk_length);
        }
        else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            zlib_struct.rawdata_func = &zlib_func_IDAT;
            while (bytes_to_read) {
                int bsize = MIN(bytes_to_read, FIXED_BUFFER_SIZE);
                buffer_read = stream_cpy(fixed_buffer, bsize);
                if (buffer_read != bsize) return 0;

                crc_update_chunk(fixed_buffer, bsize);
                zlib_read(fixed_buffer, bsize);

                bytes_to_read -= bsize;
            }
        }
        else {
            while (bytes_to_read) {
                int bsize = MIN(bytes_to_read, FIXED_BUFFER_SIZE);
                buffer_read = stream_cpy(fixed_buffer, bsize);
                if (buffer_read != bsize) return 0;
                
                crc_update_chunk(fixed_buffer, bsize);

                bytes_to_read -= bsize;
            }
        }
        LOG("Computed CRC: %X\n", crc_current());

        crc = 0;
        buffer_read = stream_cpy(&crc, 4);
        if (buffer_read != 4) return 0;
        _revBytes32(crc);
        LOG("CRC: %X\n", crc);

        LOG("\n");
        if (memcmp(chunk_type, "IEND", 4) == 0) break;
    }

    goto png_decode_success;

/* Primitive error handling */
png_decode_cleanup2:
    if (*output_array != NULL) free(*output_array);
    if (FRAME.scanline != NULL) free(FRAME.scanline);
    if (FRAME.prev_scanline != NULL) free(FRAME.prev_scanline);

png_decode_cleanup1:
    *output_array = NULL;
    FRAME.scanline = NULL;
    FRAME.prev_scanline = NULL;
    stream_destroy();
    return 0;

png_decode_success:
    stream_destroy();
    free(FRAME.scanline); FRAME.scanline = NULL;
    free(FRAME.prev_scanline); FRAME.prev_scanline = NULL;
    LOG("PNG DECODE SUCCESS");
    return 1;
}