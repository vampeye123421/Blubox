#pragma once
#include "../script/ScriptAsset.hpp"
#include "../../studio/MapDocument.hpp"
#include <cstddef>
#include <string>
#include <vector>
namespace blubox::engine::play {
// Immutable serialized editor state. Runtime worlds are reconstructed from sceneChunk;
// neither runtime retains a reference to the editor document.
struct RuntimeSnapshot {
  std::vector<std::byte> sceneChunk;
  std::vector<script::ScriptAsset> serverScripts, clientScripts, sharedModules;
  static bool capture(const studio::MapDocument& document, RuntimeSnapshot& out, std::string& error);
  bool instantiateWorld(studio::MapDocument& out, std::string& error) const;
};
}
