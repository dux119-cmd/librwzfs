#include "rwzfs/archive/create.hpp"

#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#ifndef RWZFS_MKDWARFS
#define RWZFS_MKDWARFS ""
#endif

namespace rwzfs {

namespace stdfs = std::filesystem;

std::expected<void, create_error> create_archive(const stdfs::path& source_dir,
                                                 const stdfs::path& output,
                                                 const create_options& options) {
  std::error_code ec;
  if (!stdfs::is_directory(source_dir, ec)) return std::unexpected(create_error::source_missing);
#if defined(_WIN32)
  (void)output;
  (void)options;
  return std::unexpected(create_error::unsupported_platform);
#else
  std::string tool = options.mkdwarfs ? options.mkdwarfs->string() : std::string(RWZFS_MKDWARFS);
  if (tool.empty()) tool = "mkdwarfs";

  const auto temp = stdfs::path(output.string() + ".tmp");
  stdfs::remove(temp, ec);

  const auto src_arg = "--input=" + source_dir.string();
  const auto out_arg = "--output=" + temp.string();
  const auto comp_arg = "--compression=" + options.compression;
  std::vector<std::string> args{tool,           "--force",     "--progress=simple",
                                "--categorize", "--no-history", comp_arg,
                                src_arg,        out_arg};
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = 0;
  if (posix_spawnp(&pid, tool.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    return std::unexpected(create_error::tool_not_found);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    stdfs::remove(temp, ec);
    return std::unexpected(create_error::tool_failed);
  }
  stdfs::rename(temp, output, ec);
  if (ec) {
    stdfs::remove(temp, ec);
    return std::unexpected(create_error::io);
  }
  return {};
#endif
}

}  // namespace rwzfs
