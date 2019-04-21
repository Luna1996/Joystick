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
  js_event ev[2];

  while (1) {
    s = read(fd, ev, 32);
    printf("time:%016x type:%04x name:%04x value:%08x\n", ev[0].time,
           ev[0].type, ev[0].name, ev[0].value);
  }
}