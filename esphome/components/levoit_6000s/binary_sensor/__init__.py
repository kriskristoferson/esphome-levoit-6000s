import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_PROBLEM
from .. import Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

CONF_WATER_TANK_MISSING = "water_tank_missing"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        cv.Optional(CONF_WATER_TANK_MISSING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])
    if CONF_WATER_TANK_MISSING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_WATER_TANK_MISSING])
        cg.add(parent.set_water_tank_missing(sens))
