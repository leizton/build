#pragma once

#include "base.h"

inline uint64_t now() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

#define RESET       "\033[0m"
#define BLACK       "\033[30m"         // Black
#define RED         "\033[31m"         // Red
#define GREEN       "\033[32m"         // Green
#define YELLOW      "\033[33m"         // Yellow
#define BLUE        "\033[34m"         // Blue
#define MAGENTA     "\033[35m"         // Magenta
#define CYAN        "\033[36m"         // Cyan
#define WHITE       "\033[37m"         // White

#define loginfo std::cout << "\033[37m[I" << now()-g_start_ts << "] "
#define logwarn std::cout << "\033[33m[W" << now()-g_start_ts << "] "
#define logerr  std::cout << "\033[31m[E" << now()-g_start_ts << "] "
#define logendl "\033[0m" << std::endl;

static uint64_t g_start_ts = now();

#define OSS(ss) std::ostringstream ss; ss

void fail(const std::string& msg) {
  logerr << msg << RESET << logendl;
  exit(1);
}

void ymlFail(const std::string& msg, const std::string& yml_path, int line_no) {
  OSS(ss) << "[" << yml_path << ":" << line_no << " parse fail] " << msg;
  fail(ss.str());
}

#define print(expr) loginfo << std::boolalpha << (expr) << logendl;

struct CStr {
  const char* s;
  const int n;
  CStr() : s(""), n(0) {}
  CStr(const char* s_) : s(s_), n(::strlen(s_)) {}
  CStr(const std::string& s_) : s(s_.c_str()), n(s_.length()) {}
};

struct Str {
  std::string s;

  Str() {}
  Str(const std::string& str) : s(str) {}
  Str(std::string&& str) : s(str) {}

  int len() { return (int)s.length(); }
  bool empty() { return s.empty(); }

  char operator[](int i) const { return s[i]; }
  char& operator[](int i) { return s[i]; }

  int find(char c, int p = 0) {
    size_t i = s.find(c, p);
    return i == std::string::npos ? -1 : int(i);
  }
  int find(const std::string& str, int p = 0) {
    size_t i = s.find(str, p);
    return i == std::string::npos ? -1 : int(i);
  }
  int rfind(char c, int p = -1) {
    if (p < 0) p = len();
    size_t i = s.rfind(c, p);
    return i == std::string::npos ? -1 : i;
  }
  int rfind(const std::string& str, int p = -1) {
    if (p < 0) p = len();
    size_t i = s.rfind(str, p);
    return i == std::string::npos ? -1 : i;
  }

  void trimLeft() {
    int i = 0;
    for (; isBlank(s[i]); i++);
    if (i > 0) {
      s = s.substr(i);
    }
  }

  void trimRight() {
    int i = len() - 1;
    for (; i >= 0 && isBlank(s[i]); i--);
    if (++i < len()) {
      s = s.substr(0, i);
    }
  }

  void trim() {
    if (empty()) return;
    trimLeft();
    trimRight();
  }

  bool startsWith(const std::string& prefix) {
    if (s.length() < prefix.length()) return false;
    return strncmp(s.c_str(), prefix.c_str(), prefix.length()) == 0;
  }

  bool endsWith(const std::string& prefix) {
    const int n = (int)prefix.length();
    if (len() < n) return false;
    return strncmp(s.c_str() + (len() - n), prefix.c_str(), n) == 0;
  }

  std::string sub(int begin, int end) {
    return s.substr(begin, end - begin);
  }

  static bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }
};
