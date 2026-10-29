#!/usr/bin/env python3

# SPDX-FileCopyrightText:  2020-2026 duxcmd <https://github.com/dux119-cmd>
# SPDX-License-Identifier: AGPL-3.0-or-later

"""
Analyze C++ project to find all filesystem and file operations.
Useful for creating abstraction layers or wrappers.
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
            
            # C++ filesystem (C++17)
            'filesystem::path': r'\b(filesystem::|std::filesystem::)path\s*\(',
            'filesystem::exists': r'\b(filesystem::|std::filesystem::)(exists|is_directory|is_regular_file|is_symlink|is_empty)\s*\(',
            'filesystem::create': r'\b(filesystem::|std::filesystem::)(create_directory|create_directories|create_symlink)\s*\(',
            'filesystem::remove': r'\b(filesystem::|std::filesystem::)(remove|remove_all)\s*\(',
            'filesystem::rename': r'\b(filesystem::|std::filesystem::)rename\s*\(',
            'filesystem::copy': r'\b(filesystem::|std::filesystem::)copy\s*\(',
            'filesystem::current_path': r'\b(filesystem::|std::filesystem::)current_path\s*\(',
            'filesystem::directory_iterator': r'\b(filesystem::|std::filesystem::)directory_iterator\s*\(',
            'filesystem::recursive_directory_iterator': r'\b(filesystem::|std::filesystem::)recursive_directory_iterator\s*\(',
            'filesystem::absolute': r'\b(filesystem::|std::filesystem::)absolute\s*\(',
            'filesystem::canonical': r'\b(filesystem::|std::filesystem::)canonical\s*\(',
            'filesystem::temp_directory_path': r'\b(filesystem::|std::filesystem::)temp_directory_path\s*\(',
            
            # C++ streams
            'ifstream': r'\b(std::)?ifstream\s*[({]',
            'ofstream': r'\b(std::)?ofstream\s*[({]',
            'fstream': r'\b(std::)?fstream\s*[({]',
            'stringstream': r'\b(std::)?stringstream\s*[({]',
            'file_get_contents': r'\b(file_get_contents|file_put_contents)\s*\(',
            
            # Boost filesystem
            'boost_filesystem': r'\b(boost::filesystem::|fs::)(path|exists|is_directory|remove|copy|create_directories)\s*\(',
            
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
            # Skip comments
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
                        'bin', 'obj', '.vscode', '.idea', 'node_modules'}
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
