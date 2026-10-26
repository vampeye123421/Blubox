#include "RuntimeSnapshot.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace blubox::engine::play {
namespace {
script::ScriptContext classify(const studio::MapDocument& map, const studio::Entity& entity,
                               const std::unordered_map<std::string, const studio::Entity*>& byId) {
  if (entity.kind == "localscript") return script::ScriptContext::Client;
  std::string parent = entity.parent;
  for (std::size_t depth = 0; depth <= map.entities.size(); ++depth) {
    if (parent == "ReplicatedStorage") return script::ScriptContext::Shared;
    if (parent == "ServerScriptService" || parent == "ServerStorage") return script::ScriptContext::Server;
    if (parent == "StarterGui" || parent == "StarterPlayer") return script::ScriptContext::Client;
    auto found = byId.find(parent);
    if (found == byId.end()) break;
    parent = found->second->parent;
  }
  return script::ScriptContext::Server;
}
}
bool RuntimeSnapshot::capture(const studio::MapDocument& document, RuntimeSnapshot& out, std::string& error) {
  try {
    document.Validate();
    RuntimeSnapshot result;
    const std::string json = document.ToJson().dump();
    result.sceneChunk.resize(json.size());
    if (!json.empty()) std::memcpy(result.sceneChunk.data(), json.data(), json.size());
    std::unordered_map<std::string, const studio::Entity*> byId;
    byId.reserve(document.entities.size());
    for (const auto& entity : document.entities) byId.emplace(entity.id, &entity);
    for (const auto& entity : document.entities) {
      if (entity.kind != "script" && entity.kind != "localscript" && entity.luauScript.empty()) continue;
      script::ScriptAsset asset{entity.id, entity.name, entity.luauScript, classify(document, entity, byId)};
      switch (asset.context) {
        case script::ScriptContext::Server: result.serverScripts.push_back(std::move(asset)); break;
        case script::ScriptContext::Client: result.clientScripts.push_back(std::move(asset)); break;
        case script::ScriptContext::Shared: result.sharedModules.push_back(std::move(asset)); break;
      }
    }
    out = std::move(result);
    error.clear();
    return true;
  } catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool RuntimeSnapshot::instantiateWorld(studio::MapDocument& out, std::string& error) const {
  try {
    const std::string json(reinterpret_cast<const char*>(sceneChunk.data()), sceneChunk.size());
    out = studio::MapDocument::FromJson(nlohmann::json::parse(json));
    error.clear();
    return true;
  } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
