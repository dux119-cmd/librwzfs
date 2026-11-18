#if defined(_WIN32)

#include <windows.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/win32.hpp"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;
namespace rfs    = rwz::fs;
namespace w32    = rwz::win32;

namespace {

constexpr wchar_t kFile[] = L"C:/RWZ/DIR/FILE.DAT";
constexpr wchar_t kNew[]  = L"C:/RWZ/DIR/NEW.TXT";

std::string read_all_via(void* h)
{
	char buf[64]    = {};
	std::uint32_t n = 0;
	EXPECT_TRUE(w32::read_file(h, buf, sizeof(buf), &n, nullptr));
	return std::string(buf, n);
}

} // namespace

class Win32Archive : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rfs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("DIR/FILE.DAT", "payload");
		ASSERT_TRUE(mounts::add("C:/RWZ", backend_, overlay_.path()));
	}
	void TearDown() override
	{
		mounts::clear();
		rfs::cwd::clear();
	}

	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_;
};

TEST_F(Win32Archive, ReadOnlyOpenReadsArchiveFile)
{
	void* h = w32::create_file(
	        kFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(read_all_via(h), "payload");
	EXPECT_TRUE(w32::close_handle(h));
}

TEST_F(Win32Archive, SetFilePointerOnArchiveHandle)
{
	void* h = w32::create_file(
	        kFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(w32::set_file_pointer(h, 0, nullptr, FILE_END), 7u);
	EXPECT_EQ(w32::set_file_pointer(h, 3, nullptr, FILE_BEGIN), 3u);
	EXPECT_EQ(read_all_via(h), "load");
	EXPECT_EQ(w32::set_file_pointer(h, -100, nullptr, FILE_CURRENT),
	          INVALID_SET_FILE_POINTER);
	EXPECT_TRUE(w32::close_handle(h));
}

TEST_F(Win32Archive, WriteCopiesUpAndLeavesArchiveAlone)
{
	void* h = w32::create_file(
	        kFile, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	DWORD written = 0;
	ASSERT_TRUE(::WriteFile(static_cast<HANDLE>(h), "Z", 1, &written, nullptr));
	EXPECT_TRUE(w32::close_handle(h));
	EXPECT_EQ(written, 1u);
	EXPECT_TRUE(fs::exists(overlay_.path() / "data" / "DIR" / "FILE.DAT"));
	// The archive still reads its own content.
	void* r = w32::create_file(
	        kFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	ASSERT_NE(r, INVALID_HANDLE_VALUE);
	EXPECT_EQ(read_all_via(r).substr(0, 1), "Z"); // the overlay copy shows
	                                              // the write
	EXPECT_TRUE(w32::close_handle(r));
}

TEST_F(Win32Archive, CreateNewFailsWhenFileExists)
{
	void* h = w32::create_file(kFile, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
	EXPECT_EQ(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), ERROR_FILE_EXISTS);
}

TEST_F(Win32Archive, CreateNewWritesNewOverlayFile)
{
	void* h = w32::create_file(kNew, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	DWORD written = 0;
	ASSERT_TRUE(::WriteFile(static_cast<HANDLE>(h), "new", 3, &written, nullptr));
	EXPECT_TRUE(w32::close_handle(h));
	void* r = w32::create_file(
	        kNew, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	ASSERT_NE(r, INVALID_HANDLE_VALUE);
	EXPECT_EQ(read_all_via(r), "new");
	EXPECT_TRUE(w32::close_handle(r));
}

TEST_F(Win32Archive, MissingArchiveFileIsFileNotFound)
{
	void* h = w32::create_file(L"C:/RWZ/NOPE.TXT",
	                           GENERIC_READ,
	                           FILE_SHARE_READ,
	                           nullptr,
	                           OPEN_EXISTING,
	                           0,
	                           nullptr);
	EXPECT_EQ(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_F(Win32Archive, FindFirstFileListsMergedArchiveDirectory)
{
	backend_->add_file("DIR/SUB/inner.txt", "x");
	WIN32_FIND_DATAW data{};
	void* h = w32::find_first_file(L"C:/RWZ/DIR/*", &data);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	std::set<std::wstring> names{data.cFileName};
	while (w32::find_next_file(h, &data)) {
		names.insert(data.cFileName);
	}
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_NO_MORE_FILES));
	EXPECT_TRUE(names.count(L"."));
	EXPECT_TRUE(names.count(L".."));
	EXPECT_TRUE(names.count(L"FILE.DAT"));
	EXPECT_TRUE(names.count(L"SUB"));
	EXPECT_TRUE(w32::find_close(h));
}

TEST_F(Win32Archive, FindFirstFileWithNoMatchIsFileNotFound)
{
	WIN32_FIND_DATAW data{};
	void* h = w32::find_first_file(L"C:/RWZ/DIR/*.nomatch", &data);
	EXPECT_EQ(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
}

TEST_F(Win32Archive, HostPathsPassThrough)
{
	const auto host = overlay_.path() / "host.bin";
	void* h         = w32::create_file(
	        host.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE);
	DWORD written = 0;
	ASSERT_TRUE(::WriteFile(static_cast<HANDLE>(h), "host", 4, &written, nullptr));
	EXPECT_TRUE(w32::close_handle(h));
	EXPECT_EQ(fs::file_size(host), 4u);
}

TEST_F(Win32Archive, DeleteFileHidesArchiveFile)
{
	EXPECT_TRUE(w32::delete_file(kFile));
	void* h = w32::create_file(
	        kFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	EXPECT_EQ(h, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
	EXPECT_FALSE(fs::exists(overlay_.path() / "data" / "DIR" /
	                        "FILE.DAT")); // whiteout, not a host delete
}

TEST_F(Win32Archive, DeleteFileWithoutOverlayIsWriteProtected)
{
	mounts::clear();
	ASSERT_TRUE(mounts::add("C:/RWZ", backend_, std::filesystem::path()));
	EXPECT_FALSE(w32::delete_file(kFile));
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_WRITE_PROTECT));
}

TEST_F(Win32Archive, RemoveDirectoryRemovesEmptyDirectoryOnly)
{
	ASSERT_TRUE(w32::create_directory(L"C:/RWZ/NEWDIR", nullptr));
	EXPECT_TRUE(w32::remove_directory(L"C:/RWZ/NEWDIR"));
	WIN32_FIND_DATAW data{};
	EXPECT_EQ(w32::find_first_file(L"C:/RWZ/NEWDIR/*", &data),
	          INVALID_HANDLE_VALUE);
	// DIR holds FILE.DAT and SUB, so it cannot be removed.
	EXPECT_FALSE(w32::remove_directory(L"C:/RWZ/DIR"));
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_DIR_NOT_EMPTY));
}

TEST_F(Win32Archive, DirectoryHandleNeedsBackupSemantics)
{
	void* plain = w32::create_file(L"C:/RWZ/DIR",
	                               GENERIC_READ,
	                               FILE_SHARE_READ,
	                               nullptr,
	                               OPEN_EXISTING,
	                               0,
	                               nullptr);
	EXPECT_EQ(plain, INVALID_HANDLE_VALUE);
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_ACCESS_DENIED));

	void* dir = w32::create_file(L"C:/RWZ/DIR",
	                             GENERIC_READ,
	                             FILE_SHARE_READ,
	                             nullptr,
	                             OPEN_EXISTING,
	                             FILE_FLAG_BACKUP_SEMANTICS,
	                             nullptr);
	ASSERT_NE(dir, INVALID_HANDLE_VALUE);
	char buf[4];
	std::uint32_t n = 0;
	EXPECT_FALSE(w32::read_file(dir, buf, sizeof(buf), &n, nullptr)); // directories
	                                                                  // are
	                                                                  // not
	                                                                  // read
	EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_ACCESS_DENIED));
	EXPECT_TRUE(w32::close_handle(dir));
}

TEST_F(Win32Archive, OverlayDirectoryHandleIsARealHandle)
{
	ASSERT_TRUE(w32::create_directory(L"C:/RWZ/NEWDIR", nullptr));
	void* dir = w32::create_file(L"C:/RWZ/NEWDIR",
	                             GENERIC_READ,
	                             FILE_SHARE_READ,
	                             nullptr,
	                             OPEN_EXISTING,
	                             FILE_FLAG_BACKUP_SEMANTICS,
	                             nullptr);
	ASSERT_NE(dir, INVALID_HANDLE_VALUE);
	EXPECT_TRUE(w32::close_handle(dir));
}

#endif // defined(_WIN32)
