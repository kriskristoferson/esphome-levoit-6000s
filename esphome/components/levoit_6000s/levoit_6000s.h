#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

namespace esphome {
namespace levoit_6000s {

class Levoit6000S : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void send_command(uint16_t opcode);
  void send_command(uint16_t opcode, const uint8_t *klv, size_t klv_len);

#ifdef USE_SENSOR
  void set_current_humidity_sensor(sensor::Sensor *s) { current_humidity_ = s; }
  void set_current_temp_sensor(sensor::Sensor *s) { current_temp_ = s; }
  void set_fan_speed_sensor(sensor::Sensor *s) { fan_speed_ = s; }
  void set_filter_life_pct_sensor(sensor::Sensor *s) { filter_life_pct_ = s; }
  void set_filter_life_days_sensor(sensor::Sensor *s) { filter_life_days_ = s; }
  void set_timer_remaining_sensor(sensor::Sensor *s) { timer_remaining_ = s; }
#endif

#ifdef USE_BINARY_SENSOR
  void set_water_tank_missing(binary_sensor::BinarySensor *s) { water_tank_missing_ = s; }
#endif

#ifdef USE_SWITCH
  void register_switch(uint8_t type, switch_::Switch *sw);
#endif

#ifdef USE_NUMBER
  void register_number(uint8_t type, number::Number *num);
#endif

#ifdef USE_SELECT
  void register_select(uint8_t type, select::Select *sel);
#endif

 protected:
  void process_frame_(const uint8_t *frame, size_t len);
  void parse_status_(const uint8_t *klv, size_t len);
  void send_ack_(uint16_t opcode);
  void send_wifi_ver_();

#ifdef USE_SENSOR
  sensor::Sensor *current_humidity_{nullptr};
  sensor::Sensor *current_temp_{nullptr};
  sensor::Sensor *fan_speed_{nullptr};
  sensor::Sensor *filter_life_pct_{nullptr};
  sensor::Sensor *filter_life_days_{nullptr};
  sensor::Sensor *timer_remaining_{nullptr};
#endif

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *water_tank_missing_{nullptr};
#endif

#ifdef USE_SWITCH
  // 0=POWER, 1=DISPLAY, 2=AUTO_DRY
  switch_::Switch *switches_[3]{nullptr};
#endif

#ifdef USE_NUMBER
  // 0=TARGET_HUMIDITY, 1=TIMER
  number::Number *numbers_[2]{nullptr};
#endif

#ifdef USE_SELECT
  // 0=MODE
  select::Select *selects_[1]{nullptr};
#endif

  uint8_t buf_[256];
  size_t buf_len_{0};
  uint8_t counter_{0x10};
  uint32_t last_query_{0};
  uint32_t last_temp_push_{0};
  bool wifi_ver_sent_{false};
  bool init_done_{false};
  uint8_t last_humidity_{0};
  uint8_t last_temp_{0};
};

}  // namespace levoit_6000s
}  // namespace esphome
