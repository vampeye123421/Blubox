#pragma once
#include "ScriptContext.hpp"
#include <string>
namespace blubox::engine::script {
struct ScriptAsset { std::string id; std::string name; std::string source; ScriptContext context = ScriptContext::Shared; };
}
