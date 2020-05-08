#include "types.h"
#include "stat.h"
#include "user.h"
int
main(int argc, char *argv[])
{
//   uint pending = 33;


//  for(int i = 0; i < 32; i++){
//     uint tmp = 1;
//     tmp = tmp << i;
//     int sig = pending & tmp;
//     printf(1,"sig: %d\n", sig);
//   }

  double a = 512;
  double log_a = pow(a, 2);
  printf(1, "%d", log_a);
  exit();
}
