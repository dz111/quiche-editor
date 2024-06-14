#include <iostream>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include <assert.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#define KEY_CTRL_LEFT  545
#define KEY_CTRL_RIGHT 560
#define MOUSE_SCROLL_UP(e)    ((e) & 0x00010000)
#define MOUSE_SCROLL_DN(e)    ((e) & 0x00200000)

volatile sig_atomic_t window_resized = false;

struct LineMeta {
public:
  char* start;
  uint64_t size;
  uint64_t capacity;  // 0 if using original file buffer
public:
  bool has_edit_buffer() {
    return capacity > 0;
  }
  void alloc_edit_buffer() {
    bool delete_old_buffer = (capacity > 0);  // need to be careful not to delete some one else's memory
    char* line_data = start;
    capacity = size * 2;
    start = new char[capacity];
    memcpy(start, line_data, size);
    if (delete_old_buffer) {
      delete[] line_data;
    }
    beep(); // BEL
  }
};

std::vector<LineMeta> file_lines;

FILE* file = nullptr;
std::string filePath;
char* fileBuffer = nullptr;
bool dirty = false;

int first_line = 0;
int left_margin = 0;
int cx = 0, cy = 0;
int preferred_cx = 0;

time_t cl_message_time = 0;
std::string cl_message;
int cl_message_level = 0;

enum {
  COLOR_PAIR_LINENUM = 1,
  COLOR_PAIR_LINENUM_SHADED,
  COLOR_PAIR_LINE_SHADED,
  COLOR_PAIR_ERROR,
  COLOR_PINK = 101,
  COLOR_LINE_SHADE,
};

#define CTRL(x) ((x) & 0x1f)

void handle_sigwinch(int signal) {
  window_resized = true;
}

void handle_sigcont(int signal) {
  window_resized = true;
}

void putc(char c, unsigned int line, unsigned int col) {
  assert(line < file_lines.size());
  LineMeta& line_meta = file_lines[line];
  assert(col <= line_meta.size);
  if (line_meta.size >= line_meta.capacity) {
    line_meta.alloc_edit_buffer();
  }
  char* cp = line_meta.start + col;
  while (cp < line_meta.start + line_meta.size + 1) {
    char last_c = *cp;
    *cp++ = c;
    c = last_c;
  }
  line_meta.size++;
  dirty = true;
}

void removec(unsigned int line, unsigned int col) {
  assert(line < file_lines.size());
  LineMeta& line_meta = file_lines[line];
  assert(col < line_meta.size);
  if (line_meta.size >= line_meta.capacity) {
    line_meta.alloc_edit_buffer();
  }
  char* cp = line_meta.start + col;
  while (cp < line_meta.start + line_meta.size) {
    *cp = *(cp + 1);
    cp++;
  }
  line_meta.size--;
  dirty = true;
}

void putnl(unsigned int line, unsigned int col) {
  assert(line < file_lines.size());
  LineMeta& first_line = file_lines[line];
  assert(col <= first_line.size);

  LineMeta second_line = {0};
  second_line.start = first_line.start + col;
  second_line.size = first_line.size - col;
  if (second_line.size >= second_line.capacity) {
    second_line.alloc_edit_buffer();
  }

  first_line.size = col;

  auto iter = file_lines.begin() + line + 1;
  file_lines.insert(iter, second_line);
}

void combine_lines(unsigned int line1, unsigned int line2) {
  assert(line1 < file_lines.size());
  assert(line2 < file_lines.size());
  // this is probably not strictly required, but we probably don't
  // want to combine in the wrong order
  assert(line1 < line2);

  LineMeta& first_line = file_lines[line1];
  LineMeta& second_line = file_lines[line2];

  LineMeta new_line = {0};
  new_line.size = first_line.size + second_line.size;
  new_line.capacity = new_line.size * 2;
  new_line.start = new char[new_line.capacity];
  memcpy(new_line.start,                   first_line.start, first_line.size);
  memcpy(new_line.start + first_line.size, second_line.start, second_line.size);

  // be careful not to delete someone else's memory
  if (first_line.capacity > 0) {
    delete[] first_line.start;
  }
  if (second_line.capacity > 0) {
    delete[] second_line.start;
  }

  file_lines[line1] = new_line;

  auto iter = file_lines.begin() + line2;
  file_lines.erase(iter);
}

void display_file() {
  int last_line = LINES - 2 + first_line;
  int line_num_length = 0;
  for (int k = last_line - 1; k > 0; k /= 10) {
    line_num_length++;
  }
  left_margin = line_num_length + 1;

  // clear text canvas
  for (int y = 0; y < LINES - 1; y++) {
    move(y, 0);
    clrtoeol();
  }

  // file contents
  int rows = std::min((uint64_t)LINES - 2, file_lines.size() - first_line);
  int cols = COLS - left_margin;

  for (int i = 0; i < rows; i++) {
    int line_num = first_line + i;

    move(i, 0);
    int color_pair = COLOR_PAIR_LINENUM;
    if (line_num == cy) {
      color_pair = COLOR_PAIR_LINENUM_SHADED;
    }
    attron(COLOR_PAIR(color_pair));
    printw("%*d", line_num_length, line_num + 1);
    addch(' ');
    attroff(COLOR_PAIR(color_pair));

    LineMeta& line_meta = file_lines[line_num];
    int chars_to_put = line_meta.size;
    bool char_overflow = false;
    if (chars_to_put > cols) {
      chars_to_put = cols - 1;
      char_overflow = true;
    }
    if (line_num == cy) {
      attron(COLOR_PAIR(COLOR_PAIR_LINE_SHADED));
    }
    for (int j = 0; j < chars_to_put; j++) {
      addch(line_meta.start[j]);
    }

    if (char_overflow) {
      attron(A_REVERSE);
      addch('$');
      attroff(A_REVERSE);
    }

    if (line_num == cy) {
      int x, y;
      getyx(stdscr, y, x);
      for (; x < COLS; x++) {
        addch(' ');
      }
      attroff(COLOR_PAIR(COLOR_PAIR_LINE_SHADED));
    }
  }
}

void render_status() {
  move(LINES - 2, 0);
  attron(A_REVERSE);
  printw(filePath.c_str());
  if (dirty) {
    addch('*');
  }
  printw(" (%d:%d)", cy + 1, cx + 1);

  int x, y;
  getyx(stdscr, y, x);
  for (; x < COLS; x++) {
    addch(' ');
  }
  attroff(A_REVERSE);
}

void render_cl() {
  assert(cl_message_level >= 0 && cl_message_level <= 1);

  move(LINES - 1, 0);
  clrtoeol();
  time_t now = time(nullptr);
  if (difftime(now, cl_message_time) > 5.0) {
    return;
  }
  if (cl_message_level == 1) {
    attron(COLOR_PAIR(COLOR_PAIR_ERROR));
  }
  printw(cl_message.c_str());
  attroff(COLOR_PAIR(COLOR_PAIR_ERROR));
}

void update_screen() {
  display_file();
  render_status();
  render_cl();
  refresh();
}

void regenerate_screen() {
  window_resized = false;
  endwin();

  update_screen();
}

void scroll_file(int lines) {
  first_line += lines;
  if (first_line < 0) first_line = 0;
  const int MAX_BLANK_LINES = 0;
  int clamp_first_line = file_lines.size() - LINES + 2 + MAX_BLANK_LINES;
  if (first_line > clamp_first_line) first_line = clamp_first_line;
}

void screen_to_file(int y, int x, int& cy, int& cx) {
  cy = y + first_line;
  cx = x - left_margin;
}

void get_cursor(int& y, int& x) {
  y = cy - first_line;
  x = cx + left_margin;
}

void set_cursor() {
  int y, x;
  get_cursor(y, x);
  move(y, x);
}

void save(const std::string& savePath) {
  FILE* saveFile = fopen(savePath.c_str(), "w");
  bool first_line = true;
  for (LineMeta& line_meta : file_lines) {
    if (first_line) {
      first_line = false;
    } else {
      fputc('\n', saveFile);
    }
    fwrite(line_meta.start, 1, line_meta.size, saveFile);
  }
  fclose(saveFile);
  dirty = false;
  beep();
}

void dialog_render(const std::string& prompt, const std::string& entry, const int cursor) {
  assert(cursor >= 0);
  assert(cursor <= entry.size());
  move(LINES - 1, 0);
  clrtoeol();
  printw(prompt.c_str());
  printw(entry.c_str());
  move(LINES - 1, prompt.size() + cursor);
}

void dialog_keyinput(std::string& entry, int c, int& cursor) {
  if (c >= ' ' && c <= '~') {
    entry.insert(cursor, 1, (char)c);
    cursor++;
  } else if (c == KEY_LEFT) {
    cursor--;
    if (cursor < 0) cursor = 0;
  } else if (c == KEY_RIGHT) {
    cursor++;
    if (cursor > entry.size()) cursor = entry.size();
  } else if (c == KEY_BACKSPACE) {
    if (cursor == 0) return;
    entry.erase(cursor - 1, 1);
    cursor--;
  } else if (c == KEY_DC) {
    if (cursor == entry.size()) return;
    entry.erase(cursor, 1);
  } else if (c == KEY_HOME) {
    cursor = 0;
  } else if (c == KEY_END) {
    cursor = entry.size();
  }
}

void dialog_clear() {
  move(LINES - 1, 0);
  clrtoeol();
}

bool savedialog(std::string& savePath) {
  std::string newSavePath = savePath;
  int cx = savePath.size();

  while (1) {
    dialog_render("Filename: ", newSavePath, cx);
    int c = wgetch(stdscr);

    if (c == 27) {
      dialog_clear();
      return false;
    } else if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      savePath = newSavePath;
      dialog_clear();
      return true;
    } else {
      dialog_keyinput(newSavePath, c, cx);
    }
  }
}

bool exitdialog() {
  while (1) {
    move(LINES - 1, 0);
    clrtoeol();
    printw("Save before exit? (y/n/esc) ");

    int c = wgetch(stdscr);
    if (c == 27) {
      return false;
    } else if (c == 'n') {
      return true;
    } else if (c == 'y') {
      if (savedialog(filePath)) {
        save(filePath);
        return true;
      }
    }
  }
}

template<typename... Args>
void strprintf(std::string& s, const char* fmt, Args&&... args) {
  int string_size = snprintf(nullptr, 0, fmt, args...);
  s.reserve(string_size + 1);
  snprintf(&s[0], string_size + 1, fmt, args...);
}

template<typename... Args>
void printcl(int level, const char* fmt, Args&&... args) {
  cl_message_time = time(nullptr);
  cl_message_level = level;
  strprintf(cl_message, fmt, args...);
}

void scroll_to_cursor() {
  int y, x;
  get_cursor(y, x);
  if (y < 0) {
    scroll_file(y);
  } else if (y > LINES - 3) {
    scroll_file((y - (LINES - 3)));
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: qe <filename>\n");
    return -1;
  }

  filePath = argv[1];
  const char* cFilePath = argv[1];
  file = fopen(cFilePath, "r+");
  if (!file) {
    fprintf(stderr, "Could not open file '%s' for editing.\n", cFilePath);
    return -1;
  }

  fseek(file, 0, SEEK_END);
  unsigned int fileSize;
  int result = fgetpos(file, (fpos_t*)&fileSize);
  if (result != 0) {
    fprintf(stderr, "Error calling fgetpos().\n");
    return -1;
  }
  fseek(file, 0, SEEK_SET);  // beginning

  fileBuffer = new char[fileSize];
  fread(fileBuffer, 1, fileSize, file);

  // initialise line data
  char* cp = fileBuffer;
  char* fileBufferEnd = fileBuffer + fileSize;
  while (1) {
    LineMeta line = {0};
    line.start = cp;

    char c = *cp++;
    while (c != '\r' && c != '\n' && cp <= fileBufferEnd) {
      line.size++;
      c = *cp++;
    }
    if (c == '\r') {
      if (*cp == '\n') {
        c = *cp++;
      }
    }

    file_lines.push_back(line);
    if (cp >= fileBufferEnd) {
      char ce = *(fileBufferEnd - 1);
      if (ce == '\r' || ce == '\n') {
        line = {0};
        file_lines.push_back(line);
      }
      break;
    }
  }

  // get filename
  //{
  //  const char* start = cFilePath;
  //  const char* end = start;
  //  while (*end != 0) end++;  // advance to end of string
  //  const char* fstart = end;
  //  while (fstart > start && *fstart != '/' && *fstart != '\\') fstart--;
  //  if (fstart > start) fstart++;
  //  fileName = fstart;
  //}

  initscr();  // Start curses
  raw();      // Disable line buffering so we get inputs asap
  nonl();     // No new lines
  noecho();   // No echo
  set_escdelay(0);
  mouseinterval(0);  // Skip test for 'clicked' so that 'pressed' and 'released' are snappy
  keypad(stdscr, TRUE);   // Get special keys too (Fn, arrows, etc.)
  mousemask(ALL_MOUSE_EVENTS, nullptr);

  start_color();
  init_color(COLOR_PINK, 976, 375, 554);
  init_color(COLOR_LINE_SHADE, 250, 250, 250);
  init_pair(COLOR_PAIR_LINENUM, COLOR_PINK, COLOR_BLACK);
  init_pair(COLOR_PAIR_LINENUM_SHADED, COLOR_PINK, COLOR_LINE_SHADE);
  init_pair(COLOR_PAIR_LINE_SHADED, COLOR_WHITE, COLOR_LINE_SHADE);
  init_pair(COLOR_PAIR_ERROR, COLOR_BLACK, COLOR_RED);

  regenerate_screen();
  set_cursor();

  struct sigaction deed = {0};
  deed.sa_handler = handle_sigwinch;
  sigaction(SIGWINCH, &deed, NULL);
  deed.sa_handler = handle_sigcont;
  sigaction(SIGCONT, &deed, NULL);

  while (1) {
    int c = wgetch(stdscr);
    if (window_resized) {
      regenerate_screen();
    }

    if (c >= ' ' && c <= '~') {  // all printable chars
      putc(c, cy, cx);
      cx++;
      preferred_cx = cx;
    } else if (c == KEY_UP) {
      //scroll_file(-1);
      cy--;
      if (cy < 0) cy = 0;
      if (preferred_cx > file_lines[cy].size) {
        cx = file_lines[cy].size;
      } else {
        cx = preferred_cx;
      }
      scroll_to_cursor();
    } else if (c == KEY_DOWN) {
      //scroll_file(1);
      cy++;
      if (cy >= file_lines.size()) cy = file_lines.size() - 1;
      if (preferred_cx > file_lines[cy].size) {
        cx = file_lines[cy].size;
      } else {
        cx = preferred_cx;
      }
      scroll_to_cursor();
    } else if (c == KEY_LEFT) {
      if (cx > 0) {
        cx--;
      } else if (cy > 0) {
        cy--;
        cx = file_lines[cy].size;
      }
      preferred_cx = cx;
      scroll_to_cursor();
    } else if (c == KEY_RIGHT) {
      if (cx < file_lines[cy].size) {
        cx++;
      } else if (cy < file_lines.size() - 1) {
        cy++;
        cx = 0;
      }
      preferred_cx = cx;
      scroll_to_cursor();
    } else if (c == KEY_CTRL_LEFT) {
      preferred_cx = cx;
      printcl(1, "go to previous token");
    } else if (c == KEY_CTRL_RIGHT) {
      preferred_cx = cx;
      printcl(1, "go to next token");
    } else if (c == KEY_HOME) {
      cx = 0;
      preferred_cx = cx;
    } else if (c == KEY_END) {
      cx = file_lines[cy].size;
      preferred_cx = cx;
    } else if (c == KEY_NPAGE) {
      scroll_file(4);
    } else if (c == KEY_PPAGE) {
      scroll_file(-4);
    } else if (c == CTRL('q')) {
      if (!dirty || exitdialog()) {
        endwin();
        return 0;
      }
    } else if (c == CTRL('S')) {
      if (savedialog(filePath)) {
        save(filePath);
      }
    } else if (c == KEY_RESIZE) {
      printcl(0, "[ Cols: %d Rows : %d ]", COLS, LINES);
      regenerate_screen();
    } else if (c == KEY_F(12)) {
      endwin();
      raise(SIGSTOP);
      regenerate_screen();
        } else if (c == KEY_BACKSPACE) {
      if (cx > 0) {
        removec(cy, cx - 1);
        cx--;
      } else if (cy > 0) {
        cx = file_lines[cy - 1].size;
        combine_lines(cy - 1, cy);
        cy--;
      }
      preferred_cx = cx;
    } else if (c == KEY_DC) {
      if (cx < file_lines[cy].size) {
        removec(cy, cx);
      } else if (cy < file_lines.size() - 1) {
        combine_lines(cy, cy + 1);
      }
      preferred_cx = cx;
    } else if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      putnl(cy, cx);
      cy++;
      cx = 0;
      preferred_cx = cx;
    } else if (c == KEY_MOUSE) {
      MEVENT event;
      if (getmouse(&event) == OK) {
        printcl(1, "mouse: x=%d y=%d z=%d bstate=0x%08x", event.x, event.y, event.z, (uint32_t)event.bstate);

        if (BUTTON_PRESS(event.bstate, 1)) {
          screen_to_file(event.y, event.x, cy, cx);
          if (cy >= file_lines.size()) cy = file_lines.size() - 1;
          LineMeta line = file_lines[cy];
          if (cx > line.size) cx = line.size;
          preferred_cx = cx;
        } else if (MOUSE_SCROLL_UP(event.bstate)) {
          scroll_file(-4);
        } else if (MOUSE_SCROLL_DN(event.bstate)) {
          scroll_file(4);
        }
      }
    } else {
      printcl(0, "wgetch=%d", c);
    }
    update_screen();
    set_cursor();
  }
}
