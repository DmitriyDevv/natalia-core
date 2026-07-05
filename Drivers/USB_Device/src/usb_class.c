#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "usb_dev_config.h"
#include "usb_std_def.h"
#include "usb_dev.h"
#include "usb_class_cdc.h"

void USBclass_ClearEPStall(const struct usbdevice_ *usbd, uint8_t epaddr);
void USBclass_HandleRequest(const struct usbdevice_ *usbd);

static uint16_t usb_class_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

void USBclass_ClearEPStall(const struct usbdevice_ *usbd, uint8_t epaddr)
{
    (void)usbd;
    (void)epaddr;
}

static void USBclass_HandleCdcRequest(const struct usbdevice_ *usbd,
                                      uint8_t funidx)
{
    USB_SetupPacket *req = &usbd->devdata->req;
    uint16_t length;

    switch (req->bRequest) {
    case CDCRQ_SET_LINE_CODING:
        if ((req->bmRequestType.DirIn != 0U) ||
            (req->wLength != 7U) ||
            (usbd->outep[0].ptr == 0)) {
            USBdev_CtrlError(usbd);
            return;
        }

        length = usb_class_min_u16(req->wLength, 7U);

        if (memcmp(&usbd->cdc_data[funidx].LineCoding,
                   usbd->outep[0].ptr,
                   length) != 0) {
            memcpy(&usbd->cdc_data[funidx].LineCoding,
                   usbd->outep[0].ptr,
                   length);

            usbd->cdc_data[funidx].LineCodingChanged = 1U;

            if (usbd->cdc_service->SetLineCoding != 0) {
                usbd->cdc_service->SetLineCoding(usbd, funidx);
            }
        }

        USBdev_SendStatusOK(usbd);
        break;

    case CDCRQ_GET_LINE_CODING:
        if (req->bmRequestType.DirIn == 0U) {
            USBdev_CtrlError(usbd);
            return;
        }

        USBdev_SendStatus(usbd,
                          (const uint8_t *)&usbd->cdc_data[funidx].LineCoding,
                          usb_class_min_u16(req->wLength, 7U),
                          0);
        break;

    case CDCRQ_SET_CONTROL_LINE_STATE:
        if ((req->bmRequestType.DirIn != 0U) ||
            (req->wLength != 0U)) {
            USBdev_CtrlError(usbd);
            return;
        }

        if (usbd->cdc_data[funidx].ControlLineState != req->wValue.w) {
            usbd->cdc_data[funidx].ControlLineState = req->wValue.w;
            usbd->cdc_data[funidx].ControlLineStateChanged = 1U;

            if (usbd->cdc_service->SetControlLineState != 0) {
                usbd->cdc_service->SetControlLineState(usbd, funidx);
            }
        }

        USBdev_SendStatusOK(usbd);
        break;

    case CDCRQ_SEND_BREAK:
        if ((req->bmRequestType.DirIn != 0U) ||
            (req->wLength != 0U)) {
            USBdev_CtrlError(usbd);
            return;
        }

        USBdev_SendStatusOK(usbd);
        break;

    default:
        USBdev_CtrlError(usbd);
        break;
    }
}

void USBclass_HandleRequest(const struct usbdevice_ *usbd)
{
    USB_SetupPacket *req = &usbd->devdata->req;
    uint8_t interface;
    uint8_t classid;
    uint8_t funidx;

    if (req->bmRequestType.Recipient != USB_RQREC_INTERFACE) {
        USBdev_CtrlError(usbd);
        return;
    }

    interface = req->wIndex.b.l;

    if (interface >= USBD_NUM_INTERFACES) {
        USBdev_CtrlError(usbd);
        return;
    }

    classid = usbd->cfg->ifassoc[interface].classid;
    funidx = usbd->cfg->ifassoc[interface].funidx;

    if (classid != USB_CLASS_COMMUNICATIONS) {
        USBdev_CtrlError(usbd);
        return;
    }

    if (funidx >= USBD_CDC_CHANNELS) {
        USBdev_CtrlError(usbd);
        return;
    }

    USBclass_HandleCdcRequest(usbd, funidx);
}