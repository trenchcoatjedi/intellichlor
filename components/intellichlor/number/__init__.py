import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_TIMEOUT,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_ILLUMINANCE,
    UNIT_SECOND,
    UNIT_PERCENT,
    ENTITY_CATEGORY_CONFIG,
    ICON_MOTION_SENSOR,
    ICON_TIMELAPSE,
    ICON_LIGHTBULB,
)
from .. import CONF_INTELLICHLOR_ID, INTELLICHLORComponent, intellichlor_ns

SWGPercentNumber = intellichlor_ns.class_("SWGPercentNumber", number.Number, cg.Component)


CONF_SWG_PERCENT = "swg_percent"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
        cv.Optional(CONF_SWG_PERCENT): number.number_schema(
            SWGPercentNumber,
            device_class=DEVICE_CLASS_ILLUMINANCE,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=ICON_LIGHTBULB,
        ),
    }
)

async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if swg_percnt_config := config.get(CONF_SWG_PERCENT):
        n = await number.new_number(
            swg_percnt_config, min_value=0, max_value=100, step=1
        )
        await cg.register_component(n, swg_percnt_config)
        await cg.register_parented(n, config[CONF_INTELLICHLOR_ID])
        cg.add(intellichlor_component.set_swg_percent_number(n))
