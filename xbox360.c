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
  js_event e;
  i32 js, es;

  if (argc > 1)
    device = argv[1];
  else
    device = "/dev/input/js0";

  js = open(device, O_RDONLY);
  printf("js:%d\n", js);
  es = sizeof(e);
  while (read(js, &e, es) == es) {
    printf("time:%u type:%u name:%u value:%d\n", e.time, e.type, e.name,
           e.value);
  }
  close(js);
  return 0;
}