#ifndef RSTREAM_H_FSYNC
#define RSTREAM_H_FSYNC
#include <futils/msgbus.h>
#include <futils/stream.h>
#include <futils/uuid.h>
#include <futils/errno.h>
#include <binn.h>

typedef struct frstream_info
{
    fuuid_t  peer;
    binn    *metainf;
} frstream_info_t;

typedef struct frstream_factory frstream_factory_t;
typedef void (*fristream_listener_t)(void *, fistream_t *, frstream_info_t const *info);

frstream_factory_t  *frstream_factory(fmsgbus_t *pmsgbus, fuuid_t const *uuid);
frstream_factory_t  *frstream_factory_retain(frstream_factory_t *pfactory);
void                 frstream_factory_release(frstream_factory_t *pfactory);
fostream_t          *frstream_factory_stream(frstream_factory_t *pfactory, fuuid_t const *dst, binn *metainf);  // src(ostream) ----> dst(istream)
ferr_t               frstream_factory_istream_subscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener, void *param);
ferr_t               frstream_factory_istream_unsubscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener);

#endif
