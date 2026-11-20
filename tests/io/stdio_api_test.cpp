// The stream API as client code calls it: every argument type a caller has, and
// every combination of the six openmode flags, for both stream classes. Each
// combination must construct and open or fail cleanly; none may crash or fail
// to compile.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "../fs/test_support.hpp"
#include "rwzfs/io/stdio.h"

namespace fs    = std::filesystem;
namespace stdio = rwz::stdio;

namespace {

constexpr std::ios_base::openmode kFlags[] = {std::ios::in,
                                              std::ios::out,
                                              std::ios::app,
                                              std::ios::trunc,
                                              std::ios::binary,
                                              std::ios::ate};

// Every subset of the six flags.
std::ios_base::openmode combination(unsigned mask)
{
	std::ios_base::openmode m = {};
	for (unsigned i = 0; i < 6; ++i) {
		if ((mask >> i) & 1u) {
			m |= kFlags[i];
		}
	}
	return m;
}

} // namespace

TEST(StdioApi, ConstructorAcceptsEveryPathArgumentType)
{
	TempDir dir;
	const fs::path file = dir.path() / "a.txt";
	std::ofstream(file) << "x";
	const std::string as_string    = file.string();
	const char* as_chars           = as_string.c_str();
	const std::string_view as_view = as_string;

	stdio::file_istream from_path(file, std::ios::in | std::ios::binary);
	stdio::file_istream from_string(as_string, std::ios::binary | std::ios::ate);
	stdio::file_istream from_chars(as_chars, std::ios::in);
	stdio::file_istream from_view(fs::path(as_view), std::ios::in);
	EXPECT_TRUE(from_path.is_open());
	EXPECT_TRUE(from_string.is_open());
	EXPECT_TRUE(from_chars.is_open());
	EXPECT_TRUE(from_view.is_open());
}

TEST(StdioApi, DefaultConstructAndOpenAndCloseMembers)
{
	TempDir dir;
	const fs::path file = dir.path() / "a.txt";
	std::ofstream(file) << "hello";

	stdio::file_istream in;
	in.open(file.string(), std::ios::in);
	ASSERT_TRUE(in.is_open());
	in.close();
	EXPECT_FALSE(in.is_open());

	stdio::file_ostream out = {};
	out.open(dir.path() / "b.txt",
	         std::ios::out | std::ios::trunc | std::ios::binary);
	EXPECT_TRUE(out.is_open());
	out.close();
	EXPECT_FALSE(out.is_open());
}

TEST(StdioApi, EveryOpenmodeCombinationIsHandled)
{
	TempDir dir;
	const fs::path existing = dir.path() / "existing.txt";
	std::ofstream(existing) << "seed";
	for (unsigned mask = 0; mask < 64; ++mask) {
		const auto mode = combination(mask);
		// Input streams: in is always added; trunc and app are rejected
		// (failbit, nothing opened).
		const bool input_valid = !(mode & (std::ios::trunc | std::ios::app));
		stdio::file_istream in(existing, mode);
		EXPECT_EQ(in.is_open(), input_valid) << "input mask " << mask;
		if (!input_valid) {
			EXPECT_TRUE(in.fail()) << "input mask " << mask;
		}
		in.close();

		// Output streams: out is always added; every combination is a
		// valid fopen mode. in|out without trunc means "r+", which
		// needs the file: create it first, as the standard requires.
		const fs::path target = dir.path() /
		                        ("out" + std::to_string(mask) + ".txt");
		std::ofstream(target) << "seed";
		stdio::file_ostream out(target, mode);
		EXPECT_TRUE(out.is_open()) << "output mask " << mask;
		out << "x";
		out.close();
	}
}
