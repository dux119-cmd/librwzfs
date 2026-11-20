#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "rwzfs/archive/backend.h"

namespace rwz::fs {

// One entry from a listing. Host entries wrap std::filesystem::directory_entry.
// Archive entries carry the backend's metadata.
class directory_entry {
public:
	directory_entry() = default;

	[[nodiscard]] const std::filesystem::path& path() const noexcept
	{
		return path_;
	}

	// The queries of std::filesystem::directory_entry. The error_code forms
	// return the answer and set ec; the forms without one throw
	// std::filesystem::filesystem_error with the entry's path, as the
	// standard library does. status-style queries follow symlinks;
	// symlink_status and is_symlink do not.
	[[nodiscard]] std::filesystem::file_status status(std::error_code& ec) const;
	[[nodiscard]] std::filesystem::file_status symlink_status(std::error_code& ec) const;
	[[nodiscard]] bool exists(std::error_code& ec) const;
	[[nodiscard]] bool is_directory(std::error_code& ec) const;
	[[nodiscard]] bool is_regular_file(std::error_code& ec) const;
	[[nodiscard]] bool is_symlink(std::error_code& ec) const;
	[[nodiscard]] bool is_other(std::error_code& ec) const;
	[[nodiscard]] std::uintmax_t file_size(std::error_code& ec) const;

	[[nodiscard]] std::filesystem::file_status status() const
	{
		return checked("directory_entry::status",
		               [this](std::error_code& e) { return status(e); });
	}
	[[nodiscard]] std::filesystem::file_status symlink_status() const
	{
		return checked("directory_entry::symlink_status",
		               [this](std::error_code& e) {
			               return symlink_status(e);
		               });
	}
	[[nodiscard]] bool exists() const
	{
		return checked("directory_entry::exists",
		               [this](std::error_code& e) { return exists(e); });
	}
	[[nodiscard]] bool is_directory() const
	{
		return checked("directory_entry::is_directory",
		               [this](std::error_code& e) {
			               return is_directory(e);
		               });
	}
	[[nodiscard]] bool is_regular_file() const
	{
		return checked("directory_entry::is_regular_file",
		               [this](std::error_code& e) {
			               return is_regular_file(e);
		               });
	}
	[[nodiscard]] bool is_symlink() const
	{
		return checked("directory_entry::is_symlink",
		               [this](std::error_code& e) {
			               return is_symlink(e);
		               });
	}
	[[nodiscard]] bool is_other() const
	{
		return checked("directory_entry::is_other",
		               [this](std::error_code& e) { return is_other(e); });
	}
	[[nodiscard]] std::uintmax_t file_size() const
	{
		return checked("directory_entry::file_size",
		               [this](std::error_code& e) {
			               return file_size(e);
		               });
	}

	[[nodiscard]] static directory_entry from_host(
	        const std::filesystem::directory_entry& e);
	[[nodiscard]] static directory_entry from_archive(std::filesystem::path absolute,
	                                                  rwz::archive::entry info);

private:
	template <typename F>
	auto checked(const char* operation, F query) const
	        -> decltype(query(std::declval<std::error_code&>()))
	{
		std::error_code ec = {};
		auto result        = query(ec);
		if (ec) {
			throw std::filesystem::filesystem_error(operation, path_, ec);
		}
		return result;
	}

	std::filesystem::path path_{};
	std::optional<std::filesystem::directory_entry> host_{};
	rwz::archive::entry info_{}; // used when host_ is empty
};

namespace detail {

// Position within one archive directory listing. Held by value, so copies
// advance independently.
struct archive_cursor {
	std::shared_ptr<const rwz::archive::backend> backend;
	std::filesystem::path overlay; // empty when the mount has no overlay
	std::filesystem::path parent_absolute;
	std::string parent_rel;
	std::shared_ptr<const std::vector<rwz::archive::child>> children;
	std::size_t index{0};
};

} // namespace detail

// Lists one directory. Same interface and error reporting as
// std::filesystem::directory_iterator.
class directory_iterator {
public:
	using iterator_category = std::input_iterator_tag;
	using value_type        = directory_entry;
	using difference_type   = std::ptrdiff_t;
	using pointer           = const directory_entry*;
	using reference         = const directory_entry&;

	directory_iterator() = default;
	// Same signatures as std::filesystem::directory_iterator. Options apply
	// to host directories; archive listings have no permission errors and
	// no directory symlinks to follow, so they ignore them.
	directory_iterator(const std::filesystem::path& p,
	                   std::filesystem::directory_options options,
	                   std::error_code& ec);
	directory_iterator(const std::filesystem::path& p, std::error_code& ec)
	        : directory_iterator(p, std::filesystem::directory_options::none, ec)
	{}
	// Throwing forms: std::filesystem::filesystem_error when the directory
	// cannot be listed.
	directory_iterator(const std::filesystem::path& p,
	                   std::filesystem::directory_options options);
	explicit directory_iterator(const std::filesystem::path& p);

	[[nodiscard]] reference operator*() const noexcept
	{
		return entry_;
	}
	[[nodiscard]] pointer operator->() const noexcept
	{
		return &entry_;
	}

	directory_iterator& operator++();
	directory_iterator operator++(int);

	friend bool operator==(const directory_iterator& a,
	                       const directory_iterator& b);

private:
	bool valid_{false};
	std::optional<std::filesystem::directory_iterator> host_{};
	std::optional<detail::archive_cursor> archive_{};
	directory_entry entry_{};
};

// Lists a directory tree. Same interface as
// std::filesystem::recursive_directory_iterator.
class recursive_directory_iterator {
public:
	using iterator_category = std::input_iterator_tag;
	using value_type        = directory_entry;
	using difference_type   = std::ptrdiff_t;
	using pointer           = const directory_entry*;
	using reference         = const directory_entry&;

	recursive_directory_iterator() = default;
	recursive_directory_iterator(const std::filesystem::path& p,
	                             std::filesystem::directory_options options,
	                             std::error_code& ec);
	recursive_directory_iterator(const std::filesystem::path& p,
	                             std::error_code& ec)
	        : recursive_directory_iterator(p, std::filesystem::directory_options::none,
		                               ec)
	{}

	[[nodiscard]] reference operator*() const noexcept
	{
		return entry_;
	}
	[[nodiscard]] pointer operator->() const noexcept
	{
		return &entry_;
	}

	recursive_directory_iterator& operator++();
	recursive_directory_iterator operator++(int);

	[[nodiscard]] int depth() const noexcept;
	[[nodiscard]] bool recursion_pending() const noexcept;
	void disable_recursion_pending() noexcept;
	[[nodiscard]] std::filesystem::directory_options options() const noexcept
	{
		return options_;
	}

	friend bool operator==(const recursive_directory_iterator& a,
	                       const recursive_directory_iterator& b);

private:
	void advance_archive();

	bool valid_{false};
	std::filesystem::directory_options options_{
	        std::filesystem::directory_options::none};
	std::optional<std::filesystem::recursive_directory_iterator> host_{};
	std::vector<detail::archive_cursor> stack_{}; // archive: one frame per
	                                              // open directory
	bool pending_{true}; // archive: may descend into the current entry
	directory_entry entry_{};
};

// Range-for support, so `for (const auto& e : directory_iterator(p, ec))` works
// as with std.
inline directory_iterator begin(directory_iterator it) noexcept
{
	return it;
}
inline directory_iterator end(const directory_iterator&) noexcept
{
	return {};
}
inline recursive_directory_iterator begin(recursive_directory_iterator it) noexcept
{
	return it;
}
inline recursive_directory_iterator end(const recursive_directory_iterator&) noexcept
{
	return {};
}

} // namespace rwz::fs
