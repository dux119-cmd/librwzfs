// The std-style interface of rwz::stdio::file_istream and file_ostream: default
// construction, open(), close(), and the openmode flags with what each leaves
// in the file.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"

namespace fs     = std::filesystem;
namespace stdio  = rwz::stdio;
namespace mounts = rwz::fs::mounts;

namespace {

std::string slurp(const fs::path& p)
{
	std::ifstream in(p, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

void spit(const fs::path& p, const std::string& text)
{
	std::ofstream(p, std::ios::binary) << text;
}

} // namespace

class OpenModes : public ::testing::Test {
protected:
	fs::path file(const char* name) const
	{
		return dir_.path() / name;
	}
	TempDir dir_;
};

TEST_F(OpenModes, DefaultConstructedStreamIsClosedAndGood)
{
	stdio::file_istream in;
	EXPECT_FALSE(in.is_open());
	EXPECT_TRUE(in.good());
	stdio::file_ostream out = {};
	EXPECT_FALSE(out.is_open());
	EXPECT_TRUE(out.good());
}

TEST_F(OpenModes, OpenAfterDefaultConstructionReads)
{
	spit(file("a.txt"), "hello");
	stdio::file_istream in;
	in.open(file("a.txt"), std::ios::in);
	ASSERT_TRUE(in.is_open());
	std::string text = {};
	in >> text;
	EXPECT_EQ(text, "hello");
}

TEST_F(OpenModes, BinaryAteStartsAtTheEnd)
{
	spit(file("a.bin"), "0123456789");
	stdio::file_istream in(file("a.bin"), std::ios::binary | std::ios::ate);
	ASSERT_TRUE(in.is_open());
	EXPECT_EQ(static_cast<long>(in.tellg()), 10L);
	in.seekg(0);
	char buf[4] = {};
	in.read(buf, 4);
	EXPECT_EQ(std::string(buf, 4), "0123");
}

TEST_F(OpenModes, MissingFileInputFails)
{
	stdio::file_istream in(file("missing.txt"));
	EXPECT_FALSE(in.is_open());
	EXPECT_TRUE(in.fail());
}

TEST_F(OpenModes, InputRejectsTruncAndApp)
{
	spit(file("a.txt"), "keep");
	stdio::file_istream trunc(file("a.txt"), std::ios::in | std::ios::trunc);
	EXPECT_FALSE(trunc.is_open());
	EXPECT_EQ(slurp(file("a.txt")), "keep"); // the file was not touched
	stdio::file_istream app(file("a.txt"), std::ios::in | std::ios::app);
	EXPECT_FALSE(app.is_open());
}

TEST_F(OpenModes, OutputWithTruncReplacesContents)
{
	spit(file("a.txt"), "old contents");
	{
		stdio::file_ostream out(file("a.txt"),
		                        std::ios::out | std::ios::trunc);
		ASSERT_TRUE(out.is_open());
		out << "new";
	}
	EXPECT_EQ(slurp(file("a.txt")), "new");
}

TEST_F(OpenModes, OutputAloneTruncatesAsTheStandardSays)
{
	spit(file("a.txt"), "old contents");
	{
		stdio::file_ostream out(file("a.txt")); // out alone is "w"
		out << "x";
	}
	EXPECT_EQ(slurp(file("a.txt")), "x");
}

TEST_F(OpenModes, OutputAppendKeepsContents)
{
	spit(file("a.txt"), "head-");
	{
		stdio::file_ostream out(file("a.txt"), std::ios::out | std::ios::app);
		out << "tail";
	}
	EXPECT_EQ(slurp(file("a.txt")), "head-tail");
}

TEST_F(OpenModes, InOutWritesOverTheStartWithoutTruncating)
{
	spit(file("a.txt"), "hello");
	{
		stdio::file_ostream out(file("a.txt"), std::ios::in | std::ios::out);
		ASSERT_TRUE(out.is_open());
		out << "HE";
	}
	EXPECT_EQ(slurp(file("a.txt")), "HEllo");
}

TEST_F(OpenModes, InOutOnAMissingFileFails)
{
	stdio::file_ostream out(file("missing.txt"), std::ios::in | std::ios::out);
	EXPECT_FALSE(out.is_open());
	EXPECT_FALSE(fs::exists(file("missing.txt")));
}

TEST_F(OpenModes, BinaryOutputWritesExactBytes)
{
	{
		stdio::file_ostream out(file("b.bin"),
		                        std::ios::out | std::ios::binary);
		const char bytes[] = {'a', '\0', 'b'};
		out.write(bytes, 3);
	}
	EXPECT_EQ(slurp(file("b.bin")), std::string("a\0b", 3));
}

TEST_F(OpenModes, CloseClosesAndSetsFailbitWhenAlreadyClosed)
{
	spit(file("a.txt"), "x");
	stdio::file_istream in(file("a.txt"));
	ASSERT_TRUE(in.is_open());
	in.close();
	EXPECT_FALSE(in.is_open());
	in.clear();
	in.close(); // closing a closed stream sets failbit, as std::ifstream does
	EXPECT_TRUE(in.fail());
}

TEST_F(OpenModes, ReopenAfterCloseReadsTheNewFile)
{
	spit(file("a.txt"), "first");
	spit(file("b.txt"), "second");
	stdio::file_istream in(file("a.txt"));
	in.close();
	in.clear();
	in.open(file("b.txt"));
	ASSERT_TRUE(in.is_open());
	std::string text = {};
	in >> text;
	EXPECT_EQ(text, "second");
}

TEST_F(OpenModes, OutputCloseFlushesAndReopenWrites)
{
	stdio::file_ostream out;
	out.open(file("a.txt"), std::ios::out | std::ios::trunc);
	out << "one";
	out.close();
	EXPECT_FALSE(out.is_open());
	EXPECT_EQ(slurp(file("a.txt")), "one");
	out.clear();
	out.open(file("a.txt"), std::ios::out | std::ios::app);
	out << "+two";
	out.close();
	EXPECT_EQ(slurp(file("a.txt")), "one+two");
}

TEST_F(OpenModes, MovedStreamKeepsItsFile)
{
	spit(file("a.txt"), "payload");
	stdio::file_istream a(file("a.txt"));
	stdio::file_istream b = std::move(a);
	ASSERT_TRUE(b.is_open());
	std::string text = {};
	b >> text;
	EXPECT_EQ(text, "payload");
}

// The same flags on an archive file, through the merged view.
class OpenModesArchive : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("DIR/FILE.DAT", "payload");
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

TEST_F(OpenModesArchive, BinaryAteOnArchiveFileReportsItsSize)
{
	stdio::file_istream in(RWZ_ROOT "/DIR/FILE.DAT",
	                       std::ios::binary | std::ios::ate);
	ASSERT_TRUE(in.is_open());
	EXPECT_EQ(static_cast<long>(in.tellg()), 7L);
	in.seekg(0);
	std::string text = {};
	in >> text;
	EXPECT_EQ(text, "payload");
}
