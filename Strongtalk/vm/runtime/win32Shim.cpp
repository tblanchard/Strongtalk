/* Copyright (c) 2006, Sun Microsystems, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
	  disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Sun Microsystems nor the names of its contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE


*/

// Win32 compatibility for the image on non-Windows hosts.
//
// The image calls the operating system through DLL calls to KERNEL32, USER32, GDI32 and
// COMDLG32 (see Platform>>DLLMapName:). On other hosts os::dll_load / os::dll_lookup
// resolve KERNEL32 functions to the POSIX based implementations below (with Win32
// semantics: HANDLEs, BOOL results, 32-bit DWORD out parameters, GetLastError codes).
// The GUI libraries are not available (yet): their functions are not found, so the image
// reports which one it needs.

#include "platform.hpp"
#ifndef _WIN32

#include "win32Shim.hpp"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <copyfile.h>
#include <pthread.h>

// set to see which GUI functions the image uses
static bool TraceGUIStubs = getenv("STRONGTALK_TRACE_GUI") != NULL;
// set to see which files the image accesses
static bool TraceFiles = getenv("STRONGTALK_TRACE_FILES") != NULL;

typedef intptr_t word;		// all DLL arguments and results are words
typedef uint32_t DWORD;

// ---------------------------------------------------------------------------
// Command line (set by main)

static int    shim_argc = 0;
static char** shim_argv = NULL;

void win32_shim_set_arguments(int argc, char** argv) {
  shim_argc = argc;
  shim_argv = argv;
}

// ---------------------------------------------------------------------------
// Errors

static DWORD last_error = 0;

enum {
  ERROR_SUCCESS           = 0,
  ERROR_FILE_NOT_FOUND    = 2,
  ERROR_PATH_NOT_FOUND    = 3,
  ERROR_ACCESS_DENIED     = 5,
  ERROR_INVALID_HANDLE    = 6,
  ERROR_NOT_ENOUGH_MEMORY = 8,
  ERROR_WRITE_PROTECT     = 19,
  ERROR_NO_MORE_FILES     = 18,
  ERROR_HANDLE_DISK_FULL  = 39,
  ERROR_FILE_EXISTS       = 80,
  ERROR_INVALID_PARAMETER = 87,
  ERROR_DIR_NOT_EMPTY     = 145,
  ERROR_ALREADY_EXISTS    = 183,
  ERROR_ENVVAR_NOT_FOUND  = 203
};

static void set_error_from_errno() {
  switch (errno) {
    case 0:         last_error = ERROR_SUCCESS;           break;
    case ENOENT:    last_error = ERROR_FILE_NOT_FOUND;    break;
    case ENOTDIR:   last_error = ERROR_PATH_NOT_FOUND;    break;
    case EACCES:
    case EPERM:     last_error = ERROR_ACCESS_DENIED;     break;
    case EBADF:     last_error = ERROR_INVALID_HANDLE;    break;
    case ENOMEM:    last_error = ERROR_NOT_ENOUGH_MEMORY; break;
    case EROFS:     last_error = ERROR_WRITE_PROTECT;     break;
    case ENOSPC:    last_error = ERROR_HANDLE_DISK_FULL;  break;
    case EEXIST:    last_error = ERROR_FILE_EXISTS;       break;
    case ENOTEMPTY: last_error = ERROR_DIR_NOT_EMPTY;     break;
    default:        last_error = ERROR_INVALID_PARAMETER; break;
  }
}

static inline word fail_with_errno(word result) { set_error_from_errno(); return result; }

static word GetLastError() {
  return last_error;
}

// ---------------------------------------------------------------------------
// Paths
//
// The image uses Windows paths: '\' separators, and a drive letter marks an absolute
// path (Win32FilePattern>>isAbsolute). Drive X: maps to the POSIX directory named by
// the environment variable STRONGTALK_DRIVE_X, or to the root directory by default,
// so "C:\Users\me\x" is "/Users/me/x". In the other direction, absolute POSIX paths
// given to the image (GetCurrentDirectoryA) get a drive letter.

// The POSIX directory drive letter maps to, "" for the root directory.
static const char* drive_root(char letter) {
  char var[] = "STRONGTALK_DRIVE_X";
  var[sizeof var - 2] = (char) toupper((unsigned char) letter);
  const char* root = getenv(var);
  if (root == NULL) return "";
  size_t len = strlen(root);
  while (len > 0 && root[len - 1] == '/') len--;	// "/" and "/dir/" are "" and "/dir"
  static char roots[26][PATH_MAX];
  char* r = roots[toupper((unsigned char) letter) - 'A'];
  snprintf(r, PATH_MAX, "%.*s", (int) len, root);
  return r;
}

static inline bool has_drive(const char* path) {
  return isalpha((unsigned char) path[0]) && path[1] == ':';
}

// Win32 path argument as a POSIX path.
class PosixPath {
  char _path[PATH_MAX];
 public:
  PosixPath(word name) {
    const char* win = (const char*) name;
    if (has_drive(win)) {
      const char* rest = win + 2;
      while (*rest == '\\' || *rest == '/') rest++;	// "C:x" is taken as "C:\x"
      snprintf(_path, sizeof _path, "%s/%s", drive_root(win[0]), rest);
    } else {
      strlcpy(_path, win, sizeof _path);
    }
    for (char* p = _path; *p; p++) if (*p == '\\') *p = '/';
  }
  operator const char*() const { return _path; }
};

// An absolute POSIX path as a Win32 path: the drive whose root is the longest prefix
// of path (C: if none other matches), with '\' separators. Relative paths only get '\'.
static void windows_path(const char* path, char* buffer, size_t size) {
  char drive = 0;
  size_t prefix = 0;
  if (path[0] == '/') {
    for (char letter = 'A'; letter <= 'Z'; letter++) {
      const char* root = drive_root(letter);
      size_t len = strlen(root);
      bool matches = len == 0 ? letter == 'C'	// unmapped drives other than C: are not used
                              : strncmp(path, root, len) == 0 && (path[len] == '/' || path[len] == '\0');
      if (matches && (drive == 0 || len > prefix)) { drive = letter; prefix = len; }
    }
  }
  if (drive != 0) snprintf(buffer, size, "%c:%s", drive, path[prefix] ? path + prefix : "/");
  else            strlcpy(buffer, path, size);
  for (char* p = buffer; *p; p++) if (*p == '/') *p = '\\';
}

// ---------------------------------------------------------------------------
// Process environment

extern void file_in_image_patches();	// system_prims.cpp

// The command line as one string; arguments containing blanks are quoted.
// The image reads it at startup: the image patches are filed in then (see system_prims.cpp).
static word GetCommandLineA() {
  file_in_image_patches();
  static char* line = NULL;
  if (line == NULL) {
    size_t len = 1;
    for (int i = 0; i < shim_argc; i++) len += strlen(shim_argv[i]) + 3;
    line = (char*) calloc(len, 1);
    for (int i = 0; i < shim_argc; i++) {
      if (i > 0) strcat(line, " ");
      bool quote = strchr(shim_argv[i], ' ') != NULL;
      if (quote) strcat(line, "\"");
      strcat(line, shim_argv[i]);
      if (quote) strcat(line, "\"");
    }
  }
  return (word) line;
}

// Copies a string into a Win32 style buffer: returns the length (without 0),
// or the required buffer size (with 0) if the buffer is too small.
static word copy_out(const char* value, word buffer, word size) {
  size_t len = strlen(value);
  if (len + 1 > (size_t) (DWORD) size) return (word) (len + 1);
  memcpy((char*) buffer, value, len + 1);
  return (word) len;
}

static word GetEnvironmentVariableA(word name, word buffer, word size) {
  const char* value = getenv((const char*) name);
  if (value == NULL) { last_error = ERROR_ENVVAR_NOT_FOUND; return 0; }
  return copy_out(value, buffer, size);
}

static word GetCurrentDirectoryA(word size, word buffer) {
  char dir[PATH_MAX];
  if (getcwd(dir, sizeof dir) == NULL) return fail_with_errno(0);
  char win[PATH_MAX];
  windows_path(dir, win, sizeof win);
  return copy_out(win, buffer, size);
}

static word GetCurrentThreadId() {
  uint64_t id;
  pthread_threadid_np(NULL, &id);
  return (word) (DWORD) id;
}

static word GetModuleHandleA(word name) {
  return 1;		// any non-null value: there is only one module
}

// SYSTEMTIME: wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds (16 bit each)
extern bool st_fake_time_enabled();	// os_posix.cpp
extern uint64_t st_fake_micros();

static word GetLocalTime(word system_time) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (st_fake_time_enabled()) { uint64_t us = st_fake_micros(); tv.tv_sec = (time_t) (us / 1000000); tv.tv_usec = (suseconds_t) (us % 1000000); }
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  uint16_t* st = (uint16_t*) system_time;
  st[0] = (uint16_t) (t.tm_year + 1900);
  st[1] = (uint16_t) (t.tm_mon + 1);
  st[2] = (uint16_t) t.tm_wday;
  st[3] = (uint16_t) t.tm_mday;
  st[4] = (uint16_t) t.tm_hour;
  st[5] = (uint16_t) t.tm_min;
  st[6] = (uint16_t) t.tm_sec;
  st[7] = (uint16_t) (tv.tv_usec / 1000);
  return 0;
}

// ---------------------------------------------------------------------------
// Files. HANDLEs are file descriptors + 1 (0 is never a valid handle);
// INVALID_HANDLE_VALUE is 0xFFFFFFFF.

static const word INVALID_HANDLE_VALUE = (word) (DWORD) 0xFFFFFFFF;

static inline int  fd_of(word handle) { return (int) (DWORD) handle - 1; }
static inline word handle_of(int fd)  { return (word) fd + 1; }

enum {
  GENERIC_READ  = 0x80000000u,
  GENERIC_WRITE = 0x40000000u,
  CREATE_NEW = 1, CREATE_ALWAYS = 2, OPEN_EXISTING = 3, OPEN_ALWAYS = 4, TRUNCATE_EXISTING = 5,
  FILE_BEGIN = 0, FILE_CURRENT = 1, FILE_END = 2,
  FILE_ATTRIBUTE_READONLY  = 0x01,
  FILE_ATTRIBUTE_DIRECTORY = 0x10,
  FILE_ATTRIBUTE_NORMAL    = 0x80
};

static word CreateFileA(word name, word access, word share, word security, word creation, word flags, word templ) {
  DWORD a = (DWORD) access;
  int mode;
  if ((a & GENERIC_READ) && (a & GENERIC_WRITE)) mode = O_RDWR;
  else if (a & GENERIC_WRITE)                    mode = O_WRONLY;
  else                                           mode = O_RDONLY;
  switch ((DWORD) creation) {
    case CREATE_NEW:        mode |= O_CREAT | O_EXCL;  break;
    case CREATE_ALWAYS:     mode |= O_CREAT | O_TRUNC; break;
    case OPEN_EXISTING:                                break;
    case OPEN_ALWAYS:       mode |= O_CREAT;           break;
    case TRUNCATE_EXISTING: mode |= O_TRUNC;           break;
    default: last_error = ERROR_INVALID_PARAMETER; return INVALID_HANDLE_VALUE;
  }
  struct stat st;
  bool existed = stat(PosixPath(name), &st) == 0;
  int fd = open(PosixPath(name), mode, 0666);
  if (TraceFiles) fprintf(stderr, "Strongtalk: CreateFileA(\"%s\") -> %d\n", (const char*) PosixPath(name), fd);
  if (fd < 0) return fail_with_errno(INVALID_HANDLE_VALUE);
  // CREATE_ALWAYS / OPEN_ALWAYS report an existing file via GetLastError
  last_error = (existed && ((DWORD) creation == CREATE_ALWAYS || (DWORD) creation == OPEN_ALWAYS))
             ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
  return handle_of(fd);
}

static word CloseHandle(word handle) {
  if (close(fd_of(handle)) != 0) return fail_with_errno(0);
  return 1;
}

static word ReadFile(word handle, word buffer, word count, word bytes_read, word overlapped) {
  ssize_t n = read(fd_of(handle), (void*) buffer, (size_t) (DWORD) count);
  if (n < 0) { if (bytes_read) *(DWORD*) bytes_read = 0; return fail_with_errno(0); }
  if (bytes_read) *(DWORD*) bytes_read = (DWORD) n;
  return 1;
}

static word WriteFile(word handle, word buffer, word count, word bytes_written, word overlapped) {
  ssize_t n = write(fd_of(handle), (const void*) buffer, (size_t) (DWORD) count);
  if (n < 0) { if (bytes_written) *(DWORD*) bytes_written = 0; return fail_with_errno(0); }
  if (bytes_written) *(DWORD*) bytes_written = (DWORD) n;
  return 1;
}

// Returns the low 32 bits of the new position (high bits in *distance_high if given), 0xFFFFFFFF on error.
static word SetFilePointer(word handle, word distance_low, word distance_high, word method) {
  int64_t distance = (int32_t) distance_low;
  if (distance_high) distance = ((int64_t) *(int32_t*) distance_high << 32) | (DWORD) distance_low;
  int whence = (DWORD) method == FILE_END ? SEEK_END : (DWORD) method == FILE_CURRENT ? SEEK_CUR : SEEK_SET;
  off_t pos = lseek(fd_of(handle), (off_t) distance, whence);
  if (pos < 0) return fail_with_errno((word) (DWORD) 0xFFFFFFFF);
  if (distance_high) *(int32_t*) distance_high = (int32_t) (pos >> 32);
  last_error = ERROR_SUCCESS;
  return (word) (DWORD) pos;
}

static word GetFileSize(word handle, word size_high) {
  struct stat st;
  if (fstat(fd_of(handle), &st) != 0) return fail_with_errno((word) (DWORD) 0xFFFFFFFF);
  if (size_high) *(DWORD*) size_high = (DWORD) ((uint64_t) st.st_size >> 32);
  return (word) (DWORD) st.st_size;
}

static word GetFileAttributesA(word name) {
  struct stat st;
  if (TraceFiles) fprintf(stderr, "Strongtalk: GetFileAttributesA(\"%s\")\n", (const char*) PosixPath(name));
  if (stat(PosixPath(name), &st) != 0) return fail_with_errno((word) (DWORD) 0xFFFFFFFF);
  DWORD attributes = 0;
  if (S_ISDIR(st.st_mode))      attributes |= FILE_ATTRIBUTE_DIRECTORY;
  if (!(st.st_mode & S_IWUSR))  attributes |= FILE_ATTRIBUTE_READONLY;
  return attributes ? attributes : FILE_ATTRIBUTE_NORMAL;
}

static word DeleteFileA(word name) {
  return unlink(PosixPath(name)) == 0 ? 1 : fail_with_errno(0);
}

static word MoveFileA(word from, word to) {
  struct stat st;
  if (stat(PosixPath(to), &st) == 0) { last_error = ERROR_ALREADY_EXISTS; return 0; }	// MoveFile does not replace
  return rename(PosixPath(from), PosixPath(to)) == 0 ? 1 : fail_with_errno(0);
}

static word CopyFileA(word from, word to, word fail_if_exists) {
  struct stat st;
  if (fail_if_exists && stat(PosixPath(to), &st) == 0) { last_error = ERROR_FILE_EXISTS; return 0; }
  return copyfile(PosixPath(from), PosixPath(to), NULL, COPYFILE_ALL) == 0 ? 1 : fail_with_errno(0);
}

static word CreateDirectoryA(word name, word security) {
  return mkdir(PosixPath(name), 0777) == 0 ? 1 : fail_with_errno(0);
}

static word RemoveDirectoryA(word name) {
  return rmdir(PosixPath(name)) == 0 ? 1 : fail_with_errno(0);
}

static word GetDiskFreeSpaceA(word root, word sectors_per_cluster, word bytes_per_sector, word free_clusters, word total_clusters) {
  struct statfs fs;
  if (statfs(root ? (const char*) PosixPath(root) : ".", &fs) != 0) return fail_with_errno(0);
  // report 1 sector per cluster of f_bsize bytes; clamp the counts to 32 bits
  if (sectors_per_cluster) *(DWORD*) sectors_per_cluster = 1;
  if (bytes_per_sector)    *(DWORD*) bytes_per_sector    = (DWORD) fs.f_bsize;
  if (free_clusters)       *(DWORD*) free_clusters       = (DWORD) MIN(fs.f_bavail, (uint64_t) 0xFFFFFFFF);
  if (total_clusters)      *(DWORD*) total_clusters      = (DWORD) MIN(fs.f_blocks, (uint64_t) 0xFFFFFFFF);
  return 1;
}

// ---------------------------------------------------------------------------
// Directory searches. A search HANDLE is a FindSearch*.

// WIN32_FIND_DATA field offsets (fixed layout, see WIN32_FIND_DATA>>cFileNameOffset)
enum {
  FIND_DATA_ATTRIBUTES = 0,
  FIND_DATA_SIZE_HIGH  = 28,
  FIND_DATA_SIZE_LOW   = 32,
  FIND_DATA_FILE_NAME  = 44,
  FIND_DATA_NAME_SIZE  = 260	// MAX_PATH
};

struct FindSearch {
  DIR* dir;
  char directory[PATH_MAX];	// POSIX path of the directory searched
  char pattern[PATH_MAX];	// fnmatch pattern for the names
  char bare_pattern[PATH_MAX];	// pattern without a trailing ".*" ("" if none)
};

// Fills in the next matching entry; false (ERROR_NO_MORE_FILES) at the end.
static bool find_next_entry(FindSearch* search, word data) {
  struct dirent* entry;
  while ((entry = readdir(search->dir)) != NULL) {
    if (fnmatch(search->pattern, entry->d_name, 0) != 0 &&
        (search->bare_pattern[0] == 0 || fnmatch(search->bare_pattern, entry->d_name, 0) != 0)) continue;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", search->directory, entry->d_name);
    struct stat st;
    if (stat(path, &st) != 0) continue;	// e.g. a dangling symbolic link
    char* d = (char*) data;
    memset(d, 0, FIND_DATA_FILE_NAME + FIND_DATA_NAME_SIZE);
    DWORD attributes = 0;
    if (S_ISDIR(st.st_mode))     attributes |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(st.st_mode & S_IWUSR)) attributes |= FILE_ATTRIBUTE_READONLY;
    *(DWORD*) (d + FIND_DATA_ATTRIBUTES) = attributes ? attributes : FILE_ATTRIBUTE_NORMAL;
    *(DWORD*) (d + FIND_DATA_SIZE_HIGH)  = (DWORD) ((uint64_t) st.st_size >> 32);
    *(DWORD*) (d + FIND_DATA_SIZE_LOW)   = (DWORD) st.st_size;
    strlcpy(d + FIND_DATA_FILE_NAME, entry->d_name, FIND_DATA_NAME_SIZE);
    return true;
  }
  last_error = ERROR_NO_MORE_FILES;
  return false;
}

static word FindFirstFileA(word pattern, word data) {
  PosixPath path(pattern);
  const char* p = path;
  FindSearch* search = (FindSearch*) calloc(1, sizeof(FindSearch));
  const char* slash = strrchr(p, '/');
  if (slash == NULL) {
    strlcpy(search->directory, ".", sizeof search->directory);
    strlcpy(search->pattern, p, sizeof search->pattern);
  } else {
    snprintf(search->directory, sizeof search->directory, "%.*s", (int) (slash == p ? 1 : slash - p), p);
    strlcpy(search->pattern, slash + 1, sizeof search->pattern);
  }
  // in Win32 a trailing ".*" also matches names without an extension ("*.*" matches all names)
  size_t len = strlen(search->pattern);
  if (len > 2 && strcmp(search->pattern + len - 2, ".*") == 0)
    snprintf(search->bare_pattern, sizeof search->bare_pattern, "%.*s", (int) (len - 2), search->pattern);
  else if (len == 2 && strcmp(search->pattern, ".*") == 0)
    strlcpy(search->bare_pattern, "*", sizeof search->bare_pattern);
  if (TraceFiles) fprintf(stderr, "Strongtalk: FindFirstFileA(\"%s\" in \"%s\")\n", search->pattern, search->directory);
  search->dir = opendir(search->directory);
  if (search->dir == NULL) {
    set_error_from_errno();
    free(search);
    return INVALID_HANDLE_VALUE;
  }
  if (!find_next_entry(search, data)) {
    closedir(search->dir);
    free(search);
    return INVALID_HANDLE_VALUE;	// GetLastError is ERROR_NO_MORE_FILES
  }
  return (word) search;
}

static word FindNextFileA(word handle, word data) {
  return find_next_entry((FindSearch*) handle, data) ? 1 : 0;
}

static word FindClose(word handle) {
  FindSearch* search = (FindSearch*) handle;
  closedir(search->dir);
  free(search);
  return 1;
}

// ---------------------------------------------------------------------------
// Global memory: a handle is the memory itself

enum { GMEM_ZEROINIT = 0x40 };

static word GlobalAlloc(word flags, word size) {
  void* p = ((DWORD) flags & GMEM_ZEROINIT) ? calloc(1, (size_t) (DWORD) size) : malloc((size_t) (DWORD) size);
  if (p == NULL) last_error = ERROR_NOT_ENOUGH_MEMORY;
  return (word) p;
}

static word GlobalLock(word handle)   { return handle; }
static word GlobalUnlock(word handle) { return 1; }
static word GlobalFree(word handle)   { free((void*) handle); return 0; }

// ---------------------------------------------------------------------------
// Library table

struct ShimFunction { const char* name; dll_func fn; };

#define SHIM(f) { #f, (dll_func) &f }
static const ShimFunction kernel32_functions[] = {
  SHIM(GetLastError),
  SHIM(GetCommandLineA),
  SHIM(GetEnvironmentVariableA),
  SHIM(GetCurrentDirectoryA),
  SHIM(GetModuleHandleA),
  SHIM(GetCurrentThreadId),
  SHIM(GetLocalTime),
  SHIM(CreateFileA),
  SHIM(CloseHandle),
  SHIM(ReadFile),
  SHIM(WriteFile),
  SHIM(SetFilePointer),
  SHIM(GetFileSize),
  SHIM(GetFileAttributesA),
  SHIM(DeleteFileA),
  SHIM(MoveFileA),
  SHIM(CopyFileA),
  SHIM(CreateDirectoryA),
  SHIM(RemoveDirectoryA),
  SHIM(GetDiskFreeSpaceA),
  SHIM(FindFirstFileA),
  SHIM(FindNextFileA),
  SHIM(FindClose),
  SHIM(GlobalAlloc),
  SHIM(GlobalLock),
  SHIM(GlobalUnlock),
  SHIM(GlobalFree),
  { NULL, NULL }
};
#undef SHIM

struct ShimLibrary { const char* name; const ShimFunction* functions; };

static const ShimFunction no_functions[] = { { NULL, NULL } };

static const ShimLibrary shim_libraries[] = {
  { "KERNEL32.DLL", kernel32_functions },
  { "USER32.DLL",   no_functions },		// GUI: not available yet
  { "GDI32.DLL",    no_functions },
  { "COMDLG32.DLL", no_functions },
  { NULL, NULL }
};

DLL* win32_shim_load(const char* name) {
  for (const ShimLibrary* l = shim_libraries; l->name != NULL; l++) {
    if (strcasecmp(l->name, name) == 0) return (DLL*) l;
  }
  return NULL;
}

bool win32_shim_is_library(DLL* library) {
  for (const ShimLibrary* l = shim_libraries; l->name != NULL; l++) {
    if ((DLL*) l == library) return true;
  }
  return false;
}

// Stand-in for GUI functions (USER32, GDI32, COMDLG32) until there is a native UI:
// returns a non-zero value (a fake handle / TRUE). This lets the class initializers that
// create pens, brushes and bitmaps run in command line mode.
static word gui_stub() {
  return 1;
}

dll_func win32_shim_lookup(const char* name, DLL* library) {
  const ShimLibrary* l = (const ShimLibrary*) library;
  for (const ShimFunction* f = l->functions; f->name != NULL; f++) {
    if (strcmp(f->name, name) == 0) return f->fn;
  }
  if (l->functions == no_functions) {
    if (TraceGUIStubs) fprintf(stderr, "Strongtalk: %s %s is a GUI stub (no native UI yet)\n", l->name, name);
    return (dll_func) &gui_stub;
  }
  fprintf(stderr, "Strongtalk: %s %s is not available on this platform\n", l->name, name);
  return NULL;
}

#endif // _WIN32
