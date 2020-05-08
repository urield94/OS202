#include "types.h"
#include "stat.h"
#include "user.h"

void
hello_world(){
  printf(1,"hello");
}

int
main(int argc, char *argv[])
{
  // sigaction(3, &//benny, 0);
  kill(getpid(), SIGSTOP);
  for(int j = 0; j < 10; j++){
      printf(1,"%d\n", j);
  }
  kill(getpid(), SIGCONT);
  for (int i = 0; i < 10; i++)
  {
      printf(1,"%d\n", i);
  }
  

  exit();

}
