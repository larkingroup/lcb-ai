#include "native.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Value_Input.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>
#include <algorithm>
#include <chrono>
#include <clocale>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
using namespace lcb;
namespace {
volatile std::sig_atomic_t exit_requested = 0;
void exit_signal(int) { exit_requested = 1; }
const Fl_Color Face = fl_rgb_color(236, 233, 216),
               Light = fl_rgb_color(250, 249, 241),
               Shadow = fl_rgb_color(153, 154, 145),
               Ink = fl_rgb_color(47, 55, 58),
               Muted = fl_rgb_color(103, 113, 115),
               Blue = fl_rgb_color(192, 211, 224),
               BlueInk = fl_rgb_color(49, 76, 96),
               Title = fl_rgb_color(76, 112, 157);
void edge(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  fl_color(Light);
  fl_line(x, y, x + w - 1, y);
  fl_line(x, y, x, y + h - 1);
  fl_color(Shadow);
  fl_line(x, y + h - 1, x + w - 1, y + h - 1);
  fl_line(x + w - 1, y, x + w - 1, y + h - 1);
}
void raised(int x, int y, int w, int h, Fl_Color color) {
  edge(x, y, w, h, color);
}
void sunken(int x, int y, int w, int h, Fl_Color color) {
  edge(x, y, w, h, color);
  fl_color(Shadow);
  fl_line(x, y, x + w - 1, y);
  fl_line(x, y, x, y + h - 1);
  fl_color(Light);
  fl_line(x, y + h - 1, x + w - 1, y + h - 1);
  fl_line(x + w - 1, y, x + w - 1, y + h - 1);
}
void theme() {
  Fl::scheme("none");
  Fl::background(236, 233, 216);
  Fl::background2(255, 255, 255);
  Fl::foreground(47, 55, 58);
  Fl::set_color(FL_SELECTION_COLOR, 192, 211, 224);
  Fl::set_boxtype(FL_UP_BOX, raised, 2, 2, 4, 4);
  Fl::set_boxtype(FL_DOWN_BOX, sunken, 2, 2, 4, 4);
  Fl::set_boxtype(FL_THIN_UP_BOX, raised, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_DOWN_BOX, sunken, 1, 1, 2, 2);
  Fl::set_font(FL_HELVETICA, "DejaVu Sans");
  Fl::set_font(FL_COURIER, "DejaVu Sans Mono");
  Fl::scrollbar_size(15);
  fl_message_font(FL_HELVETICA, 12);
}
std::string buffer_text(Fl_Text_Buffer &b) {
  char *s = b.text();
  std::string value = s ? s : "";
  free(s);
  return value;
}
void set_text(Fl_Text_Buffer &b, const std::string &s) { b.text(s.c_str()); }
std::string clean_label(std::string s) {
  for (char &c : s)
    if (c == '/' || c == '\\' || c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  return s;
}
std::string menu_label(std::string s) {
  for (char &c : s)
    if (c == '/' || c == '\\' || c == '&')
      c = ' ';
  return s;
}
std::string chosen(const char *title, const char *filter,
                   const std::string &initial = "") {
  const char *p = fl_file_chooser(title, filter,
                                  initial.empty() ? nullptr : initial.c_str());
  return p ? p : "";
}
void open_url(const std::string &url) {
  char error[256]{};
  if (!fl_open_uri(url.c_str(), error, sizeof(error)))
    throw std::runtime_error(error);
}
// Fixed cell geometry and clipping keep names, support and sizes in their own
// columns, including narrow panes and unusually long GGUF filenames.
class ClassicTable : public Fl_Table_Row {
public:
  struct Row {
    std::string name, value, support;
  };
  std::vector<Row> entries;
  std::function<void(int)> sorted;
  std::function<void(bool)> selected;
  bool model_rows;
  int order = 0;
  bool descending = false;
  ClassicTable(bool models) : Fl_Table_Row(0, 0, 10, 10), model_rows(models) {
    end();
    // Fl_Table draws its frame after its scrollbar children; a filled box
    // would paint over those children.
    box(FL_THIN_DOWN_FRAME);
    color(FL_WHITE);
    type(SELECT_SINGLE);
    cols(2);
    col_header(models);
    col_header_height(23);
    row_header(0);
    tab_cell_nav(0);
  }
  int value() {
    for (int r = 0; r < rows(); ++r)
      if (row_selected(r))
        return r + 1;
    return 0;
  }
  void value(int v) {
    select_all_rows(0);
    if (v > 0 && v <= rows()) {
      select_row(v - 1);
      set_selection(v - 1, 0, v - 1, 1);
    }
  }
  void fit_columns() {
    int available =
        std::max(100, w() - 4 - (model_rows ? Fl::scrollbar_size() : 0));
    int second = model_rows ? 68 : std::max(60, available - 92);
    col_width(0, available - second);
    col_width(1, second);
  }
  void refresh(int selection = 0) {
    rows(static_cast<int>(entries.size()));
    row_height_all(model_rows ? 36 : 23);
    fit_columns();
    value(selection);
    redraw();
  }
  void resize(int X, int Y, int W, int H) override {
    Fl_Table_Row::resize(X, Y, W, H);
    fit_columns();
  }
  static void cell_text(const std::string &text, int X, int Y, int W, int H,
                        Fl_Align alignment = FL_ALIGN_LEFT) {
    std::string clipped = text;
    if (fl_width(clipped.c_str()) > W) {
      while (!clipped.empty() && fl_width((clipped + "…").c_str()) > W) {
        size_t end = clipped.size() - 1;
        while (end && (static_cast<unsigned char>(clipped[end]) & 0xc0) == 0x80)
          --end;
        clipped.resize(end);
      }
      clipped += "…";
    }
    fl_draw(clipped.c_str(), X, Y, W, H,
            alignment | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  }
  void draw_cell(TableContext context, int R, int C, int X, int Y, int W,
                 int H) override {
    if (context == CONTEXT_COL_HEADER) {
      edge(X, Y, W, H, Face);
      fl_font(FL_HELVETICA, 12);
      fl_color(Ink);
      cell_text(C ? "GiB" : "Name / support", X + 5, Y, W - 20, H);
      if (C == order) {
        int ax = X + W - 11, ay = Y + H / 2;
        fl_polygon(ax - 3, ay + (descending ? -2 : 2), ax + 3,
                   ay + (descending ? -2 : 2), ax, ay + (descending ? 2 : -2));
      }
      return;
    }
    if (context != CONTEXT_CELL || R < 0 || R >= rows())
      return;
    const auto &row = entries[R];
    bool active = row_selected(R);
    fl_push_clip(X, Y, W, H);
    fl_color(active ? Title : (!model_rows && C == 0 ? Face : FL_WHITE));
    fl_rectf(X, Y, W, H);
    fl_color(fl_rgb_color(219, 218, 206));
    fl_line(X + W - 1, Y, X + W - 1, Y + H - 1);
    fl_line(X, Y + H - 1, X + W - 1, Y + H - 1);
    fl_color(active ? FL_WHITE : Ink);
    fl_font(FL_HELVETICA, 12);
    if (model_rows && C == 0) {
      // A small native pixel glyph, with a companion in ochre.
      fl_color(active ? FL_WHITE
                      : (row.support == "Chat model"
                             ? BlueInk
                             : fl_rgb_color(150, 111, 53)));
      fl_rect(X + 5, Y + 8, 10, 12);
      fl_line(X + 7, Y + 11, X + 12, Y + 11);
      fl_line(X + 7, Y + 14, X + 12, Y + 14);
      fl_color(active ? FL_WHITE : Ink);
      cell_text(row.name, X + 20, Y + 2, W - 25, 17);
      fl_font(FL_HELVETICA, 10);
      fl_color(active ? fl_rgb_color(228, 239, 249) : Muted);
      cell_text(row.support, X + 20, Y + 18, W - 25, 15);
    } else {
      cell_text(C ? row.value : row.name, X + 5, Y, W - 10, H,
                model_rows ? FL_ALIGN_RIGHT : FL_ALIGN_LEFT);
    }
    if (active && Fl::focus() == this) {
      fl_color(Blue);
      fl_line_style(FL_DOT);
      fl_rect(X + 1, Y + 1, W - 2, H - 2);
      fl_line_style(0);
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event == FL_KEYBOARD && rows()) {
      int current = value() ? value() - 1 : 0, next = current;
      switch (Fl::event_key()) {
      case FL_Up:
        next = std::max(0, current - 1);
        break;
      case FL_Down:
        next = std::min(rows() - 1, current + 1);
        break;
      case FL_Home:
        next = 0;
        break;
      case FL_End:
        next = rows() - 1;
        break;
      case FL_Enter:
      case FL_KP_Enter:
        if (selected)
          selected(true);
        return 1;
      default:
        return 0; // Let menu shortcuts, including F12, reach the window.
      }
      value(next + 1);
      if (next < toprow || next > botrow)
        row_position(next);
      if (selected)
        selected(false);
      return 1;
    }
    int R = -1, C = -1;
    ResizeFlag flag;
    auto context = cursor2rowcol(R, C, flag);
    int result = Fl_Table_Row::handle(event);
    if (event == FL_PUSH && context == CONTEXT_CELL)
      take_focus();
    if (event == FL_RELEASE && Fl::event_button() == FL_LEFT_MOUSE) {
      if (context == CONTEXT_COL_HEADER && sorted)
        sorted(C);
      else if (context == CONTEXT_CELL && selected)
        selected(Fl::event_clicks() != 0);
    }
    if (event == FL_MOVE && context == CONTEXT_CELL && R >= 0 && R < rows()) {
      const auto &row = entries[R];
      std::string text = C ? row.value : row.name;
      if (model_rows)
        text = row.name + "\n" + row.support + " · " + row.value + " GiB";
      copy_tooltip(text.c_str());
    }
    return result;
  }
};
class PaneTitle : public Fl_Box {
public:
  PaneTitle(const char *text) : Fl_Box(0, 0, 10, 10, text) {}
  void draw() override {
    edge(x(), y(), w(), h(), color());
    fl_color(color() == Title ? Blue : Shadow);
    for (int i = 0; i < 3; ++i)
      fl_rectf(x() + 4, y() + 6 + i * 4, 3, 2);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(labelcolor());
    ClassicTable::cell_text(label() ? label() : "", x() + 12, y(), w() - 16,
                            h());
  }
};
class Transcript : public Fl_Text_Display {
public:
  std::function<void()> selected;
  Transcript() : Fl_Text_Display(0, 0, 10, 10) {}
  int handle(int event) override {
    int result = Fl_Text_Display::handle(event);
    if (event == FL_RELEASE && selected)
      selected();
    return result;
  }
};
class Composer : public Fl_Text_Editor {
public:
  std::function<void()> send;
  Composer() : Fl_Text_Editor(0, 0, 10, 10) {}
  int handle(int event) override {
    if ((event == FL_KEYBOARD || event == FL_SHORTCUT) &&
        (Fl::event_state() & FL_CTRL) &&
        (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)) {
      if (send)
        send();
      return 1;
    }
    return Fl_Text_Editor::handle(event);
  }
};
class ChatTabs : public Fl_Widget {
public:
  std::vector<std::pair<std::string, std::string>> entries;
  std::string active;
  std::function<void(const std::string &, bool)> select;
  ChatTabs() : Fl_Widget(0, 0, 10, 10) {}
  void draw() override {
    edge(x(), y(), w(), h(), Face);
    fl_push_clip(x() + 1, y() + 1, w() - 2, h() - 2);
    int left = x() + 3;
    fl_font(FL_HELVETICA, 12);
    for (const auto &e : entries) {
      int width = std::clamp(static_cast<int>(fl_width(e.second.c_str())) + 30,
                             95, 180);
      edge(left, y() + 2, width, h() - 3, e.first == active ? Title : Face);
      fl_color(e.first == active ? FL_WHITE : Ink);
      fl_rect(left + 6, y() + 8, 8, 11);
      ClassicTable::cell_text(e.second, left + 19, y() + 3, width - 39,
                              h() - 4);
      fl_line(left + width - 15, y() + 10, left + width - 9, y() + 16);
      fl_line(left + width - 15, y() + 16, left + width - 9, y() + 10);
      left += width + 2;
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
      int left = x() + 3;
      fl_font(FL_HELVETICA, 12);
      for (const auto &e : entries) {
        int width = std::clamp(
            static_cast<int>(fl_width(e.second.c_str())) + 30, 95, 180);
        if (Fl::event_x() >= left && Fl::event_x() < left + width) {
          auto id = e.first;
          if (select)
            select(id, Fl::event_x() > left + width - 22);
          return 1;
        }
        left += width + 2;
      }
    }
    return Fl_Widget::handle(event);
  }
};
class Divider : public Fl_Widget {
  bool vertical_;

public:
  std::function<void(int)> moved;
  Divider(bool vertical) : Fl_Widget(0, 0, 10, 10), vertical_(vertical) {}
  void draw() override {
    fl_color(Face);
    fl_rectf(x(), y(), w(), h());
    fl_color(Shadow);
    if (vertical_)
      fl_line(x() + w() / 2, y(), x() + w() / 2, y() + h());
    else
      fl_line(x(), y() + h() / 2, x() + w(), y() + h() / 2);
  }
  int handle(int event) override {
    if (event == FL_PUSH)
      return 1;
    if (event == FL_DRAG) {
      if (moved)
        moved(vertical_ ? Fl::event_x() : Fl::event_y());
      return 1;
    }
    return Fl_Widget::handle(event);
  }
};
class Window : public Fl_Double_Window {
  Store store;
  Settings config;
  lcb::Engine engine;
  Generation g{};
  ModelInfo info{};
  bool has_info = false, busy = false, working = false, probing = false,
       ready = false, closing = false, restoring = false, dirty = false,
       show_left = true, show_right = true, show_output = false;
  int left_width = 204, right_width = 278, output_height = 160;
  int stream_prefix = -1;
  std::string rendered_partial, rendered_style;
  std::string active, workspace, engine_path, model_path, projector,
      phase = "Chat ready. Select a model, then Load.",
      context_status = "Context counted before sending",
      health = "Engine offline", partial_prompt, partial_answer;
  Json attachments = Json::array();
  std::vector<std::string> open_chats;
  std::vector<ModelInfo> models;
  std::atomic<bool> scan_cancel{false};
  std::shared_ptr<Transport> transport;
  struct Worker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::vector<Worker> workers;
  std::mutex queue_mutex;
  std::deque<std::function<void()>> queue;
  std::chrono::steady_clock::time_point started =
                                            std::chrono::steady_clock::now(),
                                        draft_changed = started;
  Fl_Menu_Bar *menu = nullptr;
  Fl_Button *new_button, *load_button, *unload_button, *engine_button,
      *model_button, *send_button, *stop_button, *attach_button, *remove_button,
      *retry_button, *edit_button, *workspace_button;
  Fl_Input *model_name;
  Fl_Int_Input *port_input;
  Fl_Box *brand, *left_header, *center_header, *right_header, *phase_box,
      *message_header, *status, *media_status, *output_header;
  Fl_Tree *tree;
  ChatTabs *tabs;
  Fl_Choice *exchange;
  Transcript *transcript;
  Composer *prompt;
  Fl_Text_Buffer text_buffer, style_buffer, prompt_buffer, activity_buffer,
      log_buffer, thinking_buffer, master_buffer;
  Fl_Text_Display::Style_Table_Entry styles[7] = {
      {Ink, FL_HELVETICA, 14, 0, FL_WHITE},
      {BlueInk, FL_HELVETICA_BOLD, 14, 0, FL_WHITE},
      {Ink, FL_HELVETICA_BOLD, 14, 0, FL_WHITE},
      {Ink, FL_HELVETICA_ITALIC, 14, 0, FL_WHITE},
      {Ink, FL_COURIER, 13, Fl_Text_Display::ATTR_BGCOLOR, Light},
      {BlueInk, FL_HELVETICA_BOLD, 16, 0, FL_WHITE},
      {Muted, FL_HELVETICA, 13, 0, FL_WHITE}};
  Fl_Hold_Browser *attachment_list;
  ClassicTable *model_list, *details;
  bool descending = false;
  int sort_column = 0;
  Fl_Tabs *right_tabs, *output_tabs;
  Fl_Group *models_page, *generation_page, *library_page, *activity_page,
      *log_page, *thinking_page;
  Fl_Box *properties_label, *projector_label, *generation_help, *folder_label,
      *master_label;
  Fl_Text_Display *master, *activity, *live_log, *thinking;
  Fl_Input *projector_text, *folder;
  Fl_Button *use_model_button, *projector_button, *projector_clear,
      *folder_button, *scan_button;
  Fl_Check_Button *recursive;
  Fl_Value_Input *generation_fields[GenCount]{};
  Fl_Box *generation_labels[GenCount]{};
  Fl_Choice *thinking_choice;
  Divider *left_divider, *right_divider, *output_divider;
  std::map<Fl_Widget *, std::function<void()>> callbacks;
  std::map<Fl_Tree_Item *, std::pair<bool, std::string>> tree_ids;
  std::vector<std::pair<int, int>> positions;

public:
  explicit Window(const fs::path &root)
      : Fl_Double_Window(1240, 820, "lcb-ai"), store(root), config(store.root) {
    xclass("lcb-ai");
    color(Face);
    size_range(1000, 680);
    begin();
    workspace = store.workspaces.begin()->first;
    engine_path = config.get("engine", "executable");
    model_path = config.get("engine", "model");
    projector = config.get(config.section(model_path), "projector");
    g = config.generation(model_path);
    build();
    end();
    callback(
        +[](Fl_Widget *, void *v) {
          static_cast<Window *>(v)->request_close();
        },
        this);
    prompt_buffer.add_modify_callback(
        +[](int, int inserted, int deleted, int, const char *, void *v) {
          auto &w = *static_cast<Window *>(v);
          if (!w.restoring && (inserted || deleted)) {
            w.dirty = true;
            w.draft_changed = std::chrono::steady_clock::now();
          }
        },
        this);
    try {
      auto saved = Json::parse(config.get("linux-session", "tabs", "[]"));
      cJSON *p;
      cJSON_ArrayForEach(p, saved.p) {
        if (cJSON_IsString(p) && store.chats.count(p->valuestring) &&
            open_chats.size() < 32)
          open_chats.emplace_back(p->valuestring);
      }
      active = config.get("linux-session", "active");
      if (!store.chats.count(active) ||
          std::find(open_chats.begin(), open_chats.end(), active) ==
              open_chats.end())
        active = open_chats.empty() ? "" : open_chats.back();
      auto sizes =
          Json::parse(config.get("linux-layout", "sizes", "[204,730,278]"));
      if (cJSON_GetArraySize(sizes.p) == 3) {
        left_width =
            std::clamp(cJSON_GetArrayItem(sizes.p, 0)->valueint, 160, 400);
        right_width =
            std::clamp(cJSON_GetArrayItem(sizes.p, 2)->valueint, 240, 500);
      }
      auto vertical =
          Json::parse(config.get("linux-layout", "vertical", "[640,160]"));
      if (cJSON_GetArraySize(vertical.p) == 2)
        output_height =
            std::clamp(cJSON_GetArrayItem(vertical.p, 1)->valueint, 100, 300);
    } catch (const std::exception &) {
    }
    show_left = config.get("linux-layout", "left", "1") != "0";
    show_right = config.get("linux-layout", "right", "1") != "0";
    if (active.empty())
      new_chat();
    else
      show_chat();
    refresh_tree();
    update_generation();
    layout();
    controls();
    note("FLTK workbench ready. Storage: " + store.root.string());
    if (store.skipped)
      note(std::to_string(store.skipped) +
           " invalid saved files were left untouched.");
    Fl::add_timeout(1, tick_cb, this);
    if (!model_path.empty())
      read_selected_model(model_path, false, true);
    else if (*folder->value())
      scan();
  }
  ~Window() override {
    scan_cancel = true;
    if (transport)
      transport->cancel();
    for (auto &worker : workers)
      if (worker.thread.joinable())
        worker.thread.join();
    Fl::remove_timeout(tick_cb, this);
    transcript->buffer(nullptr);
    transcript->highlight_data(nullptr, nullptr, 0, 'A', nullptr, nullptr);
    prompt->buffer(nullptr);
    master->buffer(nullptr);
    activity->buffer(nullptr);
    live_log->buffer(nullptr);
    thinking->buffer(nullptr);
  }
  void resize(int x, int y, int width, int height) override {
    Fl_Double_Window::resize(x, y, width, height);
    if (menu)
      layout();
  }
  int handle(int event) override {
    if ((event == FL_SHORTCUT || event == FL_KEYBOARD) &&
        !(Fl::event_state() & (FL_CTRL | FL_ALT | FL_SHIFT))) {
      int key = Fl::event_key();
      if (key == FL_F + 9 || key == FL_F + 10 || key == FL_F + 12) {
        if (key == FL_F + 9)
          show_left = !show_left;
        else if (key == FL_F + 10)
          show_right = !show_right;
        else
          show_output = !show_output;
        layout();
        return 1;
      }
    }

    if ((event == FL_SHORTCUT || event == FL_KEYBOARD) &&
        (Fl::event_state() & FL_CTRL) &&
        (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)) {
      guard([&] { send(); });
      return 1;
    }
    return Fl_Double_Window::handle(event);
  }


private:
  void guard(const std::function<void()> &fn) {
    try {
      fn();
    } catch (const std::exception &e) {
      error(e.what());
    }
  }
  static void action_cb(Fl_Widget *widget, void *data) {
    auto &w = *static_cast<Window *>(data);
    auto fn = w.callbacks.at(widget);
    w.guard(fn);
  }
  template <class T> T *bind(T *widget, std::function<void()> fn) {
    callbacks[widget] = std::move(fn);
    widget->callback(action_cb, this);
    return widget;
  }
  Fl_Button *button(const char *label, std::function<void()> fn) {
    auto b = new Fl_Button(0, 0, 10, 10, label);
    b->labelsize(12);
    b->color(Face);
    b->selection_color(Blue);
    b->box(FL_UP_BOX);
    return bind(b, std::move(fn));
  }
  Fl_Box *label(const char *text, bool header = false) {
    Fl_Box *b = header ? new PaneTitle(text) : new Fl_Box(0, 0, 10, 10, text);
    b->labelsize(12);
    b->labelcolor(Ink);
    b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    b->box(header ? FL_THIN_UP_BOX : FL_NO_BOX);
    b->color(Face);
    if (header)
      b->labelfont(FL_HELVETICA_BOLD);
    return b;
  }
  Fl_Input *input() {
    auto i = new Fl_Input(0, 0, 10, 10);
    i->textsize(12);
    i->color(FL_WHITE);
    return i;
  }
  Fl_Text_Display *display(Fl_Text_Buffer &buffer) {
    auto d = new Fl_Text_Display(0, 0, 10, 10);
    d->buffer(&buffer);
    d->textfont(FL_COURIER);
    d->textsize(12);
    d->color(FL_WHITE);
    d->selection_color(Blue);
    d->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    return d;
  }
  void menu_action(const char *path, int shortcut, std::function<void()> fn) {
    auto key = std::make_unique<std::function<void()>>(std::move(fn));
    auto raw = key.get();
    menu_callbacks.push_back(std::move(key));
    menu->add(
        path, shortcut,
        +[](Fl_Widget *, void *data) {
          auto fn = static_cast<std::function<void()> *>(data);
          (*fn)();
        },
        raw);
  }
  std::vector<std::unique_ptr<std::function<void()>>> menu_callbacks;
  void build() {
    menu = new Fl_Menu_Bar(0, 0, w(), 22);
    menu->textsize(12);
    menu->box(FL_FLAT_BOX);
    menu->color(Face);
    auto add = [&](const char *name, int key, std::function<void()> fn) {
      menu_action(name, key, [this, fn] { guard(fn); });
    };
    add("&File/&New chat", FL_CTRL + 'n', [&] { new_chat(); });
    add("&File/New workspace...", 0, [&] { edit_workspace(true); });
    add("&File/Close chat", FL_CTRL + 'w', [&] { close_chat(active); });
    add("&File/Delete chat...", 0, [&] { delete_chat(); });
    add("&File/Quit", FL_CTRL + 'q', [&] { request_close(); });
    add("&Edit/Copy", FL_CTRL + 'c', [&] {
      if (Fl::focus() == prompt)
        prompt->kf_copy(0, prompt);
      else if (text_buffer.selected()) {
        auto text = text_buffer.selection_text();
        Fl::copy(text, static_cast<int>(std::strlen(text)), 1);
        free(text);
      }
    });
    add("&Edit/Workspace settings...", 0, [&] { edit_workspace(false); });
    add("&View/Workspace Explorer", FL_F + 9, [&] {
      show_left = !show_left;
      layout();
    });
    add("&View/Properties and Models", FL_F + 10, [&] {
      show_right = !show_right;
      layout();
    });
    add("&View/Show or hide Output", FL_F + 12, [&] {
      show_output = !show_output;
      layout();
    });
    add("&View/Reset layout", 0, [&] {
      show_left = show_right = true;
      show_output = false;
      left_width = 204;
      right_width = 278;
      output_height = 160;
      layout();
    });
    add("&Engine/Load model", FL_F + 5, [&] { load_model(); });
    add("&Engine/Unload", 0, [&] { unload(); });
    add("&Engine/Stop response", FL_Escape, [&] { stop(); });
    add("&Engine/Engine details and live log", 0, [&] { engine_details(); });
    add("&Settings/Engine and model...", FL_CTRL + ',',
        [&] { settings_dialog(); });
    add("&Settings/Setup...", 0, [&] { setup(); });
    add("&Help/About", 0, [&] {
      fl_message("LCB-AI " LCB_VERSION
                 "\nLarkin Computing Bureau\n\nNative Linux workbench: "
                 "C++ / FLTK 1.4\nShared C request, model and streaming core.");
    });
    new_button = button("New", [&] { new_chat(); });
    load_button = button("@>  Load", [&] { load_model(); });
    unload_button = button("Unload", [&] { unload(); });
    engine_button = button("Engine...", [&] { choose_engine(); });
    model_button = button("Model...", [&] { choose_model(); });
    model_name = input();
    model_name->readonly(1);
    model_name->value(fs::path(model_path).filename().c_str());
    port_input = new Fl_Int_Input(0, 0, 10, 10);
    port_input->textsize(12);
    port_input->value(config.get("engine", "port", "8080").c_str());
    port_input->when(FL_WHEN_RELEASE);
    bind(port_input, [&] {
      if (busy || engine.owned())
        throw std::runtime_error("Unload the engine before changing its port.");
      port();
      config.set("engine", "port", port_input->value());
      config.save();
      ready = false;
    });
    brand = label("lcb-ai");
    brand->labelfont(FL_HELVETICA_BOLD);
    brand->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    left_header = label("Workspace Explorer", true);
    workspace_button = button("Workspace...", [&] { edit_workspace(false); });
    tree = new Fl_Tree(0, 0, 10, 10);
    tree->showroot(0);
    tree->color(FL_WHITE);
    tree->selection_color(Blue);
    tree->item_labelsize(12);
    tree->item_labelfgcolor(Ink);
    tree->connectorstyle(FL_TREE_CONNECTOR_DOTTED);
    bind(tree, [&] {
      auto item = tree->callback_item();
      if (!item || !tree_ids.count(item))
        return;
      auto id = tree_ids.at(item);
      if (Fl::event_button() == FL_RIGHT_MOUSE) {
        Fl_Menu_Item items[4]{};
        items[0].text = "Open";
        items[1].text = "Close";
        items[2].text = "Delete...";
        if (!id.first) {
          auto selected = items->popup(Fl::event_x(), Fl::event_y());
          if (selected == &items[0])
            open_chat(id.second);
          else if (selected == &items[1])
            close_chat(id.second);
          else if (selected == &items[2]) {
            open_chat(id.second);
            delete_chat();
          }
        }
      } else if (id.first) {
        if (!busy)
          workspace = id.second;
      } else
        open_chat(id.second);
    });
    center_header = label("Conversations", true);
    center_header->color(Title);
    center_header->labelcolor(FL_WHITE);
    tabs = new ChatTabs();
    tabs->select = [&](const std::string &id, bool close) {
      guard([&] {
        if (close)
          close_chat(id);
        else
          open_chat(id);
      });
    };
    exchange = new Fl_Choice(0, 0, 10, 10);
    exchange->textsize(12);
    exchange->color(FL_WHITE);
    bind(exchange, [&] { select_exchange(); });
    retry_button = button("Retry", [&] { retry(false); });
    edit_button = button("Edit and resend...", [&] { retry(true); });
    phase_box = label(phase.c_str());
    phase_box->copy_label(phase.c_str());
    phase_box->box(FL_FLAT_BOX);
    phase_box->color(fl_rgb_color(228, 237, 243));
    phase_box->labelcolor(BlueInk);
    transcript = new Transcript();
    transcript->buffer(&text_buffer);
    transcript->highlight_data(&style_buffer, styles, 7, 'A', nullptr, nullptr);
    transcript->textsize(14);
    transcript->color(FL_WHITE);
    transcript->selection_color(Blue);
    transcript->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    transcript->selected = [&] {
      if (busy)
        return;
      auto pos = transcript->insert_position();
      for (size_t i = 0; i < positions.size(); ++i) {
        if (pos >= positions[i].first && pos <= positions[i].second) {
          exchange->value(static_cast<int>(i));
          select_exchange(false);
          break;
        }
      }
    };
    message_header = label(
        "Message                                      Ctrl+Enter to send");
    attachment_list = new Fl_Hold_Browser(0, 0, 10, 10);
    attachment_list->textsize(12);
    attachment_list->color(FL_WHITE);
    bind(attachment_list, [this] {
      int index = attachment_list->value() - 1;
      if (!Fl::event_clicks() || index < 0)
        return;
      Json item(cJSON_Duplicate(cJSON_GetArrayItem(attachments.p, index), 1));
      Json list = Json::array();
      list.add(item);
      validate_attachments(list);
      auto path = store.root / "attachments" / item.str("file");
      if (fs::is_symlink(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Attachment is unavailable.");
      std::string url = "file://";
      const char *hex = "0123456789ABCDEF";
      for (unsigned char c : path.string()) {
        if (std::isalnum(c) || c == '/' || c == '.' || c == '-' || c == '_')
          url += static_cast<char>(c);
        else {
          url += '%';
          url += hex[c >> 4];
          url += hex[c & 15];
        }
      }
      open_url(url);
    });
    attach_button = button("Attach...", [&] { attach(); });
    remove_button = button("Remove", [&] { remove_attachment(); });
    media_status = label("Text / image / audio / video");
    media_status->labelcolor(Muted);
    prompt = new Composer();
    prompt->send = [this] { guard([&] { send(); }); };
    prompt->buffer(&prompt_buffer);
    prompt->textfont(FL_HELVETICA);
    prompt->textsize(14);
    prompt->color(FL_WHITE);
    prompt->selection_color(Blue);
    prompt->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    send_button = button("@>  Send", [&] { send(); });
    stop_button = button("Stop", [&] { stop(); });
    right_header = label("Properties and Models", true);
    right_tabs = new Fl_Tabs(0, 0, 10, 10);
    right_tabs->labelsize(12);
    right_tabs->selection_color(Blue);
    right_tabs->begin();
    models_page = new Fl_Group(0, 0, 10, 10, "Models");
    models_page->labelsize(12);
    models_page->begin();
    model_list = new ClassicTable(true);
    model_list->sorted = [this](int col) { guard([&] { sort_models(col); }); };
    model_list->selected = [this](bool activate) {
      guard([&] {
        inspect_model();
        if (activate)
          use_model();
      });
    };
    model_list->tooltip("Select to inspect. Double-click or Enter to use a "
                        "model. Click a column header to sort.");
    use_model_button = button("Use selected model", [&] { use_model(); });
    properties_label = label("Model properties", true);
    details = new ClassicTable(false);
    details->entries = {{"Model", "Select a model", ""},
                        {"Use", "Double-click / Enter", ""}};
    details->refresh();
    projector_label = label("Media projector (reload to apply)");
    projector_text = input();
    projector_text->readonly(1);
    projector_text->value(projector.c_str());
    projector_button = button("Choose...", [&] { choose_projector(); });
    projector_clear = button("Text only", [&] { set_projector(""); });
    models_page->end();
    generation_page = new Fl_Group(0, 0, 10, 10, "Generation");
    generation_page->labelsize(12);
    generation_page->begin();
    const double minimum[] = {1, 0, 0.000001, 512, 0, 1, 0, 0, 0, -2},
                 maximum[] = {16384, 2, 1, 1048576, 2, 2, 4, 1000, 1, 2};
    for (int i = 0; i < GenCount; ++i) {
      generation_labels[i] = label(labels[i]);
      if (i == GenThinking) {
        thinking_choice = new Fl_Choice(0, 0, 10, 10);
        thinking_choice->add("Auto|Off|On");
        thinking_choice->textsize(12);
        thinking_choice->color(FL_WHITE);
        bind(thinking_choice,
             [&] { edit_generation(GenThinking, thinking_choice->value()); });
      } else {
        auto f = new Fl_Value_Input(0, 0, 10, 10);
        f->textsize(12);
        f->color(FL_WHITE);
        f->range(minimum[i], maximum[i]);
        f->step(i == GenResponse || i == GenContext || i == GenTopK ? 1
                                                                    : 0.000001);
        f->when(FL_WHEN_RELEASE);
        generation_fields[i] =
            bind(f, [this, i, f] { edit_generation(i, f->value()); });
      }
    }
    generation_help = label(
        "Saved per model. Reload after changing context.\n\nThinking: Auto "
        "uses the model template.\nEmitted traces appear in Output > "
        "Thinking.\n\nRepeat: 1 = off.\nDRY, Top K, Min P, Presence: 0 = off.");
    generation_help->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE |
                           FL_ALIGN_WRAP);
    generation_help->labelcolor(Muted);
    generation_page->end();
    library_page = new Fl_Group(0, 0, 10, 10, "Library");
    library_page->labelsize(12);
    library_page->begin();
    folder_label = label("Model folder");
    folder = input();
    folder->value(config.get("library", "folder").c_str());
    folder_button = button("Folder...", [&] { choose_folder(); });
    scan_button = button("Scan", [&] { scan(); });
    recursive = new Fl_Check_Button(0, 0, 10, 10, "Include subfolders");
    recursive->labelsize(12);
    recursive->value(config.get("library", "recursive", "1") != "0");
    master_label = label("Workspace master prompt", true);
    master = display(master_buffer);
    master->textfont(FL_HELVETICA);
    library_page->end();
    right_tabs->end();
    output_header = label("Output", true);
    output_tabs = new Fl_Tabs(0, 0, 10, 10);
    output_tabs->selection_color(Blue);
    output_tabs->begin();
    activity_page = new Fl_Group(0, 0, 10, 10, "Session activity");
    activity_page->labelsize(12);
    activity = display(activity_buffer);
    activity_page->end();
    log_page = new Fl_Group(0, 0, 10, 10, "Live llama.cpp log");
    log_page->labelsize(12);
    live_log = display(log_buffer);
    log_page->end();
    thinking_page = new Fl_Group(0, 0, 10, 10, "Thinking");
    thinking_page->labelsize(12);
    thinking = display(thinking_buffer);
    thinking_page->end();
    output_tabs->end();
    status = label("Engine offline");
    status->box(FL_DOWN_BOX);
    left_divider = new Divider(true);
    left_divider->moved = [&](int x) {
      left_width = std::clamp(x - 4, 160, w() - right_width - 390);
      layout();
    };
    right_divider = new Divider(true);
    right_divider->moved = [&](int x) {
      right_width = std::clamp(w() - x - 4, 240, w() - left_width - 390);
      layout();
    };
    output_divider = new Divider(false);
    output_divider->moved = [&](int y) {
      output_height = std::clamp(h() - y - 30, 100, h() - 490);
      layout();
    };
  }
  static void place(Fl_Widget *w, int x, int y, int width, int height) {
    w->resize(x, y, std::max(1, width), std::max(1, height));
  }
  static void visible(Fl_Widget *w, bool show) {
    if (show)
      w->show();
    else
      w->hide();
  }
  void layout() {
    place(menu, 0, 0, w(), 22);
    int top = 25;
    place(new_button, 5, top, 75, 25);
    place(load_button, 85, top, 75, 25);
    place(unload_button, 165, top, 75, 25);
    place(engine_button, 250, top, 84, 25);
    place(model_button, 339, top, 80, 25);
    place(model_name, 425, top, w() - 595, 25);
    place(port_input, w() - 165, top, 64, 25);
    place(brand, w() - 94, top, 86, 25);
    int bottom = h() - 27 - (show_output ? output_height + 30 : 0);
    int left = show_left
                   ? std::clamp(left_width, 160,
                                w() - (show_right ? right_width : 0) - 390)
                   : 0,
        right = show_right ? std::clamp(right_width, 240, w() - left - 390) : 0;
    int cx = left ? left + 12 : 5, cw = w() - cx - right - (right ? 12 : 5),
        rx = w() - right - 5;
    visible(left_header, show_left);
    visible(workspace_button, show_left);
    visible(tree, show_left);
    visible(left_divider, show_left);
    place(left_header, 5, 56, left, 22);
    place(workspace_button, 8, 82, left - 6, 25);
    place(tree, 7, 111, left - 4, h() - 140);
    place(left_divider, left + 5, 56, 6, h() - 83);
    place(center_header, cx, 56, cw, 22);
    place(tabs, cx, 80, cw, 28);
    place(exchange, cx + 2, 112, cw - 205, 25);
    place(retry_button, cx + cw - 199, 112, 62, 25);
    place(edit_button, cx + cw - 132, 112, 130, 25);
    place(phase_box, cx + 2, 141, cw - 4, 26);
    bool has_attachments = cJSON_GetArraySize(attachments.p) > 0;
    int extra = has_attachments ? 45 : 0;
    int composer = bottom - 123 - extra;
    place(transcript, cx, 171, cw, composer - 171);
    place(message_header, cx + 4, composer + 2, cw - 8, 22);
    visible(attachment_list, has_attachments);
    place(attachment_list, cx, composer + 27, cw, 42);
    int ay = composer + 27 + extra;
    place(attach_button, cx, ay, 80, 23);
    place(remove_button, cx + 85, ay, 75, 23);
    place(media_status, cx + 166, ay, cw - 168, 23);
    place(prompt, cx, ay + 27, cw - 78, bottom - ay - 29);
    place(send_button, cx + cw - 72, ay + 27, 72, 28);
    place(stop_button, cx + cw - 72, ay + 59, 72, 28);
    visible(right_header, show_right);
    visible(right_tabs, show_right);
    visible(right_divider, show_right);
    int sidebar_bottom = h() - 27;
    place(right_header, rx, 56, right, 22);
    place(right_divider, rx - 7, 56, 6, h() - 83);
    place(right_tabs, rx, 80, right, sidebar_bottom - 80);
    for (auto page : {models_page, generation_page, library_page})
      place(page, rx + 2, 106, right - 4, sidebar_bottom - 108);
    int pw = right - 12, px = rx + 6, py = 110, ph = sidebar_bottom - py;
    int list_height = std::max(75, ph - 352);
    place(model_list, px, py, pw, list_height + 25);
    place(use_model_button, px, py + 29 + list_height, pw, 25);
    int property_y = py + 59 + list_height;
    place(properties_label, px, property_y, pw, 22);
    place(details, px, property_y + 24, pw, sidebar_bottom - property_y - 119);
    place(projector_label, px, sidebar_bottom - 88, pw, 22);
    place(projector_text, px, sidebar_bottom - 65, pw, 25);
    place(projector_button, px, sidebar_bottom - 34, pw / 2 - 2, 26);
    place(projector_clear, px + pw / 2 + 2, sidebar_bottom - 34, pw / 2 - 2,
          26);
    for (int i = 0; i < GenCount; ++i) {
      place(generation_labels[i], px, py + i * 32, pw - 114, 27);
      place(i == GenThinking ? static_cast<Fl_Widget *>(thinking_choice)
                             : generation_fields[i],
            px + pw - 110, py + i * 32, 110, 27);
    }
    place(generation_help, px, py + 328, pw,
          std::max(100, sidebar_bottom - py - 338));
    place(folder_label, px, py, pw, 22);
    place(folder, px, py + 24, pw, 25);
    place(folder_button, px, py + 54, pw / 2 - 2, 26);
    place(scan_button, px + pw / 2 + 2, py + 54, pw / 2 - 2, 26);
    place(recursive, px, py + 87, pw, 24);
    place(master_label, px, py + 120, pw, 22);
    place(master, px, py + 147, pw, sidebar_bottom - py - 155);
    visible(output_header, show_output);
    visible(output_tabs, show_output);
    visible(output_divider, show_output);
    place(output_divider, cx, bottom + 1, cw, 7);
    place(output_header, cx, bottom + 8, cw, 22);
    place(output_tabs, cx, bottom + 32, cw, output_height - 6);
    for (auto page : {activity_page, log_page, thinking_page})
      place(page, cx + 2, bottom + 58, cw - 4, output_height - 34);
    for (auto display : {activity, live_log, thinking})
      place(display, cx + 4, bottom + 60, cw - 8, output_height - 38);
    place(status, 5, h() - 24, w() - 10, 21);
    redraw();
  }
  template <class Fn> void launch(Fn fn) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    workers.push_back({std::thread([fn = std::move(fn), done] {
                         fn();
                         *done = true;
                       }),
                       done});
  }
  void reap_workers() {
    auto it = workers.begin();
    while (it != workers.end()) {
      if (*it->done) {
        it->thread.join();
        it = workers.erase(it);
      } else
        ++it;
    }
  }
  void post(std::function<void()> fn) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      queue.push_back(std::move(fn));
    }
    Fl::awake(awake_cb, this);
  }
  static void awake_cb(void *data) { static_cast<Window *>(data)->drain(); }
  void drain() {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      pending.swap(queue);
    }
    for (auto &fn : pending)
      guard(fn);
    if (closing && !busy && !working && !probing)
      guard([&] { finish_close(); });
  }
  template <class Work, class Done> void job(Work work, Done done) {
    working = true;
    controls();
    launch([this, work, done] {
      try {
        auto result = work();
        post([this, done, result = std::move(result)] {
          working = false;
          guard([&] { done(result); });
          controls();
        });
      } catch (const std::exception &e) {
        std::string detail = e.what();
        post([this, detail] {
          working = false;
          set_phase(detail);
          error(detail);
          controls();
        });
      }
    });
  }
  static void tick_cb(void *data) {
    auto &w = *static_cast<Window *>(data);
    w.guard([&] { w.tick(); });
    if (w.shown())
      Fl::repeat_timeout(1, tick_cb, data);
  }
  void tick() {
    if (exit_requested && !closing) {
      request_close();
      if (!shown())
        return;
    }
    reap_workers();
    if (dirty && !busy &&
        std::chrono::steady_clock::now() - draft_changed >
            std::chrono::milliseconds(700))
      save_draft();
    if (busy) {
      auto elapsed = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - started)
                         .count();
      std::ostringstream s;
      s << phase << "  |  " << static_cast<int>(elapsed) << "s";
      phase_box->copy_label(s.str().c_str());
    }
    if (probing || working)
      return;
    probing = true;
    auto p = port();
    auto exe = engine_path, model = model_path;
    launch([this, p, exe, model] {
      std::string logs = engine.read_log(), error_text;
      int code = 0;
      bool exit = engine.exited(code), online = false, loading = false;
      if (exit)
        error_text = "Engine exited (" + std::to_string(code) + "). " +
                     engine.failure() + "\nEngine: " + exe +
                     "\nModel: " + model + "\n" + engine.log_tail();
      if (!exit) {
        try {
          Transport t(p);
          t.exchange("/health", nullptr, {}, 2);
          online = true;
        } catch (const HttpError &e) {
          loading = e.status == 503;
        } catch (const std::exception &) {
        }
      }
      post([this, logs, error_text, online, loading, exit] {
        probing = false;
        if (!logs.empty()) {
          log_buffer.append(logs.c_str());
          trim_log(log_buffer, 160000);
          live_log->insert_position(log_buffer.length());
          live_log->show_insert_position();
        }
        bool became_ready = online && !ready;
        ready = online;
        if (became_ready && !busy)
          set_phase("Model ready. Write a message and Send.");
        health = online           ? "Engine ready"
                 : loading        ? "Engine loading"
                 : engine.owned() ? "Engine starting"
                                  : "Engine offline";
        if (exit) {
          set_phase(error_text.substr(0, error_text.find('\n')));
          note(error_text);
          show_output = true;
          output_tabs->value(log_page);
          layout();
        }
        controls();
      });
    });
  }
  void controls() {
    bool idle = !busy && !working && !closing;
    for (auto w : {new_button, engine_button, model_button, workspace_button,
                   scan_button, folder_button, use_model_button,
                   projector_button, projector_clear, attach_button,
                   remove_button, retry_button, edit_button}) {
      if (idle)
        w->activate();
      else
        w->deactivate();
    }
    if (idle && ready && !model_path.empty() && !active.empty())
      send_button->activate();
    else
      send_button->deactivate();
    if (busy)
      stop_button->activate();
    else
      stop_button->deactivate();
    if (idle && !engine.owned())
      load_button->activate();
    else
      load_button->deactivate();
    if (idle && engine.owned())
      unload_button->activate();
    else
      unload_button->deactivate();
    if (idle && !engine.owned())
      port_input->activate();
    else
      port_input->deactivate();
    if (busy)
      prompt->deactivate();
    else
      prompt->activate();
    for (int i = 0; i < GenCount; ++i) {
      auto f = i == GenThinking ? static_cast<Fl_Widget *>(thinking_choice)
                                : generation_fields[i];
      if (idle)
        f->activate();
      else
        f->deactivate();
    }
    auto line = health + "  |  127.0.0.1:" + std::string(port_input->value()) +
                "  |  " + context_status;
    status->copy_label(line.c_str());
  }
  unsigned short port() {
    size_t used = 0;
    auto s = std::string(port_input->value());
    int value = std::stoi(s, &used);
    if (used != s.size() || value < 1 || value > 65535)
      throw std::runtime_error("Port must be between 1 and 65535.");
    return static_cast<unsigned short>(value);
  }
  void note(const std::string &s) {
    auto now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char time[32];
    std::strftime(time, sizeof(time), "%H:%M:%S", std::localtime(&now));
    activity_buffer.append((std::string(time) + "  " + s + "\n").c_str());
    trim_log(activity_buffer, 64000);
    activity->insert_position(activity_buffer.length());
    activity->show_insert_position();
  }
  static void trim_log(Fl_Text_Buffer &b, int max) {
    if (b.length() > max)
      b.remove(0, b.line_end(b.length() - max) + 1);
  }
  void error(const std::string &s) {
    note(s);
    fl_alert("%s", s.c_str());
  }
  void set_phase(const std::string &s) {
    phase = s;
    phase_box->copy_label(s.c_str());
    note(s);
  }
  void refresh_tree() {
    tree_ids.clear();
    tree->clear();
    tree->root(new Fl_Tree_Item(tree));
    for (const auto &ws : store.workspaces) {
      auto parent =
          tree->insert(tree->root(), clean_label(ws.second.str("name")).c_str(),
                       tree->root()->children());
      tree_ids[parent] = {true, ws.first};
      for (const auto &chat : store.chats)
        if (chat.second.str("workspace") == ws.first) {
          auto child = tree->insert(
              parent, clean_label(chat.second.str("title")).c_str(),
              parent->children());
          tree_ids[child] = {false, chat.first};
          if (chat.first == active)
            tree->select(child, 0);
        }
    }
  }
  void refresh_tabs() {
    tabs->entries.clear();
    for (const auto &id : open_chats)
      if (store.chats.count(id))
        tabs->entries.emplace_back(id, store.chats.at(id).str("title"));
    tabs->active = active;
    tabs->redraw();
  }
  void save_draft() {
    if (active.empty() || !store.chats.count(active))
      return;
    Json candidate = store.chats.at(active);
    candidate.set("draft", buffer_text(prompt_buffer));
    candidate.set("draft_attachments", attachments);
    store.save("chats", candidate);
    dirty = false;
  }
  void open_chat(const std::string &id) {
    if (busy || working)
      return;
    if (!store.chats.count(id))
      return;
    save_draft();
    if (std::find(open_chats.begin(), open_chats.end(), id) ==
        open_chats.end()) {
      if (open_chats.size() >= 32)
        throw std::runtime_error("Close a chat tab before opening another.");
      open_chats.push_back(id);
    }
    active = id;
    show_chat();
    refresh_tree();
  }
  void show_chat() {
    if (active.empty())
      return;
    const auto &chat = store.chats.at(active);
    workspace = chat.str("workspace");
    restoring = true;
    set_text(prompt_buffer, chat.str("draft"));
    restoring = false;
    dirty = false;
    attachments = chat.get("draft_attachments")
                      ? chat.object("draft_attachments")
                      : Json::array();
    set_text(master_buffer,
             store.workspaces.at(workspace).str("master_prompt"));
    exchange->clear();
    auto messages = chat.get("messages");
    int count = cJSON_GetArraySize(messages) / 2;
    for (int i = 0; i < count; ++i) {
      Json m(cJSON_Duplicate(cJSON_GetArrayItem(messages, i * 2), 1));
      auto name = menu_label(std::to_string(i + 1) + ". " +
                             truncate(m.str("content"), 60));
      exchange->add(name.c_str());
    }
    if (count)
      exchange->value(count - 1);
    refresh_attachments();
    render();
    refresh_tabs();
    select_exchange(false);
    layout();
  }
  void new_chat() {
    if (busy || working)
      return;
    save_draft();
    for (const auto &c : store.chats) {
      if (c.second.str("workspace") == workspace &&
          cJSON_GetArraySize(c.second.get("messages")) == 0 &&
          c.second.str("draft").empty() &&
          (!c.second.get("draft_attachments") ||
           cJSON_GetArraySize(c.second.get("draft_attachments")) == 0)) {
        open_chat(c.first);
        return;
      }
    }
    auto chat = store.new_chat(workspace);
    open_chat(chat.str("id"));
  }
  void close_chat(const std::string &id) {
    if (busy || working)
      return;
    save_draft();
    open_chats.erase(std::remove(open_chats.begin(), open_chats.end(), id),
                     open_chats.end());
    if (active == id) {
      active = open_chats.empty() ? "" : open_chats.back();
      if (active.empty()) {
        restoring = true;
        set_text(prompt_buffer, "");
        restoring = false;
        attachments = Json::array();
        set_text(text_buffer, "");
        set_text(style_buffer, "");
        set_text(thinking_buffer, "");
        exchange->clear();
        refresh_attachments();
      } else
        show_chat();
    }
    refresh_tabs();
    layout();
  }
  void delete_chat() {
    if (busy || working || active.empty())
      return;
    if (fl_choice("Delete this saved conversation? This cannot be undone.",
                  "Cancel", "Delete", nullptr) != 1)
      return;
    auto id = active;
    store.erase(id);
    open_chats.erase(std::remove(open_chats.begin(), open_chats.end(), id),
                     open_chats.end());
    active = open_chats.empty() ? "" : open_chats.back();
    if (active.empty())
      new_chat();
    else
      show_chat();
    refresh_tree();
  }
  void append(std::string &text, std::string &style, const std::string &s,
              char code) {
    text += s;
    style.append(s.size(), code);
  }
  void markdown(std::string &text, std::string &style, const std::string &s) {
    struct Output {
      Window *window;
      std::string *text, *style;
    } out{this, &text, &style};
    markdown_render(
        wide(s).c_str(),
        +[](void *v, const wchar_t *s, unsigned flags) {
          auto &o = *static_cast<Output *>(v);
          char code = flags & MdCode      ? 'E'
                      : flags & MdHeading ? 'F'
                      : flags & MdBold    ? 'C'
                      : flags & MdItalic  ? 'D'
                      : flags & MdQuote   ? 'G'
                                          : 'A';
          o.window->append(*o.text, *o.style, utf8(s), code);
        },
        &out);
  }
  void render_partial() {
    std::string text, style;
    markdown(text, style, partial_answer);
    size_t common = 0;
    while (common < text.size() && common < rendered_partial.size() &&
           text[common] == rendered_partial[common] &&
           style[common] == rendered_style[common])
      ++common;
    while (common && common < text.size() &&
           (static_cast<unsigned char>(text[common]) & 0xc0) == 0x80)
      --common;
    if (common != text.size() || common != rendered_partial.size()) {
      auto offset = stream_prefix + static_cast<int>(common);
      style_buffer.replace(offset, style_buffer.length(),
                           style.c_str() + common);
      text_buffer.replace(offset, text_buffer.length(), text.c_str() + common);
      rendered_partial = std::move(text);
      rendered_style = std::move(style);
      transcript->insert_position(text_buffer.length());
      transcript->show_insert_position();
    }
  }
  void render() {
    if (busy && stream_prefix >= 0) {
      render_partial();
      return;
    }
    stream_prefix = -1;
    std::string text, style;
    positions.clear();
    if (!active.empty()) {
      const auto &chat = store.chats.at(active);
      auto messages = chat.get("messages");
      int i = 0;
      int begin = 0;
      cJSON *p;
      cJSON_ArrayForEach(p, messages) {
        Json m(cJSON_Duplicate(p, 1));
        if (!(i % 2))
          begin = static_cast<int>(text.size());
        append(text, style,
               i % 2
                   ? "Assistant · " +
                         m.str("model", fs::path(model_path).stem().string()) +
                         "\n"
                   : "You\n",
               'B');
        markdown(text, style, m.str("content"));
        append(text, style, "\n", 'A');
        if (m.get("attachments")) {
          cJSON *a;
          cJSON_ArrayForEach(a, m.get("attachments")) {
            Json item(cJSON_Duplicate(a, 1));
            append(text, style,
                   "[" + item.str("kind") + ": " + item.str("name") + "]\n",
                   'G');
          }
        }
        if (i % 2 && m.str("status", "complete") != "complete")
          append(text, style,
                 "[" + m.str("status") + "] " + m.str("error") + "\n", 'G');
        append(text, style, "\n", 'A');
        if (i % 2)
          positions.emplace_back(begin, static_cast<int>(text.size()));
        ++i;
      }
    }
    if (busy) {
      append(text, style, "You\n", 'B');
      markdown(text, style, partial_prompt);
      append(text, style,
             "\n\nAssistant · " + fs::path(model_path).stem().string() + "\n",
             'B');
      stream_prefix = static_cast<int>(text.size());
      rendered_partial.clear();
      rendered_style.clear();
    }
    if (text.empty())
      append(text, style,
             "Your chat is ready.\n\nSelect a local llama-server and GGUF "
             "model, then Load.\nWrite a message below and press Ctrl+Enter.\n",
             'G');
    set_text(style_buffer, style);
    set_text(text_buffer, text);
    transcript->insert_position(text_buffer.length());
    transcript->show_insert_position();
    if (busy)
      render_partial();
  }
  void select_exchange(bool scroll = true) {
    if (active.empty())
      return;
    int turn = exchange->value();
    auto messages = store.chats.at(active).get("messages");
    auto p = cJSON_GetArrayItem(messages, turn * 2 + 1);
    if (!p) {
      set_text(thinking_buffer, "");
      return;
    }
    Json reply(cJSON_Duplicate(p, 1));
    set_text(thinking_buffer, reply.str("reasoning_content"));
    if (scroll && turn >= 0 && static_cast<size_t>(turn) < positions.size()) {
      transcript->insert_position(positions[turn].first);
      transcript->show_insert_position();
    }
  }
  void refresh_attachments() {
    attachment_list->clear();
    cJSON *p;
    cJSON_ArrayForEach(p, attachments.p) {
      Json item(cJSON_Duplicate(p, 1));
      auto s = item.str("kind") + ": " + item.str("name") + "  (" +
               std::to_string(static_cast<int>(item.number("size") / 1024)) +
               " KiB)";
      attachment_list->add(s.c_str());
    }
    if (cJSON_GetArraySize(attachments.p))
      attachment_list->value(1);
  }
  void attach() {
    if (busy || working)
      return;
    auto path =
        chosen("Attach image, audio or video",
               "*.{png,jpg,jpeg,bmp,gif,wav,mp3,flac,mp4,mkv,webm,mov}");
    if (path.empty())
      return;
    if (cJSON_GetArraySize(attachments.p) >= 8)
      throw std::runtime_error("Use at most eight attachments per message.");
    job([this, path] { return store.import_attachment(path); },
        [this](const Json &item) {
          attachments.add(item);
          refresh_attachments();
          save_draft();
          layout();
        });
  }
  void remove_attachment() {
    int i = attachment_list->value() - 1;
    if (i < 0)
      return;
    cJSON_DeleteItemFromArray(attachments.p, i);
    refresh_attachments();
    save_draft();
    layout();
  }
  void retry(bool edit) {
    if (busy || working || active.empty())
      return;
    auto &chat = store.chats.at(active);
    int turn = exchange->value();
    auto original = cJSON_GetArrayItem(chat.get("messages"), turn * 2);
    if (!original)
      throw std::runtime_error("Choose an exchange first.");
    auto draft = Json(cJSON_Duplicate(original, 1)).str("content");
    auto branch = store.branch(chat, static_cast<size_t>(turn), draft,
                               edit ? "Edit" : "Retry");
    open_chat(branch.str("id"));
    if (edit)
      prompt->take_focus();
    else
      send();
  }
  void edit_workspace(bool create) {
    if (busy || working)
      return;
    Json ws = create ? Json() : store.workspaces.at(workspace);
    Fl_Double_Window dialog(520, 380,
                            create ? "New workspace" : "Workspace settings");
    dialog.color(Face);
    dialog.begin();
    Fl_Input name(100, 15, 400, 28, "Name");
    name.textsize(12);
    name.value(ws.str("name", "Workspace").c_str());
    Fl_Text_Buffer buffer;
    Fl_Text_Editor editor(15, 70, 490, 250, "Master prompt");
    editor.align(FL_ALIGN_TOP_LEFT);
    editor.buffer(&buffer);
    editor.textsize(13);
    set_text(buffer, ws.str("master_prompt", lcb_modules[0].instruction));
    Fl_Button cancel(330, 335, 80, 28, "Cancel"),
        save(420, 335, 80, 28, "Save");
    bool accepted = false;
    cancel.callback(+[](Fl_Widget *w, void *) { w->window()->hide(); });
    save.callback(
        +[](Fl_Widget *w, void *v) {
          *static_cast<bool *>(v) = true;
          w->window()->hide();
        },
        &accepted);
    dialog.end();
    dialog.set_modal();
    dialog.show();
    while (dialog.shown())
      Fl::wait();
    editor.buffer(nullptr);
    if (accepted) {
      auto result = store.workspace(name.value(), buffer_text(buffer),
                                    create ? "" : workspace);
      workspace = result.str("id");
      if (create)
        new_chat();
      else
        set_text(master_buffer, result.str("master_prompt"));
      refresh_tree();
    }
  }
  void update_generation() {
    for (int i = 0; i < GenCount; ++i)
      if (i == GenThinking)
        thinking_choice->value(g.thinking);
      else
        generation_fields[i]->value(generation_value(&g, i));
  }
  void edit_generation(int field, double value) {
    if (busy || working)
      return;
    Generation next = g;
    if (!generation_set(&next, field, value)) {
      update_generation();
      throw std::runtime_error("Invalid generation value.");
    }
    config.set(config.section(model_path), fields[field],
               std::to_string(value));
    config.save();
    g = next;
    if (field == GenContext && engine.owned())
      set_phase("Context changed. Unload and reload the model to apply it.");
  }
  void choose_engine() {
    if (busy || working || engine.owned())
      return;
    auto path = chosen("Select a Linux llama-server", "*", engine_path);
    if (path.empty())
      return;
    if (!fs::is_regular_file(path) || access(path.c_str(), X_OK))
      throw std::runtime_error("Select an executable Linux llama-server.");
    std::ifstream f(path, std::ios::binary);
    char magic[2]{};
    f.read(magic, 2);
    if (magic[0] == 'M' && magic[1] == 'Z')
      throw std::runtime_error(
          "Windows .exe runners cannot run natively on Linux.");
    engine_path = path;
    config.set("engine", "executable", path);
    config.save();
    note("Engine selected: " + path);
  }
  void choose_model() {
    if (busy || working || engine.owned())
      return;
    auto path = chosen("Select a GGUF chat model", "*.gguf", model_path);
    if (!path.empty())
      read_selected_model(path, false, false);
  }
  void read_selected_model(const std::string &path, bool auto_load,
                           bool initial) {
    if (working || busy)
      return;
    auto old = model_path;
    job(
        [path] {
          auto model = read_model(path);
          return std::make_pair(model, suggested_projector(model));
        },
        [this, path, old, auto_load, initial](const auto &result) {
          if (result.first.projector ||
              model_role(result.first) == "Draft companion")
            throw std::runtime_error(
                "Choose a chat model; this GGUF is a companion.");
          config.migrate(old, path);
          info = result.first;
          has_info = true;
          model_path = fs::absolute(path).string();
          projector = config.get(config.section(model_path), "projector",
                                 result.second);
          g = config.generation(model_path, &info);
          model_name->value(utf8(info.name).c_str());
          projector_text->value(projector.c_str());
          config.set("engine", "model", model_path);
          config.save();
          update_generation();
          show_model_details(info);
          if (!initial)
            set_phase("Model selected: " + utf8(info.name));
          if (auto_load)
            load_model();
          else if (initial && *folder->value() && !closing)
            scan();
        });
  }
  void set_projector(const std::string &path) {
    projector = path;
    projector_text->value(path.c_str());
    config.set(config.section(model_path), "projector", path);
    config.save();
    if (engine.owned())
      set_phase("Projector changed. Reload the model to apply it.");
  }
  void choose_projector() {
    if (busy || working)
      return;
    auto path =
        chosen("Select a matching media projector", "*.gguf", projector);
    if (path.empty())
      return;
    job([path] { return read_model(path); },
        [this, path](const ModelInfo &m) {
          if (!m.projector)
            throw std::runtime_error("This GGUF is not a media projector.");
          set_projector(path);
        });
  }
  void settings_dialog() {
    Fl_Double_Window dialog(660, 270, "Engine and model");
    dialog.color(Face);
    dialog.begin();
    Fl_Box engine_label(15, 12, 630, 35);
    engine_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    engine_label.copy_label(("Engine: " + engine_path).c_str());
    Fl_Box model_label(15, 51, 630, 35);
    model_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    model_label.copy_label(("Model: " + model_path).c_str());
    Fl_Input extra(15, 120, 630, 30, "Extra arguments (reload to apply)");
    extra.align(FL_ALIGN_TOP_LEFT);
    extra.textsize(12);
    extra.value(config.get("engine", "extra_arguments").c_str());
    Fl_Box hint(15, 157, 630, 32,
                "Example: --gpu-layers 99 --threads 8 --flash-attn on --mlock");
    hint.labelsize(12);
    hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Button cancel(465, 221, 80, 28, "Cancel"),
        save(555, 221, 90, 28, "Save");
    bool accepted = false;
    cancel.callback(+[](Fl_Widget *w, void *) { w->window()->hide(); });
    save.callback(
        +[](Fl_Widget *w, void *v) {
          *static_cast<bool *>(v) = true;
          w->window()->hide();
        },
        &accepted);
    dialog.end();
    dialog.set_modal();
    dialog.show();
    while (dialog.shown())
      Fl::wait();
    if (accepted) {
      split_arguments(extra.value());
      config.set("engine", "extra_arguments", extra.value());
      config.save();
    }
  }
  void setup() {
    int choice = fl_choice(
        "LCB-AI runs a local Linux llama-server with your existing "
        "GGUF.\nDownload and extract an engine, select Engine and Model, then "
        "Load.\nPrismML PTQ / PQ models require their compatible engine fork.",
        "Close", "llama.cpp builds", "PrismML guide");
    if (choice == 1)
      open_url("https://github.com/ggml-org/llama.cpp/releases/tag/b10566");
    else if (choice == 2)
      open_url(utf8(runtime_setup_url()));
  }
  void load_model() {
    if (busy || working || engine.owned())
      return;
    if (engine_path.empty() || model_path.empty())
      throw std::runtime_error(
          "Select a Linux llama-server and a GGUF model first.");
    if (has_info && info.runtime_requirement) {
      int result = fl_choice(
          "%s\n\nA successful load does not prove this engine has the required "
          "kernels.\nChoose a compatible engine or make a deliberate attempt.",
          "Cancel", "Choose engine", "Try selected engine",
          utf8(model_runtime_description(&info)).c_str());
      if (result == 1) {
        choose_engine();
        return;
      }
      if (result != 2)
        return;
    }
    auto exe = engine_path, model = model_path, proj = projector,
         extra = config.get("engine", "extra_arguments");
    auto p = port();
    int context = g.context_tokens;
    set_phase("Loading model...");
    ready = false;
    job(
        [this, exe, model, proj, extra, p, context] {
          engine.start(exe, model, p, context, store.root / "engine.log", proj,
                       extra);
          return true;
        },
        [this](bool) {
          set_phase("Engine started. Waiting for model readiness...");
        });
  }
  void unload() {
    if (busy || working || probing || !engine.owned())
      return;
    ready = false;
    job(
        [this] {
          engine.stop();
          return true;
        },
        [this](bool) {
          health = "Engine offline";
          set_phase("Model unloaded.");
        });
  }
  void engine_details() {
    std::string command;
    for (const auto &arg : engine.arguments())
      command += (command.empty() ? "" : " ") + arg;
    note("Engine command: " + command);
    show_output = true;
    output_tabs->value(log_page);
    layout();
  }
  void choose_folder() {
    const char *path = fl_dir_chooser("Model library folder", folder->value());
    if (path) {
      folder->value(path);
      scan();
    }
  }
  void scan() {
    if (busy || working)
      return;
    std::string path = folder->value();
    if (path.empty())
      throw std::runtime_error("Choose a model folder first.");
    bool recurse = recursive->value();
    config.set("library", "folder", path);
    config.set("library", "recursive", recurse ? "1" : "0");
    config.save();
    scan_cancel = false;
    set_phase("Scanning GGUF headers...");
    job([this, path,
         recurse] { return scan_models(path, recurse, scan_cancel); },
        [this](const auto &result) {
          models = result;
          refresh_models();
          set_phase(std::to_string(models.size()) + " models found.");
        });
  }
  void sort_models(int column) {
    std::wstring selected_path;
    int selected_index = model_list->value() - 1;
    if (selected_index >= 0 &&
        static_cast<size_t>(selected_index) < models.size())
      selected_path = models[selected_index].path;
    descending = sort_column == column ? !descending : false;
    sort_column = column;
    std::sort(models.begin(), models.end(),
              [this](const ModelInfo &a, const ModelInfo &b) {
                int result = model_compare(&a, &b, sort_column);
                return descending ? result > 0 : result < 0;
              });
    refresh_models();
    for (size_t i = 0; i < models.size(); ++i)
      if (selected_path == models[i].path) {
        model_list->value(static_cast<int>(i) + 1);
        inspect_model();
        break;
      }
  }
  void refresh_models() {
    // The workbench list is for choosing a chat model. Projector discovery
    // reads the filesystem independently, and the media chooser remains
    // available.
    models.erase(
        std::remove_if(models.begin(), models.end(),
                       [](const ModelInfo &m) { return m.projector != 0; }),
        models.end());
    model_list->entries.clear();
    for (const auto &m : models) {
      std::ostringstream size;
      size << std::fixed << std::setprecision(2)
           << double(m.bytes) / (1024 * 1024 * 1024);
      model_list->entries.push_back(
          {clean_label(utf8(m.name)), size.str(), model_role(m)});
    }
    model_list->order = sort_column;
    model_list->descending = descending;
    model_list->refresh(models.empty() ? 0 : 1);
    if (!models.empty())
      inspect_model();
    else if (has_info)
      show_model_details(info);
    else {
      details->entries = {{"Model", "No chat models found", ""},
                          {"Projector", "Use Choose below", ""}};
      details->refresh();
    }
  }
  void show_model_details(const ModelInfo &m) {
    details->entries = {{"Name", utf8(m.name), ""},
                        {"Architecture", utf8(m.architecture), ""},
                        {"Parameters", utf8(m.size), ""},
                        {"Quantization", utf8(model_quant(m.filetype)), ""},
                        {"Role", model_role(m), ""},
                        {"Runtime", utf8(model_runtime_description(&m)), ""},
                        {"Path", utf8(m.path), ""}};
    details->refresh();
  }
  void inspect_model() {
    int i = model_list->value() - 1;
    if (i >= 0 && static_cast<size_t>(i) < models.size())
      show_model_details(models[i]);
  }
  void use_model() {
    int i = model_list->value() - 1;
    if (i < 0 || static_cast<size_t>(i) >= models.size())
      return;
    auto &m = models[i];
    if (m.projector) {
      set_projector(utf8(m.path));
      return;
    }
    if (model_role(m) == "Draft companion")
      throw std::runtime_error(
          "This is a draft companion. Choose a chat model.");
    if (engine.owned())
      throw std::runtime_error("Unload the current model first.");
    read_selected_model(utf8(m.path), false, false);
  }
  void send() {
    if (busy || working || active.empty())
      return;
    auto text = buffer_text(prompt_buffer);
    if (text.find_first_not_of(" \t\r\n") == text.npos &&
        cJSON_GetArraySize(attachments.p))
      text = "Describe the attached media.";
    validate_text(text, LcbMaxPrompt, false);
    if (model_path.empty())
      throw std::runtime_error(
          "Select the GGUF model used by the local server first.");
    save_draft();
    auto id = active, model = model_path;
    auto messages = store.chats.at(id).object("messages");
    auto attached = attachments;
    auto instruction = store.workspaces.at(store.chats.at(id).str("workspace"))
                           .str("master_prompt");
    auto settings = g;
    transport = std::make_shared<Transport>(port());
    auto t = transport;
    busy = true;
    stream_prefix = -1;
    started = std::chrono::steady_clock::now();
    partial_prompt = text;
    partial_answer.clear();
    set_text(thinking_buffer, "");
    set_phase(
        "Preparing message: applying the template and counting tokens...");
    controls();
    render();
    launch([this, id, model, messages, attached, instruction, settings, text,
            t] {
      try {
        auto prepared = t->prepare(model, messages, instruction, text, settings,
                                   attached, store);
        post([this, prepared] {
          context_status = prepared.prompt < 0
                               ? "Media: engine checks full history / " +
                                     std::to_string(prepared.context)
                               : "Context: " + std::to_string(prepared.prompt) +
                                     " + " + std::to_string(prepared.response) +
                                     " reply + 32 / " +
                                     std::to_string(prepared.context) + "; " +
                                     std::to_string(prepared.omitted) +
                                     " older exchanges omitted";
          note(context_status);
          set_phase("Reading prompt: waiting for the first token...");
          controls();
        });
        auto reply = t->generate(prepared.wire, [this](const StreamReply &s) {
          std::string content = s.text, trace = s.reasoning;
          int total = s.progress_total, processed = s.progress_processed;
          post([this, content, trace, total, processed] {
            if (!content.empty())
              phase = "Writing answer";
            else if (!trace.empty())
              phase = "Thinking";
            else if (total)
              phase = "Reading prompt: " + std::to_string(processed) + " / " +
                      std::to_string(total);
            partial_answer = content;
            set_text(thinking_buffer, trace);
            phase_box->copy_label(phase.c_str());
            render();
          });
        });
        post([this, id, model, text, attached, reply] {
          busy = false;
          transport.reset();
          if (!reply.content.empty() || !reply.reasoning.empty()) {
            auto candidate = store.chats.at(id);
            auto list = candidate.object("messages");
            Json user, answer;
            user.set("role", std::string("user"));
            user.set("content", text);
            user.set("attachments", attached);
            answer.set("role", std::string("assistant"));
            answer.set("content",
                       reply.content.empty()
                           ? std::string("[No final answer was emitted.]")
                           : reply.content);
            answer.set("reasoning_content", reply.reasoning);
            answer.set("model", fs::path(model).stem().string());
            answer.set("status", reply.status);
            answer.set("error", reply.error);
            list.add(user);
            list.add(answer);
            candidate.set("messages", list);
            candidate.set("draft", std::string());
            candidate.set("draft_attachments", Json::array());
            if (cJSON_GetArraySize(list.p) == 2) {
              auto title = text;
              std::replace(title.begin(), title.end(), '\n', ' ');
              candidate.set("title", truncate(title, 80));
            }
            store.chats.insert_or_assign(id, candidate);
            try {
              store.save("chats", candidate);
            } catch (const std::exception &e) {
              error("Reply is still in memory; save failed: " +
                    std::string(e.what()));
            }
            show_chat();
            refresh_tree();
          } else
            render();
          auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
          set_phase(reply.status + " · " + std::to_string(reply.tokens) +
                    " generated tokens · " +
                    std::to_string(static_cast<int>(elapsed)) + "s" +
                    (reply.error.empty() ? "" : " · " + reply.error));
          controls();
        });
      } catch (const std::exception &e) {
        std::string detail = e.what();
        post([this, detail] {
          busy = false;
          transport.reset();
          set_phase(detail);
          render();
          controls();
        });
      }
    });
  }
  void stop() {
    if (transport) {
      transport->cancel();
      set_phase("Stopping; keeping any received text...");
    }
  }
  void request_close() {
    if (busy || working || probing) {
      closing = true;
      scan_cancel = true;
      stop();
      set_phase("Finishing the current operation before closing...");
      controls();
      return;
    }
    guard([&] { finish_close(); });
  }
  void finish_close() {
    try {
      save_draft();
      Json saved = Json::array();
      for (const auto &id : open_chats)
        saved.add(Json(cJSON_CreateString(id.c_str())));
      config.set("linux-session", "tabs", saved.dump());
      config.set("linux-session", "active", active);
      Json sizes = Json::array();
      for (int n : {left_width, w() - left_width - right_width, right_width})
        sizes.add(Json(cJSON_CreateNumber(n)));
      config.set("linux-layout", "sizes", sizes.dump());
      Json vertical = Json::array();
      vertical.add(Json(cJSON_CreateNumber(h() - output_height)));
      vertical.add(Json(cJSON_CreateNumber(output_height)));
      config.set("linux-layout", "vertical", vertical.dump());
      config.set("linux-layout", "left", show_left ? "1" : "0");
      config.set("linux-layout", "right", show_right ? "1" : "0");
      config.save();
      Fl::remove_timeout(tick_cb, this);
      hide();
    } catch (...) {
      closing = false;
      exit_requested = 0;
      controls();
      throw;
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  std::setlocale(LC_ALL, "");
  fs::path root;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--data-dir" && i + 1 < argc)
      root = argv[++i];
    else if (arg == "--help") {
      std::cout << "LCB-AI " LCB_VERSION
                   "\nusage: lcb-ai [--data-dir PATH]\nNative Linux workbench "
                   "(C++ / FLTK).\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return 2;
    }
  }
  int result = 0;
  try {
    theme();
    std::signal(SIGTERM, exit_signal);
    std::signal(SIGINT, exit_signal);
    Fl::lock();
    Window window(root);
    window.show();
    result = Fl::run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  return result;
}
