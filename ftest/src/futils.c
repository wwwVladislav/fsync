#include <futils/stream.h>
#include <assert.h>
#include <string.h>

void fstream_test()
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

void futils_test()
{
    fstream_test();
}
