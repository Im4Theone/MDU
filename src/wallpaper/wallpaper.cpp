#include "wallpaper/wallpaper.h"

#include "utilities/file_io.h"
#include "utilities/log.h"
#include "utilities/strings.h"
#include "wallpaper/adapt.h"
#include "windows/image_io.h"

#include <nlohmann/json.hpp>
#include <shobjidl.h>
#include <windows.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <format>

using Microsoft::WRL::ComPtr;

namespace mdu::wallpaper {

namespace {

// Bump when the adaptation algorithm changes so cached images are regenerated.
constexpr int algorithmVersion = 1;

class CoTaskString {
public:
    CoTaskString() = default;
    ~CoTaskString() { CoTaskMemFree(value_); }
    CoTaskString(const CoTaskString&) = delete;
    CoTaskString& operator=(const CoTaskString&) = delete;
    LPWSTR* put() { return &value_; }
    std::wstring str() const { return value_ ? value_ : L""; }

private:
    LPWSTR value_ = nullptr;
};

std::uint64_t fnv1a(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hexColor(ThemeColor color)
{
    return std::format("#{:02x}{:02x}{:02x}", color.r, color.g, color.b);
}

COLORREF toColorRef(ThemeColor color)
{
    return RGB(color.r, color.g, color.b);
}

ComPtr<IDesktopWallpaper> desktopWallpaper()
{
    ComPtr<IDesktopWallpaper> wallpaper;
    CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper));
    return wallpaper;
}

}  // namespace

Manager::Manager(std::filesystem::path cacheDirectory, std::filesystem::path restoreFile, IWICImagingFactory* factory)
    : cacheDirectory_(std::move(cacheDirectory)), restoreFile_(std::move(restoreFile)), factory_(factory)
{
}

std::optional<Manager::Prepared> Manager::prepare(const Request& request, std::string& error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(request.image, ec);
    const auto modified = std::filesystem::last_write_time(request.image, ec);
    if (ec) {
        error = "wallpaper image not found: " + toUtf8(request.image.wstring());
        return std::nullopt;
    }

    const auto key = std::format("{}|{}|{}|{}|{}|{}", toUtf8(request.image.wstring()),
                                 modified.time_since_epoch().count(), size,
                                 request.background ? hexColor(*request.background) : "auto",
                                 std::lround(request.feather * 10000), algorithmVersion);
    const auto stem = std::format("wallpaper-{:016x}", fnv1a(key));
    const auto imagePath = cacheDirectory_ / (stem + ".png");
    const auto infoPath = cacheDirectory_ / (stem + ".json");

    // Reuse a previous adaptation if it exists.
    if (std::filesystem::exists(imagePath, ec)) {
        if (const auto info = readTextFile(infoPath, 4096)) {
            const auto json = nlohmann::json::parse(*info, nullptr, false);
            if (json.is_object() && json.contains("background") && json["background"].is_string()) {
                if (const auto color = parseHexColor(json["background"].get<std::string>())) {
                    return Prepared{imagePath, *color};
                }
            }
        }
    }

    auto image = loadImage(factory_, request.image, error);
    if (!image) {
        return std::nullopt;
    }
    const ThemeColor background = request.background.value_or(sampleBorderColor(*image));
    featherEdges(*image, background, request.feather);

    std::filesystem::create_directories(cacheDirectory_, ec);
    // Only one adaptation is kept.
    for (const auto& entry : std::filesystem::directory_iterator(cacheDirectory_, ec)) {
        if (entry.path().filename().wstring().starts_with(L"wallpaper-")) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    auto temporary = imagePath;
    temporary += L".tmp";
    if (!savePng(factory_, *image, temporary, error)) {
        std::filesystem::remove(temporary, ec);
        return std::nullopt;
    }
    std::filesystem::rename(temporary, imagePath, ec);
    if (ec) {
        error = "cannot store adapted wallpaper: " + ec.message();
        return std::nullopt;
    }
    std::string ignored;
    writeFileAtomically(infoPath, nlohmann::json{{"background", hexColor(background)}}.dump(), ignored);
    log::info("adapted wallpaper written to " + toUtf8(imagePath.wstring()));
    return Prepared{imagePath, background};
}

bool Manager::saveCurrentWallpaper(std::string& error)
{
    std::error_code ec;
    if (std::filesystem::exists(restoreFile_, ec)) {
        return true;  // keep the user's original, never MDU's own wallpaper
    }

    const auto wallpaper = desktopWallpaper();
    if (!wallpaper) {
        error = "IDesktopWallpaper is not available";
        return false;
    }

    nlohmann::json monitors = nlohmann::json::array();
    UINT count = 0;
    wallpaper->GetMonitorDevicePathCount(&count);
    for (UINT i = 0; i < count; ++i) {
        CoTaskString id;
        CoTaskString path;
        if (SUCCEEDED(wallpaper->GetMonitorDevicePathAt(i, id.put())) &&
            SUCCEEDED(wallpaper->GetWallpaper(id.str().c_str(), path.put()))) {
            monitors.push_back({{"monitor", toUtf8(id.str())}, {"wallpaper", toUtf8(path.str())}});
        }
    }

    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    COLORREF color = 0;
    wallpaper->GetPosition(&position);
    wallpaper->GetBackgroundColor(&color);

    const nlohmann::json state = {
        {"position", static_cast<int>(position)},
        {"background", static_cast<std::uint32_t>(color)},
        {"monitors", monitors},
    };
    return writeFileAtomically(restoreFile_, state.dump(2), error);
}

bool Manager::apply(const Request& request, std::string& error)
{
    const auto prepared = prepare(request, error);
    if (!prepared) {
        return false;
    }

    const auto wallpaper = desktopWallpaper();
    if (!wallpaper) {
        error = "IDesktopWallpaper is not available";
        return false;
    }

    // Avoid a visible refresh when everything is already in place.
    DESKTOP_WALLPAPER_POSITION position{};
    COLORREF color = 0;
    bool upToDate = SUCCEEDED(wallpaper->GetPosition(&position)) && position == DWPOS_FIT &&
                    SUCCEEDED(wallpaper->GetBackgroundColor(&color)) && color == toColorRef(prepared->background);
    UINT count = 0;
    wallpaper->GetMonitorDevicePathCount(&count);
    for (UINT i = 0; upToDate && i < count; ++i) {
        CoTaskString id;
        CoTaskString path;
        upToDate = SUCCEEDED(wallpaper->GetMonitorDevicePathAt(i, id.put())) &&
                   SUCCEEDED(wallpaper->GetWallpaper(id.str().c_str(), path.put())) &&
                   _wcsicmp(path.str().c_str(), prepared->image.c_str()) == 0;
    }
    if (upToDate) {
        return true;
    }

    if (!saveCurrentWallpaper(error)) {
        return false;  // never replace a wallpaper we could not remember
    }

    // "Fit" shows the whole portrait image at full height; the remaining space
    // is the background colour, which matches the feathered image edges.
    if (FAILED(wallpaper->SetBackgroundColor(toColorRef(prepared->background))) ||
        FAILED(wallpaper->SetPosition(DWPOS_FIT)) ||
        FAILED(wallpaper->SetWallpaper(nullptr, prepared->image.c_str()))) {
        error = "Windows rejected the wallpaper change";
        return false;
    }
    return true;
}

bool Manager::hasRestoreState() const
{
    std::error_code ec;
    return std::filesystem::exists(restoreFile_, ec);
}

std::wstring Manager::currentWallpaper()
{
    const auto wallpaper = desktopWallpaper();
    CoTaskString id;
    CoTaskString path;
    if (wallpaper && SUCCEEDED(wallpaper->GetMonitorDevicePathAt(0, id.put())) &&
        SUCCEEDED(wallpaper->GetWallpaper(id.str().c_str(), path.put()))) {
        return path.str();
    }
    return {};
}

bool Manager::restore(std::string& error)
{
    const auto text = readTextFile(restoreFile_, 256 * 1024);
    if (!text) {
        return true;  // nothing to restore
    }

    const auto state = nlohmann::json::parse(*text, nullptr, false);
    const auto wallpaper = desktopWallpaper();
    if (!wallpaper) {
        error = "IDesktopWallpaper is not available";
        return false;
    }

    if (state.is_object()) {
        if (const auto it = state.find("background"); it != state.end() && it->is_number_unsigned()) {
            wallpaper->SetBackgroundColor(static_cast<COLORREF>(it->get<std::uint32_t>()));
        }
        if (const auto it = state.find("position"); it != state.end() && it->is_number_integer()) {
            const int value = it->get<int>();
            if (value >= DWPOS_CENTER && value <= DWPOS_SPAN) {
                wallpaper->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(value));
            }
        }
        if (const auto it = state.find("monitors"); it != state.end() && it->is_array()) {
            for (const auto& entry : *it) {
                if (!entry.is_object() || !entry.contains("monitor") || !entry.contains("wallpaper") ||
                    !entry["monitor"].is_string() || !entry["wallpaper"].is_string()) {
                    continue;
                }
                const auto monitor = toWide(entry["monitor"].get<std::string>());
                const auto path = toWide(entry["wallpaper"].get<std::string>());
                // An empty path means a solid colour background.
                std::error_code ec;
                if (path.empty() || std::filesystem::exists(path, ec)) {
                    wallpaper->SetWallpaper(monitor.c_str(), path.c_str());
                }
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(restoreFile_, ec);
    return true;
}

}  // namespace mdu::wallpaper
