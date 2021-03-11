/* https://bugs.kde.org/show_bug.cgi?id=432801 */

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#include "../../memcheck.h"

// This function fails when compiled on clang version 10 or greater with -O2.
// It's unused by the test but left here as a copy of the error in the bug
// report https://bugs.kde.org/show_bug.cgi?id=432801
void standalone() {
   struct sigaction act;
   if (sigaction(SIGTERM, 0, &act) == 1) {
      return;
   }
   if (sigaction(SIGTERM, 0, &act) == 1) {
      return;
   }

   char pattern[] = "\x1\x2\x3\x4\x5\x6\x7\x8\x9";
   const unsigned long plen = strlen(pattern);
   pattern[1] = 0;
   size_t hp=0;
   for (size_t i = 0; i < plen; ++i)
      hp += pattern[i];
   volatile int j = 0;
   if (j == hp % 10) {
      j++;
   }
   printf("%ld\n", hp);
}

typedef struct V64x2 {
   uint64_t w64[2];  /* Note: little-endian */
} V64x2;

typedef struct V32x4 {
   uint32_t w32[4];  /* Note: little-endian */
} V32x4;

typedef struct V16x8 {
   uint16_t w16[8];  /* Note: little-endian */
} V16x8;

typedef struct V8x16 {
   uint8_t w8[16];  /* Note: little-endian */
} V8x16;

static int cmpGT64Sx2(V64x2 x, V64x2 y)
{
   int result;
   __asm__("movups %1,%%xmm6\n"
           "\tmovups %2,%%xmm7\n"
           // order swapped for AT&T style which has destination second.
           "\tpcmpgtq %%xmm7,%%xmm6\n"
           "\tpextrb $0, %%xmm6, %0"
           : "=r" (result) : "m" (x), "m" (y) : "xmm6");
   return result;
}

static int cmpGT32Sx4(V32x4 x, V32x4 y)
{
   int result;
   __asm__("movups %1,%%xmm6\n"
           "\tmovups %2,%%xmm7\n"
           // order swapped for AT&T style which has destination second.
           "\tpcmpgtd %%xmm7,%%xmm6\n"
           "\tpextrb $0, %%xmm6, %0"
           : "=r" (result) : "m" (x), "m" (y) : "xmm6");
   return result;
}

static int cmpGT16Sx8(V16x8 x, V16x8 y)
{
   int result;
   __asm__("movups %1,%%xmm6\n"
           "\tmovups %2,%%xmm7\n"
           // order swapped for AT&T style which has destination second.
           "\tpcmpgtw %%xmm7,%%xmm6\n"
           "\tpextrb $0, %%xmm6, %0"
           : "=r" (result) : "m" (x), "m" (y) : "xmm6");
   return result;
}

static int cmpGT8Sx16(V8x16 x, V8x16 y)
{
   int result;
   __asm__("movups %1,%%xmm6\n"
           "\tmovups %2,%%xmm7\n"
           // order swapped for AT&T style which has destination second.
           "\tpcmpgtb %%xmm7,%%xmm6\n"
           "\tpextrb $0, %%xmm6, %0"
           : "=r" (result) : "m" (x), "m" (y) : "xmm6");
   return result;
}

/* Set the V bits on the data at "addr".  Note the convention: A zero
   bit means "defined"; 1 means "undefined". */
static void set_vbits64x2(V64x2 *addr, V64x2 vbits)
{
   for (size_t i=0 ; i<2 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w64[i], &vbits.w64[i], sizeof(vbits.w64[i]));
   }
}

/* Set the V bits on the data at "addr".  Note the convention: A zero
   bit means "defined"; 1 means "undefined". */
static void set_vbits32x4(V32x4 *addr, V32x4 vbits)
{
   for (size_t i=0 ; i<4 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w32[i], &vbits.w32[i], sizeof(vbits.w32[i]));
   }
}

/* Set the V bits on the data at "addr".  Note the convention: A zero
   bit means "defined"; 1 means "undefined". */
static void set_vbits16x8(V16x8 *addr, V16x8 vbits)
{
   for (size_t i=0 ; i<8 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w16[i], &vbits.w16[i], sizeof(vbits.w16[i]));
   }
}

/* Set the V bits on the data at "addr".  Note the convention: A zero
   bit means "defined"; 1 means "undefined". */
static void set_vbits8x16(V8x16 *addr, V8x16 vbits)
{
   for (size_t i=0 ; i<16 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w8[i], &vbits.w8[i], sizeof(vbits.w8[i]));
   }
}

/* Use a value that we know is invalid. */
static void use(char *x, char* y, int invalid, char* err_msg)
{
   /* Convince GCC it does not know what is in "invalid" so it cannot
      possibly optimize away the conditional branch below. */
   __asm__ ("" : "=r" (invalid) : "0" (invalid));

   /* Create a conditional branch on which our output depends, so that
      memcheck cannot possibly optimize it away, either. */
   if (invalid) {
      fprintf(stderr, "%s > %s == true, %s\n", x, y, err_msg);
   } else {
      fprintf(stderr, "%s > %s == false, %s\n", x, y, err_msg);
   }
}

// Convert a string like "123XXX45" to a value and vbits.
static void string_to_v64(const char *s, uint64_t *x, uint64_t *vx)
{
   *x = 0;
   *vx = 0;

   for (; *s; s++) {
      int lowered_c = tolower(*s);
      *x <<= 4;
      *vx <<= 4;
      if (lowered_c == 'x') {
         *vx |= 0xf;
      } else if (isdigit(lowered_c)) {
         *x |= lowered_c - '0';
      } else if (lowered_c >= 'a' && lowered_c <= 'f') {
         *x |= lowered_c - 'a' + 0xa;
      } else {
         fprintf(stderr, "Not a hex digit: %c\n", *s);
         exit(1);
      }
   }
}

static V64x2 string_to_v64x2(const char *s) {
   uint64_t x, vx;
   string_to_v64(s, &x, &vx);

   V64x2 vx128 = { { vx, 0 } };
   V64x2 x128 = { { x, 0 } };
   set_vbits64x2(&x128, vx128);
   return x128;
}

static V32x4 string_to_v32x4(const char *s) {
   uint64_t x, vx;
   string_to_v64(s, &x, &vx);

   V32x4 vx128 = { { vx, 0, 0, 0 } };
   V32x4 x128 = { { x, 0, 0, 0 } };
   set_vbits32x4(&x128, vx128);
   return x128;
}

static V16x8 string_to_v16x8(const char *s) {
   uint64_t x, vx;
   string_to_v64(s, &x, &vx);

   V16x8 vx128 = { { vx, 0, 0, 0, 0, 0, 0, 0 } };
   V16x8 x128 = { { x, 0, 0, 0, 0, 0, 0, 0 } };
   set_vbits16x8(&x128, vx128);
   return x128;
}

static V8x16 string_to_v8x16(const char *s) {
   uint64_t x, vx;
   string_to_v64(s, &x, &vx);

   V8x16 vx128 = { {
         vx, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0 } };
   V8x16 x128 = { {
         x, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0 } };
   set_vbits8x16(&x128, vx128);
   return x128;
}

static void doit64Sx2(char *x, char *y, char *err_msg) {
   int result = cmpGT64Sx2(string_to_v64x2(x), string_to_v64x2(y));
   use(x, y, result, err_msg);
}

static void doit32Sx4(char *x, char *y, char *err_msg) {
   int result = cmpGT32Sx4(string_to_v32x4(x), string_to_v32x4(y));
   use(x, y, result, err_msg);
}

static void doit16Sx8(char *x, char *y, char *err_msg) {
   int result = cmpGT16Sx8(string_to_v16x8(x), string_to_v16x8(y));
   use(x, y, result, err_msg);
}

static void doit8Sx16(char *x, char *y, char *err_msg) {
   int result = cmpGT8Sx16(string_to_v8x16(x), string_to_v8x16(y));
   use(x, y, result, err_msg);
}

int main() {
   doit64Sx2("xxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxx", "completely undefined, error above");
   doit64Sx2("0000000000000000", "0000000000000000", "completely defined");
   doit64Sx2("0000000000000000", "f000000000000000", "completely defined");
   doit64Sx2("f000000000000000", "0000000000000000", "completely defined");
   doit64Sx2("0000000000000000", "fxxxxxxxxxxxxxxx", "defined: 0 > all negatives");
   doit64Sx2("0xxxxxxxxxxxxxxx", "fxxxxxxxxxxxxxxx", "defined: non-negatives > all negatives");
   doit64Sx2("xxxxxxxxxxxxxxx0", "f000000000000000", "undefined, error above");
   doit64Sx2("xxxxxxxxxxxxxxx1", "8000000000000000", "defined: ends with 1 > MIN_INT");
   doit64Sx2("5xxxxxxxxxxxxxxx", "6xxxxxxxxxxxxxxx", "defined");
   doit64Sx2("8xxxxxxxxxxxxxxx", "9xxxxxxxxxxxxxxx", "defined");
   doit64Sx2("123456781234567x", "1234567812345678", "undefined, error above");
   doit64Sx2("123456781234567x", "123456781234567f", "defined: x can't be more than f");
   doit64Sx2("123456781234567x", "123456781234567e", "undefined: x can be more than e, error above");

   doit32Sx4("xxxxxxxx", "xxxxxxxx", "completely undefined, error above");
   doit32Sx4("00000000", "00000000", "completely defined");
   doit32Sx4("00000000", "f0000000", "completely defined");
   doit32Sx4("f0000000", "00000000", "completely defined");
   doit32Sx4("00000000", "fxxxxxxx", "defined: 0 > all negatives");
   doit32Sx4("0xxxxxxx", "fxxxxxxx", "defined: non-negatives > all negatives");
   doit32Sx4("xxxxxxx0", "f0000000", "undefined, error above");
   doit32Sx4("xxxxxxx1", "80000000", "defined: ends with 1 > MIN_INT");
   doit32Sx4("5xxxxxxx", "6xxxxxxx", "defined");
   doit32Sx4("8xxxxxxx", "9xxxxxxx", "defined");
   doit32Sx4("1234567x", "12345678", "undefined, error above");
   doit32Sx4("1234567x", "1234567f", "defined: x can't be more than f");
   doit32Sx4("1234567x", "1234567e", "undefined: x can be more than e, error above");

   doit16Sx8("xxxx", "xxxx", "completely undefined, error above");
   doit16Sx8("0000", "0000", "completely defined");
   doit16Sx8("0000", "f000", "completely defined");
   doit16Sx8("f000", "0000", "completely defined");
   doit16Sx8("0000", "fxxx", "defined: 0 > all negatives");
   doit16Sx8("0xxx", "fxxx", "defined: non-negatives > all negatives");
   doit16Sx8("xxx0", "f000", "undefined, error above");
   doit16Sx8("xxx1", "8000", "defined: ends with 1 > MIN_INT");
   doit16Sx8("5xxx", "6xxx", "defined");
   doit16Sx8("8xxx", "9xxx", "defined");
   doit16Sx8("123x", "1234", "undefined, error above");
   doit16Sx8("123x", "123f", "defined: x can't be more than f");
   doit16Sx8("123x", "123e", "undefined: x can be more than e, error above");

   doit8Sx16("xx", "xx", "completely undefined, error above");
   doit8Sx16("00", "00", "completely defined");
   doit8Sx16("00", "f0", "completely defined");
   doit8Sx16("f0", "00", "completely defined");
   doit8Sx16("00", "fx", "defined: 0 > all negatives");
   doit8Sx16("0x", "fx", "defined: non-negatives > all negatives");
   doit8Sx16("x0", "f0", "undefined, error above");
   doit8Sx16("x1", "80", "defined: ends with 1 > MIN_INT");
   doit8Sx16("5x", "6x", "defined");
   doit8Sx16("8x", "9x", "defined");
   doit8Sx16("1x", "12", "undefined, error above");
   doit8Sx16("1x", "1f", "defined: x can't be more than f");
   doit8Sx16("1x", "1e", "undefined: x can be more than e, error above");

   return 0;
}
