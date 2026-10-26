#pragma once

#include "AssetId.hpp"

#include <raylib.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace blubox::engine::assets {

class TextureCache {
public:
    enum class State : std::uint8_t { Queued, Decoding, UploadPending, Ready, Failed };
    struct Status { State state = State::Failed; float progress = 0.0f; std::string error; };

    explicit TextureCache(std::size_t maximumFileBytes = 128u * 1024u * 1024u,
                          int maximumDimension = 16384);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // acquire() queues CPU decode and returns immediately. GPU upload is performed
    // only by pumpGpuUploads(), which must be called on the render/context thread.
    TextureId acquire(const std::filesystem::path& path, std::string* error = nullptr);
    bool retain(TextureId id);
    void release(TextureId id);
    void pumpGpuUploads(std::size_t maximumUploads = 4);
    Texture2D texture(TextureId id) const;
    std::filesystem::path sourcePath(TextureId id) const;
    Status status(TextureId id) const;
    std::uint32_t referenceCount(TextureId id) const;
    void clear(); // call on the thread that owns the Raylib graphics context

private:
    struct Entry {
        std::filesystem::path path;
        std::string contentHash;
        std::uint32_t references = 1;
        State state = State::Queued;
        Image decoded{};
        TextureId sharedTextureId = 0;
        std::string error;
    };
    struct Completion { TextureId id = 0; Image image{}; std::string hash; std::string error; };
    struct SharedTexture { Texture2D texture{}; std::uint32_t entryReferences = 0; };

    void worker(std::stop_token stop);
    static std::string hashFile(const std::filesystem::path& path, std::string& error);
    static TextureId pathId(const std::filesystem::path& path) noexcept;

    const std::size_t maximumFileBytes_;
    const int maximumDimension_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::unordered_map<TextureId, Entry> entries_;
    std::unordered_map<std::string, SharedTexture> texturesByHash_;
    std::vector<TextureId> queue_;
    std::vector<Completion> completed_;
    std::vector<std::string> pendingTextureReleases_;
    std::jthread worker_;
};

} // namespace blubox::engine::assets
