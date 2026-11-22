// Large files through the library's stream: length by seeking to the end, and
// reads at offsets far beyond the stream buffer and copy chunk sizes. Mirrors
// what a CD-ROM image reader does: size the file with seekg(end)/tellg(), then
// read sectors at arbitrary offsets. Uses a fake archive, so it runs everywhere.
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"

namespace stdio  = rwz::stdio;
namespace mounts = rwz::fs::mounts;

namespace {

// Byte i of the test pattern. Varies every byte so a read at the wrong offset
// cannot match by accident.
std::uint8_t pattern(std::uint64_t i)
{
	return static_cast<std::uint8_t>((i * 2654435761u) >> 13);
}

std::string pattern_bytes(std::uint64_t begin, std::uint64_t count)
{
	std::string out(count, '\0');
	for (std::uint64_t i = 0; i < count; ++i) {
		out[i] = static_cast<char>(pattern(begin + i));
	}
	return out;
}

} // namespace

class LargeFile : public ::testing::TestWithParam<std::uint64_t> {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("disc.bin", pattern_bytes(0, GetParam()));
		ASSERT_TRUE(mounts::add(RWZ_ROOT, backend_, overlay_.path()));
	}
	void TearDown() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
	}
	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_ = {};
};

TEST_P(LargeFile, LengthBySeekingToTheEndIsTheFileSize)
{
	stdio::file_istream in(RWZ_ROOT "/disc.bin", std::ios::in | std::ios::binary);
	ASSERT_TRUE(in.is_open());
	in.seekg(0, std::ios::end);
	EXPECT_EQ(static_cast<std::uint64_t>(in.tellg()), GetParam());
}

TEST_P(LargeFile, ReadsAtOffsetsAcrossTheWholeFile)
{
	const std::uint64_t size = GetParam();
	stdio::file_istream in(RWZ_ROOT "/disc.bin", std::ios::in | std::ios::binary);
	ASSERT_TRUE(in.is_open());
	// Sector-sized reads at the offsets a CD reader would use, including
	// ones beyond 32 KiB and the copy chunk size (1 MiB), and the very last
	// sector.
	const std::uint64_t sector               = 2352;
	const std::vector<std::uint64_t> offsets = {0,
	                                            32768,
	                                            37400,
	                                            37648,
	                                            37656,
	                                            65536,
	                                            1u << 20,
	                                            (1u << 20) + 37656,
	                                            size / 2,
	                                            size - sector};
	for (std::uint64_t off : offsets) {
		// Only offsets with a whole sector before the end. Written this
		// way: off + sector can wrap for big offsets.
		if (off > size || size - off < sector) {
			continue;
		}
		in.clear();
		in.seekg(static_cast<std::streamoff>(off), std::ios::beg);
		std::string got(sector, '\0');
		in.read(got.data(), static_cast<std::streamsize>(sector));
		ASSERT_EQ(in.gcount(), static_cast<std::streamsize>(sector))
		        << "short read at offset " << off;
		EXPECT_EQ(got, pattern_bytes(off, sector))
		        << "wrong bytes at offset " << off;
	}
}

TEST_P(LargeFile, FopenFseekFtellFreadOnLargeFile)
{
	const std::uint64_t size = GetParam();
	std::FILE* fp            = stdio::fopen(RWZ_ROOT "/disc.bin", "rb");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(stdio::fseek(fp, 0, SEEK_END), 0);
	EXPECT_EQ(static_cast<std::uint64_t>(stdio::ftell(fp)), size);
	ASSERT_EQ(stdio::fseek(fp, 37656, SEEK_SET), 0);
	std::string got(2352, '\0');
	const std::size_t n = stdio::fread(got.data(), 1, got.size(), fp);
	if (size >= 37656 + 2352) {
		EXPECT_EQ(n, got.size());
		EXPECT_EQ(got, pattern_bytes(37656, 2352));
	} else {
		// Past the end of a file this small: end of file, nothing read
		// (as for any file).
		EXPECT_EQ(n, size > 37656 ? static_cast<std::size_t>(size - 37656) : 0u);
		EXPECT_EQ(got.substr(0, n), pattern_bytes(37656, n));
	}
	EXPECT_EQ(stdio::fclose(fp), 0);
}

INSTANTIATE_TEST_SUITE_P(Sizes, LargeFile,
                         ::testing::Values<std::uint64_t>(2014, 65536 + 1234, 2u << 20,
                                                          (8u << 20) + 17));
