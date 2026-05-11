#include "levoit_6000s_select.h"
#include "../levoit_6000s.h"

namespace esphome {
namespace levoit_6000s {

void Levoit6000SSelect::control(const std::string &value) {
  if (type_ == 0) {
    uint8_t mode_val;
    if (value == "Manual")
      mode_val = 0;
    else if (value == "Auto")
      mode_val = 1;
    else if (value == "Sleep")
      mode_val = 2;
    else
      return;
    uint8_t klv[] = {0x01, 0x01, mode_val};
    parent_->send_command(0x5537, klv, sizeof(klv));
  }
  publish_state(value);
}

}  // namespace levoit_6000s
}  // namespace esphome
