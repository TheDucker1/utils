#ifndef UTIL_STREAM_H_
#define UTIL_STREAM_H_

#include<stdio.h>
#include<stdlib.h>

int stream_init(size_t sz, FILE * stream);
int stream_destroy();

size_t stream_cpy(void * dst, size_t sz);

#endif /* UTIL_STREAM_H_ */