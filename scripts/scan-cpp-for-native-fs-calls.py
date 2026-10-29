#!/usr/bin/env python3

# SPDX-FileCopyrightText:  2020-2026 duxcmd <https://github.com/dux119-cmd>
# SPDX-License-Identifier: AGPL-3.0-or-later

"""
Analyze C++ project to find all filesystem and file operations.
Useful for creating abstraction layers or wrappers.
Supports C++20/23 std::filesystem and std_fs:: aliases.
"""

import os
import re
import sys
from collections import defaultdict
from pathlib import Path

class FilesystemCallAnalyzer:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.calls_found = defaultdict(list)  # {function_name: [(file, line_num, context), ...]}
        
        # Regex patterns for common filesystem operations
        # Using (std::filesystem::|std_fs::|filesystem::)? to catch both forms
        self.patterns = {
            # POSIX file operations
            'open': r'\b(open|openat)\s*\(',
            'creat': r'\b(creat|creat64)\s*\(',
            'stat': r'\b(stat|lstat|fstat|fstatat|stat64)\s*\(',
            'access': r'\b(access|faccessat)\s*\(',
            'read': r'\b(read|pread|pread64|readv|preadv)\s*\(',
            'write': r'\b(write|pwrite|pwrite64|writev|pwritev)\s*\(',
            'seek': r'\b(lseek|lseek64|seek)\s*\(',
            'close': r'\b(close|fclose)\s*\(',
            'unlink': r'\b(unlink|unlinkat)\s*\(',
            'rename': r'\b(rename|renameat|renameat2)\s*\(',
            'mkdir': r'\b(mkdir|mkdirat)\s*\(',
            'rmdir': r'\b(rmdir)\s*\(',
            'chdir': r'\b(chdir|fchdir)\s*\(',
            'getcwd': r'\b(getcwd|get_current_dir_name)\s*\(',
            'realpath': r'\b(realpath|realpath64)\s*\(',
            'glob': r'\b(glob|glob64)\s*\(',
            'opendir': r'\b(opendir|fdopendir)\s*\(',
            'readdir': r'\b(readdir|readdir64|readdir_r)\s*\(',
            'closedir': r'\b(closedir)\s*\(',
            'scandir': r'\b(scandir|scandir64)\s*\(',
            'listdir': r'\b(listdir)\s*\(',
            'chmod': r'\b(chmod|fchmod|fchmodat)\s*\(',
            'chown': r'\b(chown|fchown|lchown|fchownat)\s*\(',
            'link': r'\b(link|linkat|symlink|symlinkat|readlink|readlinkat)\s*\(',
            'truncate': r'\b(truncate|ftruncate)\s*\(',
            'fsync': r'\b(fsync|fdatasync|sync)\s*\(',
            'ioctl': r'\b(ioctl|fcntl)\s*\(',
            'mmap': r'\b(mmap|mmap64|munmap)\s*\(',
            'fopen': r'\b(fopen|fopen64|fdopen|freopen)\s*\(',
            'fread': r'\b(fread|fwrite|fgets|fgetc|fputs|fputc|fscanf|fprintf)\s*\(',
            'ftell': r'\b(ftell|ftello|ftello64|fseek|fseeko|fseeko64)\s*\(',
            'fflush': r'\b(fflush)\s*\(',
            'remove': r'\b(remove)\s*\(',
            
            # C++17/20/23 std::filesystem path object
            'fs::path': r'\b(std::filesystem::|std_fs::|filesystem::)?path\s*[({<]',
            
            # C++17/20/23 std::filesystem::path member functions
            'fs::path::operator/': r'\bpath\s*/.|\boperator/\s*\(',
            'fs::path::append': r'\b(std::filesystem::|std_fs::|filesystem::)?path::append|\.append\s*\(',
            'fs::path::concat': r'\b(std::filesystem::|std_fs::|filesystem::)?path::concat|\.concat\s*\(',
            'fs::path::remove_filename': r'\b(std::filesystem::|std_fs::|filesystem::)?path::remove_filename|\.remove_filename\s*\(',
            'fs::path::replace_filename': r'\b(std::filesystem::|std_fs::|filesystem::)?path::replace_filename|\.replace_filename\s*\(',
            'fs::path::replace_extension': r'\b(std::filesystem::|std_fs::|filesystem::)?path::replace_extension|\.replace_extension\s*\(',
            'fs::path::filename': r'\.filename\s*\(',
            'fs::path::extension': r'\.extension\s*\(',
            'fs::path::stem': r'\.stem\s*\(',
            'fs::path::parent_path': r'\.parent_path\s*\(',
            'fs::path::root_path': r'\.root_path\s*\(',
            'fs::path::root_name': r'\.root_name\s*\(',
            'fs::path::relative_path': r'\.relative_path\s*\(',
            'fs::path::string': r'\.string\s*\(',
            'fs::path::generic_string': r'\.generic_string\s*\(',
            'fs::path::u8string': r'\.u8string\s*\(',
            'fs::path::wstring': r'\.wstring\s*\(',
            'fs::path::c_str': r'\.c_str\s*\(',
            'fs::path::lexically_normal': r'\.lexically_normal\s*\(',
            'fs::path::lexically_relative': r'\.lexically_relative\s*\(',
            'fs::path::lexically_proximate': r'\.lexically_proximate\s*\(',
            'fs::path::is_absolute': r'\.is_absolute\s*\(',
            'fs::path::is_relative': r'\.is_relative\s*\(',
            
            # C++17/20/23 std::filesystem directory/status operations
            'fs::exists': r'\b(std::filesystem::|std_fs::|filesystem::)?exists\s*\(',
            'fs::is_directory': r'\b(std::filesystem::|std_fs::|filesystem::)?is_directory\s*\(',
            'fs::is_regular_file': r'\b(std::filesystem::|std_fs::|filesystem::)?is_regular_file\s*\(',
            'fs::is_symlink': r'\b(std::filesystem::|std_fs::|filesystem::)?is_symlink\s*\(',
            'fs::is_empty': r'\b(std::filesystem::|std_fs::|filesystem::)?is_empty\s*\(',
            'fs::is_block_file': r'\b(std::filesystem::|std_fs::|filesystem::)?is_block_file\s*\(',
            'fs::is_character_file': r'\b(std::filesystem::|std_fs::|filesystem::)?is_character_file\s*\(',
            'fs::is_fifo': r'\b(std::filesystem::|std_fs::|filesystem::)?is_fifo\s*\(',
            'fs::is_socket': r'\b(std::filesystem::|std_fs::|filesystem::)?is_socket\s*\(',
            'fs::is_other': r'\b(std::filesystem::|std_fs::|filesystem::)?is_other\s*\(',
            'fs::status': r'\b(std::filesystem::|std_fs::|filesystem::)?status\s*\(',
            'fs::symlink_status': r'\b(std::filesystem::|std_fs::|filesystem::)?symlink_status\s*\(',
            'fs::file_status': r'\b(std::filesystem::|std_fs::|filesystem::)?file_status\s*\(',
            
            # C++17/20/23 std::filesystem file operations
            'fs::create_directory': r'\b(std::filesystem::|std_fs::|filesystem::)?create_directory\s*\(',
            'fs::create_directories': r'\b(std::filesystem::|std_fs::|filesystem::)?create_directories\s*\(',
            'fs::create_symlink': r'\b(std::filesystem::|std_fs::|filesystem::)?create_symlink\s*\(',
            'fs::create_hard_link': r'\b(std::filesystem::|std_fs::|filesystem::)?create_hard_link\s*\(',
            'fs::remove': r'\b(std::filesystem::|std_fs::|filesystem::)?remove\s*\(',
            'fs::remove_all': r'\b(std::filesystem::|std_fs::|filesystem::)?remove_all\s*\(',
            'fs::rename': r'\b(std::filesystem::|std_fs::|filesystem::)?rename\s*\(',
            'fs::copy': r'\b(std::filesystem::|std_fs::|filesystem::)?copy\s*\(',
            'fs::copy_file': r'\b(std::filesystem::|std_fs::|filesystem::)?copy_file\s*\(',
            'fs::copy_symlink': r'\b(std::filesystem::|std_fs::|filesystem::)?copy_symlink\s*\(',
            'fs::resize_file': r'\b(std::filesystem::|std_fs::|filesystem::)?resize_file\s*\(',
            
            # C++17/20/23 std::filesystem path queries
            'fs::current_path': r'\b(std::filesystem::|std_fs::|filesystem::)?current_path\s*\(',
            'fs::absolute': r'\b(std::filesystem::|std_fs::|filesystem::)?absolute\s*\(',
            'fs::canonical': r'\b(std::filesystem::|std_fs::|filesystem::)?canonical\s*\(',
            'fs::weakly_canonical': r'\b(std::filesystem::|std_fs::|filesystem::)?weakly_canonical\s*\(',
            'fs::proximate': r'\b(std::filesystem::|std_fs::|filesystem::)?proximate\s*\(',
            'fs::relative': r'\b(std::filesystem::|std_fs::|filesystem::)?relative\s*\(',
            'fs::temp_directory_path': r'\b(std::filesystem::|std_fs::|filesystem::)?temp_directory_path\s*\(',
            'fs::equivalent': r'\b(std::filesystem::|std_fs::|filesystem::)?equivalent\s*\(',
            
            # C++17/20/23 std::filesystem file info
            'fs::file_size': r'\b(std::filesystem::|std_fs::|filesystem::)?file_size\s*\(',
            'fs::hard_link_count': r'\b(std::filesystem::|std_fs::|filesystem::)?hard_link_count\s*\(',
            'fs::last_write_time': r'\b(std::filesystem::|std_fs::|filesystem::)?last_write_time\s*\(',
            'fs::permissions': r'\b(std::filesystem::|std_fs::|filesystem::)?permissions\s*\(',
            'fs::perm_options': r'\b(std::filesystem::|std_fs::|filesystem::)?perm_options\s*\(',
            'fs::space': r'\b(std::filesystem::|std_fs::|filesystem::)?space\s*\(',
            'fs::read_symlink': r'\b(std::filesystem::|std_fs::|filesystem::)?read_symlink\s*\(',
            
            # C++17/20/23 std::filesystem iterators
            'fs::directory_iterator': r'\b(std::filesystem::|std_fs::|filesystem::)?directory_iterator\s*[({]',
            'fs::recursive_directory_iterator': r'\b(std::filesystem::|std_fs::|filesystem::)?recursive_directory_iterator\s*[({]',
            'fs::directory_entry': r'\b(std::filesystem::|std_fs::|filesystem::)?directory_entry\s*[({]',
            
            # C++ streams
            'ifstream': r'\b(std::)?ifstream\s*[({]',
            'ofstream': r'\b(std::)?ofstream\s*[({]',
            'fstream': r'\b(std::)?fstream\s*[({]',
            'stringstream': r'\b(std::)?stringstream\s*[({]',
            
            # Windows-specific
            'CreateFile': r'\b(CreateFile|CreateFileA|CreateFileW)\s*\(',
            'ReadFile': r'\b(ReadFile|ReadFileEx|WriteFile|WriteFileEx)\s*\(',
            'SetFilePointer': r'\b(SetFilePointer|SetFilePointerEx|GetFileSize|GetFileSizeEx)\s*\(',
            'FindFirstFile': r'\b(FindFirstFile|FindFirstFileA|FindFirstFileW|FindNextFile|FindClose)\s*\(',
            'CreateDirectory': r'\b(CreateDirectory|CreateDirectoryA|CreateDirectoryW|RemoveDirectory)\s*\(',
            'GetCurrentDirectory': r'\b(GetCurrentDirectory|SetCurrentDirectory|GetFullPathName)\s*\(',
        }
    
    def analyze_file(self, filepath):
        """Analyze a single C++ file for filesystem calls."""
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Warning: Could not read {filepath}: {e}", file=sys.stderr)
            return
        
        relative_path = filepath.relative_to(self.project_root)
        
        for line_num, line in enumerate(lines, 1):
            # Skip comment-only lines
            if line.strip().startswith('//'):
                continue
            
            # Check each pattern
            for call_name, pattern in self.patterns.items():
                if re.search(pattern, line):
                    context = line.strip()
                    self.calls_found[call_name].append({
                        'file': str(relative_path),
                        'line': line_num,
                        'context': context[:100]  # First 100 chars
                    })
    
    def crawl_project(self):
        """Recursively crawl the project directory."""
        cpp_extensions = {'.cpp', '.cc', '.cxx', '.c++', '.h', '.hpp', '.hxx', '.h++', '.c'}
        
        for root, dirs, files in os.walk(self.project_root):
            # Skip common non-source directories
            skip_dirs = {'.git', '.svn', 'build', 'Debug', 'Release', 
                        'bin', 'obj', '.vscode', '.idea', 'node_modules',
                        'CMakeFiles', '.cmake'}
            dirs[:] = [d for d in dirs if d not in skip_dirs]
            
            for file in files:
                if Path(file).suffix.lower() in cpp_extensions:
                    filepath = Path(root) / file
                    self.analyze_file(filepath)
    
    def generate_report(self):
        """Generate a summary report of all filesystem calls found."""
        report = []
        report.append("=" * 80)
        report.append("FILESYSTEM/FILE OPERATION ANALYSIS REPORT")
        report.append("=" * 80)
        report.append("")
        
        # Summary
        total_calls = sum(len(v) for v in self.calls_found.values())
        unique_calls = len(self.calls_found)
        
        report.append(f"Total filesystem calls found: {total_calls}")
        report.append(f"Unique call types: {unique_calls}")
        report.append("")
        report.append("-" * 80)
        report.append("SUMMARY OF UNIQUE CALLS (sorted by frequency)")
        report.append("-" * 80)
        
        # Sort by frequency
        sorted_calls = sorted(
            self.calls_found.items(),
            key=lambda x: len(x[1]),
            reverse=True
        )
        
        for call_name, occurrences in sorted_calls:
            report.append(f"\n{call_name}: {len(occurrences)} occurrences")
            
            # Show first 3 occurrences
            for occ in occurrences[:3]:
                report.append(f"  • {occ['file']}:{occ['line']}")
                report.append(f"    {occ['context']}")
            
            if len(occurrences) > 3:
                report.append(f"  ... and {len(occurrences) - 3} more")
        
        report.append("")
        report.append("-" * 80)
        report.append("DETAILED LISTING BY FILE")
        report.append("-" * 80)
        
        # Group by file
        files_with_calls = defaultdict(list)
        for call_name, occurrences in self.calls_found.items():
            for occ in occurrences:
                files_with_calls[occ['file']].append(
                    (call_name, occ['line'], occ['context'])
                )
        
        for filepath in sorted(files_with_calls.keys()):
            calls = sorted(files_with_calls[filepath], key=lambda x: x[1])
            report.append(f"\n{filepath} ({len(calls)} calls)")
            for call_name, line_num, context in calls:
                report.append(f"  Line {line_num}: {call_name}")
                report.append(f"    {context}")
        
        return "\n".join(report)
    
    def get_unique_calls_list(self):
        """Return just the sorted list of unique calls."""
        return sorted(self.calls_found.keys(), key=lambda x: -len(self.calls_found[x]))


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <project_root_path> [output_file.txt]")
        print(f"Example: {sys.argv[0]} . report.txt")
        sys.exit(1)
    
    project_root = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not os.path.isdir(project_root):
        print(f"Error: {project_root} is not a directory", file=sys.stderr)
        sys.exit(1)
    
    print(f"Analyzing C++ project in: {project_root}")
    analyzer = FilesystemCallAnalyzer(project_root)
    analyzer.crawl_project()
    
    report = analyzer.generate_report()
    
    print(report)
    
    if output_file:
        with open(output_file, 'w') as f:
            f.write(report)
        print(f"\nReport saved to: {output_file}")
    
    # Print quick summary
    print("\n" + "=" * 80)
    print("UNIQUE CALLS TO WRAP (in order of frequency):")
    print("=" * 80)
    for call in analyzer.get_unique_calls_list():
        count = len(analyzer.calls_found[call])
        print(f"  {count:3d}x  {call}")


if __name__ == '__main__':
    main()
