#include "../../fsync/src/rsync.h"
#include <string.h>
#include <stdio.h>

static char const FBASE_DATA[] =
    "\"But I don\'t want to go among mad people,\" Alice remarked.\n"
    "\"Oh, you can\'t help that,\" said the Cat: \"we\'re all mad here. I\'m mad. You\'re mad.\"\n"
    "\"You must be,\" said the Cat, \"or you wouldn\'t have come here.\"\n";

static char const FDATA[] =
    "\"But I don\'t want to go among mad people,\" Alice remarked.\n"
    "\"Oh, you can\'t help that,\" said the Cat: \"we\'re all mad here. I\'m mad. You\'re mad.\"\n"
    "\"How do you know I\'m mad?\" said Alice.\n"
    "\"You must be,\" said the Cat, \"or you wouldn\'t have come here.\"\n";

typedef struct
{
    uint32_t size;
    uint32_t offset;
    uint8_t  data[1024];
} fdata_stream_t;

static uint32_t frsync_read_data(void *stream, char *data, uint32_t size)
{
    enum { n = 3 };
    static int i = 0;
    if (i >= n)
        return 0;

    static const uint32_t PARTS[n] =
    {
        sizeof FBASE_DATA / 3,
        sizeof FBASE_DATA / 3,
        sizeof FBASE_DATA - 2 * sizeof FBASE_DATA / 3
    };

    memcpy(data, FBASE_DATA + i * PARTS[0], PARTS[i]);

    return PARTS[i++];
}

static uint32_t frsync_read_stream(void *stream, char *data, uint32_t size)
{
    fdata_stream_t *pstream = (fdata_stream_t *)stream;

    enum { n = 3 };
    static int i = 0;
    if (i >= n)
        return 0;

    const uint32_t PARTS[n] =
    {
        pstream->size / 3,
        pstream->size / 3,
        pstream->size - 2 * pstream->size / 3
    };

    memcpy(data, pstream->data + pstream->offset, PARTS[i]);
    pstream->offset += PARTS[i];

    return PARTS[i++];
}

static uint32_t frsync_write_stream(void *stream, char const *data, uint32_t size)
{
    fdata_stream_t *pstream = (fdata_stream_t *)stream;
    memcpy(pstream->data + pstream->size, data, size);
    pstream->size += size;
    return size;
}

static void frsync_test()
{
    fdata_stream_t signature_stream = { 0 };
    fdata_stream_t base_data_stream = { sizeof FBASE_DATA, 0 };
    memcpy(base_data_stream.data, FBASE_DATA, sizeof FBASE_DATA);

    // Calculate signature
    frsync_sig_calculator_t *psig_calc = frsync_sig_calculator_create();
    if (psig_calc)
    {
        if (frsync_sig_calculate(psig_calc, &signature_stream, frsync_read_data, frsync_write_stream) != FSUCCESS)
            printf("Signature calculation failed\n");
        frsync_sig_calculator_release(psig_calc);
    }

    // Signature
    frsync_signature_t *psig = frsync_signature_create();
    if (psig)
    {
        if (frsync_signature_load(psig, &signature_stream, frsync_read_stream) != FSUCCESS)
            printf("Signature load failed\n");
        frsync_signature_release(psig);
    }
}

void fsync_test()
{
    frsync_test();
}
