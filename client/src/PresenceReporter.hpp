#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class PresenceReporter {
public:
    PresenceReporter() = default;
    ~PresenceReporter();
    PresenceReporter(const PresenceReporter &) = delete;
    PresenceReporter &operator=(const PresenceReporter &) = delete;

    void Start(std::string token);
    void Stop() noexcept;

private:
    void Run(std::stop_token stopToken);

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::string token_;
    std::jthread worker_;
};
