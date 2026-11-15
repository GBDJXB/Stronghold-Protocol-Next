#pragma once
#include "stronghold/capture.hpp"

namespace stronghold {
// Historical archive data only: source QPC timestamps are not current-process freshness.
struct LoadedCapture {
    std::string id;
    fs::path directory;
    Capture capture;
    std::string metadata_json; // Exact original provenance, including archive-time metrics.
};
LoadedCapture load_capture_archive(const fs::path& directory);
}
