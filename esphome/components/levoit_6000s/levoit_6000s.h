#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "soc/usb_serial_jtag_reg.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <cstring>
#include <string>

namespace esphome {
namespace levoit_6000s {

class Levoit6000S : public Component {
 public:
  inline static Levoit6000S *instance = nullptr;

  float humidity = NAN;
  float temperature = NAN;
  float fan_speed = NAN;
  float target_humidity = NAN;
  float filter_life_pct = NAN;
  float filter_life_days = NAN;
  float timer_remaining = NAN;
  bool water_tank_missing = false;
  bool power_on = false;
  bool display_on = false;
  bool child_lock = false;
  std::string work_mode;
  bool state_dirty = false;

  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override {
    instance = this;

    volatile uint32_t *conf0 = (volatile uint32_t *) USB_SERIAL_JTAG_CONF0_REG;
    *conf0 &= ~USB_SERIAL_JTAG_USB_PAD_ENABLE;

    uart_config_t cfg = {};
    cfg.baud_rate = 115200;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(UART_NUM_0, &cfg);
    uart_set_pin(UART_NUM_0, 19, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 512, 512, 0, nullptr, 0);

    this->set_timeout("init", 30000, [this]() {
      send_wifi_ver_();
      init_done_ = true;
      ESP_LOGI(TAG, "Init complete");
      this->set_timeout("first_poll", 3000, [this]() {
        send_command(0x5531);
        last_poll_ = millis();
      });
    });
  }

  void loop() override {
    uint8_t tmp[128];
    int got;
    while ((got = uart_read_bytes(UART_NUM_0, tmp, sizeof(tmp), 0)) > 0) {
      for (int j = 0; j < got && buf_len_ < sizeof(buf_); j++)
        buf_[buf_len_++] = tmp[j];
    }

    while (buf_len_ >= 6) {
      size_t start = 0;
      while (start < buf_len_ && buf_[start] != 0xA5)
        start++;
      if (start == buf_len_) { buf_len_ = 0; break; }
      if (start > 0) {
        memmove(buf_, buf_ + start, buf_len_ - start);
        buf_len_ -= start;
      }
      if (buf_len_ < 6) break;

      size_t frame_len = 6 + buf_[3];
      if (frame_len > sizeof(buf_)) {
        memmove(buf_, buf_ + 1, buf_len_ - 1);
        buf_len_--;
        continue;
      }
      if (buf_len_ < frame_len) break;

      uint32_t sum = 0;
      for (size_t i = 0; i < frame_len; i++) {
        if (i != 5) sum += buf_[i];
      }
      if (buf_[5] != (uint8_t)(0xFF - (sum & 0xFF))) {
        memmove(buf_, buf_ + 1, buf_len_ - 1);
        buf_len_--;
        continue;
      }

      process_frame_(buf_, frame_len);
      memmove(buf_, buf_ + frame_len, buf_len_ - frame_len);
      buf_len_ -= frame_len;
    }

    if (init_done_) {
      uint32_t now = millis();
      if (now - last_poll_ >= 5000) {
        last_poll_ = now;
        send_command(0x5531);
      }
    }
  }

  void send_command(uint16_t opcode, const uint8_t *klv = nullptr, size_t klv_len = 0) {
    if (!init_done_) return;
    send_frame_(opcode, klv, klv_len);
  }

  bool is_init_done() const { return init_done_; }

 private:
  static constexpr const char *TAG = "levoit_6000s";
  uint8_t buf_[512];
  size_t buf_len_ = 0;
  uint8_t counter_ = 0x10;
  bool wifi_ver_sent_ = false;
  bool init_done_ = false;
  uint32_t last_poll_ = 0;
  uint8_t prev_klv_[64] = {};
  bool prev_klv_init_ = false;

  void send_frame_(uint16_t opcode, const uint8_t *klv, size_t klv_len) {
    size_t frame_len = 10 + klv_len;
    uint8_t frame[64];
    if (frame_len > sizeof(frame)) return;

    frame[0] = 0xA5;
    frame[1] = 0x22;
    frame[2] = counter_++;
    if (counter_ < 0x10) counter_ = 0x10;
    frame[3] = frame_len - 6;
    frame[4] = 0x00;
    frame[5] = 0x00;
    frame[6] = 0x02;
    frame[7] = opcode & 0xFF;
    frame[8] = (opcode >> 8) & 0xFF;
    frame[9] = 0x00;

    if (klv_len > 0 && klv != nullptr)
      memcpy(frame + 10, klv, klv_len);

    uint32_t sum = 0;
    for (size_t i = 0; i < frame_len; i++) {
      if (i != 5) sum += frame[i];
    }
    frame[5] = 0xFF - (sum & 0xFF);

    ESP_LOGD(TAG, "TX: opcode=0x%04X klv_len=%u", opcode, (unsigned) klv_len);
    uart_write_bytes(UART_NUM_0, frame, frame_len);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
  }

  void process_frame_(const uint8_t *frame, size_t len) {
    if (len < 10) return;
    uint8_t msg_type = frame[1];
    uint16_t opcode = ((uint16_t) frame[8] << 8) | frame[7];

    if (msg_type == 0x22) {
      ESP_LOGD(TAG, "RX op=0x%04X len=%u", opcode, (unsigned)(len - 10));
      if (opcode == 0x5530 && len > 10)
        parse_status_(frame + 10, len - 10);
      if (opcode == 0x5144 && !wifi_ver_sent_)
        send_wifi_ver_();
      send_ack_(opcode);
    } else if (msg_type == 0x12) {
      ESP_LOGD(TAG, "MCU ACK for opcode 0x%04X", opcode);
    } else if (msg_type == 0x52) {
      ESP_LOGW(TAG, "MCU ERROR for opcode 0x%04X", opcode);
    }
  }

  void parse_status_(const uint8_t *data, size_t len) {
    char line[256];
    size_t pos = 0;
    size_t i = 1;
    bool seen_first_01 = false;

    while (i + 2 <= len) {
      uint8_t key = data[i];
      uint8_t vlen = data[i + 1];

      if (key == 0x01 && seen_first_01) {
        water_tank_missing = (data[i + 1] != 0);
        pos += snprintf(line + pos, sizeof(line) - pos, "TANK=%u ", data[i + 1]);
        i += 2;
        continue;
      }
      if (key == 0x01) seen_first_01 = true;

      if (i + 2 + vlen > len) break;
      if (vlen == 0) { i += 2; continue; }
      const uint8_t *val = data + i + 2;

      if (vlen == 1)
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X=%u ", key, val[0]);
      else if (vlen == 2)
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X=%02X%02X ", key, val[0], val[1]);
      else if (vlen >= 3)
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X[%u]=%02X%02X%02X ", key, vlen, val[0], val[1], val[2]);

      switch (key) {
        case 0x05: if (vlen >= 2) display_on = (val[1] != 0); break;
        case 0x08: if (vlen >= 1) target_humidity = val[0]; break;
        case 0x09: if (vlen >= 1) humidity = val[0]; break;
        case 0x0A: if (vlen >= 1) temperature = val[0]; break;
        case 0x0C:
          if (vlen >= 1) {
            uint8_t m = val[0];
            if (m == 1) work_mode = "Sleep";
            else if (m == 5) work_mode = "Auto";
            else if (m == 9) work_mode = "Manual";
            else if (m == 0) work_mode = "Filter Dry";
          }
          break;
        case 0x15: if (vlen >= 1) fan_speed = val[0]; break;
        case 0x14:
          if (vlen >= 5) {
            uint16_t secs = (uint16_t) val[0] | ((uint16_t) val[1] << 8);
            timer_remaining = (val[3] == 0x01) ? secs / 3600.0f : 0;
          }
          break;
        case 0x16: if (vlen >= 1) filter_life_pct = val[0]; break;
        case 0x17: if (vlen >= 1) filter_life_days = val[0]; break;
        case 0x18: if (vlen >= 1) child_lock = (val[0] != 0); break;
        default: break;
      }
      i += 2 + vlen;
    }

    ESP_LOGI(TAG, "STATUS: %s", line);
    power_on = display_on;
    state_dirty = true;
  }

  void send_ack_(uint16_t opcode) {
    uint8_t frame[10] = {0xA5, 0x12, 0x00, 0x04, 0x00, 0x00, 0x02,
                         (uint8_t)(opcode & 0xFF), (uint8_t)((opcode >> 8) & 0xFF), 0x00};
    frame[2] = counter_++;
    if (counter_ < 0x10) counter_ = 0x10;

    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
      if (i != 5) sum += frame[i];
    }
    frame[5] = 0xFF - (sum & 0xFF);

    uart_write_bytes(UART_NUM_0, frame, 10);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
  }

  void send_wifi_ver_() {
    uint8_t klv[] = {0x01, 0x01, 0x00, 0x02, 0x01, 0x00};
    send_frame_(0xD010, klv, sizeof(klv));
    wifi_ver_sent_ = true;
    ESP_LOGI(TAG, "WiFi version sent to MCU");
  }
};

}  // namespace levoit_6000s
}  // namespace esphome
