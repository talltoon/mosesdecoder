#ifndef MERT_FILE_STREAM_H_
#define MERT_FILE_STREAM_H_

#include <fstream>
#include <streambuf>
#include <string>

class inputfilestream : public std::istream
{
protected:
  std::streambuf *m_streambuf;
  bool is_good;

public:
  explicit inputfilestream(const std::string &filePath);
  virtual ~inputfilestream();

  bool good() const { return is_good; }
  void close();
};

class outputfilestream : public std::ostream
{
protected:
  std::streambuf *m_streambuf;
  bool is_good;

public:
  explicit outputfilestream(const std::string &filePath);
  virtual ~outputfilestream();

  bool good() const { return is_good; }
  void close();
};

#endif // MERT_FILE_STREAM_H_
