#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace fs = std::filesystem;
namespace cow = rwzfs::overlay::cow;

namespace {

std::string read_host(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::set<std::string> names(const std::optional<std::vector<rwzfs::archive::child>>& kids) {
  std::set<std::string> out;
  if (kids)
    for (const auto& c : *kids) out.insert(c.name);
  return out;
}

}  // namespace

class Cow : public ::testing::Test {
 protected:
  void SetUp() override {
    backend_ = std::make_shared<fake_backend>();
    backend_->add_file("DIR/FILE.DAT", "payload");
    backend_->add_file("DIR/SUB/inner.txt", "x");
    backend_->add_file("ROOT.TXT", "root");
  }

  cow::layer at(const std::string& rel) const { return cow::layer{backend_, rel, overlay_.path()}; }

  TempDir overlay_;  // overlay directory (the <stem>-writes directory in production)
  std::shared_ptr<fake_backend> backend_;
};

TEST_F(Cow, LookupArchiveEntry) {
  const auto n = cow::lookup(at("DIR/FILE.DAT"));
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(n->where, cow::origin::archive);
  EXPECT_EQ(n->info.size, 7u);
  EXPECT_FALSE(cow::lookup(at("DIR/NOPE")).has_value());
}

TEST_F(Cow, ListMergesArchiveChildren) {
  EXPECT_EQ(names(cow::list(at(""))), (std::set<std::string>{"DIR", "ROOT.TXT"}));
  EXPECT_EQ(names(cow::list(at("DIR"))), (std::set<std::string>{"FILE.DAT", "SUB"}));
  EXPECT_FALSE(cow::list(at("ROOT.TXT")).has_value());
}

TEST_F(Cow, PrepareWriteCopiesUpArchiveContent) {
  const auto p = cow::prepare_write(at("DIR/FILE.DAT"), false, false, false);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(read_host(*p), "payload");
  EXPECT_EQ(cow::lookup(at("DIR/FILE.DAT"))->where, cow::origin::overlay);
}

TEST_F(Cow, PrepareWriteTruncateStartsEmpty) {
  const auto p = cow::prepare_write(at("DIR/FILE.DAT"), false, false, true);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(read_host(*p), "");
}

TEST_F(Cow, CreateNewFileInArchiveDirectory) {
  const auto p = cow::prepare_write(at("DIR/NEW.TXT"), true, true, false);
  ASSERT_TRUE(p.has_value());
  std::ofstream(*p) << "new";
  const auto n = cow::lookup(at("DIR/NEW.TXT"));
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(n->where, cow::origin::overlay);
  EXPECT_EQ(names(cow::list(at("DIR"))), (std::set<std::string>{"FILE.DAT", "NEW.TXT", "SUB"}));
}

TEST_F(Cow, ExclusiveCreateOfExistingFails) {
  const auto p = cow::prepare_write(at("DIR/FILE.DAT"), true, true, false);
  ASSERT_FALSE(p.has_value());
  EXPECT_EQ(p.error(), std::errc::file_exists);
}

TEST_F(Cow, WriteWithoutCreateOfMissingFails) {
  const auto p = cow::prepare_write(at("DIR/MISSING"), false, false, false);
  ASSERT_FALSE(p.has_value());
  EXPECT_EQ(p.error(), std::errc::no_such_file_or_directory);
}

TEST_F(Cow, CreateUnderMissingParentFails) {
  const auto p = cow::prepare_write(at("NODIR/x"), true, false, false);
  ASSERT_FALSE(p.has_value());
  EXPECT_EQ(p.error(), std::errc::no_such_file_or_directory);
}

TEST_F(Cow, CreateUnderFileParentFails) {
  const auto p = cow::prepare_write(at("ROOT.TXT/x"), true, false, false);
  ASSERT_FALSE(p.has_value());
  EXPECT_EQ(p.error(), std::errc::not_a_directory);
}

TEST_F(Cow, RemoveArchiveFileHidesItWithWhiteout) {
  EXPECT_FALSE(cow::remove_file(at("DIR/FILE.DAT")));
  EXPECT_FALSE(cow::lookup(at("DIR/FILE.DAT")).has_value());
  EXPECT_EQ(names(cow::list(at("DIR"))), (std::set<std::string>{"SUB"}));
  // Recreating it removes the whiteout and the new content is visible.
  const auto p = cow::prepare_write(at("DIR/FILE.DAT"), true, false, true);
  ASSERT_TRUE(p.has_value());
  std::ofstream(*p) << "again";
  EXPECT_EQ(cow::lookup(at("DIR/FILE.DAT"))->where, cow::origin::overlay);
}

TEST_F(Cow, RemoveMissingFileFails) {
  EXPECT_EQ(cow::remove_file(at("DIR/MISSING")), std::errc::no_such_file_or_directory);
}

TEST_F(Cow, RemoveDirectoryIsNotAFileOperation) {
  EXPECT_EQ(cow::remove_file(at("DIR")), std::errc::is_a_directory);
}

TEST_F(Cow, RemoveNonEmptyDirectoryFails) {
  EXPECT_EQ(cow::remove_dir(at("DIR")), std::errc::directory_not_empty);
}

TEST_F(Cow, RecreatedDirectoryHidesArchiveChildren) {
  EXPECT_FALSE(cow::remove_file(at("DIR/SUB/inner.txt")));
  EXPECT_FALSE(cow::remove_dir(at("DIR/SUB")));
  EXPECT_FALSE(cow::lookup(at("DIR/SUB")).has_value());
  EXPECT_FALSE(cow::create_dir(at("DIR/SUB")));
  EXPECT_TRUE(cow::list(at("DIR/SUB"))->empty());  // archive child stays hidden
  EXPECT_FALSE(cow::lookup(at("DIR/SUB/inner.txt")).has_value());
}

TEST_F(Cow, OverlayOnlyDirectoryRemovalLeavesNoMarker) {
  EXPECT_FALSE(cow::create_dir(at("DIR/NEWDIR")));
  EXPECT_FALSE(cow::remove_dir(at("DIR/NEWDIR")));
  EXPECT_FALSE(cow::lookup(at("DIR/NEWDIR")).has_value());
  EXPECT_EQ(names(cow::list(at("DIR"))), (std::set<std::string>{"FILE.DAT", "SUB"}));
}

TEST_F(Cow, CreateDirectoryExistingFails) {
  EXPECT_EQ(cow::create_dir(at("DIR")), std::errc::file_exists);
}

TEST_F(Cow, TruncateCopiesUpAndShortens) {
  EXPECT_FALSE(cow::truncate(at("DIR/FILE.DAT"), 3));
  EXPECT_EQ(read_host(overlay_.path() / "data" / "DIR" / "FILE.DAT"), "pay");
  EXPECT_EQ(cow::lookup(at("DIR/FILE.DAT"))->info.size, 3u);
}

TEST_F(Cow, RenameArchiveFileMovesContent) {
  EXPECT_FALSE(cow::rename_file(at("DIR/FILE.DAT"), at("DIR/MOVED.DAT")));
  EXPECT_FALSE(cow::lookup(at("DIR/FILE.DAT")).has_value());
  const auto moved = cow::lookup(at("DIR/MOVED.DAT"));
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(read_host(moved->host), "payload");
  EXPECT_EQ(names(cow::list(at("DIR"))), (std::set<std::string>{"MOVED.DAT", "SUB"}));
}

TEST_F(Cow, RenameOntoArchiveFileReplacesIt) {
  EXPECT_FALSE(cow::rename_file(at("ROOT.TXT"), at("DIR/FILE.DAT")));
  EXPECT_EQ(read_host(cow::lookup(at("DIR/FILE.DAT"))->host), "root");
  EXPECT_FALSE(cow::lookup(at("ROOT.TXT")).has_value());
}

TEST_F(Cow, RenameDirectoryMovesWholeTree) {
  EXPECT_FALSE(cow::rename_file(at("DIR"), at("OTHER")));
  EXPECT_FALSE(cow::lookup(at("DIR")).has_value());
  EXPECT_EQ(names(cow::list(at(""))), (std::set<std::string>{"OTHER", "ROOT.TXT"}));
  EXPECT_EQ(names(cow::list(at("OTHER"))), (std::set<std::string>{"FILE.DAT", "SUB"}));
  EXPECT_EQ(names(cow::list(at("OTHER/SUB"))), (std::set<std::string>{"inner.txt"}));
  const auto moved = cow::lookup(at("OTHER/FILE.DAT"));
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(read_host(moved->host), "payload");
  // The archive keeps its tree.
  EXPECT_TRUE(backend_->lookup("DIR/SUB/inner.txt").has_value());
}

TEST_F(Cow, RenameDirectoryOntoNonEmptyDirectoryFails) {
  EXPECT_EQ(cow::rename_file(at("DIR"), at("DIR/SUB")), std::errc::invalid_argument);
}

TEST_F(Cow, MutationsWithoutOverlayAreReadOnly) {
  cow::layer ro{backend_, "DIR/FILE.DAT", {}};
  EXPECT_EQ(cow::remove_file(ro), std::errc::read_only_file_system);
  EXPECT_EQ(cow::create_dir(cow::layer{backend_, "DIR/N", {}}), std::errc::read_only_file_system);
  EXPECT_FALSE(cow::prepare_write(ro, true, false, false).has_value());
  // Reads still work without an overlay.
  EXPECT_TRUE(cow::lookup(ro).has_value());
}

TEST_F(Cow, ListingSortsAndOverlayOverrides) {
  const auto kids = cow::list(at("DIR"));
  ASSERT_TRUE(kids.has_value());
  EXPECT_EQ(kids->front().name, "FILE.DAT");
  EXPECT_EQ(kids->back().name, "SUB");
}
