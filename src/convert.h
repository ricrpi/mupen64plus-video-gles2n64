#ifndef CONVERT_H
#define CONVERT_H

#include "Types.h"

extern const volatile unsigned char Five2Eight[32];
extern const volatile unsigned char Four2Eight[16];
extern const volatile unsigned char Three2Four[8];
extern const volatile unsigned char Three2Eight[8];
extern const volatile unsigned char Two2Eight[4];
extern const volatile unsigned char One2Four[2];
extern const volatile unsigned char One2Eight[2];

void UnswapCopy( void *src, void *dest, u32 numBytes );
void DWordInterleave( void *mem, u32 numDWords );
void QWordInterleave( void *mem, u32 numDWords );
u16 swapword( u16 value );

u16 RGBA8888_RGBA4444( u32 color );
u32 RGBA5551_RGBA8888( u16 color );
u16 RGBA5551_RGBA5551( u16 color );
u32 IA88_RGBA8888( u16 color );
u16 IA88_RGBA4444( u16 color );
u16 IA44_RGBA4444( u8 color );
u32 IA44_RGBA8888( u8 color );
u16 IA44_IA88( u8 color );
u16 IA31_RGBA4444( u8 color );
u16 IA31_IA88( u8 color );
u32 IA31_RGBA8888( u8 color );
u16 I8_RGBA4444( u8 color );
u32 I8_RGBA8888( u8 color );
u16 I4_RGBA4444( u8 color );
u8 I4_I8( u8 color );
u16 I4_IA88( u8 color );
u16 I8_IA88( u8 color );
u16 IA88_IA88( u16 color );
u32 I4_RGBA8888( u8 color );

#endif // CONVERT_H
