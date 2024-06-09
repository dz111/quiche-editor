#include <iostream>
#include <fstream>
#include <string>
#include <tuple>

#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <signal.h>

volatile sig_atomic_t window_resized = false;

FILE* file = nullptr;
std::string fileName;
char* editBuffer = nullptr;
char* editBufferReadHead = nullptr;
char* editBufferEnd = nullptr;
unsigned int editBufferCapacity = 0;

int first_line = 1;
int line_num_length = 0;
int cx = 1;
int cy = 1;

#define COLOR_PAIR_LINENUM 1
#define COLOR_PINK 101

#define CTRL(x) ((x) & 0x1f)

void handle_sigwinch(int signal) {
  window_resized = true;
}

void handle_sigcont(int signal) {
  window_resized = true;
}

char getc() {
  if (editBufferReadHead < editBufferEnd - 1) return *editBufferReadHead++;
  else return -1;
}

char peek() {
  if (editBufferReadHead < editBufferEnd - 1) return *editBufferReadHead;
  else return -1;
}

void reset_read() {
  editBufferReadHead = editBuffer;
}

void resize_array(char** array, char** array_end, unsigned int* array_capacity) {
}

void array_insert(char* array, char* array_end, unsigned int array_capacity, unsigned int index, char entry) {
  if ((array_end - array + 1) > array_capacity) {
    resize_array(&array, &array_end, &array_capacity);
  }
  array_end++;
  char* writeHead = array + index;
  char c = *writeHead;
  *writeHead++ = entry;
  while (writeHead < array_end) {
    char last_c = *writeHead;
    *writeHead++ = c;
    c = last_c;
  }
}

void putc(char c, unsigned int line, unsigned int col) {
  // convert 1-indexed to 0-indexed
  line--;
  col--;
  char* writeHead = editBuffer;
  unsigned int skipped_lines = 0;
  while (skipped_lines < line) {
    char c = *writeHead++;
    while (c != '\r' && c != '\n' && writeHead < editBufferEnd) {
      c = *writeHead++;
    }
    if (writeHead >= editBufferEnd) break;
    if (c == '\r') {
      if (*writeHead == '\n') {
        ++writeHead;
      }
    }
    skipped_lines++;
  }
  writeHead += col;
  array_insert(editBuffer, editBufferEnd, editBufferCapacity, (writeHead - editBuffer), c);
}

void init_buffer(unsigned int size, unsigned int fileSize) {
  editBuffer = new char[size];
  editBufferReadHead = editBuffer;
  editBufferEnd = editBuffer + fileSize;
}

void display_file() {
  int last_line = LINES - 3 + first_line;
  line_num_length = 0;
  for (int k = last_line - 1; k > 0; k /= 10) {
    line_num_length++;
  }

  reset_read();

  // advance to first displayed line
  int lines_to_skip = first_line - 1;
  int skipped_lines = 0;
  while (skipped_lines < lines_to_skip) {
    char c = getc();
    while (c != '\r' && c != '\n' && c != -1) {
      c = getc();
    }
    if (c == -1) break;
    if (c == '\r') {
      if (peek() == '\n') {
        c = getc();
      }
    }
    skipped_lines++;
  }

  int rows = LINES - 2; // leave room for status bar, command bar
  int cols = COLS - 1;
  int x_offset = line_num_length + 1;

  // clear text canvas
  for (int y = 1; y < rows; y++) {
    move(y, 0);
    clrtoeol();
  }

  // file contents
  int x = 0;
  for (int y = 1; y < rows;) {
    if (x < x_offset) {
      move(y, x_offset);
    }
    char c = getc();
    if (x >= cols) {
      while (c != '\r' && c != '\n' && c != -1) {
        c = getc();
      }
      if (c == -1) {
        last_line = first_line + y;
        break;
      }
      if (c == '\r') {
        if (peek() == '\n') {
          c = getc();
        }
      }
      attron(A_REVERSE);
      mvaddch(y, COLS-2, '$');
      attroff(A_REVERSE);
      addch('\n');
    } else {
      if (c == -1) {
        last_line = first_line + y;
        break;
      }
      if (c == '\r') {
        if (peek() == '\n') {
          c = getc();
        }
        addch('\n');
      } else {
        addch(c);
      }
    }
    getyx(stdscr, y, x);
  }

  // line numbers
  for (int y = 1, line = first_line; y < rows && line < last_line; y++, line++) {
    move(y, 0);
    attron(COLOR_PAIR(COLOR_PAIR_LINENUM));
    printw("%*d", line_num_length, line);
    attroff(COLOR_PAIR(COLOR_PAIR_LINENUM));
  }
}

void regenerate_screen() {
  window_resized = false;
  endwin();

  // Title
  int col = (COLS - fileName.size()) / 2;

  attron(A_REVERSE);
  move(0, 0);
  int x, y;
  for (x = 0; x < col; x++) {
    addch(' ');
  }
  mvprintw(0, col, fileName.c_str());
  getyx(stdscr, y, x);
  for (; x < COLS; x++) {
    addch(' ');
  }
  attroff(A_REVERSE);

  display_file();

  move(LINES - 2, 0);
  clrtoeol();
  mvprintw(LINES - 2, COLS / 2 - 10, "[ Cols: %d Rows: %d ]\n", COLS, LINES);
  clrtoeol();
  printw("  ");
  attron(A_REVERSE);
  printw("^X");
  attroff(A_REVERSE);
  printw(" Exit");
  refresh();
}

void scroll_file(int lines) {
  first_line += lines;
  if (first_line < 1) first_line = 1;
  // also clamp bottom too
  display_file();
}

void set_cursor() {
  move(cy, cx + line_num_length);
}

template<typename... Args>
void printcl(const char* fmt, Args&&... args) {
  //move(LINES - 1, 0);
  //clrtoeol();
  //printw(fmt, args...);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: qe <filename>\n");
    return -1;
  }

  std::string sFilePath = argv[1];
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

  unsigned int buffer_size = 8196;  // start with 8K
  while (buffer_size < fileSize) {
    if (buffer_size > 1048576) {  // lets limit ourselves to 1 MB for now...
      fprintf(stderr, "File exceeds 1 MB.\n");
      return -1;
    }
    buffer_size *= 2;
  }
  init_buffer(buffer_size, fileSize);
  fread(editBuffer, 1, fileSize, file);

  // get filename
  {
    const char* start = cFilePath;
    const char* end = start;
    while (*end != 0) end++;  // advance to end of string
    const char* fstart = end;
    while (fstart > start && *fstart != '/' && *fstart != '\\') fstart--;
    if (fstart > start) fstart++;
    fileName = fstart;
  }

  initscr();  // Start curses
  raw();      // Disable line buffering so we get inputs asap
  nonl();     // No new lines
  noecho();   // No echo
  keypad(stdscr, TRUE);   // Get special keys too (Fn, arrows, etc.)

  start_color();
  init_color(COLOR_PINK, 976, 375, 554);
  init_pair(COLOR_PAIR_LINENUM, COLOR_PINK, COLOR_BLACK);

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

    if (c == KEY_RESIZE) {
      regenerate_screen();
    } else if (c == KEY_UP) {
      //scroll_file(-1);
      cy--;
      if (cy < 1) cy = 1;
      printcl("up");
    } else if (c == KEY_DOWN) {
      //scroll_file(1);
      cy++;
      printcl("down");
    } else if (c == KEY_LEFT) {
      cx--;
      if (cx < 1) cx = 1;
      printcl("left");
    } else if (c == KEY_RIGHT) {
      cx++;
      printcl("right");
    } else if (c == KEY_NPAGE) {
      scroll_file(4);
      display_file();
      printcl("pg down");
    } else if (c == KEY_PPAGE) {
      scroll_file(-4);
      printcl("pg up");
    } else if (c == CTRL('x')) {
      break;
    //} else if (c == CTRL('b')) {
    //  raise(SIGTRAP);
    } else if (c == CTRL('z')) {
      endwin();
      raise(SIGSTOP);
    } else if (c >= ' ' && c <= '~') {  // all printable chars
      putc(c, cy, cx);
      cx++;
      display_file();
    }
    set_cursor();
  }
  endwin();
  return 0;
}
