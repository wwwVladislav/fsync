#include <futils/fs.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

static void fsutils_create_dirs()
{
    mkdir("1");
    mkdir("1/1");
    mkdir("1/2");
    mkdir("1/2/1");
    mkdir("1/2/2");
    mkdir("1/2/2/1");
    mkdir("1/2/2/2");
    mkdir("1/2/2/3");
    mkdir("1/2/3");
    mkdir("1/3");
}

void fsutils_test()
{
    fsutils_create_dirs();

    fsiterator_t *it = fsdir_iterator("1");
    if (it)
    {
        for(dirent_t entry; fsdir_iterator_next(it, &entry);)
        {
            if (entry.type == FS_DIR)
            {
                char path[FMAX_PATH] = { 0 };
                fsdir_iterator_directory(it, path, sizeof path);
                if (strncmp(path, "2/2/3", strlen("2/2/3")) == 0)
                    break;
            }
        }

        fsdir_iterator_seek(it, "2/2/1");

        char path[FMAX_PATH] = { 0 };
        size_t path_len = fsdir_iterator_directory(it, path, sizeof path);
        if (path_len <= sizeof path)
            printf(">> %s\n", path);

        for(dirent_t entry; fsdir_iterator_next(it, &entry);)
        {
            if (entry.type == FS_DIR)
            {
                path_len = fsdir_iterator_directory(it, path, sizeof path);
                if (path_len <= sizeof path)
                    printf(">> %s\n", path);
            }
        }
        fsdir_iterator_free(it);
    }
}
