#include "util/file.hh"

#include "util/exception.hh"
#include "util/portability.hh"

#include <cstdlib>
#include <cstdio>
#include <iostream>

#include <fcntl.h>

namespace util {

scoped_fd::~scoped_fd() {
#ifdef WIN32
  BOOL ret = CloseHandle(fd_);
  if (ret == 0) {
    std::cerr << "Could not close file " << fd_ << std::endl;
    std::abort();
  }
#else
  if (fd_ != -1 && close(fd_)) {
    std::cerr << "Could not close file " << fd_ << std::endl;
    std::abort();
  }
#endif
}

scoped_FILE::~scoped_FILE() {
  if (file_ && std::fclose(file_)) {
    std::cerr << "Could not close file " << std::endl;
    std::abort();
  }
}

FD OpenReadOrThrow(const char *name) {
  FD ret;
#ifdef WIN32
  ret = CreateFileA(name,               // file to open
                       GENERIC_READ,          // open for reading
                       FILE_SHARE_READ,       // share for reading
                       NULL,                  // default security
                       OPEN_EXISTING,         // existing file only
                       FILE_ATTRIBUTE_NORMAL, // normal file
                       NULL);                 // no attr. template
  UTIL_THROW_IF(ret == INVALID_HANDLE_VALUE, ErrnoException, "while opening " << name);

#else
  UTIL_THROW_IF(-1 == (ret = open(name, O_RDONLY)), ErrnoException, "while opening " << name);
#endif
  return ret;
}

FD CreateOrThrow(const char *name) {
  FD ret;
#ifdef WIN32
  ret = CreateFileA(name,                // name of the write
                      GENERIC_WRITE,          // open for writing
                      0,                      // do not share
                      NULL,                   // default security
                      CREATE_NEW,             // create new file only
                      FILE_ATTRIBUTE_NORMAL,  // normal file
                      NULL);                  // no attr. template
  UTIL_THROW_IF(ret == INVALID_HANDLE_VALUE, ErrnoException, "while opening " << name);

#else
  UTIL_THROW_IF(-1 == (ret = open(name, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR)), ErrnoException, "while creating " << name);
#endif

  return ret;
}

OFF_T SizeFile(FD fd) {
#if WIN32
  LARGE_INTEGER size;
  if (!GetFileSizeEx(fd, &size)) return kBadSize;
  return static_cast<OFF_T>(size);
#else
  struct stat sb;
  fstat(fd, &sb);
  if (fstat(fd, &sb) == -1 || (!sb.st_size && !S_ISREG(sb.st_mode))) return kBadSize;
  return sb.st_size;
#endif
}

void ReadOrThrow(FD fd, void *to_void, std::size_t amount) {
  uint8_t *to = static_cast<uint8_t*>(to_void);

  while (amount) {
#ifdef WIN32
    DWORD bytes_read;
    UTIL_THROW_IF(!ReadFile(fd, to, amount, &bytes_read, NULL), ErrnoException, "Reading " << amount << " from fd " << fd << " failed.");
#else
    ssize_t bytes_read = read(fd, to, amount);
    UTIL_THROW_IF(bytes_read == -1, ErrnoException, "Reading " << amount << " from fd " << fd << " failed.");
#endif
    UTIL_THROW_IF(bytes_read == 0, EndOfFileException, "Hit EOF in fd " << fd << " but there should be " << amount << " more bytes to read.");
    amount -= bytes_read;
    to += bytes_read;
  }
}

void WriteOrThrow(FD fd, const void *data_void, std::size_t size) {
  const uint8_t *data = static_cast<const uint8_t*>(data_void);
  while (size) {
#ifdef WIN32
    DWORD bytes_written;
    UTIL_THROW_IF(!WriteFile(fd, data, size, &bytes_written, NULL), ErrnoException, "Write failed");
    UTIL_THROW_IF(!bytes_written, util::ErrnoException, "Short write");
#else
    ssize_t bytes_written = write(fd, data, size);
    UTIL_THROW_IF(bytes_written < 1, util::ErrnoException, "Write failed");
#endif
    data += bytes_written;
    size -= bytes_written;
  }
}

void RemoveOrThrow(const char *name) {
  UTIL_THROW_IF(std::remove(name), util::ErrnoException, "Could not remove " << name);
}

} // namespace util
