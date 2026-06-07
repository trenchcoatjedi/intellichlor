#pragma once

#include "esphome/components/button/button.h"
#include "../intellichlor.h"

namespace esphome {
namespace intellichlor {

class EndBoostButton : public button::Button, public Parented<INTELLICHLORComponent> {
 public:
  EndBoostButton() = default;

 protected:
  void press_action() override;
};

}  // namespace intellichlor
}  // namespace esphome
