#include "levoit_6000s_number.h"
#include "../levoit_6000s.h"

namespace esphome {
namespace levoit_6000s {

static constexpr uint16_t NUMBER_OPCODES[] = {
    0x5536,  // TARGET_HUMIDITY
    0x5019,  // TIMER
};

void Levoit6000SNumber::control(float value) {
  if (type_ >= sizeof(NUMBER_OPCODES) / sizeof(NUMBER_OPCODES[0]))
    return;

  if (type_ == 1) {
    uint32_t secs = (uint32_t) value * 3600;
    uint8_t klv[] = {0x01, 0x04, (uint8_t)(secs & 0xFF), (uint8_t)((secs >> 8) & 0xFF),
                     (uint8_t)((secs >> 16) & 0xFF), (uint8_t)((secs >> 24) & 0xFF)};
    parent_->send_command(NUMBER_OPCODES[type_], klv, sizeof(klv));
  } else {
    uint8_t klv[] = {0x01, 0x01, (uint8_t) value};
    parent_->send_command(NUMBER_OPCODES[type_], klv, sizeof(klv));
  }
}

}  // namespace levoit_6000s
}  // namespace esphome
