#include "ped_reg.h"

void EXTI15_10_IRQHandler(void) {
    ped_reg_handle_exti15_10_irq();
}
