#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <istream>
#include <mutex>
#include <streambuf>
#include <thread>
#include <vector>

namespace bardzip {

// A stream buffer that reads ahead from another stream in a background thread,
// so reading a file overlaps with processing what was read before. At most
// `queue_blocks` blocks are buffered, so memory use stays bounded.
//
// Use it by wrapping the source: `std::istream in(&prefetch)`. The source
// stream must outlive this object and must not be touched by anyone else while
// it exists. The destructor stops and joins the thread (also after an error).
class PrefetchStreambuf : public std::streambuf {
public:
    explicit PrefetchStreambuf(
        std::istream& source,
        std::size_t block_size = 256 * 1024,
        std::size_t queue_blocks = 4
    );

    ~PrefetchStreambuf() override;

    PrefetchStreambuf(const PrefetchStreambuf&) = delete;
    PrefetchStreambuf& operator=(const PrefetchStreambuf&) = delete;

protected:
    int_type underflow() override;

private:
    void run();

    std::istream& source_;
    const std::size_t block_size_;
    const std::size_t queue_blocks_;

    std::mutex mutex_;
    std::condition_variable block_ready_;
    std::condition_variable space_available_;
    std::deque<std::vector<char>> ready_;
    bool finished_ = false;
    bool stop_ = false;
    std::exception_ptr error_;

    std::vector<char> current_;
    std::thread thread_;
};

}
