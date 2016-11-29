#include "../../device.h"
#include <futils/log.h>

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT   0x0501
#include <windows.h>
#include <dbt.h>

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

// System-Defined Device Interface Classes
static GUID const FSDEV_GUIDS[] =
{
    { 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } },    // GUID_CLASS_USB_DEVICE
    { 0x53f56307L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } },    // GUID_DEVINTERFACE_DISK
    { 0x53f56308L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } },    // GUID_DEVINTERFACE_CDROM
    { 0x53f56311L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } },    // GUID_DEVINTERFACE_FLOPPY
    { 0x53f5630dL, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } }     // GUID_DEVINTERFACE_VOLUME
};

typedef union
{
    int handle;
    struct
    {
        short fn_id;
        short dev_type;
    } fd;
} fsdev_handle_desc_t;

enum FSDEV_CONFIG
{
    FSDEV_MAX_CALLBACKS             = 64,
    WM_FSDEV_REGISTER_NOTIFICATION  = WM_USER + 0,
    WM_FSDEV_UNREGISTER_NOTIFICATION= WM_USER + 1
};

typedef struct
{
    int                         rc;
    HDEVNOTIFY                  dev_notify;
    fsdev_hotplug_callback_fn_t callacks[FSDEV_MAX_CALLBACKS];
} dev_event_handlers_t;

static struct
{
    volatile int         rc;
    volatile bool        is_active;
    HWND                 hwnd;
    pthread_t            thread;
    dev_event_handlers_t handlers[FSDEV_MAX];
} fsdev_unit;

static fsdev_type_t fsdev_find_type_by_guid(GUID const guid)
{
    fsdev_type_t type = 0;
    for(; type < FSDEV_MAX; ++type)
    {
        if (memcmp(&guid, &FSDEV_GUIDS[type], sizeof guid) == 0)
            break;
    }
    return type;
}

static bool fsdev_register_notification(fsdev_type_t type)
{
    if (type < 0 || type >= FSDEV_MAX)
    {
        FS_ERR("Device notification registration error. Invalid device type.");
        return false;
    }

    if (fsdev_unit.handlers[type].rc++ == 0)
    {
        HDEVNOTIFY *hdevice_notify = &fsdev_unit.handlers[type].dev_notify;
        GUID const class_guid = FSDEV_GUIDS[type];

        DEV_BROADCAST_DEVICEINTERFACE notification_filter;
        memset(&notification_filter, 0, sizeof notification_filter);
        notification_filter.dbcc_size = sizeof notification_filter;
        notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        notification_filter.dbcc_classguid = class_guid;

        *hdevice_notify = RegisterDeviceNotification(fsdev_unit.hwnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (0 == *hdevice_notify)
        {
            FS_ERR("Device notification registration error");
            return false;
        }
    }

    return true;
}

static bool fsdev_unregister_notification(fsdev_type_t type)
{
    if (type < 0 || type >= FSDEV_MAX)
    {
        FS_ERR("Device notification unregistration error. Invalid device type.");
        return false;
    }

    if (--fsdev_unit.handlers[type].rc == 0)
    {
        if (fsdev_unit.handlers[type].dev_notify)
            UnregisterDeviceNotification(fsdev_unit.handlers[type].dev_notify);
        fsdev_unit.handlers[type].dev_notify = 0;
    }

    return true;
}

static LRESULT WINAPI fsdev_message_handler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT ret = 1;

    switch (message)
    {
        case WM_FSDEV_REGISTER_NOTIFICATION:
        {
            if (!fsdev_register_notification((fsdev_type_t)wparam))
            {
                FS_WARN("Device notification listener thread is stopped");
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_FSDEV_UNREGISTER_NOTIFICATION:
        {
            fsdev_unregister_notification((fsdev_type_t)wparam);
            break;
        }

        case WM_DEVICECHANGE:
        {
            PDEV_BROADCAST_DEVICEINTERFACE pdbd = (PDEV_BROADCAST_DEVICEINTERFACE)lparam;
            fsdev_type_t device_type = fsdev_find_type_by_guid(pdbd->dbcc_classguid);

            if (device_type != FSDEV_MAX)
            {
                switch (wparam)
                {
                    case DBT_DEVICEARRIVAL:
                    {
                        for(int i = 0; i < FSDEV_MAX_CALLBACKS; ++i)
                        {
                            if (fsdev_unit.handlers[device_type].callacks[i])
                                fsdev_unit.handlers[device_type].callacks[i](FSDEV_ARRIVAL, pdbd->dbcc_name);
                        }
                        break;
                    }

                    case DBT_DEVICEREMOVECOMPLETE:
                    {
                        for(int i = 0; i < FSDEV_MAX_CALLBACKS; ++i)
                        {
                            if (fsdev_unit.handlers[device_type].callacks[i])
                                fsdev_unit.handlers[device_type].callacks[i](FSDEV_REMOVED, pdbd->dbcc_name);
                        }
                        break;
                    }

                    default:
                    {
                        FS_INFO("Unhandled device message: %d", wparam);
                    }
                }
            }

            break;
        }

        case WM_CLOSE:
        {
            for (int i = 0; i < FSDEV_MAX; ++i)
            {
                if (fsdev_unit.handlers[i].dev_notify)
                    UnregisterDeviceNotification(fsdev_unit.handlers[i].dev_notify);
            }
            memset(fsdev_unit.handlers, 0, sizeof fsdev_unit.handlers);
            DestroyWindow(hwnd);
            break;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }

        default:
        {
            ret = DefWindowProc(hwnd, message, wparam, lparam);
            break;
        }
    }

    return ret;
}

static HWND fsdev_create_msg_window()
{
    static char const *WND_CLASS = "fsmsg_wnd";

    WNDCLASSEXA wx;
    memset(&wx, 0, sizeof wx);
    wx.cbSize = sizeof(WNDCLASSEX);
    wx.lpfnWndProc = (WNDPROC)fsdev_message_handler;
    wx.hInstance = (HINSTANCE)GetModuleHandle(0);
    wx.lpszClassName = WND_CLASS;

    if (!RegisterClassEx(&wx))
    {
        FS_ERR("Unable to register window class \'%s\'", WND_CLASS);
        return 0;
    }

    HWND hwnd = CreateWindowA(WND_CLASS, "DevNotifWnd", WS_ICONIC,
                              0, 0, CW_USEDEFAULT, 0, HWND_MESSAGE,
                              0, GetModuleHandle(0), 0);

    if (!hwnd)
        FS_ERR("Unable to create window for device messages receiving. Error: %d", GetLastError());

    return hwnd;
}

static void *fsdev_thread(void *param)
{
    (void)param;
    fsdev_unit.hwnd = fsdev_create_msg_window();
    fsdev_unit.is_active = true;
    if (!fsdev_unit.hwnd)
        return 0;

    MSG msg;
    int ret;
    while ((ret = GetMessage(&msg, NULL, 0, 0)))
    {
        if (ret == -1)
            break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

static bool fsdev_init()
{
    int rc = pthread_create(&fsdev_unit.thread, 0, fsdev_thread, 0);
    if (rc)
    {
        FS_ERR("Unable to create the thread for device messages receiving. Error: %d", rc);
        return false;
    }

    static struct timespec const ts = { 1, 0 };
    while(!fsdev_unit.is_active)
        nanosleep(&ts, NULL);

    return true;
}

static void fsdev_deinit()
{
    PostMessage(fsdev_unit.hwnd, WM_CLOSE, 0, 0);
    pthread_join(fsdev_unit.thread, 0);
    memset(&fsdev_unit, 0, sizeof fsdev_unit);
}

static bool fsdev_retain()
{
    if (fsdev_unit.rc++ == 0)
        return fsdev_init();
    return true;
}

static void fsdev_release()
{
    if (fsdev_unit.rc && --fsdev_unit.rc == 0)
        fsdev_deinit();
}

fsdev_handle_t fsdev_hotplug_register_callback(fsdev_type_t dev_type, fsdev_hotplug_callback_fn_t fn)
{
    if (!fn) return FSDEV_INVALID_HANDLE;

    for(short i = 0; i < FSDEV_MAX_CALLBACKS; ++i)
    {
        if (!fsdev_unit.handlers[dev_type].callacks[i])
        {
            if (!fsdev_retain())
                return FSDEV_INVALID_HANDLE;
            SendNotifyMessage(fsdev_unit.hwnd, WM_FSDEV_REGISTER_NOTIFICATION, (WPARAM)dev_type, 0);

            fsdev_unit.handlers[dev_type].callacks[i] = fn;

            fsdev_handle_desc_t ret;
            ret.fd.dev_type = dev_type;
            ret.fd.fn_id = i;
            return (fsdev_handle_t)ret.handle;
        }
    }

    FS_ERR("The maximum number of allowed handlers was reached for device %d.", dev_type);

    return FSDEV_INVALID_HANDLE;
}

void fsdev_hotplug_unregister_callback(fsdev_handle_t handle)
{
    fsdev_handle_desc_t h;
    h.handle = handle;
    fsdev_type_t dev_type = h.fd.dev_type;
    int fn_id = h.fd.fn_id;

    if (dev_type >= 0
        && dev_type < FSDEV_MAX
        && fn_id >= 0
        && fn_id < FSDEV_MAX_CALLBACKS
        && fsdev_unit.handlers[dev_type].callacks[fn_id])
    {
        SendNotifyMessage(fsdev_unit.hwnd, WM_FSDEV_UNREGISTER_NOTIFICATION, (WPARAM)dev_type, 0);
        fsdev_unit.handlers[dev_type].callacks[fn_id] = 0;
        fsdev_release();
    }
}

unsigned fsdev_get_all_volumes(fsdev_volume_t *volumes, unsigned size)
{
    if (!volumes || !size)
        return 0u;

    // Enumerate all volumes in the system.
    HANDLE find_handle = FindFirstVolumeA(volumes[0].name, sizeof volumes[0].name);
    if (find_handle == INVALID_HANDLE_VALUE)
    {
        FS_ERR("FindFirstVolume failed with error code %d\n", GetLastError());
        return 0;
    }

    unsigned i = 1;

    for(; i < size && FindNextVolumeA(find_handle, volumes[i].name, sizeof volumes[i].name); ++i);

    FindVolumeClose(find_handle);

    return i;
}
