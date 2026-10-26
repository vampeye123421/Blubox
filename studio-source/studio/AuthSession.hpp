#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace blubox::studio {

class SessionStore {
public:
    static std::filesystem::path Path();
    static bool Save(std::string_view token, std::string& error);
    static std::optional<std::string> Load(std::string& error);
    static bool Clear(std::string& error);
    static std::optional<std::filesystem::file_time_type> ModifiedTime();
    static bool ConsumeUpdateSignal();
};

// Returns true only for a valid blubox-studio://auth?token=... URL.
// An empty error means the argument is not a Studio authentication URL.
bool ParseStudioAuthUrl(std::string_view argument, std::string& token, std::string& error);
bool CreateStudioHandoffId(std::string& handoffId, std::string& error);
bool OpenStudioLoginPage(std::string_view handoffId, std::string& error);

// Keeps the per-user singleton mutex alive for the lifetime of the process.
class StudioInstanceLock {
public:
    StudioInstanceLock();
    ~StudioInstanceLock();
    StudioInstanceLock(const StudioInstanceLock&) = delete;
    StudioInstanceLock& operator=(const StudioInstanceLock&) = delete;
    bool IsPrimary() const { return primary_; }

private:
    void* handle_ = nullptr;
    bool primary_ = false;
};

} // namespace blubox::studio
