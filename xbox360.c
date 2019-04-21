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

  int s;
  while (1) {
    s = read(fd, &ev, sizeof(struct input_event));
    printf("size:%d", s);
    printf("time:%d type:%d code:%d value:%d\n", ev.time, ev.type, ev.code,
           ev.value);
  }
}