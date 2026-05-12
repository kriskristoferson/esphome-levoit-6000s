import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from .. import levoit_6000s_ns, Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

Levoit6000SNumber = levoit_6000s_ns.class_(
    "Levoit6000SNumber", number.Number, cg.Component
)

NUMBER_CONFIGS = {
    "target_humidity": {"type_id": 0, "min": 30, "max": 80, "step": 1},
    "timer": {"type_id": 1, "min": 0, "max": 12, "step": 1},
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        **{
            cv.Optional(k): number.number_schema(Levoit6000SNumber).extend(
                cv.COMPONENT_SCHEMA
            )
            for k in NUMBER_CONFIGS
        },
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])

    for key, cfg in NUMBER_CONFIGS.items():
        if key in config:
            num = await number.new_number(
                config[key],
                min_value=cfg["min"],
                max_value=cfg["max"],
                step=cfg["step"],
            )
            await cg.register_component(num, config[key])
            cg.add(num.set_parent(parent))
            cg.add(num.set_number_type(cfg["type_id"]))
            cg.add(parent.register_number(cfg["type_id"], num))
