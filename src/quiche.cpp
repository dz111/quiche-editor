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
    bool delete_old_buffer = (capacity > 0);
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

int first_line = 1;
int left_margin = 0;
int cx = 1, cy = 1;

time_t cl_message_time = 0;
std::string cl_message;
int cl_message_level = 0;

enum {
  COLOR_PAIR_LINENUM = 1,
  COLOR_PAIR_ERROR,
  COLOR_PINK = 101,
};

#define CTRL(x) ((x) & 0x1f)

void handle_sigwinch(int signal) {
  window_resized = true;
}

void handle_sigcont(int signal) {
  window_resized = true;
}

void putc(char c, unsigned int line, unsigned int col) {
  // convert 1-indexed to 0-indexed
  line--;
  col--;
  assert(line < file_lines.size());
  LineMeta& line_meta = file_lines[line];
  assert(col < line_meta.size + 1);
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
}

void display_file() {
  int last_line = LINES - 3 + first_line;
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
  int rows = std::min((uint64_t)LINES - 2, file_lines.size() - first_line + 1);
  int cols = COLS - left_margin;

  for (int i = 0; i < rows; i++) {
    int line_num = first_line + i;

    move(i, 0);
    attron(COLOR_PAIR(COLOR_PAIR_LINENUM));
    printw("%*d", line_num_length, line_num);
    attroff(COLOR_PAIR(COLOR_PAIR_LINENUM));
    addch(' ');

    LineMeta& line_meta = file_lines[line_num - 1];
    int chars_to_put = line_meta.size;
    bool char_overflow = false;
    if (chars_to_put > cols) {
      chars_to_put = cols - 1;
      char_overflow = true;
    }
    for (int j = 0; j < chars_to_put; j++) {
      addch(line_meta.start[j]);
    }
    if (char_overflow) {
      attron(A_REVERSE);
      addch('$');
      attroff(A_REVERSE);
    }
  }
}

void render_status() {
  move(LINES - 2, 0);
  attron(A_REVERSE);
  printw(filePath.c_str());
  printw(" (%d:%d)", cy, cx);
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
  if (first_line < 1) first_line = 1;
  // also clamp bottom too
}

void set_cursor() {
  move(cy + 0 - first_line, cx + left_margin - 1);
}

void save(const std::string& savePath) {
  FILE* saveFile = fopen(savePath.c_str(), "w");
  for (LineMeta& line_meta : file_lines) {
    fwrite(line_meta.start, 1, line_meta.size, saveFile);
    fputc('\n', saveFile);
  }
  fclose(saveFile);
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
  cl_message_level = 0;
  strprintf(cl_message, fmt, args...);
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
    while (c != '\r' && c != '\n' && cp < fileBufferEnd) {
      line.size++;
      c = *cp++;
    }
    if (cp >= fileBufferEnd) break;
    if (c == '\r') {
      if (*cp == '\n') {
        c = *cp++;
      }
    }

    file_lines.push_back(line);
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
  keypad(stdscr, TRUE);   // Get special keys too (Fn, arrows, etc.)

  start_color();
  init_color(COLOR_PINK, 976, 375, 554);
  init_pair(COLOR_PAIR_LINENUM, COLOR_PINK, COLOR_BLACK);
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
      dirty = true;
    } else if (c == KEY_UP) {
      //scroll_file(-1);
      cy--;
      if (cy < 1) cy = 1;
    } else if (c == KEY_DOWN) {
      //scroll_file(1);
      cy++;
    } else if (c == KEY_LEFT) {
      cx--;
      if (cx < 1) cx = 1;
    } else if (c == KEY_RIGHT) {
      cx++;
    } else if (c == KEY_NPAGE) {
      scroll_file(4);
      cy += 4;
    } else if (c == KEY_PPAGE) {
      scroll_file(-4);
      cy -= 4;
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
    }
    update_screen();
    set_cursor();
  }
}
