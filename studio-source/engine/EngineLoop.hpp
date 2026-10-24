#pragma once
#include "EngineCore.hpp"
#include "PhysicsWorld.hpp"
#include "TaskScheduler.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace blubox::engine {
class EventBus {
 public:
  using Callback = std::function<void(double)>;
  std::uint64_t connect(std::string event, Callback callback);
  void disconnect(std::uint64_t token);
  void fire(const std::string& event, double delta);

 private:
  struct Listener {
    std::uint64_t token;
    Callback callback;
  };
  std::unordered_map<std::string, std::vector<Listener>> listeners_;
  std::uint64_t next_ = 1;
};
class EngineLoop {
 public:
  explicit EngineLoop(InstanceRegistry& registry) : registry_(registry), physics_(registry) {}
  EventBus& events() noexcept { return events_; }
  PhysicsWorld& physics() noexcept { return physics_; }
  void attachScheduler(TaskScheduler& scheduler) { scheduler_ = &scheduler; }
  void advance(double frameDelta, double renderDelta);
  double time() const noexcept { return time_; }

 private:
  InstanceRegistry& registry_;
  PhysicsWorld physics_;
  EventBus events_;
  TaskScheduler* scheduler_ = nullptr;
  double accumulator_ = 0, time_ = 0;
  static constexpr double kFixedStep = 1.0 / 60.0;
};
}  // namespace blubox::engine
