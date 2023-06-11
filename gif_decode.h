#ifndef GIF_DECODE_H_
#define GIF_DECODE_H_

#include<stdio.h>

int gif_decode(FILE * input, int frame, unsigned char ** output_array, int * width, int * height);

#endif /* GIF_DECODE_H_ */