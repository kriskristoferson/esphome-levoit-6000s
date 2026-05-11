#pragma once

#include "esphome/components/button/button.h"

namespace esphome {
namespace levoit_6000s {

class Levoit6000S;

class Levoit6000SButton : public button::Button {
 public:
  void set_parent(Levoit6000S *parent) { parent_ = parent; }

 protected:
  void press_action() override;
  Levoit6000S *parent_{nullptr};
};

}  // namespace levoit_6000s
}  // namespace esphome
