#ifndef RSTREAM_H_FSYNC
#define RSTREAM_H_FSYNC
#include <futils/msgbus.h>
#include <futils/stream.h>
#include <futils/uuid.h>
#include <futils/errno.h>

typedef struct frstream_factory frstream_factory_t;
typedef void (*fristream_listener_t)(void *, fistream_t *);
typedef void (*frostream_listener_t)(void *, fostream_t *);

frstream_factory_t  *frstream_factory(fmsgbus_t *pmsgbus, fuuid_t const *uuid);
frstream_factory_t  *frstream_factory_retain(frstream_factory_t *pfactory);
void                 frstream_factory_release(frstream_factory_t *pfactory);
ferr_t               frstream_factory_ostream(frstream_factory_t *pfactory, fuuid_t const *dst);        // src(ostream) ----> dst(istream)
ferr_t               frstream_factory_istream(frstream_factory_t *pfactory, fuuid_t const *dst);        // dst(istream) <---- src(ostream)
ferr_t               frstream_factory_istream_subscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener, void *param);
ferr_t               frstream_factory_ostream_subscribe(frstream_factory_t *pfactory, frostream_listener_t ostream_listener, void *param);
ferr_t               frstream_factory_istream_unsubscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener);
ferr_t               frstream_factory_ostream_unsubscribe(frstream_factory_t *pfactory, frostream_listener_t ostream_listener);

#endif
