#pragma once

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

class SubmissionWatcher {
public:
  using Callback = std::function<void(const fs::path &)>;

  SubmissionWatcher(const fs::path &dir, Callback cb);
  ~SubmissionWatcher();

  void start();
  void stop();
  void wait();

private:
  struct Impl;
  Impl *impl;
};
