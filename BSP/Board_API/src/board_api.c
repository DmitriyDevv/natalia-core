#include "board_api.h"

#include <stdbool.h>

#include "gpio.h"
#include "board_startup_io.h"
#include "rtc.h"

BoardStatus board_init_hardware(void) {
    BoardStatus status;

    status = board_startup_io_init();
    if (status != BOARD_OK) {
        return status;
    }

    return rtc_init();
}

BoardStatus board_enter_safe_config(void) {
    return board_startup_io_init();
}

BoardStatus board_disconnect_signal_lines(BoardSignalTarget target) {
    (void)target;
    return BOARD_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
/* MRAM                                                                       */
/* ------------------------------------------------------------------------- */

BoardStatus board_mram_read(uint8_t copy_id,
                            uint32_t offset,
                            void* buffer,
                            size_t size) {
    (void)copy_id;
    (void)offset;
    (void)buffer;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_mram_write(uint8_t copy_id,
                             uint32_t offset,
                             const void* buffer,
                             size_t size) {
    (void)copy_id;
    (void)offset;
    (void)buffer;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t* is_valid) {
    (void)copy_id;
    (void)is_valid;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_mram_restore_copy(uint8_t source_copy_id,
                                    uint8_t target_copy_id) {
    (void)source_copy_id;
    (void)target_copy_id;

    return BOARD_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
/* NAND                                                                       */
/* ------------------------------------------------------------------------- */

BoardStatus board_nand_power_on(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_power_off(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t* is_powered) {
    (void)bank_id;
    (void)is_powered;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_connect(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_disconnect(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_read(uint8_t bank_id,
                            uint32_t address,
                            void* buffer,
                            size_t size) {
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_write(uint8_t bank_id,
                             uint32_t address,
                             const void* buffer,
                             size_t size) {
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_erase_start(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t* is_done) {
    (void)bank_id;
    (void)is_done;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t* is_full) {
    (void)bank_id;
    (void)is_full;

    return BOARD_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
/* PED                                                                        */
/* ------------------------------------------------------------------------- */

BoardStatus board_ped_power_on(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_is_powered(uint8_t* is_powered) {
    (void)is_powered;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_read_status(uint32_t* status) {
    (void)status;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_write_config(const void* config, size_t size) {
    (void)config;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_read_event(void* event_buffer,
                                 size_t buffer_size,
                                 size_t* bytes_read) {
    (void)event_buffer;
    (void)buffer_size;
    (void)bytes_read;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_set_inhibit(uint8_t enabled) {
    (void)enabled;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_set_sleep(uint8_t enabled) {
    (void)enabled;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_reset_trigger(void) {
    return BOARD_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
/* RTC                                                                        */
/* ------------------------------------------------------------------------- */

BoardStatus board_rtc_get_time(InstrumentTime* time) {
    return rtc_get_time(time);
}

BoardStatus board_rtc_set_time(const InstrumentTime* time) {
    return rtc_set_time(time);
}

/* ------------------------------------------------------------------------- */
/* USB                                                                        */
/* ------------------------------------------------------------------------- */

BoardStatus board_usb_write(const void* buffer,
                            size_t size,
                            size_t* bytes_written) {
    (void)buffer;
    (void)size;
    (void)bytes_written;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_usb_is_ready(uint8_t* is_ready) {
    (void)is_ready;
    return BOARD_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------------- */
/* Monitoring                                                                 */
/* ------------------------------------------------------------------------- */

static BoardStatus board_set_status_bit_from_pin(uint16_t* status_word,
                                                 uint8_t bit,
                                                 BoardPinId pin_id,
                                                 bool read_output_latch) {
    GpioLevel level;
    BoardStatus status;

    if (status_word == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (read_output_latch) {
        status = gpio_read_output_latch(pin_id, &level);
    } else {
        status = gpio_read(pin_id, &level);
    }

    if (status != BOARD_OK) {
        return status;
    }

    if (level == GPIO_LEVEL_HIGH) {
        *status_word |= (uint16_t)(1UL << bit);
    }

    return BOARD_OK;
}

BoardStatus board_read_power_status(uint32_t* power_status) {
    uint16_t status_word = 0U;
    BoardStatus status;

    if (power_status == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    /*
     * Bits 0..4: output control lines.
     */
    status = board_set_status_bit_from_pin(&status_word,
                                           0U,
                                           BOARD_PIN_PU_CAN1_SHDN,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           1U,
                                           BOARD_PIN_PU_CAN1_S,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           2U,
                                           BOARD_PIN_PU_CAN2_SHDN,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           3U,
                                           BOARD_PIN_PU_CAN2_S,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           4U,
                                           BOARD_PIN_PU_NAND1_PS,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    /*
     * Bit 5: actual NAND1 power indication input.
     */
    status = board_set_status_bit_from_pin(&status_word,
                                           5U,
                                           BOARD_PIN_PU_NAND1_PSON,
                                           false);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           6U,
                                           BOARD_PIN_PU_NAND2_PS,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           7U,
                                           BOARD_PIN_PU_NAND2_PSON,
                                           false);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           8U,
                                           BOARD_PIN_PU_PED_PS,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           9U,
                                           BOARD_PIN_PU_USB_VBUS,
                                           false);
    if (status != BOARD_OK) {
        return status;
    }



    status = board_set_status_bit_from_pin(&status_word,
                                           13U,
                                           BOARD_PIN_PED_INHIBIT,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           14U,
                                           BOARD_PIN_PED_SLEEP,
                                           true);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_set_status_bit_from_pin(&status_word,
                                           15U,
                                           BOARD_PIN_PED_PSON,
                                           false);
    if (status != BOARD_OK) {
        return status;
    }

    *power_status = (uint32_t)status_word;

    return BOARD_OK;
}
