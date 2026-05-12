#include "levoit_6000s.h"
#include "esphome/core/log.h"
#include "soc/usb_serial_jtag_reg.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <cstring>

namespace esphome {
namespace levoit_6000s {

static const char *const TAG = "levoit_6000s";

void Levoit6000S::setup() {
  // Disable USB-SERIAL-JTAG pads and re-route UART0 to GPIO18/19.
  // ESP-IDF init may re-enable USB pads after our constructor disabled them,
  // causing uart_set_pin() in the UART component to fail silently.
  volatile uint32_t *conf0 = (volatile uint32_t *) USB_SERIAL_JTAG_CONF0_REG;
  *conf0 &= ~USB_SERIAL_JTAG_USB_PAD_ENABLE;
  // Re-apply pin routing now that USB pads are definitely released
  esp_err_t err = uart_set_pin(UART_NUM_0, 19, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  ESP_LOGI(TAG, "USB disabled, UART pins re-routed: %s", esp_err_to_name(err));
  ESP_LOGI(TAG, "Levoit 6000S — passive listen 30s then init");
  this->set_timeout("wifi_ver", 30000, [this]() {
    send_wifi_ver_();
    this->set_timeout("first_query", 5000, [this]() {
      send_command(0x5531);
      init_done_ = true;
      last_query_ = millis();
    });
  });
}

void Levoit6000S::loop() {
  // Read directly from UART0 — ESPHome assigns wrong uart_num_ for this hardware config
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
    if (start == buf_len_) {
      buf_len_ = 0;
      break;
    }
    if (start > 0) {
      memmove(buf_, buf_ + start, buf_len_ - start);
      buf_len_ -= start;
    }
    if (buf_len_ < 6)
      break;

    size_t frame_len = 6 + buf_[3];
    if (frame_len > sizeof(buf_)) {
      memmove(buf_, buf_ + 1, buf_len_ - 1);
      buf_len_--;
      continue;
    }
    if (buf_len_ < frame_len)
      break;

    uint32_t sum = 0;
    for (size_t i = 0; i < frame_len; i++) {
      if (i != 5)
        sum += buf_[i];
    }
    uint8_t expected = 0xFF - (sum & 0xFF);
    if (buf_[5] != expected) {
      ESP_LOGW(TAG, "Bad checksum: got=0x%02X expected=0x%02X", buf_[5], expected);
      memmove(buf_, buf_ + 1, buf_len_ - 1);
      buf_len_--;
      continue;
    }

    process_frame_(buf_, frame_len);
    memmove(buf_, buf_ + frame_len, buf_len_ - frame_len);
    buf_len_ -= frame_len;
  }

  uint32_t now = millis();
  if (now - last_query_ >= 10000) {
    last_query_ = now;
    send_command(0x5531);
  }
}

void Levoit6000S::process_frame_(const uint8_t *frame, size_t len) {
  if (len < 10)
    return;

  uint8_t msg_type = frame[1];
  uint16_t opcode = ((uint16_t) frame[8] << 8) | frame[7];

  // Log every frame with hex payload
  if (msg_type != 0x12) {
    char hex[256];
    size_t pos = 0;
    for (size_t i = 10; i < len && pos < sizeof(hex) - 4; i++)
      pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", frame[i]);
    hex[pos] = 0;
    ESP_LOGI(TAG, "FRAME type=0x%02X op=0x%04X len=%u payload=[%s]",
             msg_type, opcode, (unsigned)(len - 10), hex);
  }

  if (msg_type == 0x22 && opcode == 0x5530) {
    if (len > 10)
      parse_status_(frame + 10, len - 10);
    send_ack_(opcode);
  } else if (msg_type == 0x22 && opcode == 0x5144) {
    send_ack_(opcode);
    if (!wifi_ver_sent_)
      send_wifi_ver_();
  } else if (msg_type == 0x22 && opcode == 0x5143) {
    send_ack_(opcode);
  } else if (msg_type == 0x22 && opcode == 0x501B) {
    send_ack_(opcode);
  } else if (msg_type == 0x22) {
    send_ack_(opcode);
  } else if (msg_type == 0x12) {
    ESP_LOGV(TAG, "MCU ACK for opcode 0x%04X", opcode);
  } else if (msg_type == 0x52) {
    uint8_t err = (len > 10) ? frame[10] : 0xFF;
    ESP_LOGW(TAG, "MCU error 0x%02X for opcode 0x%04X", err, opcode);
  } else {
    ESP_LOGD(TAG, "RX: type=0x%02X opcode=0x%04X len=%u", msg_type, opcode, (unsigned) len);
  }
}

void Levoit6000S::parse_status_(const uint8_t *data, size_t len) {
  size_t i = 1;
  while (i + 2 <= len) {
    uint8_t key = data[i];
    uint8_t vlen = data[i + 1];
    if (i + 2 + vlen > len)
      break;
    if (vlen == 0) {
      i += 2;
      continue;
    }
    const uint8_t *val = data + i + 2;

    if (vlen == 1)
      ESP_LOGD(TAG, "KLV 0x%02X = %u", key, val[0]);
    else if (vlen == 2)
      ESP_LOGD(TAG, "KLV 0x%02X [%u] = %02X %02X", key, vlen, val[0], val[1]);
    else if (vlen <= 6)
      ESP_LOGD(TAG, "KLV 0x%02X [%u] = %02X %02X %02X %02X %02X %02X", key, vlen,
               val[0], vlen > 1 ? val[1] : 0, vlen > 2 ? val[2] : 0,
               vlen > 3 ? val[3] : 0, vlen > 4 ? val[4] : 0, vlen > 5 ? val[5] : 0);
    else
      ESP_LOGD(TAG, "KLV 0x%02X [%u]", key, vlen);

    switch (key) {
      case 0x01:  // Power (always 1 when plugged in — may not be controllable)
#ifdef USE_SWITCH
        if (switches_[0] && vlen >= 1 && switches_[0]->state != (val[0] != 0))
          switches_[0]->publish_state(val[0] != 0);
#endif
        break;

      case 0x05:  // Display state: byte[1] = 0=off, 1=on
#ifdef USE_SWITCH
        if (switches_[1] && vlen >= 2 && switches_[1]->state != (val[1] != 0))
          switches_[1]->publish_state(val[1] != 0);
#endif
        break;

      case 0x08:  // Target humidity %
#ifdef USE_NUMBER
        if (numbers_[0] && vlen >= 1) {
          float v = val[0];
          if (!numbers_[0]->has_state() || numbers_[0]->state != v)
            numbers_[0]->publish_state(v);
        }
#endif
        break;

      case 0x09:  // Current humidity %
        if (vlen >= 1)
          last_humidity_ = val[0];
#ifdef USE_SENSOR
        if (current_humidity_ && vlen >= 1) {
          float v = val[0];
          if (!current_humidity_->has_state() || current_humidity_->state != v)
            current_humidity_->publish_state(v);
        }
#endif
        break;

      case 0x0A:  // Current temp
        if (vlen >= 1)
          last_temp_ = val[0];
#ifdef USE_SENSOR
        if (current_temp_ && vlen >= 1) {
          float v = val[0];
          if (!current_temp_->has_state() || current_temp_->state != v)
            current_temp_->publish_state(v);
        }
#endif
        break;

      case 0x0C:  // Work mode: 1=Sleep, 5=Auto, 9=Manual
#ifdef USE_SELECT
        if (selects_[0] && vlen >= 1) {
          std::string mode;
          if (val[0] == 1) mode = "Sleep";
          else if (val[0] == 5) mode = "Auto";
          else if (val[0] == 9) mode = "Manual";
          else break;
          if (!selects_[0]->has_state() || selects_[0]->state != mode)
            selects_[0]->publish_state(mode);
        }
#endif
        break;

      case 0x11:  // Auto dry
#ifdef USE_SWITCH
        if (switches_[3] && vlen >= 1 && switches_[3]->state != (val[0] != 0))
          switches_[3]->publish_state(val[0] != 0);
#endif
        break;

      case 0x14:  // Timer (5 bytes: remaining_secs LE16, flags[3])
#ifdef USE_SENSOR
        if (timer_remaining_ && vlen >= 5) {
          uint16_t secs = (uint16_t) val[0] | ((uint16_t) val[1] << 8);
          bool active = (val[3] == 0x01);
          float hrs = active ? secs / 3600.0f : 0;
          if (!timer_remaining_->has_state() || timer_remaining_->state != hrs)
            timer_remaining_->publish_state(hrs);
        }
#endif
        break;

      case 0x15:  // Fan speed
#ifdef USE_SENSOR
        if (fan_speed_ && vlen >= 1) {
          float v = val[0];
          if (!fan_speed_->has_state() || fan_speed_->state != v)
            fan_speed_->publish_state(v);
        }
#endif
        break;

      case 0x16:  // Filter life %
#ifdef USE_SENSOR
        if (filter_life_pct_ && vlen >= 1) {
          float v = val[0];
          if (!filter_life_pct_->has_state() || filter_life_pct_->state != v)
            filter_life_pct_->publish_state(v);
        }
#endif
        break;

      case 0x17:  // Filter life days
#ifdef USE_SENSOR
        if (filter_life_days_ && vlen >= 1) {
          float v = val[0];
          if (!filter_life_days_->has_state() || filter_life_days_->state != v)
            filter_life_days_->publish_state(v);
        }
#endif
        break;

      case 0x18:  // Child lock
#ifdef USE_SWITCH
        if (switches_[2] && vlen >= 1 && switches_[2]->state != (val[0] != 0))
          switches_[2]->publish_state(val[0] != 0);
#endif
        break;

      default:
        break;
    }
    i += 2 + vlen;
  }
}

void Levoit6000S::send_command(uint16_t opcode) { send_command(opcode, nullptr, 0); }

void Levoit6000S::send_command(uint16_t opcode, const uint8_t *klv, size_t klv_len) {
  size_t frame_len = 10 + klv_len;
  uint8_t frame[64];
  if (frame_len > sizeof(frame))
    return;

  frame[0] = 0xA5;
  frame[1] = 0x22;
  frame[2] = counter_++;
  if (counter_ < 0x10)
    counter_ = 0x10;
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
    if (i != 5)
      sum += frame[i];
  }
  frame[5] = 0xFF - (sum & 0xFF);

  ESP_LOGD(TAG, "TX: opcode=0x%04X klv_len=%u", opcode, (unsigned) klv_len);
  uart_write_bytes(UART_NUM_0, frame, frame_len);
  uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));

  if (klv_len > 0)
    send_command(0x5531);
}

void Levoit6000S::send_ack_(uint16_t opcode) {
  uint8_t frame[10] = {0xA5, 0x12, 0x00, 0x04, 0x00, 0x00, 0x02,
                        (uint8_t) (opcode & 0xFF), (uint8_t) ((opcode >> 8) & 0xFF), 0x00};
  frame[2] = counter_++;
  if (counter_ < 0x10)
    counter_ = 0x10;

  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    if (i != 5)
      sum += frame[i];
  }
  frame[5] = 0xFF - (sum & 0xFF);

  uart_write_bytes(UART_NUM_0, frame, 10);
  uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
}

void Levoit6000S::send_wifi_ver_() {
  uint8_t klv[] = {0x01, 0x01, 0x00, 0x02, 0x01, 0x00};
  send_command(0xD010, klv, sizeof(klv));
  wifi_ver_sent_ = true;
  ESP_LOGI(TAG, "WiFi version sent to MCU");
}

#ifdef USE_SWITCH
void Levoit6000S::register_switch(uint8_t type, switch_::Switch *sw) {
  if (type < 4)
    switches_[type] = sw;
}
#endif

#ifdef USE_NUMBER
void Levoit6000S::register_number(uint8_t type, number::Number *num) {
  if (type < 2)
    numbers_[type] = num;
}
#endif

#ifdef USE_SELECT
void Levoit6000S::register_select(uint8_t type, select::Select *sel) {
  if (type < 1)
    selects_[type] = sel;
}
#endif

}  // namespace levoit_6000s
}  // namespace esphome
