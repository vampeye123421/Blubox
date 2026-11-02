#include "PlayModeBridge.hpp"
#include "SpawnService.hpp"
#include "../runtime/ClientRuntime.hpp"
#include "../runtime/ServerRuntime.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cerrno>
#include <cstdint>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
extern char** environ;
#endif
#if defined(_WIN32)
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#include <windows.h>
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

namespace blubox::engine::play {
namespace {
std::string makeCredential() {
  std::random_device random;
  constexpr char digits[] = "0123456789abcdef";
  std::string value(64, '0');
  for (char& ch : value) ch = digits[random() & 15u];
  return value;
}
#if defined(_WIN32) || defined(__APPLE__)
#ifdef _WIN32
std::wstring quoteArg(std::wstring_view value) {
  std::wstring out=L"\"";
  for (wchar_t c : value) { if (c==L'\"') out += L'\\'; out += c; }
  out += L'\"'; return out;
}
std::wstring utf8Wide(std::string_view value) {
  if (value.empty()) return {};
  const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
  if(count<=0) return std::wstring(value.begin(),value.end());
  std::wstring result(static_cast<std::size_t>(count),L'\0');
  MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count); return result;
}
#endif
std::filesystem::path clientExecutable() {
#ifdef _WIN32
  wchar_t overridePath[32768]{}; const DWORD length=GetEnvironmentVariableW(L"BLUBOX_CLIENT_EXE",overridePath,32768);
  if(length>0 && length<32768 && std::filesystem::exists(overridePath)) return overridePath;
  // A client shipped beside Studio is the version paired with this editor.
  // Prefer it over an unrelated developer checkout so Studio playtests do
  // not silently launch a stale client with incompatible gameplay behavior.
  wchar_t module[32768]{}; const DWORD moduleLength=GetModuleFileNameW(nullptr,module,32768);
  if(moduleLength>0 && moduleLength<32768) {
    const auto beside=std::filesystem::path(module).parent_path()/L"blubox_client.exe";
    if(std::filesystem::exists(beside)) return beside;
  }
  // Prefer the developer build when it exists so the in-process Studio
  // server can use the --studio-playtest protocol without requiring a
  // published client update first.
  const std::filesystem::path dev=L"D:\\blubox-client\\build\\blubox_client.exe";
  if(std::filesystem::exists(dev)) return dev;
  wchar_t local[32768]{}; const DWORD localLength=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
  if(localLength>0 && localLength<32768) {
    const auto versions=std::filesystem::path(local)/L"Blubox"/L"versions";
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    if(std::filesystem::exists(versions,ec)) for(const auto& entry:std::filesystem::directory_iterator(versions,ec)) {
      auto candidate=entry.path()/L"blubox_client.exe";
      if(entry.is_directory(ec) && std::filesystem::exists(candidate,ec)) candidates.push_back(candidate);
    }
    std::sort(candidates.begin(),candidates.end(),std::greater<>());
    if(!candidates.empty()) return candidates.front();
  }
  return {};
#else
  const char* overridePath = std::getenv("BLUBOX_CLIENT_EXE");
  if (overridePath != nullptr && *overridePath != '\0' && std::filesystem::exists(overridePath)) return overridePath;
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string current(size, '\0');
  if (_NSGetExecutablePath(current.data(), &size) == 0) {
    std::error_code error;
    const auto executable = std::filesystem::canonical(current.c_str(), error);
    if (!error) {
      const auto beside = executable.parent_path() / "blubox_client";
      if (std::filesystem::exists(beside)) return beside;
    }
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') return {};
  const auto versions = std::filesystem::path(home) / "Library" / "Application Support" /
      "Blubox" / "versions";
  std::vector<std::filesystem::path> candidates;
  std::error_code error;
  if (std::filesystem::exists(versions, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(versions, error)) {
      const auto candidate = entry.path() / "blubox_client";
      if (entry.is_directory(error) && std::filesystem::exists(candidate, error)) candidates.push_back(candidate);
      if (error) break;
    }
  }
  std::sort(candidates.begin(), candidates.end(), std::greater<>());
  return candidates.empty() ? std::filesystem::path{} : candidates.front();
#endif
}
bool launchClient(const std::filesystem::path& executable, std::uint16_t port,
                  std::string_view credential, const std::filesystem::path& mapFile,
                  std::string_view displayName, void*& processHandle) {
  if(executable.empty() || !std::filesystem::exists(executable)) return false;
#ifdef _WIN32
  std::wstring command=quoteArg(executable.wstring())+L" --studio-playtest \"127.0.0.1\" "+std::to_wstring(port)+L" "+
    utf8Wide(credential)+L" "+quoteArg(mapFile.wstring())+L" "+quoteArg(utf8Wide(displayName));
  STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,
                     nullptr,executable.parent_path().c_str(),&startup,&process)) return false;
  CloseHandle(process.hThread); processHandle=process.hProcess; return true;
#else
  std::vector<std::string> arguments{
      executable.string(), "--studio-playtest", "127.0.0.1", std::to_string(port),
      std::string(credential), mapFile.string(), std::string(displayName)};
  std::vector<char*> argumentPointers;
  argumentPointers.reserve(arguments.size() + 1);
  for (auto& argument : arguments) argumentPointers.push_back(argument.data());
  argumentPointers.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) return false;
  const int chdirResult = posix_spawn_file_actions_addchdir_np(&actions, executable.parent_path().c_str());
  pid_t process = 0;
  const int result = chdirResult == 0
      ? posix_spawn(&process, executable.c_str(), &actions, nullptr, argumentPointers.data(), environ)
      : chdirResult;
  posix_spawn_file_actions_destroy(&actions);
  if (result != 0) return false;
  processHandle = reinterpret_cast<void*>(static_cast<intptr_t>(process + 1));
  return true;
#endif
}
#endif
}
PlayModeBridge::PlayModeBridge(studio::MapDocument& editorWorld) : editorWorld_(editorWorld) {}
PlayModeBridge::~PlayModeBridge() { stop(); }
bool PlayModeBridge::start(const PlayConfig& config, PlayError& error) {
  if (state() != PlayModeState::Stopped) { error.message = "Play Mode is already active."; return false; }
  state_ = PlayModeState::Starting;
  config_ = config;
  paused_ = false;
  { std::lock_guard lock(errorMutex_); lastError_.clear(); }
  if (!RuntimeSnapshot::capture(editorWorld_, snapshot_, error.message)) {
    state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  const std::string credential = makeCredential();
  server_ = std::make_unique<runtime::ServerRuntime>();
  if (!server_->initialize(snapshot_, config_, credential, error.message)) {
    server_->shutdown(); server_.reset(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  if (config_.mode != PlayModeType::Run) {
    studio::MapDocument authoritativeWorld = server_->documentSnapshot();
    if (!RuntimeSnapshot::capture(authoritativeWorld, snapshot_, error.message)) {
      server_->shutdown(); server_.reset(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
    }
    if(config_.useStandaloneClient) {
#if defined(_WIN32) || defined(__APPLE__)
      try {
        std::random_device rd;
        standaloneMapDirectory_=std::filesystem::temp_directory_path()/
          ("BluboxStudioPlaytest-"+std::to_string((static_cast<std::uint64_t>(rd())<<32)|rd()));
        std::filesystem::create_directories(standaloneMapDirectory_);
        studio::MapDocument playWorld;
        if(!snapshot_.instantiateWorld(playWorld,error.message)) throw std::runtime_error(error.message);
        const auto mapFile=standaloneMapDirectory_/"playtest.blubox.json";
        std::ofstream output(mapFile,std::ios::binary|std::ios::trunc);
        const auto json=playWorld.ToJson().dump(); output.write(json.data(),static_cast<std::streamsize>(json.size())); output.close();
        if(!output) throw std::runtime_error("Could not write the temporary playtest map.");
        if(!launchClient(clientExecutable(),server_->port(),credential,mapFile,config_.playerDisplayName,standaloneProcess_))
          throw std::runtime_error("Could not launch the standalone Blubox client. Set BLUBOX_CLIENT_EXE to its executable path.");
      } catch(const std::exception& exception) {
        error.message=exception.what();
        server_->shutdown(); server_.reset();
        if(!standaloneMapDirectory_.empty()) { std::error_code ec; std::filesystem::remove_all(standaloneMapDirectory_,ec); standaloneMapDirectory_.clear(); }
        state_=PlayModeState::Failed; setFailure(error.message); return false;
      }
#endif
    } else {
      client_ = std::make_unique<runtime::ClientRuntime>();
      if (!client_->initialize(snapshot_, config_, server_->port(), credential, error.message)) {
        client_->shutdown(); client_.reset(); server_->shutdown(); server_.reset();
        state_ = PlayModeState::Failed; setFailure(error.message); return false;
      }
    }
  }
  serverStopRequested_.store(false, std::memory_order_release);
  serverThread_ = std::thread([this] { runServer(); });
  const auto deadline = std::chrono::steady_clock::now() + config_.clientConnectTimeout;
  while (client_ && config_.enableNetworking && !client_->authenticated() && std::chrono::steady_clock::now() < deadline) {
    if (state() == PlayModeState::Failed) break;
    client_->tick(0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (client_ && config_.enableNetworking && !client_->authenticated()) {
    error.message = state() == PlayModeState::Failed ? lastError() : "Local client did not complete the ENet Welcome handshake before timeout.";
    stop(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  state_ = PlayModeState::Running;
  error.message.clear();
  return true;
}
void PlayModeBridge::runServer() {
  using clock = std::chrono::steady_clock;
  const auto step = std::max(std::chrono::milliseconds(1), config_.fixedStep);
  auto next = clock::now();
  try {
    while (!serverStopRequested_.load(std::memory_order_acquire)) {
      if (!paused_.load()) server_->tick(std::chrono::duration<double>(step).count());
      next += step;
      std::this_thread::sleep_until(next);
      if (clock::now() - next > step * 4) next = clock::now();
    }
    server_->shutdown();
  } catch (const std::exception& exception) {
    setFailure(std::string("Local server runtime failed: ") + exception.what());
    server_->shutdown();
  } catch (...) {
    setFailure("Local server runtime failed with an unknown error.");
    server_->shutdown();
  }
}
void PlayModeBridge::tick(double dt) {
  if (state() != PlayModeState::Running || paused_.load()) return;
  if (client_) {
    client_->tick(dt);
    if (config_.enableNetworking && !client_->authenticated()) {
      const auto& detail = client_->networkDiagnostic();
      setFailure(detail.empty() ? "Local server connection was lost."
                                : "Local server connection was lost: " + detail);
    }
  }
#ifdef _WIN32
  else if(standaloneProcess_ && WaitForSingleObject(static_cast<HANDLE>(standaloneProcess_),0)==WAIT_OBJECT_0)
    setFailure("The standalone client closed the Studio playtest.");
#elif defined(__APPLE__)
  else if (standaloneProcess_) {
    const pid_t process = static_cast<pid_t>(reinterpret_cast<intptr_t>(standaloneProcess_) - 1);
    int status = 0;
    const pid_t result = waitpid(process, &status, WNOHANG);
    if (result == process) {
      standaloneProcess_ = nullptr;
      setFailure("The standalone client closed the Studio playtest.");
    }
  }
#endif
}
bool PlayModeBridge::sendInput(const input::InputCommand& command) { return client_ && client_->sendInput(command); }
void PlayModeBridge::stop() {
  const auto current = state();
  if (current == PlayModeState::Stopped) return;
  state_ = PlayModeState::Stopping;
  paused_ = true;
#ifdef _WIN32
  if(standaloneProcess_) {
    HANDLE process=static_cast<HANDLE>(standaloneProcess_);
    if(WaitForSingleObject(process,0)==WAIT_TIMEOUT) { TerminateProcess(process,0); WaitForSingleObject(process,1000); }
    CloseHandle(process); standaloneProcess_=nullptr;
  }
#elif defined(__APPLE__)
  if (standaloneProcess_) {
    const pid_t process = static_cast<pid_t>(reinterpret_cast<intptr_t>(standaloneProcess_) - 1);
    if (kill(process, SIGTERM) != 0 && errno != ESRCH) {
      kill(process, SIGKILL);
    }
    int status = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
      const pid_t result = waitpid(process, &status, WNOHANG);
      if (result == process || (result < 0 && errno == ECHILD)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (waitpid(process, &status, WNOHANG) == 0) {
      kill(process, SIGKILL);
      waitpid(process, &status, 0);
    }
    standaloneProcess_ = nullptr;
  }
#endif
  if (client_) {
    client_->disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    client_->shutdown();
    client_.reset();
  }
  if (serverThread_.joinable()) {
    serverStopRequested_.store(true, std::memory_order_release);
    serverThread_.join();
  } else if (server_) server_->shutdown();
  server_.reset();
  snapshot_ = {};
  if(!standaloneMapDirectory_.empty()) { std::error_code ec; std::filesystem::remove_all(standaloneMapDirectory_,ec); standaloneMapDirectory_.clear(); }
  state_ = PlayModeState::Stopped;
}
void PlayModeBridge::pause() {
  if (state() != PlayModeState::Running) return;
  paused_ = true;
  state_ = PlayModeState::Paused;
}
void PlayModeBridge::resume() {
  if (state() != PlayModeState::Paused) return;
  paused_ = false;
  state_ = PlayModeState::Running;
}
bool PlayModeBridge::clientAuthenticated() const noexcept { return client_ && client_->authenticated(); }
const studio::MapDocument* PlayModeBridge::clientWorld() const noexcept {
  return client_ ? &client_->document() : nullptr;
}
std::optional<studio::MapDocument> PlayModeBridge::serverWorldSnapshot() const {
  return server_ ? std::optional<studio::MapDocument>(server_->documentSnapshot()) : std::nullopt;
}
void PlayModeBridge::setFailure(std::string message) {
  { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); }
  state_ = PlayModeState::Failed;
}
std::string PlayModeBridge::lastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
}
