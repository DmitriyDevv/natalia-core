#ifndef USB_DESC_GEN_H_
#define USB_DESC_GEN_H_

#include <stdint.h>

#include "usb_std_def.h"
#include "usb_desc_def.h"
#include "usb_class_cdc.h"
#include "usb_dev_config.h"

#ifndef __cplusplus
typedef uint16_t char16_t;
#endif

#define USB_DESC_IAD_LENGTH 8u
#define USB_DESC_INTERFACE_LENGTH 9u
#define USB_DESC_ENDPOINT_LENGTH 7u
#define CDC_DESC_HEADER_LENGTH 5u
#define CDC_DESC_CALL_MANAGEMENT_LENGTH 5u
#define CDC_DESC_ACM_LENGTH 4u
#define CDC_DESC_UNION_LENGTH 5u

#define STRLANGID(name, langid) \
static const struct { \
    uint8_t bLength; \
    uint8_t bDescriptorType; \
    uint16_t wLANGID[1]; \
} name = { \
    .bLength = 4u, \
    .bDescriptorType = USB_DESCTYPE_STRING, \
    .wLANGID = { (uint16_t)(langid) } \
}

#define STRINGDESC(name, value) \
static const struct { \
    uint8_t bLength; \
    uint8_t bDescriptorType; \
    char16_t bString[(sizeof(value) / sizeof(char16_t)) - 1u]; \
} name = { \
    .bLength = (uint8_t)sizeof(value), \
    .bDescriptorType = USB_DESCTYPE_STRING, \
    .bString = value \
}

#define IADESC(first_if, if_count, class_id, subclass_id, protocol_id, str_idx) \
{ \
    .bLength = USB_DESC_IAD_LENGTH, \
    .bDescriptorType = USB_DESCTYPE_IAD, \
    .bFirstInterface = (uint8_t)(first_if), \
    .bInterfaceCount = (uint8_t)(if_count), \
    .bFunctionClass = (uint8_t)(class_id), \
    .bFunctionSubClass = (uint8_t)(subclass_id), \
    .bFunctionProtocol = (uint8_t)(protocol_id), \
    .iFunction = (uint8_t)(str_idx) \
}

#define IFDESC(if_num, ep_count, class_id, subclass_id, protocol_id, str_idx) \
{ \
    .bLength = USB_DESC_INTERFACE_LENGTH, \
    .bDescriptorType = USB_DESCTYPE_INTERFACE, \
    .bInterfaceNumber = (uint8_t)(if_num), \
    .bAlternateSetting = 0u, \
    .bNumEndpoints = (uint8_t)(ep_count), \
    .bInterfaceClass = (uint8_t)(class_id), \
    .bInterfaceSubClass = (uint8_t)(subclass_id), \
    .bInterfaceProtocol = (uint8_t)(protocol_id), \
    .iInterface = (uint8_t)(str_idx) \
}

#define EPDESC(ep_addr, ep_type, ep_size, ep_interval) \
{ \
    .bLength = USB_DESC_ENDPOINT_LENGTH, \
    .bDescriptorType = USB_DESCTYPE_ENDPOINT, \
    .bEndpointAddress = (uint8_t)(ep_addr), \
    .bmAttributes = (uint8_t)(ep_type), \
    .wMaxPacketSize = USB16(ep_size), \
    .bInterval = (uint8_t)(ep_interval) \
}

#define CDC_HEADER_DESC(cdc_version) \
{ \
    CDC_DESC_HEADER_LENGTH, \
    CDC_CS_INTERFACE, \
    CDC_HEADER, \
    USB16(cdc_version) \
}

#define CDC_CALL_MANAGEMENT_DESC(capabilities, data_if) \
{ \
    CDC_DESC_CALL_MANAGEMENT_LENGTH, \
    CDC_CS_INTERFACE, \
    CDC_CALL_MANAGEMENT, \
    (uint8_t)(capabilities), \
    (uint8_t)(data_if) \
}

#define CDC_ACM_DESC(capabilities) \
{ \
    CDC_DESC_ACM_LENGTH, \
    CDC_CS_INTERFACE, \
    CDC_ABSTRACT_CONTROL_MANAGEMENT, \
    (uint8_t)(capabilities) \
}

#define CDC_UNION_DESC(control_if, data_if) \
{ \
    CDC_DESC_UNION_LENGTH, \
    CDC_CS_INTERFACE, \
    CDC_UNION, \
    (uint8_t)(control_if), \
    (uint8_t)(data_if) \
}

#define CDCVCOMIAD(control_if, str_idx) \
IADESC(control_if, 2u, USB_CLASS_COMMUNICATIONS, CDC_ABSTRACT_CONTROL_MODEL, 0u, str_idx)

#define CDCVCOMDESC(control_if, notify_ep, data_in_ep, data_out_ep, acm_capabilities) \
{ \
    IFDESC(control_if, 1u, USB_CLASS_COMMUNICATIONS, CDC_ABSTRACT_CONTROL_MODEL, 0u, 0u), \
    CDC_HEADER_DESC(CDC_V1_10), \
    CDC_CALL_MANAGEMENT_DESC(0u, (uint8_t)((control_if) + 1u)), \
    CDC_ACM_DESC(acm_capabilities), \
    CDC_UNION_DESC(control_if, (uint8_t)((control_if) + 1u)), \
    EPDESC(notify_ep, USB_EPTYPE_INT, CDC_INT_EP_SIZE, CDC_INT_POLLING_INTERVAL), \
    IFDESC((uint8_t)((control_if) + 1u), 2u, CDC_DATA_INTERFACE_CLASS, 0u, 0u, 0u), \
    EPDESC(data_out_ep, USB_EPTYPE_BULK, CDC_DATA_EP_SIZE, 0u), \
    EPDESC(data_in_ep, USB_EPTYPE_BULK, CDC_DATA_EP_SIZE, 0u) \
}

#endif