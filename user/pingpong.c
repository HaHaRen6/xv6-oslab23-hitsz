#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

int main(int argc, char **argv) {
  if (argc != 1) {
    printf("pingpoFng needs no argument!\n");
    exit(-1);
  }
  // create pipe
  int p1[2], p2[2];
  // p[1] for in, p[0] for out
  pipe(p1);
  pipe(p2);


  int pid;
  if ((pid = fork()) != 0) {
    // parent process
    close(p1[0]);
    close(p2[1]);
    char buffer1[] = "ping";
    write(p1[1], buffer1, 5);
    close(p1[1]);

    char buffer2[5];
    read(p2[0], buffer2, 5);
    printf("%d: received %s\n", getpid(), buffer2);
    close(p2[0]);
  }
  else {
    // child process
    close(p1[1]);
    close(p2[0]);
    char buffer1[5];
    read(p1[0], buffer1, 5);
    printf("%d: received %s\n", getpid(), buffer1);
    close(p1[0]);

    char buffer2[5] = "pong";
    write(p2[1], buffer2, 5);
    close(p2[1]);
  }
  exit(0);
}
