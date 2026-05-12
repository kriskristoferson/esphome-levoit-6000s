import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from .. import levoit_6000s_ns, Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

Levoit6000SSelect = levoit_6000s_ns.class_(
    "Levoit6000SSelect", select.Select, cg.Component
)

MODE_OPTIONS = ["Manual", "Auto", "Sleep", "Filter Dry L", "Filter Dry H"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        cv.Optional("mode"): select.select_schema(Levoit6000SSelect).extend(
            cv.COMPONENT_SCHEMA
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])

    if "mode" in config:
        sel = await select.new_select(config["mode"], options=MODE_OPTIONS)
        await cg.register_component(sel, config["mode"])
        cg.add(sel.set_parent(parent))
        cg.add(sel.set_select_type(0))
        cg.add(parent.register_select(0, sel))
