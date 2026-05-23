#ifndef NATALIA_GPIO_H
#define NATALIA_GPIO_H

#include <stdint.h>

#include "board_pins.h"
#include "status.h"

typedef enum {
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT = 1,
    GPIO_MODE_ALTERNATE = 2,
    GPIO_MODE_ANALOG = 3
} GpioMode;

typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP = 1,
    GPIO_PULL_DOWN = 2
} GpioPull;

typedef enum {
    GPIO_OUTPUT_PUSH_PULL = 0,
    GPIO_OUTPUT_OPEN_DRAIN = 1
} GpioOutputType;

typedef enum {
    GPIO_SPEED_LOW = 0,
    GPIO_SPEED_MEDIUM = 1,
    GPIO_SPEED_HIGH = 2,
    GPIO_SPEED_VERY_HIGH = 3
} GpioSpeed;

typedef enum {
    GPIO_LEVEL_LOW = 0,
    GPIO_LEVEL_HIGH = 1
} GpioLevel;

typedef struct {
    GpioMode mode;
    GpioPull pull;
    GpioOutputType output_type;
    GpioSpeed speed;

    GpioLevel initial_level;
} GpioConfig;

BoardStatus gpio_enable_port_clock(GPIO_TypeDef *port);

BoardStatus gpio_configure(BoardPinId id, const GpioConfig *config);

BoardStatus gpio_write(BoardPinId id, GpioLevel level);

BoardStatus gpio_read(BoardPinId id, GpioLevel *level);

BoardStatus gpio_read_output_latch(BoardPinId id, GpioLevel *level);

BoardStatus gpio_set_disconnected(BoardPinId id);

#endif /* NATALIA_GPIO_H */