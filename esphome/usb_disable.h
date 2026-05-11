#pragma once
#include "soc/usb_serial_jtag_reg.h"

// Runs before app_main() to free GPIO18/19 from USB-SERIAL-JTAG
static void __attribute__((constructor(101))) disable_usb_pad() {
  volatile uint32_t *conf0 = (volatile uint32_t *)USB_SERIAL_JTAG_CONF0_REG;
  *conf0 &= ~USB_SERIAL_JTAG_USB_PAD_ENABLE;
}
