#pragma once

#include "esphome/components/select/select.h"
#include "../intellichlor.h"

namespace esphome {
namespace intellichlor {

class SWGBoostSelect : public select::Select, public Parented<INTELLICHLORComponent> {
 public:
  SWGBoostSelect() = default;

 protected:
  void control(const std::string &value) override;
};

}  // namespace intellichlor
}  // namespace esphome
