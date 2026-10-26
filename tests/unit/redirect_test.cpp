// FdRedirect in read mode: the fd reads the file while the object lives and is closed after.
#include "redirect.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace
{
  bool is_open(int fd)
  {
    return fcntl(fd, F_GETFD) != -1;
  }

  std::string write_temp_file(const std::string &content)
  {
    char path[] = "/tmp/shell-redirect-test-XXXXXX";
    int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    REQUIRE(write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size()));
    close(fd);
    return path;
  }
}

TEST_CASE("a read redirect makes the fd read the file, and closes it afterwards")
{
  const int fd = 7;
  REQUIRE_FALSE(is_open(fd)); // the test needs a free fd

  const std::string path = write_temp_file("hello");
  {
    FdRedirect redirect(fd, path, OpenMode::Read);
    REQUIRE(redirect.ok());
    char buffer[16] = {};
    REQUIRE(read(fd, buffer, sizeof buffer) == 5);
    REQUIRE(std::string(buffer, 5) == "hello");
  }
  REQUIRE_FALSE(is_open(fd));
  std::remove(path.c_str());
}

TEST_CASE("a read redirect of a missing file fails with the system's reason")
{
  FdRedirect redirect(7, "/nonexistent_dir/f", OpenMode::Read);
  REQUIRE_FALSE(redirect.ok());
  REQUIRE(redirect.error() == "/nonexistent_dir/f: No such file or directory");
  REQUIRE_FALSE(is_open(7));
}
