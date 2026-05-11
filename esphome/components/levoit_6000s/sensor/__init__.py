import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_CELSIUS,
)
from .. import Levoit6000S, CONF_LEVOIT_6000S_ID

DEPENDENCIES = ["levoit_6000s"]

CONF_CURRENT_HUMIDITY = "current_humidity"
CONF_CURRENT_TEMP = "current_temp"
CONF_FAN_SPEED = "fan_speed"
CONF_FILTER_LIFE_PCT = "filter_life_pct"
CONF_FILTER_LIFE_DAYS = "filter_life_days"
CONF_TIMER_REMAINING = "timer_remaining"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LEVOIT_6000S_ID): cv.use_id(Levoit6000S),
        cv.Optional(CONF_CURRENT_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_TEMP): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FAN_SPEED): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FILTER_LIFE_PCT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FILTER_LIFE_DAYS): sensor.sensor_schema(
            unit_of_measurement="d",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_TIMER_REMAINING): sensor.sensor_schema(
            unit_of_measurement="min",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_6000S_ID])

    for conf_key, setter in [
        (CONF_CURRENT_HUMIDITY, "set_current_humidity_sensor"),
        (CONF_CURRENT_TEMP, "set_current_temp_sensor"),
        (CONF_FAN_SPEED, "set_fan_speed_sensor"),
        (CONF_FILTER_LIFE_PCT, "set_filter_life_pct_sensor"),
        (CONF_FILTER_LIFE_DAYS, "set_filter_life_days_sensor"),
        (CONF_TIMER_REMAINING, "set_timer_remaining_sensor"),
    ]:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(parent, setter)(sens))
