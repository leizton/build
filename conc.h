#pragma once

#include "base.h"

namespace conc {

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

typedef std::function<void()> Task;

class Thread {
public:
  Thread(const Task& task)
    : name("_"), task_(task), th_(nullptr) {}

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
  const std::string name;

private:
  Task task_;
  std::shared_ptr<std::thread> th_;
};

typedef std::shared_ptr<Thread> ThreadPtr;

class ThreadGroup {
public:
  ThreadGroup(const std::string _name)
    : name(_name) {}

  void addTask(const Task& task) {
    auto th = std::make_shared<Thread>(
      name + "-" + std::to_string(next_thread_id_++), task);
    threads_.push_back(th);
    th->run();
  }

  void wait() {
    for (auto& th : threads_) {
      th->join();
    }
  }

public:
  const std::string name;

private:
  int next_thread_id_ = 0;
  std::vector<ThreadPtr> threads_;
};

}  // namespace conc
