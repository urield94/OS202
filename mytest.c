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
  const struct sigaction benny = {&hello_world, 0};

  sigaction(3, &benny, 0);
  kill(getpid(), 3);
  for(int j = 0; j < 99999; j++){
      printf(1,"%d\n", j);
  }

  exit();
}
