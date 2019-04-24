#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#define histLen 1000
#define evSize 256
char ** hist=NULL;
int start=0;
void sigint_handler(int signum) {
  FILE* fp = fopen("log.txt","wa");
  if(!fp){
    fprintf(stderr,"Couldnt open log file\n");
    exit(0);
  }
  for(int i =start;i<histLen+start;i++){
    fprintf(fp, "%s\n", hist[i%histLen]);
    free(hist[i%histLen]);
  }
  free(hist);
  fclose(fp);
  exit(0);
}


int main(){

  struct sigaction ss;
  
  ss.sa_handler = sigint_handler;
  ss.sa_flags   = SA_RESTART;
  
  sigaction(SIGINT, &ss, NULL);


  hist=(char**)malloc(sizeof(char*)*histLen);
  for(int i =0;i<histLen;i++){
    hist[i]=(char*)calloc(1,evSize);
  }
  char buf[256]="";
  while(1){
    
    sprintf(buf,"%d) event lets see if this works", start);
    printf("%s\n",buf);
    strcpy(hist[start],buf);
    start++;
    if(start>=histLen){
      start=0;
    }



  }
}
