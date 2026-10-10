#ifndef LCB_LINUX_NATIVE_H
#define LCB_LINUX_NATIVE_H
extern "C" {
#include "cJSON.h"
#include "lcb.h"
#include "library_win.h"
#include "markdown.h"
#include "model_defaults.h"
}
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace lcb {
namespace fs = std::filesystem;
struct Json {
  cJSON *p = nullptr;
  Json() : p(cJSON_CreateObject()) {}
  explicit Json(cJSON *v) : p(v) {
    if (!p)
      throw std::bad_alloc();
  }
  Json(const Json &v) : Json(cJSON_Duplicate(v.p, 1)) {}
  Json(Json &&v) noexcept : p(v.p) { v.p = nullptr; }
  Json &operator=(Json v) {
    std::swap(p, v.p);
    return *this;
  }
  ~Json() { cJSON_Delete(p); }
  static Json parse(const std::string &s);
  static Json array() { return Json(cJSON_CreateArray()); }
  std::string dump() const;
  cJSON *get(const char *key) const {
    return cJSON_GetObjectItemCaseSensitive(p, key);
  }
  std::string str(const char *key, const std::string &fallback = "") const;
  double number(const char *key, double fallback = 0) const;
  Json object(const char *key) const;
  void set(const char *key, const std::string &value);
  void set(const char *key, double value);
  void set(const char *key, const Json &value);
  void add(const Json &value);
};
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::string truncate(std::string s, size_t bytes);
std::string read_file(const fs::path &path, size_t limit);
void atomic_write(const fs::path &path, const std::string &bytes);
std::string digest(const std::string &bytes);
std::string identifier();
fs::path data_root();
extern const char *fields[GenCount];
extern const char *labels[GenCount];
void validate_text(const std::string &s, size_t limit, bool empty = true);
void validate_attachments(const Json &items);
std::string model_role(const ModelInfo &model);
ModelInfo read_model(const std::string &path);
std::string suggested_projector(const ModelInfo &model);
class Store {
  int lock_ = -1;
  void validate(const std::string &folder, const Json &obj) const;

public:
  fs::path root;
  std::map<std::string, Json> workspaces, chats;
  size_t skipped = 0;
  explicit Store(fs::path path = {});
  ~Store();
  Store(const Store &) = delete;
  void save(const std::string &folder, const Json &obj);
  Json workspace(const std::string &name, const std::string &prompt,
                 const std::string &id = "");
  Json new_chat(const std::string &workspace);
  Json branch(const Json &chat, size_t turn, const std::string &prompt,
              const std::string &action);
  void erase(const std::string &id);
  Json import_attachment(const std::string &path);
  Json media_part(const Json &item) const;
};
class Settings {
  fs::path path_;
  std::map<std::string, std::map<std::string, std::string>> ini_, saved_;

public:
  explicit Settings(const fs::path &root);
  std::string get(const std::string &section, const std::string &key,
                  const std::string &fallback = "") const;
  void set(const std::string &section, const std::string &key,
           const std::string &value);
  void save();
  std::string section(const std::string &model) const;
  void migrate(const std::string &old_path, const std::string &new_path);
  Generation generation(const std::string &model,
                        const ModelInfo *info = nullptr) const;
};
struct HttpError : std::runtime_error {
  long status;
  HttpError(long code, const std::string &detail);
};
struct Prepared {
  std::string wire;
  int context = 0, prompt = -1, response = 0;
  size_t omitted = 0;
};
struct Reply {
  std::string content, reasoning, error, status;
  int tokens = 0, prompt_tokens = 0;
};
class Transport {
  unsigned short port_;

public:
  std::atomic<bool> cancelled{false};
  explicit Transport(unsigned short port) : port_(port) {}
  void cancel() { cancelled = true; }
  std::string
  exchange(const std::string &path, const std::string *body = nullptr,
           const std::function<bool(const char *, size_t)> &receive = {},
           long deadline = 120);
  Json json(const std::string &path, const Json *body = nullptr);
  Prepared prepare(const std::string &model, const Json &messages,
                   const std::string &instruction, const std::string &prompt,
                   const Generation &g, const Json &attachments,
                   const Store &store);
  Reply generate(const std::string &wire,
                 const std::function<void(const StreamReply &)> &update = {});
};
class Engine {
  std::atomic<int> pid_{-1};
  mutable std::mutex mutex_;
  fs::path log_;
  size_t offset_ = 0;

public:
  std::vector<std::string> command;
  std::string tail;
  ~Engine();
  std::vector<std::string> arguments() const;
  std::string log_tail() const;
  bool owned() const { return pid_ > 0; }
  void start(const std::string &exe, const std::string &model,
             unsigned short port, int context, const fs::path &log,
             const std::string &projector, const std::string &extra);
  void stop();
  bool exited(int &code);
  std::string read_log();
  std::string failure();
};
std::vector<std::string> split_arguments(const std::string &text);
std::vector<ModelInfo> scan_models(const std::string &folder, bool recursive,
                                   const std::atomic<bool> &cancel);
} // namespace lcb
#endif
