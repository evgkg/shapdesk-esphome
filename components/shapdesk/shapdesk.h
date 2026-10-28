#pragma once
#include "esphome/core/component.h"
#include "driver/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/cover/cover.h"
#include "control.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"

namespace esphome::shapdesk {
class Shapdesk : public Component, public cover::Cover, public shapdesk_core::Output {
 public:
  Shapdesk() : controller(*this) {}
  void setup() override;
  void loop() override;
  void on_shutdown() override { stop(); }
  float get_setup_priority() const override { return setup_priority::HARDWARE + 1; }
  cover::CoverTraits get_traits() override {
    auto traits = cover::CoverTraits(); traits.set_supports_position(true); traits.set_supports_stop(true); return traits;
  }
  void control(const cover::CoverCall &call) override;
  bool press(int key, uint32_t duration) override;
  void release() override;
  bool active() const override;
  void stop() { controller.stop(); }
  void inhibit() { ready_ = false; controller.stop("shutdown/OTA"); }
  void press_key(int key, int duration);
  bool move_to(float target);
  void set_max_hold_ms(uint32_t value) { controller.settings.max_hold_ms = value; }
  void set_pulse_ms(uint32_t value) { controller.settings.pulse_ms = value; }
  void set_start_timeout_ms(uint32_t value) { controller.settings.start_timeout_ms = value; }
  void set_minimum(float value) { controller.settings.minimum = value; }
  void set_maximum(float value) { controller.settings.maximum = value; }
  void set_tolerance(float value) { controller.settings.tolerance = value; }
  void set_stop_offset(float value) { controller.settings.stop_offset = value; }
  void set_adapt(bool value) { controller.settings.adapt = value; }
  void set_height(sensor::Sensor *value) { height = value; }
  void set_display(sensor::Sensor *value) { display = value; }
  void set_raw(sensor::Sensor *value) { raw = value; }
  void set_good(sensor::Sensor *value) { good = value; }
  void set_rejected(sensor::Sensor *value) { rejected = value; }
  void set_moving(binary_sensor::BinarySensor *value) { moving = value; }
  void set_link(binary_sensor::BinarySensor *value) { link = value; }
  shapdesk_core::Controller controller;
  sensor::Sensor *height{}, *display{}, *raw{}, *good{}, *rejected{};
  binary_sensor::BinarySensor *moving{}, *link{};
 protected:
  static bool IRAM_ATTR alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
  static constexpr uint32_t MASK = (1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<0)|(1u<<1)|(1u<<3);
  static portMUX_TYPE lock_;
  static volatile int64_t deadline_;
  static volatile bool active_;
  static volatile int64_t released_at_;
  gptimer_handle_t timer_{};
  QueueHandle_t uart_events_{};
  desk_parser_t parser_{};
  int64_t published_{0};
  int64_t last_loop_{0};
  bool ready_{false};
};
}
