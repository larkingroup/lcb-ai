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
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_SVG_Image.H>
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
#include <fontconfig/fontconfig.h>
#define LCB_CLASSIC_GLYPH_DATA
#include "classic_glyphs.h"
#include "shortcuts.h"
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
#include <sys/stat.h>
#include <ctime>
#include <limits>
using namespace lcb;
namespace {
namespace icons {
// The Windows build's hand-drawn 16px glyphs, rendered with transparency.
enum Kind { Document = ClassicDocument, Folder = ClassicFolder, Run = ClassicRun,
            Stop = ClassicStop, Engine = ClassicEngine, Model = ClassicModel,
            Output = ClassicOutput, Settings = ClassicSettings };
Fl_Image *get(Kind kind, bool enabled = true) {
  static std::unique_ptr<Fl_RGB_Image> images[ClassicIconCount], disabled[ClassicIconCount];
  static unsigned char pixels[ClassicIconCount][16 * 16 * 4];
  if (!images[kind]) {
    auto *p = pixels[kind];
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x, p += 4) {
        char c = classic_glyphs[kind][y][x];
        unsigned long rgb = classic_glyph_rgb(c);
        p[0] = static_cast<unsigned char>(rgb >> 16);
        p[1] = static_cast<unsigned char>(rgb >> 8);
        p[2] = static_cast<unsigned char>(rgb);
        p[3] = c == '.' ? 0 : 255;
      }
    images[kind] = std::make_unique<Fl_RGB_Image>(pixels[kind], 16, 16, 4);
  }
  if (enabled) return images[kind].get();
  if (!disabled[kind]) {
    disabled[kind].reset(static_cast<Fl_RGB_Image *>(images[kind]->copy()));
    disabled[kind]->inactive();
  }
  return disabled[kind].get();
}
}
volatile std::sig_atomic_t exit_requested = 0;
void exit_signal(int) { exit_requested = 1; }
// The Windows build's palette (src/winmain.c), so both look like one app.
const Fl_Color Face = fl_rgb_color(236, 233, 216),
               Light = fl_rgb_color(250, 249, 241),
               Shadow = fl_rgb_color(153, 154, 145),
               Ink = fl_rgb_color(47, 55, 58),
               Muted = fl_rgb_color(103, 113, 115),
               ThinkingInk = fl_rgb_color(128, 133, 137),
               Blue = fl_rgb_color(192, 211, 224),
               BlueInk = fl_rgb_color(49, 76, 96),
               Title = fl_rgb_color(76, 112, 157),
               TitleLine = fl_rgb_color(145, 175, 205),
               Header = fl_rgb_color(218, 216, 201),
               CodePaper = fl_rgb_color(243, 241, 230),
               // Classic 3D edge colours, as Windows draws EDGE_RAISED.
               EdgeLight = fl_rgb_color(227, 227, 227),
               EdgeShadow = fl_rgb_color(166, 166, 166),
               EdgeDark = fl_rgb_color(106, 106, 106);
const Fl_Boxtype TranscriptFrame = FL_FREE_BOXTYPE;
const char *const EngineReleases =
    "https://github.com/ggml-org/llama.cpp/releases/tag/b10566";
void bevel(int x, int y, int w, int h, Fl_Color top_left, Fl_Color bottom_right) {
  fl_color(top_left);
  fl_xyline(x, y, x + w - 1);
  fl_yxline(x, y, y + h - 1);
  fl_color(bottom_right);
  fl_xyline(x, y + h - 1, x + w - 1);
  fl_yxline(x + w - 1, y, y + h - 1);
}
// Two-pixel classic edges: raised for buttons and panes, sunken for wells.
void raised(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, EdgeLight, EdgeDark);
  bevel(x + 1, y + 1, w - 2, h - 2, FL_WHITE, EdgeShadow);
}
void sunken(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, EdgeShadow, FL_WHITE);
  bevel(x + 1, y + 1, w - 2, h - 2, EdgeDark, EdgeLight);
}
void thin_raised(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, FL_WHITE, EdgeShadow);
}
void thin_sunken(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, EdgeShadow, FL_WHITE);
}
// Single-pixel strip edge used by headers and status cells.
void edge(int x, int y, int w, int h, Fl_Color color) {
  thin_raised(x, y, w, h, color);
}
void frame_raised(int x, int y, int w, int h, Fl_Color) {
  bevel(x, y, w, h, EdgeLight, EdgeDark);
  bevel(x + 1, y + 1, w - 2, h - 2, FL_WHITE, EdgeShadow);
}
void frame_sunken(int x, int y, int w, int h, Fl_Color) {
  bevel(x, y, w, h, EdgeShadow, FL_WHITE);
  bevel(x + 1, y + 1, w - 2, h - 2, EdgeDark, EdgeLight);
}
// A white field with a one-pixel border, as Windows draws edits and combos.
void field_box(int x, int y, int w, int h, Fl_Color color) {
  fl_color(color);
  fl_rectf(x, y, w, h);
  fl_color(fl_rgb_color(171, 173, 179));
  fl_rect(x, y, w, h);
}
const Fl_Boxtype FieldBox = static_cast<Fl_Boxtype>(FL_FREE_BOXTYPE + 1);
// Tahoma ships beside the executable; it is registered for this process only.
// Returns whether it is available, bundled or installed.
// The executable's folder; bundled fonts and assets sit beside it.
fs::path exe_dir() {
  char self[4096];
  ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n <= 0) return {};
  self[n] = 0;
  return fs::path(self).parent_path();
}
// Window icons from the logo: the simple mark for small sizes, the full one above.
void set_window_icons() {
  auto dir = exe_dir();
  static std::vector<std::unique_ptr<Fl_SVG_Image>> images;
  for (const auto &[file, size] : {std::pair<const char *, int>{"lcb-icon-small.svg", 16},
                                   {"lcb-icon-small.svg", 24}, {"lcb-icon.svg", 32},
                                   {"lcb-icon.svg", 48}, {"lcb-icon.svg", 64}}) {
    for (const auto &folder : {dir / "assets", dir.parent_path() / "share/lcb-ai"}) {
      std::error_code error;
      if (!fs::is_regular_file(folder / file, error)) continue;
      auto image = std::make_unique<Fl_SVG_Image>((folder / file).c_str());
      if (image->fail()) break;
      image->resize(size, size);
      images.push_back(std::move(image));
      break;
    }
  }
  std::vector<const Fl_RGB_Image *> list;
  for (const auto &image : images) list.push_back(image.get());
  if (!list.empty()) Fl_Window::default_icons(list.data(), static_cast<int>(list.size()));
}
bool load_interface_font() {
  // As Wine's own fontconfig rule: the embedded bitmap strikes have metrics
  // that disagree with the outlines, which spaces letters unevenly.
  FcConfigParseAndLoadFromMemory(FcConfigGetCurrent(), reinterpret_cast<const FcChar8 *>(
      "<?xml version=\"1.0\"?><!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">"
      "<fontconfig><match target=\"font\"><test name=\"family\">"
      "<string>Tahoma</string></test><edit name=\"embeddedbitmap\" mode=\"assign\">"
      "<bool>false</bool></edit></match></fontconfig>"), FcFalse);
  if (auto dir = exe_dir(); !dir.empty()) {
    for (const auto &folder : {dir / "fonts", dir.parent_path() / "share/lcb-ai/fonts"})
      for (const char *file : {"tahoma.ttf", "tahomabd.ttf"}) {
        std::error_code error;
        if (fs::is_regular_file(folder / file, error))
          FcConfigAppFontAddFile(nullptr,
              reinterpret_cast<const FcChar8 *>((folder / file).c_str()));
      }
  }
  FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>("Tahoma"));
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result;
  FcPattern *match = FcFontMatch(nullptr, pattern, &result);
  FcChar8 *family = nullptr;
  bool found = match && FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch &&
               std::strcmp(reinterpret_cast<const char *>(family), "Tahoma") == 0;
  if (match) FcPatternDestroy(match);
  FcPatternDestroy(pattern);
  return found;
}
void theme() {
  bool tahoma = load_interface_font();
  Fl::scheme("none");
  Fl::background(236, 233, 216);
  Fl::background2(255, 255, 255);
  Fl::foreground(47, 55, 58);
  Fl::set_color(FL_SELECTION_COLOR, 76, 112, 157);
  Fl::set_boxtype(FL_UP_BOX, raised, 2, 2, 4, 4);
  Fl::set_boxtype(FL_DOWN_BOX, sunken, 2, 2, 4, 4);
  Fl::set_boxtype(FL_UP_FRAME, frame_raised, 2, 2, 4, 4);
  Fl::set_boxtype(FL_DOWN_FRAME, frame_sunken, 2, 2, 4, 4);
  Fl::set_boxtype(FL_THIN_UP_BOX, thin_raised, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_DOWN_BOX, thin_sunken, 1, 1, 2, 2);
  Fl::set_boxtype(TranscriptFrame, field_box, 10, 6, 20, 12);
  Fl::set_boxtype(FieldBox, field_box, 1, 1, 2, 2);
  // FLTK's Xft and Pango backends use different face-name conventions.
  const bool xft_names = Fl::get_font(FL_HELVETICA_BOLD)[0] == 'B';
  const char *face = tahoma ? "Tahoma" : "Helvetica";
  std::string plain = face, bold = face, italic = face, both = face;
  if (xft_names) {
    bold = "B" + bold;
    italic = "I" + italic;
    both = "P" + both;
  } else {
    bold += " Bold";
    italic += " Italic";
    both += " Bold Italic";
  }
  // FLTK keeps the pointers, so the names live for the whole run.
  static std::string names[4];
  names[0] = plain; names[1] = bold; names[2] = italic; names[3] = both;
  Fl::set_font(FL_HELVETICA, names[0].c_str());
  Fl::set_font(FL_HELVETICA_BOLD, names[1].c_str());
  Fl::set_font(FL_HELVETICA_ITALIC, names[2].c_str());
  Fl::set_font(FL_HELVETICA_BOLD_ITALIC, names[3].c_str());
  Fl::scrollbar_size(16);
  fl_message_font(FL_HELVETICA, 12);
}
// Draws like the Windows build's owner-drawn buttons: always raised, light
// when hot, sunken blue when pressed, with underlined Alt-key letters.
class IconButton : public Fl_Button {
  icons::Kind icon_;
  bool with_icon_, hovered_ = false;
public:
  bool accent = false;
  IconButton(const char *text, icons::Kind icon, bool with_icon = true)
      : Fl_Button(0, 0, 10, 10, text), icon_(icon), with_icon_(with_icon) {
    labelsize(12);
    labelcolor(Ink);
  }
  void show_as(const char *text, icons::Kind icon) {
    if (icon_ == icon && label() && std::strcmp(label(), text) == 0) return;
    icon_ = icon;
    copy_label(text);
    redraw();
  }
  void draw() override {
    bool pressed = value(), enabled = active_r();
    Fl_Color fill = !enabled ? Face : pressed ? Blue : hovered_ ? Light : accent ? Blue : Face;
    if (pressed) sunken(x(), y(), w(), h(), fill);
    else raised(x(), y(), w(), h(), fill);
    int shift = pressed ? 1 : 0, left = x() + 4, width = w() - 8;
    if (with_icon_) {
      icons::get(icon_, enabled)->draw(x() + 5 + shift, y() + (h() - 16) / 2 + shift);
      left = x() + 23;
      width = w() - 27;
    }
    draw_label(left + shift, y() + shift, width, h(), FL_ALIGN_CENTER | FL_ALIGN_CLIP);
    if (Fl::focus() == this) {
      fl_color(Ink);
      fl_line_style(FL_DOT);
      fl_rect(x() + 4, y() + 4, w() - 8, h() - 8);
      fl_line_style(0);
    }
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
// Case-folded text for searching; ASCII stays on the fast path.
std::string fold(const std::string &s) {
  bool ascii = std::all_of(s.begin(), s.end(),
                           [](char c) { return !(static_cast<unsigned char>(c) & 0x80); });
  if (ascii) {
    std::string out = s;
    for (char &c : out)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return out;
  }
  auto w = wide(s);
  std::transform(w.begin(), w.end(), w.begin(), towlower);
  return utf8(w);
}
// Collapses whitespace and cuts at a word boundary within the byte budget.
std::string clip_words(const std::string &s, size_t limit) {
  std::string out;
  for (char c : s) {
    bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (space) {
      if (!out.empty() && out.back() != ' ') out += ' ';
    } else
      out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() <= limit) return out;
  auto cut = out.rfind(' ', limit);
  out = truncate(out, cut != out.npos && cut > limit / 2 ? cut : limit);
  while (!out.empty() && std::strchr(" ,;:-", out.back())) out.pop_back();
  return out + "…";
}
// An immediate title from the first message: drop greetings and requests
// phrased as filler, keep the first sentence.
std::string quick_title(const std::string &text) {
  auto title = clip_words(text, 400);
  static const char *const openers[] = {
      "hey there", "hey claude", "hey", "hi there", "hi", "hello", "yo",
      "ok so", "okay so", "so", "please", "can you please", "could you please",
      "can you", "could you", "would you", "i need you to", "i want you to",
      "help me", "quick question"};
  for (bool trimmed = true; trimmed;) {
    trimmed = false;
    auto lower = fold(title);
    for (const char *opener : openers) {
      size_t n = std::strlen(opener);
      if (lower.compare(0, n, opener) == 0 &&
          (lower.size() == n || std::strchr(" ,.!?:", lower[n]))) {
        size_t at = n;
        while (at < title.size() && std::strchr(" ,.!?:", title[at])) ++at;
        if (at < title.size()) {
          title.erase(0, at);
          trimmed = true;
        }
        break;
      }
    }
  }
  auto end = title.find_first_of(".?!\n");
  if (end != title.npos && end >= 12) title.resize(end + (title[end] == '?' ? 1 : 0));
  title = clip_words(title, 56);
  if (!title.empty() && title[0] >= 'a' && title[0] <= 'z')
    title[0] = static_cast<char>(title[0] - 32);
  return title.empty() ? "New chat" : title;
}
// Model titles arrive with quotes, labels or trailing punctuation.
std::string tidy_title(std::string s) {
  auto line = s.find_first_not_of(" \t\r\n");
  if (line == s.npos) return "";
  s.erase(0, line);
  s = s.substr(0, s.find('\n'));
  for (const char *prefix : {"Title:", "title:", "TITLE:"})
    if (s.rfind(prefix, 0) == 0) s.erase(0, std::strlen(prefix));
  auto strip = [](std::string &t) {
    while (!t.empty() && std::strchr(" \t\"'*#`.", t.front())) t.erase(0, 1);
    while (!t.empty() && std::strchr(" \t\"'*#`.", t.back())) t.pop_back();
  };
  strip(s);
  return clip_words(s, 60);
}
// Compact recency for a list row: time today, weekday this week, then date.
std::string when_label(std::time_t then, std::time_t now) {
  if (then <= 0) return "";
  std::tm a = *std::localtime(&then), b = *std::localtime(&now);
  char text[32];
  if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday)
    std::strftime(text, sizeof(text), "%H:%M", &a);
  else if (now - then < 6 * 86400 && now >= then)
    std::strftime(text, sizeof(text), "%a", &a);
  else if (a.tm_year == b.tm_year)
    std::strftime(text, sizeof(text), "%b %e", &a);
  else
    std::strftime(text, sizeof(text), "%b %Y", &a);
  std::string out = text;
  auto twice = out.find("  ");
  if (twice != out.npos) out.erase(twice, 1);
  return out;
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
// Help > Keyboard shortcuts: the shared table, drawn like the model list.
class ShortcutTable : public Fl_Table_Row {
public:
  ShortcutTable(int X, int Y, int W, int H) : Fl_Table_Row(X, Y, W, H) {
    end();
    box(FL_THIN_DOWN_FRAME);
    color(FL_WHITE);
    type(SELECT_SINGLE);
    rows(LcbShortcutCount);
    cols(3);
    col_header(1);
    col_header_height(23);
    row_header(0);
    row_height_all(21);
    // Every row fits, so no room is kept for a scrollbar.
    int available = W - 4;
    col_width(0, 80);
    col_width(1, available - 270);
    col_width(2, 190);
  }
  void draw_cell(TableContext context, int R, int C, int X, int Y, int W,
                 int H) override {
    static const char *headings[] = {"Group", "Action", "Keys"};
    if (context == CONTEXT_COL_HEADER) {
      edge(X, Y, W, H, Face);
      fl_font(FL_HELVETICA, 12);
      fl_color(Ink);
      ClassicTable::cell_text(headings[C], X + 5, Y, W - 10, H);
      return;
    }
    if (context != CONTEXT_CELL || R < 0 || R >= rows())
      return;
    const auto &shortcut = lcb_shortcuts[R];
    bool active = row_selected(R);
    // The group shows once, on the first row of each run.
    bool repeat = R && !std::strcmp(shortcut.group, lcb_shortcuts[R - 1].group);
    fl_push_clip(X, Y, W, H);
    fl_color(active ? Title : FL_WHITE);
    fl_rectf(X, Y, W, H);
    fl_color(fl_rgb_color(219, 218, 206));
    fl_line(X + W - 1, Y, X + W - 1, Y + H - 1);
    fl_line(X, Y + H - 1, X + W - 1, Y + H - 1);
    fl_color(active ? FL_WHITE : Ink);
    fl_font(FL_HELVETICA, 12);
    ClassicTable::cell_text(C == 0   ? (repeat ? "" : shortcut.group)
                            : C == 1 ? shortcut.action
                                     : shortcut.keys,
                            X + 5, Y, W - 10, H);
    fl_pop_clip();
  }
};
// Rows are stored by index in the browser text, so geometry and drawing look
// up the row directly instead of scanning the list.
class ChatSidebar : public Fl_Hold_Browser {
public:
  enum Kind { Chat, Empty, Section };
  enum Action { Open, Menu, Rename, Delete };
  struct Row {
    Kind kind;
    std::string id, title, when, snippet;
    bool pinned = false;
  };
  std::vector<Row> entries;
  std::string active;
  std::function<void(const Row &, Action)> action;
  int hovered = 0;
  ChatSidebar() : Fl_Hold_Browser(0, 0, 10, 10) {
    box(FieldBox);
    color(Light);
    selection_color(Title);
    has_scrollbar(VERTICAL);
    textsize(12);
  }
  const Row *row_of(void *item) const {
    if (!item) return nullptr;
    size_t index = std::strtoul(item_text(item), nullptr, 10);
    return index < entries.size() ? &entries[index] : nullptr;
  }
  void refresh() {
    int scroll = vposition();
    clear();
    hovered = 0;
    for (size_t i = 0; i < entries.size(); ++i)
      add(std::to_string(i).c_str());
    vposition(scroll);
    redraw();
  }
  bool selectable(int line) const {
    return line > 0 && line <= static_cast<int>(entries.size()) &&
           entries[line - 1].kind == Chat;
  }
  int line_of(const std::string &id) const {
    for (size_t i = 0; i < entries.size(); ++i)
      if (entries[i].kind == Chat && entries[i].id == id)
        return static_cast<int>(i) + 1;
    return 0;
  }
  // Screen rectangle of a line, for the inline rename field and menus.
  int line_y(int line) const {
    int top = y() + Fl::box_dy(box()) - vposition();
    for (int i = 1; i < line && i <= size(); ++i)
      top += item_height(find_line(i));
    return top;
  }
  int item_height(void *item) const override {
    auto row = row_of(item);
    if (!row) return 26;
    return row->snippet.empty() ? 26 : 40;
  }
  int item_width(void *) const override { return 0; }
  // Hovered chats show a "..." menu button at the right edge, as on Windows.
  bool over_dots() const {
    int right = x() + w() - Fl::box_dx(box()) -
                (scrollbar.visible() ? Fl::scrollbar_size() : 0);
    return Fl::event_x() >= right - 28;
  }
  std::pair<int, int> context_position() const {
    if (Fl::event() != FL_KEYDOWN && Fl::event() != FL_SHORTCUT)
      return {Fl::event_x(), Fl::event_y()};
    int row_y = line_y(value()) + 22;
    return {x() + 30, std::clamp(row_y, y() + 2, y() + h() - 2)};
  }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    auto row = row_of(item);
    if (!row) return;
    bool selected = row->kind == Chat && row->id == active;
    bool hover = row->kind == Chat && hovered == lineno(item);
    fl_push_clip(X, Y, W, H);
    fl_color(selected ? Title : hover ? Face : Light);
    fl_rectf(X, Y, W, H);
    if (selected) {
      fl_color(TitleLine);
      fl_xyline(X, Y, X + W - 1);
    }
    if (row->kind != Chat) {
      fl_font(row->kind == Section ? FL_HELVETICA_BOLD : FL_HELVETICA, 12);
      fl_color(Muted);
      ClassicTable::cell_text(row->title, X + 9, Y, W - 17, H);
      fl_pop_clip();
      return;
    }
    Fl_Color soft = selected ? fl_rgb_color(214, 226, 240) : Muted;
    int right = X + W - 8;
    if (hover) {
      fl_font(FL_HELVETICA, 12);
      fl_color(selected ? FL_WHITE : Muted);
      fl_draw("...", X + W - 28, Y, 26, 26, FL_ALIGN_CENTER);
      right = X + W - 30;
    } else if (!row->when.empty()) {
      fl_font(FL_HELVETICA, 11);
      fl_color(soft);
      int width = static_cast<int>(fl_width(row->when.c_str()));
      fl_draw(row->when.c_str(), right - width, Y, width, 26,
              FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
      right -= width + 8;
    }
    int left = X + 9;
    if (row->pinned) {
      fl_color(selected ? FL_WHITE : fl_rgb_color(195, 146, 58));
      int cx = left + 3, cy = Y + 13;
      fl_polygon(cx - 4, cy - 1, cx + 4, cy - 1, cx, cy + 4);
      fl_rectf(cx - 2, cy - 6, 5, 6);
      fl_line(cx, cy + 4, cx, cy + 7);
      left += 13;
    }
    fl_font(FL_HELVETICA, 12);
    fl_color(selected ? FL_WHITE : Ink);
    ClassicTable::cell_text(row->title, left, Y, right - left, 26);
    if (!row->snippet.empty()) {
      fl_font(FL_HELVETICA, 11);
      fl_color(soft);
      ClassicTable::cell_text(row->snippet, X + 9, Y + 21, W - 17, 16);
    }
    if (item_selected(item) && Fl::focus() == this) {
      fl_color(selected ? FL_WHITE : Ink);
      fl_line_style(FL_DOT);
      fl_rect(X + 2, Y + 2, W - 4, H - 4);
      fl_line_style(0);
    }
    fl_pop_clip();
  }
  void select_line(int line) {
    value(line);
    if (!displayed(line)) middleline(line);
    redraw();
  }
  // Moves the keyboard selection to the next chat row in the given direction.
  bool step(int direction) {
    for (int line = value() + direction; line > 0 && line <= size(); line += direction)
      if (selectable(line)) {
        select_line(line);
        return true;
      }
    return false;
  }
  int handle(int event) override {
    if (event == FL_MOVE || event == FL_ENTER) {
      auto item = find_item(Fl::event_y());
      int line = item ? lineno(item) : 0;
      if (!selectable(line)) line = 0;
      if (hovered != line) {
        hovered = line;
        copy_tooltip(line ? entries[line - 1].title.c_str() : nullptr);
        redraw();
      }
    } else if (event == FL_LEAVE) {
      if (hovered) { hovered = 0; redraw(); }
    } else if (event == FL_PUSH &&
               !(scrollbar.visible() && Fl::event_inside(&scrollbar))) {
      auto item = find_item(Fl::event_y());
      int line = item ? lineno(item) : 0;
      if (selectable(line) && action && active_r()) {
        value(line);
        take_focus();
        auto row = entries[line - 1];
        action(row, Fl::event_button() == FL_RIGHT_MOUSE || over_dots() ? Menu
                    : Fl::event_clicks() ? Rename : Open);
      }
      return 1;
    } else if (event == FL_KEYDOWN && action) {
      int key = Fl::event_key();
      if (key == FL_Up || key == FL_Down) {
        step(key == FL_Up ? -1 : 1);
        return 1;
      }
      if (key == FL_Home || key == FL_End) {
        value(key == FL_Home ? 0 : size() + 1);
        step(key == FL_Home ? 1 : -1);
        return 1;
      }
      if (!selectable(value())) return Fl_Hold_Browser::handle(event);
      auto row = entries[value() - 1];
      if (key == FL_Enter || key == FL_KP_Enter) action(row, Open);
      else if (key == FL_F + 2) action(row, Rename);
      else if (key == FL_Delete) action(row, Delete);
      else if (key == FL_Menu || (key == FL_F + 10 && Fl::event_state(FL_SHIFT)))
        action(row, Menu);
      else return Fl_Hold_Browser::handle(event);
      return 1;
    }
    return Fl_Hold_Browser::handle(event);
  }
};
// A search box that shows its purpose while empty and hands navigation keys
// to the list below it.
class SearchField : public Fl_Input {
public:
  std::function<bool(int)> key;
  SearchField() : Fl_Input(0, 0, 10, 10) {}
  void draw() override {
    Fl_Input::draw();
    if (!size() && Fl::focus() != this) {
      fl_font(FL_HELVETICA, 12);
      fl_color(Muted);
      fl_draw("Search chats  (Ctrl+K)", x() + 6, y(), w() - 12, h(),
              FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    }
  }
  int handle(int event) override {
    if (event == FL_KEYBOARD && key && key(Fl::event_key()))
      return 1;
    if (event == FL_FOCUS || event == FL_UNFOCUS) redraw();
    return Fl_Input::handle(event);
  }
};
// An in-place title editor: Enter or leaving the field saves, Escape cancels.
class RenameField : public Fl_Input {
public:
  std::function<void(bool)> done;
  RenameField() : Fl_Input(0, 0, 10, 10) {
    textsize(12);
    box(FieldBox);

    hide();
  }
  void finish(bool save) {
    if (!visible()) return;
    hide();
    if (done) done(save);
  }
  int handle(int event) override {
    if (event == FL_KEYBOARD) {
      int k = Fl::event_key();
      if (k == FL_Escape) { finish(false); return 1; }
      if (k == FL_Enter || k == FL_KP_Enter) { finish(true); return 1; }
    }
    if (event == FL_UNFOCUS) {
      int result = Fl_Input::handle(event);
      finish(true);
      return result;
    }
    return Fl_Input::handle(event);
  }
};
// A small lamp beside the model picker: grey offline, amber loading, green ready.
class StatusLamp : public Fl_Widget {
public:
  Fl_Color lamp = fl_rgb_color(150, 150, 146);
  StatusLamp() : Fl_Widget(0, 0, 10, 10) {}
  void draw() override {
    fl_color(Face);
    fl_rectf(x(), y(), w(), h());
    int d = 10, cx = x() + (w() - d) / 2, cy = y() + (h() - d) / 2;
    fl_color(lamp);
    fl_pie(cx, cy, d, d, 0, 360);
    fl_color(Shadow);
    fl_arc(cx, cy, d, d, 0, 360);
  }
  void set(Fl_Color color, const char *tip) {
    if (lamp != color) { lamp = color; redraw(); }
    copy_tooltip(tip);
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
  std::function<void()> rewrapped;
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
    if (changed) {
      wrap_mode(WRAP_AT_PIXEL, std::max(80, std::min(720, W - 47)));
      if (rewrapped)
        rewrapped();
    }
  }
  // Monospace characters per line, so tables can be fitted to the pane.
  size_t columns() const {
    fl_font(FL_COURIER, 13);
    double glyph = fl_width("0");
    return glyph > 0 ? static_cast<size_t>(std::max(80, std::min(720, w() - 47)) / glyph) : 0;
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
      if (selected) {
        fl_color(Title);
        fl_rectf(left, y() + 2, width, 2);
      }
      icons::get(icons::Document)->draw(left + 5, y() + (h() - 16) / 2 + 1);
      fl_font(selected ? FL_HELVETICA_BOLD : FL_HELVETICA, 12);
      fl_color(selected ? BlueInk : Ink);
      ClassicTable::cell_text(entry.second, left + 25, y() + 3, width - 48, h() - 4);
      if (selected) {
        fl_color(Light);
        fl_line(left + 1, y() + h() - 2, left + width - 2, y() + h() - 2);
        if (Fl::focus() == this) {
          fl_color(Ink);
          fl_line_style(FL_DOT);
          fl_rect(left + 3, y() + 6, width - 26, h() - 10);
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
  // The gap between panes is the splitter, plain as on Windows.
  void draw() override {
    fl_color(Face);
    fl_rectf(x(), y(), w(), h());
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
       show_chats = false, pending_send = false;
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
  std::shared_ptr<Transport> transport, title_transport;
  // The chat receiving the current reply; other chats stay browsable.
  std::string stream_chat, pending_chat;

  struct SearchText {
    std::string title;
    int messages = -1;
    std::string folded, original;
  };
  std::map<std::string, SearchText> search_cache;
  std::map<std::string, std::time_t> file_times;
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
      *retry_button, *edit_button, *left_toggle, *chats_new,
      *chats_toggle, *latest_button;
  Fl_Input *model_name;
  Fl_Int_Input *port_input;
  Fl_Box *brand, *left_header, *center_header, *right_header, *phase_box,
      *message_header, *media_status, *output_header, *speed_box,
      *search_label, *port_label, *runtime_model_label, *model_library_label;
  Fl_Box *toolbar_separators[3]{};
  Fl_Button *status;
  // Toolbar model picker; the Properties window holds everything else.
  Fl_Box *model_picker_label, *thinking_label, *engine_path_label;
  Fl_Choice *model_picker, *quick_thinking;
  Fl_Button *properties_button, *engine_choose_button, *engine_options_button;
  Fl_Double_Window *props;
  Fl_Group *engine_page;
  std::vector<std::string> picker_paths;
  std::string pending_model, gpu;
  ChatSidebar *sidebar;
  SearchField *chat_search;
  RenameField *rename_field;
  std::string renaming;
  Fl_Choice *workspace_picker;
  std::vector<std::string> workspace_ids;
  StatusLamp *lamp;
  LoadingMeter *loading_meter;
  bool model_loading = false;
  std::chrono::steady_clock::time_point load_started;
  ChatTabs *tabs;
  Fl_Choice *exchange;
  Transcript *transcript = nullptr;
  Composer *prompt = nullptr;
  Fl_Text_Buffer text_buffer, style_buffer, prompt_buffer, activity_buffer,
      log_buffer, master_buffer;
  static constexpr int StyleCount = 17;
  Fl_Text_Display::Style_Table_Entry styles[StyleCount] = {
      {Ink, FL_HELVETICA, 13, 0, FL_WHITE},
      {BlueInk, FL_HELVETICA, 12, 0, FL_WHITE},
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
      {Muted, FL_HELVETICA, 12, 0, FL_WHITE},
      {Ink, FL_COURIER, 13, Fl_Text_Display::ATTR_BGCOLOR_EXT, CodePaper},
      {ThinkingInk, FL_COURIER, 12, Fl_Text_Display::ATTR_BGCOLOR_EXT, CodePaper},
      {Ink, FL_COURIER_BOLD, 13, Fl_Text_Display::ATTR_BGCOLOR_EXT, CodePaper},
      {ThinkingInk, FL_HELVETICA, 11, 0, FL_WHITE}};
  Fl_Hold_Browser *attachment_list;
  ClassicTable *model_list, *details;
  bool descending = false;
  int sort_column = 0;
  Fl_Tabs *right_tabs, *output_tabs;
  Fl_Group *models_page, *generation_page, *library_page, *activity_page,
      *log_page;
  Fl_Box *properties_label, *projector_label, *generation_help, *folder_label,
      *master_label;
  Fl_Text_Display *master = nullptr, *activity = nullptr, *live_log = nullptr;
  Fl_Input *projector_text, *folder;
  Fl_Button *use_model_button, *projector_button, *projector_clear,
      *folder_button, *scan_button;
  Fl_Check_Button *recursive;
  Fl_Value_Input *generation_fields[GenCount]{};
  Fl_Box *generation_labels[GenCount]{};
  Fl_Choice *thinking_choice;
  Fl_Button *reset_generation;
  bool media_engine = false, reload_media = false;
  Divider *left_divider, *right_divider, *output_divider;
  std::map<Fl_Widget *, std::function<void()>> callbacks;
  std::set<std::string> cpu_warned;
  std::map<std::pair<std::string, size_t>, bool> thinking_collapsed;
  struct Disclosure { int start, end; size_t turn; };
  std::vector<Disclosure> disclosures;
  std::vector<std::pair<int, int>> positions;

public:
  explicit Window(const fs::path &root)
      : Fl_Double_Window(1240, 820, "lcb-ai"), store(root), config(store.root) {
    // The buffers are members and die before the widgets that show them, so a
    // failed start detaches them before rethrowing.
    try {
      start();
    } catch (...) {
      detach_buffers();
      throw;
    }
  }
  ~Window() override {
    scan_cancel = true;
    if (transport)
      transport->cancel();
    for (auto &worker : workers)
      if (worker.thread.joinable())
        worker.thread.join();
    Fl::remove_timeout(tick_cb, this);
    detach_buffers();
  }
  void resize(int x, int y, int width, int height) override {
    Fl_Double_Window::resize(x, y, width, height);
    if (menu)
      layout();
  }

private:
  void detach_buffers() {
    if (transcript) {
      transcript->buffer(nullptr);
      transcript->highlight_data(nullptr, nullptr, 0, 'A', nullptr, nullptr);
    }
    for (Fl_Text_Display *display :
         std::initializer_list<Fl_Text_Display *>{prompt, master, activity, live_log})
      if (display) display->buffer(nullptr);
  }
  void start() {
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
    // FLTK closes windows on Escape; here Escape only stops a reply.
    callback(
        +[](Fl_Widget *, void *v) {
          if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape)
            return;
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
    show_chats = config.get("linux-layout", "tabs", "0") != "0";
    show_output = config.get("linux-layout", "output", "0") != "0";
    if (active.empty())
      new_chat();
    else
      show_chat();
    refresh_workspaces();
    refresh_sidebar();
    update_generation();

    layout();
    controls();
    prompt->take_focus();
    note("FLTK workbench ready. Storage: " + store.root.string());

    if (store.skipped)
      note(std::to_string(store.skipped) +
           " invalid saved files were left untouched.");
    Fl::add_timeout(1, tick_cb, this);
    // Builds the search index once the window is up, so the first search
    // keystroke is as quick as the rest.
    Fl::add_timeout(0.3, +[](void *data) {
      auto &w = *static_cast<Window *>(data);
      for (const auto &chat : w.store.chats) w.search_text(chat.first);
    }, this);

    if (!model_path.empty())
      read_selected_model(model_path, false, true);
    else if (*folder->value())
      scan();
  }

public:
  int handle(int event) override {
    if ((event == FL_SHORTCUT || event == FL_KEYBOARD) &&
        Fl::event_state(FL_CTRL) &&

        (Fl::event_key() == FL_Page_Up || Fl::event_key() == FL_Page_Down)) {
      guard([&] { step_chat(Fl::event_key() == FL_Page_Up ? -1 : 1); });
      return 1;
    }
    if ((event == FL_SHORTCUT || event == FL_KEYBOARD) &&
        Fl::event_state(FL_CTRL) && Fl::event_key() == FL_Tab) {
      guard([&] { step_chat(Fl::event_state(FL_SHIFT) ? -1 : 1); });
      return 1;
    }
    if (event == FL_PUSH || event == FL_KEYBOARD || event == FL_RELEASE) {
      int result = handle_events(event);
      track_focus();
      return result;
    }
    return handle_events(event);
  }
  int handle_events(int event) {
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
        else if (key == FL_F + 10) {
          show_properties(0);
          return 1;
        }
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
    i->box(FieldBox);
    return i;
  }
  Fl_Text_Display *display(Fl_Text_Buffer &buffer) {
    auto d = new ReadingDisplay();
    d->buffer(&buffer);
    d->textfont(FL_COURIER);
    d->textsize(12);
    d->color(FL_WHITE);
    d->box(FieldBox);
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
    menu->color(FL_WHITE);
    auto add = [&](const char *name, int key, std::function<void()> fn) {
      menu_action(name, key, [this, fn] { guard(fn); });
    };
    add("&File/&New chat", FL_CTRL + 'n', [&] { new_chat(); });
    add("&File/New workspace...", 0, [&] { edit_workspace(true); });
    add("&File/Close chat", FL_CTRL + 'w', [&] { close_chat(active); });
    add("&File/Rename chat", FL_F + 2, [&] { begin_rename(active); });
    add("&File/Pin chat", 0, [&] { toggle_pin(active); });
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
    add("&Edit/Find chat", FL_CTRL + 'f', [&] { focus_search(); });
    add("&Edit/Jump to chat", FL_CTRL + 'k', [&] { focus_search(); });
    add("&View/Chats sidebar", FL_F + 9, [&] {
      show_left = !show_left;
      layout();
    });
    add("&View/Chat tabs", 0, [&] {
      show_chats = !show_chats;
      layout();
    });
    add("&View/Output", FL_F + 12, [&] {
      show_output = !show_output;
      layout();
    });
    add("&View/Previous chat", FL_CTRL + FL_Page_Up, [&] { step_chat(-1); });
    add("&View/Next chat", FL_CTRL + FL_Page_Down, [&] { step_chat(1); });
    add("&View/Reset layout", 0, [&] {
      show_left = show_right = true;
      show_output = false;
      show_chats = false;
      left_width = 210;
      right_width = 286;
      output_height = 110;
      refresh_sidebar();
      layout();
    });
    add("&Chat/Send", FL_CTRL + FL_Enter, [&] { send(); });
    add("&Chat/Stop response", FL_Escape, [&] { stop(); });
    add("&Chat/Retry", FL_CTRL + 'r', [&] { retry(false); });
    add("&Chat/Edit and resend...", FL_CTRL + 'e', [&] { retry(true); });
    add("&Chat/Attach...", 0, [&] { attach(); });
    add("&Model/Load", FL_F + 5, [&] { load_model(); });
    add("&Model/Unload", 0, [&] { unload(); });
    add("&Model/Open model file...", FL_CTRL + 'o', [&] { choose_model(); });
    add("&Model/Properties...", FL_ALT + FL_Enter, [&] { show_properties(0); });
    add("&Model/Engine log", 0, [&] { engine_details(); });
    add("&Tools/Engine options...", FL_CTRL + ',', [&] { settings_dialog(); });
    add("&Tools/Setup...", 0, [&] { setup(); });
    add("&Help/Keyboard shortcuts", FL_F + 1, [&] { show_shortcuts(); });
    add("&Help/About", 0, [&] {
      fl_message("LCB-AI " LCB_VERSION
                 "\nLarkin Computing Bureau\n\nNative Linux workbench: "
                 "C++ / FLTK 1.4\nShared C request, model and streaming core.\n\n"
                 "Interface font: Tahoma from the Wine project (LGPL 2.1).");
    });
    for (const char *path : {"&File/New workspace...", "&File/Delete chat...",
                             "&Edit/Copy", "&View/Output", "&View/Next chat",
                             "&Chat/Stop response", "&Model/Open model file...",
                             "&Help/Keyboard shortcuts"}) {
      int index = menu->find_index(path);
      menu->mode(index, menu->mode(index) | FL_MENU_DIVIDER);
    }
    new_button = icon_button("Ne&w chat", icons::Document, [&] { new_chat(); });
    new_button->tooltip("New chat (Ctrl+N)");
    load_button = icon_button("&Load", icons::Run, [&] {
      if (engine.owned()) unload();
      else load_model();
    });
    lamp = new StatusLamp();
    unload_button = icon_button("Unload", icons::Stop, [&] { unload(); });
    engine_button = icon_button("Settings...", icons::Settings, [&] { settings_dialog(); });
    model_button = icon_button("Open model...", icons::Folder, [&] { choose_model(); });
    model_picker_label = label("Model:");
    model_picker = new Fl_Choice(0, 0, 10, 10);
    model_picker->textsize(12);
    model_picker->color(FL_WHITE);
    model_picker->box(FieldBox);
    model_picker->tooltip("Choosing a model loads it");
    bind(model_picker, [&] { pick_model(); });
    properties_button = icon_button("&Properties", icons::Settings,
                                    [&] { show_properties(0); });
    properties_button->tooltip("Model, generation, library and engine (F10)");
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
    chats_new = button("+", [&] { new_chat(); });
    chats_new->tooltip("New chat (Ctrl+N)");
    for (auto b : {left_toggle, chats_new}) {
      b->labelsize(13);

      b->visible_focus(0);
    }
    workspace_picker = new Fl_Choice(0, 0, 10, 10);
    workspace_picker->textsize(12);
    workspace_picker->color(FL_WHITE);
    workspace_picker->box(FieldBox);
    workspace_picker->tooltip("Workspace: its chats and master prompt");
    bind(workspace_picker, [&] { pick_workspace(); });
    search_label = label("Search:");
    chat_search = new SearchField();
    chat_search->textsize(12);
    chat_search->color(FL_WHITE);
    chat_search->box(FieldBox);
    chat_search->tooltip("Search titles and messages in every workspace.\n"
                         "Enter opens the first result, Down moves to the list, "
                         "Escape clears.");
    chat_search->when(FL_WHEN_CHANGED);
    bind(chat_search, [&] {
      sidebar->vposition(0);
      refresh_sidebar();
      if (*chat_search->value()) sidebar->step(1);
    });
    chat_search->key = [&](int key) {
      if (key == FL_Escape) {
        if (!*chat_search->value()) {
          prompt->take_focus();
          return true;
        }
        chat_search->value("");
        refresh_sidebar();
        return true;
      }
      if (key == FL_Down || key == FL_Up) {
        if (!sidebar->selectable(sidebar->value())) sidebar->step(1);
        else sidebar->step(key == FL_Up ? -1 : 1);
        sidebar->take_focus();
        return true;
      }
      if (key == FL_Enter || key == FL_KP_Enter) {
        int line = sidebar->selectable(sidebar->value()) ? sidebar->value() : 0;
        if (!line) {
          sidebar->value(0);
          if (sidebar->step(1)) line = sidebar->value();
        }
        if (line) {
          auto id = sidebar->entries[line - 1].id;
          guard([&] {
            chat_search->value("");
            open_chat(id);
            prompt->take_focus();
          });
        }
        return true;
      }
      return false;
    };
    sidebar = new ChatSidebar();
    sidebar->action = [&](const ChatSidebar::Row &row, ChatSidebar::Action action) {
      auto copy = row;
      guard([&] { sidebar_action(copy, action); });
    };
    rename_field = new RenameField();
    rename_field->done = [&](bool save) {
      guard([&] { finish_rename(save); });
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
    exchange->box(FieldBox);
    bind(exchange, [&] { select_exchange(); });
    retry_button = icon_button("&Retry", icons::Run, [&] { retry(false); }, false);
    edit_button = icon_button("E&dit and resend...", icons::Settings,
                              [&] { retry(true); }, false);
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
    transcript->rewrapped = [&] {
      // Tables depend on the pane width; re-render once the resize settles.
      Fl::remove_timeout(rewrap_cb, this);
      Fl::add_timeout(0.15, rewrap_cb, this);
    };
    transcript->disclosure = [&](int position) {
      for (const auto &item : disclosures)
        if (position >= item.start && position < item.end) {
          transcript->follow(false);
          auto &collapsed = thinking_collapsed[{active, item.turn}];
          collapsed = !collapsed;
          if (streaming_here() && item.turn == positions.size()) {
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
    attachment_list->box(FieldBox);
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
    attach_button = icon_button("&Attach...", icons::Folder, [&] { attach(); }, false);
    remove_button = button("Remove", [&] { remove_attachment(); });
    media_status = label("Text / image / audio / video");
    media_status->labelcolor(Muted);
    prompt = new Composer();
    prompt->send = [this] { guard([&] { send(); }); };
    prompt->buffer(&prompt_buffer);
    prompt->textfont(FL_HELVETICA);
    prompt->textsize(13);
    prompt->color(FL_WHITE);
    prompt->box(FieldBox);
    prompt->selection_color(Blue);
    prompt->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    thinking_label = label("Thinking:");
    thinking_label->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    quick_thinking = new Fl_Choice(0, 0, 10, 10);
    quick_thinking->add("Auto|Off|On");
    quick_thinking->textsize(12);
    quick_thinking->color(FL_WHITE);
    quick_thinking->box(FieldBox);
    quick_thinking->tooltip("Auto follows the model's own default");
    bind(quick_thinking,
         [&] { edit_generation(GenThinking, quick_thinking->value()); });
    send_button = icon_button("&Send", icons::Run, [&] {
      if (streaming_here()) stop();
      else send();
    });
    static_cast<IconButton *>(send_button)->accent = true;
    send_button->tooltip("Send (Ctrl+Enter). While replying: Stop (Esc)");
    stop_button = icon_button("Stop", icons::Stop, [&] { stop(); }, false);
    // Model details and settings live in one classic property sheet.
    Fl_Group::current(nullptr); // A top-level window, not a child of this one.
    props = new Fl_Double_Window(560, 560, "Model Properties");
    props->color(Face);
    props->begin();
    right_header = label("Runtime", true);
    right_tabs = new Fl_Tabs(0, 0, 10, 10);
    right_tabs->labelsize(12);
    right_tabs->selection_color(Light);
    right_tabs->begin();
    models_page = new Fl_Scroll(0, 0, 10, 10, "Model");
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
    projector_label = label("Media projector (loaded for attachments)");
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
    const double minimum[] = {0, 0, 0.000001, 0, 0, 1, 0, 0, 0, -2},
                 maximum[] = {131072, 2, 1, 1048576, 2, 2, 4, 1000, 1, 2};
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
        if (i == GenResponse || i == GenContext || i == GenTopK)
          f->step(1);
        else
          f->precision(2);
        f->when(FL_WHEN_RELEASE);
        generation_fields[i] =
            bind(f, [this, i, f] { edit_generation(i, f->value()); });
      }
    }
    reset_generation = button("Model defaults", [&] { reset_defaults(); });
    generation_help = label(
        "Defaults come from the GGUF publisher, then the model family. Edits "
        "are saved per model.\n\nResponse 0: until the model stops.\nContext 0: "
        "llama.cpp fits it to free memory (reload to apply).\nThinking: Auto "
        "uses the model template.\n\nRepeat: 1 = off.\nDRY, Top K, Min P, "
        "Presence: 0 = off.");
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
    engine_page = new Fl_Group(0, 0, 10, 10, "Engine");
    engine_page->labelsize(12);
    engine_page->begin();
    engine_path_label = label("");
    engine_path_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP |
                             FL_ALIGN_WRAP);
    engine_choose_button = button("Choose engine...", [&] { choose_engine(); });
    engine_options_button = button("Launch options...", [&] { settings_dialog(); });
    engine_page->end();
    right_tabs->end();
    library_page->add(model_list);
    library_page->add(use_model_button);
    engine_page->add(port_label);
    engine_page->add(port_input);
    for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{
             runtime_model_label, model_name, model_library_label, master_label,
             master, right_header})
      props->add(widget);
    auto close = button("Close", [&] { props->hide(); });
    close->resize(472, 524, 80, 28);
    props->end();
    props->set_non_modal();
    begin();
    output_header = label("Output", true);
    output_tabs = new Fl_Tabs(0, 0, 10, 10);
    output_tabs->selection_color(Light);
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
    status = button("Engine offline", [&] {
      show_output = !show_output;
      layout();
    });
    status->box(FL_THIN_DOWN_BOX);
    status->down_box(FL_THIN_DOWN_BOX);
    status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    status->visible_focus(0);
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
    // Three sunken cells, as the Windows status bar.
    int y = h() - 22;
    place(status, 4, y, 300, 20);
    place(phase_box, 307, y, std::max(120, w() - 307 - 161), 20);
    place(speed_box, w() - 158, y, 154, 20);
    visible(speed_box, !model_loading);
    place(loading_meter, w() - 154, y + 3, 146, 14);
  }
  // Pane rectangles, kept for painting the chrome in draw().
  struct Pane {
    int x = 0, y = 0, w = 0, h = 0;
    bool shown = false;
  } panes[3];
  int active_pane = 1, composer_strip = 0;
  void layout() {
    menu_checked("&View/Chats sidebar", show_left);
    menu_checked("&View/Chat tabs", show_chats);
    menu_checked("&View/Output", show_output);
    place(menu, 0, 0, w(), 22);
    // Toolbar strip.
    const int top = 26, pane_top = 58;
    place(new_button, 6, top, 92, 26);
    place(toolbar_separators[0], 103, top + 1, 2, 24);
    place(model_picker_label, 110, top, 44, 26);
    place(lamp, 152, top, 16, 26);
    int picker = std::clamp(w() - 520, 220, 380);
    place(model_picker, 170, top + 1, picker, 24);
    int tx = 170 + picker + 6;
    place(load_button, tx, top, 84, 26);
    unload_button->hide();
    place(toolbar_separators[1], tx + 90, top + 1, 2, 24);
    place(properties_button, tx + 98, top, 104, 26);
    toolbar_separators[2]->hide();
    place(brand, w() - 94, top, 86, 26);
    for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{model_button,
             engine_button, center_header, chats_toggle, message_header,
             right_divider, media_status, left_header, search_label, output_header,
             stop_button})
      widget->hide();
    output_height = std::clamp(output_height, 100, std::max(100, h() - 540));
    int status_top = h() - 25;
    int output_top = status_top - output_height;
    int pane_bottom = show_output ? output_top - 5 : status_top;
    int left = show_left ? std::clamp(left_width, 160, std::max(160, w() - 350)) : 30;
    // Chats pane.
    auto &lp = panes[0];
    lp = {4, pane_top, left, pane_bottom - pane_top, true};
    int lx = lp.x + 5, lw = lp.w - 10, ly = lp.y + 26;
    visible(workspace_picker, show_left);
    visible(chat_search, show_left);
    visible(sidebar, show_left);
    visible(chats_new, show_left);
    visible(left_divider, show_left);
    if (!show_left) rename_field->finish(true);
    place(left_toggle, lp.x + lp.w - 26, lp.y + 2, 22, 19);
    place(chats_new, lp.x + lp.w - 49, lp.y + 2, 22, 19);
    left_toggle->copy_label(show_left ? "<" : ">");
    left_toggle->tooltip(show_left ? "Collapse Chats (F9)" : "Expand Chats (F9)");
    place(workspace_picker, lx, ly, lw, 24);
    place(chat_search, lx, ly + 29, lw, 24);
    place(sidebar, lx, ly + 58, lw, lp.y + lp.h - 6 - (ly + 58));
    if (rename_field->visible()) place_rename();
    place(left_divider, lp.x + lp.w, pane_top, 5, pane_bottom - pane_top);
    // Conversations pane.
    auto &cp = panes[1];
    cp = {lp.x + lp.w + 5, pane_top, w() - (lp.x + lp.w + 5) - 4, pane_bottom - pane_top, true};
    int cx = cp.x + 4, cw = cp.w - 8, cy = cp.y + 24;
    visible(tabs, show_chats);
    place(tabs, cx, cy, cw, 26);
    if (show_chats) cy += 28;
    // Exchange row: choose a message to retry or edit, as on Windows.
    exchange->show();
    int buttons = 96 + 4 + 132;
    place(exchange, cx + 1, cy + 2, cw - buttons - 6, 23);
    place(retry_button, cx + cw - buttons, cy + 1, 96, 25);
    place(edit_button, cx + cw - 132, cy + 1, 132, 25);
    cy += 30;
    bool has_attachments = cJSON_GetArraySize(attachments.p) > 0;
    int extra = has_attachments ? 46 : 0;
    int bottom = cp.y + cp.h - 5;
    int prompt_top = bottom - 70;
    composer_strip = prompt_top - extra - 23;
    place(transcript, cx, cy, cw, composer_strip - cy - 3);
    place(latest_button, cx + cw - 100, composer_strip - 33, 80, 22);
    place(thinking_label, cx + cw - 146, composer_strip + 1, 62, 20);
    place(quick_thinking, cx + cw - 82, composer_strip + 1, 80, 20);
    visible(attachment_list, has_attachments);
    place(attachment_list, cx + 1, composer_strip + 23, cw - 86, 42);
    visible(remove_button, has_attachments);
    place(remove_button, cx + cw - 80, composer_strip + 23, 80, 25);
    place(prompt, cx + 1, prompt_top, cw - 86, bottom - prompt_top);
    place(send_button, cx + cw - 80, prompt_top, 80, 32);
    place(attach_button, cx + cw - 80, prompt_top + 36, 80, 26);
    // Output pane.
    auto &op = panes[2];
    op = {4, output_top, w() - 8, status_top - output_top - 3, show_output};
    visible(output_tabs, show_output);
    visible(output_divider, show_output);
    place(output_divider, 4, output_top - 5, w() - 8, 5);
    place(output_tabs, op.x + 4, op.y + 23, op.w - 8, op.h - 27);
    for (auto page : {activity_page, log_page})
      place(page, op.x + 4, op.y + 47, op.w - 8, op.h - 51);
    for (auto display : {activity, live_log})
      place(display, op.x + 5, op.y + 48, op.w - 10, op.h - 53);
    layout_status();
    if (transcript->following)
      transcript->latest();
    redraw();
  }
  // Which pane holds keyboard focus; its title strip is drawn active.
  int focused_pane() const {
    Fl_Widget *f = Fl::focus();
    if (!f || f->window() != this) return active_pane;
    int fx = f->x() + f->w() / 2, fy = f->y() + f->h() / 2;
    for (int i = 0; i < 3; ++i) {
      const auto &p = panes[i];
      if (p.shown && fx >= p.x && fx < p.x + p.w && fy >= p.y && fy < p.y + p.h) return i;
    }
    return active_pane;
  }
  void track_focus() {
    int pane = focused_pane();
    if (pane == active_pane) return;
    for (int i : {active_pane, pane})
      damage(FL_DAMAGE_ALL, panes[i].x, panes[i].y, panes[i].w, 22);
    active_pane = pane;
  }
  void draw_pane(int index, icons::Kind icon, const std::string &title) {
    const auto &p = panes[index];
    if (!p.shown) return;
    bool selected = index == active_pane;
    raised(p.x, p.y, p.w, p.h, Face);
    fl_color(selected ? Title : Header);
    fl_rectf(p.x + 2, p.y + 2, p.w - 4, 18);
    fl_color(selected ? TitleLine : Light);
    fl_xyline(p.x + 2, p.y + 2, p.x + p.w - 3);
    fl_color(Shadow);
    fl_xyline(p.x + 2, p.y + 20, p.x + p.w - 3);
    icons::get(icon)->draw(p.x + 5, p.y + 3);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(selected ? FL_WHITE : Ink);
    int room = p.w - 30 - (index == 0 ? 50 : 0);
    ClassicTable::cell_text(title, p.x + 25, p.y + 2, room, 18);
  }
  void draw() override {
    if (damage() & ~FL_DAMAGE_CHILD) {
      fl_color(Face);
      fl_rectf(0, 0, w(), h());
      fl_color(FL_WHITE);
      fl_rectf(0, 0, w(), 22);
      raised(2, 23, w() - 4, 32, Face);
      draw_pane(0, icons::Folder, show_left ? "Chats" : "");
      std::string title = "Conversations";
      if (!show_chats && !active.empty() && store.chats.count(active))
        title += " \xc2\xb7 " + clean_label(store.chats.at(active).str("title"));
      draw_pane(1, icons::Document, title);
      draw_pane(2, icons::Output, "Output");
      // The composer's caption strip.
      const auto &cp = panes[1];
      fl_color(Shadow);
      fl_xyline(cp.x + 4, composer_strip, cp.x + cp.w - 5);
      fl_font(FL_HELVETICA, 12);
      fl_color(Ink);
      fl_draw("Message \xc2\xb7 Ctrl+Enter to send", cp.x + 9, composer_strip + 1,
              cp.w - 170, 21, FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    }
    draw_children();
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
  static void rewrap_cb(void *data) {
    auto &w = *static_cast<Window *>(data);
    w.guard([&] {
      if (w.streaming_here()) {
        w.rendered_partial.clear();
        w.rendered_style.clear();
      }
      w.render(true);
    });
  }
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
    // Poll quickly while a model loads so Send unlocks the moment it is ready.
    if (w.shown())
      Fl::repeat_timeout(w.model_loading || (w.engine.owned() && !w.ready) ? 0.2 : 1,
                         tick_cb, data);
  }
  void tick() {
    if (exit_requested && !closing) {
      request_close();
      if (!shown())
        return;
    }
    reap_workers();
    if (dirty && !streaming_here() &&
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
        if (exit) pending_send = false;
        if (became_ready && pending_send) {
          pending_send = false;
          if (active == pending_chat)
            guard([&] { send(); });
        }
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
  bool streaming_here() const { return busy && !stream_chat.empty() && stream_chat == active; }
  void controls() {
    bool idle = !busy && !working && !closing;
    // Browsing chats stays available while a reply streams into one of them.
    bool browse = !working && !closing;
    bool here = streaming_here();
    for (auto w : {engine_button, model_button,
                   scan_button, folder_button, use_model_button,
                   projector_button, projector_clear,
                   remove_button, retry_button, edit_button}) {
      if (idle)
        w->activate();
      else
        w->deactivate();
    }
    for (Fl_Widget *w : std::initializer_list<Fl_Widget *>{new_button, sidebar,
             chat_search, tabs, workspace_picker})
      if (browse) w->activate();
      else w->deactivate();
    if (browse && !here) attach_button->activate();
    else attach_button->deactivate();
    loading_meter->run(model_loading);
    layout_status();
    auto send_icon = static_cast<IconButton *>(send_button);
    if (here) {
      send_icon->show_as("St&op", icons::Stop);
      send_button->activate();
    } else {
      send_icon->show_as(pending_send ? "Queued" : "&Send", icons::Run);
      if (idle && !pending_send && !model_path.empty() && !active.empty() &&
          (ready || engine.owned() || !engine_path.empty()))
        send_button->activate();
      else
        send_button->deactivate();
    }
    if (busy)
      stop_button->activate();
    else
      stop_button->deactivate();
    auto load_icon = static_cast<IconButton *>(load_button);
    if (engine.owned()) {
      load_icon->show_as(model_loading || !ready ? "Ca&ncel" : "&Unload", icons::Stop);
      load_button->tooltip("Unload the model and stop the engine");
    } else {
      load_icon->show_as("&Load", icons::Run);

      load_button->tooltip("Load the selected model (F5)");
    }
    if (idle) load_button->activate();
    else load_button->deactivate();
    if (idle && engine.owned())
      unload_button->activate();
    else
      unload_button->deactivate();
    if (ready)
      lamp->set(fl_rgb_color(46, 160, 67), "Model ready");
    else if (model_loading || engine.owned())
      lamp->set(fl_rgb_color(222, 160, 32), "Model loading");
    else
      lamp->set(fl_rgb_color(150, 150, 146), "No model loaded. Send loads it.");
    bool has_exchange = exchange->value() >= 0 && exchange->size() > 1;
    if (!idle || !has_exchange) {
      retry_button->deactivate();
      edit_button->deactivate();
      exchange->deactivate();
    } else {
      exchange->activate();
    }
    menu_enabled("&File/&New chat", browse);
    menu_enabled("&File/New workspace...", idle);
    menu_enabled("&File/Close chat", browse && show_chats && !active.empty());
    menu_enabled("&File/Rename chat", browse && !active.empty());
    menu_enabled("&File/Pin chat", browse && !active.empty());
    menu_enabled("&File/Delete chat...", idle && !active.empty());
    menu_enabled("&Edit/Workspace settings...", idle);
    menu_enabled("&Edit/Find chat", browse);
    menu_enabled("&Edit/Jump to chat", browse);
    menu_enabled("&View/Previous chat", browse);
    menu_enabled("&View/Next chat", browse);
    menu_enabled("&Model/Load", idle && !engine.owned());
    menu_enabled("&Model/Unload", idle && engine.owned());

    menu_enabled("&Model/Open model file...", idle);
    menu_enabled("&Chat/Send", send_button->active() && !here);
    menu_enabled("&Chat/Stop response", busy);
    menu_enabled("&Chat/Retry", retry_button->active());
    menu_enabled("&Chat/Edit and resend...", edit_button->active());
    menu_enabled("&Chat/Attach...", idle);
    menu_enabled("&Tools/Engine options...", idle);
    menu_enabled("&Tools/Setup...", idle);
    if (idle) model_picker->activate();
    else model_picker->deactivate();
    if (idle && !engine.owned())
      port_input->activate();
    else
      port_input->deactivate();
    if (here)
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
    auto line = state;
    if (engine.owned() && !gpu.empty())
      line += "  |  " + gpu;
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
  // Last conversation activity. Older chats have no stamp, so their file time
  // is read once and kept; later metadata saves must not reorder them.
  std::time_t last_active(const std::string &id) {
    double updated = store.chats.at(id).number("updated");
    if (updated > 0) return static_cast<std::time_t>(updated);
    auto found = file_times.find(id);
    if (found != file_times.end()) return found->second;
    struct stat info {};
    std::time_t time = 0;
    if (::stat((store.root / "chats" / (id + ".json")).c_str(), &info) == 0)
      time = info.st_mtime;
    file_times[id] = time;
    return time;
  }
  void touch(Json &chat) {
    chat.set("updated", static_cast<double>(std::time(nullptr)));
  }
  // Saves title, pin or workspace changes without changing the chat's place.
  void save_meta(Json chat) {
    if (chat.number("updated") <= 0)
      chat.set("updated", static_cast<double>(last_active(chat.str("id"))));
    store.save("chats", chat);
  }
  const SearchText &search_text(const std::string &id) {
    const auto &chat = store.chats.at(id);
    auto messages = chat.get("messages");
    int count = cJSON_GetArraySize(messages);
    auto &entry = search_cache[id];
    auto title = chat.str("title");
    if (entry.messages != count || entry.title != title) {
      entry.title = title;
      entry.messages = count;
      entry.original.clear();
      cJSON *p;
      cJSON_ArrayForEach(p, messages) {
        auto content = cJSON_GetObjectItemCaseSensitive(p, "content");
        if (cJSON_IsString(content)) {
          entry.original += content->valuestring;
          entry.original += '\n';
        }
      }
      entry.folded = fold(entry.original);
    }
    return entry;
  }
  static std::string snippet(const SearchText &text, size_t at) {
    const auto &source = text.folded.size() == text.original.size() ? text.original
                                                                    : text.folded;
    size_t start = at > 30 ? at - 30 : 0;
    while (start && (static_cast<unsigned char>(source[start]) & 0xc0) == 0x80) --start;
    if (start) {
      auto space = source.find_first_of(" \n", start);
      if (space != source.npos && space < at) start = space + 1;
    }
    auto piece = clip_words(source.substr(start, 140), 110);
    return start ? "…" + piece : piece;
  }
  static ChatSidebar::Row label_row(ChatSidebar::Kind kind, const char *text) {
    ChatSidebar::Row row{};
    row.kind = kind;
    row.title = text;
    return row;
  }
  void refresh_sidebar() {
    std::string previous;

    if (sidebar->selectable(sidebar->value()))
      previous = sidebar->entries[sidebar->value() - 1].id;
    auto query = clip_words(fold(chat_search->value()), 200);
    auto now = std::time(nullptr);
    auto &rows = sidebar->entries;
    rows.clear();
    struct Item {
      std::string id;
      std::time_t time;
      size_t at;
    };
    auto by_time = [](const Item &a, const Item &b) {
      return a.time != b.time ? a.time > b.time : a.id < b.id;
    };
    auto chat_row = [&](const Item &item, const std::string &snip = "") {
      const auto &chat = store.chats.at(item.id);
      ChatSidebar::Row row{ChatSidebar::Chat, item.id, clean_label(chat.str("title")),
                           when_label(item.time, now), snip};
      row.pinned = chat.number("pinned") != 0;
      if (!query.empty() && chat.str("workspace") != workspace)
        row.when = clean_label(store.workspaces.at(chat.str("workspace")).str("name")) +
                   (row.when.empty() ? "" : " · " + row.when);
      rows.push_back(row);
    };
    if (query.empty()) {
      std::vector<Item> pinned, items;
      for (const auto &chat : store.chats)
        if (chat.second.str("workspace") == workspace)
          (chat.second.number("pinned") != 0 ? pinned : items)
              .push_back({chat.first, last_active(chat.first), 0});
      std::sort(pinned.begin(), pinned.end(), by_time);
      std::sort(items.begin(), items.end(), by_time);
      if (!pinned.empty()) {
        rows.push_back(label_row(ChatSidebar::Section, "Pinned"));
        for (const auto &item : pinned) chat_row(item);
      }
      std::tm day = *std::localtime(&now);
      day.tm_hour = day.tm_min = day.tm_sec = 0;
      std::time_t today = std::mktime(&day);
      const std::pair<std::time_t, const char *> groups[] = {
          {today, "Today"}, {today - 86400, "Yesterday"},
          {today - 6 * 86400, "Previous 7 days"},
          {today - 29 * 86400, "Previous 30 days"},
          {std::numeric_limits<std::time_t>::min(), "Older"}};
      size_t group = std::size(groups);
      for (const auto &item : items) {
        size_t g = 0;
        while (g + 1 < std::size(groups) && item.time < groups[g].first) ++g;
        if (g != group) {
          group = g;
          rows.push_back(label_row(ChatSidebar::Section, groups[g].second));
        }
        chat_row(item);
      }
      if (rows.empty())
        rows.push_back(label_row(ChatSidebar::Empty, "No chats yet. Ctrl+N starts one."));
    } else {
      std::vector<Item> titles, contents;
      for (const auto &chat : store.chats) {
        if (fold(chat.second.str("title")).find(query) != std::string::npos)
          titles.push_back({chat.first, last_active(chat.first), 0});
        else {
          auto at = search_text(chat.first).folded.find(query);
          if (at != std::string::npos)
            contents.push_back({chat.first, last_active(chat.first), at});
        }
      }
      std::sort(titles.begin(), titles.end(), by_time);
      std::sort(contents.begin(), contents.end(), by_time);
      if (!titles.empty()) {
        rows.push_back(label_row(ChatSidebar::Section, "Titles"));
        for (const auto &item : titles) chat_row(item);
      }
      if (!contents.empty()) {
        rows.push_back(label_row(ChatSidebar::Section, "Messages"));
        size_t shown = 0;
        for (const auto &item : contents) {
          if (++shown > 200) break;
          chat_row(item, snippet(search_cache.at(item.id), item.at));
        }
      }
      if (rows.empty()) rows.push_back(label_row(ChatSidebar::Empty, "No matches"));
    }
    sidebar->active = active;
    new_button->copy_tooltip(("New chat in " + store.workspaces.at(workspace).str("name") +
                             " (Ctrl+N)").c_str());
    sidebar->refresh();
    int line = previous.empty() ? 0 : sidebar->line_of(previous);
    if (!line) line = sidebar->line_of(active);
    sidebar->value(line);
  }
  void sidebar_action(const ChatSidebar::Row &row, ChatSidebar::Action action) {
    if (working || !store.chats.count(row.id))
      return;
    switch (action) {
    case ChatSidebar::Open: open_chat(row.id); break;
    case ChatSidebar::Rename: begin_rename(row.id); break;
    case ChatSidebar::Delete: delete_chat(row.id); break;
    case ChatSidebar::Menu: chat_menu(row.id); break;
    }
  }
  void chat_menu(const std::string &id) {
    const auto &chat = store.chats.at(id);
    bool pinned = chat.number("pinned") != 0;
    std::vector<std::pair<std::string, std::string>> targets;
    for (const auto &ws : store.workspaces)
      if (ws.first != chat.str("workspace"))
        targets.emplace_back(menu_label(clean_label(ws.second.str("name"))), ws.first);
    std::sort(targets.begin(), targets.end());
    enum { Open = 1, Rename, Pin, Close, Delete, Move = 100 };
    std::vector<Fl_Menu_Item> items;
    auto item = [&](const char *text, long code, int flags = 0, int key = 0) {
      Fl_Menu_Item m{};
      m.text = text;
      m.shortcut_ = key;
      m.user_data_ = reinterpret_cast<void *>(code);
      m.flags = flags;
      items.push_back(m);
    };
    bool tab = show_chats &&
               std::find(open_chats.begin(), open_chats.end(), id) != open_chats.end();
    item("&Open", Open);
    item("&Rename", Rename, 0, FL_F + 2);
    item(pinned ? "Un&pin" : "&Pin to top", Pin, targets.empty() && !tab ? FL_MENU_DIVIDER : 0);
    if (!targets.empty()) {
      item("&Move to", 0, FL_SUBMENU | (tab ? 0 : FL_MENU_DIVIDER));
      for (size_t i = 0; i < targets.size(); ++i)
        item(targets[i].first.c_str(), Move + static_cast<long>(i));
      items.push_back(Fl_Menu_Item{});
    }
    if (tab) item("&Close tab", Close, FL_MENU_DIVIDER);
    item("&Delete...", Delete, busy && id == stream_chat ? FL_MENU_INACTIVE : 0,
         FL_Delete);
    items.push_back(Fl_Menu_Item{});
    auto at = sidebar->context_position();
    auto chosen = items.data()->popup(at.first, at.second);
    if (!chosen) return;
    long code = reinterpret_cast<long>(chosen->user_data_);
    if (code == Open) open_chat(id);
    else if (code == Rename) begin_rename(id);
    else if (code == Pin) toggle_pin(id);
    else if (code == Close) close_chat(id);
    else if (code == Delete) delete_chat(id);
    else if (code >= Move && static_cast<size_t>(code - Move) < targets.size())
      move_chat(id, targets[code - Move].second);
  }
  void toggle_pin(const std::string &id) {
    if (working || !store.chats.count(id)) return;
    auto chat = store.chats.at(id);
    chat.set("pinned", chat.number("pinned") != 0 ? 0.0 : 1.0);
    save_meta(chat);
    refresh_sidebar();
  }
  void move_chat(const std::string &id, const std::string &target) {
    if (working || !store.chats.count(id) || !store.workspaces.count(target)) return;
    auto chat = store.chats.at(id);
    chat.set("workspace", target);
    save_meta(chat);
    if (id == active) show_chat(true);
    refresh_workspaces();
    refresh_sidebar();
    set_phase("Moved to " + store.workspaces.at(target).str("name"));
  }
  void begin_rename(const std::string &id) {
    if (working || id.empty() || !store.chats.count(id)) return;
    if (!show_left) {
      show_left = true;
      layout();
    }
    if (!sidebar->line_of(id) && *chat_search->value()) {
      chat_search->value("");
      refresh_sidebar();
    }
    int line = sidebar->line_of(id);
    if (!line) return;
    sidebar->select_line(line);
    renaming = id;
    rename_field->value(store.chats.at(id).str("title").c_str());
    place_rename();
    rename_field->show();
    rename_field->take_focus();
    rename_field->insert_position(0, rename_field->size());
  }
  void place_rename() {
    int line = sidebar->line_of(renaming);
    int bar = sidebar->scrollbar.visible() ? Fl::scrollbar_size() : 0;
    int top = std::clamp(sidebar->line_y(std::max(line, 1)), sidebar->y() + 2,
                         sidebar->y() + std::max(2, sidebar->h() - 26));
    place(rename_field, sidebar->x() + 6, top, sidebar->w() - 10 - bar, 24);
    rename_field->redraw();
    sidebar->redraw();
  }
  void finish_rename(bool save) {
    auto id = renaming;
    renaming.clear();
    if (save && store.chats.count(id)) {
      auto title = clip_words(rename_field->value(), 200);
      if (!title.empty() && title != store.chats.at(id).str("title")) {
        auto chat = store.chats.at(id);
        chat.set("title", title);
        chat.set("renamed", 1.0);
        save_meta(chat);
        refresh_tabs();
      }
    }
    refresh_sidebar();
    if (Fl::event() == FL_KEYBOARD) sidebar->take_focus();
    redraw();
  }
  void focus_search() {
    if (!show_left) {
      show_left = true;
      layout();
    }
    chat_search->take_focus();
    chat_search->insert_position(0, chat_search->size());
  }
  // Ctrl+Page Up/Down follow the tabs when shown, otherwise the list order.
  void step_chat(int direction) {
    if (show_chats) {
      tabs->step(direction);
      return;
    }
    int line = sidebar->line_of(active);
    for (int next = line + direction; line && next > 0 && next <= sidebar->size();
         next += direction)
      if (sidebar->selectable(next)) {
        open_chat(sidebar->entries[next - 1].id);
        return;
      }
  }
  void refresh_workspaces() {
    workspace_picker->clear();
    workspace_ids.clear();
    std::vector<std::pair<std::string, std::string>> list;
    for (const auto &ws : store.workspaces)
      list.emplace_back(clean_label(ws.second.str("name")), ws.first);
    std::sort(list.begin(), list.end());
    for (const auto &ws : list) {
      auto text = ws.first;
      for (size_t at = 0; (at = text.find('&', at)) != text.npos; at += 2)
        text.insert(at, "&");
      int index = workspace_picker->add(("\x01" + std::to_string(workspace_ids.size())).c_str());
      workspace_picker->replace(index, text.c_str());
      workspace_ids.push_back(ws.second);
    }
    workspace_picker->mode(static_cast<int>(workspace_ids.size()) - 1, FL_MENU_DIVIDER);
    workspace_picker->add("\x01" "new");
    workspace_picker->replace(static_cast<int>(workspace_ids.size()), "New workspace...");
    workspace_picker->add("\x01" "settings");
    workspace_picker->replace(static_cast<int>(workspace_ids.size()) + 1,
                              "Workspace settings...");
    for (size_t i = 0; i < workspace_ids.size(); ++i)
      if (workspace_ids[i] == workspace)
        workspace_picker->value(static_cast<int>(i));
  }
  void pick_workspace() {
    int i = workspace_picker->value();
    if (i < 0) return;
    if (static_cast<size_t>(i) < workspace_ids.size()) {
      switch_workspace(workspace_ids[i]);
      return;
    }
    refresh_workspaces();
    edit_workspace(static_cast<size_t>(i) == workspace_ids.size());
  }
  // Switching opens the workspace's most recent chat, or a fresh one.
  void switch_workspace(const std::string &id) {
    if (working || id == workspace || !store.workspaces.count(id)) return;
    chat_search->value("");
    std::string best;
    std::time_t latest = 0;
    for (const auto &chat : store.chats)
      if (chat.second.str("workspace") == id &&
          (best.empty() || last_active(chat.first) > latest)) {
        best = chat.first;
        latest = last_active(chat.first);
      }
    if (best.empty()) {
      save_draft();
      workspace = id;
      new_chat();
    } else
      open_chat(best);
    refresh_workspaces();
    refresh_sidebar();
  }
  // Asks the loaded model for a short title; the instant title stays if the
  // engine is busy, thinks instead of answering, or the user renames first.
  void suggest_title(const std::string &id, const std::string &question,
                     const std::string &answer) {
    if (title_transport) title_transport->cancel();
    auto t = std::make_shared<Transport>(port());
    title_transport = t;
    Json body, system, user, kwargs;
    auto messages = Json::array();
    system.set("role", std::string("system"));
    system.set("content", std::string(
        "You name chat conversations. Reply with only a title of 2 to 6 words "
        "in the language of the conversation. No quotes. No final punctuation."));
    user.set("role", std::string("user"));
    user.set("content", "User: " + truncate(question, 600) + "\nAssistant: " +
                            truncate(answer, 300) + "\n\nTitle:");
    messages.add(system);
    messages.add(user);
    body.set("model", std::string("local"));
    body.set("messages", messages);
    body.set("max_tokens", 24.0);
    body.set("temperature", 0.2);
    cJSON_AddBoolToObject(kwargs.p, "enable_thinking", 0);
    body.set("chat_template_kwargs", kwargs);
    cJSON_AddBoolToObject(body.p, "stream", 0);
    auto wire = body.dump();
    launch([this, t, id, wire] {
      std::string title;
      try {
        auto reply = Json::parse(t->exchange("/v1/chat/completions", &wire, {}, 30));
        auto first = cJSON_GetArrayItem(reply.get("choices"), 0);
        auto message = first ? cJSON_GetObjectItemCaseSensitive(first, "message") : nullptr;
        auto content = message ? cJSON_GetObjectItemCaseSensitive(message, "content") : nullptr;
        if (cJSON_IsString(content)) title = tidy_title(content->valuestring);
      } catch (const std::exception &) {
      }
      post([this, t, id, title] {
        if (title_transport == t) title_transport.reset();
        if (title.empty() || t->cancelled || !store.chats.count(id)) return;
        auto chat = store.chats.at(id);
        if (chat.number("renamed") != 0) return;
        chat.set("title", title);
        save_meta(chat);
        refresh_tabs();
        refresh_sidebar();
      });
    });
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
      save_meta(candidate);
    dirty = false;
  }
  void open_chat(const std::string &id) {
    if (working || !store.chats.count(id))
      return;
    if (id == active) {

      refresh_sidebar();
      return;
    }
    save_draft();
    if (std::find(open_chats.begin(), open_chats.end(), id) ==
        open_chats.end()) {
      // Tabs are history now; the oldest quietly gives way.
      if (open_chats.size() >= 32)
        open_chats.erase(open_chats.begin());
      open_chats.push_back(id);
    }
    active = id;
    stream_prefix = -1;
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
    if (workspace_picker->size() &&
        (workspace_picker->value() < 0 ||
         workspace_ids.at(workspace_picker->value()) != workspace))
      refresh_workspaces();
    layout();
    controls();
  }
  void new_chat() {
    if (working)
      return;
    save_draft();
    chat_search->value("");
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
    if (working)
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
  void delete_chat(std::string id = "") {
    if (id.empty()) id = active;
    if (working || id.empty() || !store.chats.count(id))
      return;
    if (busy && id == stream_chat)
      throw std::runtime_error("Stop the reply before deleting this chat.");
    auto title = clip_words(store.chats.at(id).str("title"), 80);
    if (fl_choice("Delete \"%s\"?\nThis cannot be undone.", "Cancel", "Delete",
                  nullptr, title.c_str()) != 1)
      return;
    // Selection moves to the neighbouring row, as in a file list.
    std::string next;
    int line = sidebar->line_of(id);
    for (int step : {1, -1}) {
      for (int at = line + step; line && next.empty() && at > 0 && at <= sidebar->size(); at += step)
        if (sidebar->selectable(at)) next = sidebar->entries[at - 1].id;
    }
    store.erase(id);
    search_cache.erase(id);
    file_times.erase(id);
    open_chats.erase(std::remove(open_chats.begin(), open_chats.end(), id),
                     open_chats.end());
    if (id == active) {
      active.clear();
      stream_prefix = -1;
      if (!next.empty() && store.chats.count(next))
        open_chat(next);
      else
        new_chat();
    }
    refresh_tabs();
    refresh_sidebar();
  }
  void append(std::string &text, std::string &style, const std::string &s,
              char code) {
    text += s;
    style.append(s.size(), code);
  }
  // Quiet speaker lines, as the Windows transcript writes them.
  void speaker(std::string &text, std::string &style, bool assistant,
               const std::string &model = "") {
    append(text, style, assistant ? "Assistant says:" : "You say:", assistant ? 'B' : 'M');
    if (assistant && !model.empty())
      append(text, style, "  " + model, 'Q');
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
    markdown_render_width(
        wide(s).c_str(), transcript->columns(),
        +[](void *v, const wchar_t *s, unsigned flags) {
          auto &o = *static_cast<Output *>(v);
          char code = o.thinking
                      ? (flags & MdBlock ? 'O' : flags & MdCode ? 'K' : flags & MdHeading ? 'L'
                         : flags & MdBold ? 'I' : flags & MdItalic ? 'J' : 'H')
                      : flags & MdBlock     ? (flags & MdBold ? 'P' : 'N')
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
    if (streaming_here() && stream_prefix >= 0) {
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
    if (streaming_here()) {
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
    if (streaming_here())
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
        chosen("Attach a file",
               "All supported (*.{txt,md,markdown,csv,tsv,json,jsonl,xml,yaml,yml,"
               "toml,ini,cfg,conf,log,html,htm,css,js,mjs,ts,tsx,jsx,py,c,h,cpp,"
               "hpp,cc,cs,java,kt,go,rs,rb,php,sh,bash,ps1,bat,sql,lua,swift,r,"
               "tex,srt,vtt,png,jpg,jpeg,bmp,gif,wav,mp3,flac,mp4,mkv,webm,mov})\t"
               "Text and code (*.{txt,md,markdown,csv,tsv,json,jsonl,xml,yaml,yml,"
               "toml,ini,cfg,conf,log,html,htm,css,js,mjs,ts,tsx,jsx,py,c,h,cpp,"
               "hpp,cc,cs,java,kt,go,rs,rb,php,sh,bash,ps1,bat,sql,lua,swift,r,"
               "tex,srt,vtt})\t"
               "Images, audio and video (*.{png,jpg,jpeg,bmp,gif,wav,mp3,flac,"
               "mp4,mkv,webm,mov})");
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
    branch.set("pinned", 0.0);
    touch(branch);
    store.save("chats", branch);
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
        save_draft();
        workspace = result.str("id");
        new_chat();
      } else if (id == workspace) {
        set_text(master_buffer, result.str("master_prompt"));
      }
      refresh_workspaces();
      refresh_sidebar();
      layout();
    }
  }
  void update_generation() {
    quick_thinking->value(g.thinking);
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
    // Thinking changes which recommended sampler applies.
    if (field == GenThinking) {
      g = config.generation(model_path, has_info ? &info : nullptr);
      update_generation();
    }
    if (field == GenContext && engine.owned())
      set_phase("Context changed. Unload and reload the model to apply it.");
  }
  // One entry per library model, then the current file if it lives elsewhere.
  void refresh_picker() {
    model_picker->clear();
    picker_paths.clear();
    auto add = [&](std::string text, const std::string &path) {
      for (size_t at = 0; (at = text.find('&', at)) != text.npos; at += 2)
        text.insert(at, "&");
      int index = model_picker->add(("\x01" + std::to_string(picker_paths.size())).c_str());
      model_picker->replace(index, text.c_str());
      picker_paths.push_back(path);
    };
    auto current = model_path.empty() ? fs::path() : fs::weakly_canonical(model_path);
    bool listed = false;
    for (const auto &m : models) {
      if (model_role(m) == "Draft companion")
        continue;
      auto path = utf8(m.path);
      std::ostringstream text;
      text << clean_label(utf8(m.name)) << "   (" << std::fixed
           << std::setprecision(1) << double(m.bytes) / (1024 * 1024 * 1024)
           << " GB)";
      listed |= !current.empty() && fs::weakly_canonical(path) == current;
      add(text.str(), path);
    }
    // Recently used files outside the library folder stay one click away.
    std::vector<std::string> recent;
    try {
      auto list = Json::parse(config.get("engine", "recent", "[]"));
      cJSON *item;
      cJSON_ArrayForEach(item, list.p) if (cJSON_IsString(item)) recent.push_back(item->valuestring);
    } catch (const std::exception &) {
    }
    if (!model_path.empty())
      recent.insert(recent.begin(), model_path);
    for (const auto &path : recent) {
      std::error_code error;
      if (!fs::is_regular_file(path, error))
        continue;
      auto canonical = fs::weakly_canonical(path);
      bool known = false;
      for (const auto &p : picker_paths)
        known |= fs::weakly_canonical(p) == canonical;
      if (known)
        continue;
      add(canonical == current && has_info ? clean_label(utf8(info.name))
                                           : fs::path(path).stem().string(),
          path);
    }
    if (!picker_paths.empty())
      model_picker->mode(static_cast<int>(picker_paths.size()) - 1, FL_MENU_DIVIDER);
    model_picker->add("\x01" "browse");
    model_picker->replace(static_cast<int>(picker_paths.size()), "Browse for a GGUF file...");
    model_picker->value(-1);
    for (size_t i = 0; i < picker_paths.size(); ++i)
      if (!current.empty() && fs::weakly_canonical(picker_paths[i]) == current)
        model_picker->value(static_cast<int>(i));
  }
  void remember_model(const std::string &path) {
    auto list = Json::array();
    Json old = Json::array();
    try {
      old = Json::parse(config.get("engine", "recent", "[]"));
    } catch (const std::exception &) {
    }
    cJSON *item;
    list.add(Json(cJSON_CreateString(path.c_str())));
    cJSON_ArrayForEach(item, old.p) {
      if (cJSON_GetArraySize(list.p) >= 8)
        break;
      if (cJSON_IsString(item) && path != item->valuestring)
        list.add(Json(cJSON_Duplicate(item, 1)));
    }
    config.set("engine", "recent", list.dump());
    config.save();
  }
  void pick_model() {
    int i = model_picker->value();
    if (i < 0)
      return;
    if (static_cast<size_t>(i) >= picker_paths.size()) {
      refresh_picker();
      auto path = chosen("Select a GGUF chat model", "*.gguf", model_path);
      if (!path.empty())
        switch_model(path);
      return;
    }
    switch_model(picker_paths[i]);
  }
  // Picking a model loads it, replacing whatever is loaded.
  void switch_model(const std::string &path) {
    if (busy || working)
      return;
    if (!model_path.empty() && fs::weakly_canonical(path) == fs::weakly_canonical(model_path)) {
      if (!engine.owned())
        load_model();
      return;
    }
    if (engine.owned()) {
      pending_model = path;
      unload();
    } else
      read_selected_model(path, true, false);
  }
  void layout_props() {
    const int px = 20, py = 44, pw = 520;
    place(right_tabs, 8, 8, 544, 506);
    for (auto page : {models_page, generation_page, library_page})
      place(page, 10, 34, 540, 478);
    place(engine_page, 10, 34, 540, 478);
    for (Fl_Widget *widget : std::initializer_list<Fl_Widget *>{runtime_model_label,
             model_name, model_library_label, master_label, master, right_header})
      widget->hide();
    place(properties_label, px, py, pw, 22);
    place(details, px, py + 26, pw, 270);
    place(projector_label, px, py + 310, pw, 20);
    place(projector_text, px, py + 332, pw, 24);
    place(projector_button, px, py + 362, pw / 2 - 2, 26);
    place(projector_clear, px + pw / 2 + 2, py + 362, pw / 2 - 2, 26);
    int row = 0;
    for (int i = 0; i < GenCount; ++i) {
      place(generation_labels[i], px, py + row * 28, pw - 170, 24);
      place(i == GenThinking ? static_cast<Fl_Widget *>(thinking_choice) : generation_fields[i],
            px + pw - 160, py + row * 28, 160, 24);
      ++row;
    }
    place(reset_generation, px + pw - 160, py + row * 28 + 4, 160, 26);
    place(generation_help, px, py + row * 28 + 40, pw, 478 - (row * 28 + 50));
    generation_help->show();
    place(folder_label, px, py, pw, 20);
    place(folder, px, py + 22, pw - 196, 24);
    place(folder_button, px + pw - 190, py + 22, 92, 24);
    place(scan_button, px + pw - 94, py + 22, 94, 24);
    place(recursive, px, py + 50, pw, 22);
    place(model_list, px, py + 78, pw, 330);
    place(use_model_button, px + pw - 180, py + 414, 180, 26);
    use_model_button->copy_label("Load selected model");
    std::string engine_text = "llama-server:\n" +
                              (engine_path.empty() ? std::string("Not chosen") : engine_path);
    if (!gpu.empty())
      engine_text += "\n\nAccelerator: " + gpu;
    engine_path_label->copy_label(engine_text.c_str());
    place(engine_path_label, px, py, pw, 96);
    place(engine_choose_button, px, py + 102, 160, 26);
    place(port_label, px, py + 146, 60, 24);
    place(port_input, px + 64, py + 146, 100, 24);
    place(engine_options_button, px, py + 184, 160, 26);
  }
  void show_properties(int tab) {
    layout_props();
    Fl_Widget *pages[] = {models_page, generation_page, library_page, engine_page};
    right_tabs->value(pages[std::clamp(tab, 0, 3)]);
    if (has_info)
      show_model_details(info);
    props->show();
  }
  void reset_defaults() {
    if (busy || working)
      return;
    for (const char *key : fields)
      config.erase(config.section(model_path), key);
    config.save();
    g = config.generation(model_path, has_info ? &info : nullptr);
    update_generation();
    note("Generation settings reset to the model defaults.");
    if (engine.owned())
      set_phase("Defaults restored. Reload the model to apply the context.");
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
    if (busy || working)
      return;
    auto path = chosen("Select a GGUF chat model", "*.gguf", model_path);
    if (!path.empty())
      switch_model(path);
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
          remember_model(model_path);
          refresh_picker();
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
  void show_shortcuts() {
    Fl_Double_Window dialog(x() + (w() - 520) / 2, y() + (h() - 559) / 2, 520, 559,
                            "Keyboard shortcuts");
    dialog.color(Face);
    dialog.begin();
    ShortcutTable table(12, 12, 496, 500);
    Fl_Return_Button close(422, 522, 86, 25, "Close");
    close.labelsize(12);
    close.callback(+[](Fl_Widget *w, void *) { w->window()->hide(); });
    dialog.end();
    dialog.set_modal();
    dialog.show();
    while (dialog.shown())
      Fl::wait();
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
  // The projector costs VRAM and context, so it loads only for media chats.
  void load_model(bool media = false) {
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
    auto exe = engine_path, model = model_path,
         proj = media ? projector : std::string(),
         extra = config.get("engine", "extra_arguments");
    media_engine = !proj.empty();
    auto p = port();
    int context = g.context_tokens;
    set_phase("Loading model...");
    ready = false;
    model_loading = true;
    load_started = std::chrono::steady_clock::now();
    job(
        [this, exe, model, proj, extra, p, context] {
          std::vector<std::string> devices;
          std::string accelerator;
          if (list_devices(exe, devices)) {
            accelerator = devices.empty() ? "CPU only" : devices[0];
            auto name = accelerator.find(": ");
            if (name != accelerator.npos)
              accelerator.erase(0, name + 2);
            auto memory = accelerator.find(" (");
            if (memory != accelerator.npos)
              accelerator.erase(memory);
          }
          engine.start(exe, model, p, context, store.root / "engine.log", proj,
                       extra);
          return accelerator;
        },
        [this, exe](const std::string &accelerator) {
          gpu = accelerator;
          bool cpu_only = accelerator == "CPU only";
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
    if (busy || working || !engine.owned())
      return;
    pending_send = false;
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
          if (!pending_model.empty()) {
            auto next = pending_model;
            pending_model.clear();
            read_selected_model(next, true, false);
          } else if (reload_media) {
            reload_media = false;
            load_model(true);
          }
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
          refresh_picker();
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
    switch_model(utf8(m.path));
  }
  void send() {
    if (busy || working || active.empty())
      return;
    auto text = buffer_text(prompt_buffer);
    // Text files are inlined; only images, audio and video need the projector.
    int media_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, attachments.p)
      if (!Store::is_text(Json(cJSON_Duplicate(item, 1)))) ++media_count;
    if (text.find_first_not_of(" \t\r\n") == text.npos) {
      if (!cJSON_GetArraySize(attachments.p))
        return;
      text = media_count ? "Describe the attached media." : "Review the attached file.";
    }
    validate_text(text, LcbMaxPrompt, false);
    if (media_count && engine.owned() && !media_engine) {
      if (projector.empty())
        throw std::runtime_error("Attachments need a media projector. Choose "
                                 "one for this model under Media projector.");
      if (fl_choice("Attachments need the model's media projector, which is "
                    "loaded only when needed.\nReload the model with it now? "
                    "Send again once it is ready.",
                    "Cancel", "Reload", nullptr) == 1) {
        reload_media = true;
        unload();
      }
      return;
    }
    if (model_path.empty())
      throw std::runtime_error(
          "Select the GGUF model used by the local server first.");
    // Send loads the model when needed and goes out as soon as it is ready.
    if (!ready) {
      if (!engine.owned() && engine_path.empty())
        throw std::runtime_error("Choose a llama-server under Properties > Engine first.");
      save_draft();
      pending_send = true;
      pending_chat = active;
      if (!engine.owned()) {
        try {
          load_model(media_count > 0 && !projector.empty());
        } catch (...) {
          pending_send = false;
          throw;
        }
        if (!model_loading) pending_send = false;
      }
      if (pending_send) set_phase("Loading model. Your message sends when it is ready.");
      controls();
      return;
    }
    if (title_transport) title_transport->cancel();
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
    stream_chat = id;
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
            if (streaming_here()) render();
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
            bool first = cJSON_GetArraySize(list.p) == 2 &&
                         candidate.number("renamed") == 0;
            if (first)
              candidate.set("title", quick_title(text));
            touch(candidate);
            store.chats.insert_or_assign(id, candidate);
            try {
              store.save("chats", candidate);
            } catch (const std::exception &e) {
              error("Reply is still in memory; save failed: " +
                    std::string(e.what()));
            }
            if (active == id)
              show_chat(true);
            else
              controls();
            refresh_tabs();
            refresh_sidebar();
            if (first && reply.status == "complete" && !reply.content.empty())
              suggest_title(id, text, reply.content);
          } else if (active == id)
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
      config.set("linux-layout", "tabs", show_chats ? "1" : "0");
      config.set("linux-layout", "output", show_output ? "1" : "0");
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
    set_window_icons();

    std::signal(SIGTERM, exit_signal);
    std::signal(SIGINT, exit_signal);
    Fl::lock();
    // Text editors consume page and tab keys before their parent receives them.
    // Dispatch chat-switching shortcuts first, but leave modal input alone.
    Fl::event_dispatch(+[](int event, Fl_Window *window) {
      int modifiers = Fl::event_state() & (FL_CTRL | FL_ALT | FL_SHIFT), key = Fl::event_key();
      if (event == FL_KEYBOARD && !Fl::modal() && !Fl::grab() &&
          ((modifiers == FL_CTRL && (key == FL_Page_Up || key == FL_Page_Down)) ||
           ((modifiers & ~FL_SHIFT) == FL_CTRL && key == FL_Tab)) &&
          dynamic_cast<Window *>(window))
        return window->handle(event);
      return Fl::handle_(event, window);
    });
    Window window(root);
    window.show();
    result = Fl::run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    fl_alert("lcb-ai could not start:\n%s", e.what());
    result = 1;
  }
  return result;
}
