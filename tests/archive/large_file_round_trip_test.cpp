// Binary files of several sizes, archived with dwarfs and read back through the
// mount: the size each file reports (host and archive), seeking to its end, and
// reads at offsets that a CD-image reader uses (including past 32 KiB and past
// the first blocks). Needs dwarfs; the files are pattern-filled so a read at
// the wrong offset cannot match.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

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

void write_pattern(const fs::path& p, std::uint64_t size)
{
	std::ofstream out(p, std::ios::binary);
	const std::string chunk = pattern_bytes(0, size);
	out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
}

} // namespace

class LargeFileRoundTrip : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(LargeFileRoundTrip, SizeSeekAndReadsMatchTheSourceFile)
{
	const std::uint64_t size = GetParam();
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	write_pattern(src / "disc.bin", size);
	ASSERT_EQ(fs::file_size(src / "disc.bin"), size);

	const auto made = rwz::archive_directory(dir.path() / "Disc", src);
	ASSERT_TRUE(made.has_value())
	        << "create error " << static_cast<int>(made.error());

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	auto disc                    = rwz::session::open(*made, options);
	ASSERT_TRUE(disc.has_value());
	const fs::path file = disc->root() / "disc.bin";

	// The size as the view reports it, against the source.
	std::uintmax_t view_size = 0;
	for (const auto& entry : rwz::fs::directory_iterator(disc->root())) {
		if (entry.path().filename() == "disc.bin") {
			view_size = entry.file_size();
		}
	}
	EXPECT_EQ(view_size, size)
	        << "file size as the mounted view reports it (creator or reader)";

	{
		// Seeking to the end gives the same length, as the CD-image
		// code measures it.
		rwz::stdio::file_istream in(file, std::ios::in | std::ios::binary);
		ASSERT_TRUE(in.is_open());
		in.seekg(0, std::ios::end);
		EXPECT_EQ(static_cast<std::uint64_t>(in.tellg()), size)
		        << "seekg(end)/tellg length";
	}

	// Sector reads at the offsets from the failing log (32768, 37400,
	// 37648, 37656), past the first 64 KiB, and the middle and end of the
	// file. Each read is checked against the pattern at that offset.
	const std::uint64_t sector = 2352;
	rwz::stdio::file_istream in(file, std::ios::in | std::ios::binary);
	ASSERT_TRUE(in.is_open());
	for (std::uint64_t off : {std::uint64_t{0},
	                          std::uint64_t{32768},
	                          std::uint64_t{37400},
	                          std::uint64_t{37648},
	                          std::uint64_t{37656},
	                          std::uint64_t{65536},
	                          std::uint64_t{1u << 20},
	                          size / 2,
	                          size - sector}) {
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

	// The whole file, read in one pass, matches the source.
	in.clear();
	in.seekg(0, std::ios::beg);
	std::string all;
	std::vector<char> buf(64 * 1024);
	while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) ||
	       in.gcount() > 0) {
		all.append(buf.data(), static_cast<std::size_t>(in.gcount()));
		if (in.eof()) {
			break;
		}
	}
	EXPECT_EQ(all.size(), size) << "bytes read in one pass";
	EXPECT_TRUE(all == pattern_bytes(0, size))
	        << "contents differ from the source";
}

// 2014 is the length in the CD-image log; the others cross 64 KiB, 1 MiB and
// several dwarfs blocks.
INSTANTIATE_TEST_SUITE_P(Sizes, LargeFileRoundTrip,
                         ::testing::Values<std::uint64_t>(2014, 65536 + 1234, 1u << 20,
                                                          (3u << 20) + 2352 * 7 + 3,
                                                          (8u << 20) + 17));
