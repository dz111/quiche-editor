#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

#include <ncurses.h>
#include <signal.h>

namespace fs = std::filesystem;
using namespace std::string_literals;

volatile sig_atomic_t window_resized = false;

std::fstream file;
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
  file.clear();
  file.seekg(std::ios_base::beg);

  // advance to first displayed line
  int lines_to_skip = first_line - 1;
  int skipped_lines = 0;
  while (skipped_lines < lines_to_skip) {
    char c = file.get();
    while (c != '\r' && c != '\n') {
      c = file.get();
    }
    if (c == '\r') {
      if (file.peek() == '\n') {
        c = file.get();
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
    if (x >= cols) {
      char c = file.get();
      while (c != '\r' && c != '\n') {
        c = file.get();
      }
      if (c == '\r') {
        if (file.peek() == '\n') {
          c = file.get();
        }
      }
      attron(A_REVERSE);
      mvaddch(y, COLS-2, '$');
      attroff(A_REVERSE);
      addch('\n');
    } else {
      addch(file.get());
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

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Usage: qe <filename>\n");
    return -1;
  }

  fs::path filePath = argv[1];
  if (!fs::exists(filePath)) {
    printf("File '%s' does not exist.\n", filePath.string().c_str());
    return -1;
  }

  file = std::fstream(filePath, std::ios::in | std::ios::out);
  if (!file.is_open()) {
    printf("Could not open file '%s' for editing.\n", filePath.string().c_str());
    return -1;
  }

  fileName = filePath.filename();

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
      first_line--;
      if (first_line < 1) first_line = 1;
      display_file();
      move(LINES - 1, 0);
      clrtoeol();
      printw("up");
    } else if (c == KEY_DOWN) {
      first_line++;
      display_file();
      move(LINES - 1, 0);
      clrtoeol();
      printw("down");
    } else {
      move(LINES - 1, 0);
      clrtoeol();
      printw("%d %c", c, c);
    }

    if (c == 'q') break;
  }
  endwin();
  //printf("Last: %d", (unsigned int)a);
  return 0;
}
