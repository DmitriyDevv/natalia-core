#ifndef NATALIA_PED_REG_H
#define NATALIA_PED_REG_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    uint16_t amp;
    uint16_t status;
    uint16_t trigger;
    uint16_t dead_time;
} PedRegEvent;

BoardStatus ped_reg_init(void);
BoardStatus ped_reg_power_on(void);
BoardStatus ped_reg_power_off(void);
BoardStatus ped_reg_is_powered(uint8_t* is_powered);
BoardStatus ped_reg_read_status(uint32_t* status);
BoardStatus ped_reg_write_config(const void* config, size_t size);
BoardStatus ped_reg_read_event(void* event_buffer, size_t buffer_size, size_t* bytes_read);
BoardStatus ped_reg_set_inhibit(uint8_t enabled);
BoardStatus ped_reg_set_sleep(uint8_t enabled);
BoardStatus ped_reg_reset_trigger(void);

void ped_reg_handle_exti15_10_irq(void);
BoardStatus ped_reg_take_trigger_pending(uint8_t* pending);
BoardStatus ped_reg_take_power_alarm_pending(uint8_t* pending);
BoardStatus ped_reg_take_ready_alarm_pending(uint8_t* pending);

#endif
