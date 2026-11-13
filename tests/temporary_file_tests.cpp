#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "io/file_writer.hpp"
#include "io/temporary_file.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

std::vector<std::uint8_t> sample_bytes(std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(i * 31 + 7);
    }
    return bytes;
}

}  // namespace

TEST(temporary_file_commit_publishes_content) {
    const auto dir = test::fresh_dir("tmp_commit");
    const auto destination = dir / "out.bin";
    const auto bytes = sample_bytes(1000);

    {
        TemporaryFile temporary(destination);
        temporary.write(bytes);
        temporary.commit();
    }

    CHECK(test::read_all(destination) == bytes);
    CHECK(test::count_entries(dir) == 1);  // временного файла не осталось
}

TEST(temporary_file_hidden_until_commit) {
    const auto dir = test::fresh_dir("tmp_hidden");
    const auto destination = dir / "out.bin";

    TemporaryFile temporary(destination);
    temporary.write(sample_bytes(10));

    CHECK(!std::filesystem::exists(destination));
    CHECK(test::count_entries(dir) == 1);  // только временный файл

    temporary.commit();
    CHECK(std::filesystem::exists(destination));
}

TEST(temporary_file_removed_without_commit) {
    const auto dir = test::fresh_dir("tmp_abandon");
    const auto destination = dir / "out.bin";

    {
        TemporaryFile temporary(destination);
        temporary.write(sample_bytes(10));
    }

    CHECK(test::count_entries(dir) == 0);
}

TEST(temporary_file_removed_when_exception_unwinds) {
    const auto dir = test::fresh_dir("tmp_unwind");

    CHECK_THROWS(([&] {
        TemporaryFile temporary(dir / "out.bin");
        temporary.write(sample_bytes(10));
        throw std::runtime_error("boom");
    }()));

    CHECK(test::count_entries(dir) == 0);
}

TEST(temporary_file_large_and_empty_writes) {
    const auto dir = test::fresh_dir("tmp_large");
    const auto destination = dir / "out.bin";

    // Больше одного чанка (64 KiB), записано несколькими вызовами.
    const auto bytes = sample_bytes(200 * 1024 + 123);
    const std::span<const std::uint8_t> view{ bytes };

    TemporaryFile temporary(destination);
    temporary.write({});
    temporary.write(view.first(70000));
    temporary.write(view.subspan(70000));
    temporary.commit();

    CHECK(test::read_all(destination) == bytes);
}

TEST(temporary_file_empty_content) {
    const auto dir = test::fresh_dir("tmp_empty");
    const auto destination = dir / "out.bin";

    TemporaryFile temporary(destination);
    temporary.commit();

    CHECK(std::filesystem::exists(destination));
    CHECK(std::filesystem::file_size(destination) == 0);
}

TEST(temporary_file_never_overwrites_existing_destination) {
    const auto dir = test::fresh_dir("tmp_exists");
    const auto destination = dir / "out.bin";
    const auto original = sample_bytes(5);

    write_binary_file(destination, original);

    CHECK_THROWS(write_binary_file(destination, sample_bytes(50)));

    CHECK(test::read_all(destination) == original);
    CHECK(test::count_entries(dir) == 1);  // неудачный commit убрал временный файл
}

TEST(temporary_file_concurrent_writers_use_distinct_temp_files) {
    const auto dir = test::fresh_dir("tmp_concurrent");
    const auto destination = dir / "out.bin";
    const auto first_bytes = sample_bytes(10);

    TemporaryFile first(destination);
    TemporaryFile second(destination);
    CHECK(test::count_entries(dir) == 2);

    first.write(first_bytes);
    second.write(sample_bytes(20));

    first.commit();
    CHECK_THROWS(second.commit());

    CHECK(test::read_all(destination) == first_bytes);
}

TEST(temporary_file_misuse_throws_logic_error) {
    const auto dir = test::fresh_dir("tmp_misuse");
    TemporaryFile temporary(dir / "out.bin");
    temporary.commit();

    bool write_after_commit = false;
    try {
        temporary.write(sample_bytes(1));
    }
    catch (const std::logic_error&) {
        write_after_commit = true;
    }
    CHECK(write_after_commit);

    bool double_commit = false;
    try {
        temporary.commit();
    }
    catch (const std::logic_error&) {
        double_commit = true;
    }
    CHECK(double_commit);
}

TEST(temporary_file_rejects_invalid_destinations) {
    const auto dir = test::fresh_dir("tmp_invalid");

    CHECK_THROWS(TemporaryFile(std::filesystem::path{}));
    CHECK_THROWS(TemporaryFile(dir / ""));                  // только каталог
    CHECK_THROWS(TemporaryFile(dir / "missing" / "out.bin"));  // нет папки
    CHECK(test::count_entries(dir) == 0);
}

TEST(temporary_file_supports_unicode_and_spaces_in_path) {
    const auto dir = test::fresh_dir("tmp_unicode");
    const auto destination = dir / L"папка с пробелом" / L"файл №1.bin";
    std::filesystem::create_directories(destination.parent_path());

    const auto bytes = sample_bytes(300);
    write_binary_file(destination, bytes);

    CHECK(test::read_all(destination) == bytes);
    CHECK(test::count_entries(destination.parent_path()) == 1);
}
