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
#if defined(_WIN32)
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
std::filesystem::path clientExecutable() {
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
}
bool launchClient(const std::filesystem::path& executable, std::uint16_t port,
                  std::string_view credential, const std::filesystem::path& mapFile,
                  std::string_view displayName, void*& processHandle) {
  if(executable.empty() || !std::filesystem::exists(executable)) return false;
  std::wstring command=quoteArg(executable.wstring())+L" --studio-playtest \"127.0.0.1\" "+std::to_wstring(port)+L" "+
    utf8Wide(credential)+L" "+quoteArg(mapFile.wstring())+L" "+quoteArg(utf8Wide(displayName));
  STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,
                     nullptr,executable.parent_path().c_str(),&startup,&process)) return false;
  CloseHandle(process.hThread); processHandle=process.hProcess; return true;
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
#if defined(_WIN32)
      try {
        wchar_t temp[32768]{}; const DWORD tempLength=GetTempPathW(32768,temp);
        if(!tempLength || tempLength>=32768) throw std::runtime_error("Could not resolve the temporary directory for the playtest map.");
        std::random_device rd;
        standaloneMapDirectory_=(std::filesystem::path(temp)/(L"BluboxStudioPlaytest-"+std::to_wstring((static_cast<std::uint64_t>(rd())<<32)|rd()))).wstring();
        std::filesystem::create_directories(standaloneMapDirectory_);
        studio::MapDocument playWorld;
        if(!snapshot_.instantiateWorld(playWorld,error.message)) throw std::runtime_error(error.message);
        const auto mapFile=std::filesystem::path(standaloneMapDirectory_)/L"playtest.blubox.json";
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
#else
      error.message="Standalone Studio playtest launch is supported on Windows only.";
      server_->shutdown(); server_.reset(); state_=PlayModeState::Failed; setFailure(error.message); return false;
#endif
    } else {
      client_ = std::make_unique<runtime::ClientRuntime>();
      if (!client_->initialize(snapshot_, config_, server_->port(), credential, error.message)) {
        client_->shutdown(); client_.reset(); server_->shutdown(); server_.reset();
        state_ = PlayModeState::Failed; setFailure(error.message); return false;
      }
    }
  }
  serverThread_ = std::jthread([this](std::stop_token stopToken) { runServer(stopToken); });
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
void PlayModeBridge::runServer(std::stop_token stopToken) {
  using clock = std::chrono::steady_clock;
  const auto step = std::max(std::chrono::milliseconds(1), config_.fixedStep);
  auto next = clock::now();
  try {
    while (!stopToken.stop_requested()) {
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
#if defined(_WIN32)
  else if(standaloneProcess_ && WaitForSingleObject(static_cast<HANDLE>(standaloneProcess_),0)==WAIT_OBJECT_0)
    setFailure("The standalone client closed the Studio playtest.");
#endif
}
bool PlayModeBridge::sendInput(const input::InputCommand& command) { return client_ && client_->sendInput(command); }
void PlayModeBridge::stop() {
  const auto current = state();
  if (current == PlayModeState::Stopped) return;
  state_ = PlayModeState::Stopping;
  paused_ = true;
#if defined(_WIN32)
  if(standaloneProcess_) {
    HANDLE process=static_cast<HANDLE>(standaloneProcess_);
    if(WaitForSingleObject(process,0)==WAIT_TIMEOUT) { TerminateProcess(process,0); WaitForSingleObject(process,1000); }
    CloseHandle(process); standaloneProcess_=nullptr;
  }
#endif
  if (client_) {
    client_->disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    client_->shutdown();
    client_.reset();
  }
  if (serverThread_.joinable()) {
    serverThread_.request_stop();
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


