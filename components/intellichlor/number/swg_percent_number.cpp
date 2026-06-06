#include "swg_percent_number.h"
#include "esphome/core/log.h"

namespace esphome {
namespace intellichlor {

static const char *const TAG = "intellichlor.number";

void SWGPercentNumber::setup() {
  // Restore the last commanded output % from flash so it persists across reboots/OTA.
  float value = 0.0f;
  this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
  if (this->pref_.load(&value)) {
    ESP_LOGD(TAG, "Restored SWG percent: %.0f", value);
  } else {
    value = 0.0f;
  }
  this->publish_state(value);
  // Push it to the cell now (no-op unless takeover is on); the periodic poll also re-applies it.
  this->parent_->set_swg_percent();
}

void SWGPercentNumber::control(float value) {
  this->publish_state(value);
  this->pref_.save(&value);
  this->parent_->set_swg_percent();
}

}  // namespace intellichlor
}  // namespace esphome
