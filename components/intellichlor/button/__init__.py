import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
)
from .. import CONF_INTELLICHLOR_ID, INTELLICHLORComponent, intellichlor_ns

EndBoostButton = intellichlor_ns.class_("EndBoostButton", button.Button)

CONF_END_BOOST = "end_boost"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_INTELLICHLOR_ID): cv.use_id(INTELLICHLORComponent),
        cv.Optional(CONF_END_BOOST): button.button_schema(
            EndBoostButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:timer-off",
        ),
    }
)


async def to_code(config):
    # The button calls parent_->cancel_boost() directly, so it only needs the parent
    # relationship — no hub-side pointer/setter is required.
    if end_boost_config := config.get(CONF_END_BOOST):
        b = await button.new_button(end_boost_config)
        await cg.register_parented(b, config[CONF_INTELLICHLOR_ID])
