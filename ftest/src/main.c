
void fbd_test();
void fsutils_test();
void fsync_test();
void futils_test();

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fbd_test();
    fsutils_test();
    fsync_test();
    futils_test();
    return 0;
}
