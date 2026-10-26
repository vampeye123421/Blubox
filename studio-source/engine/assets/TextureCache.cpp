#include "TextureCache.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace blubox::engine::assets {
namespace {
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::string HexHash(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(16, '0');
    for (int i = 15; i >= 0; --i) { output[static_cast<std::size_t>(i)] = digits[value & 0xfu]; value >>= 4; }
    return output;
}
}

TextureCache::TextureCache(std::size_t maximumFileBytes, int maximumDimension)
    : maximumFileBytes_(maximumFileBytes), maximumDimension_(maximumDimension),
      worker_([this](std::stop_token stop) { worker(stop); }) {}

TextureCache::~TextureCache() {
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    // Texture destruction requires a live graphics context. Call clear() explicitly
    // before closing Raylib; this fallback only frees CPU-side Images.
    for (auto& completion : completed_) if (completion.image.data) UnloadImage(completion.image);
    for (auto& [id, entry] : entries_) { (void)id; if (entry.decoded.data) UnloadImage(entry.decoded); }
}

TextureId TextureCache::pathId(const std::filesystem::path& path) noexcept {
    const std::string key = path.generic_string();
    std::uint64_t hash = kFnvOffset;
    for (unsigned char ch : key) { hash ^= ch; hash *= kFnvPrime; }
    return hash ? hash : 1;
}

TextureId TextureCache::acquire(const std::filesystem::path& input, std::string* error) {
    std::error_code ec;
    const auto path = std::filesystem::weakly_canonical(input, ec);
    if (ec || !std::filesystem::is_regular_file(path, ec)) {
        if (error) *error = "Texture path is not a readable regular file.";
        return 0;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > maximumFileBytes_) {
        if (error) *error = "Texture file is empty or exceeds the configured size limit.";
        return 0;
    }
    const TextureId id = pathId(path);
    {
        std::lock_guard lock(mutex_);
        auto found = entries_.find(id);
        if (found != entries_.end()) {
            if (found->second.path != path) {
                if (error) *error = "Texture asset id collision.";
                return 0;
            }
            ++found->second.references;
            return id;
        }
        entries_.emplace(id, Entry{path});
        queue_.push_back(id);
    }
    wake_.notify_one();
    if (error) error->clear();
    return id;
}

bool TextureCache::retain(TextureId id) {
    std::lock_guard lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    ++found->second.references;
    return true;
}

void TextureCache::release(TextureId id) {
    std::lock_guard lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end() || found->second.references == 0) return;
    if (--found->second.references != 0) return;
    if (found->second.decoded.data) UnloadImage(found->second.decoded);
    if (!found->second.contentHash.empty()) pendingTextureReleases_.push_back(found->second.contentHash);
    entries_.erase(found);
}

std::string TextureCache::hashFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { error = "Could not read texture file."; return {}; }
    std::uint64_t hash = kFnvOffset;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        for (std::streamsize i = 0; i < count; ++i) { hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]); hash *= kFnvPrime; }
    }
    if (!stream.eof()) { error = "Texture file read failed."; return {}; }
    return HexHash(hash);
}

void TextureCache::worker(std::stop_token stop) {
    while (!stop.stop_requested()) {
        TextureId id = 0;
        std::filesystem::path path;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [&] { return !queue_.empty(); });
            if (stop.stop_requested()) break;
            id = queue_.front(); queue_.erase(queue_.begin());
            auto found = entries_.find(id);
            if (found == entries_.end()) continue;
            found->second.state = State::Decoding;
            path = found->second.path;
        }
        std::string error;
        const std::string hash = hashFile(path, error);
        Image image{};
        if (error.empty()) image = LoadImage(path.string().c_str());
        if (error.empty() && (!image.data || image.width <= 0 || image.height <= 0 ||
                              image.width > maximumDimension_ || image.height > maximumDimension_)) {
            if (image.data) UnloadImage(image);
            image = {};
            error = "Unsupported, corrupt, or oversized texture image.";
        }
        std::lock_guard lock(mutex_);
        completed_.push_back({id, image, hash, std::move(error)});
    }
}

void TextureCache::pumpGpuUploads(std::size_t maximumUploads) {
    std::lock_guard lock(mutex_);
    for (const std::string& hash : pendingTextureReleases_) {
        auto storage = texturesByHash_.find(hash);
        if (storage == texturesByHash_.end()) continue;
        if (storage->second.entryReferences > 0) --storage->second.entryReferences;
        if (storage->second.entryReferences == 0) {
            if (storage->second.texture.id) UnloadTexture(storage->second.texture);
            texturesByHash_.erase(storage);
        }
    }
    pendingTextureReleases_.clear();
    std::size_t uploads = 0;
    for (auto& completion : completed_) {
        auto found = entries_.find(completion.id);
        if (found == entries_.end()) { if (completion.image.data) UnloadImage(completion.image); continue; }
        Entry& entry = found->second;
        if (!completion.error.empty()) { entry.state = State::Failed; entry.error = std::move(completion.error); continue; }
        entry.contentHash = completion.hash;
        auto shared = texturesByHash_.find(entry.contentHash);
        if (shared != texturesByHash_.end()) {
            ++shared->second.entryReferences;
            entry.sharedTextureId = completion.id;
            entry.state = State::Ready;
            if (completion.image.data) UnloadImage(completion.image);
            continue;
        }
        if (uploads >= maximumUploads) { entry.state = State::UploadPending; entry.decoded = completion.image; continue; }
        Texture2D texture = LoadTextureFromImage(completion.image);
        UnloadImage(completion.image);
        if (texture.id == 0) { entry.state = State::Failed; entry.error = "Raylib GPU texture upload failed."; continue; }
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        texturesByHash_.emplace(entry.contentHash, SharedTexture{texture, 1});
        entry.sharedTextureId = completion.id;
        entry.state = State::Ready;
        ++uploads;
    }
    completed_.clear();
    for (auto& [id, entry] : entries_) {
        if (uploads >= maximumUploads || entry.state != State::UploadPending || !entry.decoded.data) continue;
        auto shared = texturesByHash_.find(entry.contentHash);
        if (shared != texturesByHash_.end()) {
            ++shared->second.entryReferences;
            entry.sharedTextureId = id;
            entry.state = State::Ready;
            UnloadImage(entry.decoded);
            continue;
        }
        Texture2D texture = LoadTextureFromImage(entry.decoded);
        UnloadImage(entry.decoded);
        entry.decoded = {};
        if (texture.id == 0) { entry.state = State::Failed; entry.error = "Raylib GPU texture upload failed."; continue; }
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        texturesByHash_.emplace(entry.contentHash, SharedTexture{texture, 1});
        entry.sharedTextureId = id;
        entry.state = State::Ready;
        ++uploads;
    }
}

Texture2D TextureCache::texture(TextureId id) const {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end() || found->second.state != State::Ready) return {};
    const auto storage = texturesByHash_.find(found->second.contentHash);
    return storage == texturesByHash_.end() ? Texture2D{} : storage->second.texture;
}

std::filesystem::path TextureCache::sourcePath(TextureId id) const {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    return found == entries_.end() ? std::filesystem::path{} : found->second.path;
}

TextureCache::Status TextureCache::status(TextureId id) const {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return {};
    float progress = found->second.state == State::Ready ? 1.0f :
                     found->second.state == State::Failed ? 1.0f :
                     found->second.state == State::UploadPending ? 0.9f :
                     found->second.state == State::Decoding ? 0.4f : 0.1f;
    return {found->second.state, progress, found->second.error};
}

std::uint32_t TextureCache::referenceCount(TextureId id) const {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    return found == entries_.end() ? 0 : found->second.references;
}

void TextureCache::clear() {
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    for (auto& completion : completed_) if (completion.image.data) UnloadImage(completion.image);
    completed_.clear();
    for (auto& [id, entry] : entries_) { (void)id; if (entry.decoded.data) UnloadImage(entry.decoded); }
    entries_.clear(); queue_.clear();
    for (auto& [hash, storage] : texturesByHash_) { (void)hash; if (storage.texture.id) UnloadTexture(storage.texture); }
    texturesByHash_.clear(); pendingTextureReleases_.clear();
    worker_ = std::jthread([this](std::stop_token stop) { worker(stop); });
}

} // namespace blubox::engine::assets
