#pragma once

#include "base.h"

namespace conc {

typedef std::function<void()> Task;

class Thread {
public:
  Thread(const std::string& _name, const Task& task)
    : name(_name), task_(task), th_(nullptr) {}

  ~Thread() { join(); }

  void run() {
    th_.reset(new std::thread(task_));
  }

  void join() {
    if (th_ && th_->joinable()) {
      th_->join();
      th_.reset();
    }
  }

public:
  const uint64_t id = id_gen_++;
  const std::string name;

private:
  static std::atomic<uint64_t> id_gen_;
  Task task_;
  std::shared_ptr<std::thread> th_;
};

std::atomic<uint64_t> Thread::id_gen_(0);

typedef std::shared_ptr<Thread> ThreadPtr;

class CountDownLatch {
public:
  CountDownLatch() : num_(0) {}
  CountDownLatch(int32_t n) : num_(n) {}

  void wait() {
    std::unique_lock<std::mutex> lk(mtx_);
    cond_.wait(lk, [this] { return this->num_ <= 0; });
  }

  void countDown(int32_t n) {
    std::lock_guard<std::mutex> lk(mtx_);
    num_ -= n;
    if (num_ <= 0) {
      cond_.notify_all();
    }
  }

private:
  int32_t num_;
  std::mutex mtx_;
  std::condition_variable cond_;
};

}  // namespace conc
