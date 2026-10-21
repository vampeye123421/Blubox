// What the desktops do each their own way: where a program keeps the user's data.

#include "core/platform.hpp"

#include "core/log.hpp"

namespace redgem::platform {
namespace {

constexpr const char* kConfigHomeVariable = "XDG_CONFIG_HOME";  // where the desktop keeps settings, when it says
constexpr const char* kConfigFolder = ".config";                // in the home folder, when it does not
constexpr const char* kUserDataFolder = "redgem";
// Where the Mac keeps what programs remember: the Library's Application Support, in the home folder.
constexpr const char* kMacLibraryFolder = "Library";
constexpr const char* kMacSupportFolder = "Application Support";
constexpr const char* kMacUserDataFolder = "RedGem";
#if defined(_WIN32)
constexpr Desktop kDesktop = Desktop::Windows;
#elif defined(__APPLE__)
constexpr Desktop kDesktop = Desktop::Mac;
#else
constexpr Desktop kDesktop = Desktop::Linux;
#endif

// The folder set_user_data_dir gave; empty until it gives one.
std::filesystem::path& given_user_data_dir() {
  static std::filesystem::path folder;
  return folder;
}

}  // namespace

Desktop this_desktop() { return kDesktop; }

bool is_windows() { return kDesktop == Desktop::Windows; }

bool is_mac() { return kDesktop == Desktop::Mac; }

bool has_posix_shell() { return !is_windows(); }

std::filesystem::path user_data_dir_on(Desktop desktop, const std::filesystem::path& home,
                                       const std::string& config_home) {
  if (desktop == Desktop::Mac) return home / kMacLibraryFolder / kMacSupportFolder / kMacUserDataFolder;
  const std::filesystem::path settings =
      config_home.empty() ? home / kConfigFolder : std::filesystem::path(config_home);
  return settings / kUserDataFolder;
}

std::filesystem::path user_data_dir() {
  if (!given_user_data_dir().empty()) return given_user_data_dir();
  return user_data_dir_on(kDesktop, home_dir(), environment(kConfigHomeVariable));
}

void set_user_data_dir(const std::filesystem::path& folder) { given_user_data_dir() = folder; }

}  // namespace redgem::platform
