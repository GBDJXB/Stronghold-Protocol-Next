#pragma once
#include "stronghold/capture.hpp"

namespace stronghold {
// Private executable/worker entry points; never part of the strategy-facing interface.
Frame acquire_isolated(const Window& window, const std::string& method,
                       const fs::path& executable,
                       std::chrono::milliseconds timeout = std::chrono::seconds(10));
Frame capture_wgc(const Window& expected, std::chrono::milliseconds timeout);
int worker_main(const fs::path& request, const fs::path& response);
void check_wgc_support();
struct ScreenRect { std::int32_t left, top, right, bottom; };
bool rectangle_covered(const ScreenRect& target, std::span<const ScreenRect> monitors);
bool rectangles_overlap(const ScreenRect& a, const ScreenRect& b);
Frame frame_from_bgra_rows(std::int32_t width, std::int32_t height, std::span<const std::uint8_t> pixels,
                          std::uint32_t row_pitch, std::int32_t crop_x, std::int32_t crop_y);
int run_tests(const fs::path& executable);
void test_worker_supervision(const fs::path& executable);
}
