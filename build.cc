#include "base.h"
#include "utils.h"
#include "conc.h"
#include "filesys.h"

using namespace std;

const char* version() {
  return VERSION;
}

const std::string gk_targetdir_bin = "target/bin/";
const std::string gk_targetdir_lib = "target/lib/";
const std::string gk_targetdir_obj = "target/.objs/";
const std::string gk_targetdir_src = "target/.src/";
const std::string gk_targetdir_meta = "target/.meta/";

unordered_map<string, uint64_t> g_headers_mod_ts;
std::shared_mutex g_headers_mod_ts_mtx;

std::atomic<bool> g_has_build_fail(false);
conc::CountDownLatch g_leaf_target_left;

const string& srcRootPath() {
  static const string path_ = filesys::getCurrPath();
  return path_;
}

const string& cppflag() {
  static string flag_ = []() {
    OSS(ss) << "g++ -std=c++17 -pthread -fpic -fPIC -O2 "
            << "-g -Werror -Wall -Wno-unused-variable -g -I"
            << srcRootPath();
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

bool needUpdateWhenHeaderMod(const std::string& cc,
                             uint64_t obj_mod_ts) {
  ifstream fin(cc);
  if (!fin.good()) {
    fail("read src fail: " + cc);
  }
  int i, j;
  bool in_comment = false;
  for (Str line; getline(fin, line.s); ) {
    if (line.empty()) {
      continue;
    }
    if (in_comment) {
      if (line.endsWith("*/")) in_comment = false;
      continue;
    }
    if (line.s[0] == '/') {
      switch (line.s[1]) {
      case '/':
        break;
      case '*':
        in_comment = true;
        break;
      default:
        return true;
      }
      continue;
    }
    if (!line.startsWith("#include")) {
      return false;
    }

    // header file abs_path
    string abs_path;
    i = line.find('"', 8);
    if (i > 0) {
      j = line.find('"', ++i);
      if (j < 0) return true;
      abs_path = filesys::getDirPath(cc) + "/" + line.sub(i, j);
    } else {
      i = line.find('<', 8);
      if (i < 0) return true;
      j = line.find('>', ++i);
      if (j < 0) return true;
      abs_path = srcRootPath() + "/" + line.sub(i, j);
    }

    {
      std::shared_lock rlk(g_headers_mod_ts_mtx);
      auto iter = g_headers_mod_ts.find(abs_path);
      if (iter != g_headers_mod_ts.end()) {
        if (iter->second > obj_mod_ts) return true;
        continue;
      }
    }
    auto mod_ts = filesys::lastModTimeSec(abs_path);
    {
      std::unique_lock wlk(g_headers_mod_ts_mtx);
      g_headers_mod_ts.emplace(abs_path, mod_ts);
    }
    if (mod_ts > obj_mod_ts) return true;
  }
  fin.close();
  return false;
}

struct TargetItem;
typedef shared_ptr<TargetItem> TargetItemPtr;
struct TargetMgr;
typedef shared_ptr<TargetMgr> TargetMgrPtr;

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
      obj_dir(gk_targetdir_obj + name + "/"),
      prevn_(0), th_(nullptr)
  {}

  void init() {
    string& target_path_ref = const_cast<string&>(target_path);
    switch (build_type) {
    case BuildType::STATIC :
      target_path_ref = gk_targetdir_lib + "lib" + name + ".a";
      break;
    case BuildType::SHARE :
      target_path_ref = gk_targetdir_lib + "lib" + name + ".so";
      break;
    case BuildType::BINARY :
      target_path_ref = gk_targetdir_bin + name;
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
        if (cc_mod_ts < obj_mod_ts &&
            !needUpdateWhenHeaderMod(cc, obj_mod_ts)) {
          continue;
        }
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

    string lib_path_param = " -L./" + gk_targetdir_lib + " ";
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

/**
 * project_name: name of dir of yml file
 */
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
          filesys::FileInfo fi(srcRootPath() + "/" + dir_path);
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
  filesys::mkdirp(gk_targetdir_bin);
  filesys::mkdirp(gk_targetdir_lib);
  filesys::mkdirp(gk_targetdir_obj);
  filesys::mkdirp(gk_targetdir_src);
  filesys::mkdirp(gk_targetdir_meta);

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
