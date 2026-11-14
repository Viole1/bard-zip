#include "io/prefetch_stream.hpp"

#include <stdexcept>

namespace bardzip {

PrefetchStreambuf::PrefetchStreambuf(
    std::istream& source,
    std::size_t block_size,
    std::size_t queue_blocks
)
    : source_(source),
      block_size_(block_size),
      queue_blocks_(queue_blocks)
{
    if (block_size_ == 0 || queue_blocks_ == 0) {
        throw std::invalid_argument("Prefetch block size and queue length must be positive");
    }

    thread_ = std::thread([this] { run(); });
}

PrefetchStreambuf::~PrefetchStreambuf() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }

    space_available_.notify_all();
    thread_.join();
}

void PrefetchStreambuf::run() {
    try {
        while (true) {
            std::vector<char> block(block_size_);

            source_.read(block.data(), static_cast<std::streamsize>(block.size()));

            const bool failed = source_.bad() || (source_.fail() && !source_.eof());

            block.resize(static_cast<std::size_t>(source_.gcount()));
            const bool last = source_.eof() || block.empty() || failed;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                space_available_.wait(lock, [this] {
                    return stop_ || ready_.size() < queue_blocks_;
                });

                if (stop_) {
                    return;
                }

                if (!block.empty()) {
                    ready_.push_back(std::move(block));
                }

                if (last) {
                    finished_ = true;
                }

                // Set together with finished_, so the reader never sees a clean
                // end of data when the source actually failed.
                if (failed) {
                    error_ = std::make_exception_ptr(
                        std::runtime_error("Failed to read input file"));
                }
            }

            block_ready_.notify_one();

            if (last) {
                return;
            }
        }
    }
    catch (...) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            error_ = std::current_exception();
            finished_ = true;
        }

        block_ready_.notify_one();
    }
}

PrefetchStreambuf::int_type PrefetchStreambuf::underflow() {
    if (gptr() != nullptr && gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        block_ready_.wait(lock, [this] { return !ready_.empty() || finished_; });

        if (ready_.empty()) {
            if (error_) {
                std::rethrow_exception(error_);
            }

            return traits_type::eof();
        }

        current_ = std::move(ready_.front());
        ready_.pop_front();
    }

    space_available_.notify_one();

    setg(current_.data(), current_.data(), current_.data() + current_.size());
    return traits_type::to_int_type(*gptr());
}

}
