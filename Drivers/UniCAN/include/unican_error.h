#ifndef UNICAN_ERROR_H
#define UNICAN_ERROR_H

#define UNICAN_OK                        (0U)

#define UNICAN_OFFLINE                   (1U)
#define UNICAN_WARNING_BUFFER_OVERWRITE  (2U)
#define UNICAN_WRONG_CRC                 (3U)
#define UNICAN_NO_FREE_BUFFER            (4U)
#define UNICAN_DATA_WITHOUT_START        (5U)
#define UNICAN_WARNING_UNEXPECTED_DATA   (6U)

#define UNICAN_CAN_MESSAGE_TOO_SHORT     (11U)
#define UNICAN_CAN_MESSAGE_TOO_LONG      (12U)
#define UNICAN_CAN_IDENTIFIER_TOO_LONG   (13U)
#define UNICAN_CAN_IDENTIFIER_EXT_TOO_LONG (14U)
#define UNICAN_HEADER_TOO_SHORT          (15U)

#define UNICAN_CANT_ALLOCATE_NODE        (16U)
#define UNICAN_HW_ERROR                  (17U)
#define UNICAN_MESSAGE_TIMEOUT           (18U)
#define UNICAN_READY_QUEUE_FULL          (19U)
#define UNICAN_MESSAGE_TOO_LONG          (20U)
#define UNICAN_TX_TIMEOUT               (21U)

#endif /* UNICAN_ERROR_H */