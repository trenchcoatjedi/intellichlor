import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_SWITCH,
    ICON_BLUETOOTH,
    ENTITY_CATEGORY_CONFIG,
    ICON_PULSE,
)
from .. import CONF_INTELLICHLOR_ID, INTELLICHLORComponent, intellichlor_ns

TakeoverModeSwitch = intellichlor_ns.class_("TakeoverModeSwitch", switch.Switch)

CONF_TAKEOVER_MODE = "takeover_mode"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
    cv.Optional(CONF_TAKEOVER_MODE): switch.switch_schema(
        TakeoverModeSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_PULSE,
        default_restore_mode="RESTORE_DEFAULT_ON",
    ),
}


async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if takeover_mode_config := config.get(CONF_TAKEOVER_MODE):
        s = await switch.new_switch(takeover_mode_config)
        await cg.register_parented(s, config[CONF_INTELLICHLOR_ID])
        cg.add(intellichlor_component.set_takeover_mode_switch(s))
