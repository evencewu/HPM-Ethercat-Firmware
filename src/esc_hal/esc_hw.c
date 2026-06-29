/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information.
 */

#include "esc_hw.h"
#include "esc.h"
#include "esc_eep.h"
#include "esc_hw_eep.h"

#include <string.h>

#define ESCADDR(x)   (((uint8_t *)HPM_ESC_BASE) + (x))

/* HPM5E00 ESC AL event register is 32-bit; SOES expects 16-bit in ESCvar.ALevent */
#define ESC_AL_EVENT_REG    (HPM_ESC->AL_EVT_REQ)

/** ESC read function used by the Slave stack.
 *
 * @param[in]   address     = address of ESC register to read
 * @param[out]  buf         = pointer to buffer to read into
 * @param[in]   len         = number of bytes to read
 */
void ESC_read(uint16_t address, void *buf, uint16_t len)
{
   ESCvar.ALevent = etohs((uint16_t)ESC_AL_EVENT_REG);
   memcpy(buf, ESCADDR(address), len);

   /* HPM5E00 workaround: DL Status register has no bit 0 (PDI Operational).
    * Fake it so SOES ecat_slv_init() doesn't hang in its DLstatus wait loop. */
   if (address == ESCREG_DLSTATUS) {
      *(uint16_t *)buf |= 0x0001;
   }
}

/** ESC write function used by the Slave stack.
 *
 * @param[in]   address     = address of ESC register to write
 * @param[in]   buf         = pointer to buffer to write from
 * @param[in]   len         = number of bytes to write
 */
void ESC_write(uint16_t address, void *buf, uint16_t len)
{
   ESCvar.ALevent = etohs((uint16_t)ESC_AL_EVENT_REG);
   memcpy(ESCADDR(address), buf, len);
}

/** ESC emulated EEPROM handler.
 *  Called periodically from the main loop when using EEPROM emulation.
 */
void ESC_eep_handler(void)
{
   EEP_process();
   EEP_hw_process();
}

/** ESC reset.
 *  Puts ESC in a known state and re-initializes EEPROM emulation.
 */
void ESC_reset(void)
{
   /* Note: ESC clock/pin/PHY initialization is done by board_init_ethercat()
    * in the application before ecat_slv_init() calls this function.
    */

   /* Initialize emulated EEPROM from flash/SII data */
   EEP_init();
}

/** ESC init.
 *  Called by ecat_slv_init() after ESC_reset().
 *  Most hardware setup is board-specific and already done by the BSP.
 */
void ESC_init(const esc_cfg_t *cfg)
{
   (void)cfg;

   /* HPM5E00 has an integrated ESC. The board support package has already:
    * - enabled ESC clock
    * - configured ESC pins (MII, MDIO, NMII_LINK, PHY reset)
    * - enabled EEPROM emulation mode
    * - released PHY reset
    * - set PHY offset
    *
    * SOES stack takes over from here.
    */
}

/** Write SM configuration to ESC hardware via PDI.
 *  HPM5E00 ESC SM registers are not writable via EtherCAT FPWR,
 *  so the slave must program them directly via AHB bus.
 *  Only PSA, Length and Control are written; Status and Activate are preserved.
 */
void ESC_hw_sm_write(uint8_t sm, uint16_t psa, uint16_t len, uint8_t cmd)
{
   uint16_t base = ESCREG_SM0 + (sm * 8);
   uint16_t tmp;
   tmp = htoes(psa);
   memcpy(ESCADDR(base + 0), &tmp, 2);
   tmp = htoes(len);
   memcpy(ESCADDR(base + 2), &tmp, 2);
   ESCADDR(base + 4)[0] = cmd;
}

/** Configure BOOT SM0 and SM1 via PDI before the state machine checks them. */
void ESC_hw_sm_boot_cfg(void)
{
   ESC_hw_sm_write(0, MBX0_sma_b, MBX0_sml_b, MBX0_smc_b);
   ESC_hw_sm_write(1, MBX1_sma_b, MBX1_sml_b, MBX1_smc_b);
}
