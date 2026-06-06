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
            // Take a reference so the incremented attempt count is persisted
            // back into the queued entry across loop iterations.
            auto &packet = this->send_queue_.front();
            auto retries = std::get<0>(packet);
            auto attempts = ++std::get<1>(packet);
            auto data = std::get<2>(packet);

            ESP_LOGD(TAG, "Process Queue Retries:%i Attempt:%i", retries, attempts);

            if(attempts > retries)
            {
                ESP_LOGE(TAG, "No response after %i attempt(s), removing from send queue", attempts);
                this->send_queue_.pop();
            }
            else
            {
                if (this->flow_control_pin_ != nullptr)
                {
                    ESP_LOGV(TAG, "Enable Send");
                    this->flow_control_pin_->digital_write(true);
                }

                ESP_LOGV(TAG, "Sending %i bytes", (int) data.size());
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
    // percent == 16 (0x10) is DLE-stuffed automatically by send_command_ (§3), which
    // emits the same 10 02 50 11 10 00 83 10 03 the old 16%-only pad byte produced.
    this->last_set_percent_ = percent;
    uint8_t cmd[3] = {0x50, 0x11, percent};
    this->send_command_(cmd, 3, 3);
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

  // DLE byte-stuffing (protocol §3): the framing header (10 02) and footer (10 03) are
  // emitted literally, but any 0x10 appearing in the ADDR/CMD/DATA/CKS region is escaped
  // as the two bytes 10 00. The checksum is computed over the *unstuffed* bytes (§4).
  ESP_LOGV(TAG, "send_command_ write_array CMD_FRAME_HEADER 2");
  packet.push_back(CMD_FRAME_HEADER[0]);
  crc += CMD_FRAME_HEADER[0];

  packet.push_back(CMD_FRAME_HEADER[1]);
  crc += CMD_FRAME_HEADER[1];

  if (command != nullptr) {
    for (int i = 0; i < command_len; i++) {
      ESP_LOGV(TAG, "send_command_ write_byte command %i %02X", i, command[i]);
      crc += command[i];
      packet.push_back(command[i]);
      if (command[i] == 0x10) {
        packet.push_back(0x00);  // stuff
      }
    }
  }

  ESP_LOGV(TAG, "send_command_ write_byte CRC %02X", crc);
  packet.push_back(crc);
  if (crc == 0x10) {
    packet.push_back(0x00);  // stuff
  }

  ESP_LOGV(TAG, "send_command_ write_array CMD_FRAME_FOOTER 2");
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
    // DLE escape state (§3): set when the previous body byte was 0x10 and we are waiting
    // on the next byte to decide stuffed-data (10 00) vs footer (10 03) vs resync (10 02).
    static bool esc = false;

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

        // Body byte. Apply DLE un-stuffing (§3): a body 0x10 is held in `esc`; the next
        // byte decides whether it was a stuffed data byte (10 00 -> single 0x10), the real
        // footer (10 03), or a mid-frame resync (10 02). Framing DLEs are never un-stuffed.
        bool frame_complete = false;

        if (esc) {
            esc = false;
            if (readch == 0x00) {
                // stuffed data byte: collapse 10 00 back to a single 0x10
                buffer[pos] = 0x10;
                pos++;
            } else if (readch == 0x03) {
                // real footer (10 03). Keep the footer bytes in the buffer so the handler
                // offsets below (which expect pos to index the trailing 0x03) stay valid.
                buffer[pos] = 0x10;
                pos++;
                buffer[pos] = 0x03;
                frame_complete = true;
            } else if (readch == 0x02) {
                // unexpected STX mid-frame: treat as the start of a fresh frame
                ESP_LOGW(TAG, "readline_ DLE STX mid-frame, resyncing");
                buffer[0] = 0x10;
                buffer[1] = 0x02;
                pos = 2;
            } else {
                ESP_LOGW(TAG, "readline_ bad DLE escape %02X, clearing", readch);
                pos = 0;
                for (int i = 0; i < len; i++) buffer[i] = 0x00;
            }
        } else if (readch == 0x10) {
            // hold: could be a stuffed data byte (10 00) or the footer start (10 03)
            esc = true;
        } else {
            buffer[pos] = readch;
            pos++;
        }

        if (frame_complete)
        {

            auto packet_len = pos + 1;

            // Validate checksum (§4): (sum of bytes from the leading 10 02 through the last
            // DATA byte) & 0xFF must equal the CKS byte. Layout is [.. CKS][10][03] with the
            // footer 0x03 at index `pos`, so CKS is buffer[pos-2] and DATA ends at pos-3.
            uint8_t cks_calc = 0;
            for (int i = 0; i <= pos - 3; i++) {
                cks_calc += buffer[i];
            }
            uint8_t cks_rx = buffer[pos - 2];
            if (cks_calc != cks_rx) {
                ESP_LOGW(TAG, "readline_ bad checksum calc:%02X rx:%02X, dropping frame", cks_calc, cks_rx);
                pos = 0;
                esc = false;
                for (int i = 0; i < len; i++) buffer[i] = 0x00;
                return false;  // leave the send queue intact so the command retries
            }

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
                if (this->version_text_sensor_ != nullptr) {
                    this->version_text_sensor_->publish_state(this->version_);
                }
                
            } else if(pos >= 4 && buffer[3] == 0x16 )
            {
                //log_hex("TempResp", buffer, packet_len, ' ');
                auto temp = buffer[4];
                ESP_LOGD(TAG, "TempResp Packet Temp:%i", temp);

                // This is ocassionally 0 for one packet
                if(temp != 0 && this->water_temp_sensor_ != nullptr)
                {
                    this->water_temp_sensor_->publish_state(temp);
                }

                // The 0x16 Status reply (§5.2) also carries the cell's current generation
                // output % and firmware version. Older/short replies only return temp, so
                // these are length-guarded (footer 0x03 is at index `pos`): a 2nd data byte
                // exists when pos >= 8, and the version bytes [6][7] exist when pos >= 10.
                if(pos >= 8)
                {
                    auto out_pct = buffer[5];
                    ESP_LOGD(TAG, "StatusResp Output:%i", out_pct);
                    if (this->output_percent_sensor_ != nullptr)
                        this->output_percent_sensor_->publish_state(out_pct);
                }
                if(pos >= 10)
                {
                    auto fw = string_format("%i.%i", buffer[6], buffer[7]);
                    ESP_LOGD(TAG, "StatusResp Firmware:%s", fw.c_str());
                    if (this->firmware_version_text_sensor_ != nullptr)
                        this->firmware_version_text_sensor_->publish_state(fw);
                }

                ESP_LOGD(TAG, "Got Temp, immidiately try another loop");
                this->run_again_ = true;

            } else if(pos >= 4 && buffer[3] == 0x12 )
            {
                //log_hex("SetResp", buffer, packet_len, ' ');
                uint16_t saltPPM = buffer[4] * 50;
                auto errorField = buffer[5];
                ESP_LOGD(TAG, "SetResp Packet Salt:%u Error:%02X", saltPPM, errorField);

                // Status/error bitfield decode per protocol §5.4:
                //   bit0 no flow   bit1 low salt    bit2 very low salt  bit3 high current
                //   bit4 clean cell  bit5 low voltage  bit6 cold water   bit7 check PCB*
                // *bit7 is not in §5.4 but is emitted by real hardware (notes.txt) and kept.
                // WARNING: bits 3/4 here follow the spec and DIVERGE from the notes.txt
                // hardware capture (which has bit3=clean cell, bit4=high current, and a real
                // Error:80 / Check-PCB sample). If a real "clean cell" condition lights up the
                // high_current entity (or vice versa), revert bits 3/4 to the notes.txt order.
                if (this->no_flow_binary_sensor_ != nullptr)
                    this->no_flow_binary_sensor_->publish_state(GETBIT8(errorField, 0));
                if (this->low_salt_binary_sensor_ != nullptr)
                    this->low_salt_binary_sensor_->publish_state(GETBIT8(errorField, 1));
                if (this->very_low_salt_binary_sensor_ != nullptr)
                    this->very_low_salt_binary_sensor_->publish_state(GETBIT8(errorField, 2));
                if (this->high_current_binary_sensor_ != nullptr)
                    this->high_current_binary_sensor_->publish_state(GETBIT8(errorField, 3));
                if (this->clean_binary_sensor_ != nullptr)
                    this->clean_binary_sensor_->publish_state(GETBIT8(errorField, 4));
                if (this->low_volts_binary_sensor_ != nullptr)
                    this->low_volts_binary_sensor_->publish_state(GETBIT8(errorField, 5));
                if (this->low_temp_binary_sensor_ != nullptr)
                    this->low_temp_binary_sensor_->publish_state(GETBIT8(errorField, 6));
                if (this->check_pcb_binary_sensor_ != nullptr)
                    this->check_pcb_binary_sensor_->publish_state(GETBIT8(errorField, 7));


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
                // 0x01 Hello/Ack. Spec §7 shows a single data byte (buffer[4]); some replies
                // may carry none, in which case buffer[4] is the checksum. Only read it when a
                // data byte is actually present (footer at pos => data byte exists at pos >= 7).
                uint8_t status = (pos >= 7) ? buffer[4] : 0;
                ESP_LOGD(TAG, "TakeoverResp Packet Status:%02X", status);
                if (this->status_sensor_ != nullptr)
                    this->status_sensor_->publish_state(status);
                
            }

            if (!this->send_queue_.empty())
            {
                auto packet = this->send_queue_.front();
                auto retries = std::get<0>(packet);
                auto attempts = std::get<1>(packet);
                ESP_LOGD(TAG, "Got response, removing from send queue Retries:%i Attempts:%i", retries, attempts);
                this->send_queue_.pop();
            }

            pos=0;
            esc=false;
            for(int i = 0; i < len; i++)
            {
                buffer[i] = 0x00;
            }
            return true;
        }
    } else {
        ESP_LOGW(TAG, "Clearing Buffer after error");
        pos=0;
        esc=false;
        for(int i = 0; i < len; i++)
        {
            buffer[i] = 0x00;
        }
    }
    return false;
}

}  // namespace intellichlor
}  //