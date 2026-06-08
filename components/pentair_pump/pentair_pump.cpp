#include "pentair_pump.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace pentair_pump {

static const char *const TAG = "pentair_pump";

// Minimum spacing between queued transmits — mirrors the IntelliChlor's polite
// ">100 ms since last send" gate so the two masters don't talk over each other.
static const uint32_t SEND_GAP_MS = 100;
static const size_t MAX_BUF = 256;
static const size_t MAX_QUEUE = 64;

void PentairPump::setup() {
  this->buf_.reserve(MAX_BUF);
  ESP_LOGCONFIG(TAG, "Pentair pump master started (pump 0x%02X, controller 0x%02X)", this->address_,
                this->source_);
  // Reflect initial UI states.
  if (this->run_switch_ != nullptr)
    this->run_switch_->publish_state(this->run_);
  if (this->target_rpm_number_ != nullptr)
    this->target_rpm_number_->publish_state(this->target_rpm_);
  // Default the mode select to Speed (RPM) if nothing has set it yet.
  if (this->mode_select_ != nullptr && !this->mode_select_->has_state() &&
      !this->mode_select_->traits.get_options().empty())
    this->mode_select_->publish_state(this->mode_select_->traits.get_options().front());
}

void PentairPump::loop() {
  // RX: accumulate bytes and parse complete A5 frames.
  uint8_t b;
  while (this->available()) {
    if (!this->read_byte(&b))
      break;
    this->buf_.push_back(b);
    if (this->buf_.size() > MAX_BUF)
      this->buf_.erase(this->buf_.begin());
  }
  this->parse_buffer_();

  // TX: one queued frame per gap (each write+flush is short, well under the
  // 30 ms loop budget; never blast a burst).
  if (!this->send_queue_.empty() && (millis() - this->last_send_) > SEND_GAP_MS) {
    auto &f = this->send_queue_.front();
    this->write_array(f);
    this->flush();
    this->send_queue_.pop();
    this->last_send_ = millis();
  }
}

void PentairPump::update() {
  // Only assert control when takeover is enabled; otherwise we are a passive
  // monitor (and let the pump's own panel / another controller run it).
  if (this->takeover_) {
    this->enqueue_frame_(0x04, {0xFF});  // remote control ON (keep-alive)
    if (this->run_) {
      if (this->flow_mode_) {
        this->enqueue_frame_(0x01, {0x02, 0xE4, 0x00, this->target_gpm_});  // set GPM (flow)
      } else {
        uint8_t hi = (this->target_rpm_ >> 8) & 0xFF;
        uint8_t lo = this->target_rpm_ & 0xFF;
        this->enqueue_frame_(0x01, {0x02, 0xC4, hi, lo});  // set RPM (register 0x02C4)
      }
      this->enqueue_frame_(0x06, {0x0A});  // run
    } else {
      this->enqueue_frame_(0x06, {0x04});  // stop
    }
  }
  // Always poll status so the RPM/watts/etc. sensors update whether or not we
  // are the one driving the pump.
  this->enqueue_frame_(0x07, {});  // status request
}

void PentairPump::enqueue_frame_(uint8_t cmd, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> f;
  f.reserve(11 + data.size());
  f.push_back(0xFF);
  f.push_back(0x00);
  f.push_back(0xFF);  // preamble (not part of checksum)
  size_t start = f.size();
  f.push_back(0xA5);
  f.push_back(0x00);
  f.push_back(this->address_);
  f.push_back(this->source_);
  f.push_back(cmd);
  f.push_back((uint8_t) data.size());
  for (uint8_t d : data)
    f.push_back(d);
  uint16_t sum = 0;
  for (size_t i = start; i < f.size(); i++)
    sum += f[i];
  f.push_back((sum >> 8) & 0xFF);
  f.push_back(sum & 0xFF);

  if (this->send_queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "send queue overflow, purging");
    std::queue<std::vector<uint8_t>> empty;
    std::swap(this->send_queue_, empty);
  }
  this->send_queue_.push(std::move(f));
}

void PentairPump::parse_buffer_() {
  while (!this->buf_.empty()) {
    if (this->buf_[0] != 0xA5) {
      this->buf_.erase(this->buf_.begin());  // skip preamble / noise
      continue;
    }
    if (this->buf_.size() < 6)
      return;  // need more
    uint8_t len = this->buf_[5];
    if (len > 64) {
      this->buf_.erase(this->buf_.begin());
      continue;
    }
    size_t total = 6 + (size_t) len + 2;
    if (this->buf_.size() < total)
      return;  // need more
    uint16_t sum = 0;
    for (size_t i = 0; i < total - 2; i++)
      sum += this->buf_[i];
    uint16_t chk = (uint16_t(this->buf_[total - 2]) << 8) | this->buf_[total - 1];
    if (sum == chk) {
      this->handle_frame_(total);
      this->buf_.erase(this->buf_.begin(), this->buf_.begin() + total);
    } else {
      this->buf_.erase(this->buf_.begin());  // resync
    }
  }
}

void PentairPump::handle_frame_(size_t total) {
  uint8_t src = this->buf_[3];
  uint8_t cmd = this->buf_[4];
  uint8_t len = this->buf_[5];
  if (src != this->address_)
    return;  // only the pump's own replies
  if (cmd != 0x07 || len < 7)
    return;  // we only decode status replies here

  // Status reply payload (data starts at buf_[6]):
  //   [0] run state (0x0A run / 0x04 stop)   [1] mode        [2] drive state
  //   [3..4] watts (BE)   [5..6] rpm (BE)     [7] gpm         [10] status/error
  const uint8_t *d = &this->buf_[6];
  uint8_t run_state = d[0];
  uint8_t mode = d[1];
  uint8_t drive_state = d[2];
  uint16_t watts = (uint16_t(d[3]) << 8) | d[4];
  uint16_t rpm = (uint16_t(d[5]) << 8) | d[6];

  ESP_LOGD(TAG, "pump status rpm=%u watts=%u mode=%u drive=0x%02X run=0x%02X", rpm, watts, mode,
           drive_state, run_state);

  if (this->rpm_sensor_ != nullptr)
    this->rpm_sensor_->publish_state(rpm);
  if (this->watts_sensor_ != nullptr)
    this->watts_sensor_->publish_state(watts);
  if (this->mode_sensor_ != nullptr)
    this->mode_sensor_->publish_state(mode);
  if (this->drive_state_sensor_ != nullptr)
    this->drive_state_sensor_->publish_state(drive_state);
  if (this->run_state_sensor_ != nullptr)
    this->run_state_sensor_->publish_state(run_state);
  if (this->running_binary_sensor_ != nullptr)
    this->running_binary_sensor_->publish_state(rpm > 0);
  if (len >= 8 && this->gpm_sensor_ != nullptr)
    this->gpm_sensor_->publish_state(d[7]);
  if (len >= 11 && this->status_code_sensor_ != nullptr)
    this->status_code_sensor_->publish_state(d[10]);

  // Full reply as hex — captures every byte, incl. fields not yet decoded.
  if (this->last_status_text_sensor_ != nullptr) {
    std::vector<uint8_t> frame(this->buf_.begin(), this->buf_.begin() + total);
    this->last_status_text_sensor_->publish_state(format_hex_pretty(frame));
  }
}

void PentairPump::set_target_rpm(float rpm) {
  if (rpm < 0)
    rpm = 0;
  this->target_rpm_ = (uint16_t) rpm;
  if (this->target_rpm_number_ != nullptr)
    this->target_rpm_number_->publish_state(rpm);
}

void PentairPump::set_target_gpm(float gpm) {
  if (gpm < 0)
    gpm = 0;
  this->target_gpm_ = (uint8_t) gpm;
  if (this->target_gpm_number_ != nullptr)
    this->target_gpm_number_->publish_state(gpm);
}

void PentairPump::set_control_mode(const std::string &mode) {
  // Anything mentioning flow/gpm selects flow mode; otherwise speed (rpm).
  this->flow_mode_ = mode.find("GPM") != std::string::npos || mode.find("Flow") != std::string::npos ||
                     mode.find("gpm") != std::string::npos || mode.find("flow") != std::string::npos;
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(mode);
}

void PentairPump::set_run_state(bool run) {
  this->run_ = run;
  if (this->run_switch_ != nullptr)
    this->run_switch_->publish_state(run);
}

void PentairPump::set_takeover(bool enable) {
  this->takeover_ = enable;
  if (this->takeover_switch_ != nullptr)
    this->takeover_switch_->publish_state(enable);
  if (!enable) {
    // Hand control back to the pump's local panel / external controller.
    this->enqueue_frame_(0x04, {0x00});  // remote control OFF
  }
}

void PentairPump::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair Pump:");
  ESP_LOGCONFIG(TAG, "  Pump address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  Controller address: 0x%02X", this->source_);
  this->check_uart_settings(9600);
}

void PentairPumpNumber::control(float value) {
  if (this->parent_ == nullptr) {
    this->publish_state(value);
    return;
  }
  // The hub publishes the new value back to this number.
  if (this->kind_ == 1)
    this->parent_->set_target_gpm(value);
  else
    this->parent_->set_target_rpm(value);
}

void PentairPumpRunSwitch::write_state(bool state) {
  if (this->parent_ != nullptr)
    this->parent_->set_run_state(state);  // hub publishes back to this switch
  else
    this->publish_state(state);
}

void PentairPumpTakeoverSwitch::write_state(bool state) {
  if (this->parent_ != nullptr)
    this->parent_->set_takeover(state);  // hub publishes back to this switch
  else
    this->publish_state(state);
}

void PentairPumpModeSelect::control(const std::string &value) {
  if (this->parent_ != nullptr)
    this->parent_->set_control_mode(value);  // hub publishes back to this select
  else
    this->publish_state(value);
}

}  // namespace pentair_pump
}  // namespace esphome
