/*
 * Minimal ESC test with EEPROM event handling.
 * Goal: make the slave discoverable by SOEM slaveinfo.
 */
#include <stdio.h>
#include "board.h"
#include "hpm_esc_drv.h"
#include "hpm_gpio_drv.h"
#include "esc_eep.h"
#include "esc.h"

void esc_full_init(void);     /* from esc_init.c */
void ESC_eep_handler(void);   /* from esc_hw.c */

/* Minimal global variables needed by SOES esc_eep.c and esc_hw.c */
_ESCvar ESCvar;

int main(void)
{
    board_init();
    board_init_led_pins();

    printf("HPM5E00 ESC minimal + EEPROM handler\n");

    esc_full_init();

    printf("ESC init done, entering poll loop\n");

    uint32_t cnt = 0;
    while (1) {
        /* Poll ESC EEPROM events - critical for master EEPROM reads */
        ESC_eep_handler();

        cnt++;
        if (!(cnt & 0x7FF)) {
            board_led_toggle();
        }
    }
    return 0;
}
