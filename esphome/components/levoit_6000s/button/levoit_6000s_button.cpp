#include "levoit_6000s_button.h"
#include "../levoit_6000s.h"

namespace esphome {
namespace levoit_6000s {

void Levoit6000SButton::press_action() {
  parent_->send_command(0x5544);
}

}  // namespace levoit_6000s
}  // namespace esphome
