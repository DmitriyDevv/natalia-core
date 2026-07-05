#ifdef SIMPLE_CDC

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "usb_hw.h"
#include "usb_dev_config.h"
#include "usb_std_def.h"
#include "usb_dev.h"
#include "usb_hw_if.h"
#include "usb_desc_gen.h"
#include "usb_log.h"
#include "usb_app.h"
#include "usbdev_binding.h"

static const struct cfgdesc_cdc_ ConfigDesc;

#define TX_TOUT 2U

#if defined(USBD_CDC_CHANNELS) && USBD_CDC_CHANNELS

static struct cdc_data_ cdc_data[USBD_CDC_CHANNELS] = {
    [0] = {
        .LineCoding = {
            .dwDTERate = 115200,
            .bDataBits = 8
        }
    }
};

#endif

static _Alignas(USB_SetupPacket) uint8_t ep0outpkt[USBD_CTRL_EP_SIZE];

static struct epdata_ out_epdata[USBD_NUM_EPPAIRS] = {
    {
        .ptr = ep0outpkt,
        .count = 0
    },
#if USBD_CDC_CHANNELS
    {
        .ptr = 0,
        .count = 0
    },
    {
        .ptr = cdc_data[0].RxData,
        .count = 0
    },
#endif
};

static struct epdata_ in_epdata[USBD_NUM_EPPAIRS];

#if USBD_CDC_CHANNELS

struct vcomcfg_ {
    uint8_t rx_irqn;
    uint8_t tx_irqn;
};

static const struct vcomcfg_ vcomcfg[USBD_CDC_CHANNELS] = {
    {
        VCOM0_rx_IRQn,
        VCOM0_tx_IRQn
    }
};

extern const struct usbdevice_ usbdev;

static struct cdc_SerialStateNotif_ ssnotif = {
    .bmRequestType = {
        .Recipient = USB_RQREC_INTERFACE,
        .Type = USB_RQTYPE_CLASS,
        .DirIn = 1
    },
    .bNotification = CDC_NOTIFICATION_SERIAL_STATE,
    .wIndex = 0,
    .wLength = 2,
    .wSerialState = 0
};

static void send_serialstate_notif(uint8_t ch) {
    ssnotif.wIndex = ConfigDesc.cdc[ch].cdcdesc.cdccomifdesc.bInterfaceNumber;
    ssnotif.wSerialState = cdc_data[ch].SerialState;

    if (USBdev_SendData(&usbdev,
                        ConfigDesc.cdc[ch].cdcdesc.cdcnotif.bEndpointAddress,
                        (const uint8_t*)&ssnotif,
                        sizeof(ssnotif),
                        0) == 0) {
        cdc_data[ch].SerialStateSent =
            ssnotif.wSerialState &
            (CDC_SERIAL_STATE_TX_CARRIER | CDC_SERIAL_STATE_RX_CARRIER);

        cdc_data[ch].SerialState ^=
            ssnotif.wSerialState &
            ~(CDC_SERIAL_STATE_TX_CARRIER | CDC_SERIAL_STATE_RX_CARRIER);
    }
}

static uint8_t vcom_data_in_epaddr(uint8_t ch) {
    switch (ch) {
    case 0U:
        return ConfigDesc.cdc[0].cdcdesc.cdcin.bEndpointAddress;
    default:
        return 0U;
    }
}

void vcom_write(uint8_t ch, const void* buffer, uint16_t size) {
    const uint8_t* source;

    if ((ch >= USBD_CDC_CHANNELS) || ((buffer == 0) && (size > 0U))) {
        return;
    }

    if (size == 0U) {
        return;
    }

    source = buffer;

    while ((size > 0U) && (cdc_data[ch].session.connected != false)) {
        struct cdc_data_* cdp = &cdc_data[ch];
        uint16_t free_size;
        uint16_t chunk_size;

        while ((cdp->session.connected != false) &&
            (cdp->session.TxLength == CDC_DATA_EP_SIZE)) {
            __NOP();
        }

        if (cdp->session.connected == false) {
            return;
        }

        __disable_irq();

        free_size = (uint16_t)(CDC_DATA_EP_SIZE - cdp->session.TxLength);
        chunk_size = (size < free_size) ? size : free_size;

        memcpy(&cdp->TxData[cdp->session.TxLength], source, chunk_size);

        cdp->session.TxLength = (uint8_t)(cdp->session.TxLength + chunk_size);
        source += chunk_size;
        size = (uint16_t)(size - chunk_size);

        if (cdp->session.TxLength == CDC_DATA_EP_SIZE) {
            cdp->session.TxTout = 0U;
            NVIC_SetPendingIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);
        } else {
            cdp->session.TxTout = TX_TOUT;
        }

        __enable_irq();
    }
}

void vcom0_write(const void* buffer, uint16_t size) {
    vcom_write(0U, buffer, size);
}

void vcom0_putc(uint8_t c) {
    vcom_write(0U, &c, 1U);
}

void vcom0_putstring(const char* s) {
    if (s == 0) {
        return;
    }

    vcom_write(0U, s, (uint16_t)strlen(s));
}

__attribute__((weak)) bool vcom_process_input(uint8_t ch, uint8_t c) {
    (void)ch;
    (void)c;

    return false;
}

static void allow_rx(uint8_t epn) {
    __disable_irq();
    usbdev.hwif->EnableRx(&usbdev, epn);
    __enable_irq();
}

bool vcom0_rxrdy(void) {
    return cdc_data[0].session.RxLength != 0U;
}

uint8_t vcom0_getc(void) {
    uint8_t c;

    while (!vcom0_rxrdy()) {
        __NOP();
    }

    c = cdc_data[0].RxData[cdc_data[0].session.RxIdx];

    ++cdc_data[0].session.RxIdx;

    if (cdc_data[0].session.RxIdx == cdc_data[0].session.RxLength) {
        cdc_data[0].session.RxLength = 0U;
        cdc_data[0].session.RxIdx = 0U;
        allow_rx(ConfigDesc.cdc[0].cdcdesc.cdcout.bEndpointAddress);
    }

    return c;
}

#endif

uint32_t usbdev_msec;

void usbdev_tick(void) {
    ++usbdev_msec;

#if USBD_CDC_CHANNELS
    for (uint8_t ch = 0U; ch < USBD_CDC_CHANNELS; ++ch) {
        struct cdc_data_* cdcp = &cdc_data[ch];

        if ((cdcp->session.TxTout != 0U) &&
            (--cdcp->session.TxTout == 0U)) {
            NVIC_SetPendingIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);
        }

        if (cdcp->SerialState != cdcp->SerialStateSent) {
            send_serialstate_notif(ch);
        }
    }
#endif
}

#if USBD_CDC_CHANNELS

static void cdc_LineStateHandler(const struct usbdevice_* usbd, uint8_t ch) {
    (void)usbd;

    if ((cdc_data[ch].ControlLineState & CDC_CTL_DTR) != 0U) {
        cdc_data[ch].SerialState |=
            CDC_SERIAL_STATE_TX_CARRIER | CDC_SERIAL_STATE_RX_CARRIER;

        cdc_data[ch].session.connected = true;
        cdc_data[ch].session.TxLength = 0U;
        cdc_data[ch].session.TxTout = 0U;

        NVIC_EnableIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);
    } else {
        NVIC_DisableIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);

        cdc_data[ch].session.connected = false;
        cdc_data[ch].session.TxLength = 0U;
        cdc_data[ch].session.TxTout = 0U;
    }

    cdc_data[ch].ControlLineStateChanged = false;
}

static void VCOM_rx_IRQHandler_Internal(uint8_t ch) {
    if (cdc_data[ch].LineCodingChanged != false) {
        cdc_data[ch].LineCodingChanged = false;
    }

    if (cdc_data[ch].ControlLineStateChanged != false) {
        cdc_data[ch].ControlLineStateChanged = false;
    }

    if (cdc_data[ch].session.RxLength != 0U) {
        uint8_t* rxptr = cdc_data[ch].RxData;

        for (uint8_t i = 0U; i < cdc_data[ch].session.RxLength; ++i) {
            (void)vcom_process_input(ch, rxptr[i]);
        }

        cdc_data[ch].session.RxLength = 0U;
        cdc_data[ch].session.RxIdx = 0U;

        allow_rx(ConfigDesc.cdc[ch].cdcdesc.cdcout.bEndpointAddress);
    }
}

void VCOM0_rx_IRQHandler(void) {
    VCOM_rx_IRQHandler_Internal(0U);
}

static void VCOM_tx_IRQHandler_Internal(uint8_t ch)
{
    struct cdc_data_ *cdp;
    uint8_t epaddr;

    if (ch >= USBD_CDC_CHANNELS) {
        return;
    }

    cdp = &cdc_data[ch];

    NVIC_DisableIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);

    if (cdp->session.connected == false) {
        cdp->session.TxLength = 0U;
        cdp->session.TxTout = 0U;
        return;
    }

    switch (ch) {
    case 0U:
        epaddr = ConfigDesc.cdc[0].cdcdesc.cdcin.bEndpointAddress;
        break;
    default:
        return;
    }

    if (cdp->session.TxLength == CDC_DATA_EP_SIZE) {
        NVIC_SetPendingIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);
    }

    if (USBdev_SendData(&usbdev,
                        epaddr,
                        cdp->TxData,
                        cdp->session.TxLength,
                        0) == 0) {
        cdp->session.TxLength = 0U;
        cdp->session.TxTout = 0U;
                        } else {
                            NVIC_EnableIRQ((IRQn_Type)vcomcfg[ch].tx_irqn);
                        }
}

void VCOM0_tx_IRQHandler(void) {
    VCOM_tx_IRQHandler_Internal(0U);
}

#endif

static void DataReceivedHandler(const struct usbdevice_* usbd, uint8_t epn) {
    uint16_t length = usbd->outep[epn].count;

    if (length == 0U) {
        usbd->hwif->EnableRx(usbd, epn);
        return;
    }

    switch (epn) {
#if USBD_CDC_CHANNELS
    case CDC0_DATA_OUT_EP:
        cdc_data[0].session.RxIdx = 0U;
        cdc_data[0].session.RxLength = (uint8_t)length;
        NVIC_SetPendingIRQ(VCOM0_rx_IRQn);
        break;
#endif
    default:
        break;
    }
}

static void DataSentHandler(const struct usbdevice_* usbd, uint8_t epn) {
    (void)usbd;

    switch (epn) {
#if USBD_CDC_CHANNELS
    case CDC0_DATA_IN_EP:
        NVIC_EnableIRQ(VCOM0_tx_IRQn);
        break;
#endif
    default:
        break;
    }
}

STRLANGID(sdLangID, USB_LANGID_US);

STRINGDESC(sdVendor, u"NATALIA");

STRINGDESC(sdName, u"NATALIA CDC");

STRINGDESC(sdSerial, u"0001");

#if USBD_CDC_CHANNELS
STRINGDESC(sdVcom0, u"DUMP");
#endif

enum usbd_sidx_ {
    USBD_SIDX_LANGID,
    USBD_SIDX_MFG,
    USBD_SIDX_PRODUCT,
    USBD_SIDX_SERIALNUM,
#if USBD_CDC_CHANNELS
    USBD_SIDX_FUN_VCOM0,
#endif
    USBD_NSTRINGDESCS
};

static const uint8_t* const strdescv[USBD_NSTRINGDESCS] = {
    &sdLangID.bLength,
    &sdVendor.bLength,
    &sdName.bLength,
    &sdSerial.bLength,
#if USBD_CDC_CHANNELS
    &sdVcom0.bLength,
#endif
};

static const struct USBdesc_device_ DevDesc = {
    .bLength = sizeof(struct USBdesc_device_),
    .bDescriptorType = USB_DESCTYPE_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = USB_CLASS_COMMUNICATIONS,
    .bDeviceSubClass = CDC_ABSTRACT_CONTROL_MODEL,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = USBD_CTRL_EP_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0000,
    .iManufacturer = USBD_SIDX_MFG,
    .iProduct = USBD_SIDX_PRODUCT,
    .iSerialNumber = USBD_SIDX_SERIALNUM,
    .bNumConfigurations = 1
};

static const struct cfgdesc_cdc_ ConfigDesc = {
    .cfgdesc = {
        .bLength = sizeof(struct USBdesc_config_),
        .bDescriptorType = USB_DESCTYPE_CONFIGURATION,
        .wTotalLength = USB16(sizeof(ConfigDesc)),
        .bNumInterfaces = USBD_NUM_INTERFACES,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = USB_CONFIGD_BUS_POWERED,
        .bMaxPower = USB_CONFIGD_POWER_mA(100)
    },
    .cdc = {
        [0] = {
            .cdcdesc = CDCVCOMDESC(IFNUM_CDC0_CONTROL,
                                   CDC0_INT_IN_EP,
                                   CDC0_DATA_IN_EP,
                                   CDC0_DATA_OUT_EP,
                                   CDCACM_FDCAP_LC_LS)
        }
    }
};

static const struct epcfg_ outcfg[USBD_NUM_EPPAIRS] = {
    {
        .ifidx = 0,
        .handler = 0
    },
#if USBD_CDC_CHANNELS
    {
        .ifidx = IFNUM_CDC0_CONTROL,
        .handler = 0
    },
    {
        .ifidx = IFNUM_CDC0_DATA,
        .handler = DataReceivedHandler
    },
#endif
};

static const struct epcfg_ incfg[USBD_NUM_EPPAIRS] = {
    {
        .ifidx = 0,
        .handler = 0
    },
#if USBD_CDC_CHANNELS
    {
        .ifidx = IFNUM_CDC0_CONTROL,
        .handler = 0
    },
    {
        .ifidx = IFNUM_CDC0_DATA,
        .handler = DataSentHandler
    },
#endif
};

const struct ifassoc_ if2fun[USBD_NUM_INTERFACES] = {
#if USBD_CDC_CHANNELS
    [IFNUM_CDC0_CONTROL] = {
        .classid = USB_CLASS_COMMUNICATIONS,
        .funidx = 0
    },
    [IFNUM_CDC0_DATA] = {
        .classid = USB_CLASS_COMMUNICATIONS,
        .funidx = 0
    },
#endif
};

static const struct usbdcfg_ usbdcfg = {
    .irqn = USB_IRQn,
    .irqpri = USB_IRQ_PRI,
    .numeppairs = USBD_NUM_EPPAIRS,
    .numif = USBD_NUM_INTERFACES,
    .nstringdesc = USBD_NSTRINGDESCS,
    .outepcfg = outcfg,
    .inepcfg = incfg,
    .ifassoc = if2fun,
    .devdesc = &DevDesc,
    .cfgdesc = &ConfigDesc.cfgdesc,
    .strdesc = (const uint8_t**)strdescv
};

static struct usbdevdata_ uddata;

static const struct cdc_services_ cdc_service = {
    .SetLineCoding = 0,
    .SetControlLineState = cdc_LineStateHandler
};

const struct usbdevice_ usbdev = {
    .usb = (void*)USB_BASE,
    .hwif = &usb_hw_services,
    .cfg = &usbdcfg,
    .devdata = &uddata,
    .outep = out_epdata,
    .inep = in_epdata,
    .SOF_Handler = usbdev_tick,
    .cdc_service = &cdc_service,
    .cdc_data = cdc_data
};

void USBapp_Init(void) {
#if USBD_CDC_CHANNELS
    NVIC_SetPriority(VCOM0_tx_IRQn, USB_IRQ_PRI);
    NVIC_SetPriority(VCOM0_rx_IRQn, USB_IRQ_PRI + 1U);
    NVIC_DisableIRQ(VCOM0_tx_IRQn);
    NVIC_EnableIRQ(VCOM0_rx_IRQn);
#endif

    NVIC_SetPriority((IRQn_Type)usbdev.cfg->irqn, USB_IRQ_PRI);
    usbdev.hwif->Init(&usbdev);
}

void USBapp_DeInit(void) {
#if USBD_CDC_CHANNELS
    NVIC_DisableIRQ(VCOM0_tx_IRQn);
    NVIC_DisableIRQ(VCOM0_rx_IRQn);

    cdc_data[0] = (struct cdc_data_){
        .LineCoding = {
            .dwDTERate = 115200,
            .bDataBits = 8
        }
    };

#endif

    usbdev.hwif->DeInit(&usbdev);
    memset(&uddata, 0, sizeof(uddata));
    memset(in_epdata, 0, sizeof(in_epdata));
}

void USBapp_Poll(void) {}

uint8_t USBapp_CdcIsReady(void) {
#if USBD_CDC_CHANNELS
    if (usbdev.devdata->devstate != USBD_STATE_CONFIGURED) {
        return 0U;
    }

    return (cdc_data[0].session.connected != false) ? 1U : 0U;
#else
    return 0U;
#endif
}

void USB_IRQHandler(void);

void USB_IRQHandler(void) {
    usbdev.hwif->IRQHandler(&usbdev);
}

#endif
