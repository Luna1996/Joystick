#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include "types.h"

typedef struct {
  u32 sec;
  u32 usec;
  u16 type;
  u16 code;
  i32 value;
} js_event;

#define JS_BUTTON 0x0001
#define JS_AXIS 0x0003

#define JS_A 0x0130
#define JS_B 0x0131
#define JS_X 0x0133
#define JS_Y 0x0134
#define JS_LB 0x0136
#define JS_RB 0x0137
#define JS_BACK 0x013A
#define JS_MENU 0x013B
#define JS_HOME 0x013C
#define JS_LS 0x013D
#define JS_RS 0x013E

#define JS_LX 0x0000
#define JS_LY 0x0001
#define JS_LT 0x0002
#define JS_RX 0x0003
#define JS_RY 0x0004
#define JS_RT 0x0005

#define JS_DX 0x0010
#define JS_DY 0x0011

#define JS_TMAX 255
#define JS_XYMAX 32768

char btns[15][5] = {"A", "B", "",     "X",    "Y",    "",   "LB", "RB",
                    "",  "",  "BACK", "MENU", "HOME", "LS", "RS"};
char axis[6][3] = {"LX", "LY", "LT", "RX", "RY", "RT"};
char dpad[2][2][6] = {{"LEFT", "RIGHT"}, {"UP", "DOWN"}};

#define histLen 1000
#define evSize 256
char** hist = NULL;
int start = 0;

void sigint_handler(int signum) {
  FILE* fp = fopen("log.txt", "wa");
  if (!fp) {
    fprintf(stderr, "Couldnt open log file\n");
    exit(0);
  }
  for (int i = start; i < histLen + start; i++) {
    fprintf(fp, "%s", hist[i % histLen]);
    free(hist[i % histLen]);
  }
  free(hist);
  fclose(fp);
  exit(0);
}

int main(int argc, char** argv) {
  int fd, s;
  char path[64];
  u16 type;
  u16 code;
  i32 value;
  i32 dpadv[2];

  struct sigaction ss;
  ss.sa_handler = sigint_handler;
  ss.sa_flags = SA_RESTART;
  sigaction(SIGINT, &ss, NULL);

  hist = (char**)malloc(sizeof(char*) * histLen);
  for (int i = 0; i < histLen; i++) {
    hist[i] = (char*)calloc(1, evSize);
  }

  sprintf(path, "/dev/input/event%s", (argc == 2) ? argv[1] : "2");
  fd = open(path, O_RDONLY);
  js_event ev[2];
  char buf[256] = "";
  while (1) {
    s = read(fd, ev, 32);
    type = ev[0].type;
    if (!type) continue;
    code = ev[0].code;
    value = ev[0].value;
    if (code >= JS_LX && code <= JS_RT) {
      printf("Axis[%s] value change:%d\n", axis[code - JS_LX], value);
    } else if (code > JS_DY) {
      printf("Button[%s] %s\n", btns[code - JS_A],
             value ? "pressed" : "released");
    } else {
      printf("D-[%s] %s\n",
             dpad[code - JS_DX]
                 [(((value == 0) ? dpadv[code - JS_DX] : value) + 1) / 2],
             (value == 0) ? "released" : "pressed");
      dpadv[code - JS_DX] = value;
    }
    sprintf(buf, "%08x %08x %01x %04x %d\n", ev[0].sec, ev[0].usec, ev[0].type,
            ev[0].code, ev[0].value);
    strcpy(hist[start], buf);
    start++;
    if (start >= histLen) {
      start = 0;
    }
  }
  return 0;
}