/*
 * Kilo
 * ------------------------------------------------
 * Author: Christopher Leggett <chris@leggett.dev>
 * ------------------------------------------------
 * A small pet text editor I wrote to flex my
 * C coding skills in the event I ever need them
 * in the future. 
*/

/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termio.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct editorConfig{
    int screenrows;
    int screencols;
    // stores original terminal state
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/* Function: die
 * ------------------------------------------------------
 * Prints an error and kills the program
 * 
 * *s: pointer to the error message to print
*/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

/* Function: disableRawMode
 * ------------------------------------------------------
 * Disables the raw mode by restoring the original state
 */
void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/* Function: enableRawMode
 * ------------------------------------------------------
 * enables raw mode for the terminal by setting the
 * appropriate terminal flags.
 */
void enableRawMode() {
    // gets the original terminal state.
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    // sets up restoration of that state on exit.
    atexit(disableRawMode);

    // sets necessary flags to enable raw mode.
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

}

/* Function: editorReadKey
 * -----------------------------------------------------
 * Reads a key from the standard input
 * 
 * returns: the character read from the standard input.
*/
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/* Function: getCursorPosition
** ---------------------------------------------------------
** Gets the current cursor position on the screen.
**
** *rows and *cols: pointers to variables to store the row
** and column that the cursor is currently positioned at.
**
** returns: a status code indicating success (0) or failure (non-zero)
*/
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) -1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

/* Function: getWindowSize
** ---------------------------------------------------------
** Simply enough, returns the window size of the terminal.
**
** *rows and *cols: pointers to variables that will store the
** number of row and columns.
**
** returns: a status code indicating success (0) or failure (non-zero)
*/
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/*** input ***/

/* Function: editorProcessKeypress
** -----------------------------------------------------
** Reads a keypress and performs an editor function accordingly
*/
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/* Function: abAppend
** ---------------------------------------------------------
** Appends a string to an append buffer and appropriately
** allocates memory for it accordingly.
**
** ab: a pointer to an append buffer instance
** s: a string to append to the buffer
** len: the length of the new string
*/
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/* Function: abFree
** ---------------------------------------------------------
** The destructor for our abuf type. Frees the memory in use
** by the given abuf
**
** ab: a pointer to an append buffer instance
*/
void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

/* Function: editorDrawRows
** ----------------------------------------------------------
** Draws a tilde on any unpopulated rows on the screen.
**
** ab: pointer to an abuf instance.
*/
void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        abAppend(ab, "~", 1);

        if(y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

/* Function: editorRefreshScreen
** ----------------------------------------------------------
** Refreshes the screen with the editor's output
*/
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);
    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** init ***/

/* Function: initEditor
** ----------------------------------------------------
** Initializes the global editor state with things like
** the screen size
*/
void initEditor() {
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
