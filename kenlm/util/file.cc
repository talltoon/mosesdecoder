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
  BOOL ret = GetFileSizeEx(fd, &size);
  return size;

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
    ssize_t ret = read(fd, to, amount);
    if (ret == -1) UTIL_THROW(ErrnoException, "Reading " << amount << " from fd " << fd << " failed.");
    if (ret == 0) UTIL_THROW(Exception, "Hit EOF in fd " << fd << " but there should be " << amount << " more bytes to read.");
    amount -= ret;
    to += ret;
  }
}

void WriteOrThrow(FD fd, const void *data_void, std::size_t size) {
  const uint8_t *data = static_cast<const uint8_t*>(data_void);
  while (size) {
    ssize_t ret = write(fd, data, size);
    if (ret < 1) UTIL_THROW(util::ErrnoException, "Write failed");
    data += ret;
    size -= ret;
  }
}

void RemoveOrThrow(const char *name) {
  UTIL_THROW_IF(std::remove(name), util::ErrnoException, "Could not remove " << name);
}

} // namespace util
