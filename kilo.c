/*
 * Kilo
 * ------------------------------------------------
 * Author: Christopher Leggett <chris@leggett.dev>
 * ------------------------------------------------
 * A small pet text editor I wrote to flex my
 * C coding skills in the event I ever need them
 * in the future. 
*/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>

// stores original terminal state
struct termios orig_termios;

/* Function: disableRawMode
 * ------------------------------------------------------
 * Disables the raw mode by restoring the original state
 */
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Function: enableRawMode
 * ------------------------------------------------------
 * enables raw mode for the terminal by setting the
 * appropriate terminal flags.
 */
void enableRawMode() {
    // gets the original terminal state.
    tcgetattr(STDIN_FILENO, &orig_termios);
    // sets up restoration of that state on exit.
    atexit(disableRawMode);

    // sets necessary flags to enable raw mode.
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        if(iscntrl(c)) {
            printf("%d\n", c);
        } else {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}
