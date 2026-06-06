import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.cpp_helpers import gpio_pin_expression
from esphome.const import (
    
    CONF_ID,
    CONF_VERSION,
    ICON_BUG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from . import CONF_INTELLICHLOR_ID, INTELLICHLORComponent

DEPENDENCIES = ["intellichlor"]

CONF_SWG_DEBUG = "swg_debug"
CONF_FIRMWARE_VERSION = "firmware_version"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
    cv.Optional(CONF_VERSION): text_sensor.text_sensor_schema(
        icon=ICON_BUG,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
        icon=ICON_BUG,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    cv.Optional(CONF_SWG_DEBUG): text_sensor.text_sensor_schema(
        icon=ICON_BUG,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if version_config := config.get(CONF_VERSION):
        sens = await text_sensor.new_text_sensor(version_config)
        cg.add(intellichlor_component.set_version_text_sensor(sens))
    if firmware_version_config := config.get(CONF_FIRMWARE_VERSION):
        sens = await text_sensor.new_text_sensor(firmware_version_config)
        cg.add(intellichlor_component.set_firmware_version_text_sensor(sens))
    if swg_debug_config := config.get(CONF_SWG_DEBUG):
        sens = await text_sensor.new_text_sensor(swg_debug_config)
        cg.add(intellichlor_component.set_swg_debug_text_sensor(sens))

