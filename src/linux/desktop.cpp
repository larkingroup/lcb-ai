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
#include <FL/Fl_Pixmap.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Editor.H>
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
#include <cwctype>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>
using namespace lcb;
namespace {
namespace icons {
#include "../../vendor/kde1-icons/filenew.xpm"
#include "../../vendor/kde1-icons/fileopen.xpm"
#include "../../vendor/kde1-icons/forward.xpm"
#include "../../vendor/kde1-icons/stop.xpm"
#include "../../vendor/kde1-icons/pencil.xpm"
#include "../../vendor/kde1-icons/send.xpm"
#include "../../vendor/kde1-icons/filemail.xpm"
#include "../../vendor/kde1-icons/page.xpm"
enum Kind { New, Open, Load, Stop, Settings, Send, Attach, Document };
Fl_Image *get(Kind kind, bool enabled = true) {
  static Fl_Pixmap images[] = {Fl_Pixmap(filenew), Fl_Pixmap(fileopen),
      Fl_Pixmap(forward), Fl_Pixmap(stop), Fl_Pixmap(pencil), Fl_Pixmap(send),
      Fl_Pixmap(filemail), Fl_Pixmap(page)};
  static std::unique_ptr<Fl_Image> disabled[8];
  if (enabled) return &images[kind];
  if (!disabled[kind]) {
    disabled[kind].reset(images[kind].copy());
    disabled[kind]->inactive();
  }
  return disabled[kind].get();
}
}
volatile std::sig_atomic_t exit_requested = 0;
void exit_signal(int) { exit_requested = 1; }
const Fl_Color Face = fl_rgb_color(222, 222, 218),
               Light = fl_rgb_color(250, 250, 248),
               Shadow = fl_rgb_color(142, 144, 140),
               Ink = fl_rgb_color(32, 34, 32),
               Muted = fl_rgb_color(94, 98, 94),
               ThinkingInk = fl_rgb_color(128, 133, 137),
               Blue = fl_rgb_color(192, 211, 224),
               BlueInk = fl_rgb_color(49, 76, 96),
               Title = fl_rgb_color(49, 91, 134),
               CodePaper = fl_rgb_color(236, 234, 224);
const Fl_Boxtype TranscriptFrame = FL_FREE_BOXTYPE;
const char *const EngineReleases =
    "https://github.com/ggml-org/llama.cpp/releases/tag/b10566";
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
  Fl::background(222, 222, 218);
  Fl::background2(255, 255, 255);
  Fl::foreground(32, 34, 32);
  Fl::set_color(FL_SELECTION_COLOR, 192, 211, 224);
  Fl::set_boxtype(FL_UP_BOX, raised, 2, 2, 4, 4);
  Fl::set_boxtype(FL_DOWN_BOX, sunken, 2, 2, 4, 4);
  Fl::set_boxtype(FL_THIN_UP_BOX, raised, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_DOWN_BOX, sunken, 1, 1, 2, 2);
  Fl::set_boxtype(TranscriptFrame, sunken, 12, 8, 24, 16);
  // FLTK's Xft and Pango backends use different face-name conventions.
  const bool xft_names = Fl::get_font(FL_HELVETICA_BOLD)[0] == 'B';
  Fl::set_font(FL_HELVETICA, "Helvetica");
  Fl::set_font(FL_HELVETICA_BOLD, xft_names ? "BHelvetica" : "Helvetica Bold");
  Fl::set_font(FL_HELVETICA_ITALIC, xft_names ? "IHelvetica" : "Helvetica Italic");
  Fl::set_font(FL_HELVETICA_BOLD_ITALIC,
               xft_names ? "PHelvetica" : "Helvetica Bold Italic");
  Fl::scrollbar_size(15);
  fl_message_font(FL_HELVETICA, 12);
}
class IconButton : public Fl_Button {
  icons::Kind icon_;
  bool toolbar_, hovered_ = false;
public:
  IconButton(const char *text, icons::Kind icon, bool toolbar)
      : Fl_Button(0, 0, 10, 10, text), icon_(icon), toolbar_(toolbar) {
    labelsize(12);
  }
  void draw() override {
    if (value()) sunken(x(), y(), w(), h(), Face);
    else if (!toolbar_ || (hovered_ && active_r())) raised(x(), y(), w(), h(), Face);
    else { fl_color(Face); fl_rectf(x(), y(), w(), h()); }
    int shift = value() ? 1 : 0;
    icons::get(icon_, active_r())->draw(x() + 3 + shift, y() + (h() - 22) / 2 + shift);
    fl_color(active_r() ? Ink : Shadow);
    fl_font(labelfont(), labelsize());
    fl_draw(label(), x() + 28 + shift, y() + shift, w() - 31, h(),
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    if (Fl::focus() == this) draw_focus(FL_FLAT_BOX, x() + 2, y() + 2, w() - 4, h() - 4);
  }
  int handle(int event) override {
    if (event == FL_ENTER || event == FL_LEAVE) {
      hovered_ = event == FL_ENTER;
      redraw();
    }
    return Fl_Button::handle(event);
  }
};
std::string buffer_text(Fl_Text_Buffer &b) {
  char *s = b.text();
  std::string value = s ? s : "";
  free(s);
  return value;
}
void set_text(Fl_Text_Buffer &b, const std::string &s) { b.text(s.c_str()); }
std::string clean_label(std::string s) {
  for (char &c : s)
    if (c == '\t' || c == '\n' || c == '\r')
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
        std::max(100, w() - 4 - Fl::scrollbar_size());
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
class ChatSidebar : public Fl_Hold_Browser {
public:
  enum Kind { Workspace, Chat, Empty, Section };
  struct Row {
    Kind kind;
    std::string id, title;
    bool expanded = false, indented = false;
  };
  std::vector<Row> entries;
  std::string active, workspace;
  std::function<void(Row, bool)> action;
  int hovered = 0;
  ChatSidebar() : Fl_Hold_Browser(0, 0, 10, 10) {
    box(FL_THIN_DOWN_BOX);
    color(FL_WHITE);
    selection_color(Title);
    has_scrollbar(VERTICAL);
    textsize(12);
  }
  void refresh() {
    int scroll = vposition();
    clear();
    hovered = 0;
    for (const auto &row : entries)
      add(row.title.c_str());
    vposition(scroll);
    redraw();
  }
  int item_height(void *) const override { return textsize() + 10; }
  int item_width(void *) const override { return 0; }
  std::pair<int, int> context_position() const {
    if (Fl::event() != FL_KEYDOWN && Fl::event() != FL_SHORTCUT)
      return {Fl::event_x(), Fl::event_y()};
    int row_y = y() + 2 + value() * item_height(nullptr) - vposition();
    return {x() + 30, std::clamp(row_y, y() + 2, y() + h() - 2)};
  }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    int index = lineno(item);
    if (index < 1 || static_cast<size_t>(index) > entries.size())
      return;
    const auto &row = entries[index - 1];
    bool selected = row.kind == Chat && row.id == active;
    bool group = row.kind == Workspace;
    fl_push_clip(X, Y, W, H);
    fl_color(selected ? Title : row.kind == Section ? Face : FL_WHITE);
    fl_rectf(X, Y, W, H);
    fl_color(selected ? FL_WHITE : row.kind == Empty || row.kind == Section ? Muted : Ink);
    fl_font(row.kind == Section || (group && row.id == workspace)
                ? FL_HELVETICA_BOLD : FL_HELVETICA, textsize());
    int inset = row.indented ? 40 : 9;
    if (group) {
      int cy = Y + H / 2;
      fl_color(Shadow);
      fl_rect(X + 7, cy - 4, 9, 9);
      fl_color(Ink);
      fl_line(X + 9, cy, X + 13, cy);
      if (!row.expanded)
        fl_line(X + 11, cy - 2, X + 11, cy + 2);
      fl_color(fl_rgb_color(225, 195, 116));
      fl_rectf(X + 21, cy - 3, 13, 10);
      fl_rectf(X + 21, cy - 6, 7, 4);
      fl_color(fl_rgb_color(142, 118, 65));
      fl_rect(X + 21, cy - 3, 13, 10);
      fl_color(Ink);
      inset = 40;
    }
    if (row.kind == Chat) {
      icons::get(icons::New)->draw(X + inset - 3, Y + (H - 22) / 2);
      inset += 21;
    }
    ClassicTable::cell_text(row.title, X + inset, Y, W - inset - 8, H);
    if (item_selected(item) && Fl::focus() == this) {
      fl_color(selected ? FL_WHITE : Shadow);
      fl_rect(X + 2, Y + 2, W - 4, H - 4);
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event == FL_MOVE || event == FL_ENTER) {
      auto item = find_item(Fl::event_y());
      int row = item ? lineno(item) : 0;
      if (hovered != row) {
        hovered = row;
        copy_tooltip(row > 0 ? entries[row - 1].title.c_str() : "");
      }
    } else if (event == FL_LEAVE) {
      hovered = 0;
    } else if (event == FL_PUSH &&
               !(scrollbar.visible() && Fl::event_inside(&scrollbar))) {
      auto item = find_item(Fl::event_y());
      int index = item ? lineno(item) : 0;
      if (index > 0 && action && active_r()) {
        auto row = entries[index - 1];
        if (row.kind != Chat && row.kind != Workspace)
          return 1;
        value(index);
        take_focus();
        action(row, Fl::event_button() == FL_RIGHT_MOUSE);
        return 1;
      }
    } else if (event == FL_KEYDOWN && value() > 0 && action) {
      auto row = entries[value() - 1];
      int key = Fl::event_key();
      if (key == FL_Enter || key == FL_KP_Enter ||
          (row.kind == Workspace && ((key == FL_Right && !row.expanded) ||
                                    (key == FL_Left && row.expanded)))) {
        action(row, false);
        return 1;
      }
      if (key == FL_Menu || (key == FL_F + 10 && Fl::event_state(FL_SHIFT))) {
        action(row, true);
        return 1;
      }
      int target = 0;
      if (key == FL_Left && row.kind == Chat && row.indented) {
        for (int index = value() - 1; index > 0; --index)
          if (entries[index - 1].kind == Workspace) {
            target = index;
            break;
          }
      } else if (key == FL_Right && row.kind == Workspace && row.expanded &&
                 value() < size() && entries[value()].kind == Chat) {
        target = value() + 1;
      }
      if (target) {
        value(target);
        if (!displayed(target)) middleline(target);
        redraw();
        return 1;
      }
    }
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Up || Fl::event_key() == FL_Down)) {
      int step = Fl::event_key() == FL_Up ? -1 : 1;
      for (int index = value() + step; index > 0 && index <= size(); index += step)
        if (entries[index - 1].kind == Chat || entries[index - 1].kind == Workspace) {
          value(index);
          if (!displayed(index)) middleline(index);
          redraw();
          return 1;
        }
      return 1;
    }
    return Fl_Hold_Browser::handle(event);
  }
};
class LoadingMeter : public Fl_Widget {
public:
  bool running = false;
  LoadingMeter() : Fl_Widget(0, 0, 10, 10) {
    tooltip("Loading model. Waiting for the engine to become ready.");
    hide();
  }
  ~LoadingMeter() override { Fl::remove_timeout(animate, this); }
  void run(bool value) {
    if (value == running)
      return;
    running = value;
    if (value) {
      show();
      Fl::add_timeout(0.125, animate, this);
    } else {
      Fl::remove_timeout(animate, this);
      hide();
    }
  }
  static void animate(void *data) {
    auto meter = static_cast<LoadingMeter *>(data);
    meter->redraw();
    if (meter->running)
      Fl::repeat_timeout(0.125, animate, data);
  }
  void draw() override {
    sunken(x(), y(), w(), h(), FL_WHITE);
    int cells = std::max(1, (w() - 6) / 8);
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 125;
    int step = ticks % cells;
    fl_color(fl_rgb_color(38, 116, 61));
    for (int i = 0; i < cells; ++i)
      if ((i - step + cells) % cells < 5)
        fl_rectf(x() + 3 + i * 8, y() + 3, 6, h() - 6);
  }
};
class PaneTitle : public Fl_Box {
public:
  PaneTitle(const char *text) : Fl_Box(0, 0, 10, 10, text) {}
  void draw() override {
    edge(x(), y(), w(), h(), color());
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(labelcolor());
    ClassicTable::cell_text(label() ? label() : "", x() + 6, y(), w() - 12,
                            h());
  }
};
// A wheel event belongs to the display under the pointer, even at its edges.
// FLTK otherwise offers an unhandled wheel event to unrelated sibling panes.
class ReadingDisplay : public Fl_Text_Display {
public:
  ReadingDisplay() : Fl_Text_Display(0, 0, 10, 10) {}
  int handle(int event) override {
    if (event == FL_MOUSEWHEEL) {
      if (!Fl::event_inside(this))
        return 0;
      Fl_Text_Display::handle(event);
      return 1;
    }
    return Fl_Text_Display::handle(event);
  }
};
class Transcript : public ReadingDisplay {
public:
  std::function<void()> selected;
  std::function<void(bool)> follow_changed;
  std::function<bool(int)> disclosure;
  bool following = true;
  Transcript() {
    box(TranscriptFrame);
    mVScrollBar->callback(+[](Fl_Widget *widget, void *data) {
      auto &display = *static_cast<Transcript *>(data);
      auto &bar = *static_cast<Fl_Scrollbar *>(widget);
      Fl_Text_Display::v_scrollbar_cb(&bar, &display);
      display.follow(bar.value() >= bar.maximum());
    }, this);
  }
  void resize(int X, int Y, int W, int H) override {
    bool changed = W != w();
    ReadingDisplay::resize(X, Y, W, H);
    if (changed)
      wrap_mode(WRAP_AT_PIXEL, std::max(80, std::min(720, W - 47)));
  }
  void follow(bool value) {
    following = value;
    if (!value)
      display_insert_position_hint = 0;
    if (follow_changed)
      follow_changed(value);
  }
  int view_row() const { return mTopLineNumHint; }
  int view_column() const { return mHorizOffsetHint; }
  void latest() {
    follow(true);
    insert_position(buffer()->length());
    show_insert_position();
  }
  int handle(int event) override {
    if (event == FL_MOUSEWHEEL && !Fl::event_inside(this))
      return 0;
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE &&
        Fl::event_inside(text_area.x, text_area.y, text_area.w, text_area.h) &&
        disclosure && disclosure(xy_to_position(Fl::event_x(), Fl::event_y())))
      return 1;
    if (event == FL_KEYBOARD && !(Fl::event_state() & (FL_ALT | FL_SHIFT))) {
      int key = Fl::event_key();
      if (key == FL_End) {
        latest();
        return 1;
      }
      if (key == FL_Home || key == FL_Page_Up || key == FL_Page_Down ||
          key == FL_Up || key == FL_Down) {
        int step = key == FL_Page_Up || key == FL_Page_Down
                       ? std::max(1, mNVisibleLines - 1) : 3;
        int row = key == FL_Home ? 1 : view_row() +
            ((key == FL_Page_Up || key == FL_Up) ? -step : step);
        scroll(row, mHorizOffset);
        follow(mTopLineNumHint >= mVScrollBar->maximum());
        return 1;
      }
    }
    if (event == FL_MOUSEWHEEL && Fl::event_dy() < 0)
      follow(false);
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE)
      follow(false);
    int result = ReadingDisplay::handle(event);
    if (event == FL_MOUSEWHEEL && Fl::event_dy() > 0)
      follow(!mVScrollBar->visible() || mVScrollBar->value() >= mVScrollBar->maximum());
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
  int offset_ = 0;
  static constexpr int arrow_width = 24;
  int tab_width(const std::string &title) const {
    fl_font(FL_HELVETICA, 12);
    return std::clamp(static_cast<int>(fl_width(title.c_str())) + 51, 95, 180);
  }
  int total_width() const {
    int total = 0;
    for (const auto &entry : entries) total += tab_width(entry.second) + 2;
    return total;
  }
  int viewport() const {
    return w() - 6 - (total_width() > w() - 6 ? arrow_width * 2 : 0);
  }
  bool fully_visible(int left, int width) const {
    return left >= x() + 3 && left + width <= x() + 3 + viewport();
  }
  void scroll_tabs(int distance) {
    offset_ = std::clamp(offset_ + distance, 0,
                         std::max(0, total_width() - viewport()));
    redraw();
  }
public:
  std::vector<std::pair<std::string, std::string>> entries;
  std::string active;
  std::function<void(const std::string &, bool)> select;
  ChatTabs() : Fl_Widget(0, 0, 10, 10) {
    tooltip("Switch chats with Ctrl+Page Up or Ctrl+Page Down");
  }
  void reveal_active() {
    int left = 0;
    for (const auto &entry : entries) {
      int width = tab_width(entry.second);
      if (entry.first == active) {
        if (left < offset_) offset_ = left;
        else if (left + width > offset_ + viewport())
          offset_ = left + width - viewport();
        break;
      }
      left += width + 2;
    }
    scroll_tabs(0);
  }
  void resize(int X, int Y, int W, int H) override {
    Fl_Widget::resize(X, Y, W, H);
    reveal_active();
  }
  void step(int direction) {
    if (entries.empty() || !select || !active_r()) return;
    auto found = std::find_if(entries.begin(), entries.end(),
                             [&](const auto &e) { return e.first == active; });
    int index = found == entries.end() ? 0 : static_cast<int>(found - entries.begin());
    index = (index + direction + static_cast<int>(entries.size())) % entries.size();
    auto id = entries[index].first;
    select(id, false);
  }
  void draw() override {
    edge(x(), y(), w(), h(), Face);
    int available = viewport();
    fl_push_clip(x() + 3, y() + 1, available, h() - 2);
    int left = x() + 3 - offset_;
    for (const auto &entry : entries) {
      int width = tab_width(entry.second);
      bool selected = entry.first == active;
      edge(left, y() + (selected ? 2 : 4), width, h() - (selected ? 3 : 5),
           selected ? Light : Face);
      fl_color(Ink);
      icons::get(icons::Document)->draw(left + 2, y() + 4);
      ClassicTable::cell_text(entry.second, left + 25, y() + 3, width - 48, h() - 4);
      if (selected) {
        fl_color(Light);
        fl_line(left + 1, y() + h() - 2, left + width - 2, y() + h() - 2);
        if (Fl::focus() == this) {
          fl_color(Shadow);
          fl_line_style(FL_DOT);
          fl_rect(left + 3, y() + 5, width - 26, h() - 10);
          fl_line_style(0);
        }
      }
      if (fully_visible(left, width)) {
        fl_color(Ink);
        fl_line(left + width - 15, y() + 10, left + width - 9, y() + 16);
        fl_line(left + width - 15, y() + 16, left + width - 9, y() + 10);
      }
      left += width + 2;
    }
    fl_pop_clip();
    if (total_width() > w() - 6) {
      for (int i = 0; i < 2; ++i) {
        int ax = x() + w() - 3 - (2 - i) * arrow_width;
        raised(ax, y() + 2, arrow_width, h() - 4, Face);
        bool enabled = i ? offset_ + available < total_width() : offset_ > 0;
        fl_color(enabled && active_r() ? Ink : Shadow);
        fl_draw(i ? ">" : "<", ax, y(), arrow_width, h(), FL_ALIGN_CENTER);
      }
    }
  }
  int handle(int event) override {
    if (event == FL_FOCUS || event == FL_UNFOCUS) { redraw(); return 1; }
    if (!active_r()) return 0;
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Left || Fl::event_key() == FL_Right)) {
      step(Fl::event_key() == FL_Left ? -1 : 1);
      return 1;
    }
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
      take_focus();
      if (total_width() > w() - 6 && Fl::event_x() >= x() + 3 + viewport()) {
        scroll_tabs(Fl::event_x() < x() + w() - 3 - arrow_width ? -150 : 150);
        return 1;
      }
      int left = x() + 3 - offset_;
      for (const auto &entry : entries) {
        int width = tab_width(entry.second);
        if (Fl::event_x() >= left && Fl::event_x() < left + width) {
          auto id = entry.first;
          if (select)
            select(id, fully_visible(left, width) && Fl::event_x() > left + width - 22);
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
    if (event == FL_ENTER || event == FL_LEAVE) {
      if (window())
        window()->cursor(event == FL_LEAVE ? FL_CURSOR_DEFAULT
                         : vertical_ ? FL_CURSOR_WE : FL_CURSOR_NS);
      return 1;
    }
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
       show_left = true, show_right = true, show_output = false,
       show_chats = true, show_recents = false;
  int left_width = 210, right_width = 286, output_height = 110;
  int stream_prefix = -1;
  std::string rendered_partial, rendered_style;
  std::string active, workspace, engine_path, model_path, projector,
      phase = "Ready",
      context_status,
      health = "Engine offline", partial_prompt, partial_answer, partial_reasoning;
  GenerationTimings timings{};
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
      *retry_button, *edit_button, *left_toggle,
      *chats_toggle, *latest_button;
  Fl_Input *model_name;
  Fl_Int_Input *port_input;
  Fl_Box *brand, *left_header, *center_header, *right_header, *phase_box,
      *message_header, *status, *media_status, *output_header, *speed_box,
      *search_label, *port_label, *runtime_model_label, *model_library_label;
  Fl_Box *toolbar_separators[3]{};
  ChatSidebar *sidebar;
  Fl_Input *chat_search;
  LoadingMeter *loading_meter;
  bool model_loading = false;
  std::chrono::steady_clock::time_point load_started;
  ChatTabs *tabs;
  Fl_Choice *exchange;
  Transcript *transcript;
  Composer *prompt;
  Fl_Text_Buffer text_buffer, style_buffer, prompt_buffer, activity_buffer,
      log_buffer, master_buffer;
  static constexpr int StyleCount = 15;
  Fl_Text_Display::Style_Table_Entry styles[StyleCount] = {
      {Ink, FL_HELVETICA, 13, 0, FL_WHITE},
      {FL_WHITE, FL_HELVETICA_BOLD, 13, Fl_Text_Display::ATTR_BGCOLOR_EXT, Title},
      {Ink, FL_HELVETICA_BOLD, 13, 0, FL_WHITE},
      {Ink, FL_HELVETICA_ITALIC, 13, 0, FL_WHITE},
      {Ink, FL_COURIER, 13, Fl_Text_Display::ATTR_BGCOLOR, CodePaper},
      {Title, FL_HELVETICA_BOLD, 14, 0, FL_WHITE},
      {Muted, FL_HELVETICA, 13, 0, FL_WHITE},
      {ThinkingInk, FL_HELVETICA, 13, 0, FL_WHITE},
      {ThinkingInk, FL_HELVETICA_BOLD, 13, 0, FL_WHITE},
      {ThinkingInk, FL_HELVETICA_ITALIC, 13, 0, FL_WHITE},
      {ThinkingInk, FL_COURIER, 12, Fl_Text_Display::ATTR_BGCOLOR, CodePaper},
      {ThinkingInk, FL_HELVETICA_BOLD, 13, 0, FL_WHITE},
      {Ink, FL_HELVETICA_BOLD, 13, Fl_Text_Display::ATTR_BGCOLOR_EXT, Face},
      {Ink, FL_COURIER, 13, Fl_Text_Display::ATTR_BGCOLOR_EXT, CodePaper},
      {ThinkingInk, FL_COURIER, 12, Fl_Text_Display::ATTR_BGCOLOR_EXT, CodePaper}};
  Fl_Hold_Browser *attachment_list;
  ClassicTable *model_list, *details;
  bool descending = false;
  int sort_column = 0;
  Fl_Tabs *right_tabs, *output_tabs;
  Fl_Group *models_page, *generation_page, *library_page, *activity_page,
      *log_page;
  Fl_Box *properties_label, *projector_label, *generation_help, *folder_label,
      *master_label;
  Fl_Text_Display *master, *activity, *live_log;
  Fl_Input *projector_text, *folder;
  Fl_Button *use_model_button, *projector_button, *projector_clear,
      *folder_button, *scan_button;
  Fl_Check_Button *recursive;
  Fl_Value_Input *generation_fields[GenCount]{};
  Fl_Box *generation_labels[GenCount]{};
  Fl_Choice *thinking_choice;
  Divider *left_divider, *right_divider, *output_divider;
  std::map<Fl_Widget *, std::function<void()>> callbacks;
  std::map<std::string, bool> workspace_expanded;
  std::set<std::string> cpu_warned;
  std::map<std::string, bool> search_workspace_expanded;
  std::map<std::pair<std::string, size_t>, bool> thinking_collapsed;
  struct Disclosure { int start, end; size_t turn; };
  std::vector<Disclosure> disclosures;
  std::vector<std::pair<int, int>> positions;

public:
  explicit Window(const fs::path &root)
      : Fl_Double_Window(1240, 820, "lcb-ai"), store(root), config(store.root) {
    xclass("lcb-ai");
    color(Face);
    size_range(800, 600);
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
          Json::parse(config.get("linux-layout", "sizes", "[210,720,286]"));
      if (cJSON_GetArraySize(sizes.p) == 3) {
        left_width =
            std::clamp(cJSON_GetArrayItem(sizes.p, 0)->valueint, 160, 400);
        right_width =
            std::clamp(cJSON_GetArrayItem(sizes.p, 2)->valueint, 240, 500);
      }
      auto vertical =
          Json::parse(config.get("linux-layout", "vertical", "[680,110]"));
      if (cJSON_GetArraySize(vertical.p) == 2)
        output_height =
            std::clamp(cJSON_GetArrayItem(vertical.p, 1)->valueint, 100, 300);
    } catch (const std::exception &) {
    }
    show_left = config.get("linux-layout", "left", "1") != "0";
    show_right = config.get("linux-layout", "right", "1") != "0";
    show_chats = config.get("linux-layout", "chats", "1") != "0";
    show_recents = config.get("linux-layout", "recents", "0") != "0";
    show_output = config.get("linux-layout", "output", "0") != "0";
    try {
      auto states = Json::parse(config.get("linux-layout", "workspaces", "{}"));
      for (const auto &ws : store.workspaces)
        if (cJSON_IsBool(states.get(ws.first.c_str())))
          workspace_expanded[ws.first] = cJSON_IsTrue(states.get(ws.first.c_str()));
    } catch (const std::exception &) {
    }
    if (active.empty())
      new_chat();
    else
      show_chat();
    refresh_sidebar();
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
  }
  void resize(int x, int y, int width, int height) override {
    Fl_Double_Window::resize(x, y, width, height);
    if (menu)
      layout();
  }
  int handle(int event) override {
    if ((event == FL_SHORTCUT || event == FL_KEYBOARD) &&
        Fl::event_state(FL_CTRL) &&
        (Fl::event_key() == FL_Page_Up || Fl::event_key() == FL_Page_Down)) {
      guard([&] { tabs->step(Fl::event_key() == FL_Page_Up ? -1 : 1); });
      return 1;
    }
    if (event == FL_MOUSEWHEEL) {
      for (auto page : {models_page, generation_page, library_page})
        if (page->visible_r() && Fl::event_inside(page) &&
            static_cast<Fl_Scroll *>(page)->scrollbar.visible()) {
          page->handle(event);
          return 1;
        }
      // Route to the pane under the pointer exactly once. Group's fallback
      // delivery to siblings is inappropriate for independent workbench panes.
      for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{transcript,
           prompt, sidebar, model_list, details, attachment_list, activity,
           live_log, master})
        if (widget->visible_r() && widget->active_r() && Fl::event_inside(widget)) {
          widget->handle(event);
          return 1;
        }
      for (auto field : generation_fields)
        if (field && field->visible_r() && field->active_r() && Fl::event_inside(field)) {
          field->handle(event);
          return 1;
        }
      return 1;
    }
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
  Fl_Button *icon_button(const char *text, icons::Kind icon,
                         std::function<void()> fn, bool toolbar = true) {
    return bind(new IconButton(text, icon, toolbar), std::move(fn));
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
    auto d = new ReadingDisplay();
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
  void menu_enabled(const char *path, bool enabled) {
    int index = menu->find_index(path);
    if (index >= 0)
      menu->mode(index, enabled ? menu->mode(index) & ~FL_MENU_INACTIVE
                                : menu->mode(index) | FL_MENU_INACTIVE);
  }
  void menu_checked(const char *path, bool checked) {
    int index = menu->find_index(path);
    if (index >= 0) {
      int flags = menu->mode(index) | FL_MENU_TOGGLE;
      menu->mode(index, checked ? flags | FL_MENU_VALUE : flags & ~FL_MENU_VALUE);
    }
  }
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
    add("&Edit/Find chat", FL_CTRL + 'f', [&] {
      show_left = true;
      layout();
      chat_search->take_focus();
      chat_search->insert_position(0, chat_search->size());
    });
    add("&View/Chats sidebar", FL_F + 9, [&] {
      show_left = !show_left;
      layout();
    });
    add("&View/Show recent chats", 0, [&] {
      show_recents = !show_recents;
      if (show_recents) {
        show_left = true;
        chat_search->value("");
        search_workspace_expanded.clear();
      }
      refresh_sidebar();
      layout();
      if (show_recents)
        for (size_t i = 0; i < sidebar->entries.size(); ++i)
          if (sidebar->entries[i].kind == ChatSidebar::Section) {
            if (i + 1 < sidebar->entries.size() &&
                sidebar->entries[i + 1].kind == ChatSidebar::Chat)
              sidebar->value(static_cast<int>(i) + 2);
            sidebar->topline(static_cast<int>(i) + 1);
            sidebar->take_focus();
            break;
          }
    });
    add("&View/Open chat tabs", 0, [&] {
      show_chats = !show_chats;
      layout();
    });
    add("&View/Properties and Models", FL_F + 10, [&] {
      show_right = !show_right;
      layout();
    });
    add("&View/Output", FL_F + 12, [&] {
      show_output = !show_output;
      layout();
    });
    add("&View/Previous chat", FL_CTRL + FL_Page_Up, [&] { tabs->step(-1); });
    add("&View/Next chat", FL_CTRL + FL_Page_Down, [&] { tabs->step(1); });
    add("&View/Reset layout", 0, [&] {
      show_left = show_right = true;
      show_output = false;
      show_chats = true;
      show_recents = false;
      left_width = 210;
      right_width = 286;
      output_height = 110;
      refresh_sidebar();
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
                 "C++ / FLTK 1.4\nShared C request, model and streaming core.\n\n"
                 "Toolbar icons: KDE 1, Mark Donohoe and contributors (LGPL 2).");
    });
    for (const char *path : {"&File/New workspace...", "&File/Delete chat...",
                             "&Edit/Copy", "&View/Output", "&View/Next chat",
                             "&Engine/Unload"}) {
      int index = menu->find_index(path);
      menu->mode(index, menu->mode(index) | FL_MENU_DIVIDER);
    }
    new_button = icon_button("New chat", icons::New, [&] { new_chat(); });
    new_button->tooltip("New chat (Ctrl+N)");
    load_button = icon_button("Load", icons::Load, [&] { load_model(); });
    unload_button = icon_button("Unload", icons::Stop, [&] { unload(); });
    engine_button = icon_button("Settings...", icons::Settings, [&] { settings_dialog(); });
    model_button = icon_button("Open model...", icons::Open, [&] { choose_model(); });
    for (auto &separator : toolbar_separators) {
      separator = new Fl_Box(FL_ENGRAVED_BOX, 0, 0, 2, 24, nullptr);
    }
    runtime_model_label = label("Model:");
    model_library_label = label("Local models", true);
    model_name = input();
    model_name->readonly(1);
    model_name->value(fs::path(model_path).filename().c_str());
    port_input = new Fl_Int_Input(0, 0, 10, 10);
    port_input->textsize(12);
    port_input->tooltip("Local engine port");
    port_label = label("Port:");
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
    left_header = label("Workspace", true);
    left_toggle = button("<", [&] {
      show_left = !show_left;
      layout();
    });
    search_label = label("Search:");
    chat_search = input();
    chat_search->tooltip("Search chat titles and workspace names");
    chat_search->when(FL_WHEN_CHANGED);
    bind(chat_search, [&] {
      search_workspace_expanded.clear();
      sidebar->vposition(0);
      refresh_sidebar();
    });
    sidebar = new ChatSidebar();
    sidebar->action = [&](ChatSidebar::Row row, bool menu) {
      guard([&] { sidebar_action(row, menu); });
    };
    center_header = label("Conversations", true);
    chats_toggle = button("-", [&] {
      show_chats = !show_chats;
      layout();
    });
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
    exchange->tooltip("Select a message to retry or edit");
    exchange->color(FL_WHITE);
    bind(exchange, [&] { select_exchange(); });
    retry_button = icon_button("Retry", icons::Load, [&] { retry(false); });
    edit_button = icon_button("Edit...", icons::Settings, [&] { retry(true); });
    phase_box = label(phase.c_str());
    phase_box->copy_label(phase.c_str());
    phase_box->box(FL_THIN_DOWN_BOX);
    phase_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    speed_box = label("");
    speed_box->box(FL_THIN_DOWN_BOX);
    speed_box->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    speed_box->labelcolor(Muted);
    transcript = new Transcript();
    transcript->buffer(&text_buffer);
    transcript->highlight_data(&style_buffer, styles, StyleCount, 'A', nullptr, nullptr);
    transcript->textsize(13);
    transcript->color(FL_WHITE);
    transcript->selection_color(Blue);
    latest_button = button("Latest", [&] { transcript->latest(); });
    latest_button->tooltip("Resume following the response (End)");
    transcript->follow_changed = [&](bool follow) {
      visible(latest_button, !follow);
    };
    latest_button->hide();
    transcript->disclosure = [&](int position) {
      for (const auto &item : disclosures)
        if (position >= item.start && position < item.end) {
          transcript->follow(false);
          auto &collapsed = thinking_collapsed[{active, item.turn}];
          collapsed = !collapsed;
          if (busy && item.turn == positions.size()) {
            rendered_partial.clear();
            rendered_style.clear();
            render_partial();
          } else {
            // Rebuild saved sections before the streaming tail if necessary.
            stream_prefix = -1;
            render(true);
          }
          return true;
        }
      return false;
    };
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
    message_header = label("Message · Ctrl+Enter to send");
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
    attach_button = icon_button("Attach...", icons::Attach, [&] { attach(); }, false);
    remove_button = button("Remove", [&] { remove_attachment(); });
    media_status = label("Text / image / audio / video");
    media_status->labelcolor(Muted);
    prompt = new Composer();
    prompt->send = [this] { guard([&] { send(); }); };
    prompt->buffer(&prompt_buffer);
    prompt->textfont(FL_HELVETICA);
    prompt->textsize(13);
    prompt->color(FL_WHITE);
    prompt->selection_color(Blue);
    prompt->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    send_button = icon_button("Send", icons::Send, [&] { send(); }, false);
    stop_button = icon_button("Stop", icons::Stop, [&] { stop(); }, false);
    right_header = label("Runtime", true);
    right_tabs = new Fl_Tabs(0, 0, 10, 10);
    right_tabs->labelsize(12);
    right_tabs->selection_color(Blue);
    right_tabs->begin();
    models_page = new Fl_Scroll(0, 0, 10, 10, "Models");
    static_cast<Fl_Scroll *>(models_page)->type(Fl_Scroll::VERTICAL);
    models_page->color(Face);
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
    generation_page = new Fl_Scroll(0, 0, 10, 10, "Generation");
    static_cast<Fl_Scroll *>(generation_page)->type(Fl_Scroll::VERTICAL);
    generation_page->color(Face);
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
    library_page = new Fl_Scroll(0, 0, 10, 10, "Library");
    static_cast<Fl_Scroll *>(library_page)->type(Fl_Scroll::VERTICAL);
    library_page->color(Face);
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
    // Reuse the existing widgets in the permanent runtime and property sections.
    for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{properties_label, details,
           generation_labels[GenContext], generation_fields[GenContext],
           generation_labels[GenTemperature], generation_fields[GenTemperature]})
      Fl_Group::add(widget);
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
    output_tabs->end();
    loading_meter = new LoadingMeter();
    status = label("Engine offline");
    status->box(FL_THIN_DOWN_BOX);
    status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    left_divider = new Divider(true);
    left_divider->moved = [&](int x) {
      left_width = std::clamp(x - 4, 160, std::max(160, w() - (show_right ? std::min(right_width, std::max(240, w() - 520)) : 0) - 350));
      layout();
    };
    right_divider = new Divider(true);
    right_divider->moved = [&](int x) {
      right_width = std::clamp(w() - x - 4, 240, std::max(240, w() - 520));
      layout();
    };
    output_divider = new Divider(false);
    output_divider->moved = [&](int y) {
      output_height = std::clamp(h() - y - 30, 100, std::max(100, h() - 540));
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
  void layout_status() {
    place(status, 5, h() - 24, w() - 446, 21);
    place(phase_box, w() - 437, h() - 24, 278, 21);
    place(speed_box, w() - 155, h() - 24, 150, 21);
    visible(speed_box, !model_loading);
    place(loading_meter, w() - 152, h() - 21, 144, 15);
  }
  void layout() {
    menu_checked("&View/Chats sidebar", show_left);
    menu_checked("&View/Show recent chats", show_recents);
    menu_checked("&View/Open chat tabs", show_chats);
    menu_checked("&View/Properties and Models", show_right);
    menu_checked("&View/Output", show_output);
    place(menu, 0, 0, w(), 22);
    const int top = 25, pane_top = 59;
    place(new_button, 5, top, 94, 28);
    place(model_button, 102, top, 122, 28);
    place(toolbar_separators[0], 229, top + 2, 2, 24);
    place(load_button, 238, top, 69, 28);
    place(unload_button, 311, top, 83, 28);
    place(toolbar_separators[1], 401, top + 2, 2, 24);
    place(engine_button, 410, top, 106, 28);
    place(toolbar_separators[2], 523, top + 2, 2, 24);
    place(brand, w() - 94, top, 86, 28);
    output_height = std::clamp(output_height, 100, std::max(100, h() - 540));
    int bottom = h() - 27 - (show_output ? output_height + 30 : 0);
    int right = show_right
                    ? std::clamp(right_width, 240, std::max(240, w() - 520)) : 0;
    int left = show_left
                   ? std::clamp(left_width, 160, std::max(160, w() - right - 350)) : 28;
    int cx = left + 12, cw = w() - cx - right - (right ? 12 : 5),
        rx = w() - right - 5;
    visible(left_header, show_left);
    visible(search_label, show_left);
    visible(chat_search, show_left);
    visible(sidebar, show_left);
    visible(left_divider, show_left);
    place(left_header, 5, pane_top, left - 26, 22);
    place(left_toggle, show_left ? left - 21 : 5, pane_top, 26, 22);
    left_toggle->copy_label(show_left ? "<" : ">");
    left_toggle->tooltip(show_left ? "Collapse Chats (F9)" : "Expand Chats (F9)");
    place(search_label, 8, 86, 43, 23);
    place(chat_search, 52, 86, left - 48, 23);
    place(sidebar, 7, 114, left - 4, bottom - 114);
    place(left_divider, left + 5, pane_top, 6, bottom - pane_top);
    place(center_header, cx, pane_top, cw - 26, 22);
    auto title = "Conversation - " + store.workspaces.at(workspace).str("name");
    if (!show_chats && !active.empty()) title += " - " + store.chats.at(active).str("title");
    center_header->copy_label(title.c_str());
    center_header->copy_tooltip(title.c_str());
    place(chats_toggle, cx + cw - 26, pane_top, 24, 22);
    chats_toggle->copy_label(show_chats ? "-" : "+");
    chats_toggle->tooltip(show_chats ? "Collapse open chat tabs" : "Expand open chat tabs");
    visible(tabs, show_chats);
    place(tabs, cx, 84, cw, 28);
    int chat_top = show_chats ? 116 : 86;
    place(exchange, cx + 2, chat_top, cw - 166, 25);
    place(retry_button, cx + cw - 159, chat_top, 76, 25);
    place(edit_button, cx + cw - 80, chat_top, 78, 25);
    bool has_attachments = cJSON_GetArraySize(attachments.p) > 0;
    int extra = has_attachments ? 44 : 0;
    int composer = bottom - 112 - extra;
    place(transcript, cx, chat_top + 29, cw, composer - chat_top - 29);
    place(message_header, cx + 4, composer + 2, cw - 90, 22);
    place(latest_button, cx + cw - 82, composer + 2, 80, 22);
    visible(attachment_list, has_attachments);
    place(attachment_list, cx, composer + 26, cw - 107, 40);
    visible(remove_button, has_attachments);
    place(remove_button, cx + cw - 101, composer + 30, 101, 26);
    media_status->hide();
    int ay = composer + 26 + extra;
    place(prompt, cx, ay, cw - 107, bottom - ay - 2);
    place(send_button, cx + cw - 101, ay, 101, 26);
    place(stop_button, cx + cw - 101, ay + 28, 101, 26);
    place(attach_button, cx + cw - 101, ay + 56, 101, 26);

    for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{right_header,
         right_tabs, right_divider, runtime_model_label, model_name, port_label,
         port_input, properties_label, details, model_library_label,
         generation_labels[GenContext], generation_fields[GenContext],
         generation_labels[GenTemperature], generation_fields[GenTemperature]})
      visible(widget, show_right);
    place(right_header, rx, pane_top, right, 22);
    place(right_divider, rx - 7, pane_top, 6, bottom - pane_top);
    int pw = right - 12, px = rx + 6;
    place(runtime_model_label, px, 87, 62, 23);
    place(model_name, px + 66, 87, pw - 66, 23);
    place(port_label, px, 114, 62, 23);
    place(port_input, px + 66, 114, pw - 66, 23);
    place(generation_labels[GenContext], px, 141, 110, 23);
    place(generation_fields[GenContext], px + 114, 141, pw - 114, 23);
    place(generation_labels[GenTemperature], px, 168, 110, 23);
    place(generation_fields[GenTemperature], px + 114, 168, pw - 114, 23);
    place(properties_label, rx, 199, right, 22);
    int property_height = std::clamp(bottom - 480, 70, 151);
    place(details, px, 224, pw, property_height);
    int library_top = 231 + property_height;
    place(model_library_label, rx, library_top, right, 22);
    place(right_tabs, rx, library_top + 25, right, bottom - library_top - 25);
    for (auto page : {models_page, generation_page, library_page})
      place(page, rx + 2, library_top + 51, right - 4, bottom - library_top - 51);
    int py = library_top + 55;
    pw -= Fl::scrollbar_size();
    int content_bottom = std::max(bottom, py + 170);
    int list_height = std::max(40, content_bottom - py - 117);
    place(model_list, px, py, pw, list_height);
    place(use_model_button, px, py + list_height + 3, pw, 24);
    place(projector_label, px, content_bottom - 86, pw, 20);
    place(projector_text, px, content_bottom - 64, pw, 24);
    place(projector_button, px, content_bottom - 35, pw / 2 - 2, 26);
    place(projector_clear, px + pw / 2 + 2, content_bottom - 35, pw / 2 - 2, 26);
    int row = 0;
    for (int i = 0; i < GenCount; ++i) {
      if (i == GenContext || i == GenTemperature) continue;
      place(generation_labels[i], px, py + row * 26, pw - 104, 23);
      place(i == GenThinking ? static_cast<Fl_Widget *>(thinking_choice) : generation_fields[i],
            px + pw - 100, py + row * 26, 100, 23);
      ++row;
    }
    place(generation_help, px, py + row * 26 + 6, pw, bottom - py - row * 26 - 8);
    visible(generation_help, bottom - py - row * 26 > 100);
    place(folder_label, px, py, pw, 20);
    place(folder, px, py + 22, pw, 23);
    place(folder_button, px, py + 49, pw / 2 - 2, 25);
    place(scan_button, px + pw / 2 + 2, py + 49, pw / 2 - 2, 25);
    place(recursive, px, py + 77, pw, 22);
    place(master_label, px, py + 103, pw, 22);
    place(master, px, py + 128, pw, std::max(60, bottom - py - 131));
    visible(output_header, show_output);
    visible(output_tabs, show_output);
    visible(output_divider, show_output);
    place(output_divider, 5, bottom + 1, w() - 10, 7);
    place(output_header, 5, bottom + 8, w() - 10, 22);
    place(output_tabs, 5, bottom + 32, w() - 10, output_height - 6);
    for (auto page : {activity_page, log_page})
      place(page, 7, bottom + 58, w() - 14, output_height - 34);
    for (auto display : {activity, live_log})
      place(display, 9, bottom + 60, w() - 18, output_height - 38);
    layout_status();
    if (transcript->following)
      transcript->latest();
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
          if (!engine.owned()) model_loading = false;
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
        if (online || exit) model_loading = false;
        else if (loading && !model_loading) {
          model_loading = true;
          load_started = std::chrono::steady_clock::now();
        } else if (!loading && !engine.owned()) model_loading = false;
        if (became_ready && !busy)
          set_phase("Ready");
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
    for (auto w : {new_button, engine_button, model_button,
                   scan_button, folder_button, use_model_button,
                   projector_button, projector_clear, attach_button,
                   remove_button, retry_button, edit_button}) {
      if (idle)
        w->activate();
      else
        w->deactivate();
    }
    if (idle) { sidebar->activate(); chat_search->activate(); }
    else { sidebar->deactivate(); chat_search->deactivate(); }
    if (idle) tabs->activate();
    else tabs->deactivate();
    loading_meter->run(model_loading);
    layout_status();
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
    bool has_exchange = exchange->value() >= 0 && exchange->size() > 1;
    if (!idle || !has_exchange) {
      retry_button->deactivate();
      edit_button->deactivate();
      exchange->deactivate();
    } else {
      exchange->activate();
    }
    menu_enabled("&File/&New chat", idle);
    menu_enabled("&File/New workspace...", idle);
    menu_enabled("&File/Close chat", idle && !active.empty());
    menu_enabled("&File/Delete chat...", idle && !active.empty());
    menu_enabled("&Edit/Workspace settings...", idle);
    menu_enabled("&Edit/Find chat", idle);
    menu_enabled("&View/Previous chat", idle && open_chats.size() > 1);
    menu_enabled("&View/Next chat", idle && open_chats.size() > 1);
    menu_enabled("&Engine/Load model", load_button->active());
    menu_enabled("&Engine/Unload", unload_button->active());
    menu_enabled("&Engine/Stop response", busy);
    menu_enabled("&Settings/Engine and model...", idle);
    menu_enabled("&Settings/Setup...", idle);
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
    std::string state = health;
    if (model_loading) {
      auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - load_started).count();
      state = "Loading model... " + std::to_string(seconds) + "s";
    }
    auto line = state + "  |  127.0.0.1:" + std::string(port_input->value());
    if (!context_status.empty()) line += "  |  " + context_status;
    status->copy_label(line.c_str());
    status->copy_tooltip(line.c_str());
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
    phase_box->copy_tooltip(s.c_str());
    note(s);
  }
  void update_speed() {
    std::ostringstream label, detail;
    label << std::fixed << std::setprecision(1);
    if (timings.tokens_per_second > 0)
      label << timings.tokens_per_second << " tok/s";
    speed_box->copy_label(label.str().c_str());
    detail << "Generation speed";
    if (timings.generated_tokens > 0)
      detail << "\n" << timings.generated_tokens << " generated tokens in "
             << std::fixed << std::setprecision(2) << timings.generated_ms / 1000 << "s";
    if (timings.prompt_per_second > 0)
      detail << "\nPrompt processing: " << std::fixed << std::setprecision(1)
             << timings.prompt_per_second << " tok/s";
    speed_box->copy_tooltip(detail.str().c_str());
  }
  void sidebar_action(const ChatSidebar::Row &row, bool menu) {
    if (busy || working)
      return;
    if (row.kind == ChatSidebar::Workspace) {
      if (menu) {
        Fl_Menu_Item items[3]{};
        items[0].text = "&New chat here";
        items[1].text = "Workspace &settings...";
        auto at = sidebar->context_position();
        auto selected = items->popup(at.first, at.second);
        if (!selected) return;
        auto previous = workspace;
        if (selected == &items[0]) {
          workspace = row.id;
          workspace_expanded[row.id] = true;
          try {
            new_chat();
          } catch (...) {
            workspace = previous;
            throw;
          }
        } else {
          edit_workspace(false, row.id);
        }
      } else {
        auto &expanded = *chat_search->value() ? search_workspace_expanded
                                               : workspace_expanded;
        expanded[row.id] = !row.expanded;
        refresh_sidebar();
      }
    } else if (row.kind == ChatSidebar::Chat) {
      if (menu) {
        Fl_Menu_Item items[4]{};
        items[0].text = "&Open";
        items[1].text = "&Close tab";
        if (std::find(open_chats.begin(), open_chats.end(), row.id) == open_chats.end())
          items[1].flags = FL_MENU_INACTIVE;
        items[2].text = "&Delete...";
        auto at = sidebar->context_position();
        auto selected = items->popup(at.first, at.second);
        if (selected == &items[0]) open_chat(row.id);
        else if (selected == &items[1]) close_chat(row.id);
        else if (selected == &items[2]) {
          open_chat(row.id);
          if (active == row.id) delete_chat();
        }
      } else open_chat(row.id);
    }
  }
  void refresh_sidebar() {
    ChatSidebar::Row previous{};
    bool had_selection = sidebar->value() > 0 &&
        static_cast<size_t>(sidebar->value()) <= sidebar->entries.size();
    if (had_selection) previous = sidebar->entries[sidebar->value() - 1];
    int scroll = sidebar->vposition();
    auto query = wide(chat_search->value());
    std::transform(query.begin(), query.end(), query.begin(), towlower);
    auto matches = [&](const std::string &text) {
      auto value = wide(text);
      std::transform(value.begin(), value.end(), value.begin(), towlower);
      return value.find(query) != std::wstring::npos;
    };
    std::vector<std::pair<std::string, fs::file_time_type>> recent;
    for (const auto &chat : store.chats) {
      std::error_code ec;
      auto time = fs::last_write_time(store.root / "chats" / (chat.first + ".json"), ec);
      recent.emplace_back(chat.first, ec ? fs::file_time_type::min() : time);
    }
    std::sort(recent.begin(), recent.end(), [](const auto &a, const auto &b) {
      return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    auto &rows = sidebar->entries;
    rows.clear();
    // Filter once, keeping chats in recency order within each workspace.
    std::map<std::string, std::vector<std::string>> children;
    for (const auto &entry : recent) {
      const auto &chat = store.chats.at(entry.first);
      if (matches(chat.str("title")) || matches(store.workspaces.at(chat.str("workspace")).str("name"))) {
        children[chat.str("workspace")].push_back(entry.first);
      }
    }
    std::vector<std::pair<std::string, std::string>> groups;
    for (const auto &ws : store.workspaces)
      if (query.empty() || children.count(ws.first) || matches(ws.second.str("name")))
        groups.emplace_back(ws.second.str("name"), ws.first);
    std::sort(groups.begin(), groups.end());
    // Search reveals matches without changing the saved tree state.
    auto &states = query.empty() ? workspace_expanded : search_workspace_expanded;
    for (const auto &ws : groups) {
      bool expanded = states.try_emplace(ws.second, true).first->second;
      rows.push_back({ChatSidebar::Workspace, ws.second, clean_label(ws.first), expanded});
      if (!expanded) continue;
      auto found = children.find(ws.second);
      if (found == children.end()) {
        rows.push_back({ChatSidebar::Empty, "", "No chats yet", false, true});
      } else {
        for (const auto &id : found->second)
          rows.push_back({ChatSidebar::Chat, id,
                          clean_label(store.chats.at(id).str("title")), false, true});
      }
    }
    if (rows.empty()) rows.push_back({ChatSidebar::Empty, "", "No matching chats"});
    if (show_recents && query.empty()) {
      rows.push_back({ChatSidebar::Section, "", "Recent chats"});
      for (const auto &entry : recent)
        rows.push_back({ChatSidebar::Chat, entry.first,
                        clean_label(store.chats.at(entry.first).str("title"))});
      if (recent.empty()) rows.push_back({ChatSidebar::Empty, "", "No chats yet"});
    }
    sidebar->active = active;
    sidebar->workspace = workspace;
    new_button->copy_tooltip(("New chat in " + store.workspaces.at(workspace).str("name") +
                             " (Ctrl+N)").c_str());
    sidebar->refresh();
    int selected = 0, fallback = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto &row = rows[i];
      if (row.kind != ChatSidebar::Chat && row.kind != ChatSidebar::Workspace) continue;
      int index = static_cast<int>(i) + 1;
      if (!fallback || (row.kind == ChatSidebar::Workspace && row.id == workspace))
        fallback = index;
      if (row.kind == ChatSidebar::Chat && row.id == active && row.indented) fallback = index;
      if (had_selection && row.kind == previous.kind && row.id == previous.id &&
          row.indented == previous.indented)
        selected = index;
    }
    sidebar->value(selected ? selected : fallback);
    sidebar->vposition(scroll);
  }
  void refresh_tabs() {
    tabs->entries.clear();
    for (const auto &id : open_chats)
      if (store.chats.count(id))
        tabs->entries.emplace_back(id, store.chats.at(id).str("title"));
    tabs->active = active;
    tabs->reveal_active();
    tabs->redraw();
  }
  void save_draft() {
    if (active.empty() || !store.chats.count(active))
      return;
    Json candidate = store.chats.at(active);
    candidate.set("draft", buffer_text(prompt_buffer));
    candidate.set("draft_attachments", attachments);
    const auto &saved = store.chats.at(active);
    auto saved_attachments = saved.get("draft_attachments")
                                 ? saved.object("draft_attachments").dump() : "[]";
    if (saved.str("draft") != candidate.str("draft") || saved_attachments != attachments.dump())
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
    if (!*chat_search->value())
      workspace_expanded[store.chats.at(id).str("workspace")] = true;
    show_chat();
    refresh_sidebar();
  }
  void show_chat(bool preserve_view = false) {
    if (active.empty())
      return;
    if (!preserve_view) {
      timings = {};
      update_speed();
      context_status.clear();
      controls();
    }
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
    render(preserve_view);
    refresh_tabs();
    layout();
    controls();
  }
  void new_chat() {
    if (busy || working)
      return;
    save_draft();
    chat_search->value("");
    search_workspace_expanded.clear();
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
        disclosures.clear();
        exchange->clear();
        refresh_attachments();
      } else
        show_chat();
    }
    refresh_tabs();
    refresh_sidebar();
    layout();
    controls();
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
    refresh_sidebar();
  }
  void append(std::string &text, std::string &style, const std::string &s,
              char code) {
    text += s;
    style.append(s.size(), code);
  }
  void speaker(std::string &text, std::string &style, bool assistant,
               const std::string &model = "") {
    auto label = assistant ? " Assistant" : " You";
    append(text, style, model.empty() ? label : label + std::string(" \xc2\xb7 ") + model,
           assistant ? 'B' : 'M');
    append(text, style, "\n", assistant ? 'B' : 'M');
  }
  void message_text(std::string &text, std::string &style, const std::string &s,
                    char code) {
    for (char c : s) {
      if (c == '\r') continue;
      // Wrapped lines cannot hang an indent, so only code boxes get one.
      if ((code == 'N' || code == 'O') && c != '\n' &&
          (text.empty() || text.back() == '\n'))
        append(text, style, "  ", code);
      text += c;
      style += code;
    }
  }
  void message_end(std::string &text, std::string &style, char code = 'A') {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
      text.pop_back();
      style.pop_back();
    }
    append(text, style, "\n\n", code);
  }
  void markdown(std::string &text, std::string &style, const std::string &s,
                bool thinking = false) {
    struct Output {
      Window *window;
      std::string *text, *style;
      bool thinking;
    } out{this, &text, &style, thinking};
    markdown_render(
        wide(s).c_str(),
        +[](void *v, const wchar_t *s, unsigned flags) {
          auto &o = *static_cast<Output *>(v);
          char code = o.thinking
                      ? (flags & MdBlock ? 'O' : flags & MdCode ? 'K' : flags & MdHeading ? 'L'
                         : flags & MdBold ? 'I' : flags & MdItalic ? 'J' : 'H')
                      : flags & MdBlock     ? 'N'
                      : flags & MdCode      ? 'E'
                      : flags & MdHeading ? 'F'
                      : flags & MdBold    ? 'C'
                      : flags & MdItalic  ? 'D'
                      : flags & MdQuote   ? 'G'
                                          : 'A';
          o.window->message_text(*o.text, *o.style, utf8(s), code);
        },
        &out);
  }
  void thinking_section(std::string &text, std::string &style,
                        const std::string &trace, size_t turn, int offset = 0) {
    if (trace.empty())
      return;
    bool collapsed = thinking_collapsed.try_emplace(std::make_pair(active, turn), true).first->second;
    int start = offset + static_cast<int>(text.size());
    append(text, style, collapsed ? "[+] Thinking\n" : "[-] Thinking\n", 'I');
    disclosures.push_back({start, offset + static_cast<int>(text.size()), turn});
    if (!collapsed) {
      markdown(text, style, trace, true);
      message_end(text, style, 'H');
    }
  }
  void render_partial() {
    std::string text, style;
    disclosures.erase(std::remove_if(disclosures.begin(), disclosures.end(),
        [&](const Disclosure &d) { return d.start >= stream_prefix; }),
        disclosures.end());
    thinking_section(text, style, partial_reasoning, positions.size(), stream_prefix);
    markdown(text, style, partial_answer);
    if (text.empty()) append(text, style, "...", 'G');
    size_t common = 0;
    while (common < text.size() && common < rendered_partial.size() &&
           text[common] == rendered_partial[common] &&
           style[common] == rendered_style[common])
      ++common;
    while (common && common < text.size() &&
           (static_cast<unsigned char>(text[common]) & 0xc0) == 0x80)
      --common;
    if (common != text.size() || common != rendered_partial.size()) {
      int row = transcript->view_row(), column = transcript->view_column();
      auto offset = stream_prefix + static_cast<int>(common);
      style_buffer.replace(offset, style_buffer.length(),
                           style.c_str() + common);
      text_buffer.replace(offset, text_buffer.length(), text.c_str() + common);
      rendered_partial = std::move(text);
      rendered_style = std::move(style);
      if (transcript->following)
        transcript->latest();
      else
        transcript->scroll(row, column);
    }
  }
  void render(bool preserve_view = false) {
    if (busy && stream_prefix >= 0) {
      render_partial();
      return;
    }
    stream_prefix = -1;
    int row = transcript->view_row(), column = transcript->view_column();
    int selection_start = 0, selection_end = 0;
    bool selected = preserve_view && text_buffer.selection_position(
                                         &selection_start, &selection_end);
    std::string text, style;
    positions.clear();
    disclosures.clear();
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
        speaker(text, style, i % 2 != 0, m.str("model"));
        if (i % 2)
          thinking_section(text, style, m.str("reasoning_content"), i / 2);
        if (i % 2) markdown(text, style, m.str("content"));
        else message_text(text, style, m.str("content"), 'A');
        append(text, style, "\n", 'A');
        if (m.get("attachments")) {
          cJSON *a;
          cJSON_ArrayForEach(a, m.get("attachments")) {
            Json item(cJSON_Duplicate(a, 1));
            message_text(text, style,
                   item.str("name") + "\n",
                   'G');
          }
        }
        if (i % 2) {
          auto state = m.str("status", "complete");
          auto error = m.str("error");
          if (state == "stopped") message_text(text, style, "Stopped\n", 'G');
          else if (state == "length") message_text(text, style, "Response limit reached\n", 'G');
          else if (state == "error" && error.empty()) message_text(text, style, "Interrupted\n", 'G');
          if (!error.empty()) message_text(text, style, error + "\n", 'G');
        }
        message_end(text, style);
        if (i % 2)
          positions.emplace_back(begin, static_cast<int>(text.size()));
        ++i;
      }
    }
    if (busy) {
      speaker(text, style, false);
      message_text(text, style, partial_prompt, 'A');
      message_end(text, style);
      speaker(text, style, true, fs::path(model_path).stem().string());
      stream_prefix = static_cast<int>(text.size());
      rendered_partial.clear();
      rendered_style.clear();
    }
    set_text(style_buffer, style);
    set_text(text_buffer, text);
    if (!preserve_view || transcript->following)
      transcript->latest();
    else {
      transcript->scroll(row, column);
      if (selected)
        text_buffer.select(std::min(selection_start, text_buffer.length()),
                           std::min(selection_end, text_buffer.length()));
    }
    if (busy)
      render_partial();
  }
  void select_exchange(bool scroll = true) {
    if (active.empty())
      return;
    int turn = exchange->value();
    if (scroll && turn >= 0 && static_cast<size_t>(turn) < positions.size()) {
      transcript->follow(false);
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
  void edit_workspace(bool create, const std::string &target = "") {
    if (busy || working)
      return;
    auto id = target.empty() ? workspace : target;
    Json ws = create ? Json() : store.workspaces.at(id);
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
    Fl_Return_Button save(320, 335, 90, 28, "&Save");
    Fl_Button cancel(420, 335, 80, 28, "&Cancel");
    save.labelsize(12);
    cancel.labelsize(12);
    cancel.shortcut(FL_Escape);
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
                                    create ? "" : id);
      if (create) {
        workspace = result.str("id");
        new_chat();
      } else if (id == workspace) {
        set_text(master_buffer, result.str("master_prompt"));
      }
      refresh_sidebar();
      layout();
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
    Fl_Return_Button save(465, 221, 90, 28, "&Save");
    Fl_Button cancel(565, 221, 80, 28, "&Cancel");
    save.labelsize(12);
    cancel.labelsize(12);
    cancel.shortcut(FL_Escape);
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
        "Load.\nWith a graphics card, use the ubuntu-vulkan-x64 build; the "
        "plain ubuntu-x64 build is CPU only.\nPrismML PTQ / PQ models require "
        "their compatible engine fork.",
        "Close", "llama.cpp builds", "PrismML guide");
    if (choice == 1)
      open_url(EngineReleases);
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
    model_loading = true;
    load_started = std::chrono::steady_clock::now();
    job(
        [this, exe, model, proj, extra, p, context] {
          std::vector<std::string> devices;
          bool cpu_only = list_devices(exe, devices) && devices.empty();
          engine.start(exe, model, p, context, store.root / "engine.log", proj,
                       extra);
          return cpu_only;
        },
        [this, exe](bool cpu_only) {
          set_phase("Engine started. Waiting for model readiness...");
          if (cpu_only && cpu_warned.insert(exe).second) {
            note("This llama-server build found no GPU; it runs on the CPU only.");
            if (fl_choice("This llama-server build found no GPU, so the model "
                          "runs on the CPU only and replies will be several "
                          "times slower.\n\nFor NVIDIA, AMD or Intel graphics, "
                          "download the ubuntu-vulkan-x64 build and choose its "
                          "llama-server with Engine.",
                          "Continue", "Get GPU build", nullptr) == 1)
              open_url(EngineReleases);
          }
        });
  }
  void unload() {
    if (busy || working || probing || !engine.owned())
      return;
    ready = false;
    model_loading = false;
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
    if (text.find_first_not_of(" \t\r\n") == text.npos) {
      if (!cJSON_GetArraySize(attachments.p))
        return;
      text = "Describe the attached media.";
    }
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
    partial_reasoning.clear();
    timings = {};
    update_speed();
    set_phase("Connecting...");
    controls();
    render();
    launch([this, id, model, messages, attached, instruction, settings, text,
            t] {
      try {
        auto prepared = t->prepare(model, messages, instruction, text, settings,
                                   attached, store);
        post([this, prepared] {
          context_status = prepared.prompt < 0
                               ? "Context: media / " +
                                     std::to_string(prepared.context)
                               : "Context: " + std::to_string(prepared.prompt) +
                                     " + " + std::to_string(prepared.response) +
                                     " reply / " +
                                     std::to_string(prepared.context);
          if (prepared.omitted)
            context_status += " · " + std::to_string(prepared.omitted) + " older excluded";
          note(context_status);
          set_phase("Waiting for reply...");
          controls();
        });
        auto reply = t->generate(prepared.wire, [this](const StreamReply &s) {
          std::string content = s.text, trace = s.reasoning;
          int total = s.progress_total, processed = s.progress_processed;
          auto performance = s.timings;
          post([this, content, trace, total, processed, performance] {
            if (!content.empty())
              phase = "Replying";
            else if (!trace.empty())
              phase = "Thinking";
            else if (total)
              phase = "Reading prompt: " + std::to_string(processed) + " / " +
                      std::to_string(total);
            partial_answer = content;
            partial_reasoning = trace;
            timings = performance;
            update_speed();
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
                       reply.content.empty() ? std::string(NoAnswer)
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
            show_chat(true);
            refresh_sidebar();
          } else
            render(true);
          auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
          note(reply.status + " · " + std::to_string(reply.tokens) +
               " tokens · " + std::to_string(static_cast<int>(elapsed)) + "s");
          set_phase(!reply.error.empty() ? reply.error
                    : reply.status == "stopped" ? "Stopped"
                    : reply.status == "length" ? "Response limit reached" : "Ready");
          timings = reply.timings;
          update_speed();
          controls();
        });
      } catch (const std::exception &e) {
        std::string detail = e.what();
        post([this, detail] {
          busy = false;
          transport.reset();
          set_phase(detail);
          render(true);
          controls();
        });
      }
    });
  }
  void stop() {
    if (transport) {
      transport->cancel();
      set_phase("Stopping...");
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
      config.set("linux-layout", "chats", show_chats ? "1" : "0");
      config.set("linux-layout", "recents", show_recents ? "1" : "0");
      config.set("linux-layout", "output", show_output ? "1" : "0");
      Json expanded;
      for (const auto &ws : workspace_expanded)
        cJSON_AddBoolToObject(expanded.p, ws.first.c_str(), ws.second);
      config.set("linux-layout", "workspaces", expanded.dump());
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
    // Text editors consume page keys before their parent receives them.
    // Dispatch document-switching shortcuts first, but leave modal input alone.
    Fl::event_dispatch(+[](int event, Fl_Window *window) {
      if (event == FL_KEYBOARD && !Fl::modal() && !Fl::grab() &&
          (Fl::event_state() & (FL_CTRL | FL_ALT | FL_SHIFT)) == FL_CTRL &&
          (Fl::event_key() == FL_Page_Up || Fl::event_key() == FL_Page_Down) &&
          dynamic_cast<Window *>(window))
        return window->handle(event);
      return Fl::handle_(event, window);
    });
    Window window(root);
    window.show();
    result = Fl::run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  return result;
}
