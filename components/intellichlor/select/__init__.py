import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    ICON_TIMELAPSE,
)
from .. import CONF_INTELLICHLOR_ID, INTELLICHLORComponent, intellichlor_ns

SWGBoostSelect = intellichlor_ns.class_("SWGBoostSelect", select.Select)

CONF_SWG_BOOST = "swg_boost"

# "Off" first so it is the default state on boot (before any reboot-resume).
BOOST_OPTIONS = ["Off", "6h", "12h", "24h", "48h"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
        cv.Optional(CONF_SWG_BOOST): select.select_schema(
            SWGBoostSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=ICON_TIMELAPSE,
        ),
    }
)


async def to_code(config):
    intellichlor_component = await cg.get_variable(config[CONF_INTELLICHLOR_ID])
    if swg_boost_config := config.get(CONF_SWG_BOOST):
        s = await select.new_select(swg_boost_config, options=BOOST_OPTIONS)
        await cg.register_parented(s, config[CONF_INTELLICHLOR_ID])
        cg.add(intellichlor_component.set_swg_boost_select(s))
