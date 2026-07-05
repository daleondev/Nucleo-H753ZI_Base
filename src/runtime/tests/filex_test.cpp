#include "runtime/filex/filesystem.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" int _open(const char* path, int flags, ...);
extern "C" int _close(int file);
extern "C" int _read(int file, char* buffer, int length);
extern "C" int _write(int file, const char* buffer, int length);
extern "C" off_t _lseek(int file, off_t offset, int origin);
extern "C" int _fstat(int file, struct stat* value);
extern "C" int _stat(const char* path, struct stat* value);
extern "C" int _unlink(const char* path);
extern "C" int _rename(const char* old_path, const char* new_path);
extern "C" int _mkdir(const char* path, mode_t mode);
extern "C" int _rmdir(const char* path);
extern "C" int _ftruncate(int file, off_t length);

namespace
{
    class FileXTest : public ::testing::Test
    {
      protected:
        static void SetUpTestSuite() { runtime_filex_initialize(); }

        void TearDown() override
        {
            static_cast<void>(_unlink("/renamed.bin"));
            static_cast<void>(_unlink("/mode.bin"));
            static_cast<void>(_unlink("/append.bin"));
            static_cast<void>(_unlink("/truncate.bin"));
            static_cast<void>(_unlink("/shared.bin"));
            static_cast<void>(_rmdir("/directory"));
            for (unsigned index{}; index < 4U; ++index) {
                const std::string path{ "/concurrent" + std::to_string(index) };
                static_cast<void>(_unlink(path.c_str()));
            }
            static_cast<void>(runtime::filex::setCurrentPath("/"));
        }
    };
}

TEST_F(FileXTest, NormalizesAbsoluteAndRelativePaths)
{
    char path[runtime::filex::MAXIMUM_PATH]{};
    EXPECT_EQ(runtime::filex::normalizePath("/alpha//beta/./gamma", path), 0);
    EXPECT_STREQ(path, "/alpha/beta/gamma");
    EXPECT_EQ(runtime::filex::normalizePath("../../escape", path), EACCES);
}

TEST_F(FileXTest, RejectsPathsBeyondFileXLimit)
{
    std::string path(runtime::filex::MAXIMUM_PATH, 'x');
    char normalized[runtime::filex::MAXIMUM_PATH]{};
    EXPECT_EQ(runtime::filex::normalizePath(path.c_str(), normalized), ENAMETOOLONG);
}

TEST_F(FileXTest, NewlibDescriptorRoundTripAndMetadata)
{
    const int file{ _open("/mode.bin", O_CREAT | O_RDWR | O_TRUNC, 0666) };
    ASSERT_GE(file, 3);
    constexpr char payload[]{ "FileX descriptor mode switching" };
    ASSERT_EQ(_write(file, payload, sizeof(payload)), static_cast<int>(sizeof(payload)));
    ASSERT_EQ(_lseek(file, 0, SEEK_SET), 0);
    std::array<char, sizeof(payload)> result{};
    EXPECT_EQ(_read(file, result.data(), result.size()), static_cast<int>(result.size()));
    EXPECT_EQ(result, std::to_array(payload));

    struct stat metadata{};
    EXPECT_EQ(_fstat(file, &metadata), 0);
    EXPECT_EQ(metadata.st_size, static_cast<off_t>(sizeof(payload)));
    EXPECT_EQ(_close(file), 0);
    EXPECT_EQ(_stat("/mode.bin", &metadata), 0);
    EXPECT_TRUE(S_ISREG(metadata.st_mode));
}

TEST_F(FileXTest, AppendAndTruncateAreDeterministic)
{
    int file{ _open("/append.bin", O_CREAT | O_WRONLY | O_TRUNC, 0666) };
    ASSERT_GE(file, 3);
    ASSERT_EQ(_write(file, "one", 3), 3);
    ASSERT_EQ(_close(file), 0);

    file = _open("/append.bin", O_WRONLY | O_APPEND);
    ASSERT_GE(file, 3);
    ASSERT_EQ(_lseek(file, 0, SEEK_SET), 0);
    ASSERT_EQ(_write(file, "two", 3), 3);
    ASSERT_EQ(_ftruncate(file, 4), 0);
    ASSERT_EQ(_close(file), 0);

    file = _open("/append.bin", O_RDONLY);
    ASSERT_GE(file, 3);
    std::array<char, 8> result{};
    EXPECT_EQ(_read(file, result.data(), result.size()), 4);
    EXPECT_EQ(std::string_view(result.data(), 4), "onet");
    EXPECT_EQ(_close(file), 0);
}

TEST_F(FileXTest, ExtendingAFileZeroFillsTheNewRange)
{
    int file{ _open("/truncate.bin", O_CREAT | O_RDWR | O_TRUNC, 0666) };
    ASSERT_GE(file, 3);
    ASSERT_EQ(_write(file, "abc", 3), 3);
    ASSERT_EQ(_ftruncate(file, 8), 0);
    ASSERT_EQ(_lseek(file, 0, SEEK_SET), 0);
    std::array<unsigned char, 8> result{};
    ASSERT_EQ(_read(file, reinterpret_cast<char*>(result.data()), result.size()),
              static_cast<int>(result.size()));
    EXPECT_EQ(result[0], 'a');
    EXPECT_EQ(result[1], 'b');
    EXPECT_EQ(result[2], 'c');
    EXPECT_EQ(result[3], 0U);
    EXPECT_EQ(result[7], 0U);
    EXPECT_EQ(_close(file), 0);
}

TEST_F(FileXTest, DescriptorTableHasExactlySixteenFileSlots)
{
    std::vector<int> files;
    for (std::size_t index{}; index < runtime::filex::MAXIMUM_OPEN_FILES; ++index) {
        const std::string path{ "/slot" + std::to_string(index) };
        const int file{ _open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0666) };
        ASSERT_GE(file, 3);
        files.push_back(file);
    }
    errno = 0;
    EXPECT_EQ(_open("/overflow", O_CREAT | O_RDWR, 0666), -1);
    EXPECT_EQ(errno, EMFILE);
    for (int file : files) {
        EXPECT_EQ(_close(file), 0);
    }
    for (std::size_t index{}; index < files.size(); ++index) {
        const std::string path{ "/slot" + std::to_string(index) };
        EXPECT_EQ(_unlink(path.c_str()), 0);
    }
    errno = 0;
    EXPECT_EQ(_unlink("/overflow"), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST_F(FileXTest, RenameDirectoriesAndInvalidHandles)
{
    EXPECT_EQ(_mkdir("/directory", 0777), 0);
    errno = 0;
    EXPECT_EQ(_unlink("/directory"), -1);
    EXPECT_EQ(errno, EISDIR);
    EXPECT_EQ(_rmdir("/directory"), 0);
    int file{ _open("/mode.bin", O_CREAT | O_WRONLY | O_TRUNC, 0666) };
    ASSERT_GE(file, 3);
    EXPECT_EQ(_close(file), 0);
    errno = 0;
    EXPECT_EQ(_rmdir("/mode.bin"), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(_rename("/mode.bin", "/renamed.bin"), 0);
    errno = 0;
    char unused{};
    EXPECT_EQ(_read(file, &unused, 0), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(FileXTest, SerializesSharedAndDistinctThreadAccess)
{
    const int shared{ _open("/shared.bin", O_CREAT | O_WRONLY | O_TRUNC, 0666) };
    ASSERT_GE(shared, 3);
    constexpr std::array<char, 64> payload{};
    std::array<std::thread, 4> threads;
    for (unsigned index{}; index < threads.size(); ++index) {
        threads[index] = std::thread{ [shared, index, &payload] {
            EXPECT_EQ(_write(shared, payload.data(), payload.size()), static_cast<int>(payload.size()));
            const std::string path{ "/concurrent" + std::to_string(index) };
            const int file{ _open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666) };
            ASSERT_GE(file, 3);
            EXPECT_EQ(_write(file, payload.data(), payload.size()), static_cast<int>(payload.size()));
            EXPECT_EQ(_close(file), 0);
        } };
    }
    for (auto& thread : threads) {
        thread.join();
    }
    struct stat metadata{};
    EXPECT_EQ(_fstat(shared, &metadata), 0);
    EXPECT_EQ(metadata.st_size, static_cast<off_t>(payload.size() * threads.size()));
    EXPECT_EQ(_close(shared), 0);
}
