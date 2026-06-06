#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/number/number.h"
#include "../intellichlor.h"

namespace esphome {
namespace intellichlor {

class SWGPercentNumber : public number::Number, public Component, public Parented<INTELLICHLORComponent> {
 public:
  SWGPercentNumber() = default;

  void setup() override;
  // Restore just before the parent (default DATA) so its first poll sees the restored value.
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }

 protected:
  void control(float value) override;

  // Flash-backed so the commanded output % survives a reboot / OTA update.
  ESPPreferenceObject pref_;
};

}  // namespace intellichlor
}  // namespace esphome
