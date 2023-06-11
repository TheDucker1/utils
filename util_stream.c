#include"util_stream.h"

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include"util_macros.h"

struct _stream_struct {
    size_t sz, idx;
    unsigned char * buf;
    FILE * s;
    int eof;
} _stream;

int STREAM_INIT = 0;

size_t _fread(void * _buffer, size_t sz, size_t count, FILE * stream) {
    unsigned char * buffer = (unsigned char * )_buffer;
    for (size_t i = 0; i < sz * count; i++) {
        int c = fgetc(stream);
        if (c == EOF) {
            LOG("_fread EOF!\n");
            return i;
        }
        if (_buffer != NULL) {
            *buffer = c;
            buffer++;
        }
    }

    return count * sz;
}

void stream_refill() {
    if (!STREAM_INIT) return;
    if (_stream.eof) return;

    LOG("| REFILL | \n");

    size_t i;
    for (i = 0; _stream.idx < _stream.sz; i++, _stream.idx++) {
        _stream.buf[i] = _stream.buf[_stream.idx];
    }

    if (!_stream.eof) {
        size_t x = _fread(_stream.buf + i, 1, _stream.sz - i, _stream.s);
        if (x < _stream.sz - i) _stream.eof = 1;
    }
    _stream.idx = 0;
}

int stream_init(size_t sz, FILE * s) {
    if (!STREAM_INIT) {
        _stream.sz = sz;
        _stream.idx = sz;
        _stream.buf = (unsigned char *)malloc(sz);
        if (_stream.buf == NULL) {
            return 0;
        }
        _stream.s = s;
        _stream.eof = 0;
        STREAM_INIT = 1;

        stream_refill();

        return 1;
    }
    return -1;
}

int stream_destroy() {
    if (STREAM_INIT) {
        free(_stream.buf);
        _stream.sz = 0;
        _stream.idx = 0;
        _stream.s = NULL;
    }
    return 1;
}

size_t stream_cpy(void * dst, size_t sz) {
    size_t _sz = sz;
    if (!STREAM_INIT) return 0;
    unsigned char * ptr = (unsigned char *) dst;
    while (sz) {
        size_t mem_to_cpy = MIN(sz, _stream.sz - _stream.idx);
        if (dst != NULL) {
            memcpy(ptr, _stream.buf + _stream.idx, mem_to_cpy);
            ptr += mem_to_cpy;
        }
        sz -= mem_to_cpy;
            
        _stream.idx += mem_to_cpy;
        if (_stream.idx == _stream.sz) {
            stream_refill();
        }
    }

    return _sz;
}
