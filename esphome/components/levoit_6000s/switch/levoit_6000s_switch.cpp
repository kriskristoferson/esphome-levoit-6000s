#include "levoit_6000s_switch.h"
#include "../levoit_6000s.h"

namespace esphome {
namespace levoit_6000s {

static constexpr uint16_t SWITCH_OPCODES[] = {
    0x5000,  // POWER
    0x500F,  // DISPLAY
    0x5546,  // AUTO_DRY
};

void Levoit6000SSwitch::write_state(bool state) {
  if (type_ >= sizeof(SWITCH_OPCODES) / sizeof(SWITCH_OPCODES[0]))
    return;
  uint8_t klv[] = {0x01, 0x01, (uint8_t)(state ? 1 : 0)};
  parent_->send_command(SWITCH_OPCODES[type_], klv, sizeof(klv));
}

}  // namespace levoit_6000s
}  // namespace esphome
