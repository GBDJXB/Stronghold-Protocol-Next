#pragma once
#include "stronghold/analysis.hpp"
namespace stronghold {
// Compact historical snapshot. Source path is never opened on reload.
struct StateRecord {
    std::string id, saved_at, observation_status = "historical";
    std::string frame_id, source_metadata_json, request_start, request_end;
    fs::path source_directory;
    std::string category, special_view, reason, recognizer_version, runtime_version, model_sha256, dictionary_sha256;
    std::vector<std::string> evidence;
    NumericObservation funds, remaining_time;
    StageObservation stage;
};
fs::path save_state_record(const Analysis& analysis, const fs::path& output);
StateRecord load_state_record(const fs::path& file);
std::string state_record_json(const StateRecord& record);
}
