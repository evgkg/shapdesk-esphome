#include "shapdesk.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/wifi/wifi_component.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include "esp_timer.h"

#if !CONFIG_GPTIMER_ISR_CACHE_SAFE
#error "Independent button guard requires CONFIG_GPTIMER_ISR_CACHE_SAFE"
#endif
namespace esphome::shapdesk {
portMUX_TYPE Shapdesk::lock_ = portMUX_INITIALIZER_UNLOCKED;
volatile int64_t Shapdesk::deadline_ = 0;
volatile bool Shapdesk::active_ = false;
volatile int64_t Shapdesk::released_at_ = -50000;
bool IRAM_ATTR Shapdesk::alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *) {
  portENTER_CRITICAL_ISR(&lock_);
  if (active_ && esp_timer_get_time() >= deadline_) {
    GPIO.out_w1tc.val = MASK; active_ = false; released_at_ = esp_timer_get_time();
  }
  portEXIT_CRITICAL_ISR(&lock_); return false;
}
void Shapdesk::release() {
  portENTER_CRITICAL(&lock_); GPIO.out_w1tc.val = MASK;
  if (active_) released_at_ = esp_timer_get_time();
  active_ = false; portEXIT_CRITICAL(&lock_);
}
bool Shapdesk::active() const {
  portENTER_CRITICAL(&lock_); bool value = active_; portEXIT_CRITICAL(&lock_); return value;
}
bool Shapdesk::press(int key, uint32_t duration) {
  if (!ready_ || !shapdesk_core::valid_press(key, duration, controller.settings.max_hold_ms)) return false;
  static constexpr int pins[]{5,6,7,10,0,1,3};
  portENTER_CRITICAL(&lock_);
  if (active_ || esp_timer_get_time() - released_at_ < 50000) { portEXIT_CRITICAL(&lock_); return false; }
  GPIO.out_w1tc.val = MASK;
  deadline_ = esp_timer_get_time() + static_cast<int64_t>(duration) * 1000;
  active_ = true; GPIO.out_w1ts.val = 1u << pins[key-1];
  portEXIT_CRITICAL(&lock_); return true;
}
void Shapdesk::setup() {
  release();
  gpio_config_t gpio{}; gpio.pin_bit_mask = MASK; gpio.mode = GPIO_MODE_OUTPUT;
  gpio.pull_down_en = GPIO_PULLDOWN_ENABLE;
  esp_err_t err = gpio_config(&gpio); release();
  gptimer_config_t cfg{}; cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP; cfg.resolution_hz = 1000000; cfg.intr_priority = 3;
  if (err == ESP_OK) err = gptimer_new_timer(&cfg, &timer_);
  gptimer_event_callbacks_t callbacks{}; callbacks.on_alarm = alarm;
  if (err == ESP_OK) err = gptimer_register_event_callbacks(timer_, &callbacks, nullptr);
  gptimer_alarm_config_t guard{}; guard.alarm_count = 1000; guard.reload_count = 0; guard.flags.auto_reload_on_alarm = true;
  if (err == ESP_OK) err = gptimer_set_alarm_action(timer_, &guard);
  if (err == ESP_OK) err = gptimer_enable(timer_);
  if (err == ESP_OK) err = gptimer_start(timer_);
  uart_config_t serial{}; serial.baud_rate = 9600; serial.data_bits = UART_DATA_8_BITS;
  serial.parity = UART_PARITY_DISABLE; serial.stop_bits = UART_STOP_BITS_1;
  serial.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; serial.source_clk = UART_SCLK_DEFAULT;
  if (err == ESP_OK) err = uart_driver_install(UART_NUM_1, 8192, 0, 64, &uart_events_, 0);
  if (err == ESP_OK) err = uart_param_config(UART_NUM_1, &serial);
  // TX explicitly disconnected; no UART write call exists in this component.
  if (err == ESP_OK) err = uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, 4, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) { release(); mark_failed(); ESP_LOGE("shapdesk", "Guard setup failed: %s", esp_err_to_name(err)); return; }
  controller.learned_offset[0] = controller.learned_offset[1] = controller.settings.stop_offset;
  ready_ = true;
}
static bool connected() {
  return wifi::global_wifi_component->is_connected() && api::global_api_server->is_connected();
}
void Shapdesk::press_key(int key, int duration) {
  if (!ready_ || !connected()) { stop(); ESP_LOGW("shapdesk", "Request rejected: not ready or no Wi-Fi/API"); return; }
  if (!controller.press_key(key, duration, esp_timer_get_time())) ESP_LOGW("shapdesk", "Request rejected: %s", controller.reason);
}
bool Shapdesk::move_to(float target) {
  if (!ready_ || !connected()) { stop(); return false; }
  bool ok = controller.move(target, esp_timer_get_time());
  if (!ok) ESP_LOGW("shapdesk", "Target rejected: %s", controller.reason);
  return ok;
}
void Shapdesk::control(const cover::CoverCall &call) {
  if (call.get_stop()) { stop(); return; }
  if (call.get_position().has_value()) {
    float p = *call.get_position();
    if (p == 1.0f) press_key(1, controller.settings.max_hold_ms);
    else if (p == 0.0f) press_key(2, controller.settings.max_hold_ms);
    else move_to(controller.settings.minimum + p * (controller.settings.maximum - controller.settings.minimum));
  }
}
void Shapdesk::loop() {
  if (is_failed() || !ready_) { release(); return; }
  int64_t now = esp_timer_get_time();
  // Bounded work prevents a noisy stream from starving the safety policy.
  uart_event_t event{};
  bool transport_error = last_loop_ && now - last_loop_ > 100000;
  last_loop_ = now;
  for (unsigned i = 0; i < 64 && xQueueReceive(uart_events_, &event, 0) == pdTRUE; ++i) {
    if (event.type != UART_DATA) transport_error = true;
  }
  size_t waiting = 0;
  if (uart_get_buffered_data_len(UART_NUM_1, &waiting) != ESP_OK || waiting > 512) transport_error = true;
  if (transport_error) {
    stop(); uart_flush_input(UART_NUM_1); xQueueReset(uart_events_);
    desk_parser_discard_partial(&parser_); desk_tracking_discontinuity(&controller.tracking);
    ESP_LOGW("shapdesk", "UART transport error/backlog: height invalidated");
  } else {
    uint8_t bytes[512]; int count = uart_read_bytes(UART_NUM_1, bytes, waiting, 0);
    if (count < 0) { stop(); desk_parser_discard_partial(&parser_); desk_tracking_discontinuity(&controller.tracking); }
    for (int i = 0; i < count; ++i) {
      desk_event_t ev{};
      if (desk_feed_rx(&parser_, bytes[i], &ev)) desk_tracking_feed(&controller.tracking, &ev, now);
    }
  }
  const char *before = controller.reason;
  controller.tick(now, connected());
  if (before != controller.reason && active_ == false) ESP_LOGI("shapdesk", "Released: %s", controller.reason);
  if (now - published_ < 100000) return;
  published_ = now;
  auto &t = controller.tracking;
  height->publish_state(controller.fresh(now) ? t.current_height : NAN);
  display->publish_state(t.known ? t.display_height : NAN);
  raw->publish_state(t.known ? t.position_raw : NAN);
  good->publish_state(parser_.good); rejected->publish_state(parser_.invalid);
  link->publish_state(t.known);
  if (t.known) moving->publish_state(t.moving); else moving->invalidate_state();
  position = controller.fresh(now) ? std::clamp((t.current_height-controller.settings.minimum)/(controller.settings.maximum-controller.settings.minimum),0.0f,1.0f) : NAN;
  current_operation = t.known && t.moving ? (t.motion_direction > 0 ? cover::COVER_OPERATION_OPENING : cover::COVER_OPERATION_CLOSING) : cover::COVER_OPERATION_IDLE;
  publish_state(false);
}
}
