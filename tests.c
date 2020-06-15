#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

/************************Tests pagining framework section********************************/
/****                                Task 1                                          ****/

void test_paging1(void)
{
    int i, pid;
    char *child_array = sbrk(16 * PGSIZE);
    int child_array_length = (16 * PGSIZE) / sizeof(int);
    for (i = 0; i < child_array_length; i++)
    {
        child_array[i] = (char)(i + 1);
    }
    printf(1, "Forking...\n");
    if ((pid = fork()) == 0)
    {
        int ok_child = 1;
        printf(1, "Child - start checking for changes in the array\n");
        for (i = 0; i < child_array_length; i++)
        {
            if (child_array[i] != (char)(i + 1))
            {
                printf(2, "Child - Unexpected value %d instead of %d in position %d\n", child_array[i], i + 1, i);
                ok_child = 0;
                break;
            }
        }
        if (ok_child)
            printf(1, "Fork work!\n");
        else
            printf(2, "Fork faild.\n");
    }
    else if (pid > 0)
    {
        int ok_parent = 1;
        printf(1, "Parent - start checking for changes in the array\n");
        for (i = 0; i < child_array_length; i++)
        {
            if (child_array[i] != (char)(i + 1))
            {
                printf(2, "Parent - Unexpected value %d instead of %d in position %d\n", child_array[i], i + 1, i);
                ok_parent = 0;
                break;
            }
        }
        if (ok_parent)
            printf(1, "Fork work!\n");
        else
            printf(2, "Fork faild.\n");
        printf(1, "Parent - Waiting.\n");
        wait();
    }
    else
        printf(2, "Unsuccessful fork.\n");

    free(child_array);
}

void test_paging2(void)
{
    char *a = sbrk(3 * PGSIZE);
    char *b = sbrk(3 * PGSIZE);
    for (int i = 0; i < 10; i++)
    {
        char tmp = a[i];
        a[i] = b[i + 5];
        b[i] = tmp;
    }
    free(a);
    free(b);
}

void test_paging3(void)
{
    int i, pid, size = 20;
    int allocate_size = 1024;
    char *array[size];

    for (i = 0; i < size; i++)
    {
        array[i] = sbrk(allocate_size);
        array[i][0] = size;
    }

    if ((pid =fork())== 0)
    {
        for (i = 0; i < size; i++){
            array[i][0] = size;
        }
        printf(1, "Child - Sleeping for 250 ms...\n");
        sleep(250);

        for (i = 0; i < size; i++){
            printf(1, "array[%d][0] = %d\n", i, array[i][0]);
        }
        printf(1, "Child - Sleeping for 250 ms\n");
        sleep(250);
        exit();
    }
    wait();
    printf(1, "Parent - Freeing array\n");

    for (i = 0; i < size; i++)
    {
        free(array[i]);
    }
}

void tests_paging_framework(void)
{
    printf(1, "Paging Test1 starts....\n");
    test_paging1();
    printf(1, "Paging Test1 done!\n--------------------\n\n");
    printf(1, "Paging Test2 starts....\n");
    test_paging2();
    printf(1, "Paging Test2 done!\n--------------------\n\n");
    printf(1, "Paging Test3 starts....\n");
    test_paging3();
    printf(1, "Paging Test3 done!\n--------------------\n\n");
}

/****************************************************************************************/

/***********************************Tests_Cow Section************************************/
/****                                   TASK 2                                       ****/

int i = 1;
void test_cow1()
{
    printf(1, "There are %d free pages\nForking...\n", gnofp());
    int pid = fork();
    if (pid == 0)
    {
        printf(1, "Child: Before changing - there are %d free pages and i = %d\n", gnofp(), i);
        i = 100;
        printf(1, "Child: After changing - there are %d free pages and i = %d\n", gnofp(), i);
        exit();
    }
    printf(1, "Parent: Before wait - there are %d free pages and i = %d\n", gnofp(), i);
    wait();
    printf(1, "Parent: After wait - there are %d free pages and i = %d\n", gnofp(), i);
}

void test_cow2()
{
    printf(1, "There are %d free pages\nForking...\n", gnofp());
    if (fork() == 0)
    {
        printf(1, "Child1: exiting... There are %d free pages and i = %d\n", gnofp(), i);
        exit();
    }
    else
    {
        printf(1, "Parent: There are %d free pages\nForking...\n", gnofp());
        if (fork() == 0)
        {
            printf(1, "Child2: Before changing - there are %d free pages and i = %d\n", gnofp(), i);
            i = 5;
            printf(1, "Child2: After changing - there are %d free pages and i = %d\n", gnofp(), i);
            exit();
        }
        printf(1, "Parent: Before wait1 - there are %d free pages and i = %d\n", gnofp(), i);
        wait();
        printf(1, "Parent: After wait1 - there are %d free pages and i = %d\n", gnofp(), i);
    }
    printf(1, "Parent: Before wait2 - there are %d free pages and i = %d\n", gnofp(), i);
    wait();
    printf(1, "Parent: After wait2 - there are %d free pages and i = %d\n", gnofp(), i);
    return;
}

void tests_cow(void)
{
    printf(1, "Cow Test1 starts....\n");
    test_cow1();
    printf(1, "Cow Test1 done!\n--------------------\n\n");
    printf(1, "Cow Test2 starts....\n");
    test_cow2();
    printf(1, "Cow Test2 done!\n--------------------\n\n");
}

/*******************************************************************************************/

int main(void)
{
    tests_paging_framework();
    tests_cow();

    exit();
}