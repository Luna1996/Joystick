#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "types.h"

typedef struct {
  u64 time;
  u16 type;
  u16 name;
  i32 value;
} js_event;

int main(int argc, char **argv) {
  int fd, s;
  if (argc < 2) {
    printf("usage: %s \n", argv[0]);
    return 1;
  }
  fd = open(argv[1], O_RDONLY);
  u64 ev[4];

  while (1) {
    s = read(fd, ev, 32);
    for (int i = 0; i < 4; i++) printf("%08x ", ev[i]);
    printf("\n");
  }
}