#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>

#include "core/progress.hpp"

namespace bardzip {

// The text of one progress line, without the carriage return:
//   "Writing poem  42%  1.2 GiB written  ~40 s left"
// With an unknown total only the amount done is shown. The remaining time is
// shown once at least 1 % is done and the stage has run for a second.
std::string format_progress_line(
    std::string_view stage,
    std::uint64_t done,
    std::uint64_t total,
    std::uint64_t produced,
    double stage_seconds
);

std::string format_duration(double seconds);

// Draws a single, constantly rewritten line ("\r") on a stream - meant for the
// console. Updates are throttled so printing never slows the work down; the
// line is erased when a stage ends, so it never mixes with other output.
class ConsoleProgress : public ProgressObserver {
public:
    using Clock = std::chrono::steady_clock;

    ConsoleProgress(
        std::ostream& out,
        std::chrono::milliseconds interval = std::chrono::milliseconds(100),
        std::function<Clock::time_point()> now = [] { return Clock::now(); }
    );

    void begin(std::string_view stage, std::uint64_t total) override;
    void advance(std::uint64_t done, std::uint64_t produced) override;
    void end() override;

private:
    void draw(std::uint64_t done, std::uint64_t produced);
    void erase();

    std::ostream& out_;
    std::chrono::milliseconds interval_;
    std::function<Clock::time_point()> now_;

    std::string stage_;
    std::uint64_t total_ = 0;
    Clock::time_point stage_start_;
    Clock::time_point last_draw_;
    std::size_t drawn_width_ = 0;
    bool active_ = false;
};

}
