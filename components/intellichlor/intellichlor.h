#pragma once
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include <map>
#include <queue>
#include <tuple>
#include <vector>

namespace esphome {
namespace intellichlor {

static const uint8_t CMD_FRAME_HEADER[2] = {0x10, 0x02};
static const uint8_t CMD_FRAME_FOOTER[2] = {0x10, 0x03};

#define GETBIT8(a, b) ((a) & ((uint8_t) 1 << (b)))

class INTELLICHLORComponent : public PollingComponent, public uart::UARTDevice {
  SUB_TEXT_SENSOR(version)
  SUB_TEXT_SENSOR(swg_debug)
  SUB_SWITCH(takeover_mode)
  SUB_NUMBER(swg_percent)
  SUB_SENSOR(salt_ppm)
  SUB_SENSOR(water_temp)
  SUB_SENSOR(status)
  SUB_SENSOR(error)
  SUB_SENSOR(set_percent)
  SUB_BINARY_SENSOR(no_flow)
  SUB_BINARY_SENSOR(low_salt)
  SUB_BINARY_SENSOR(very_low_salt)
  SUB_BINARY_SENSOR(clean)
  SUB_BINARY_SENSOR(high_current)
  SUB_BINARY_SENSOR(low_volts)
  SUB_BINARY_SENSOR(low_temp)
  SUB_BINARY_SENSOR(check_pcb)


 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  void read_all_info();

  void set_swg_percent();
  void set_takeover_mode(bool enable);

  void set_flow_control_pin(GPIOPin *flow_control_pin) { this->flow_control_pin_ = flow_control_pin; }

 protected:
  GPIOPin *flow_control_pin_{nullptr};
  
  void get_version_();
  void get_temp_();
  void takeover_();
  void set_percent_(uint8_t percent);

  void restart_();
  void send_command_(const uint8_t *command, int command_len, uint8_t retries);
  bool readline_(int readch, uint8_t *buffer, int len);

  void log_hex(std::string prefix, std::vector<uint8_t> bytes, size_t len, uint8_t separator);
  void log_hex(std::string prefix, uint8_t* bytes, size_t len, uint8_t separator);

  // last send operation
  uint32_t last_command_timestamp_;
  uint32_t last_recv_timestamp_;
  uint32_t last_loop_timestamp_;
  uint32_t last_debug_timestamp_;
  
  // send queue tuple<retries,current_attempt,data>
  std::queue<std::tuple<uint8_t, uint8_t, std::vector<uint8_t> >> send_queue_;

  // last set percent to publish as sensor on set response
  uint8_t last_set_percent_ = 0;

  bool run_again_;

  std::string version_;

};

}  // namespace intellichlor
}  // namespace esphome
