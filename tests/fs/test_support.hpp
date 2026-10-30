#pragma once

#include <filesystem>
#include <random>
#include <string>

// Creates a unique temporary directory and removes it on destruction.
class TempDir {
 public:
  TempDir() {
    std::random_device rd;
    root_ = std::filesystem::temp_directory_path() /
            ("rwzfs_test_" + std::to_string(rd()) + std::to_string(rd()));
    std::filesystem::create_directories(root_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return root_; }

 private:
  std::filesystem::path root_;
};
