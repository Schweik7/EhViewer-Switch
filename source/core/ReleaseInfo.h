#pragma once

#include <cstdint>
#include <string>

namespace ehviewer {

// Where updates come from. Both sources answer with the shape of GitHub's
// /releases/latest response, so one parser serves both (same design as the
// MusicPlayer2 Switch port).
constexpr const char* kUpdateMirrorUrl = "https://download.psyventures.cn/ehviewer/latest.json";
constexpr const char* kUpdateGithubUrl = "https://api.github.com/repos/Schweik7/EhViewer-Switch/releases/latest";
constexpr const char* kUpdateAssetName = "EhViewerSwitch.nro";

struct ReleaseInfo {
    std::string tag;        // e.g. "v0.5.3"
    std::string notes;      // release body
    std::string asset_url;  // download URL of the NRO; empty if missing
    std::uint64_t asset_size = 0;
};

// Parses a /releases/latest response and picks the asset named asset_name.
// A release without that asset is not an error (asset_url stays empty);
// GitHub's {"message": ...} errors and missing tag_name are.
bool ParseReleaseInfo(const std::string& json_text, const std::string& asset_name, ReleaseInfo* out,
                      std::string* error = nullptr);

// "v0.5.10" > "0.5.9"; anything after the numeric part ("-beta") is ignored.
bool IsNewerVersion(const std::string& remote, const std::string& local);

// An NRO has "NRO0" at offset 0x10; guards against installing an HTML error page.
bool LooksLikeNro(const std::string& bytes);

}  // namespace ehviewer
