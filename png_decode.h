#ifndef PNG_DECODE_H_
#define PNG_DECODE_H_

#include<stdio.h>

int png_decode(FILE * input, int frame, unsigned char ** output_array, int * width, int * height);

#endif /* PNG_DECODE_H_ */