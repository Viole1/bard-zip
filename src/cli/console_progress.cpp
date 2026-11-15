#include "cli/console_progress.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>

#include "core/size_format.hpp"

namespace bardzip {

std::string format_duration(double seconds) {
    const auto total = static_cast<std::uint64_t>(std::llround((std::max)(seconds, 0.0)));

    if (total < 60) {
        return std::to_string(total) + " s";
    }

    if (total < 3600) {
        return std::to_string(total / 60) + " min " + std::to_string(total % 60) + " s";
    }

    return std::to_string(total / 3600) + " h " + std::to_string(total % 3600 / 60) + " min";
}

std::string format_progress_line(
    std::string_view stage,
    std::uint64_t done,
    std::uint64_t total,
    std::uint64_t produced,
    double stage_seconds
) {
    std::string line(stage);

    if (total == 0) {
        line += "  " + format_size(done);
        return line;
    }

    done = (std::min)(done, total);
    const auto percent = done * 100 / total;

    line += "  " + std::to_string(percent) + "%";

    if (produced != 0) {
        line += "  " + format_size(produced) + " written";
    }

    if (percent >= 1 && done < total && stage_seconds >= 1.0) {
        const auto remaining = stage_seconds * static_cast<double>(total - done)
            / static_cast<double>(done);
        line += "  ~" + format_duration(remaining) + " left";
    }

    return line;
}

ConsoleProgress::ConsoleProgress(
    std::ostream& out,
    std::chrono::milliseconds interval,
    std::function<Clock::time_point()> now
)
    : out_(out), interval_(interval), now_(std::move(now))
{
}

void ConsoleProgress::begin(std::string_view stage, std::uint64_t total) {
    erase();

    stage_ = std::string(stage);
    total_ = total;
    stage_start_ = now_();
    last_draw_ = stage_start_ - interval_;  // the first update is drawn at once
    active_ = true;
}

void ConsoleProgress::advance(std::uint64_t done, std::uint64_t produced) {
    if (!active_) {
        return;
    }

    const auto now = now_();

    if (now - last_draw_ < interval_) {
        return;
    }

    last_draw_ = now;
    draw(done, produced);
}

void ConsoleProgress::end() {
    active_ = false;
    erase();
}

void ConsoleProgress::draw(std::uint64_t done, std::uint64_t produced) {
    const auto seconds = std::chrono::duration<double>(now_() - stage_start_).count();
    auto line = format_progress_line(stage_, done, total_, produced, seconds);

    const auto width = line.size();

    // Pad with spaces so a shorter line fully covers the previous, longer one.
    if (width < drawn_width_) {
        line.append(drawn_width_ - width, ' ');
    }

    out_ << '\r' << line << std::flush;
    drawn_width_ = (std::max)(drawn_width_, width);
}

void ConsoleProgress::erase() {
    if (drawn_width_ != 0) {
        out_ << '\r' << std::string(drawn_width_, ' ') << '\r' << std::flush;
        drawn_width_ = 0;
    }
}

}
