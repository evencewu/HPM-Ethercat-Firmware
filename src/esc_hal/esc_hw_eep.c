/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information.
 */

#include "cc.h"
#include "esc.h"
#include "esc_eep.h"
#include "esc_hw_eep.h"

#include <string.h>

#include "sii_eeprom_data.c"

static uint8_t eep_buf[EEP_EMU_SIZE];

/** Initialize EEPROM emulation.
 *  Copy the default SII data (linked as sii_eeprom_data) into a RAM buffer.
 */
void EEP_init(void)
{
   memcpy(eep_buf, sii_eeprom_data, EEP_EMU_SIZE);
}

/** EEPROM emulation periodic task.
 *  No deferred flash writes in this minimal demo implementation.
 */
void EEP_hw_process(void)
{
   /* No-op: SII data is kept in RAM only. */
}

/** EEPROM read function.
 *
 * @param[in]   addr     = EEPROM byte address
 * @param[out]  data     = pointer to buffer for output data
 * @param[in]   count    = number of bytes to read
 * @return 0 on OK, 1 on error
 */
int8_t EEP_read(uint32_t addr, uint8_t *data, uint16_t count)
{
   if ((addr + count) > EEP_EMU_SIZE) {
      return 1;
   }

   memcpy(data, eep_buf + addr, count);
   return 0;
}

/** EEPROM write function.
 *
 * @param[in]   addr     = EEPROM byte address
 * @param[in]   data     = pointer to buffer of input data
 * @param[in]   count    = number of bytes to write
 * @return 0 on OK, 1 on error
 */
int8_t EEP_write(uint32_t addr, uint8_t *data, uint16_t count)
{
   if ((addr + count) > EEP_EMU_SIZE) {
      return 1;
   }

   memcpy(eep_buf + addr, data, count);
   /* Note: changes are RAM-only and lost after reset. */
   return 0;
}
