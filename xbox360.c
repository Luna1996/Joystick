#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


int main(int argc, char **argv) {
  int fd;
  if (argc < 2) {
    printf("usage: %s \n", argv[0]);
    return 1;
  }
  fd = open(argv[1], O_RDONLY);
  struct input_event ev;

  while (1) {
    read(fd, &ev, sizeof(struct input_event));

    if (ev.type == 1)
      // printf("key %i state %i\n", ev.code, ev.value);
      if (ev.value == 0) printf(" : [key %i]\n ", ev.code);
  }
}