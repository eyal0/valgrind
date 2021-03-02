/* https://bugs.kde.org/show_bug.cgi?id=432801 */

#include <signal.h>
#include <string.h>
#include <stdio.h>
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

typedef unsigned long ULong;

typedef struct {
   ULong w64[2];  /* Note: little-endian */
} V128;

static int cmpGT32Sx4(V128 x, V128 y)
{
   int result;
   __asm__("movups %1,%%xmm6\n"
           "\tmovups %2,%%xmm7\n"
           // order swapped for AT&T style which has destination second.
           "\tpcmpgtd %%xmm7,%%xmm6\n"
           "\tmovd %%xmm6, %0"
           : "=r" (result) : "m" (x), "m" (y) : "xmm6");
   return result;
}

/* Set the V bits on the data at "addr".  Note the convention: A zero
   bit means "defined"; 1 means "undefined". */
static void set_vbits(V128 *addr, V128 vbits)
{
   int i;
   for (i=0 ; i<2 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w64[i], &vbits.w64[i], sizeof(vbits.w64[i]));
   }
}

/* Use a value that we know is invalid. */
static void use(char *x, char* y, int invalid)
{
   /* Convince GCC it does not know what is in "invalid" so it cannot
      possibly optimize away the conditional branch below. */
   __asm__ ("" : "=r" (invalid) : "0" (invalid));

   /* Create a conditional branch on which our output depends, so that
      memcheck cannot possibly optimize it away, either. */
   if (invalid) {
      fprintf(stderr, "%s > %s == true\n", x, y);
   } else {
      fprintf(stderr, "%s > %s == false\n", x, y);
   }
}

// Convert a string like "123XXX45" to a value and vbits.
V128 string_to_v128(char *s) {
   ULong x = 0;
   ULong vx = 0;

   for (; *s; s++) {
      int lowered_c = tolower(*s);
      x <<= 4;
      vx <<= 4;
      if (lowered_c == 'x') {
         vx |= 0xf;
      } else if (isdigit(lowered_c)) {
         x |= lowered_c - '0';
      } else if (lowered_c >= 'a' && lowered_c <= 'f') {
         x |= lowered_c - 'a' + 0xa;
      } else {
         fprintf(stderr, "Not a hex digit: %c\n", *s);
         exit(1);
      }
   }

   V128 vx128 = { { vx, 0 } };
   V128 x128 = { { x, 0 } };
   set_vbits(&x128, vx128);
   return x128;
}

static void doit(char *x, char *y) {
   int result = cmpGT32Sx4(string_to_v128(x), string_to_v128(y));
   use(x, y, result);
}

int main() {
   // completely undefined
   doit("xxxxxxxx", "xxxxxxxx");

   // completely defined
   doit("00000000", "00000000");
   doit("00000000", "f0000000");
   doit("f0000000", "00000000");

   doit("00000000", "fxxxxxxx"); // defined: 0 > all negatives
   doit("0xxxxxxx", "fxxxxxxx"); // defined: non-negatives > all negatives
   doit("xxxxxxx0", "f0000000"); // undefined
   doit("xxxxxxx1", "80000000"); // defined: ends with 1 > MIN_INT
   doit("5xxxxxxx", "6xxxxxxx"); // defined
   doit("8xxxxxxx", "9xxxxxxx"); // defined

   doit("1234567x", "12345678"); // undefined
   doit("1234567x", "1234567f"); // defined: x can't be more than f
   doit("1234567x", "1234567e"); // undefined: x can be more than e

   return 0;
}
