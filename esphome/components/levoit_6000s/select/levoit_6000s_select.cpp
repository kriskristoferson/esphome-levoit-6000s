#include "levoit_6000s_select.h"
#include "../levoit_6000s.h"

namespace esphome {
namespace levoit_6000s {

void Levoit6000SSelect::control(const std::string &value) {
  if (type_ == 0) {
    uint8_t on[] = {0x01, 0x01, 0x01};
    uint8_t off[] = {0x01, 0x01, 0x00};
    if (value == "Filter Dry L" || value == "Filter Dry H") {
      parent_->send_command(0x5000, on, sizeof(on));
      parent_->send_command(0x5000, off, sizeof(off));
      return;
    }
    uint8_t mode_val;
    if (value == "Manual")
      mode_val = 9;
    else if (value == "Auto")
      mode_val = 5;
    else if (value == "Sleep")
      mode_val = 1;
    else
      return;
    parent_->send_command(0x5000, on, sizeof(on));
    uint8_t klv[] = {0x01, 0x01, mode_val};
    parent_->send_command(0x5537, klv, sizeof(klv));
  }
}

}  // namespace levoit_6000s
}  // namespace esphome
