#pragma once
#include "desk_tracking.h"
#include <algorithm>
#include <cmath>

namespace shapdesk_core {
struct Settings {
  uint32_t max_hold_ms{20000}, pulse_ms{300}, start_timeout_ms{3000};
  float minimum{66}, maximum{131}, tolerance{1}, stop_offset{1};
  bool adapt{true};
};
// Hardware owns its absolute deadline; this interface must never extend it.
struct Output {
  virtual ~Output() = default;
  virtual bool press(int key, uint32_t duration_ms) = 0;
  virtual void release() = 0;
  virtual bool active() const = 0;
};
inline bool valid_press(int key, int duration, uint32_t limit) {
  return key >= 1 && key <= 7 && duration > 0 &&
    static_cast<uint32_t>(duration) <= ((key >= 3 && key <= 6) ? std::min<uint32_t>(limit, 500) : limit);
}
class Controller {
 public:
  explicit Controller(Output &output) : output_(output) {}
  Settings settings;
  desk_tracking_t tracking{};
  const char *reason{"idle"};
  bool automatic{false};
  float learned_offset[2]{1, 1};
  bool fresh(int64_t now) const {
    return tracking.known && tracking.current_known && !tracking.current_held &&
      now >= tracking.last_rx_us && now - tracking.last_rx_us <= 1000000;
  }
  void stop(const char *why = "stop") {
    output_.release(); automatic = false; settling_ = false; key_ = 0; reason = why;
  }
  bool press_key(int key, int duration, int64_t now) {
    if (!valid_press(key, duration, settings.max_hold_ms)) { reason = "invalid key/duration"; return false; }
    if (output_.active() || automatic) {
      // A repeated request cannot restart or extend a press. A conflict stops it.
      if (key != key_) stop("conflicting request");
      else reason = "busy: deadline unchanged";
      return false;
    }
    settling_ = false;
    if ((key == 1 || key == 2) && boundary(key == 1 ? 1 : -1, now)) {
      reason = "height boundary"; return false;
    }
    if (!output_.press(key, duration)) { stop("output failure"); return false; }
    key_ = key; reason = "manual press"; return true;
  }
  bool move(float target, int64_t now) {
    if (output_.active() || automatic) { stop("conflicting target request"); return false; }
    settling_ = false;
    if (!std::isfinite(target) || target < settings.minimum || target > settings.maximum) {
      reason = "target outside limits"; return false;
    }
    if (!fresh(now)) { reason = "fresh unheld height required"; return false; }
    const float delta = target - tracking.current_height;
    if (std::fabs(delta) <= settings.tolerance) { reason = "within tolerance"; return true; }
    direction_ = delta > 0 ? 1 : -1;
    if (!press_key(direction_ > 0 ? 1 : 2, settings.max_hold_ms, now)) return false;
    automatic = true; target_ = target; began_ = now; started_ = false;
    reason = "moving to target"; return true;
  }
  void tick(int64_t now, bool connected) {
    desk_tracking_tick(&tracking, now);
    if (!connected) { stop("Wi-Fi/API disconnected"); return; }
    if (settling_) {
      if (!fresh(now) || tracking.motion_direction == -direction_) { settling_ = false; }
      else if (!tracking.moving && now - released_ >= 500000) {
        const float coast = direction_ * (static_cast<float>(tracking.current_height) - release_height_);
        if (settings.adapt && coast >= 0 && coast <= 5) {
          float &offset = learned_offset[direction_ > 0 ? 0 : 1];
          offset = std::clamp(0.75f * offset + 0.25f * coast, 0.0f, 5.0f);
        }
        settling_ = false;
      } else if (now - released_ > 5000000) settling_ = false;
    }
    if (!output_.active()) { if (automatic) stop("hard deadline"); key_ = 0; return; }
    int direction = key_ == 1 ? 1 : key_ == 2 ? -1 : tracking.motion_direction;
    if (direction && boundary(direction, now)) { stop("height boundary"); return; }
    if (!automatic) return;
    if (!tracking.known || now - tracking.last_rx_us > 500000) { stop("UART silence >500ms"); return; }
    if (!fresh(now)) { stop("physical height uncertain"); return; }
    if (tracking.moving && tracking.motion_direction != direction_) { stop("wrong direction"); return; }
    if (tracking.moving) started_ = true;
    if (!started_ && now - began_ >= static_cast<int64_t>(settings.start_timeout_ms) * 1000) {
      stop("start timeout"); return;
    }
    float offset = settings.adapt ? learned_offset[direction_ > 0 ? 0 : 1] : settings.stop_offset;
    if (direction_ * (target_ - tracking.current_height) <= offset) {
      release_height_ = tracking.current_height; output_.release(); automatic = false;
      key_ = 0; settling_ = true; released_ = now; reason = "target release";
    }
  }
 private:
  bool boundary(int direction, int64_t now) const {
    return fresh(now) && (direction > 0 ? tracking.current_height >= settings.maximum : tracking.current_height <= settings.minimum);
  }
  Output &output_;
  int key_{0}, direction_{0};
  bool started_{false}, settling_{false};
  float target_{0}, release_height_{0};
  int64_t began_{0}, released_{0};
};
}  // namespace shapdesk_core
