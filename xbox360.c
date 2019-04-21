#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "types.h"

typedef struct {
} js_event;

int main(int argc, char **argv) {
  int fd;
  if (argc < 2) {
    printf("usage: %s \n", argv[0]);
    return 1;
  }
  fd = open(argv[1], O_RDONLY);
  struct input_event ev;

  int s;
  u16 e[16];
  while (1) {
    s = read(fd, e, 32);
    for (int i = 0; i < 16; i++) {
      printf("%04x ", e[i]);
    }
    printf("size:%d\n", s);
  }
}