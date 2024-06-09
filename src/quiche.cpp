#include <iostream>
#include <fstream>
#include <string>

#include <ncurses.h>
#include <signal.h>
#include <stdio.h>

volatile sig_atomic_t window_resized = false;

FILE* file = nullptr;
std::string fileName;

int first_line = 1;

#define COLOR_PAIR_LINENUM 1
#define COLOR_PINK 101

void handle_sigwinch(int signal) {
  window_resized = true;
}

void display_file() {
  int last_line = LINES - 3 + first_line;
  int line_no_length = 0;
  for (int k = last_line - 1; k > 0; k /= 10) {
    line_no_length++;
  }

  // reset file
  rewind(file);

  // advance to first displayed line
  int lines_to_skip = first_line - 1;
  int skipped_lines = 0;
  while (skipped_lines < lines_to_skip) {
    char c = fgetc(file);
    while (c != '\r' && c != '\n' && c != 0) {
      c = fgetc(file);
    }
    if (c == 0) break;
    if (c == '\r') {
      c = fgetc(file);
      if (c != '\n') {
        ungetc(c, file);
      }
    }
    skipped_lines++;
  }

  int rows = LINES - 2; // leave room for status bar, command bar
  int cols = COLS - 1 - line_no_length - 1;  // always sub 1, then make room for line numbers and space
  int x_offset = line_no_length + 1;

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
    char c = fgetc(file);
    if (x >= cols) {
      while (c != '\r' && c != '\n' && c != 0) {
        c = fgetc(file);
      }
      if (c == 0) break;
      if (c == '\r') {
        c = fgetc(file);
        if (c != '\n') {
          ungetc(c, file);
        }
      }
      attron(A_REVERSE);
      mvaddch(y, COLS-2, '$');
      attroff(A_REVERSE);
      addch('\n');
    } else {
      if (c == 0) break;
      addch(c);
    }
    getyx(stdscr, y, x);
  }

  // line numbers
  int line = first_line;
  for (int y = 1; y < rows; y++) {
    move(y, 0);
    attron(COLOR_PAIR(COLOR_PAIR_LINENUM));
    printw("%*d", line_no_length, line);
    attroff(COLOR_PAIR(COLOR_PAIR_LINENUM));
    line++;
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

  mvprintw(LINES - 2, 0, "Cols %d Rows %d\n", COLS, LINES);
  move(LINES - 1, 0);
  refresh();
}

void scroll_file(int lines) {
  first_line += lines;
  if (first_line < 1) first_line = 1;
  // also clamp bottom too
  display_file();
}

template<typename... Args>
void printcl(const char* fmt, Args&&... args) {
  move(LINES - 1, 0);
  clrtoeol();
  printw(fmt, args...);
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

  struct sigaction deed = {0};
  deed.sa_handler = handle_sigwinch;
  sigaction(SIGWINCH, &deed, NULL);

  while (1) {
    int c = wgetch(stdscr);
    if (window_resized) {
      regenerate_screen();
    }

    if (c == KEY_UP) {
      scroll_file(-1);
      printcl("up");
    } else if (c == KEY_DOWN) {
      scroll_file(1);
      printcl("down");
    } else if (c == KEY_NPAGE) {
      scroll_file(4);
      display_file();
      printcl("pg down");
    } else if (c == KEY_PPAGE) {
      scroll_file(-4);
      printcl("pg down");
    } else if (c == 'q') {
      break;
    } else {
      printcl("%d %c", c, c);
    }
  }
  endwin();
  return 0;
}
