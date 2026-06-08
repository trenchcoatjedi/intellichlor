import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from . import CONF_PENTAIR_PUMP_ID, PentairPump, pentair_pump_ns

DEPENDENCIES = ["pentair_pump"]

PentairPumpModeSelect = pentair_pump_ns.class_("PentairPumpModeSelect", select.Select)

CONF_MODE = "mode"

# First option is the default (see PentairPump::setup). C++ picks flow mode when
# the chosen string mentions "GPM"/"Flow", so labels can be customized freely.
MODE_OPTIONS = ["Speed (RPM)", "Flow (GPM)"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PENTAIR_PUMP_ID): cv.use_id(PentairPump),
        cv.Optional(CONF_MODE): select.select_schema(
            PentairPumpModeSelect, icon="mdi:swap-horizontal"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_PENTAIR_PUMP_ID])
    if CONF_MODE in config:
        s = await select.new_select(config[CONF_MODE], options=MODE_OPTIONS)
        cg.add(s.set_parent(hub))
        cg.add(hub.set_mode_select(s))
