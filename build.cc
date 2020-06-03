#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <memory>
#include <cstring>
#include <string>
#include <algorithm>
#include <functional>
#include <vector>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <queue>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

const char* version() {
  return VERSION;
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

inline uint64_t now() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static uint64_t g_start_ts = now();

#define OSS(ss) std::ostringstream ss; ss

void fail(const string& msg) {
  logerr << msg << RESET << logendl;
  exit(1);
}

void ymlFail(const string& msg, const std::string& yml_path, int line_no) {
  ostringstream ss;
  ss << "[" << yml_path << ":" << line_no << " parse fail] " << msg;
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

  static bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }
};

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

const string& projectDir() {
  static string path_ = filesys::getCurrPath();
  return path_;
}

const string& cppflag() {
  static string flag_ = []() {
    OSS(ss) << "g++ -std=c++11 -pthread -fpic -fPIC -O2 "
            << "-g -Werror -Wall -Wno-unused-variable -g -I"
            << projectDir();
    return ss.str();
  }();
  return flag_;
};

enum BuildType {
  STATIC = 1,
  SHARE,
  BINARY,
};

int strToBuildType(const std::string& s) {
  static map<string, BuildType> map_ {
    {"static", BuildType::STATIC},
    {"share", BuildType::SHARE},
    {"binary", BuildType::BINARY},
  };
  auto iter = map_.find(s);
  return iter == map_.end() ? 0 : iter->second;
}

std::atomic<bool> g_has_build_fail(false);
conc::CountDownLatch g_leaf_target_left;

struct TargetItem;
typedef shared_ptr<TargetItem> TargetItemPtr;

struct TargetItem {
public:
  const uint32_t id;
  const string name;
  const string obj_dir;
  const string target_path;
  BuildType build_type;
  vector<string> src;
  vector<string> lib;
  unordered_map<uint32_t, TargetItemPtr> prev;
  unordered_map<uint32_t, TargetItemPtr> next;
  int dep_level = 0;

private:
  vector<string> obj_names_;
  atomic<int> prevn_;
  conc::ThreadPtr th_;

public:
  TargetItem(int id_, const string& name_)
    : id(id_), name(name_),
      obj_dir("target/objects/" + name + "/"),
      prevn_(0), th_(nullptr)
  {}

  void init() {
    string& target_path_ref = const_cast<string&>(target_path);
    switch (build_type) {
    case BuildType::STATIC :
      target_path_ref = "target/lib/lib" + name + ".a";
      break;
    case BuildType::SHARE :
      target_path_ref = "target/lib/lib" + name + ".so";
      break;
    case BuildType::BINARY :
      target_path_ref = "target/bin/" + name;
      break;
    default:
      fail("init target fail: " + name);
    }
  }

  void addPrev(TargetItemPtr p) {
    prev[p->id] = p;
    prevn_++;
  }

  void addNext(TargetItemPtr p) {
    next[p->id] = p;
  }

  void getObjectCompileCmd(vector<string>& obj_compiles) {
    filesys::mkdirp(obj_dir);
    const std::string cmd_prefix = cppflag() + " -c -o " + obj_dir;
    ostringstream cmd;

    for (auto& cc : src) {
      auto obj_name = filesys::getFileName(cc);
      obj_name = obj_name.substr(0, obj_name.rfind('.')) + ".o";
      obj_names_.push_back(obj_name);
      uint64_t obj_mod_ts = filesys::lastModTimeSec(obj_dir + obj_name);
      if (obj_mod_ts > 0) {
        uint64_t cc_mod_ts = filesys::lastModTimeSec(cc);
        if (cc_mod_ts < obj_mod_ts) continue;
      }
      cmd.clear();
      cmd << cmd_prefix << obj_name << " " << cc;
      obj_compiles.emplace_back(cmd.str());
    }
  }

  bool buildTarget() {
    string objs;
    {
      ostringstream ss;
      for (auto& obj_name : obj_names_) {
        ss << " " << obj_dir << obj_name;
      }
      ss << " ";
      objs = ss.str();
    }

    string lib_path_param = " -L./target/lib ";
    string link_base_libs = " -lpthread ";
    string lib_dep;
    if (build_type != BuildType::STATIC) {
      ostringstream ss;
      unordered_map<uint32_t, TargetItem*> dep;
      queue<TargetItem*> que;
      for (auto& e : prev) {
        que.push(e.second.get());
      }
      while (!que.empty()) {
        auto ti = que.front();
        que.pop();
        dep[ti->id] = ti;
        for (auto& e : ti->prev) {
          que.push(e.second.get());
        }
      }
      map<int, vector<TargetItem*>> lvl_dep;
      for (auto& e : dep) {
        lvl_dep[e.second->dep_level].push_back(e.second);
      }
      for (auto& e : lvl_dep) {
        for (auto ti : e.second) {
          switch (ti->build_type) {
          case BuildType::STATIC:
            ss << " " << ti->target_path;
            break;
          case BuildType::SHARE:
            ss << " -l" << ti->name;
            break;
          default:
            break;
          }
        }
      }
      ss << " ";
      lib_dep = ss.str();
    }

    ostringstream cmd;
    switch (build_type) {
    case BuildType::STATIC :
      cmd << "ar -crs " << target_path << objs;
      break;
    case BuildType::SHARE :
      cmd << "g++ -o " << target_path << " -Wl -m64 -shared"
          << objs << lib_path_param << lib_dep << link_base_libs;
      break;
    case BuildType::BINARY :
      cmd << "g++ -o " << target_path << " -Wl -m64"
          << objs << lib_path_param << lib_dep << link_base_libs;
      break;
    default:
      return false;
    }

    if (::system(cmd.str().c_str())) {
      g_has_build_fail = true;
      return false;
    } else {
      return true;
    }
  }

  void run() {
    if (g_has_build_fail) {
      complete();
      return;
    }
    conc::Task task = std::bind(&TargetItem::doRun, this);
    th_.reset(new conc::Thread(name, task));
    th_->run();
  }

private:
  void doRun() {
    if (buildTarget()) {
      OSS(ss) << BLUE << "build target ok: " << target_path << RESET << "\n";
      cout << ss.str();
    } else {
      OSS(ss) << RED << "build target fail: " << target_path << RESET << "\n";
      cout << ss.str();
    }
    complete();
  }

  void complete() {
    for (auto& e : next) {
      if (--(e.second->prevn_) == 0) {
        e.second->run();
      }
    }
    if (next.empty()) {
      g_leaf_target_left.countDown(1);
    }
  }
};

struct TargetMgr {
  std::string project_name;
  unordered_map<string, TargetItemPtr> targets;

  TargetItemPtr get(const std::string& name) {
    auto iter = targets.find(name);
    if (iter == targets.end()) return nullptr;
    return iter->second;
  }

  TargetItemPtr createIfAbsent(const std::string& name) {
    TargetItemPtr item(new TargetItem(id_gen_++, name));
    auto ret = targets.emplace(name, item);
    return ret.second ? item : nullptr;
  }

private:
  int id_gen_ = 1;
};

enum ParseYmlState {
  start,
  targetField,
  targetType,
  targetSrc,
  targetLib,
};

void initTargetMgr(TargetMgr& mgr) {
  for (auto& e : mgr.targets) {
    e.second->init();
  }
}

void parseBuildYml(std::string yml_path, TargetMgr& targetMgr) {
  yml_path = filesys::toAbsPath(yml_path);
  loginfo << "to parse: " << yml_path << logendl;

  targetMgr.project_name = filesys::getFileName(filesys::getDirPath(yml_path));
  loginfo << "project: " << targetMgr.project_name << logendl;

  ifstream fin(yml_path);
  if (!fin.good()) {
    fail("read build.yml fail: " + yml_path);
  }

  TargetItemPtr curr;
  ParseYmlState state = ParseYmlState::start;
  int line_no = 0;
  bool read_next = true;

  for (Str line; true; ) {
    if (read_next) {
      if (!getline(fin, line.s)) break;
      line_no++;
      int i = line.find('#');
      if (i >= 0) {
        line.s = line.s.substr(0, i);
      }
      line.trim();
      if (line.empty()) continue;
    }
    read_next = true;

    #define IF(ST) if (state == ST)
    #define EL(ST) else if (state == ST)

    IF(ParseYmlState::start) {
      if (!line.endsWith(":")) {
        ymlFail("not a target declare", yml_path, line_no);
      }
      curr = targetMgr.createIfAbsent(line.s.substr(0, line.len()-1));
      if (!curr) {
        ymlFail("duplicated target", yml_path, line_no);
      }
      state = ParseYmlState::targetField;
    }
    EL(ParseYmlState::targetField) {
      if (line.startsWith("type:")) {
        state = ParseYmlState::targetType;
        read_next = false;
      }
      else if (line.s == "src:") {
        state = ParseYmlState::targetSrc;
      }
      else if (line.s == "lib:") {
        state = ParseYmlState::targetLib;
      }
      else {
        state = ParseYmlState::start;
        read_next = false;
      }
    }
    EL(ParseYmlState::targetType) {
      Str type_s(line.s.substr(CStr("type:").n));
      type_s.trim();
      if (type_s.empty()) {
        ymlFail("target type is empty", yml_path, line_no);
      }
      int type = strToBuildType(type_s.s);
      if (type == 0) {
        ymlFail("unsupport target type: " + type_s.s, yml_path, line_no);
      }
      curr->build_type = static_cast<BuildType>(type);
      state = ParseYmlState::targetField;
    }
    EL(ParseYmlState::targetSrc) {
      if (!line.startsWith("-")) {
        state = ParseYmlState::targetField;
        read_next = false;
        continue;
      }
      Str src_s(line.s.substr(CStr("-").n));
      src_s.trim();
      if (!src_s.empty()) {
        if (!src_s.endsWith("*.cc")) {
          curr->src.emplace_back(src_s.s);
        } else {
          auto dir_path = filesys::getDirPath(src_s.s);
          filesys::FileInfo fi(projectDir() + "/" + dir_path);
          if (!fi.is_dir) {
            ymlFail("dir not exist: " + fi.path, yml_path, line_no);
          }
          auto fetch_cc_file = [&curr, &dir_path] (const std::string& name) {
            if (Str(name).endsWith(".cc")) {
              curr->src.emplace_back(dir_path + "/" + name);
            }
          };
          fi.traverseSubFiles(fetch_cc_file);
        }
      }
    }
    EL(ParseYmlState::targetLib) {
      if (!line.startsWith("-")) {
        state = ParseYmlState::targetField;
        read_next = false;
        continue;
      }
      Str lib_s(line.s.substr(CStr("-").n));
      lib_s.trim();
      if (!lib_s.empty()) {
        curr->lib.emplace_back(lib_s.s);
      }
    }
    #undef IF
    #undef EL
  }
  fin.close();
  initTargetMgr(targetMgr);
}

std::atomic<int> g_compile_objects_n(0);

void doCompileObjects(vector<string*>& cmds) {
  char buf[8*1024];
  size_t n = 0;
  const char* cmd = nullptr;
  const char* s = nullptr;

  for (auto e : cmds) {
    if (g_has_build_fail) {
      break;
    }
    cmd = e->c_str();

    n = e->length();
    for (s = cmd+n-1; *s != ' '; s--);
    n -= (++s) - cmd;
    ::memcpy(buf, s, n+1);

    if (::system(cmd)) {
      g_has_build_fail = true;
      OSS(ss) << RED << "compile fail: " << buf << RESET << "\n";
      cout << ss.str();
      break;
    } else {
      g_compile_objects_n++;
      OSS(ss) << BLUE << "compile ok: " << buf << RESET << "\n";
      cout << ss.str();
    }
  }
}

void compileObjects(vector<string>& cmds, int task_num) {
  loginfo << "compile " << cmds.size() << " objects ..." << logendl;
  vector<vector<string*>> cmd_parts;
  cmd_parts.resize(task_num);
  int i = 0;
  for (auto& cmd : cmds) {
    cmd_parts[i].push_back(&cmd);
    i = (i+1) % task_num;
  }

  vector<conc::Thread> threads;
  for (auto& part : cmd_parts) {
    if (part.empty()) continue;
    threads.emplace_back("", std::bind(doCompileObjects, part));
  }
  for (auto& th : threads) {
    th.run();
  }
  loginfo << "compile " << g_compile_objects_n.load()
          << " objects done" << logendl;
}

void computeTargetDepLevel(TargetMgr& mgr) {
  queue<TargetItem*> que;
  for (auto& e : mgr.targets) {
    if (e.second->prev.empty()) {
      e.second->dep_level = 1;
      que.push(e.second.get());
    }
  }
  while (!que.empty()) {
    auto ti = que.front();
    que.pop();
    int next_dep_level = ti->dep_level + 1;
    for (auto& e : ti->next) {
      e.second->dep_level = max(e.second->dep_level, next_dep_level);
      que.push(e.second.get());
    }
  }
}

void build(TargetMgr& targetMgr) {
  filesys::mkdirp("target/objects");
  filesys::mkdirp("target/lib");
  filesys::mkdirp("target/bin");

  vector<string> obj_compiles;
  for (auto& e : targetMgr.targets) {
    e.second->getObjectCompileCmd(obj_compiles);
  }
  compileObjects(obj_compiles, 10);
  if (g_has_build_fail) return;

  for (auto& e : targetMgr.targets) {
    TargetItemPtr ti = e.second;
    for (auto& lib : ti->lib) {
      auto prev = targetMgr.get(lib);
      if (!prev) {
        OSS(ss) << "not found dependent target. ["
                << ti->name << "] dep [" << lib << "]";
        fail(ss.str());
      }
      ti->addPrev(prev);
      prev->addNext(ti);
    }
  }
  computeTargetDepLevel(targetMgr);

  loginfo << "link ..." << logendl;
  for (auto& e : targetMgr.targets) {
    TargetItemPtr ti = e.second;
    if (ti->prev.empty()) {
      ti->run();
    }
    if (ti->next.empty()) {
      g_leaf_target_left.countDown(-1);
    }
  }
  g_leaf_target_left.wait();
}

int main() {
  TargetMgr rootTargets;
  parseBuildYml("build.yml", rootTargets);
  build(rootTargets);
}
