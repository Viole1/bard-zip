#pragma once

#include <cstdint>
#include <string_view>

namespace bardzip {

// Receives progress reports from long operations. The core never prints:
// whoever calls it decides how (and whether) to show them.
class ProgressObserver {
public:
    virtual ~ProgressObserver() = default;

    // A new stage starts. total is its amount of work in bytes, 0 if unknown.
    virtual void begin(std::string_view stage, std::uint64_t total) = 0;

    // The position inside the current stage (cumulative, never decreasing) and
    // the number of result bytes produced so far (0 if it is not meaningful).
    virtual void advance(std::uint64_t done, std::uint64_t produced) = 0;

    // The stage is over (also called when it ends with an error).
    virtual void end() = 0;
};

// Calls begin() on construction and end() on destruction, so a stage is closed
// on every path, including exceptions. All calls are no-ops without an observer.
class ProgressStage {
public:
    ProgressStage(ProgressObserver* observer, std::string_view stage, std::uint64_t total)
        : observer_(observer)
    {
        if (observer_ != nullptr) {
            observer_->begin(stage, total);
        }
    }

    ~ProgressStage() {
        if (observer_ != nullptr) {
            observer_->end();
        }
    }

    ProgressStage(const ProgressStage&) = delete;
    ProgressStage& operator=(const ProgressStage&) = delete;

    void advance(std::uint64_t done, std::uint64_t produced = 0) const {
        if (observer_ != nullptr) {
            observer_->advance(done, produced);
        }
    }

private:
    ProgressObserver* observer_;
};

}
