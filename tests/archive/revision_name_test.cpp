#include <gtest/gtest.h>

#include <filesystem>

#include "rwzfs/revise.hpp"

namespace fs = std::filesystem;

TEST(RevisionName, FirstRevisionAppendsRev1) {
  EXPECT_EQ(rwzfs::next_revision_name("archive.dwarfs"), "archive-rev1.dwarfs");
  EXPECT_EQ(rwzfs::next_revision_name(fs::path("/data/archive.dwarfs")), "archive-rev1.dwarfs");
}

TEST(RevisionName, RevisionBumpsExistingNumber) {
  EXPECT_EQ(rwzfs::next_revision_name("archive-rev1.dwarfs"), "archive-rev2.dwarfs");
  EXPECT_EQ(rwzfs::next_revision_name("archive-rev9.dwarfs"), "archive-rev10.dwarfs");
}

TEST(RevisionName, ExtensionStartsAtFirstDot) {
  EXPECT_EQ(rwzfs::next_revision_name("my.data.dwarfs"), "my-rev1.data.dwarfs");
}

TEST(RevisionName, NonRevisionSuffixIsKept) {
  EXPECT_EQ(rwzfs::next_revision_name("report-rev.dwarfs"), "report-rev-rev1.dwarfs");
  EXPECT_EQ(rwzfs::next_revision_name("archive-revx.dwarfs"), "archive-revx-rev1.dwarfs");
}
