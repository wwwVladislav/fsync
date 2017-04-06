#ifndef DEVICE_H_FSYNC
#define DEVICE_H_FSYNC
#include <fcommon/limits.h>

typedef enum fsdev_type
{
    FSDEV_USB_DEVICE = 0,
    FSDEV_DISK,
    FSDEV_CDROM,
    FSDEV_FLOPPY,
    FSDEV_VOLUME,
    FSDEV_MAX
} fsdev_type_t;

typedef enum fsdev_action
{
    FSDEV_ARRIVAL,
    FSDEV_REMOVED
} fsdev_action_t;

typedef int fsdev_handle_t;
typedef void (*fsdev_hotplug_callback_fn_t)(fsdev_action_t, char const *);

#define FSDEV_INVALID_HANDLE -1

typedef struct
{
    char name[FMAX_FILENAME];
} fsdev_volume_t;

fsdev_handle_t fsdev_hotplug_register_callback(fsdev_type_t, fsdev_hotplug_callback_fn_t);
void           fsdev_hotplug_unregister_callback(fsdev_handle_t);
unsigned       fsdev_get_all_volumes(fsdev_volume_t *, unsigned);

#endif
