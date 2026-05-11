import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
MULTI_CONF = False

levoit_6000s_ns = cg.esphome_ns.namespace("levoit_6000s")
Levoit6000S = levoit_6000s_ns.class_("Levoit6000S", cg.Component, uart.UARTDevice)

CONF_LEVOIT_6000S_ID = "levoit_6000s_id"

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(Levoit6000S)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
