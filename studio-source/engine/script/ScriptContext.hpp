#pragma once
#include <cstdint>
namespace blubox::engine::script {
enum class ScriptContext : std::uint8_t { Server, Client, Shared };
}
