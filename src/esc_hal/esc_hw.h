/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information.
 */

#ifndef __esc_hw__
#define __esc_hw__

#include <stdint.h>
#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_esc_drv.h"

void ESC_eep_handler(void);

/* HPM5E00-specific: write SM config via PDI (AHB bus) */
void ESC_hw_sm_write(uint8_t sm, uint16_t psa, uint16_t len, uint8_t cmd);
void ESC_hw_sm_boot_cfg(void);

#endif /* __esc_hw__ */
