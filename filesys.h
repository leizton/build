#pragma once

#include "base.h"

namespace filesys {

struct FileInfo {
  const std::string path;
  const bool is_exist = false;
  const bool is_file = false;
  const bool is_dir = false;
  const int size = 0;
  const uint64_t last_mod_sec = 0;

  explicit FileInfo(const std::string& path_) : path(path_) {
    init();
  }

  void traverseSubFiles(std::function<void(const std::string&)> udf) {
    if (!is_dir) return;
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return;

    struct dirent* e = nullptr;
    while ((e = ::readdir(dir)) != nullptr) {
      udf(e->d_name);
    }
    ::closedir(dir);
  }

private:
  void init() {
    struct stat st;
    if (::stat(path.c_str(), &st)) {
      return;
    }
    const_cast<bool&>(is_exist) = true;
    const_cast<bool&>(is_file) = S_ISREG(st.st_mode);
    const_cast<bool&>(is_dir) = S_ISDIR(st.st_mode);
    const_cast<int&>(size) = st.st_size;
    const_cast<uint64_t&>(last_mod_sec) = st.st_mtime;
  }
};

const std::string& getCurrPath() {
  static char buf[8192];
  static std::string path = []() -> const char* {
    if (getcwd(buf, 8192-1) == buf) return buf;
    return "";
  }();
  return path;
}

std::string getDirPath(const std::string& path, int upper = 1) {
  size_t idx = path.length();
  for (; upper > 0; upper--) {
    idx = path.rfind('/', idx);
    if (idx == std::string::npos) return "/";
    if (idx == 0) return "/";
    idx--;
  }
  return path.substr(0, idx+1);
}

std::string toAbsPath(const std::string& path) {
  if (path.empty()) return getCurrPath();
  if (path[0] == '/') return path;

  int upper = 0, idx = 0;
  int len = path.length();
  int len1 = len - 1;
  while (idx < len) {
    if (path[idx] == '/') {
      idx++;
      continue;
    }
    if (path[idx] != '.') {
      break;
    }
    if (idx == len1) {
      idx++;
      break;
    }

    if (path[idx+1] == '.') {
      upper++;
      idx += 2;
      while (idx < len && path[idx] == '.') idx++;
    }
    else if (path[idx+1] == '/') {
      idx += 2;
    }
    else {
      break;
    }
  }

  if (upper == 0) {
    if (idx >= len) return getCurrPath();
    return getCurrPath() + "/" + path;
  }

  std::string prefix = getDirPath(getCurrPath(), upper);
  if (idx >= len) return prefix;
  return prefix + "/" + path.substr(idx);
}

std::string getFileName(const std::string& path) {
  auto idx = path.rfind('/');
  if (idx == path.length()-1) {
    if (idx == 0) return "";
    if (--idx == 0) return "";
    idx = path.rfind('/', idx);
  }
  return (idx == std::string::npos) ? "" : path.substr(idx+1);
}

bool isExist(const std::string& path) {
  return access(path.c_str(), F_OK) == 0;
}

bool isReadable(const std::string& path) {
  return access(path.c_str(), F_OK | R_OK) == 0;
}

/**
 * @return
 *   0  fail
 */
uint64_t lastModTimeSec(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st)) {
    return 0;
  }
  return st.st_mtime;
}

/**
 * @param
 *   _p  create intermediate directories as required
 * @return
 *    0  succ;
 *    1  already exist;
 *   -1  fail;
 */
int mkdir(const std::string& path, bool _p = false) {
  int ret = ::mkdir(path.c_str(), S_IRWXU|S_IRWXG);
  if (ret == 0) return 0;
  if (access(path.c_str(), F_OK|X_OK) == 0) return 0;
  if (!_p) return -1;

  auto p1 = getDirPath(path);
  if (p1.empty() || isExist(p1)) return ret;
  if ((ret = mkdir(p1, true)) != 0) return ret;
  return ::mkdir(path.c_str(), S_IRWXU|S_IRWXG);
}

int mkdirp(const std::string& path) {
  return mkdir(path, true);
}

}  // namespace filesys
