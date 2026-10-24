#include "EngineLoop.hpp"
#include <algorithm>
namespace blubox::engine {
std::uint64_t EventBus::connect(std::string e, Callback cb) {
  if (!cb) return 0;
  const auto id = next_++;
  listeners_[std::move(e)].push_back({id, std::move(cb)});
  return id;
}
void EventBus::disconnect(std::uint64_t token) {
  for (auto& [name, list] : listeners_) {
    (void)name;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Listener& l) { return l.token == token; }),
               list.end());
  }
}
void EventBus::fire(const std::string& e, double dt) {
  auto i = listeners_.find(e);
  if (i == listeners_.end()) return;
  auto snapshot = i->second;
  for (const auto& l : snapshot) l.callback(dt);
}
void EngineLoop::advance(double frameDt, double renderDt) {
  frameDt = std::clamp(frameDt, 0.0, 0.25);
  accumulator_ += frameDt;
  while (accumulator_ >= kFixedStep) {
    time_ += kFixedStep;
    events_.fire("PreSimulation", kFixedStep);
    if (scheduler_) scheduler_->fireEvent("PreSimulation", kFixedStep);
    events_.fire("Stepped", kFixedStep);
    if (scheduler_) scheduler_->fireEvent("Stepped", kFixedStep);
    physics_.step(kFixedStep);
    registry_.flushTransforms();
    events_.fire("Heartbeat", kFixedStep);
    if (scheduler_) {
      scheduler_->tick(time_);
      scheduler_->fireEvent("Heartbeat", kFixedStep);
    }
    accumulator_ -= kFixedStep;
  }
  const auto render = std::clamp(renderDt, 0.0, 0.25);
  events_.fire("RenderStepped", render);
  if (scheduler_) scheduler_->fireEvent("RenderStepped", render);
}
}  // namespace blubox::engine
