#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/overlay/cow.hpp"
#include "rwzfs/overlay/flatten.hpp"

namespace fs = std::filesystem;
namespace cow = rwzfs::overlay::cow;

namespace {

std::string read_text(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

class Flatten : public ::testing::Test {
 protected:
  void SetUp() override {
    backend_ = std::make_shared<fake_backend>();
    backend_->add_file("DIR/FILE.DAT", "payload");
    backend_->add_file("DIR/SUB/inner.txt", "x");
    backend_->add_file("ROOT.TXT", "root");
#if !defined(_WIN32)
    backend_->add_symlink("LINK", "DIR/FILE.DAT");
#endif
  }

  cow::layer at(const std::string& rel) const { return cow::layer{backend_, rel, overlay_.path()}; }

  TempDir overlay_;
  TempDir out_;
  std::shared_ptr<fake_backend> backend_;
};

#if !defined(_WIN32)  // creating symlinks needs privileges on Windows
TEST_F(Flatten, ArchiveWithoutChangesIsReproduced) {
  ASSERT_FALSE(rwzfs::overlay::flatten(at(""), out_.path()));
  EXPECT_EQ(read_text(out_.path() / "DIR" / "FILE.DAT"), "payload");
  EXPECT_EQ(read_text(out_.path() / "DIR" / "SUB" / "inner.txt"), "x");
  EXPECT_TRUE(fs::is_symlink(out_.path() / "LINK"));
  EXPECT_EQ(fs::read_symlink(out_.path() / "LINK"), fs::path("DIR/FILE.DAT"));
}

#endif

TEST_F(Flatten, OverlayChangesAreFlattenedAndArchiveUntouched) {
  // Modify, delete, create, rename a directory, and set a mode.
  const auto p = cow::prepare_write(at("DIR/FILE.DAT"), false, false, true);
  ASSERT_TRUE(p.has_value());
  std::ofstream(*p) << "changed";
  ASSERT_FALSE(cow::remove_file(at("ROOT.TXT")));
  ASSERT_FALSE(cow::rename_file(at("DIR/SUB"), at("NEWSUB")));
  const auto created = cow::prepare_write(at("NEW.TXT"), true, true, false);
  ASSERT_TRUE(created.has_value());
  std::ofstream(*created) << "new";
  ASSERT_FALSE(cow::set_mode(at("DIR/FILE.DAT"), 0600));

  ASSERT_FALSE(rwzfs::overlay::flatten(at(""), out_.path()));

  EXPECT_EQ(read_text(out_.path() / "DIR" / "FILE.DAT"), "changed");
#if !defined(_WIN32)  // Windows reports group bits for every file; only read-only is stored there
  EXPECT_EQ(fs::status(out_.path() / "DIR" / "FILE.DAT").permissions() & fs::perms::group_all,
            fs::perms::none);
#endif
  EXPECT_FALSE(fs::exists(out_.path() / "ROOT.TXT"));
  EXPECT_FALSE(fs::exists(out_.path() / "DIR" / "SUB"));
  EXPECT_EQ(read_text(out_.path() / "NEWSUB" / "inner.txt"), "x");
  EXPECT_EQ(read_text(out_.path() / "NEW.TXT"), "new");

  // The archive still has its original content.
  const auto orig = backend_->lookup("DIR/FILE.DAT");
  ASSERT_TRUE(orig.has_value());
  EXPECT_EQ(orig->size, 7u);
  EXPECT_TRUE(backend_->lookup("ROOT.TXT").has_value());
}

TEST_F(Flatten, HiddenArchiveChildrenAreNotFlattened) {
  ASSERT_FALSE(cow::remove_file(at("DIR/SUB/inner.txt")));
  ASSERT_FALSE(cow::remove_dir(at("DIR/SUB")));
  ASSERT_FALSE(rwzfs::overlay::flatten(at(""), out_.path()));
  EXPECT_FALSE(fs::exists(out_.path() / "DIR" / "SUB"));
  EXPECT_TRUE(fs::exists(out_.path() / "DIR" / "FILE.DAT"));
}
