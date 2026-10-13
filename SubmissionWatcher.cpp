#include "SubmissionWatcher.h"

#include "Base.h"

#include <efsw/efsw.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

using namespace std::chrono;
namespace fs = std::filesystem;

/* =======================
   Thread-safe queue
   ======================= */

class SubmissionQueue {
public:
  void push(fs::path p) {
    {
      std::lock_guard<std::mutex> lock(mtx);
      q.push(std::move(p));
    }
    cv.notify_one();
  }

  bool pop(fs::path &out) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return !q.empty() || !running; });
    // Drain queued items first: stop() must not silently drop submissions
    // that were already accepted.
    if (q.empty())
      return false;
    out = std::move(q.front());
    q.pop();
    return true;
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mtx);
      running = false;
    }
    cv.notify_all();
  }

  void restart() {
    {
      std::lock_guard<std::mutex> lock(mtx);
      running = true;
    }
    cv.notify_all();
  }

private:
  std::queue<fs::path> q;
  std::mutex mtx;
  std::condition_variable cv;
  bool running = true;
};

/* =======================
   Listener
   ======================= */

class Listener final : public efsw::FileWatchListener {
public:
  explicit Listener(SubmissionQueue &q) : queue(q) {}

  void handleFileAction(efsw::WatchID, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        const std::string & /*oldFilename*/) override {
    if (action != efsw::Actions::Add && action != efsw::Actions::Modified)
      return;

    fs::path p = fs::path(dir) / filename;
    if (!iequals(p.extension().string(), ".cpp"))
      return;

    // debounce (bounded: drop the table instead of growing forever)
    auto now = steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(debounceMtx);
      if (seen.size() > 10000)
        seen.clear();
      auto &last = seen[p.string()];
      if (now - last < milliseconds(500))
        return;
      last = now;
    }

    queue.push(std::move(p));
  }

private:
  SubmissionQueue &queue;
  std::unordered_map<std::string, steady_clock::time_point> seen;
  std::mutex debounceMtx;
};

/* =======================
   Impl
   ======================= */

struct SubmissionWatcher::Impl {
  fs::path dir;
  Callback callback;

  SubmissionQueue queue;
  std::unique_ptr<efsw::FileWatcher> watcher;
  std::unique_ptr<Listener> listener;
  efsw::WatchID watchId{};

  std::thread worker;
  std::atomic<bool> running{false};

  Impl(const fs::path &d, Callback cb) : dir(d), callback(std::move(cb)) {}

  void start() {
    if (running.exchange(true))
      return; // already started; joining/replacing the thread would terminate
    queue.restart();
    watcher = std::make_unique<efsw::FileWatcher>();
    listener = std::make_unique<Listener>(queue);

    watchId = watcher->addWatch(dir.string(), listener.get(), true);
    watcher->watch();

    worker = std::thread([this] {
      fs::path p;
      // pop() drains already-queued items even after stop(); it returns
      // false only once the queue is empty and shutdown.
      while (queue.pop(p)) {
        // An escaping exception here would terminate the whole process.
        try {
          callback(p);
        } catch (...) {
        }
      }
    });
  }

  void stop() {
    running = false;
    queue.shutdown();

    if (watcher) {
      watcher->removeWatch(watchId);
      // Destroying the FileWatcher stops its internal thread; removeWatch
      // alone does not.
      watcher.reset();
      listener.reset();
    }

    if (worker.joinable())
      worker.join();
  }
  // Join the worker thread. If start() was never called this is a no-op;
  // if the watcher is still running this blocks until stop() is called
  // from another thread.
  void wait() {
    if (worker.joinable())
      worker.join();
  }
};

/* =======================
   Public API
   ======================= */

SubmissionWatcher::SubmissionWatcher(const fs::path &dir, Callback cb)
    : impl(new Impl(dir, std::move(cb))) {}

SubmissionWatcher::~SubmissionWatcher() {
  stop();
  delete impl;
}

void SubmissionWatcher::start() { impl->start(); }

void SubmissionWatcher::stop() { impl->stop(); }

void SubmissionWatcher::wait() { impl->wait(); }
