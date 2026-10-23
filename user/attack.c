#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#include "kernel/memlayout.h"

#ifndef DATASIZE
  #define DATASIZE (64 * PGSIZE)
#endif

#ifndef MSG
  #define MSG "Here it is: "
#endif

int
main(int argc, char *argv[])
{
  int msglen = strlen(MSG);

  char *data = sbrk(DATASIZE);
  if (data == SBRK_ERROR) {
    printf("sbrk() failed\n");
    exit(1);
  }

  char *end = data + DATASIZE;
  int matches = 0;

  for (char *p = data; p + msglen <= end; p++) {
    if (memcmp(p, MSG, msglen) == 0) {
      matches++;
      if (matches == 2) {
          printf("%s\n", p + msglen);
      }
    }
  }

  exit(0);
}
