/* https://bugs.kde.org/show_bug.cgi?id=432801 */

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <immintrin.h>

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
   volatile size_t j = 0;
   if (j == hp % 10) {
      j++;
   }
   printf("%ld\n", hp);
}

typedef union V128 {
  struct {
    uint64_t w64[2];  /* Note: little-endian */
  };
  struct {
   uint32_t w32[4];  /* Note: little-endian */
  };
  struct {
    uint16_t w16[8];  /* Note: little-endian */
  };
  struct {
   uint8_t w8[16];  /* Note: little-endian */
  };
} V128;

static constexpr char q[0] asm("q") = {}; // override asm symbol name which gets mangled for const or constexpr
static constexpr char d[0] asm("d") = {};
static constexpr char w[0] asm("w") = {};
static constexpr char b[0] asm("b") = {};

template <typename T>
static T cmpGT(V128 x, V128 y, size_t element_select) {
  constexpr const char *op =
      (sizeof(T)==8) ? q :
      (sizeof(T)==4) ? d :
      (sizeof(T)==2) ? w :
      b;

  __asm__(
      // order swapped for AT&T style which has destination second.
      "pcmpgt%p[op] %[src],%[dst]"
      : [dst]"+x"(x)
      : [src]"x"(y),
        [op]"i"(op)  // address as an immediate = symbol name
      : /* no clobbers */);

  return ((T*)&x)[element_select];
}

static void set_vbits(V128 *addr, V128 vbits)
{
   for (size_t i=0 ; i<2 ; ++i) {
      (void)VALGRIND_SET_VBITS(&addr->w64[i], &vbits.w64[i], sizeof(vbits.w64[i]));
   }
}

// Convert a string like "123XXX45" to a value and vbits.
template <typename T>
static void string_to_vbits(const char *s, T *x, T *vx)
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

template <typename T>
static V128 string_to_vbits(const char *s, size_t lane) {
   T x, vx;
   string_to_vbits(s, &x, &vx);

   V128 vx128 = {0};
   vx128.w32[0] = 0xffffffff;
   vx128.w32[1] = 0xffffffff;
   vx128.w32[2] = 0xffffffff;
   vx128.w32[3] = 0xffffffff;
   V128 x128 = {0};
   if (sizeof(T) == 8) {
     vx128.w64[lane] = vx;
     x128.w64[lane] = x;
   } else if (sizeof(T) == 4) {
     vx128.w32[lane] = vx;
     x128.w32[lane] = x;
   } else if (sizeof(T) == 2) {
     vx128.w16[lane] = vx;
     x128.w16[lane] = x;
   } else {
     vx128.w8[lane] = vx;
     x128.w8[lane] = x;
   }
   set_vbits(&x128, vx128);
   return x128;
}

template <typename T>
static void doit(const char *x, const char *y, bool expected_undefined, const char *err_msg) {
  for (size_t lane = 0; lane < sizeof(V128)/sizeof(T); lane++) {
    int result = cmpGT<T>(string_to_vbits<T>(x, lane),
                          string_to_vbits<T>(y, lane),
                          lane);
    int undefined = VALGRIND_CHECK_VALUE_IS_DEFINED(result);
    if (!!undefined != expected_undefined) {
      fprintf(stderr, "ERROR: ");
    }
    fprintf(stderr, "%s > %s == %d, %s, %d == %d\n", x, y, result, err_msg, !!undefined, !!expected_undefined);
  }
}

int main() {
  doit<uint64_t>("xxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxx", true, "completely undefined, error above");
  doit<uint64_t>("0000000000000000", "0000000000000000", false, "completely defined");
  doit<uint64_t>("0000000000000000", "f000000000000000", false, "completely defined");
  doit<uint64_t>("f000000000000000", "0000000000000000", false, "completely defined");
  doit<uint64_t>("0000000000000000", "fxxxxxxxxxxxxxxx", false, "defined: 0 > all negatives");
  doit<uint64_t>("0xxxxxxxxxxxxxxx", "fxxxxxxxxxxxxxxx", false, "defined: non-negatives > all negatives");
  doit<uint64_t>("xxxxxxxxxxxxxxx0", "f000000000000000", true, "undefined, error above");
  doit<uint64_t>("xxxxxxxxxxxxxxx1", "8000000000000000", false, "defined: ends with 1 > MIN_INT");
  doit<uint64_t>("5xxxxxxxxxxxxxxx", "6xxxxxxxxxxxxxxx", false, "defined");
  doit<uint64_t>("8xxxxxxxxxxxxxxx", "9xxxxxxxxxxxxxxx", false, "defined");
  doit<uint64_t>("123456781234567x", "1234567812345678", true, "undefined, error above");
  doit<uint64_t>("123456781234567x", "123456781234567f", false, "defined: x can't be more than f");
  doit<uint64_t>("123456781234567x", "123456781234567e", true, "undefined: x can be more than e, error above");

  doit<uint32_t>("xxxxxxxx", "xxxxxxxx", true, "completely undefined, error above");
  doit<uint32_t>("00000000", "00000000", false, "completely defined");
  doit<uint32_t>("00000000", "f0000000", false, "completely defined");
  doit<uint32_t>("f0000000", "00000000", false, "completely defined");
  doit<uint32_t>("00000000", "fxxxxxxx", false, "defined: 0 > all negatives");
  doit<uint32_t>("0xxxxxxx", "fxxxxxxx", false, "defined: non-negatives > all negatives");
  doit<uint32_t>("xxxxxxx0", "f0000000", true, "undefined, error above");
  doit<uint32_t>("xxxxxxx1", "80000000", false, "defined: ends with 1 > MIN_INT");
  doit<uint32_t>("5xxxxxxx", "6xxxxxxx", false, "defined");
  doit<uint32_t>("8xxxxxxx", "9xxxxxxx", false, "defined");
  doit<uint32_t>("1234567x", "12345678", true, "undefined, error above");
  doit<uint32_t>("1234567x", "1234567f", false, "defined: x can't be more than f");
  doit<uint32_t>("1234567x", "1234567e", true, "undefined: x can be more than e, error above");

  doit<uint16_t>("xxxx", "xxxx", true, "completely undefined, error above");
  doit<uint16_t>("0000", "0000", false, "completely defined");
  doit<uint16_t>("0000", "f000", false, "completely defined");
  doit<uint16_t>("f000", "0000", false, "completely defined");
  doit<uint16_t>("0000", "fxxx", false, "defined: 0 > all negatives");
  doit<uint16_t>("0xxx", "fxxx", false, "defined: non-negatives > all negatives");
  doit<uint16_t>("xxx0", "f000", true, "undefined, error above");
  doit<uint16_t>("xxx1", "8000", false, "defined: ends with 1 > MIN_INT");
  doit<uint16_t>("5xxx", "6xxx", false, "defined");
  doit<uint16_t>("8xxx", "9xxx", false, "defined");
  doit<uint16_t>("123x", "1234", true, "undefined, error above");
  doit<uint16_t>("123x", "123f", false, "defined: x can't be more than f");
  doit<uint16_t>("123x", "123e", true, "undefined: x can be more than e, error above");

  doit<uint8_t>("xx", "xx", true, "completely undefined, error above");
  doit<uint8_t>("00", "00", false, "completely defined");
  doit<uint8_t>("00", "f0", false, "completely defined");
  doit<uint8_t>("f0", "00", false, "completely defined");
  doit<uint8_t>("00", "fx", false, "defined: 0 > all negatives");
  doit<uint8_t>("0x", "fx", false, "defined: non-negatives > all negatives");
  doit<uint8_t>("x0", "f0", true, "undefined, error above");
  doit<uint8_t>("x1", "80", false, "defined: ends with 1 > MIN_INT");
  doit<uint8_t>("5x", "6x", false, "defined");
  doit<uint8_t>("8x", "9x", false, "defined");
  doit<uint8_t>("1x", "12", true, "undefined, error above");
  doit<uint8_t>("1x", "1f", false, "defined: x can't be more than f");
  doit<uint8_t>("1x", "1e", true, "undefined: x can be more than e, error above");

  return 0;
}
