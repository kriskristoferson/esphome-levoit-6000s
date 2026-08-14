#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "soc/usb_serial_jtag_reg.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <cstring>
#include <string>

namespace esphome {
namespace levoit_6000s {

// Wire protocol, confirmed against logic-analyzer captures of the stock firmware:
//   A5 | type | seq | len16_LE | cksum | payloadType[3] | result | TLV...
//   cksum = (255 - sum(all frame bytes except byte[5])) & 0xFF
// type 0x22 = SEND, 0x12 = ACK, 0x52 = ERROR. Payload begins at offset 6.
// Opcodes below are the low two bytes of payloadType; byte[6] is always 0x02
// (device family = humidifier).
static constexpr uint16_t OP_POWER        = 0x5000;  // k01 = 0/1
static constexpr uint16_t OP_DISPLAY      = 0x500F;  // k01 = 0/1
static constexpr uint16_t OP_DISPLAY_CFG  = 0x5018;  // k01..k04, brightness config
static constexpr uint16_t OP_TIMER        = 0x5019;  // k01 = uint32 LE seconds, 0 = cancel
static constexpr uint16_t OP_CHILD_LOCK   = 0x5140;  // k01 = 0/1
static constexpr uint16_t OP_STATUS_REPORT= 0x5530;  // MCU -> ESP, unsolicited
static constexpr uint16_t OP_STATUS_REQ   = 0x5531;  // ESP -> MCU poll
static constexpr uint16_t OP_MODE         = 0x5532;  // k01 = 1 manual, 2 sleep, 3 humidity, 4 away
static constexpr uint16_t OP_FAN_SPEED    = 0x5533;  // k01 = 1..9
static constexpr uint16_t OP_TARGET_RH    = 0x5536;  // k01 = percent
static constexpr uint16_t OP_AUTO_SUBMODE = 0x5537;  // k01 = 1/2
// 0x5546 is the whole filter-dry family: k01 = auto-dry-on-power-off setting,
// k02 = run a dry cycle now (1 low, 2 high), k03 = auto-dry-on-water-shortage.
static constexpr uint16_t OP_FILTER_DRY   = 0x5546;

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
  bool low_water = false;
  bool output_running = false;
  // Setting, not a sensor: auto-dry when water runs short (status key 0x1F).
  bool dry_on_water_shortage = false;
  bool power_on = false;
  bool display_on = false;
  bool child_lock = false;
  std::string work_mode;
  bool state_dirty = false;

  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override {
    instance = this;

    // GPIO18/19 are the USB Serial/JTAG D-/D+ pads; the pad must be released
    // before they can be used as a UART.
    volatile uint32_t *conf0 = (volatile uint32_t *) USB_SERIAL_JTAG_CONF0_REG;
    *conf0 &= ~USB_SERIAL_JTAG_USB_PAD_ENABLE;

    uart_config_t cfg = {};
    cfg.baud_rate = 115200;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(UART_NUM_1, &cfg);
    uart_set_pin(UART_NUM_1, 19, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 512, 512, 0, nullptr, 0);

    // The MCU reports state unsolicited, so no handshake is needed to start
    // receiving. One poll is issued to populate state before the first change.
    this->set_timeout("init", 2000, [this]() {
      init_done_ = true;
      send_command(OP_STATUS_REQ);
      last_poll_ = millis();
      ESP_LOGI(TAG, "MCU link up on UART1 (tx=GPIO19 rx=GPIO18)");
    });
  }

  void loop() override {
    uint8_t tmp[128];
    int got;
    while ((got = uart_read_bytes(UART_NUM_1, tmp, sizeof(tmp), 0)) > 0) {
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

      size_t frame_len = 6 + ((size_t) buf_[3] | ((size_t) buf_[4] << 8));
      if (frame_len > sizeof(buf_)) {
        memmove(buf_, buf_ + 1, buf_len_ - 1);
        buf_len_--;
        continue;
      }
      if (buf_len_ < frame_len) break;

      uint32_t sum = 0;
      for (size_t i = 0; i < frame_len; i++)
        if (i != 5) sum += buf_[i];
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
      if (now - last_poll_ >= 30000) {
        last_poll_ = now;
        send_command(OP_STATUS_REQ);
      }
    }
  }

  // --- typed setters -------------------------------------------------------
  void set_power(bool on)          { send_kv_(OP_POWER, 0x01, on ? 1 : 0); }
  void set_display(bool on)        { send_kv_(OP_DISPLAY, 0x01, on ? 1 : 0); }
  void set_child_lock(bool on)     { send_kv_(OP_CHILD_LOCK, 0x01, on ? 1 : 0); }
  void set_mode(uint8_t mode)      { send_kv_(OP_MODE, 0x01, mode); }
  void set_fan_speed(uint8_t s)    { send_kv_(OP_FAN_SPEED, 0x01, s); }
  void set_target_humidity(uint8_t pct) { send_kv_(OP_TARGET_RH, 0x01, pct); }
  void set_auto_submode(uint8_t s) { send_kv_(OP_AUTO_SUBMODE, 0x01, s); }
  void set_fan_mode(uint8_t v)     { send_kv_(OP_AUTO_SUBMODE, 0x02, v); }
  void run_filter_dry(uint8_t lvl) { send_kv_(OP_FILTER_DRY, 0x02, lvl); }
  void set_dry_on_power_off(bool on)      { send_kv_(OP_FILTER_DRY, 0x01, on ? 1 : 0); }
  void set_dry_on_water_shortage(bool on) { send_kv_(OP_FILTER_DRY, 0x03, on ? 1 : 0); }

  void set_timer(uint32_t seconds) {
    uint8_t klv[6] = {0x01, 0x04, (uint8_t)(seconds & 0xFF), (uint8_t)((seconds >> 8) & 0xFF),
                      (uint8_t)((seconds >> 16) & 0xFF), (uint8_t)((seconds >> 24) & 0xFF)};
    send_command(OP_TIMER, klv, sizeof(klv));
  }

  void send_command(uint16_t opcode, const uint8_t *klv = nullptr, size_t klv_len = 0) {
    if (!init_done_) return;
    send_frame_(opcode, klv, klv_len);
  }

  // Debug/probe path: inject an arbitrary frame regardless of init state.
  void send_raw(uint16_t opcode, const uint8_t *klv = nullptr, size_t klv_len = 0) {
    ESP_LOGI(TAG, "PROBE inject opcode=0x%04X klv_len=%u", opcode, (unsigned) klv_len);
    send_frame_(opcode, klv, klv_len);
  }

  bool is_init_done() const { return init_done_; }

 private:
  static constexpr const char *TAG = "levoit_6000s";
  uint8_t buf_[512];
  size_t buf_len_ = 0;
  uint8_t counter_ = 0x10;
  bool init_done_ = false;
  uint32_t last_poll_ = 0;

  void send_kv_(uint16_t opcode, uint8_t key, uint8_t value) {
    uint8_t klv[3] = {key, 0x01, value};
    send_command(opcode, klv, sizeof(klv));
  }

  void send_frame_(uint16_t opcode, const uint8_t *klv, size_t klv_len) {
    size_t frame_len = 10 + klv_len;
    uint8_t frame[64];
    if (frame_len > sizeof(frame)) return;

    frame[0] = 0xA5;
    frame[1] = 0x22;
    frame[2] = counter_++;
    if (counter_ < 0x10) counter_ = 0x10;
    frame[3] = (uint8_t)((frame_len - 6) & 0xFF);
    frame[4] = (uint8_t)((frame_len - 6) >> 8);
    frame[5] = 0x00;
    frame[6] = 0x02;
    frame[7] = opcode & 0xFF;
    frame[8] = (opcode >> 8) & 0xFF;
    frame[9] = 0x00;

    if (klv_len > 0 && klv != nullptr)
      memcpy(frame + 10, klv, klv_len);

    uint32_t sum = 0;
    for (size_t i = 0; i < frame_len; i++)
      if (i != 5) sum += frame[i];
    frame[5] = 0xFF - (sum & 0xFF);

    ESP_LOGD(TAG, "TX op=0x%04X: %s", opcode, format_hex_pretty(frame, frame_len).c_str());
    uart_write_bytes(UART_NUM_1, frame, frame_len);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));
  }

  void process_frame_(const uint8_t *frame, size_t len) {
    if (len < 10) return;
    uint8_t msg_type = frame[1];
    uint16_t opcode = ((uint16_t) frame[8] << 8) | frame[7];

    if (msg_type == 0x22) {
      if (opcode == OP_STATUS_REPORT && len > 10)
        parse_status_(frame + 10, len - 10);
      send_ack_(opcode);
    } else if (msg_type == 0x52) {
      ESP_LOGW(TAG, "MCU rejected opcode 0x%04X", opcode);
    }
  }

  // Status body is a leading result byte followed by key/len/value records.
  void parse_status_(const uint8_t *data, size_t len) {
    uint8_t mode_raw = 0xFF;
    bool have_fan = false;
    uint8_t fan_raw = 0;

    size_t i = 1;
    while (i + 2 <= len) {
      uint8_t key = data[i];
      uint8_t vlen = data[i + 1];
      if (i + 2 + vlen > len) break;
      if (vlen == 0) { i += 2; continue; }
      const uint8_t *val = data + i + 2;

      switch (key) {
        case 0x02: power_on = (val[0] != 0); break;
        case 0x03: water_tank_missing = (val[0] != 0); break;
        // Float switch on the pump harness. With the tank present this is what
        // makes the unit start, run ~1s, then stop and refuse further modes.
        case 0x04: low_water = (val[0] != 0); break;
        case 0x05: if (vlen >= 2) display_on = (val[1] != 0); break;
        case 0x07: output_running = (val[0] != 0); break;
        case 0x08: target_humidity = val[0]; break;
        case 0x09: humidity = val[0]; break;
        case 0x0B: mode_raw = val[0]; break;
        case 0x0C: fan_raw = val[0]; have_fan = true; break;
        case 0x14:
          if (vlen >= 2) {
            uint16_t secs = (uint16_t) val[0] | ((uint16_t) val[1] << 8);
            timer_remaining = secs / 3600.0f;
          }
          break;
        // 0x17 is the wear counter: observed at 65, and a filter reset
        // (0x5544) put it back to 100. 0x16 is a constant 120, the rated life.
        case 0x16: filter_life_days = val[0]; break;
        case 0x17: filter_life_pct = val[0]; break;
        case 0x18: child_lock = (val[0] != 0); break;
        case 0x1E:
          // Temperature in 0.1 C; preferred over key 0x0A, which is the same
          // value rounded to whole degrees and lags behind it.
          if (vlen >= 2) temperature = ((uint16_t) val[0] | ((uint16_t) val[1] << 8)) / 10.0f;
          break;
        case 0x1F: dry_on_water_shortage = (val[0] != 0); break;
        default: break;
      }
      i += 2 + vlen;
    }

    if (have_fan) fan_speed = fan_raw;

    // Fan speed 0 while powered means the filter-dry cycle, not "off".
    if (!power_on)
      work_mode = "Off";
    else if (have_fan && fan_raw == 0)
      work_mode = "Filter Dry";
    else
      switch (mode_raw) {
        case 1: work_mode = "Manual"; break;
        case 2: work_mode = "Sleep"; break;
        case 3: work_mode = "Humidity"; break;
        case 4: work_mode = "Away"; break;
        default: work_mode = "Unknown"; break;
      }

    ESP_LOGI(TAG, "power=%d mode=%s fan=%.0f rh=%.0f%% target=%.0f%% temp=%.1fC "
                  "tank_missing=%d low_water=%d running=%d lock=%d",
             power_on, work_mode.c_str(), fan_speed, humidity, target_humidity,
             temperature, water_tank_missing, low_water, output_running, child_lock);
    state_dirty = true;
  }

  void send_ack_(uint16_t opcode) {
    uint8_t frame[10] = {0xA5, 0x12, 0x00, 0x04, 0x00, 0x00, 0x02,
                         (uint8_t)(opcode & 0xFF), (uint8_t)((opcode >> 8) & 0xFF), 0x00};
    frame[2] = counter_++;
    if (counter_ < 0x10) counter_ = 0x10;

    uint32_t sum = 0;
    for (int i = 0; i < 10; i++)
      if (i != 5) sum += frame[i];
    frame[5] = 0xFF - (sum & 0xFF);

    uart_write_bytes(UART_NUM_1, frame, 10);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));
  }
};

}  // namespace levoit_6000s
}  // namespace esphome
