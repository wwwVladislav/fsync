#include "test.h"
#include <futils/stream.h>
#include <futils/msgbus.h>
#include <futils/fs.h>
#include <futils/utils.h>
#include <assert.h>
#include <string.h>
#include <io.h>
#include <stdio.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// streams test
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

FTEST_START(fstream)
{
    for(size_t block_size = 1; block_size < 5; ++block_size)
    {
        fmem_iostream_t *piostream = fmem_iostream(block_size);

        fostream_t *postream = fmem_ostream(piostream);
        assert(postream->write(postream, "12345", 5) == 5);
        assert(postream->write(postream, "67890", 5) == 5);
        postream->release(postream);

        fistream_t *pistream = fmem_istream(piostream);
        char buf1[3], buf2[4], buf3[3];
        assert(pistream->read(pistream, buf1, sizeof buf1) == sizeof buf1);
        assert(pistream->read(pistream, buf2, sizeof buf2) == sizeof buf2);
        assert(pistream->read(pistream, buf3, sizeof buf3) == sizeof buf3);
        pistream->release(pistream);

        assert(memcmp(buf1, "123", sizeof buf1) == 0);
        assert(memcmp(buf2, "4567", sizeof buf2) == 0);
        assert(memcmp(buf3, "890", sizeof buf3) == 0);

        postream = fmem_ostream(piostream);
        assert(postream->write(postream, "123456", 6) == 6);
        assert(postream->write(postream, "7890", 4) == 4);
        postream->release(postream);

        pistream = fmem_istream(piostream);
        char buf4[3], buf5[3], buf6[4];
        assert(pistream->read(pistream, buf4, sizeof buf4) == sizeof buf4);
        assert(pistream->read(pistream, buf5, sizeof buf5) == sizeof buf5);
        assert(pistream->read(pistream, buf6, sizeof buf6) == sizeof buf6);
        pistream->release(pistream);

        assert(memcmp(buf4, "123", sizeof buf4) == 0);
        assert(memcmp(buf5, "456", sizeof buf5) == 0);
        assert(memcmp(buf6, "7890", sizeof buf6) == 0);

        fmem_iostream_release(piostream);
    }
}
FTEST_END()

FTEST_START(dir_iterator)
{
    static char const *dirs[] =
    {
        "1",
        "1/1",
        "1/2",
        "1/2/1",
        "1/2/2",
        "1/2/2/1",
        "1/2/2/2",
        "1/2/2/3",
        "1/2/3",
        "1/3"
    };

    for(int i = 0; i < FARRAY_SIZE(dirs); ++i)
        mkdir(dirs[i]);

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
        assert(strncmp(path, "2/2/1", path_len) == 0);

        int i = 6;
        for(dirent_t entry; fsdir_iterator_next(it, &entry); ++i)
        {
            if (entry.type == FS_DIR)
            {
                strcpy(path, "1/");
                path_len = fsdir_iterator_directory(it, path + 2, sizeof path);
                assert(strcmp(path, dirs[i]) == 0);
            }
        }
        fsdir_iterator_free(it);
    }
}
FTEST_END()

FUNIT_TEST_START(futils)
    FTEST(fstream);
    FTEST(dir_iterator);
FUNIT_TEST_END()
