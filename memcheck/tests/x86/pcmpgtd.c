#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>


int main()
{
  struct sigaction act;
  if (sigaction(SIGTERM, 0, &act) == 1) {
    return 12;
  }
  if (sigaction(SIGTERM, 0, &act) == 1) {
    return 12;
  }

  char pattern[] = "\x1\x2\x3\x4\x5\x6\x7\x8\x9";
    const unsigned long plen = strlen(pattern);
    pattern[1] = 0;
    size_t hp=0;
    for (size_t i = 0; i < plen; ++i)
        hp += pattern[i];
    return hp % 10;
}
