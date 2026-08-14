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
static constexpr uint16_t OP_STATUS_REQ   = 0x5531;  // ESP -> MCU poll; its ACK carries the status body
static constexpr uint16_t OP_MODE         = 0x5532;  // k01 = 1 manual, 2 sleep, 3 humidity, 4 auto
static constexpr uint16_t OP_FAN_SPEED    = 0x5533;  // k01 = 1..9
static constexpr uint16_t OP_FILTER_RESET = 0x5544;  // empty body
static constexpr uint16_t OP_TARGET_RH    = 0x5536;  // k01 = percent
static constexpr uint16_t OP_AUTO_SUBMODE = 0x5537;  // k01 = auto_preference, k02 = humidity_preference
// 0x5546 is the whole filter-dry family: k01 = auto-dry-on-power-off setting,
// k02 = run a dry cycle now (1 low, 2 high), k03 = auto-dry-on-water-shortage.
static constexpr uint16_t OP_FILTER_DRY   = 0x5546;

// Wire mode values, matching the name table in the stock ESP firmware at
// .rodata 0x3C0F0518 (0 auto, 1 manual, 2 sleep, 3 humidity, 4 autoPro, 5 dry).
static constexpr uint8_t MODE_MANUAL   = 1;
static constexpr uint8_t MODE_SLEEP    = 2;
static constexpr uint8_t MODE_HUMIDITY = 3;
static constexpr uint8_t MODE_AUTO     = 4;

// 0x5537 k01 in mode 4. These two are the app's auto buttons and are the only
// values it ever sends; they pin the target to 50 % and 55 %.
//
// The MCU accepts 0-9 and 0x13 echoes them, and any value outside 1-2 makes
// 0x08 report the user's stored target -- but that is a reporting artefact, not
// a working setpoint. In that state the controller hunts (fan 9<->0 every
// ~12 s at target 62 / RH 57, where Humidity mode at the same target ran fan 5
// flat). Treat 1 and 2 as the only usable preferences.
static constexpr uint8_t AUTO_PREF_LOW  = 1;
static constexpr uint8_t AUTO_PREF_HIGH = 2;

// The app emits a mode change and its follow-up parameter as two frames 14-52 ms
// apart rather than back to back. Reproduce that spacing.
static constexpr uint32_t MODE_FOLLOWUP_MS = 60;

class Levoit6000S : public Component {
 public:
  inline static Levoit6000S *instance = nullptr;

  // --- measured values -----------------------------------------------------
  float humidity = NAN;
  float temperature = NAN;
  float target_humidity = NAN;
  float filter_life_pct = NAN;
  float filter_life_days = NAN;
  // Key 0x0C is the fan that is actually turning right now and reads 0 whenever
  // the unit is idle -- which is normal in Auto once RH reaches target. Key 0x15
  // is the setpoint and survives idling. They only track each other in Manual:
  // during an Auto capture 0x15 held 9 while 0x0C went 1 -> 0.
  float fan_actual = NAN;    // 0x0C, 0-9
  float fan_setpoint = NAN;  // 0x15, 1-9
  // Only meaningful while a dry cycle runs; NAN otherwise, so the entity reads
  // "unknown" rather than showing a frozen leftover countdown.
  float dry_minutes_left = NAN;

  bool water_tank_missing = false;
  bool low_water = false;
  bool output_running = false;
  // Settings, not sensors: auto-dry on water shortage (key 0x1F) and auto-dry
  // on power off (key 0x14 byte 2).
  bool dry_on_water_shortage = false;
  bool dry_on_power_off = false;
  // A dry cycle runs *on top of* the current mode: 0x0B keeps reporting the
  // underlying mode and 0x0C stays 0 throughout. Key 0x14 byte 3 is the only
  // thing that moves (0x02 idle -> 0x01 drying -> 0x02), so it is the only
  // sound indicator. Do not infer this from fan == 0.
  bool filter_drying = false;
  bool power_on = false;
  bool display_on = false;
  bool child_lock = false;

  uint8_t mode_raw = 0;  // 0x0B as last reported; 0 = nothing seen yet
  // Status key 0x13, echoing whatever 0x5537 k01 was last sent. In mode 4 it
  // selects the target: 1 -> 50 %, 2 -> 55 %, anything else -> the user's own
  // stored target. Proven by setting the stored target to 45 in Humidity mode,
  // entering Auto (which snapped to the preset 50) and then sending k01=3,
  // which moved the reported target to 45 rather than to a third preset. This
  // also explains the stock capture that showed 0x13=9 in mode 4 with target
  // 55: 55 was that unit's stored target.
  uint8_t auto_preference = 1;
  std::string work_mode;
  std::string fan_level_name;

  // Set on every parse; the publishing interval clears it. publish_all forces
  // one unconditional pass so entities leave "unknown" on the first frame.
  bool state_dirty = false;
  bool publish_all = true;

  // The device stores 1-9 and echoes whatever it is sent, but the hardware only
  // has three fan levels: 1-3 low, 4-6 medium, 7-9 high. Metered at every step.
  static uint8_t fan_level_to_raw(uint8_t level) { return level <= 1 ? 1 : (level == 2 ? 5 : 9); }
  static uint8_t fan_raw_to_level(uint8_t raw) { return raw <= 3 ? 1 : (raw <= 6 ? 2 : 3); }

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

    // The link needs no handshake, so commands are accepted immediately. The
    // old code gated sending on a 2 s timer and silently dropped anything sent
    // before it fired.
    link_up_ = true;
    ESP_LOGI(TAG, "MCU link up on UART1 (tx=GPIO19 rx=GPIO18)");
    this->set_timeout("first_poll", 500, [this]() { request_status(); });
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

    // The MCU reports unsolicited every 11-15 s and within ~150 ms of any
    // command, so this poll is only a backstop against a missed edge.
    uint32_t now = millis();
    if (now - last_poll_ >= 30000) {
      last_poll_ = now;
      request_status();
    }
  }

  void request_status() {
    last_poll_ = millis();
    send_command(OP_STATUS_REQ);
  }

  // --- typed setters -------------------------------------------------------
  void set_power(bool on)      { send_kv_(OP_POWER, 0x01, on ? 1 : 0); }
  void set_display(bool on)    { send_kv_(OP_DISPLAY, 0x01, on ? 1 : 0); }
  void set_child_lock(bool on) { send_kv_(OP_CHILD_LOCK, 0x01, on ? 1 : 0); }
  void set_mode(uint8_t mode)  { send_kv_(OP_MODE, 0x01, mode); }
  void set_auto_preference(uint8_t p) { send_kv_(OP_AUTO_SUBMODE, 0x01, p); }
  void set_humidity_preference(uint8_t p) { send_kv_(OP_AUTO_SUBMODE, 0x02, p); }
  void reset_filter()          { send_command(OP_FILTER_RESET); }
  void run_filter_dry(uint8_t level)      { send_kv_(OP_FILTER_DRY, 0x02, level); }
  void set_dry_on_power_off(bool on)      { send_kv_(OP_FILTER_DRY, 0x01, on ? 1 : 0); }
  void set_dry_on_water_shortage(bool on) { send_kv_(OP_FILTER_DRY, 0x03, on ? 1 : 0); }

  // Auto and "Auto Pro" are one wire mode with two preferences, not two modes.
  // Measured: preference 1 settles on target 50 % / fan 1, preference 2 on
  // target 55 % / fan 5. The app never sends mode 4 without a preference.
  void set_auto(uint8_t pref) {
    pending_auto_pref_ = pref;
    set_mode(MODE_AUTO);
    this->set_timeout("auto_pref", MODE_FOLLOWUP_MS, [this, pref]() { set_auto_preference(pref); });
  }

  // The MCU ignores 0x5533 outside Manual, so reaching for the fan control has
  // to mean "put me in Manual at this speed" or it silently does nothing.
  void set_fan_level(uint8_t level) {
    uint8_t raw = fan_level_to_raw(level);
    if (mode_raw == MODE_MANUAL) {
      send_kv_(OP_FAN_SPEED, 0x01, raw);
      return;
    }
    ESP_LOGI(TAG, "fan level %u requested in mode %u; switching to Manual first", level, mode_raw);
    set_mode(MODE_MANUAL);
    this->set_timeout("fan_after_mode", MODE_FOLLOWUP_MS,
                      [this, raw]() { send_kv_(OP_FAN_SPEED, 0x01, raw); });
  }

  // Auto pins the target to its preference's own value (50 % or 55 %) and
  // overrides anything written while mode 4 is active, so a target set from
  // Auto has to leave Auto. Humidity mode is where a user-chosen target is
  // actually honoured.
  //
  // Do NOT be tempted by auto_preference 3 here. Preferences other than 1 and 2
  // do make 0x08 report the user's stored target, which looks like "Auto with a
  // custom setpoint" -- but the controller behind it does not regulate to that
  // value. Measured back to back at target 62 with RH 56-59: mode 4 preference
  // 3 cycled the fan 9<->0 every ~12 s indefinitely, while Humidity mode at the
  // same target held fan 5 steady with zero transitions across RH 57->58->59.
  // The reported target follows; the control loop does not.
  void set_target_humidity(uint8_t pct) {
    if (mode_raw != MODE_AUTO) {
      send_kv_(OP_TARGET_RH, 0x01, pct);
      return;
    }
    ESP_LOGI(TAG, "target %u%% requested in Auto; switching to Humidity mode first", pct);
    set_mode(MODE_HUMIDITY);
    this->set_timeout("target_after_mode", MODE_FOLLOWUP_MS,
                      [this, pct]() { send_kv_(OP_TARGET_RH, 0x01, pct); });
  }

  // Write-only: no status key tracks the user timer. 0x14 is the filter-dry
  // countdown and does not react to 0x5019 at all, so there is nothing to read
  // back and no way to confirm the timer took.
  void set_timer(uint32_t seconds) {
    uint8_t klv[6] = {0x01, 0x04, (uint8_t)(seconds & 0xFF), (uint8_t)((seconds >> 8) & 0xFF),
                      (uint8_t)((seconds >> 16) & 0xFF), (uint8_t)((seconds >> 24) & 0xFF)};
    send_command(OP_TIMER, klv, sizeof(klv));
  }

  void send_command(uint16_t opcode, const uint8_t *klv = nullptr, size_t klv_len = 0) {
    if (!link_up_) return;
    send_frame_(0x22, opcode, counter_next_(), klv, klv_len);
  }

  // Debug/probe path: inject an arbitrary frame regardless of link state.
  void send_raw(uint16_t opcode, const uint8_t *klv = nullptr, size_t klv_len = 0) {
    ESP_LOGI(TAG, "PROBE inject opcode=0x%04X klv_len=%u", opcode, (unsigned) klv_len);
    send_frame_(0x22, opcode, counter_next_(), klv, klv_len);
  }

 private:
  static constexpr const char *TAG = "levoit_6000s";
  uint8_t buf_[512];
  size_t buf_len_ = 0;
  uint8_t counter_ = 0x10;
  bool link_up_ = false;
  uint32_t last_poll_ = 0;
  uint16_t dry_seconds_ = 0;
  uint8_t pending_auto_pref_ = 1;

  uint8_t counter_next_() {
    uint8_t c = counter_++;
    if (counter_ < 0x10) counter_ = 0x10;
    return c;
  }

  void send_kv_(uint16_t opcode, uint8_t key, uint8_t value) {
    uint8_t klv[3] = {key, 0x01, value};
    send_command(opcode, klv, sizeof(klv));
  }

  void send_frame_(uint8_t type, uint16_t opcode, uint8_t seq, const uint8_t *klv, size_t klv_len) {
    size_t frame_len = 10 + klv_len;
    uint8_t frame[64];
    if (frame_len > sizeof(frame)) return;

    frame[0] = 0xA5;
    frame[1] = type;
    frame[2] = seq;
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

    if (type == 0x22)
      ESP_LOGD(TAG, "TX op=0x%04X: %s", opcode, format_hex_pretty(frame, frame_len).c_str());
    uart_write_bytes(UART_NUM_1, frame, frame_len);
    // A 13-byte frame takes ~1.1 ms on the wire; this only exists so the buffer
    // is flushed before the next call, and must not stall the main loop.
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(20));
  }

  void process_frame_(const uint8_t *frame, size_t len) {
    if (len < 10) return;
    uint8_t msg_type = frame[1];
    uint8_t seq = frame[2];
    uint16_t opcode = ((uint16_t) frame[8] << 8) | frame[7];

    if (msg_type == 0x22) {
      // Body starts at byte 9: the result byte, then the TLV records at byte 10.
      if (opcode == OP_STATUS_REPORT) {
        ESP_LOGD(TAG, "RX status: %s", format_hex_pretty(frame, len).c_str());
        parse_status_(frame + 9, len - 9);
      }
      // The stock ESP echoes the sequence number of the frame it is acking; it
      // does not use its own counter.
      send_ack_(opcode, seq);
    } else if (msg_type == 0x12) {
      // The ACK to a status request carries the full status body, laid out
      // exactly like an unsolicited 0x5530 report (result byte at 9, TLVs at
      // 10). Verified against the stock capture: MCU->ESP ACK payloadType=
      // 0x023155 len=96. Every other command ACK is a bare len=10. Dropping
      // these made the 30 s poll a no-op.
      if (opcode == OP_STATUS_REQ && len > 10) {
        ESP_LOGD(TAG, "RX poll reply: %s", format_hex_pretty(frame, len).c_str());
        parse_status_(frame + 9, len - 9);
      }
    } else if (msg_type == 0x52) {
      ESP_LOGW(TAG, "MCU rejected opcode 0x%04X", opcode);
    }
  }

  // Status body is a leading result byte followed by key/len/value records, so
  // the walk starts at index 1. Getting this offset wrong does not desync the
  // whole frame -- the walk re-aligns a few records in -- it just silently
  // corrupts the first handful of keys.
  void parse_status_(const uint8_t *data, size_t len) {
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
        case 0x0C: fan_actual = val[0]; break;
        // Taken verbatim: 0x13 faithfully echoes the last 0x5537 k01, including
        // values the app never sends. An earlier guard here accepted only 1 and
        // 2, which would silently discard the custom-target preference.
        case 0x13: auto_preference = val[0]; break;
        // 0x14 is not the user timer. bytes[0:2] is a uint16 LE countdown that
        // starts at 7200 (2 h) when a dry cycle begins, ticks down while it
        // runs and freezes when it ends -- it does not react to 0x5019 at all.
        // byte[2] is the dry-on-power-off setting (proven in both directions by
        // 0x5546 k01). byte[3] is 1 while a dry cycle runs and 2 otherwise,
        // captured across a full 02 -> 01 -> 02 transition.
        case 0x14:
          if (vlen >= 2) dry_seconds_ = (uint16_t) val[0] | ((uint16_t) val[1] << 8);
          if (vlen >= 3) dry_on_power_off = (val[2] != 0);
          if (vlen >= 4) filter_drying = (val[3] == 1);
          break;
        case 0x15: fan_setpoint = val[0]; break;
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

    dry_minutes_left = filter_drying ? (dry_seconds_ / 60.0f) : NAN;

    // The select is a Manual control, and 0x15 only means anything in Manual --
    // elsewhere it holds a stale setpoint the device is ignoring while it picks
    // its own speed, which showed "High" next to an actual output of 5. Prefer
    // what the fan is really doing whenever the device is choosing, and fall
    // back to the setpoint only when there is nothing running to report.
    uint8_t level_src = 0;
    if (mode_raw == MODE_MANUAL && !std::isnan(fan_setpoint))
      level_src = (uint8_t) fan_setpoint;
    else if (!std::isnan(fan_actual) && fan_actual > 0)
      level_src = (uint8_t) fan_actual;
    else if (!std::isnan(fan_setpoint))
      level_src = (uint8_t) fan_setpoint;
    if (level_src > 0)
      fan_level_name = level_name_(fan_raw_to_level(level_src));

    // Mode is whatever key 0x0B says, and nothing else. A dry cycle is not a
    // mode -- it runs on top of one -- and fan 0 only means the unit is idle,
    // which is the normal resting state of Auto once RH reaches target.
    // Inferring "Filter Dry" from fan == 0 made every satisfied Auto cycle
    // report as a dry cycle.
    //
    // Every string produced here must be an option on the Mode select, or
    // Select::publish_state rejects it. An unrecognised mode keeps the previous
    // value rather than inventing an "Unknown" option.
    switch (mode_raw) {
      case MODE_MANUAL:   work_mode = "Manual"; break;
      case MODE_SLEEP:    work_mode = "Sleep"; break;
      case MODE_HUMIDITY: work_mode = "Humidity"; break;
      // One Auto, matching the single Auto button on the unit. The preference
      // behind it is a target selector, not a mode: 1 and 2 are the app's two
      // canned targets and 3 hands the target to the user. Exposing those as
      // separate modes put three Auto entries in one menu for a device with one
      // button, so the preference is now driven by the target control instead.
      case MODE_AUTO: work_mode = "Auto"; break;
      case 0: break;  // key 0x0B not seen yet
      default:
        ESP_LOGW(TAG, "unmapped mode 0x%02X, keeping '%s'", mode_raw, work_mode.c_str());
        break;
    }

    ESP_LOGI(TAG, "power=%d mode=%s (raw=0x%02X pref=%u) fan=%.0f/%.0f rh=%.0f%% target=%.0f%% "
                  "temp=%.1fC tank_missing=%d low_water=%d running=%d drying=%d lock=%d display=%d",
             power_on, work_mode.c_str(), mode_raw, auto_preference, fan_actual, fan_setpoint,
             humidity, target_humidity, temperature, water_tank_missing, low_water, output_running,
             filter_drying, child_lock, display_on);
    state_dirty = true;
  }

  static std::string level_name_(uint8_t level) {
    return level == 1 ? "Low" : (level == 2 ? "Medium" : "High");
  }

  void send_ack_(uint16_t opcode, uint8_t seq) { send_frame_(0x12, opcode, seq, nullptr, 0); }
};

}  // namespace levoit_6000s
}  // namespace esphome
