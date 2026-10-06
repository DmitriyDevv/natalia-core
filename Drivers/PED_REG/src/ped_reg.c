#include "ped_reg.h"

#include <stdbool.h>

#include "rtc.h"
#include "stm32l4xx.h"
#include "timebase.h"

#define PED_REG_READY_PIN      (1UL << 0U)
#define PED_REG_DIR_PIN        (1UL << 8U)
#define PED_REG_WRCLK_PIN      (1UL << 9U)
#define PED_REG_ADDR_MASK      (0x00FFUL)

#define PED_REG_POWER_PIN      (1UL << 6U)
#define PED_REG_TGRES_PIN      (1UL << 7U)
#define PED_REG_INHIBIT_PIN    (1UL << 8U)
#define PED_REG_SLEEP_PIN      (1UL << 9U)
#define PED_REG_TRIGGER_PIN    (1UL << 13U)
#define PED_REG_PSON_PIN       (1UL << 14U)

#define PED_REG_ADDR_STATUS_L  (0x00U)
#define PED_REG_ADDR_STATUS_H  (0x01U)
#define PED_REG_ADDR_AMP_D     (0x02U)
#define PED_REG_ADDR_TRIG_STAT (0x03U)
#define PED_REG_ADDR_T_TRIG    (0x04U)
#define PED_REG_ADDR_T_PE_DEAD (0x05U)
#define PED_REG_ADDR_N_D       (0x06U)
#define PED_REG_ADDR_N_AC1     (0x07U)
#define PED_REG_ADDR_N_AC2     (0x08U)
#define PED_REG_ADDR_N_TRIG    (0x09U)
#define PED_REG_ADDR_T_S_DEAD  (0x0AU)

#define PED_REG_STATUS_L_EXPECTED (0x455AU)
#define PED_REG_STATUS_H_EXPECTED (0x5941U)

#define PED_REG_POWERON_DELAY_MS     (200UL)
#define PED_REG_EVENT_SIZE           (8U)
#define PED_REG_COUNTERS_SIZE        (10U)
#define PED_REG_CONFIG_MAX_WORDS     (256U)

#define PED_REG_IRQ_PRIORITY         (0U)
#define PED_REG_BUS_SETTLE_NOPS      (3U)
#define PED_REG_WRCLK_PULSE_NOPS     (3U)
#define PED_REG_TGRES_PULSE_NOPS     (2U)
#define PED_REG_COUNTERS_SETTLE_NOPS (0U)
#define PED_REG_MS_ROUND_UP          (500U)

#define PED_REG_EXTICR_PORT_B        (1UL)
#define PED_REG_EXTICR_PORT_G        (6UL)

static PedRegRing ped_reg_ring;

void EXTI2_IRQHandler(void);
void EXTI15_10_IRQHandler(void);

static volatile uint8_t ped_reg_trigger_pending;
static volatile uint8_t ped_reg_power_expected;
static volatile uint8_t ped_reg_bus_active;
static volatile uint8_t ped_reg_events_enabled;
static volatile uint8_t ped_reg_seconds_enabled;
static volatile uint8_t ped_reg_event_held;
static volatile uint32_t ped_reg_faults;
static volatile uint32_t ped_reg_events_read;
static volatile uint32_t ped_reg_events_held;
static volatile uint32_t ped_reg_seconds_marked;

static void ped_reg_delay_nops(uint32_t count) {
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        __asm volatile ("nop");
    }
}

static void ped_reg_write_le_u16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value & 0x00FFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0x00FFU);
}

static uint16_t ped_reg_read_le_u16(const uint8_t* buffer) {
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
}

static void ped_reg_set_pin_output(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER &= ~(3UL << (pin_index * 2UL));
    gpio->MODER |= 1UL << (pin_index * 2UL);
}

static void ped_reg_set_pin_input(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER &= ~(3UL << (pin_index * 2UL));
}

static void ped_reg_set_pin_analog(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER |= 3UL << (pin_index * 2UL);
}

static void ped_reg_configure_bus_safe(void) {
    GPIOD->MODER = 0xFFFFFFFFUL;
    GPIOF->MODER = 0xFFFFFFFFUL;

    ped_reg_set_pin_analog(GPIOE, 7UL);
    ped_reg_set_pin_analog(GPIOE, 8UL);
    ped_reg_set_pin_analog(GPIOE, 9UL);
}

static void ped_reg_configure_power_control(void) {
    ped_reg_set_pin_output(GPIOE, 6UL);
    GPIOE->BSRR = PED_REG_POWER_PIN << 16U;
}

static void ped_reg_configure_monitor_inputs(void) {
    ped_reg_set_pin_input(GPIOG, 0UL);
    ped_reg_set_pin_input(GPIOG, 13UL);
    ped_reg_set_pin_input(GPIOG, 14UL);

    GPIOG->PUPDR &= ~(3UL << (0UL * 2UL));
    GPIOG->PUPDR &= ~(3UL << (13UL * 2UL));
    GPIOG->PUPDR &= ~(3UL << (14UL * 2UL));
}

static void ped_reg_configure_exti(void) {
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI2;
    SYSCFG->EXTICR[0] |= PED_REG_EXTICR_PORT_B << SYSCFG_EXTICR1_EXTI2_Pos;

    SYSCFG->EXTICR[3] &= ~(SYSCFG_EXTICR4_EXTI13 | SYSCFG_EXTICR4_EXTI14);
    SYSCFG->EXTICR[3] |= PED_REG_EXTICR_PORT_G << SYSCFG_EXTICR4_EXTI13_Pos;
    SYSCFG->EXTICR[3] |= PED_REG_EXTICR_PORT_G << SYSCFG_EXTICR4_EXTI14_Pos;

    EXTI->RTSR1 |= EXTI_RTSR1_RT2 | EXTI_RTSR1_RT13;
    EXTI->FTSR1 |= EXTI_FTSR1_FT14;

    EXTI->PR1 = EXTI_PR1_PIF2 | EXTI_PR1_PIF13 | EXTI_PR1_PIF14;
    EXTI->IMR1 |= EXTI_IMR1_IM2 | EXTI_IMR1_IM13 | EXTI_IMR1_IM14;

    NVIC_SetPriority(EXTI2_IRQn, PED_REG_IRQ_PRIORITY);
    NVIC_SetPriority(EXTI15_10_IRQn, PED_REG_IRQ_PRIORITY);
    NVIC_EnableIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void ped_reg_mask_irqs(void) {
    NVIC_DisableIRQ(EXTI2_IRQn);
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    __DSB();
    __ISB();
}

static void ped_reg_unmask_irqs(void) {
    NVIC_EnableIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void ped_reg_raise_fault(uint32_t fault) {
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    ped_reg_faults |= fault;
    ped_reg_bus_active = 0U;
    ped_reg_events_enabled = 0U;
    ped_reg_event_held = 0U;
    ped_reg_configure_bus_safe();

    if (primask == 0UL) {
        __enable_irq();
    }
}

static bool ped_reg_power_good(void) {
    return (GPIOG->IDR & PED_REG_PSON_PIN) != 0UL;
}

static bool ped_reg_ready_low(void) {
    return (GPIOG->IDR & PED_REG_READY_PIN) == 0UL;
}

static BoardStatus ped_reg_check_bus(void) {
    if (ped_reg_bus_active == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    if (!ped_reg_power_good()) {
        ped_reg_raise_fault(PED_REG_FAULT_POWER);
        return BOARD_ERR_NOT_READY;
    }

    if (!ped_reg_ready_low()) {
        ped_reg_raise_fault(PED_REG_FAULT_READY);
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}

static uint16_t ped_reg_register_read(uint8_t address) {
    GPIOF->BSRR = (((uint32_t)(~address) & PED_REG_ADDR_MASK) << 16U) |
        ((uint32_t)address & PED_REG_ADDR_MASK);

    ped_reg_delay_nops(PED_REG_BUS_SETTLE_NOPS);

    return (uint16_t)(GPIOD->IDR & 0xFFFFUL);
}

static BoardStatus ped_reg_register_write_raw(uint8_t address, uint16_t data) {
    uint8_t ready_before;
    uint8_t ready_after;

    GPIOD->MODER = 0xFFFFFFFFUL;

    GPIOF->BSRR = PED_REG_DIR_PIN;
    __asm volatile ("nop");

    ready_before = ((GPIOG->IDR & PED_REG_READY_PIN) != 0UL) ? 1U : 0U;

    if (ready_before != 0U) {
        GPIOD->MODER = 0x55555555UL;

        GPIOF->BSRR = (((uint32_t)(~address) & PED_REG_ADDR_MASK) << 16U) |
            ((uint32_t)address & PED_REG_ADDR_MASK);

        GPIOD->ODR = data;

        ped_reg_delay_nops(PED_REG_BUS_SETTLE_NOPS);

        GPIOF->BSRR = PED_REG_WRCLK_PIN;

        ped_reg_delay_nops(PED_REG_WRCLK_PULSE_NOPS);

        GPIOF->BSRR = PED_REG_WRCLK_PIN << 16U;
    }

    GPIOD->MODER = 0xFFFFFFFFUL;

    GPIOF->BSRR = PED_REG_DIR_PIN << 16U;
    __asm volatile ("nop");

    ready_after = ((GPIOG->IDR & PED_REG_READY_PIN) != 0UL) ? 1U : 0U;

    GPIOD->MODER = 0x00000000UL;

    if ((ready_before == 0U) || (ready_after != 0U)) {
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}

static void ped_reg_pulse_tgres(void) {
    GPIOE->BSRR = PED_REG_TGRES_PIN;

    ped_reg_delay_nops(PED_REG_TGRES_PULSE_NOPS);

    GPIOE->BSRR = PED_REG_TGRES_PIN << 16U;
}

static void ped_reg_fill_event_record(PedRegRecord* record) {
    record->kind = (uint8_t)PED_REG_RECORD_EVENT;
    record->flags = 0U;
    record->data[0] = ped_reg_register_read(PED_REG_ADDR_T_TRIG);
    record->data[1] = ped_reg_register_read(PED_REG_ADDR_T_PE_DEAD);
    record->data[2] = ped_reg_register_read(PED_REG_ADDR_AMP_D);
    record->data[3] = ped_reg_register_read(PED_REG_ADDR_TRIG_STAT);
    record->data[4] = 0U;
    record->rtc_seconds = 0U;
}

static void ped_reg_fill_counters(uint16_t* words) {
    words[0] = ped_reg_register_read(PED_REG_ADDR_N_D);
    words[1] = ped_reg_register_read(PED_REG_ADDR_N_AC1);
    words[2] = ped_reg_register_read(PED_REG_ADDR_N_AC2);
    words[3] = ped_reg_register_read(PED_REG_ADDR_N_TRIG);
    words[4] = ped_reg_register_read(PED_REG_ADDR_T_S_DEAD);
}

static void ped_reg_capture_event(void) {
    PedRegRecord record;

    ped_reg_fill_event_record(&record);
    ped_reg_pulse_tgres();

    if (ped_reg_ring_push_event(&ped_reg_ring, &record)) {
        ++ped_reg_events_read;
    }
}

static void ped_reg_on_trigger(void) {
    if (ped_reg_events_enabled == 0U) {
        ped_reg_trigger_pending = 1U;
        return;
    }

    if (ped_reg_event_held != 0U) {
        return;
    }

    if (!ped_reg_ring_has_event_room(&ped_reg_ring)) {
        ped_reg_event_held = 1U;
        ++ped_reg_events_held;
        return;
    }

    if (ped_reg_check_bus() != BOARD_OK) {
        return;
    }

    ped_reg_capture_event();
}

static void ped_reg_on_second(void) {
    PedRegRecord record;
    InstrumentTime now;
    uint32_t seconds;

    if (ped_reg_seconds_enabled == 0U) {
        return;
    }

    record.kind = (uint8_t)PED_REG_RECORD_SECOND;
    record.flags = 0U;
    record.data[0] = 0U;
    record.data[1] = 0U;
    record.data[2] = 0U;
    record.data[3] = 0U;
    record.data[4] = 0U;
    record.rtc_seconds = 0U;

    if (ped_reg_check_bus() == BOARD_OK) {
        ped_reg_delay_nops(PED_REG_COUNTERS_SETTLE_NOPS);
        ped_reg_fill_counters(record.data);
    } else {
        record.flags |= (uint8_t)PED_REG_RECORD_FLAG_NO_COUNTERS;
    }

    if (rtc_get_time(&now) == BOARD_OK) {
        seconds = now.seconds;
        if (now.milliseconds >= PED_REG_MS_ROUND_UP) {
            ++seconds;
        }
        record.rtc_seconds = seconds;
    } else {
        record.flags |= (uint8_t)PED_REG_RECORD_FLAG_NO_TIME;
    }

    if (ped_reg_ring_push_second(&ped_reg_ring, &record)) {
        ++ped_reg_seconds_marked;
    }
}

BoardStatus ped_reg_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIODEN |
        RCC_AHB2ENR_GPIOEEN |
        RCC_AHB2ENR_GPIOFEN |
        RCC_AHB2ENR_GPIOGEN;

    ped_reg_trigger_pending = 0U;
    ped_reg_power_expected = 0U;
    ped_reg_bus_active = 0U;
    ped_reg_events_enabled = 0U;
    ped_reg_seconds_enabled = 0U;
    ped_reg_event_held = 0U;
    ped_reg_faults = 0U;
    ped_reg_events_read = 0U;
    ped_reg_events_held = 0U;
    ped_reg_seconds_marked = 0U;
    ped_reg_ring_reset(&ped_reg_ring);

    ped_reg_configure_bus_safe();
    ped_reg_configure_power_control();
    ped_reg_configure_monitor_inputs();
    ped_reg_configure_exti();

    return BOARD_OK;
}

BoardStatus ped_reg_power_on(void) {
    uint16_t status_l;
    uint16_t status_h;

    if (ped_reg_power_good()) {
        ped_reg_raise_fault(PED_REG_FAULT_POWER);
        return BOARD_ERR_NOT_READY;
    }

    GPIOE->BSRR = PED_REG_POWER_PIN;

    timebase_delay_ms_blocking(PED_REG_POWERON_DELAY_MS);

    if (!ped_reg_power_good()) {
        ped_reg_raise_fault(PED_REG_FAULT_POWER);
        return BOARD_ERR_NOT_READY;
    }

    GPIOF->MODER &= ~(GPIO_MODER_MODE8 | GPIO_MODER_MODE9);
    GPIOF->MODER |= (1UL << GPIO_MODER_MODE8_Pos) |
        (1UL << GPIO_MODER_MODE9_Pos);
    GPIOF->BSRR = (PED_REG_DIR_PIN | PED_REG_WRCLK_PIN) << 16U;

    GPIOF->MODER &= 0xFFFF0000UL;
    GPIOF->MODER |= 0x00005555UL;
    GPIOF->BSRR = PED_REG_ADDR_MASK << 16U;

    if (!ped_reg_ready_low()) {
        ped_reg_raise_fault(PED_REG_FAULT_READY);
        return BOARD_ERR_NOT_READY;
    }

    GPIOD->MODER = 0x00000000UL;

    status_l = ped_reg_register_read(PED_REG_ADDR_STATUS_L);
    status_h = ped_reg_register_read(PED_REG_ADDR_STATUS_H);

    if ((status_l != PED_REG_STATUS_L_EXPECTED) ||
        (status_h != PED_REG_STATUS_H_EXPECTED)) {
        ped_reg_raise_fault(PED_REG_FAULT_STATUS);
        return BOARD_ERR_IO;
    }

    ped_reg_set_pin_output(GPIOE, 7UL);
    ped_reg_set_pin_output(GPIOE, 8UL);
    ped_reg_set_pin_output(GPIOE, 9UL);

    GPIOE->BSRR = (PED_REG_TGRES_PIN | PED_REG_INHIBIT_PIN |
        PED_REG_SLEEP_PIN) << 16U;

    ped_reg_power_expected = 1U;
    ped_reg_bus_active = 1U;

    return BOARD_OK;
}

BoardStatus ped_reg_power_off(void) {
    ped_reg_mask_irqs();

    ped_reg_power_expected = 0U;
    ped_reg_bus_active = 0U;
    ped_reg_events_enabled = 0U;
    ped_reg_event_held = 0U;
    ped_reg_configure_bus_safe();
    GPIOE->BSRR = PED_REG_POWER_PIN << 16U;

    ped_reg_unmask_irqs();

    return BOARD_OK;
}

BoardStatus ped_reg_is_powered(uint8_t* is_powered) {
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = ped_reg_power_good() ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus ped_reg_read_status(uint32_t* status) {
    uint16_t status_l = 0U;
    uint16_t status_h = 0U;
    BoardStatus board_status;

    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    ped_reg_mask_irqs();

    board_status = ped_reg_check_bus();
    if (board_status == BOARD_OK) {
        status_l = ped_reg_register_read(PED_REG_ADDR_STATUS_L);
        status_h = ped_reg_register_read(PED_REG_ADDR_STATUS_H);
    }

    ped_reg_unmask_irqs();

    *status = (uint32_t)status_l | ((uint32_t)status_h << 16U);

    return board_status;
}

BoardStatus ped_reg_write_register(uint8_t address, uint16_t value) {
    BoardStatus status;

    ped_reg_mask_irqs();

    status = ped_reg_check_bus();
    if (status == BOARD_OK) {
        status = ped_reg_register_write_raw(address, value);
        if (status != BOARD_OK) {
            ped_reg_raise_fault(PED_REG_FAULT_READY);
        }
    }

    ped_reg_unmask_irqs();

    return status;
}

BoardStatus ped_reg_write_config(const void* config, size_t size) {
    const uint8_t* bytes;
    size_t index;
    size_t word_count;
    BoardStatus status;

    if ((config == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (((size % 2U) != 0U) || ((size / 2U) > PED_REG_CONFIG_MAX_WORDS)) {
        return BOARD_ERR_INVALID_ARG;
    }

    bytes = config;
    word_count = size / 2U;

    for (index = 0U; index < word_count; ++index) {
        status = ped_reg_write_register((uint8_t)index,
                                        ped_reg_read_le_u16(&bytes[index * 2U]));
        if (status != BOARD_OK) {
            return status;
        }
    }

    return BOARD_OK;
}

BoardStatus ped_reg_read_event(void* event_buffer,
                               size_t buffer_size,
                               size_t* bytes_read) {
    uint8_t* buffer;
    PedRegEvent event = {0U, 0U, 0U, 0U};
    BoardStatus status;

    if (bytes_read == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_read = 0U;

    if ((event_buffer == 0) || (buffer_size < PED_REG_EVENT_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    ped_reg_mask_irqs();

    status = ped_reg_check_bus();
    if (status == BOARD_OK) {
        event.amp_d = ped_reg_register_read(PED_REG_ADDR_AMP_D);
        event.trig_stat = ped_reg_register_read(PED_REG_ADDR_TRIG_STAT);
        event.t_trig = ped_reg_register_read(PED_REG_ADDR_T_TRIG);
        event.t_pe_dead = ped_reg_register_read(PED_REG_ADDR_T_PE_DEAD);
        ped_reg_pulse_tgres();
    }

    ped_reg_unmask_irqs();

    if (status != BOARD_OK) {
        return status;
    }

    buffer = event_buffer;
    ped_reg_write_le_u16(&buffer[0], event.amp_d);
    ped_reg_write_le_u16(&buffer[2], event.trig_stat);
    ped_reg_write_le_u16(&buffer[4], event.t_trig);
    ped_reg_write_le_u16(&buffer[6], event.t_pe_dead);

    *bytes_read = PED_REG_EVENT_SIZE;

    return BOARD_OK;
}

BoardStatus ped_reg_read_counters(void* counters_buffer,
                                  size_t buffer_size,
                                  size_t* bytes_read) {
    uint8_t* buffer;
    uint16_t words[PED_REG_RECORD_DATA_WORDS];
    BoardStatus status;
    size_t i;

    if (bytes_read == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_read = 0U;

    if ((counters_buffer == 0) || (buffer_size < PED_REG_COUNTERS_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    ped_reg_mask_irqs();

    status = ped_reg_check_bus();
    if (status == BOARD_OK) {
        ped_reg_fill_counters(words);
    }

    ped_reg_unmask_irqs();

    if (status != BOARD_OK) {
        return status;
    }

    buffer = counters_buffer;
    for (i = 0U; i < PED_REG_RECORD_DATA_WORDS; ++i) {
        ped_reg_write_le_u16(&buffer[i * 2U], words[i]);
    }

    *bytes_read = PED_REG_COUNTERS_SIZE;

    return BOARD_OK;
}

BoardStatus ped_reg_set_inhibit(uint8_t enabled) {
    if (enabled != 0U) {
        GPIOE->BSRR = PED_REG_INHIBIT_PIN;
    } else {
        GPIOE->BSRR = PED_REG_INHIBIT_PIN << 16U;
    }

    return BOARD_OK;
}

BoardStatus ped_reg_set_sleep(uint8_t enabled) {
    if (enabled != 0U) {
        GPIOE->BSRR = PED_REG_SLEEP_PIN;
    } else {
        GPIOE->BSRR = PED_REG_SLEEP_PIN << 16U;
    }

    return BOARD_OK;
}

BoardStatus ped_reg_reset_trigger(void) {
    ped_reg_pulse_tgres();

    return BOARD_OK;
}

BoardStatus ped_reg_acquisition_start(void) {
    ped_reg_mask_irqs();

    ped_reg_ring_reset(&ped_reg_ring);
    ped_reg_trigger_pending = 0U;
    ped_reg_event_held = 0U;
    ped_reg_events_read = 0U;
    ped_reg_events_held = 0U;
    ped_reg_seconds_marked = 0U;

    EXTI->PR1 = EXTI_PR1_PIF2 | EXTI_PR1_PIF13;

    ped_reg_seconds_enabled = 1U;

    if (ped_reg_bus_active != 0U) {
        ped_reg_events_enabled = 1U;
        ped_reg_pulse_tgres();
    }

    ped_reg_unmask_irqs();

    return BOARD_OK;
}

BoardStatus ped_reg_acquisition_stop(void) {
    ped_reg_mask_irqs();

    ped_reg_events_enabled = 0U;
    ped_reg_seconds_enabled = 0U;
    ped_reg_event_held = 0U;

    ped_reg_unmask_irqs();

    return BOARD_OK;
}

BoardStatus ped_reg_take_records_raw(void* records, size_t capacity, size_t* count) {
    if ((count == 0) || ((records == 0) && (capacity > 0U))) {
        return BOARD_ERR_INVALID_ARG;
    }

    *count = (capacity == 0U) ? 0U : ped_reg_ring_pop_many(&ped_reg_ring, records, (uint32_t)capacity);

    if ((ped_reg_event_held != 0U) && ped_reg_ring_has_event_room(&ped_reg_ring)) {
        ped_reg_mask_irqs();

        if ((ped_reg_events_enabled != 0U) && (ped_reg_check_bus() == BOARD_OK)) {
            ped_reg_capture_event();
        }

        ped_reg_event_held = 0U;

        ped_reg_unmask_irqs();
    }

    return BOARD_OK;
}

BoardStatus ped_reg_take_records(PedRegRecord* records, size_t capacity, size_t* count) {
    return ped_reg_take_records_raw(records, capacity, count);
}

BoardStatus ped_reg_take_faults(uint32_t* faults) {
    uint32_t primask;

    if (faults == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    *faults = ped_reg_faults;
    ped_reg_faults = 0U;

    if (primask == 0UL) {
        __enable_irq();
    }

    return BOARD_OK;
}

BoardStatus ped_reg_get_stats(PedRegStats* stats) {
    if (stats == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    stats->events_read = ped_reg_events_read;
    stats->events_held = ped_reg_events_held;
    stats->seconds_marked = ped_reg_seconds_marked;
    stats->seconds_lost = ped_reg_ring_seconds_lost(&ped_reg_ring);
    stats->ring_count = ped_reg_ring_count(&ped_reg_ring);
    stats->ring_high_water = ped_reg_ring_high_water(&ped_reg_ring);

    return BOARD_OK;
}

void EXTI2_IRQHandler(void) {
    if ((EXTI->PR1 & EXTI_PR1_PIF2) != 0UL) {
        EXTI->PR1 = EXTI_PR1_PIF2;
        ped_reg_on_second();
    }
}

void EXTI15_10_IRQHandler(void) {
    if ((EXTI->PR1 & EXTI_PR1_PIF14) != 0UL) {
        EXTI->PR1 = EXTI_PR1_PIF14;

        if (ped_reg_power_expected != 0U) {
            ped_reg_power_expected = 0U;
            ped_reg_raise_fault(PED_REG_FAULT_POWER);
        }
    }

    if ((EXTI->PR1 & EXTI_PR1_PIF13) != 0UL) {
        EXTI->PR1 = EXTI_PR1_PIF13;
        ped_reg_on_trigger();
    }
}

BoardStatus ped_reg_take_trigger_pending(uint8_t* pending) {
    uint32_t primask;

    if (pending == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    *pending = ped_reg_trigger_pending;
    ped_reg_trigger_pending = 0U;

    if (primask == 0UL) {
        __enable_irq();
    }

    return BOARD_OK;
}

#if defined(NATALIA_PED_REG_INJECT) && (NATALIA_PED_REG_INJECT != 0)
BoardStatus ped_reg_inject_event(const uint16_t* words) {
    PedRegRecord record;

    if (words == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (ped_reg_seconds_enabled == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    if (!ped_reg_ring_has_event_room(&ped_reg_ring)) {
        ++ped_reg_events_held;
        return BOARD_ERR_BUSY;
    }

    record.kind = (uint8_t)PED_REG_RECORD_EVENT;
    record.flags = 0U;
    record.data[0] = words[0];
    record.data[1] = words[1];
    record.data[2] = words[2];
    record.data[3] = words[3];
    record.data[4] = 0U;
    record.rtc_seconds = 0U;

    if (!ped_reg_ring_push_event(&ped_reg_ring, &record)) {
        ++ped_reg_events_held;
        return BOARD_ERR_BUSY;
    }

    ++ped_reg_events_read;

    return BOARD_OK;
}

BoardStatus ped_reg_inject_second(void) {
    if (ped_reg_seconds_enabled == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    ped_reg_on_second();

    return BOARD_OK;
}
#endif
