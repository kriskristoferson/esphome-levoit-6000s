#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome {
namespace levoit_6000s {

class Levoit6000S;

class Levoit6000SSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(Levoit6000S *parent) { parent_ = parent; }
  void set_switch_type(uint8_t type) { type_ = type; }
  void dump_config() override {}

 protected:
  void write_state(bool state) override;
  Levoit6000S *parent_{nullptr};
  uint8_t type_{0};
};

}  // namespace levoit_6000s
}  // namespace esphome
