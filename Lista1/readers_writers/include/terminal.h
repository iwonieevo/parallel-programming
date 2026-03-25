#pragma once
#ifndef PRR_TERMINAL_H
#define PRR_TERMINAL_H

#include <termios.h>
#include <unistd.h>

struct RawTerminal {
    termios original;

    RawTerminal() {
        tcgetattr(STDIN_FILENO, &original);
        termios raw = original;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    ~RawTerminal() {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
    }
};
#endif //PRR_TERMINAL_H
