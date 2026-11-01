#include "rwzfs/fs/directory.hpp"

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::fs {

namespace stdfs = std::filesystem;

namespace {

stdfs::file_type file_type_of(archive::kind k) {
  switch (k) {
    case archive::kind::file: return stdfs::file_type::regular;
    case archive::kind::directory: return stdfs::file_type::directory;
    case archive::kind::symlink: return stdfs::file_type::symlink;
    case archive::kind::other: break;
  }
  return stdfs::file_type::unknown;
}

std::string join_rel(const std::string& parent, const std::string& name) {
  return parent.empty() ? name : parent + "/" + name;
}

directory_entry entry_at(const detail::archive_cursor& c) {
  const auto& child = (*c.children)[c.index];
  return directory_entry::from_archive(c.parent_absolute / child.name, child.info);
}

// Opens an archive directory. Returns false with `ec` set when the path is not a listable directory.
bool open_archive_listing(const mounts::route& r, detail::archive_cursor& out,
                          std::error_code& ec) {
  if (!r.backend) {
    ec = std::make_error_code(std::errc::not_supported);
    return false;
  }
  const auto e = r.backend->lookup(r.rel);
  if (!e) {
    ec = std::make_error_code(std::errc::no_such_file_or_directory);
    return false;
  }
  if (e->type != archive::kind::directory) {
    ec = std::make_error_code(std::errc::not_a_directory);
    return false;
  }
  auto children = r.backend->list(r.rel);
  if (!children) {
    ec = std::make_error_code(std::errc::not_a_directory);
    return false;
  }
  out.backend = r.backend;
  out.parent_absolute = r.absolute;
  out.parent_rel = r.rel;
  out.children = std::make_shared<const std::vector<archive::child>>(std::move(*children));
  out.index = 0;
  ec.clear();
  return true;
}

}  // namespace

// directory_entry

directory_entry directory_entry::from_host(const stdfs::directory_entry& e) {
  directory_entry d;
  d.path_ = e.path();
  d.host_ = e;
  return d;
}

directory_entry directory_entry::from_archive(stdfs::path absolute, archive::entry info) {
  directory_entry d;
  d.path_ = std::move(absolute);
  d.info_ = info;
  return d;
}

stdfs::file_status directory_entry::status(std::error_code& ec) const {
  if (host_) return host_->status(ec);
  ec.clear();
  return stdfs::file_status(file_type_of(info_.type),
                            static_cast<stdfs::perms>(info_.mode & 07777));
}

bool directory_entry::is_directory(std::error_code& ec) const {
  if (host_) return host_->is_directory(ec);
  ec.clear();
  return info_.type == archive::kind::directory;
}

bool directory_entry::is_regular_file(std::error_code& ec) const {
  if (host_) return host_->is_regular_file(ec);
  ec.clear();
  return info_.type == archive::kind::file;
}

bool directory_entry::is_symlink(std::error_code& ec) const {
  if (host_) return host_->is_symlink(ec);
  ec.clear();
  return info_.type == archive::kind::symlink;
}

std::uintmax_t directory_entry::file_size(std::error_code& ec) const {
  if (host_) return host_->file_size(ec);
  ec.clear();
  return info_.size;
}

// directory_iterator

directory_iterator::directory_iterator(const stdfs::path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = mounts::find(p)) {
    detail::archive_cursor cursor;
    if (!open_archive_listing(*r, cursor, ec)) return;
    if (cursor.children->empty()) return;  // empty listing: equal to end()
    archive_ = std::move(cursor);
    entry_ = entry_at(*archive_);
    valid_ = true;
    return;
  }
  stdfs::directory_iterator it(p, ec);
  if (ec) return;
  if (it == stdfs::directory_iterator{}) {
    ec.clear();
    return;
  }
  host_ = it;
  entry_ = directory_entry::from_host(*it);
  valid_ = true;
  ec.clear();
}

directory_iterator& directory_iterator::operator++() {
  if (!valid_) return *this;
  if (archive_) {
    if (++archive_->index < archive_->children->size()) {
      entry_ = entry_at(*archive_);
    } else {
      archive_.reset();
      entry_ = {};
      valid_ = false;
    }
    return *this;
  }
  std::error_code ec;
  host_->increment(ec);
  if (ec) throw stdfs::filesystem_error("rwzfs::fs::directory_iterator::operator++", ec);
  if (*host_ == stdfs::directory_iterator{}) {
    host_.reset();
    entry_ = {};
    valid_ = false;
  } else {
    entry_ = directory_entry::from_host(**host_);
  }
  return *this;
}

directory_iterator directory_iterator::operator++(int) {
  auto old = *this;
  ++*this;
  return old;
}

bool operator==(const directory_iterator& a, const directory_iterator& b) {
  if (a.valid_ != b.valid_) return false;
  if (!a.valid_) return true;
  if (a.archive_ || b.archive_) {
    return a.archive_ && b.archive_ && a.archive_->backend == b.archive_->backend &&
           a.archive_->parent_rel == b.archive_->parent_rel &&
           a.archive_->index == b.archive_->index;
  }
  return *a.host_ == *b.host_;
}

// recursive_directory_iterator

recursive_directory_iterator::recursive_directory_iterator(const stdfs::path& in,
                                                           stdfs::directory_options options,
                                                           std::error_code& ec)
    : options_(options) {
  const auto p = resolve(in);
  if (const auto r = mounts::find(p)) {
    detail::archive_cursor cursor;
    if (!open_archive_listing(*r, cursor, ec)) return;
    if (cursor.children->empty()) return;
    stack_.push_back(std::move(cursor));
    entry_ = entry_at(stack_.back());
    valid_ = true;
    return;
  }
  stdfs::recursive_directory_iterator it(p, options, ec);
  if (ec) return;
  if (it == stdfs::recursive_directory_iterator{}) {
    ec.clear();
    return;
  }
  host_ = it;
  entry_ = directory_entry::from_host(*it);
  valid_ = true;
  ec.clear();
}

void recursive_directory_iterator::advance_archive() {
  while (!stack_.empty()) {
    auto& top = stack_.back();
    if (++top.index < top.children->size()) {
      entry_ = entry_at(top);
      pending_ = true;
      return;
    }
    stack_.pop_back();
  }
  entry_ = {};
  valid_ = false;
}

recursive_directory_iterator& recursive_directory_iterator::operator++() {
  if (!valid_) return *this;
  if (host_) {
    std::error_code ec;
    host_->increment(ec);
    if (ec) throw stdfs::filesystem_error("rwzfs::fs::recursive_directory_iterator::operator++", ec);
    if (*host_ == stdfs::recursive_directory_iterator{}) {
      host_.reset();
      entry_ = {};
      valid_ = false;
    } else {
      entry_ = directory_entry::from_host(**host_);
    }
    return *this;
  }

  // Descend into the current entry if it is a directory and recursion has not been disabled.
  std::error_code is_dir_ec;
  if (pending_ && entry_.is_directory(is_dir_ec)) {
    const auto& parent = stack_.back();
    const auto child_rel = join_rel(parent.parent_rel, entry_.path().filename().generic_string());
    if (auto children = parent.backend->list(child_rel); children && !children->empty()) {
      detail::archive_cursor frame;
      frame.backend = parent.backend;
      frame.parent_absolute = entry_.path();
      frame.parent_rel = child_rel;
      frame.children = std::make_shared<const std::vector<archive::child>>(std::move(*children));
      stack_.push_back(std::move(frame));
      entry_ = entry_at(stack_.back());
      pending_ = true;
      return *this;
    }
  }
  advance_archive();
  return *this;
}

recursive_directory_iterator recursive_directory_iterator::operator++(int) {
  auto old = *this;
  ++*this;
  return old;
}

int recursive_directory_iterator::depth() const noexcept {
  if (host_) return host_->depth();
  if (!valid_ || stack_.empty()) return 0;
  return static_cast<int>(stack_.size()) - 1;
}

bool recursive_directory_iterator::recursion_pending() const noexcept {
  if (host_) return host_->recursion_pending();
  return pending_;
}

void recursive_directory_iterator::disable_recursion_pending() noexcept {
  if (host_) {
    host_->disable_recursion_pending();
    return;
  }
  pending_ = false;
}

bool operator==(const recursive_directory_iterator& a, const recursive_directory_iterator& b) {
  if (a.valid_ != b.valid_) return false;
  if (!a.valid_) return true;
  if (a.host_ || b.host_) return a.host_ && b.host_ && *a.host_ == *b.host_;
  return a.stack_.size() == b.stack_.size() && a.entry_.path() == b.entry_.path();
}

}  // namespace rwzfs::fs
