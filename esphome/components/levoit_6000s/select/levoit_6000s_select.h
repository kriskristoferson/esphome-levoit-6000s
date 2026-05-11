#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

namespace esphome {
namespace levoit_6000s {

class Levoit6000S;

class Levoit6000SSelect : public select::Select, public Component {
 public:
  void set_parent(Levoit6000S *parent) { parent_ = parent; }
  void set_select_type(uint8_t type) { type_ = type; }
  void dump_config() override {}

 protected:
  void control(const std::string &value) override;
  Levoit6000S *parent_{nullptr};
  uint8_t type_{0};
};

}  // namespace levoit_6000s
}  // namespace esphome
