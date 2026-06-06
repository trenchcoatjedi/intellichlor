#include "intellichlor.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace intellichlor {

static const char *TAG = "intellichlor.component";

void INTELLICHLORComponent::setup() {
    ESP_LOGCONFIG(TAG, "Setting up Intellichlor...");
    this->read_all_info();
    ESP_LOGCONFIG(TAG, "Version : %s", const_cast<char *>(this->version_.c_str()));
    if (this->flow_control_pin_ != nullptr) {
        ESP_LOGCONFIG(TAG, "Using Flow Control");
        this->flow_control_pin_->setup();
    }
    this->last_command_timestamp_ = millis();
    this->last_recv_timestamp_ = millis();
    this->last_loop_timestamp_ = millis();
    this->last_debug_timestamp_ = millis();
}

void INTELLICHLORComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "Intellichlor RS485 Component");
    LOG_TEXT_SENSOR("  ", "VersionTextSensor", this->version_text_sensor_);
    LOG_SWITCH("  ", "TakeoverModeSwitch", this->takeover_mode_switch_);
    LOG_NUMBER("  ", "SWGPercentNumber", this->swg_percent_number_);
    LOG_SENSOR("  ", "WaterTempSensor", this->water_temp_sensor_);
    LOG_SENSOR("  ", "SaltPPMSensor", this->salt_ppm_sensor_);
    LOG_SENSOR("  ", "ErrorSensor", this->error_sensor_);
    LOG_SENSOR("  ", "StatusSensor", this->status_sensor_);
    LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
}

void INTELLICHLORComponent::loop() {
    const int max_line_length = 64;
    static uint8_t buffer[max_line_length];

    // read bytes off the wire first
    while (available()) {
        this->readline_(read(), buffer, max_line_length);
    }

    if(this->run_again_)
    {
        this->read_all_info();
    }

    // hopefully the remote is not sending at this time
    // if we haven't sent a command in 50ms send another in queue
    auto since_last = millis() - this->last_command_timestamp_;
    auto since_last_recv = millis() - this->last_recv_timestamp_;
    auto since_last_debug = millis() - this->last_debug_timestamp_;

    auto size = this->send_queue_.size();
    if (size >= 64)
    {
        ESP_LOGE(TAG, "Send Queue Overflow, purging");
        std::queue<std::tuple<uint8_t, uint8_t, std::vector<uint8_t> >> empty;
        std::swap( this->send_queue_, empty );
    }
    
    

    if(since_last > 100)
    {
        if (!this->send_queue_.empty()) {
            auto packet = this->send_queue_.front();
            auto retries = std::get<0>(packet);
            auto attempts = std::get<1>(packet);
            auto data = std::get<2>(packet);

            attempts++;
            
            ESP_LOGD(TAG, "Process Queue Retries:%i Attempt:%i", retries, attempts);

            if(attempts > retries)
            {
                ESP_LOGE(TAG, "No response %i > %i removing from send queue", retries, attempts);
                this->send_queue_.pop();
            }

            if (this->flow_control_pin_ != nullptr)
            {
                ESP_LOGV(TAG, "Enable Send");
                this->flow_control_pin_->digital_write(true);
            }
            
            ESP_LOGV(TAG, "Sending %i bytes", data->size());
            this->write_array(data);
            this->flush();

            if (this->flow_control_pin_ != nullptr)
            {
                ESP_LOGV(TAG, "Disable Send");
                this->flow_control_pin_->digital_write(false);
            }
            
            this->last_command_timestamp_ = millis();
        }
    }


}

void INTELLICHLORComponent::update() {
    this->read_all_info();
}

void INTELLICHLORComponent::set_swg_percent() {
    if(this->takeover_mode_switch_ != nullptr && this->takeover_mode_switch_->state)
    {
        this->read_all_info();
    }
}

void INTELLICHLORComponent::set_takeover_mode(bool enable) {
    this->read_all_info();
}

void INTELLICHLORComponent::get_version_() {
    uint8_t cmd[3] = {0x50, 0x14, 0x00};
    ESP_LOGD(TAG, "send GetVersion");
    this->send_command_(cmd, 3, 1);
}

void INTELLICHLORComponent::get_temp_() {
    uint8_t cmd[3] = {0x50, 0x15, 0x00};
    ESP_LOGD(TAG, "send GetTemp");
    this->send_command_(cmd, 3, 3);
}

void INTELLICHLORComponent::takeover_() {
    uint8_t cmd[3] = {0x50, 0x00, 0x00};
    ESP_LOGD(TAG, "send Takeover");
    this->send_command_(cmd, 3, 3);
}

void INTELLICHLORComponent::set_percent_(uint8_t percent) {
    ESP_LOGD(TAG, "send SetPercent");
    // 16% requires an extra zero byte of padding, because ...
    this->last_set_percent_ = percent;
    if(percent == 16) {
        uint8_t cmd[4] = {0x50, 0x11, percent, 0x00};
        this->send_command_(cmd, 4, 3);
    } else {
        uint8_t cmd[3] = {0x50, 0x11, percent};
        this->send_command_(cmd, 3, 3);
    }
    
}

void INTELLICHLORComponent::restart_() {

}

void INTELLICHLORComponent::read_all_info() {
    
    // make sure we don't run this more than ~ once per second
    if(millis() - this->last_loop_timestamp_ > 900)
    {
        if(this->run_again_)
        {
            ESP_LOGD(TAG, "Run again after successful iteration");
        }
        this->run_again_ = false;
        this->last_loop_timestamp_ = millis();
        if(this->takeover_mode_switch_ != nullptr && this->takeover_mode_switch_->state)
        {
            this->takeover_();
            if(this->swg_percent_number_ != nullptr)
            {
                this->set_percent_(this->swg_percent_number_->state);
            }
        }
        this->get_version_();
        this->get_temp_();
    }
}

void INTELLICHLORComponent::send_command_(const uint8_t *command, int command_len, uint8_t retries) {
  uint8_t crc = 0;
  std::vector<uint8_t> packet;
  packet.reserve(command_len+5);

  ESP_LOGD(TAG, "send_command_ Len:%i Retries:%i", command_len, retries);
  for (int i = 0; i < command_len; i++) {
    ESP_LOGV(TAG, "send_command_ %i: %02X", i, command[i]);
  }

  ESP_LOGV(TAG, "send_command_ write_array CMD_FRAME_HEADER 2");
  //this->write_array(CMD_FRAME_HEADER, 2);
  packet.push_back(CMD_FRAME_HEADER[0]);
  crc += CMD_FRAME_HEADER[0];

  packet.push_back(CMD_FRAME_HEADER[1]);
  crc += CMD_FRAME_HEADER[1];

  if (command != nullptr) {
    for (int i = 0; i < command_len; i++) {
      ESP_LOGV(TAG, "send_command_ write_byte command %i %02X", i, command[i]);
      //this->write_byte(command[i]);
      packet.push_back(command[i]);
      crc += command[i];
    }
  }

  ESP_LOGV(TAG, "send_command_ write_byte CRC %02X", crc);
  //this->write_byte(crc);
  packet.push_back(crc);

  ESP_LOGV(TAG, "send_command_ write_array CMD_FRAME_FOOTER 2");
  //this->write_array(CMD_FRAME_FOOTER, 2);
  packet.push_back(CMD_FRAME_FOOTER[0]);
  packet.push_back(CMD_FRAME_FOOTER[1]);

  auto size = this->send_queue_.size();
  ESP_LOGV(TAG, "send_command_ queue packet QueueSize:%i", size);
  this->send_queue_.push(std::make_tuple(retries, 0, packet));

  //this->flush();
  
  //delay(30);  // NOLINT
}

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ return std::string(); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

void INTELLICHLORComponent::log_hex(std::string prefix, std::vector<uint8_t> bytes, size_t len, uint8_t separator)
{
    std::string res;
    res += prefix;
    res += ": ";
    char buf[5];
    for (size_t i = 0; i < len; i++) {
      if (i > 0) {
        res += separator;
      }
      sprintf(buf, "%02X", bytes[i]);
      res += buf;
    }
    ESP_LOGI(TAG, "%s", res.c_str());
    delay(10);
}

void INTELLICHLORComponent::log_hex(std::string prefix, uint8_t* bytes, size_t len, uint8_t separator)
{
    std::string res;
    res += prefix;
    res += ": ";
    char buf[5];
    for (size_t i = 0; i < len; i++) {
      if (i > 0) {
        res += separator;
      }
      sprintf(buf, "%02X", bytes[i]);
      res += buf;
    }
    ESP_LOGI(TAG, "%s", res.c_str());
    delay(10);
}

bool INTELLICHLORComponent::readline_(int readch, uint8_t *buffer, int len) {
    static int pos = 0;
   
    if (pos == 0 && readch == 0x10) {
        ESP_LOGV(TAG, "readline_ Good header1");
        buffer[pos] = readch;
        pos++;

    } else if (pos == 1 && readch == 0x02) {
        ESP_LOGV(TAG, "readline_ Good header2");
        buffer[pos] = readch;
        pos++;

    } else if (pos == 0) {
        ESP_LOGV(TAG, "readline_ BAD header1");

    } else if (pos == 1) {
        ESP_LOGV(TAG, "readline_ BAD header2");

    } else if (pos >= 2 && pos < len - 1) {
        buffer[pos] = readch;
        
        if(buffer[pos] == 0x03 && buffer[pos-1] == 0x10)
        {

            auto packet_len = pos + 1;
            
            std::string debug = "FullPacket ";
            this->last_recv_timestamp_ = millis();
            
            ESP_LOGD(TAG, "readline_ complete packet RecvBuffer:%i", this->available());

            for (int i = 0; i <= pos; i++) {
                ESP_LOGV(TAG, "readline_ complete packet %i: %02X", i, buffer[i]);
            }

            if(pos >= 4 && buffer[3] == 0x03 )
            {
                //log_hex("VerResp", buffer, packet_len, ' ');
                this->version_ = "";
                for(int i = 5; i <= pos-3; i++)
                {
                    this->version_ += buffer[i];
                }
                ESP_LOGD(TAG, "VersionResp Packet Version:%s", this->version_.c_str());
                debug += "VersionResp Version:";
                debug += this->version_;
                if (this->version_text_sensor_ != nullptr) {
                    this->version_text_sensor_->publish_state(this->version_);
                }
                
            } else if(pos >= 4 && buffer[3] == 0x16 )
            {
                //log_hex("TempResp", buffer, packet_len, ' ');
                auto temp = buffer[4];
                ESP_LOGD(TAG, "TempResp Packet Temp:%i", temp);
                debug += string_format("TempResp Temp:%i", temp);

                // This is ocassionally 0 for one packet
                if(temp != 0 && this->water_temp_sensor_ != nullptr)
                {
                    this->water_temp_sensor_->publish_state(temp);
                }
                
                ESP_LOGD(TAG, "Got Temp, immidiately try another loop");
                this->run_again_ = true;
                
            } else if(pos >= 4 && buffer[3] == 0x12 )
            {
                //log_hex("SetResp", buffer, packet_len, ' ');
                uint16_t saltPPM = buffer[4] * 50;
                auto errorField = buffer[5];
                ESP_LOGD(TAG, "SetResp Packet Salt:%u Error:%02X", saltPPM, errorField);
                debug += string_format("SetResp Salt:%u Error:%02X", saltPPM, errorField);

                /*
                this->high_current_binary_sensor_->publish_state(GETBIT8(errorField, 4));
                this->low_volts_binary_sensor_->publish_state(GETBIT8(errorField, 5));
                this->check_pcb_binary_sensor_->publish_state(GETBIT8(errorField, 7));
                */

                if (this->no_flow_binary_sensor_ != nullptr)
                    this->no_flow_binary_sensor_->publish_state(GETBIT8(errorField, 0));
                if (this->low_salt_binary_sensor_ != nullptr)
                    this->low_salt_binary_sensor_->publish_state(GETBIT8(errorField, 1));
                if (this->very_low_salt_binary_sensor_ != nullptr)
                    this->very_low_salt_binary_sensor_->publish_state(GETBIT8(errorField, 2));
                // 3 unknown
                if (this->clean_binary_sensor_ != nullptr)
                    this->clean_binary_sensor_->publish_state(GETBIT8(errorField, 4));
                // 5 unknown
                if (this->low_temp_binary_sensor_ != nullptr)
                    this->low_temp_binary_sensor_->publish_state(GETBIT8(errorField, 6));


                if (this->salt_ppm_sensor_ != nullptr)
                    this->salt_ppm_sensor_->publish_state(saltPPM);
                if (this->error_sensor_ != nullptr)
                    this->error_sensor_->publish_state(errorField);

                if (this->set_percent_sensor_ != nullptr) {
                    this->set_percent_sensor_->publish_state(this->last_set_percent_);
                }
                
            } else if(pos >= 4 && buffer[3] == 0x01 )
            {
                //log_hex("TakeoverResp", buffer, packet_len, ' ');
                auto status = buffer[3];
                ESP_LOGD(TAG, "TakeoverResp Packet Status:%02X", status);
                debug += string_format("TakeoverResp Status:%02x", status);
                if (this->status_sensor_ != nullptr)
                    this->status_sensor_->publish_state(status);
                
            }

            //this->swg_debug_text_sensor_->publish_state(debug);

            if (!this->send_queue_.empty())
            {
                auto packet = this->send_queue_.front();
                auto retries = std::get<0>(packet);
                auto attempts = std::get<1>(packet);
                ESP_LOGD(TAG, "Got response, removing from send queue Retries:%i Attempts:%i", retries, attempts);
                this->send_queue_.pop();
            }

            pos=0;
            for(int i = 0; i < len; i++)
            {
                buffer[i] = 0x00;
            }
            return true;


        } else {
            pos++;
        }
    } else {
        ESP_LOGW(TAG, "Clearing Buffer after error");
        pos=0;
        for(int i = 0; i < len; i++)
        {
            buffer[i] = 0x00;
        }
    }
    return false;
}

}  // namespace intellichlor
}  //