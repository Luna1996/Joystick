#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "types.h"

typedef struct {
  u32 sec;
  u32 usec;
  u16 type;
  u16 code;
  i32 value;
} js_event;

int main(int argc, char **argv) {
  int fd, s;
  if (argc < 2) {
    printf("usage: %s \n", argv[0]);
    return 1;
  }
  fd = open(argv[1], O_RDONLY);
  js_event ev;

  while (1) {
    s = read(fd, &ev, 16);
    printf("sec:%08x usec:%08x type:%04x code:%04x value:%08x\n", ev.sec,
           ev.usec, ev.type, ev.code, ev.value);
    // for (int i = 0; i < s; i++) printf("%02x ", ev[i]);
    // printf("\n");
  }
}