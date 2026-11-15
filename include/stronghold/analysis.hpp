#pragma once
#include "stronghold/recognition.hpp"
#include <memory>
namespace stronghold {
struct TextObservation { std::string text; double confidence = 0; };
// Loads general MAA PaddleOCR recognition model; caller supplies trusted installation root.
class NeuralTextRecognizer {
public:
    explicit NeuralTextRecognizer(const fs::path& maa_root);
    ~NeuralTextRecognizer();
    NeuralTextRecognizer(const NeuralTextRecognizer&) = delete;
    NeuralTextRecognizer& operator=(const NeuralTextRecognizer&) = delete;
    TextObservation read(const Frame& frame, int x, int y, int width, int height);
    std::string runtime_version, model_sha256, dictionary_sha256;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
struct Analysis {
    LoadedCapture source;
    std::string category = "unknown", reason;
    std::vector<std::string> evidence;
    std::string recognizer_version = "maa-text-screen-v1", runtime_version, model_sha256, dictionary_sha256;
};
Analysis analyze_capture_archive(const fs::path& directory, const fs::path& maa_root);
}
