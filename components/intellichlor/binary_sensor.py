import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_BUG,
)
from . import CONF_INTELLICHLOR_ID, INTELLICHLORComponent

DEPENDENCIES = ["intellichlor"]
CONF_NO_FLOW = "no_flow"
CONF_LOW_SALT = "low_salt"
CONF_VERY_LOW_SALT = "very_low_salt"
CONF_CLEAN = "clean"
CONF_HIGH_CURRENT = "high_current"
CONF_LOW_VOLTS = "low_volts"
CONF_LOW_TEMP = "low_temp"
CONF_CHECK_PCB = "check_pcb"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
    cv.Optional(CONF_NO_FLOW): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_LOW_SALT): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_VERY_LOW_SALT): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_CLEAN): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_HIGH_CURRENT): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_LOW_VOLTS): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_LOW_TEMP): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
    cv.Optional(CONF_CHECK_PCB): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_BUG,
    ),
}


async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if no_flow_config := config.get(CONF_NO_FLOW):
        sens = await binary_sensor.new_binary_sensor(no_flow_config)
        cg.add(intellichlor_component.set_no_flow_binary_sensor(sens))
    
    if low_salt_config := config.get(CONF_LOW_SALT):
        sens = await binary_sensor.new_binary_sensor(low_salt_config)
        cg.add(intellichlor_component.set_low_salt_binary_sensor(sens))
    
    if very_low_salt_config := config.get(CONF_VERY_LOW_SALT):
        sens = await binary_sensor.new_binary_sensor(very_low_salt_config)
        cg.add(intellichlor_component.set_very_low_salt_binary_sensor(sens))
    
    if clean_config := config.get(CONF_CLEAN):
        sens = await binary_sensor.new_binary_sensor(clean_config)
        cg.add(intellichlor_component.set_clean_binary_sensor(sens))

    if high_current_config := config.get(CONF_HIGH_CURRENT):
        sens = await binary_sensor.new_binary_sensor(high_current_config)
        cg.add(intellichlor_component.set_high_current_binary_sensor(sens))

    if low_volts_config := config.get(CONF_LOW_VOLTS):
        sens = await binary_sensor.new_binary_sensor(low_volts_config)
        cg.add(intellichlor_component.set_low_volts_binary_sensor(sens))

    if low_temp_config := config.get(CONF_LOW_TEMP):
        sens = await binary_sensor.new_binary_sensor(low_temp_config)
        cg.add(intellichlor_component.set_low_temp_binary_sensor(sens))

    if check_pcb_config := config.get(CONF_CHECK_PCB):
        sens = await binary_sensor.new_binary_sensor(check_pcb_config)
        cg.add(intellichlor_component.set_check_pcb_binary_sensor(sens))
