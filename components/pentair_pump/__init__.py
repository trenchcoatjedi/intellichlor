import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ADDRESS, CONF_ID

CODEOWNERS = ["@wolfson292"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "number", "switch", "select"]
MULTI_CONF = True

pentair_pump_ns = cg.esphome_ns.namespace("pentair_pump")
PentairPump = pentair_pump_ns.class_(
    "PentairPump", cg.PollingComponent, uart.UARTDevice
)

# Shared by the platform files (sensor.py, switch.py, ...).
CONF_PENTAIR_PUMP_ID = "pentair_pump_id"
CONF_SOURCE_ADDRESS = "source_address"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PentairPump),
            cv.Optional(CONF_ADDRESS, default=0x60): cv.hex_uint8_t,
            cv.Optional(CONF_SOURCE_ADDRESS, default=0x10): cv.hex_uint8_t,
        }
    )
    .extend(cv.polling_component_schema("2s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_source_address(config[CONF_SOURCE_ADDRESS]))
