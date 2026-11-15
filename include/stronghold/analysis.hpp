#pragma once
#include "stronghold/recognition.hpp"
#include <memory>
#include <optional>
namespace stronghold {
struct TextObservation { std::string text; double confidence = 0; };
enum class TextModel { General, Characters };
// Loads an external MAA recognition model; caller supplies trusted installation root.
class NeuralTextRecognizer {
public:
    explicit NeuralTextRecognizer(const fs::path& maa_root, TextModel model = TextModel::General);
    ~NeuralTextRecognizer();
    NeuralTextRecognizer(const NeuralTextRecognizer&) = delete;
    NeuralTextRecognizer& operator=(const NeuralTextRecognizer&) = delete;
    TextObservation read(const Frame& frame, int x, int y, int width, int height);
    std::string runtime_version, model_sha256, dictionary_sha256;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
struct NumericObservation {
    std::string status = "unknown";
    std::optional<int> value;
    double confidence = 0;
    std::string reason = "screen_not_supported", raw_text;
    std::string model_sha256, dictionary_sha256;
};
struct Analysis {
    NumericObservation funds;
    LoadedCapture source;
    std::string category = "unknown", special_view, reason;
    std::vector<std::string> evidence;
    std::string recognizer_version = "maa-text-special-v3", runtime_version, model_sha256, dictionary_sha256;
};
Analysis analyze_capture_archive(const fs::path& directory, const fs::path& maa_root);
}
