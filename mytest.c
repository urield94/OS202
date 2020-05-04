#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  uint pending = 0;
  uint tmp = 0;
  int signum = 5;
  if(signum != 0)
    tmp = 1;
  tmp = tmp << signum;
  printf(1,"tmp: %d\n", tmp);
  pending = pending | tmp;
  printf(1,"pending: %d\n", pending);

  tmp = 1;
  signum = 0;
  tmp = tmp << signum;
  printf(1,"tmp: %d\n", tmp);
  pending = pending | tmp;
  printf(1,"pending: %d\n", pending);
  exit();
}
