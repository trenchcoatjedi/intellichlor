#include "swg_boost_select.h"
#include <cstdlib>

namespace esphome {
namespace intellichlor {

void SWGBoostSelect::control(const std::string &value) {
  this->publish_state(value);
  if (value == "Off") {
    this->parent_->cancel_boost();
  } else {
    // options are "6h" / "12h" / "24h" / "48h" -> leading integer is the hour count
    this->parent_->start_boost((uint8_t) atoi(value.c_str()));
  }
}

}  // namespace intellichlor
}  // namespace esphome
