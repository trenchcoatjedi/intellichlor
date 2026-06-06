import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DISTANCE,
    UNIT_CENTIMETER,
    UNIT_PERCENT,
    CONF_LIGHT,
    DEVICE_CLASS_ILLUMINANCE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_SIGNAL,
    ICON_FLASH,
    ICON_MOTION_SENSOR,
    ICON_LIGHTBULB,
    DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
    UNIT_PARTS_PER_MILLION,
    DEVICE_CLASS_TEMPERATURE,
    UNIT_PERCENT
)
from . import CONF_INTELLICHLOR_ID, INTELLICHLORComponent

DEPENDENCIES = ["intellichlor"]

CONF_SALT_PPM = "salt_ppm"
CONF_TEMP = "water_temp"
CONF_STATUS = "status"
CONF_ERROR = "error"
CONF_SET_PERCENT = "set_percent"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
        cv.Optional(CONF_SALT_PPM): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
            unit_of_measurement=UNIT_PARTS_PER_MILLION,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_TEMP): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            unit_of_measurement="°F",
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_STATUS): sensor.sensor_schema(
        ),
        cv.Optional(CONF_ERROR): sensor.sensor_schema(
        ),
        cv.Optional(CONF_SET_PERCENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
        ),
    }
)

async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if salt_ppm_config := config.get(CONF_SALT_PPM):
        sens = await sensor.new_sensor(salt_ppm_config)
        cg.add(intellichlor_component.set_salt_ppm_sensor(sens))
    if temp_config := config.get(CONF_TEMP):
        sens = await sensor.new_sensor(temp_config)
        cg.add(intellichlor_component.set_water_temp_sensor(sens))
    if status_config := config.get(CONF_STATUS):
        sens = await sensor.new_sensor(status_config)
        cg.add(intellichlor_component.set_status_sensor(sens))
    if error_config := config.get(CONF_ERROR):
        sens = await sensor.new_sensor(error_config)
        cg.add(intellichlor_component.set_error_sensor(sens))
    if set_percent_config := config.get(CONF_SET_PERCENT):
        sens = await sensor.new_sensor(set_percent_config)
        cg.add(intellichlor_component.set_set_percent_sensor(sens))
    