#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

void
hello_world(){
  printf(1,"hello\n");
}

void test_stop_and_cont(){
  printf(1,"\nSending SIGSTOP following by SIGCONT\n");
  kill(getpid(), SIGSTOP);
  kill(getpid(), SIGCONT);
  printf(1,"Test passed\n");
}

void test_user_handler(){
  printf(1,"\nSending user handler signal\n");
  struct sigaction uh = {hello_world, 0};
  sigaction(3, &uh, 0);
  kill(getpid(), 3);
  printf(1,"Test passed\n");
}

void test_mask(){
  printf(1,"\nSending blocked user handler signal\n");
  struct sigaction uh = {hello_world, 0};
  sigaction(3, &uh, 0);
  sigprocmask(1 << 3);
  kill(getpid(), 3);
  printf(1,"Test passed\n");
}

void
test_fork(){
  printf(1,"\nForking and waiting\n");
  struct sigaction uh = {hello_world, 0};
  sigaction(3, &uh, 0);

  int parent = fork();
  if(!parent){
    int unil_you_die = 1;
    while(unil_you_die);
  }
  for(int i = 0; i < 100; i++);
  
  kill(parent, 3);
  
  for(int i = 0; i < 75; i++);
  
  kill(parent, SIGKILL);
  printf(1, "SIGKILL was sent\n");
  wait();
  printf(1, "Test passed\n");
}

void test_kill(){
  printf(1,"\nSending SIGKILL\n");
  kill(getpid(), SIGKILL);
  printf(1,"Test passed\n");
}

int
main(int argc, char *argv[])
{
  test_stop_and_cont();
  test_user_handler();
  test_mask();
  test_fork();
  test_kill();
}
