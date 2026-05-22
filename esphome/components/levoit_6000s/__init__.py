import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

levoit_6000s_ns = cg.esphome_ns.namespace("levoit_6000s")
Levoit6000S = levoit_6000s_ns.class_("Levoit6000S", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(Levoit6000S)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
