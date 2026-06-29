/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information.
 */

#ifndef __esc_hw_eep__
#define __esc_hw_eep__

#include <stdint.h>

/* SII EEPROM size in bytes */
#define EEP_EMU_SIZE   2048

/* periodic task */
void EEP_hw_process(void);

#endif /* __esc_hw_eep__ */
