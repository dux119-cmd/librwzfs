#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../fs/test_support.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/overlay/lock.hpp"

namespace fs = std::filesystem;
namespace lock = rwzfs::overlay::lock;
namespace mounts = rwzfs::fs::mounts;

class OverlayLock : public ::testing::Test {
 protected:
  void SetUp() override { mounts::clear(); }
  void TearDown() override { mounts::clear(); }
  TempDir dir_;
};

TEST_F(OverlayLock, AcquireCreatesLockFileAndReleaseRemovesIt) {
  ASSERT_TRUE(lock::acquire(dir_.path()).has_value());
  EXPECT_TRUE(fs::exists(dir_.path() / lock::kLockName));
  lock::release(dir_.path());
  EXPECT_FALSE(fs::exists(dir_.path() / lock::kLockName));
}

TEST_F(OverlayLock, SecondAcquireWhileHeldIsInUse) {
  ASSERT_TRUE(lock::acquire(dir_.path()).has_value());
  const auto second = lock::acquire(dir_.path());
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), lock::lock_error::in_use);
  lock::release(dir_.path());
}

TEST_F(OverlayLock, ReacquireAfterReleaseSucceeds) {
  ASSERT_TRUE(lock::acquire(dir_.path()).has_value());
  lock::release(dir_.path());
  EXPECT_TRUE(lock::acquire(dir_.path()).has_value());
  lock::release(dir_.path());
}

TEST_F(OverlayLock, StaleLockOfDeadProcessIsTakenOver) {
  // A process id far above any real pid: its owner cannot be running.
  std::ofstream(dir_.path() / lock::kLockName) << "999999999\n";
  EXPECT_TRUE(lock::acquire(dir_.path()).has_value());
  lock::release(dir_.path());
}

TEST_F(OverlayLock, UnmountReleasesArchiveLock) {
  ASSERT_TRUE(lock::acquire(dir_.path()).has_value());
  ASSERT_TRUE(mounts::add("/rwz", nullptr, dir_.path()));
  const auto released = mounts::remove("/rwz");
  ASSERT_TRUE(released.has_value());
  EXPECT_FALSE(fs::exists(dir_.path() / lock::kLockName));
}

TEST_F(OverlayLock, ClearReleasesLocksOfAllMounts) {
  ASSERT_TRUE(lock::acquire(dir_.path()).has_value());
  ASSERT_TRUE(mounts::add("/rwz", nullptr, dir_.path()));
  mounts::clear();
  EXPECT_FALSE(fs::exists(dir_.path() / lock::kLockName));
}
