import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_PROBLEM
from .. import Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

BINARY_SENSORS = {
    "water_tank_missing": ("set_water_tank_missing_sensor", DEVICE_CLASS_PROBLEM),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        **{
            cv.Optional(k): binary_sensor.binary_sensor_schema(
                device_class=dc,
            )
            for k, (_, dc) in BINARY_SENSORS.items()
        },
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])
    for key, (setter, _) in BINARY_SENSORS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(parent, setter)(sens))
