#ifndef NATALIA_PED_REG_H
#define NATALIA_PED_REG_H

#include <stddef.h>
#include <stdint.h>

#include "ped_reg_ring.h"
#include "status.h"

#define PED_REG_FAULT_POWER  0x01U
#define PED_REG_FAULT_READY  0x02U
#define PED_REG_FAULT_STATUS 0x04U

typedef struct {
    uint16_t amp_d;
    uint16_t trig_stat;
    uint16_t t_trig;
    uint16_t t_pe_dead;
} PedRegEvent;

typedef struct {
    uint16_t n_d;
    uint16_t n_ac1;
    uint16_t n_ac2;
    uint16_t n_trig;
    uint16_t t_s_dead;
} PedRegCounters;

typedef struct {
    uint32_t events_read;
    uint32_t events_held;
    uint32_t seconds_marked;
    uint32_t seconds_lost;
    uint32_t ring_count;
    uint32_t ring_high_water;
} PedRegStats;

BoardStatus ped_reg_init(void);
BoardStatus ped_reg_power_on(void);
BoardStatus ped_reg_power_off(void);
BoardStatus ped_reg_is_powered(uint8_t* is_powered);
BoardStatus ped_reg_read_status(uint32_t* status);
BoardStatus ped_reg_write_config(const void* config, size_t size);
BoardStatus ped_reg_write_register(uint8_t address, uint16_t value);
BoardStatus ped_reg_read_event(void* event_buffer, size_t buffer_size, size_t* bytes_read);
BoardStatus ped_reg_read_counters(void* counters_buffer, size_t buffer_size, size_t* bytes_read);
BoardStatus ped_reg_set_inhibit(uint8_t enabled);
BoardStatus ped_reg_set_sleep(uint8_t enabled);
BoardStatus ped_reg_reset_trigger(void);

BoardStatus ped_reg_acquisition_start(void);
BoardStatus ped_reg_acquisition_stop(void);
BoardStatus ped_reg_take_records(PedRegRecord* records, size_t capacity, size_t* count);
BoardStatus ped_reg_take_records_raw(void* records, size_t capacity, size_t* count);
BoardStatus ped_reg_take_faults(uint32_t* faults);
BoardStatus ped_reg_get_stats(PedRegStats* stats);

BoardStatus ped_reg_take_trigger_pending(uint8_t* pending);

#if defined(NATALIA_PED_REG_INJECT) && (NATALIA_PED_REG_INJECT != 0)
BoardStatus ped_reg_inject_event(const uint16_t* words);
BoardStatus ped_reg_inject_second(void);
#endif

#endif
