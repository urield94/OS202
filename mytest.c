#include "types.h"
#include "stat.h"
#include "user.h"
int
main(int argc, char *argv[])
{
  kill(getpid(),SIGKILL);

  for(int j = 0; j < 99999; j++){
      printf(1,"%d\n", j);
  }
  
  exit();
}
