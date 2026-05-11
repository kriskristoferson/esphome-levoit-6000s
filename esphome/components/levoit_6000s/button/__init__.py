import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from .. import levoit_6000s_ns, Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

Levoit6000SButton = levoit_6000s_ns.class_(
    "Levoit6000SButton", button.Button
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        cv.Optional("reset_filter"): button.button_schema(Levoit6000SButton),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])
    if "reset_filter" in config:
        btn = await button.new_button(config["reset_filter"])
        cg.add(btn.set_parent(parent))
