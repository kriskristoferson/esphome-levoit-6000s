import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from .. import levoit_6000s_ns, Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

Levoit6000SSwitch = levoit_6000s_ns.class_(
    "Levoit6000SSwitch", switch.Switch, cg.Component
)

SWITCH_TYPES = {
    "power": 0,
    "display": 1,
    "auto_dry": 2,
}

SWITCH_SCHEMA = switch.switch_schema(Levoit6000SSwitch).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        **{cv.Optional(k): SWITCH_SCHEMA for k in SWITCH_TYPES},
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])
    for key, type_id in SWITCH_TYPES.items():
        if key in config:
            sw = await switch.new_switch(config[key])
            await cg.register_component(sw, config[key])
            cg.add(sw.set_parent(parent))
            cg.add(sw.set_switch_type(type_id))
            cg.add(parent.register_switch(type_id, sw))
