// Implementation of buttons/hub.hpp (read that header for the contract).
#include "buttons/hub.hpp"

namespace buttons {
ButtonHub::ButtonHub()
    : map_(std::make_shared<const ControlMap>()), detector_(map_, read_) {}

void ButtonHub::configure(std::shared_ptr<const ControlMap> map, ReadMethod read) {
    const std::lock_guard<std::mutex> lock(mutex_);
    map_ = std::move(map);
    read_ = read;
    detector_ = EdgeDetector(map_, read_);
    edges_.clear();
    state_.reset();
}

void ButtonHub::publish(const ButtonState& state, Clock::time_point now) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto edges = detector_.feed(state);
    edges_.insert(edges_.end(), edges.begin(), edges.end());
    state_ = state;
    stamp_ = now;
}

void ButtonHub::release_all() {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto edges = detector_.release_all();
    edges_.insert(edges_.end(), edges.begin(), edges.end());
    state_.reset();
}

std::vector<Edge> ButtonHub::take_edges() {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Edge> out;
    out.swap(edges_);
    return out;
}

std::optional<std::uint32_t> ButtonHub::vjoy_word(Clock::time_point now) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_) return std::nullopt;
    if (read_ == ReadMethod::can_bus && now > stamp_ && now - stamp_ > can_freshness) return std::nullopt;
    std::uint32_t word = 0;
    for (const int control : pressed_controls(*map_, read_, *state_))
        if (const int button = map_->vjoy_button(control)) word |= 1u << (button - 1);   // 0: no virtual button
    return word;
}

std::shared_ptr<const ControlMap> ButtonHub::controls() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return map_;
}
}
