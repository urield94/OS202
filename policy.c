#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    if(argc != 2){
        printf(1, "Error replacing policy, invalid number of arguments.\n");
        exit(1);
    }
    int new_policy = atoi(argv[1]);
    printf(1,"new_policy: %d\n", atoi(argv[1]));
    int success = policy(atoi(argv[1]));
    switch(success){
        case 0:
            printf(1, "Policy has been successfully changed to Default Policy\n");
            break;
        case 1:
            printf(1, "Policy has been successfully changed to Priority Policy\n");
            break;
        case 2:
            printf(1, "Not implemented yet, restoring to defualt policy\n");
            policy(0);
            break;
            // printf(1, "Policy has been successfully changed to CFS Policy");
        default:
            printf(1, "Error replacing policy, no such a policy number (%d)\n", new_policy);
            exit(1);

    }
    exit(0);
}
