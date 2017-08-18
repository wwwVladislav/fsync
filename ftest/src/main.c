#include "test.h"
#include <conio.h>

FUNIT(fbd);
FUNIT(fsync);
FUNIT(futils);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    FUNIT_TEST(futils);
    FUNIT_TEST(fbd);
    FUNIT_TEST(fsync);

    printf("Press any key...");
    getch();
    return 0;
}
