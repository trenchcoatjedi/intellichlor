#include "end_boost_button.h"

namespace esphome {
namespace intellichlor {

void EndBoostButton::press_action() { this->parent_->cancel_boost(); }

}  // namespace intellichlor
}  // namespace esphome
