#ifndef MARSHALLER_H_FNET
#define MARSHALLER_H_FNET
#include "transport.h"
#include <futils/uuid.h>
#include <stdint.h>
#include <stdbool.h>

bool fmarshal(fnet_client_t *client, uint8_t const *data, size_t size);
bool fmarshal_u8(fnet_client_t *client, uint8_t v);
bool fmarshal_u16(fnet_client_t *client, uint16_t v);
bool fmarshal_u32(fnet_client_t *client, uint32_t v);
bool fmarshal_u64(fnet_client_t *client, uint64_t v);
bool fmarshal_uuid(fnet_client_t *client, fuuid_t const *v);
bool fmarshal_bool(fnet_client_t *client, bool v);

bool funmarshal(fnet_client_t *client, uint8_t *data, size_t size);
bool funmarshal_u8(fnet_client_t *client, uint8_t *v);
bool funmarshal_u16(fnet_client_t *client, uint16_t *v);
bool funmarshal_u32(fnet_client_t *client, uint32_t *v);
bool funmarshal_u64(fnet_client_t *client, uint64_t *v);
bool funmarshal_uuid(fnet_client_t *client, fuuid_t *v);
bool funmarshal_bool(fnet_client_t *client, bool *v);

#endif
