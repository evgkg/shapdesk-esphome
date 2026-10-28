import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, binary_sensor, cover, esp32
from esphome.const import CONF_ID

DEPENDENCIES = ['esp32', 'api', 'wifi']
AUTO_LOAD = ['sensor', 'binary_sensor', 'cover']
MULTI_CONF = False
ns = cg.esphome_ns.namespace('shapdesk')
Shapdesk = ns.class_('Shapdesk', cg.Component, cover.Cover)

def limits(config):
    if config['minimum'] >= config['maximum']:
        raise cv.Invalid('minimum must be less than maximum')
    return config

CONFIG_SCHEMA = cv.All(cover.cover_schema(Shapdesk).extend({
    cv.GenerateID(): cv.declare_id(Shapdesk),
    cv.Optional('max_hold_ms', default=20000): cv.int_range(min=1, max=120000),
    cv.Optional('pulse_ms', default=300): cv.int_range(min=1, max=500),
    cv.Optional('start_timeout_ms', default=3000): cv.int_range(min=1, max=30000),
    cv.Optional('minimum', default=66): cv.float_range(min=50, max=150),
    cv.Optional('maximum', default=131): cv.float_range(min=50, max=150),
    cv.Optional('tolerance', default=1): cv.float_range(min=0, max=5),
    cv.Optional('stop_offset', default=1): cv.float_range(min=0, max=5),
    cv.Optional('adapt', default=True): cv.boolean,
    cv.Required('height'): sensor.sensor_schema(unit_of_measurement='cm', accuracy_decimals=0),
    cv.Required('display'): sensor.sensor_schema(unit_of_measurement='cm', accuracy_decimals=0, entity_category='diagnostic'),
    cv.Required('raw'): sensor.sensor_schema(accuracy_decimals=0, entity_category='diagnostic'),
    cv.Required('good'): sensor.sensor_schema(accuracy_decimals=0, entity_category='diagnostic'),
    cv.Required('rejected'): sensor.sensor_schema(accuracy_decimals=0, entity_category='diagnostic'),
    cv.Required('moving'): binary_sensor.binary_sensor_schema(),
    cv.Required('link'): binary_sensor.binary_sensor_schema(),
}).extend(cv.COMPONENT_SCHEMA), limits)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)
    for key in ['max_hold_ms','pulse_ms','start_timeout_ms','minimum','maximum','tolerance','stop_offset','adapt']:
        cg.add(getattr(var, 'set_' + key)(config[key]))
    for key in ['height','display','raw','good','rejected']:
        entity = await sensor.new_sensor(config[key])
        cg.add(getattr(var, 'set_' + key)(entity))
    for key in ['moving','link']:
        entity = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, 'set_' + key)(entity))
    esp32.add_idf_sdkconfig_option('CONFIG_GPTIMER_ISR_CACHE_SAFE', True)
    esp32.add_idf_sdkconfig_option('CONFIG_GPIO_CTRL_FUNC_IN_IRAM', True)
    esp32.include_builtin_idf_component('esp_driver_gptimer')
    esp32.include_builtin_idf_component('esp_driver_uart')
