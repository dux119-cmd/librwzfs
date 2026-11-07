// Read performance benchmark: host tree vs the same tree as a mounted archive.
//
//   rwzfs_bench generate <tree-dir> [--sizes LIST]
//   rwzfs_bench archive  <tree-dir> <archive-file> [--compression SPEC]
//   rwzfs_bench run      <root>     [--budget SEC] [--max-iters N] [--sizes LIST] [--csv FILE]
//   rwzfs_bench compare  <work-dir> [--budget SEC] [--max-iters N] [--sizes LIST] [--csv FILE]
//
// `run` uses the same wrapper calls whatever the root is: a host directory (no mount) or the
// archive root "/rwz" (mounted). `compare` does the whole thing under <work-dir>: generate the
// tree, archive it, benchmark the host tree, mount the archive, benchmark it, unmount, compare.
//
// Content classes (compressibility): zero, runs (long runs of one value), patterns (short
// repeating patterns that change every 4 KiB), random. Sizes default to the full set:
// 100 B, 16 KiB, 256 KiB, 1 MiB, 100 MiB, 1 GiB. The full set is about 4.4 GB.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rwzfs/archive/create.hpp"
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"

namespace fs = std::filesystem;
namespace posix = rwzfs::io::posix;
using clk = std::chrono::steady_clock;

namespace {

constexpr std::uint64_t kKiB = 1024;
constexpr std::uint64_t kMiB = 1024 * kKiB;
constexpr std::uint64_t kGiB = 1024 * kMiB;
constexpr std::uint64_t kHeadBytes = 4 * kKiB;
constexpr std::uint64_t kChunkBytes = 64 * kKiB;
constexpr std::uint64_t kSeekSkew = kKiB;  // "near the start" / "near the end" offset
constexpr std::uint64_t kFullReadBuf = 128 * kKiB;
constexpr std::uint64_t kGenBlock = 1 * kMiB;
constexpr std::uint64_t kRunBlock = 64 * kKiB;
constexpr std::uint64_t kPatternBlock = 4 * kKiB;
constexpr int kMinIters = 5;

const std::vector<std::string> kClasses = {"zero", "runs", "patterns", "random"};
const std::vector<std::uint64_t> kDefaultSizes = {100, 16 * kKiB, 256 * kKiB, kMiB, 100 * kMiB, kGiB};

struct options {
  double budget_sec = 2.0;  // time budget per (file, operation); stops early for large files
  int max_iters = 500;
  std::vector<std::uint64_t> sizes = kDefaultSizes;
  std::string csv;
  std::string compression = "zstd:level=12";
};

std::string label(std::uint64_t bytes) {
  if (bytes >= kGiB && bytes % kGiB == 0) return std::to_string(bytes / kGiB) + "GiB";
  if (bytes >= kMiB && bytes % kMiB == 0) return std::to_string(bytes / kMiB) + "MiB";
  if (bytes >= kKiB && bytes % kKiB == 0) return std::to_string(bytes / kKiB) + "KiB";
  return std::to_string(bytes) + "B";
}

std::string rel_path(const std::string& cls, std::uint64_t size) {
  return cls + "/" + label(size) + ".bin";
}

// ---- data generation -------------------------------------------------------

void fill_block(const std::string& cls, std::uint64_t offset, char* buf, std::uint64_t n,
                std::mt19937_64& rng) {
  if (cls == "zero") {
    std::memset(buf, 0, n);
  } else if (cls == "runs") {
    for (std::uint64_t i = 0; i < n; i += kRunBlock) {
      const auto value = static_cast<char>((offset + i) / kRunBlock % 251);
      std::memset(buf + i, value, std::min<std::uint64_t>(kRunBlock, n - i));
    }
  } else if (cls == "patterns") {
    for (std::uint64_t i = 0; i < n; i += kPatternBlock) {
      std::uint64_t pattern[2] = {rng(), rng()};  // 16-byte pattern, new for every 4 KiB block
      const auto len = std::min<std::uint64_t>(kPatternBlock, n - i);
      for (std::uint64_t j = 0; j < len; ++j) {
        buf[i + j] = static_cast<char>(reinterpret_cast<const unsigned char*>(pattern)[j % 16]);
      }
    }
  } else {  // random
    for (std::uint64_t i = 0; i < n; i += 8) {
      const auto v = rng();
      std::memcpy(buf + i, &v, std::min<std::uint64_t>(8, n - i));
    }
  }
}

bool generate(const fs::path& tree, const options& opt) {
  std::mt19937_64 rng(42);
  std::vector<char> buf(kGenBlock);
  for (const auto& cls : kClasses) {
    for (const auto size : opt.sizes) {
      const auto path = tree / rel_path(cls, size);
      fs::create_directories(path.parent_path());
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      for (std::uint64_t off = 0; off < size;) {
        const auto n = std::min<std::uint64_t>(kGenBlock, size - off);
        fill_block(cls, off, buf.data(), n, rng);
        out.write(buf.data(), static_cast<std::streamsize>(n));
        off += n;
      }
      std::cout << "generated " << path.string() << " (" << label(size) << ")\n";
    }
  }
  return true;
}

// ---- measurement helpers ---------------------------------------------------

// Reads up to `want` bytes, one buffer-sized chunk at a time. The data is discarded: the
// buffer is reused, so `want` may be larger than the buffer.
std::uint64_t read_fully(int fd, char* buf, std::uint64_t want) {
  std::uint64_t got = 0;
  while (got < want) {
    const auto n = posix::read(fd, buf, std::min<std::uint64_t>(want - got, kFullReadBuf));
    if (n <= 0) break;
    got += static_cast<std::uint64_t>(n);
  }
  return got;
}

std::uint64_t file_size(const std::string& path) {
  struct stat st {};
  return posix::stat(path.c_str(), &st) == 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
}

struct stats {
  std::size_t count = 0;
  double min = 0, max = 0, mean = 0, stddev = 0, p50 = 0, p90 = 0, p95 = 0, p99 = 0, p999 = 0;
};

double percentile(const std::vector<double>& sorted, double p) {
  if (sorted.empty()) return 0;
  const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
  return sorted[std::clamp<std::size_t>(rank == 0 ? 0 : rank - 1, 0, sorted.size() - 1)];
}

stats summarize(std::vector<double> v) {
  stats s;
  s.count = v.size();
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.min = v.front();
  s.max = v.back();
  s.mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
  double sq = 0;
  for (const auto x : v) sq += (x - s.mean) * (x - s.mean);
  s.stddev = std::sqrt(sq / static_cast<double>(v.size()));
  s.p50 = percentile(v, 50);
  s.p90 = percentile(v, 90);
  s.p95 = percentile(v, 95);
  s.p99 = percentile(v, 99);
  s.p999 = percentile(v, 99.9);
  return s;
}

enum class op { open, head, tail, full, seek_start, seek_end };
const std::vector<op> kOps = {op::open, op::head, op::tail, op::full, op::seek_start, op::seek_end};

const char* op_name(op o) {
  switch (o) {
    case op::open: return "open";
    case op::head: return "read head (4KiB)";
    case op::tail: return "read tail (4KiB)";
    case op::full: return "read whole file";
    case op::seek_start: return "seek+read near start (64KiB)";
    case op::seek_end: return "seek+read near end (64KiB)";
  }
  return "?";
}

struct op_result {
  stats latency_us;   // per-operation latency, microseconds
  stats throughput;   // MB/s for read operations (empty for open)
  double total_mb_per_sec = 0;  // total bytes / total time for read operations
  std::size_t iterations = 0;
};

// Runs one operation repeatedly until max_iters, or the budget is spent after kMinIters.
op_result measure(op o, const std::string& path, std::uint64_t size, const options& opt) {
  std::vector<double> lat;
  std::vector<double> mbps;
  double total_bytes = 0, total_sec = 0;
  std::vector<char> buf(kFullReadBuf);
  const auto started = clk::now();

  while (static_cast<int>(lat.size()) < opt.max_iters) {
    const double elapsed = std::chrono::duration<double>(clk::now() - started).count();
    if (static_cast<int>(lat.size()) >= kMinIters && elapsed >= opt.budget_sec) break;

    if (o == op::open) {
      const auto t0 = clk::now();
      const int fd = posix::open(path.c_str(), O_RDONLY, 0);
      const auto t1 = clk::now();
      if (fd < 0) break;
      posix::close(fd);
      lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
      continue;
    }

    const int fd = posix::open(path.c_str(), O_RDONLY, 0);
    if (fd < 0) break;
    std::uint64_t bytes = 0;
    const auto t0 = clk::now();
    switch (o) {
      case op::head:
        posix::lseek(fd, 0, SEEK_SET);
        bytes = read_fully(fd, buf.data(), std::min<std::uint64_t>(kHeadBytes, size));
        break;
      case op::tail: {
        const auto n = std::min<std::uint64_t>(kHeadBytes, size);
        posix::lseek(fd, static_cast<off_t>(size - n), SEEK_SET);
        bytes = read_fully(fd, buf.data(), n);
        break;
      }
      case op::full:
        bytes = read_fully(fd, buf.data(), size);
        break;
      case op::seek_start:
        // Capped at half the file so tiny files still have bytes to read.
        posix::lseek(fd, static_cast<off_t>(std::min<std::uint64_t>(kSeekSkew, size / 2)), SEEK_SET);
        bytes = read_fully(fd, buf.data(), std::min<std::uint64_t>(kChunkBytes, size));
        break;
      case op::seek_end: {
        const auto n = std::min<std::uint64_t>(kChunkBytes, size);
        const auto off = size > n + kSeekSkew ? size - n - kSeekSkew : 0;
        posix::lseek(fd, static_cast<off_t>(off), SEEK_SET);
        bytes = read_fully(fd, buf.data(), n);
        break;
      }
      case op::open: break;
    }
    const auto t1 = clk::now();
    posix::close(fd);

    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    lat.push_back(us);
    if (us > 0) mbps.push_back(static_cast<double>(bytes) / (1024.0 * 1024.0) / (us / 1e6));
    total_bytes += static_cast<double>(bytes);
    total_sec += us / 1e6;
  }

  op_result r;
  r.latency_us = summarize(lat);
  r.throughput = summarize(mbps);
  r.total_mb_per_sec = total_sec > 0 ? total_bytes / (1024.0 * 1024.0) / total_sec : 0;
  r.iterations = lat.size();
  return r;
}

// ---- run and report --------------------------------------------------------

struct file_key {
  std::string cls;
  std::uint64_t size;
};

using results_t = std::map<std::pair<std::string, std::uint64_t>, std::map<op, op_result>>;

results_t run(const std::string& root, const options& opt) {
  results_t out;
  for (const auto& cls : kClasses) {
    for (const auto size : opt.sizes) {
      const auto path = root + "/" + rel_path(cls, size);
      const auto actual = file_size(path);
      if (actual == 0) {
        std::cerr << "missing or empty: " << path << " (generate/archive first)\n";
        continue;
      }
      auto& per_op = out[{cls, size}];
      for (const auto o : kOps) {
        std::cout << "  " << std::left << std::setw(28) << (cls + " " + label(size)) << op_name(o)
                  << "\r" << std::flush;
        per_op[o] = measure(o, path, actual, opt);
      }
      std::cout << std::string(80, ' ') << "\r" << std::flush;
    }
  }
  return out;
}

void print_table(const std::string& title, const results_t& r) {
  std::cout << "\n== " << title << " ==\n";
  std::cout << std::left << std::setw(20) << "file" << std::setw(28) << "operation" << std::right
            << std::setw(7) << "iters" << std::setw(11) << "min us" << std::setw(11) << "p50 us"
            << std::setw(11) << "p90 us" << std::setw(11) << "p99 us" << std::setw(11) << "p99.9 us"
            << std::setw(11) << "mean us" << std::setw(11) << "stddev" << std::setw(12) << "MB/s p50"
            << std::setw(12) << "MB/s agg" << "\n";
  for (const auto& [key, per_op] : r) {
    for (const auto o : kOps) {
      const auto it = per_op.find(o);
      if (it == per_op.end()) continue;
      const auto& x = it->second;
      std::cout << std::left << std::setw(20) << (key.first + " " + label(key.second))
                << std::setw(28) << op_name(o) << std::right << std::setw(7) << x.iterations
                << std::fixed << std::setprecision(1) << std::setw(11) << x.latency_us.min
                << std::setw(11) << x.latency_us.p50 << std::setw(11) << x.latency_us.p90
                << std::setw(11) << x.latency_us.p99 << std::setw(11) << x.latency_us.p999
                << std::setw(11) << x.latency_us.mean << std::setw(11) << x.latency_us.stddev;
      if (o == op::open) {
        std::cout << std::setw(12) << "-" << std::setw(12) << "-";
      } else {
        std::cout << std::setw(12) << x.throughput.p50 << std::setw(12) << x.total_mb_per_sec;
      }
      std::cout << "\n";
    }
  }
}

void print_comparison(const results_t& host, const results_t& mounted) {
  std::cout << "\n== comparison: host tree vs archive mounted (p50 latency; ratio = mounted/host) ==\n";
  std::cout << std::left << std::setw(20) << "file" << std::setw(28) << "operation" << std::right
            << std::setw(14) << "host p50 us" << std::setw(14) << "mount p50 us" << std::setw(9) << "ratio"
            << std::setw(14) << "host MB/s" << std::setw(14) << "mount MB/s" << std::setw(9) << "ratio" << "\n";
  for (const auto& [key, hops] : host) {
    const auto mit = mounted.find(key);
    if (mit == mounted.end()) continue;
    for (const auto o : kOps) {
      const auto h = hops.find(o);
      const auto m = mit->second.find(o);
      if (h == hops.end() || m == mit->second.end()) continue;
      const auto hl = h->second.latency_us.p50;
      const auto ml = m->second.latency_us.p50;
      std::cout << std::left << std::setw(20) << (key.first + " " + label(key.second))
                << std::setw(28) << op_name(o) << std::right << std::fixed << std::setprecision(1)
                << std::setw(14) << hl << std::setw(14) << ml << std::setprecision(2)
                << std::setw(9) << (hl > 0 ? ml / hl : 0);
      if (o == op::open) {
        std::cout << std::setw(14) << "-" << std::setw(14) << "-" << std::setw(9) << "-";
      } else {
        const auto ht = h->second.total_mb_per_sec;
        const auto mt = m->second.total_mb_per_sec;
        std::cout << std::setprecision(1) << std::setw(14) << ht << std::setw(14) << mt
                  << std::setprecision(2) << std::setw(9) << (mt > 0 ? ht / mt : 0);
      }
      std::cout << "\n";
    }
  }
  std::cout << "(MB/s ratio is host/mounted: above 1 means the mounted archive is slower.)\n";
}

void write_csv(const std::string& path, const std::string& which, const results_t& r) {
  std::ofstream out(path, std::ios::app);
  if (out.tellp() == 0) {
    out << "run,file,operation,iterations,min_us,p50_us,p90_us,p95_us,p99_us,p999_us,mean_us,"
           "stddev_us,mb_per_s_p50,mb_per_s_agg\n";
  }
  for (const auto& [key, per_op] : r) {
    for (const auto o : kOps) {
      const auto it = per_op.find(o);
      if (it == per_op.end()) continue;
      const auto& x = it->second;
      out << which << "," << key.first << " " << label(key.second) << "," << op_name(o) << ","
          << x.iterations << "," << x.latency_us.min << "," << x.latency_us.p50 << ","
          << x.latency_us.p90 << "," << x.latency_us.p95 << "," << x.latency_us.p99 << ","
          << x.latency_us.p999 << "," << x.latency_us.mean << "," << x.latency_us.stddev << ","
          << x.throughput.p50 << "," << x.total_mb_per_sec << "\n";
    }
  }
}

// ---- command line ----------------------------------------------------------

std::vector<std::uint64_t> parse_sizes(const std::string& list) {
  std::vector<std::uint64_t> out;
  std::stringstream ss(list);
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto mult = item.size() > 1 && item.back() == 'G' ? kGiB
                      : item.size() > 1 && item.back() == 'M' ? kMiB
                      : item.size() > 1 && item.back() == 'K' ? kKiB
                                                              : 1;
    if (mult != 1) item.pop_back();
    out.push_back(std::stoull(item) * mult);
  }
  return out;
}

bool parse_options(int argc, char** argv, int first, options& opt) {
  for (int i = first; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
      return argv[++i];
    };
    if (a == "--budget") opt.budget_sec = std::stod(next());
    else if (a == "--max-iters") opt.max_iters = std::stoi(next());
    else if (a == "--sizes") opt.sizes = parse_sizes(next());
    else if (a == "--csv") opt.csv = next();
    else if (a == "--compression") opt.compression = next();
    else { std::cerr << "unknown option " << a << "\n"; return false; }
  }
  return true;
}

void usage() {
  std::cerr << "usage:\n"
               "  rwzfs_bench generate <tree-dir> [--sizes LIST]\n"
               "  rwzfs_bench archive  <tree-dir> <archive-file> [--compression SPEC]\n"
               "  rwzfs_bench run      <root> [--budget SEC] [--max-iters N] [--sizes LIST] [--csv FILE]\n"
               "  rwzfs_bench compare  <work-dir> [--budget SEC] [--max-iters N] [--sizes LIST] [--csv FILE]\n"
               "sizes: comma list, suffix K/M/G (e.g. 100,16K,1M,100M,1G)\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  const std::string cmd = argv[1];
  try {
    if (cmd == "generate") {
      options opt;
      if (!parse_options(argc, argv, 3, opt)) return 2;
      return generate(argv[2], opt) ? 0 : 1;
    }
    if (cmd == "archive") {
      if (argc < 4) { usage(); return 2; }
      options opt;
      if (!parse_options(argc, argv, 4, opt)) return 2;
      rwzfs::create_options c;
      c.compression = opt.compression;
      const auto ok = rwzfs::create_archive(argv[2], argv[3], c);
      if (!ok) { std::cerr << "create_archive failed\n"; return 1; }
      std::cout << "archive " << argv[3] << "\n";
      return 0;
    }
    if (cmd == "run") {
      options opt;
      if (!parse_options(argc, argv, 3, opt)) return 2;
      const auto results = run(argv[2], opt);
      print_table(std::string("root ") + argv[2], results);
      if (!opt.csv.empty()) write_csv(opt.csv, argv[2], results);
      return 0;
    }
    if (cmd == "compare") {
      const fs::path work = argv[2];
      options opt;
      if (!parse_options(argc, argv, 3, opt)) return 2;
      const auto tree = work / "tree";
      const auto archive = work / "archive" / "bench.dwarfs";
      const auto writes = work / "archive" / "writes";
      fs::remove_all(work / "archive");
      fs::remove_all(tree);
      fs::create_directories(work / "archive");

      if (!generate(tree, opt)) { std::cerr << "generate failed\n"; return 1; }
      rwzfs::create_options c;
      c.compression = opt.compression;
      if (!rwzfs::create_archive(tree, archive, c)) { std::cerr << "create_archive failed\n"; return 1; }
      std::cout << "archive " << archive << " (" << file_size(archive.string()) << " bytes)\n";

      std::cout << "\nbenchmarking host tree (no mount)\n";
      const auto host = run(tree.string(), opt);

      std::cout << "\nmounting archive at " << rwzfs::fs::archive_root() << "\n";
      rwzfs::mount_options m;
      m.write_root = writes;
      if (rwzfs::mount_archive(archive, rwzfs::fs::archive_root().string(), m) != rwzfs::MountError::None) {
        std::cerr << "mount failed\n";
        return 1;
      }
      std::cout << "benchmarking mounted archive\n";
      const auto mounted = run(rwzfs::fs::archive_root().string(), opt);
      rwzfs::unmount_archive(rwzfs::fs::archive_root());

      print_table("host tree (no mount)", host);
      print_table("archive mounted", mounted);
      print_comparison(host, mounted);
      if (!opt.csv.empty()) {
        write_csv(opt.csv, "host", host);
        write_csv(opt.csv, "mounted", mounted);
      }
      return 0;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  usage();
  return 2;
}
