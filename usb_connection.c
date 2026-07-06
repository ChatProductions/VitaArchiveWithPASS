/*
 * VitaArchive - File Archiver & Browser for PS Vita
 * Created by theheroGAC.
 * Special thanks to TheFloW, Rinnegatamante, SKGleba, and all developers, hackers,
 * and contributors of the PlayStation Vita homebrew scene.
 */

#include "usb_connection.h"
#include <psp2/usbstorvstor.h>
#include <psp2/sysmodule.h>
#include <psp2/mtpif.h>
#include <taihen.h>
#include <psp2/io/stat.h>
#include <string.h>

static int usb_active = 0;
static SceUID usb_mod_id = -1;

extern int settings_usb_device;

int usb_connection_start(void) {
    if (usb_active) return 0;

    int res = -1;

    
    res = taiLoadStartKernelModule("ux0:app/VITARCH01/module/usbdevice.skprx", 0, NULL, 0);
    if (res < 0) {
        res = taiLoadStartKernelModule("ur0:app/VITARCH01/module/usbdevice.skprx", 0, NULL, 0);
    }
    
    if (res < 0) {
        res = taiLoadStartKernelModule("ux0:app/VITASHELL/module/usbdevice.skprx", 0, NULL, 0);
    }
    if (res < 0) {
        res = taiLoadStartKernelModule("ur0:app/VITASHELL/module/usbdevice.skprx", 0, NULL, 0);
    }

    if (res < 0) {
        return -1; 
    }

    usb_mod_id = res;

    
    sceMtpIfStopDriver(1);

    
    sceUsbstorVStorStop();

    
    sceUsbstorVStorSetDeviceInfo("\"PS Vita\" MC", "1.00");

    const char *path = NULL;
    char psvsd_path[64] = {0};

    if (settings_usb_device == 1) {
        path = "sdstor0:gcd-lp-ign-entire";
    } else if (settings_usb_device == 2) {
        path = "sdstor0:xmc-lp-ign-userext";
    } else if (settings_usb_device == 3) {
        SceIoStat stat;
        if (sceIoGetstat("sdstor0:uma-pp-act-a", &stat) >= 0) {
            path = "sdstor0:uma-pp-act-a";
        } else {
            path = "sdstor0:uma-lp-act-entire";
        }
    } else if (settings_usb_device == 4) {
        path = "sdstor0:gcd-lp-ign-gamero";
    }

    if (path != NULL) {
        res = sceUsbstorVStorSetImgFilePath(path);
        if (res >= 0) {
            res = sceUsbstorVStorStart(SCE_USBSTOR_VSTOR_TYPE_FAT);
            if (res >= 0) {
                usb_active = 1;
                return 0;
            }
        }
    } else {
        
        
        res = sceUsbstorVStorSetImgFilePath("sdstor0:gcd-lp-ign-entire");
        if (res >= 0) {
            res = sceUsbstorVStorStart(SCE_USBSTOR_VSTOR_TYPE_FAT);
            if (res >= 0) {
                usb_active = 1;
                return 0;
            }
        }

        
        sceUsbstorVStorStop();
        res = sceUsbstorVStorSetImgFilePath("sdstor0:xmc-lp-ign-userext");
        if (res >= 0) {
            res = sceUsbstorVStorStart(SCE_USBSTOR_VSTOR_TYPE_FAT);
            if (res >= 0) {
                usb_active = 1;
                return 0;
            }
        }

        
        sceUsbstorVStorStop();
        SceIoStat stat;
        if (sceIoGetstat("sdstor0:uma-pp-act-a", &stat) >= 0) {
            strncpy(psvsd_path, "sdstor0:uma-pp-act-a", sizeof(psvsd_path) - 1);
        } else {
            strncpy(psvsd_path, "sdstor0:uma-lp-act-entire", sizeof(psvsd_path) - 1);
        }
        res = sceUsbstorVStorSetImgFilePath(psvsd_path);
        if (res >= 0) {
            res = sceUsbstorVStorStart(SCE_USBSTOR_VSTOR_TYPE_FAT);
            if (res >= 0) {
                usb_active = 1;
                return 0;
            }
        }

        
        sceUsbstorVStorStop();
        res = sceUsbstorVStorSetImgFilePath("sdstor0:gcd-lp-ign-gamero");
        if (res >= 0) {
            res = sceUsbstorVStorStart(SCE_USBSTOR_VSTOR_TYPE_FAT);
            if (res >= 0) {
                usb_active = 1;
                return 0;
            }
        }
    }

    
    sceUsbstorVStorStop();
    sceMtpIfStartDriver(1);
    if (usb_mod_id >= 0) {
        taiStopUnloadKernelModule(usb_mod_id, 0, NULL, 0, NULL, NULL);
        usb_mod_id = -1;
    }
    return -1;
}

void usb_connection_stop(void) {
    if (!usb_active) return;
    sceUsbstorVStorStop();
    sceMtpIfStartDriver(1);
    if (usb_mod_id >= 0) {
        taiStopUnloadKernelModule(usb_mod_id, 0, NULL, 0, NULL, NULL);
        usb_mod_id = -1;
    }
    usb_active = 0;
}

int usb_connection_is_active(void) {
    return usb_active;
}
