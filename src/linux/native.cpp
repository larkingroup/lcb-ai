#include "native.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstring>
#include <curl/curl.h>
#include <cwctype>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <locale>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <random>
#include <regex>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
namespace lcb {
const char *fields[GenCount] = {
    "max_tokens", "temperature",     "top_p",          "context_tokens",
    "thinking",   "repeat_penalty",  "dry_multiplier", "top_k",
    "min_p",      "presence_penalty"};
const char *labels[GenCount] = {
    "Response tokens", "Temperature",     "Top P",          "Context (reload)",
    "Thinking",        "Repeat penalty",  "DRY multiplier", "Top K",
    "Min P",           "Presence penalty"};
namespace {
std::string system_error(const std::string &what) {
  return what + ": " + std::strerror(errno);
}
std::string text(cJSON *p, const std::string &fallback = "") {
  return cJSON_IsString(p) ? p->valuestring : fallback;
}
std::string lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == s.npos)
    return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
bool valid_id(const std::string &id, size_t n) {
  return id.size() == n && id.find_first_not_of("0123456789abcdef") == id.npos;
}
struct Media {
  const char *kind, *mime;
};
const std::map<std::string, Media> media = {
    {".png", {"vision", "image/png"}},
    {".jpg", {"vision", "image/jpeg"}},
    {".jpeg", {"vision", "image/jpeg"}},
    {".bmp", {"vision", "image/bmp"}},
    {".gif", {"vision", "image/gif"}},
    {".wav", {"audio", "audio/wav"}},
    {".mp3", {"audio", "audio/mpeg"}},
    {".flac", {"audio", "audio/flac"}},
    {".mp4", {"video", "video/mp4"}},
    {".mkv", {"video", "video/x-matroska"}},
    {".webm", {"video", "video/webm"}},
    {".mov", {"video", "video/quicktime"}}};
constexpr size_t MaxMedia = 16 * 1048576, MaxMediaWire = 64 * 1048576;
struct ConversationOwner {
  Conversation value{};
  ~ConversationOwner() { conversation_clear(&value); }
};
std::string generated(const Conversation &c, const Module &m,
                      const std::string &prompt, const Generation &g,
                      bool effort) {
  char *p = conversation_generate_capable(&c, &m, prompt.c_str(), &g, effort);
  if (!p)
    throw std::runtime_error("Cannot build the conversation request.");
  std::unique_ptr<char, decltype(&cJSON_free)> owner(p, cJSON_free);
  return p;
}
} // namespace
Json Json::parse(const std::string &s) {
  validate_text(s, s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      if (s.compare(i + 1, 5, "u0000") == 0)
        throw std::runtime_error("JSON text contains a NUL character.");
      ++i;
    }
  }
  const char *end = nullptr;
  cJSON *p = cJSON_ParseWithLengthOpts(s.c_str(), s.size() + 1, &end, 1);
  if (!p)
    throw std::runtime_error("Invalid JSON.");
  return Json(p);
}
std::string Json::dump() const {
  char *s = cJSON_PrintUnformatted(p);
  if (!s)
    throw std::bad_alloc();
  std::unique_ptr<char, decltype(&cJSON_free)> owner(s, cJSON_free);
  return s;
}
std::string Json::str(const char *key, const std::string &fallback) const {
  return text(get(key), fallback);
}
double Json::number(const char *key, double fallback) const {
  auto v = get(key);
  return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}
Json Json::object(const char *key) const {
  auto v = get(key);
  return v ? Json(cJSON_Duplicate(v, 1)) : Json();
}
void Json::set(const char *key, const std::string &value) {
  cJSON_DeleteItemFromObjectCaseSensitive(p, key);
  if (!cJSON_AddStringToObject(p, key, value.c_str()))
    throw std::bad_alloc();
}
void Json::set(const char *key, double value) {
  cJSON_DeleteItemFromObjectCaseSensitive(p, key);
  if (!cJSON_AddNumberToObject(p, key, value))
    throw std::bad_alloc();
}
void Json::set(const char *key, const Json &value) {
  cJSON_DeleteItemFromObjectCaseSensitive(p, key);
  if (!cJSON_AddItemToObject(p, key, cJSON_Duplicate(value.p, 1)))
    throw std::bad_alloc();
}
void Json::add(const Json &value) {
  if (!cJSON_AddItemToArray(p, cJSON_Duplicate(value.p, 1)))
    throw std::bad_alloc();
}
std::wstring wide(const std::string &s) {
  std::wstring out;
  for (size_t i = 0; i < s.size();) {
    auto first = static_cast<unsigned char>(s[i++]);
    unsigned cp = first, remaining = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      cp = first & 31;
      remaining = 1;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      cp = first & 15;
      remaining = 2;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      cp = first & 7;
      remaining = 3;
      minimum = 0x10000;
    } else if (first >= 0x80)
      throw std::runtime_error("Invalid UTF-8 text.");
    if (remaining > s.size() - i)
      throw std::runtime_error("Incomplete UTF-8 text.");
    while (remaining--) {
      auto next = static_cast<unsigned char>(s[i++]);
      if ((next & 0xc0) != 0x80)
        throw std::runtime_error("Invalid UTF-8 text.");
      cp = (cp << 6) | (next & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw std::runtime_error("Invalid Unicode code point.");
    out.push_back(static_cast<wchar_t>(cp));
  }
  return out;
}
std::string utf8(const std::wstring &s) {
  std::string out;
  for (wchar_t value : s) {
    auto cp = static_cast<unsigned>(value);
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw std::runtime_error("Invalid Unicode code point.");
    if (cp < 0x80)
      out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
  }
  return out;
}
std::string truncate(std::string s, size_t n) {
  if (s.size() > n) {
    while (n && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)
      --n;
    s.resize(n);
  }
  return s;
}
void validate_text(const std::string &s, size_t limit, bool empty) {
  if (s.size() > limit || s.find('\0') != s.npos || (!empty && s.empty()))
    throw std::runtime_error(
        "Text exceeds the permitted size or contains NUL characters.");
  wide(s);
}
std::string read_file(const fs::path &path, size_t limit) {
  if (fs::is_symlink(path) || !fs::is_regular_file(path))
    throw std::runtime_error("Expected a regular file: " + path.string());
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read " + path.string());
  const auto size = fs::file_size(path);
  if (size > limit)
    throw std::runtime_error("File exceeds the size limit: " + path.string());
  std::string data(static_cast<size_t>(size), '\0');
  in.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!in && !data.empty())
    throw std::runtime_error("File is incomplete: " + path.string());
  return data;
}
void atomic_write(const fs::path &path, const std::string &bytes) {
  auto name = (path.parent_path() / ".save-XXXXXX").string();
  std::vector<char> temp(name.begin(), name.end());
  temp.push_back(0);
  int fd = mkstemp(temp.data());
  if (fd < 0)
    throw std::runtime_error(system_error("Cannot create save file"));
  bool renamed = false;
  try {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    size_t offset = 0;
    while (offset < bytes.size()) {
      ssize_t n = write(fd, bytes.data() + offset, bytes.size() - offset);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error(system_error("Cannot write save file"));
      offset += static_cast<size_t>(n);
    }
    if (fsync(fd))
      throw std::runtime_error(system_error("Cannot flush save file"));
    if (close(fd)) {
      fd = -1;
      throw std::runtime_error(system_error("Cannot close save file"));
    }
    fd = -1;
    if (rename(temp.data(), path.c_str()))
      throw std::runtime_error(system_error("Cannot replace save file"));
    renamed = true;
    int dir =
        open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0)
      throw std::runtime_error(system_error("Cannot open storage directory"));
    int result = fsync(dir);
    close(dir);
    if (result)
      throw std::runtime_error(system_error("Cannot flush storage directory"));
  } catch (...) {
    if (fd >= 0)
      close(fd);
    if (!renamed)
      unlink(temp.data());
    throw;
  }
}
std::string digest(const std::string &s) {
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned n = 0;
  if (!EVP_Digest(s.data(), s.size(), out, &n, EVP_sha256(), nullptr))
    throw std::runtime_error("Cannot hash data.");
  std::ostringstream hex;
  for (unsigned i = 0; i < n; ++i)
    hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(out[i]);
  return hex.str();
}
std::string identifier() {
  std::random_device rng;
  std::ostringstream s;
  for (int i = 0; i < 4; ++i)
    s << std::hex << std::setw(8) << std::setfill('0') << rng();
  return s.str();
}
fs::path data_root() {
  if (const char *p = getenv("LCB_DATA_HOME"))
    return fs::absolute(p);
  const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME");
  fs::path base =
      xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".local/share";
  for (const char *name : {"lcb-ai", "lts-ai", "lti-ai"})
    if (fs::exists(base / name))
      return base / name;
  return base / "lcb-ai";
}
void validate_attachments(const Json &items) {
  if (!cJSON_IsArray(items.p) || cJSON_GetArraySize(items.p) > 8)
    throw std::runtime_error("Use at most eight attachments per message.");
  cJSON *p;
  cJSON_ArrayForEach(p, items.p) {
    Json a(cJSON_Duplicate(p, 1));
    fs::path name = a.str("file");
    auto ext = name.extension().string();
    auto type = media.find(ext);
    auto size = a.number("size");
    if (type == media.end() || !valid_id(name.stem().string(), 64) ||
        a.str("file") != name.filename().string() ||
        a.str("kind") != type->second.kind || size < 1 || size > MaxMedia ||
        size != static_cast<size_t>(size))
      throw std::runtime_error("Invalid saved attachment.");
    if (!cJSON_IsString(a.get("name")))
      throw std::runtime_error("Invalid attachment name.");
    validate_text(a.str("name"), 1024, false);
  }
}
ModelInfo read_model(const std::string &path) {
  ModelInfo m{};
  if (!model_read(wide(fs::absolute(path).string()).c_str(), &m))
    throw std::runtime_error("Cannot read the GGUF header or tensor index. "
                             "Check file access and download completeness.");
  return m;
}
std::string model_role(const ModelInfo &m) {
  if (m.projector)
    return "Projector companion";
  if (std::regex_search(
          fs::path(utf8(m.path)).filename().string(),
          std::regex("FastMTP|(^|[-_])MTP[-_](Q[0-9]|F16|BF16|32K)",
                     std::regex::icase)))
    return "Draft companion";
  return m.runtime_requirement ? "Requires PrismML engine" : "Chat model";
}
std::string suggested_projector(const ModelInfo &m) {
  auto stem = [](const fs::path &p) {
    auto s = lower(p.stem().string());
    if (s.rfind("mmproj-", 0) == 0)
      s.erase(0, 7);
    return std::regex_replace(
        s, std::regex("-(iq[0-9][^-]*|q[0-9][^-]*|bf16|f16|f32)$"), "");
  };
  fs::path path(utf8(m.path));
  std::string found;
  for (const auto &entry : fs::directory_iterator(path.parent_path())) {
    auto p = entry.path();
    if (p.extension() == ".gguf" &&
        lower(p.filename().string()).rfind("mmproj-", 0) == 0 &&
        stem(p) == stem(path)) {
      if (!found.empty())
        return "";
      found = p.string();
    }
  }
  try {
    if (!found.empty() && read_model(found).projector)
      return found;
  } catch (const std::exception &) {
  }
  return "";
}
Store::Store(fs::path path)
    : root(path.empty() ? data_root() : fs::absolute(path)) {
  fs::create_directories(root);
  lock_ =
      open((root / "session.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (lock_ < 0)
    throw std::runtime_error(system_error("Cannot open storage lock"));
  if (flock(lock_, LOCK_EX | LOCK_NB)) {
    close(lock_);
    lock_ = -1;
    throw std::runtime_error(
        "Storage is in use. Close the other LCB-AI window.");
  }
  try {
    for (const char *folder : {"workspaces", "chats"}) {
      fs::create_directories(root / folder);
      auto &index = std::string(folder) == "chats" ? chats : workspaces;
      for (const auto &entry : fs::directory_iterator(root / folder)) {
        if (entry.path().extension() != ".json")
          continue;
        try {
          auto obj = Json::parse(read_file(entry.path(), 2147483647));
          validate(folder, obj);
          if (obj.str("id") != entry.path().stem().string())
            throw std::runtime_error("Identifier mismatch.");
          index.emplace(obj.str("id"), obj);
        } catch (const std::exception &) {
          ++skipped;
        }
      }
      if (std::string(folder) == "workspaces" && workspaces.empty())
        workspace(
            "General",
            "You are a helpful local assistant. Answer clearly and concisely.");
    }
  } catch (...) {
    close(lock_);
    lock_ = -1;
    throw;
  }
}
Store::~Store() {
  if (lock_ >= 0)
    close(lock_);
}
void Store::validate(const std::string &folder, const Json &obj) const {
  if (!cJSON_IsObject(obj.p) || obj.number("version") != 1 ||
      !valid_id(obj.str("id"), 32))
    throw std::runtime_error("Invalid saved identifier/version.");
  if (folder == "workspaces") {
    if (!cJSON_IsString(obj.get("name")) ||
        !cJSON_IsString(obj.get("master_prompt")))
      throw std::runtime_error("Invalid workspace fields.");
    validate_text(obj.str("name"), 240, false);
    validate_text(obj.str("master_prompt"), LcbMaxPrompt);
    return;
  }
  if (!workspaces.count(obj.str("workspace")))
    throw std::runtime_error("Workspace not found.");
  if (!cJSON_IsString(obj.get("draft")) || !cJSON_IsString(obj.get("title")) ||
      !cJSON_IsString(obj.get("workspace")))
    throw std::runtime_error("Invalid conversation fields.");
  validate_text(obj.str("title"), 240, false);
  validate_text(obj.str("draft"), LcbMaxPrompt);
  if (obj.get("draft_attachments"))
    validate_attachments(obj.object("draft_attachments"));
  auto messages = obj.get("messages");
  if (!cJSON_IsArray(messages) || cJSON_GetArraySize(messages) % 2)
    throw std::runtime_error("Incomplete saved conversation.");
  size_t i = 0;
  cJSON *p;
  cJSON_ArrayForEach(p, messages) {
    Json m(cJSON_Duplicate(p, 1));
    if (m.str("role") != (i % 2 ? "assistant" : "user"))
      throw std::runtime_error("Invalid message order.");
    if (!cJSON_IsString(m.get("content")))
      throw std::runtime_error("Invalid message text.");
    validate_text(m.str("content"), LcbMaxReply, false);
    if (m.get("attachments"))
      validate_attachments(m.object("attachments"));
    if (i % 2) {
      static const std::vector<std::string> statuses = {
          "unknown", "complete", "stopped", "length", "error", "other"};
      auto status = m.str("status", "unknown");
      if (std::find(statuses.begin(), statuses.end(), status) == statuses.end())
        throw std::runtime_error("Invalid answer status.");
      validate_text(m.str("error"), 255);
      validate_text(m.str("reasoning_content"), LcbMaxReply);
      validate_text(m.str("model"), 4096);
    }
    ++i;
  }
}
void Store::save(const std::string &folder, const Json &obj) {
  validate(folder, obj);
  auto &index = folder == "workspaces" ? workspaces : chats;
  auto id = obj.str("id");
  if (!index.count(id) && index.size() >= (folder == "workspaces" ? 64 : 1024))
    throw std::runtime_error("Saved workspace/conversation limit reached.");
  atomic_write(root / folder / (id + ".json"), obj.dump());
  index.insert_or_assign(id, obj);
}
Json Store::workspace(const std::string &name, const std::string &prompt,
                      const std::string &id) {
  Json obj;
  obj.set("version", 1);
  obj.set("id", id.empty() ? identifier() : id);
  obj.set("name", name);
  obj.set("master_prompt", prompt);
  save("workspaces", obj);
  return obj;
}
Json Store::new_chat(const std::string &workspace) {
  Json obj;
  obj.set("version", 1);
  obj.set("id", identifier());
  obj.set("workspace", workspace);
  obj.set("title", std::string("New chat"));
  obj.set("draft", std::string());
  obj.set("messages", Json::array());
  save("chats", obj);
  return obj;
}
Json Store::branch(const Json &chat, size_t turn, const std::string &prompt,
                   const std::string &action) {
  auto messages = chat.object("messages");
  if (turn >= static_cast<size_t>(cJSON_GetArraySize(messages.p)) / 2)
    throw std::runtime_error("Choose an exchange first.");
  validate_text(prompt, LcbMaxPrompt, false);
  Json obj(chat), before = Json::array();
  for (size_t i = 0; i < turn * 2; ++i)
    before.add(Json(cJSON_Duplicate(
        cJSON_GetArrayItem(messages.p, static_cast<int>(i)), 1)));
  obj.set("id", identifier());
  obj.set("title", truncate(action + " " + std::to_string(turn + 1) + ": " +
                                chat.str("title"),
                            240));
  obj.set("draft", prompt);
  obj.set("messages", before);
  Json original(cJSON_Duplicate(
      cJSON_GetArrayItem(messages.p, static_cast<int>(turn * 2)), 1));
  obj.set("draft_attachments", original.get("attachments")
                                   ? original.object("attachments")
                                   : Json::array());
  save("chats", obj);
  return obj;
}
void Store::erase(const std::string &id) {
  if (!chats.count(id))
    throw std::runtime_error("Conversation not found.");
  if (!fs::remove(root / "chats" / (id + ".json")))
    throw std::runtime_error("Cannot delete conversation.");
  chats.erase(id);
}
Json Store::import_attachment(const std::string &path) {
  auto suffix = lower(fs::path(path).extension().string());
  auto type = media.find(suffix);
  if (type == media.end())
    throw std::runtime_error("Choose a supported image, audio or video file.");
  auto data = read_file(path, MaxMedia);
  if (data.empty())
    throw std::runtime_error("Attachment is empty.");
  Json item;
  item.set("file", digest(data) + suffix);
  item.set("name", fs::path(path).filename().string());
  item.set("kind", std::string(type->second.kind));
  item.set("size", static_cast<double>(data.size()));
  auto folder = root / "attachments";
  fs::create_directories(folder);
  chmod(folder.c_str(), 0700);
  auto target = folder / item.str("file");
  if (!fs::exists(target))
    atomic_write(target, data);
  return item;
}
Json Store::media_part(const Json &item) const {
  Json items = Json::array();
  items.add(item);
  validate_attachments(items);
  auto path = root / "attachments" / item.str("file");
  auto data = read_file(path, MaxMedia);
  if (data.size() != item.number("size") ||
      digest(data) != path.stem().string())
    throw std::runtime_error("Saved attachment changed or is incomplete: " +
                             item.str("name"));
  std::string encoded(4 * ((data.size() + 2) / 3) + 1, '\0');
  int n = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
                          reinterpret_cast<const unsigned char *>(data.data()),
                          static_cast<int>(data.size()));
  encoded.resize(static_cast<size_t>(n));
  Json part, detail;
  auto kind = item.str("kind"), ext = path.extension().string();
  if (kind == "vision") {
    part.set("type", std::string("image_url"));
    detail.set("url", "data:" + std::string(media.at(ext).mime) + ";base64," +
                          encoded);
    part.set("image_url", detail);
  } else {
    auto key = "input_" + kind;
    part.set("type", key);
    detail.set("data", encoded);
    detail.set("format", ext.substr(1));
    part.set(key.c_str(), detail);
  }
  return part;
}
Settings::Settings(const fs::path &root) : path_(root / "settings.ini") {
  if (!fs::exists(path_))
    return;
  auto raw = read_file(path_, LcbMaxWire);
  if (raw.size() > 1 && ((static_cast<unsigned char>(raw[0]) == 0xff &&
                          static_cast<unsigned char>(raw[1]) == 0xfe) ||
                         (static_cast<unsigned char>(raw[0]) == 0xfe &&
                          static_cast<unsigned char>(raw[1]) == 0xff))) {
    if (raw.size() % 2)
      throw std::runtime_error("Incomplete UTF-16 settings.");
    bool le = static_cast<unsigned char>(raw[0]) == 0xff;
    std::wstring u;
    for (size_t i = 2; i + 1 < raw.size(); i += 2) {
      auto a = static_cast<unsigned char>(raw[i]),
           b = static_cast<unsigned char>(raw[i + 1]);
      unsigned cp = le ? (a | (b << 8)) : (b | (a << 8));
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (i + 3 >= raw.size())
          throw std::runtime_error("Incomplete UTF-16 settings.");
        unsigned next = le ? (static_cast<unsigned char>(raw[i + 2]) |
                              (static_cast<unsigned char>(raw[i + 3]) << 8))
                           : (static_cast<unsigned char>(raw[i + 3]) |
                              (static_cast<unsigned char>(raw[i + 2]) << 8));
        if (next < 0xdc00 || next > 0xdfff)
          throw std::runtime_error("Invalid UTF-16 settings.");
        cp = 0x10000 + ((cp - 0xd800) << 10) + (next - 0xdc00);
        i += 2;
      }
      u.push_back(static_cast<wchar_t>(cp));
    }
    raw = utf8(u);
  } else if (raw.rfind("\xef\xbb\xbf", 0) == 0)
    raw.erase(0, 3);
  std::istringstream in(raw);
  std::string line, section;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line.front() == '[' && line.back() == ']')
      section = line.substr(1, line.size() - 2);
    else {
      auto equal = line.find('=');
      if (equal != line.npos)
        ini_[section][lower(trim(line.substr(0, equal)))] =
            trim(line.substr(equal + 1));
    }
  }
  saved_ = ini_;
}
std::string Settings::get(const std::string &section, const std::string &key,
                          const std::string &fallback) const {
  auto s = ini_.find(section);
  if (s == ini_.end())
    return fallback;
  auto k = s->second.find(lower(key));
  return k == s->second.end() ? fallback : k->second;
}
void Settings::set(const std::string &section, const std::string &key,
                   const std::string &value) {
  if (value.find('\n') != value.npos || value.find('\r') != value.npos)
    throw std::runtime_error("Settings must be a single line.");
  ini_[section][lower(key)] = value;
}
void Settings::save() {
  std::ostringstream out;
  for (const auto &s : ini_) {
    out << '[' << s.first << "]\n";
    for (const auto &k : s.second)
      out << k.first << " = " << k.second << '\n';
    out << '\n';
  }
  try {
    atomic_write(path_, out.str());
    saved_ = ini_;
  } catch (...) {
    ini_ = saved_;
    throw;
  }
}
std::string Settings::section(const std::string &model) const {
  return model.empty()
             ? "no-model"
             : "linux-model-" +
                   digest(fs::absolute(model).lexically_normal().string());
}
void Settings::migrate(const std::string &old_path,
                       const std::string &new_path) {
  if (old_path.size() < 3 || old_path[1] != ':')
    return;
  auto p = old_path;
  std::replace(p.begin(), p.end(), '\\', '/');
  if (fs::path(p).filename() != fs::path(new_path).filename())
    return;
  p = lower(p);
  std::replace(p.begin(), p.end(), '/', '\\');
  auto u = wide(p);
  std::string bytes;
  for (wchar_t value : u) {
    unsigned cp = static_cast<unsigned>(std::towlower(value));
    if (cp > 0xffff) {
      cp -= 0x10000;
      unsigned high = 0xd800 + (cp >> 10), low = 0xdc00 + (cp & 1023);
      bytes.push_back(static_cast<char>(high & 255));
      bytes.push_back(static_cast<char>(high >> 8));
      bytes.push_back(static_cast<char>(low & 255));
      bytes.push_back(static_cast<char>(low >> 8));
    } else {
      bytes.push_back(static_cast<char>(cp & 255));
      bytes.push_back(static_cast<char>(cp >> 8));
    }
  }
  auto legacy = "model-" + digest(bytes), target = section(new_path);
  for (const char *key : fields) {
    auto v = get(legacy, key);
    if (!v.empty() && get(target, key).empty())
      set(target, key, v);
  }
  save();
}
Generation Settings::generation(const std::string &path,
                                const ModelInfo *info) const {
  Generation g = generation_defaults();
  model_defaults(info, -1, &g);
  g.thinking = ThinkingAuto;
  for (int i = 0; i < 3; ++i) {
    const char *keys[] = {"max_tokens", "temperature100", "top_p100"};
    try {
      auto v = get("generation", keys[i]);
      if (!v.empty())
        generation_set(&g, i, std::stod(v) / (i ? 100 : 1));
    } catch (const std::exception &) {
    }
  }
  for (int i = 0; i < GenCount; ++i) {
    try {
      auto v = get(section(path), fields[i]);
      if (!v.empty())
        generation_set(&g, i, std::stod(v));
    } catch (const std::exception &) {
    }
  }
  return g;
}
HttpError::HttpError(long code, const std::string &detail)
    : std::runtime_error("Local engine returned HTTP " + std::to_string(code) +
                         ": " + detail),
      status(code) {}
std::string
Transport::exchange(const std::string &path, const std::string *body,
                    const std::function<bool(const char *, size_t)> &receive,
                    long deadline) {
  if (cancelled)
    throw std::runtime_error("Stopped by user.");
  if (body && body->size() > (path == "/v1/chat/completions"
                                  ? MaxMediaWire
                                  : static_cast<size_t>(LcbMaxWire)))
    throw std::runtime_error("Local request exceeds the size limit.");
  static const int curl_ready = []() {
    return curl_global_init(CURL_GLOBAL_DEFAULT);
  }();
  if (curl_ready)
    throw std::runtime_error("Cannot initialize HTTP transport.");
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                           curl_easy_cleanup);
  if (!curl)
    throw std::bad_alloc();
  struct State {
    Transport *owner;
    const std::function<bool(const char *, size_t)> *receive;
    std::string data, error;
    std::exception_ptr exception;
    size_t count = 0;
    bool done = false;
    long status = 0;
  } state{this, &receive, {}, {}, nullptr, 0, false, 0};
  auto url = "http://127.0.0.1:" + std::to_string(port_) + path;
  char error[CURL_ERROR_SIZE]{};
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, deadline);
  curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 90L);
  curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error);
  curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &state);
  curl_easy_setopt(
      curl.get(), CURLOPT_XFERINFOFUNCTION,
      +[](void *v, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return static_cast<State *>(v)->owner->cancelled ? 1 : 0;
      });
  curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(
      curl.get(), CURLOPT_HEADERFUNCTION,
      +[](char *p, size_t s, size_t n, void *v) -> size_t {
        auto &st = *static_cast<State *>(v);
        size_t len = s * n;
        if (len > 5 && std::memcmp(p, "HTTP/", 5) == 0) {
          std::string line(p, len);
          auto at = line.find(' ');
          if (at != line.npos)
            st.status = std::strtol(line.c_str() + at + 1, nullptr, 10);
        }
        return len;
      });
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(
      curl.get(), CURLOPT_WRITEFUNCTION,
      +[](char *p, size_t s, size_t n, void *v) -> size_t {
        auto &st = *static_cast<State *>(v);
        size_t len = s * n;
        try {
          if (st.owner->cancelled)
            return 0;
          st.count += len;
          if (st.count > ((*st.receive) ? LcbMaxStreamWire : LcbMaxWire))
            throw std::runtime_error("Local response exceeds the size limit.");
          if (st.status != 200) {
            if (st.error.size() < 8192)
              st.error.append(p, std::min(len, 8192 - st.error.size()));
            return len;
          }
          if (*st.receive) {
            if ((*st.receive)(p, len)) {
              st.done = true;
              return 0;
            }
          } else
            st.data.append(p, len);
          return len;
        } catch (...) {
          st.exception = std::current_exception();
          return 0;
        }
      });
  curl_slist *raw =
      curl_slist_append(nullptr, "Content-Type: application/json");
  raw = curl_slist_append(raw, "Connection: close");
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
      raw, curl_slist_free_all);
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  if (body) {
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body->data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body->size()));
  }
  CURLcode result = curl_easy_perform(curl.get());
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (cancelled)
    throw std::runtime_error("Stopped by user.");
  if (state.exception)
    std::rethrow_exception(state.exception);
  if (status && status != 200) {
    std::string detail = "check that the model is ready";
    try {
      detail = Json::parse(state.error).object("error").str("message", detail);
    } catch (const std::exception &) {
    }
    throw HttpError(status, detail);
  }
  if (result != CURLE_OK && !state.done)
    throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
  return state.data;
}
Json Transport::json(const std::string &path, const Json *body) {
  auto wire = body ? body->dump() : std::string();
  auto result = Json::parse(exchange(path, body ? &wire : nullptr));
  if (!cJSON_IsObject(result.p))
    throw std::runtime_error("Invalid JSON from the local engine.");
  return result;
}
Prepared Transport::prepare(const std::string &model, const Json &messages,
                            const std::string &instruction,
                            const std::string &prompt, const Generation &g,
                            const Json &attachments, const Store &store) {
  validate_text(instruction, LcbMaxPrompt);
  validate_text(prompt, LcbMaxPrompt, false);
  validate_attachments(attachments);
  auto props = json("/props");
  auto context_value =
      props.object("default_generation_settings").number("n_ctx");
  if (!std::isfinite(context_value) || context_value < 1 ||
      context_value > 2147483647 || context_value != std::floor(context_value))
    throw std::runtime_error("Invalid context size from the engine.");
  int context = static_cast<int>(context_value);
  auto templ = props.str("chat_template");
  if (context <= 0 || templ.empty())
    throw std::runtime_error(
        "Engine must report its context size and chat template.");
  if (!model.empty() && fs::weakly_canonical(props.str("model_path")) !=
                            fs::weakly_canonical(model))
    throw std::runtime_error(
        "The local server is running a different model. Select its GGUF file.");
  bool effort = cJSON_IsTrue(
      props.object("chat_template_caps").get("supports_reasoning_effort"));
  if (const char *problem = reasoning_settings_error(&g, effort))
    throw std::runtime_error(problem);
  if (context - g.max_tokens - 32 < 1)
    throw std::runtime_error("Response limit leaves no prompt room. Lower it "
                             "or reload with more context.");
  ConversationOwner c;
  cJSON *item;
  std::vector<Json> attachment_sets;
  cJSON_ArrayForEach(item, messages.p) {
    Json m(cJSON_Duplicate(item, 1));
    if (!conversation_add(&c.value,
                          (m.str("role") == "assistant" ? "assistant" : "user"),
                          m.str("content").c_str()))
      throw std::runtime_error("Cannot prepare conversation.");
    attachment_sets.push_back(m.get("attachments") ? m.object("attachments")
                                                   : Json::array());
  }
  attachment_sets.push_back(attachments);
  Module module{"Workspace", instruction.c_str()};
  Prepared prepared;
  prepared.context = context;
  prepared.response = g.max_tokens;
  bool has_media = false;
  for (const auto &a : attachment_sets)
    has_media |= cJSON_GetArraySize(a.p) > 0;
  if (has_media) {
    auto modalities = props.object("modalities");
    size_t total = 0;
    for (const auto &a : attachment_sets) {
      validate_attachments(a);
      cJSON *it;
      cJSON_ArrayForEach(it, a.p) {
        Json att(cJSON_Duplicate(it, 1));
        if (!cJSON_IsTrue(modalities.get(att.str("kind").c_str())))
          throw std::runtime_error("The loaded engine does not support " +
                                   att.str("kind") +
                                   " input: " + att.str("name"));
        total += static_cast<size_t>(att.number("size"));
      }
    }
    if (total > 46 * 1048576)
      throw std::runtime_error("Conversation media exceeds 46 MiB. Start a new "
                               "chat or use smaller files.");
    auto request = Json::parse(generated(c.value, module, prompt, g, effort));
    auto turns = request.get("messages");
    size_t i = 0;
    cJSON *turn;
    cJSON_ArrayForEach(turn, turns) {
      if (text(cJSON_GetObjectItemCaseSensitive(turn, "role")) == "system")
        continue;
      const auto &a = attachment_sets.at(i++);
      if (cJSON_GetArraySize(a.p)) {
        Json content = Json::array(), part;
        part.set("type", std::string("text"));
        part.set("text",
                 text(cJSON_GetObjectItemCaseSensitive(turn, "content")));
        content.add(part);
        cJSON *it;
        cJSON_ArrayForEach(it, a.p) {
          if (cancelled)
            throw std::runtime_error("Stopped by user.");
          content.add(store.media_part(Json(cJSON_Duplicate(it, 1))));
        }
        cJSON_ReplaceItemInObjectCaseSensitive(turn, "content",
                                               cJSON_Duplicate(content.p, 1));
      }
    }
    prepared.wire = request.dump();
    if (prepared.wire.size() > MaxMediaWire)
      throw std::runtime_error("Media request exceeds 64 MiB.");
    return prepared;
  }
  struct Counter {
    Transport *transport;
    std::exception_ptr exception;
    std::string identity;
    bool cacheable;
  } counter{this, nullptr,
            props.str("model_path") + templ + std::to_string(context) +
                props.object("build_info").dump(),
            templ.find("strftime_now") == templ.npos};
  auto count = +[](const char *wire, void *v, int *tokens, char *error,
                   size_t capacity) -> int {
    auto &c = *static_cast<Counter *>(v);
    try {
      static std::map<std::string, int> cache;
      static std::mutex cache_mutex;
      auto key = digest(c.identity + std::string(1, '\0') + wire);
      {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto found = cache.find(key);
        if (c.cacheable && found != cache.end()) {
          *tokens = found->second;
          return CountOk;
        }
      }
      auto request = Json::parse(wire);
      auto formatted = c.transport->json("/apply-template", &request);
      if (!cJSON_IsString(formatted.get("prompt")))
        throw std::runtime_error("Engine could not apply the chat template.");
      Json input;
      input.set("content", formatted.str("prompt"));
      cJSON_AddBoolToObject(input.p, "add_special", 1);
      cJSON_AddBoolToObject(input.p, "parse_special", 1);
      auto result = c.transport->json("/tokenize", &input);
      auto array = result.get("tokens");
      if (!cJSON_IsArray(array))
        throw std::runtime_error("Invalid token count from the engine.");
      cJSON *t;
      cJSON_ArrayForEach(t, array) {
        if (!cJSON_IsNumber(t) || t->valuedouble < 0 ||
            t->valuedouble != t->valueint)
          throw std::runtime_error("Invalid token count from the engine.");
      }
      *tokens = cJSON_GetArraySize(array);
      if (c.cacheable) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (cache.size() >= 128)
          cache.erase(cache.begin());
        cache[key] = *tokens;
      }
      return CountOk;
    } catch (const std::exception &e) {
      c.exception = std::current_exception();
      if (capacity)
        std::snprintf(error, capacity, "%s", e.what());
      return CountFailed;
    }
  };
  char *wire = nullptr, error[512]{};
  FitBudget budget{};
  if (!conversation_fit(&c.value, &module, prompt.c_str(), &g, context, effort,
                        count, &counter, &wire, &budget, error,
                        sizeof(error))) {
    if (counter.exception)
      std::rethrow_exception(counter.exception);
    throw std::runtime_error(error);
  }
  std::unique_ptr<char, decltype(&cJSON_free)> owner(wire, cJSON_free);
  prepared.wire = wire;
  prepared.prompt = budget.prompt_tokens;
  prepared.omitted = budget.omitted_messages / 2;
  return prepared;
}
Reply Transport::generate(
    const std::string &wire,
    const std::function<void(const StreamReply &)> &update) {
  auto state = std::make_unique<StreamReply>();
  Reply reply;
  int result = 1;
  bool emitted_content = false, emitted_reasoning = false;
  auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  try {
    exchange(
        "/v1/chat/completions", &wire,
        [&](const char *p, size_t n) {
          if (!stream_feed(state.get(), p, n))
            throw std::runtime_error(
                "Invalid or oversized stream from the local engine.");
          auto now = std::chrono::steady_clock::now();
          if (update && (state->done || (state->used && !emitted_content) ||
                         (state->reasoning_used && !emitted_reasoning) ||
                         now - last >= std::chrono::milliseconds(50))) {
            update(*state);
            emitted_content = state->used > 0;
            emitted_reasoning = state->reasoning_used > 0;
            last = now;
          }
          return state->done != 0;
        },
        0);
    if (!state->done || state->failed)
      throw std::runtime_error("Engine closed an incomplete response stream.");
  } catch (const std::exception &e) {
    result = cancelled ? 2 : 0;
    reply.error = truncate(e.what(), 255);
  }
  reply.content = state->text;
  reply.reasoning = state->reasoning;
  reply.tokens = state->tokens;
  reply.prompt_tokens = state->prompt_tokens;
  if (result == 1 && reply.content.empty() && reply.reasoning.empty()) {
    result = 0;
    reply.error =
        "The model ended without emitting an answer or a thinking trace.";
  }
  reply.status = answer_status_name(answer_status(result, state->finish));
  if (update)
    update(*state);
  return reply;
}
std::vector<std::string> split_arguments(const std::string &s) {
  std::vector<std::string> out;
  std::string word;
  char quote = 0;
  bool escape = false, started = false;
  for (char c : s) {
    if (escape) {
      word += c;
      escape = false;
      started = true;
      continue;
    }
    if (c == '\\' && quote != '\'') {
      escape = true;
      started = true;
      continue;
    }
    if (quote) {
      if (c == quote)
        quote = 0;
      else
        word += c;
      started = true;
    } else if (c == '\'' || c == '"') {
      quote = c;
      started = true;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (started) {
        out.push_back(word);
        word.clear();
        started = false;
      }
    } else {
      word += c;
      started = true;
    }
  }
  if (quote || escape)
    throw std::runtime_error(
        "Extra arguments contain an unfinished quote or escape.");
  if (started)
    out.push_back(word);
  return out;
}
Engine::~Engine() { stop(); }
std::vector<std::string> Engine::arguments() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return command;
}
std::string Engine::log_tail() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tail;
}
void Engine::start(const std::string &exe, const std::string &model,
                   unsigned short port, int context, const fs::path &log,
                   const std::string &projector, const std::string &extra) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (owned())
    throw std::runtime_error("Unload the current model first.");
  auto executable = fs::absolute(exe).string();
  auto selected = fs::absolute(model).string();
  if (!fs::is_regular_file(executable) || access(executable.c_str(), X_OK))
    throw std::runtime_error("Choose an executable Linux llama-server first.");
  std::ifstream binary(executable, std::ios::binary);
  char magic[2]{};
  binary.read(magic, 2);
  if (magic[0] == 'M' && magic[1] == 'Z')
    throw std::runtime_error("Choose a Linux llama-server; Windows executables "
                             "cannot run natively.");
  if (!fs::is_regular_file(selected))
    throw std::runtime_error("Choose an available GGUF model.");
  int probe = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe < 0)
    throw std::runtime_error(system_error("Cannot check engine port"));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int bound =
      bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  close(probe);
  if (bound)
    throw std::runtime_error("Engine port is already in use. Choose a "
                             "different port or use the existing server.");
  command = {executable};
  auto tuning = split_arguments(extra);
  command.insert(command.end(), tuning.begin(), tuning.end());
  for (const auto &s : std::vector<std::string>{"--model",
                                                selected,
                                                "--alias",
                                                "local",
                                                "--host",
                                                "127.0.0.1",
                                                "--port",
                                                std::to_string(port),
                                                "--ctx-size",
                                                std::to_string(context),
                                                "--parallel",
                                                "1",
                                                "--jinja",
                                                "--reasoning-format",
                                                "deepseek",
                                                "--sse-ping-interval",
                                                "5",
                                                "--no-webui",
                                                "--no-agent",
                                                "--no-ui-mcp-proxy",
                                                "--cors-origins",
                                                "localhost",
                                                "--no-cors-credentials"})
    command.push_back(s);
  if (!projector.empty()) {
    if (!fs::is_regular_file(projector))
      throw std::runtime_error("The selected projector is unavailable.");
    command.push_back("--mmproj");
    command.push_back(fs::absolute(projector).string());
  }
  log_ = log;
  tail.clear();
  offset_ = fs::exists(log) ? static_cast<size_t>(fs::file_size(log)) : 0;
  int output =
      open(log.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
  if (output < 0)
    throw std::runtime_error(system_error("Cannot open engine log"));
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, output);
  posix_spawn_file_actions_addchdir_np(
      &actions, fs::path(executable).parent_path().c_str());
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attr, 0);
  std::vector<char *> argv;
  for (auto &s : command)
    argv.push_back(s.data());
  argv.push_back(nullptr);
  pid_t child = -1;
  int rc = posix_spawn(&child, executable.c_str(), &actions, &attr, argv.data(),
                       environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  close(output);
  if (rc)
    throw std::runtime_error("Cannot launch engine: " +
                             std::string(std::strerror(rc)));
  pid_ = child;
}
void Engine::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pid_ <= 0)
    return;
  kill(-pid_, SIGTERM);
  int status = 0;
  for (int i = 0; i < 40; ++i) {
    auto result = waitpid(pid_, &status, WNOHANG);
    if (result == pid_ || (result < 0 && errno == ECHILD)) {
      pid_ = -1;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  kill(-pid_, SIGKILL);
  while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
  }
  pid_ = -1;
}
bool Engine::exited(int &code) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pid_ <= 0)
    return false;
  int status = 0;
  auto result = waitpid(pid_, &status, WNOHANG);
  if (result != pid_)
    return false;
  pid_ = -1;
  code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return true;
}
std::string Engine::read_log() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (log_.empty())
    return "";
  std::ifstream in(log_, std::ios::binary);
  if (!in)
    return "";
  in.seekg(static_cast<std::streamoff>(offset_));
  std::string bytes(65536, '\0');
  in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<size_t>(in.gcount()));
  offset_ += bytes.size();
  tail += bytes;
  if (tail.size() > 16000)
    tail.erase(0, tail.size() - 16000);
  return bytes;
}
std::string Engine::failure() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!log_.empty()) {
    std::ifstream in(log_, std::ios::binary);
    if (in) {
      in.seekg(0, std::ios::end);
      auto size = static_cast<std::streamoff>(in.tellg());
      in.seekg(std::max<std::streamoff>(0, size - 16000));
      tail.assign(std::istreambuf_iterator<char>(in), {});
    }
  }
  try {
    return utf8(engine_failure_advice(wide(tail).c_str()));
  } catch (const std::exception &) {
    return "Check the engine log.";
  }
}
std::vector<ModelInfo> scan_models(const std::string &folder, bool recursive,
                                   const std::atomic<bool> &cancel) {
  std::vector<ModelInfo> result;
  auto read = [&](const fs::directory_entry &e) {
    if (cancel || result.size() >= LibraryMax)
      return;
    if (e.is_regular_file() && !e.is_symlink() &&
        lower(e.path().extension().string()) == ".gguf") {
      try {
        result.push_back(read_model(e.path().string()));
      } catch (const std::exception &) {
      }
    }
  };
  if (recursive) {
    for (const auto &e : fs::recursive_directory_iterator(
             folder, fs::directory_options::skip_permission_denied)) {
      if (cancel || result.size() >= LibraryMax)
        break;
      read(e);
    }
  } else
    for (const auto &e : fs::directory_iterator(folder)) {
      if (cancel || result.size() >= LibraryMax)
        break;
      read(e);
    }
  std::sort(result.begin(), result.end(),
            [](const ModelInfo &a, const ModelInfo &b) {
              return model_compare(&a, &b, 0) < 0;
            });
  return result;
}
} // namespace lcb
