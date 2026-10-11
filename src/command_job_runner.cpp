#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
// Spawned without SETPGROUP. This fresh executable, not a forked C++ thread,
// establishes the private terminal before replacing itself with the command.
int main(int argc, char **argv) {
  unsigned char stage = 1;
  auto failure = [&](int error) {
    const unsigned char tag = 2;
    (void)::write(3, &tag, sizeof(tag));
    (void)::write(3, &stage, sizeof(stage));
    (void)::write(3, &error, sizeof(error));
    _exit(127);
  };
  if (argc < 3)
    failure(EINVAL);
  stage = 2;
  if (::setsid() < 0)
    failure(errno);
  stage = 3;
  const int slave = ::open(argv[1], O_RDWR);
  if (slave < 0)
    failure(errno);
  stage = 4;
  if (::ioctl(slave, TIOCSCTTY, 0) < 0 || ::tcsetpgrp(slave, ::getpgrp()) < 0)
    failure(errno);
  stage = 5;
  for (int destination = 0; destination < 3; ++destination)
    if (::dup2(slave, destination) < 0)
      failure(errno);
  if (slave > 3)
    (void)::close(slave);
  stage = 6;
  if (::fcntl(3, F_SETFD, FD_CLOEXEC) < 0)
    failure(errno);
  stage = 7;
  const unsigned char ready = 1;
  if (::write(3, &ready, sizeof(ready)) != static_cast<ssize_t>(sizeof(ready)))
    failure(errno);
  // Reconstruct an owned argument vector at this executable boundary. No shell
  // interpretation occurs here; target and each argument remain distinct bytes.
  stage = 8;
  std::vector<std::string> arguments;
  std::vector<char *> target;
  try {
    for (int i = 2; i < argc; ++i)
      arguments.emplace_back(argv[i]);
    if (arguments.front().empty())
      failure(EINVAL);
    for (auto &argument : arguments)
      target.push_back(argument.data());
    target.push_back(nullptr);
  } catch (...) {
    failure(ENOMEM);
  }
  stage = 9;
  ::execvp(target.front(), target.data());
  failure(errno);
}
