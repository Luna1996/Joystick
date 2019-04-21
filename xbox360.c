#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "types.h"

typedef struct {
  u32 time;
  u8 type;
  u8 name;
  i16 value;
} js_event;

int main(int argc, char **argv) {
  const char *device;
  // js_event e;
  char e[8];
  i32 js, es;

  if (argc > 1)
    device = argv[1];
  else
    device = "/dev/input/js0";

  js = open(device, O_RDONLY);
  int s;
  while (1) {
    s = read(js, &e, 8);
    for (int i = 0; i < 8; i++) printf("%04x ", e[i]);
    printf("size:%d\n", s);
  }
  close(js);
  return 0;
}