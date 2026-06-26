/*
 * Minimal test using SDK's verified ecat_hardware_init() directly.
 * If this works, the issue is in our custom esc_full_init().
 */
#include <stdio.h>
#include "board.h"
#include "hpm_esc_drv.h"
#include "hpm_ecat_hw.h"
#include "esc_eep.h"
#include "esc.h"

/* Global variable needed by SOES esc_eep.c */
_ESCvar ESCvar;

/* esc_hw_eep_handler callback for EEPROM event handling */
void ESC_eep_handler(void);

int main(void)
{
    hpm_stat_t stat;

    board_init();
    board_init_led_pins();

    printf("HPM5E00 SDK ecat_hardware_init test\n");

    /* Use SDK's verified EtherCAT hardware init (replaces esc_full_init) */
    stat = ecat_hardware_init(HPM_ESC);
    if (stat != status_success) {
        printf("ecat_hardware_init failed: %d\n", stat);
        while (1) { board_led_toggle(); board_delay_ms(500); }
    }

    printf("ESC hardware init OK, starting EEPROM poll loop\n");

    uint32_t cnt = 0;
    while (1) {
        /* Handle EEPROM commands from the master */
        ESC_eep_handler();

        cnt++;
        if (!(cnt & 0x7FF)) {
            board_led_toggle();
        }
    }
    return 0;
}
