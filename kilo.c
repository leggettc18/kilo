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

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k)&0x1f)
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_DOWN,
    ARROW_UP,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow
{
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig
{
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    // stores original terminal state
    struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

/*** terminal ***/

/* Function: die
 * ------------------------------------------------------
 * Prints an error and kills the program
 * 
 * *s: pointer to the error message to print
*/
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

/* Function: disableRawMode
 * ------------------------------------------------------
 * Disables the raw mode by restoring the original state
 */
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/* Function: enableRawMode
 * ------------------------------------------------------
 * enables raw mode for the terminal by setting the
 * appropriate terminal flags.
 */
void enableRawMode()
{
    // gets the original terminal state.
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/* Function: editorReadKey
 * -----------------------------------------------------
 * Reads a key from the standard input
 * 
 * returns: the character read from the standard input.
*/
int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

/* Function: getCursorPosition
 * ---------------------------------------------------------
 * Gets the current cursor position on the screen.
 *
 * *rows and *cols: pointers to variables to store the row
 * and column that the cursor is currently positioned at.
 *
 * returns: a status code indicating success (0) or failure (non-zero)
*/
int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

/* Function: getWindowSize
 * ---------------------------------------------------------
 * Simply enough, returns the window size of the terminal.
 *
 * *rows and *cols: pointers to variables that will store the
 * number of row and columns.
 *
 * returns: a status code indicating success (0) or failure (non-zero)
*/
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

/* Function: editorUpdateRow
 * -------------------------------------------------------
 * Renders an erow so that non-printable characters can be
 * processed.
 * 
 * row: pointer to an erow instance
*/
void editorUpdateRow(erow *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            ;
            while (idx % KILO_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

/*  Function: editorAppendRow
 * -----------------------------------------------------------
 * Appends a row to the end of the editor's buffer, allocating
 * memory accordingly.
 * 
 * s: string contents of the row to append.
 * len: the length of the string to append.
*/
void editorAppendRow(char *s, size_t len)
{
    // adds an additional row to the array of rows
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // gets index of the new row
    int at = E.numrows;
    // sets the size of the new row
    E.row[at].size = len;
    // allocates memory for the new row's string
    E.row[at].chars = malloc(len + 1);
    // copies the string into the new row
    memcpy(E.row[at].chars, s, len);
    // adds the null terminator string
    E.row[at].chars[len] = '\0';
    // initializes rsize and render values for the new erow
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    // Renders the row so any non-printable characters can be displayed.
    editorUpdateRow(&E.row[at]);
    // increases the numrows counter
    E.numrows++;
    E.dirty++;
}

/* Function: editorFreeRow
 * ------------------------------------------------------------
 * Frees an erow instance's internal data from memory
 * 
 * row: pointer to an erow instance.
*/
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
}

/* Function: editorDelRow
 * -------------------------------------------------------------
 * Deletes a row from the editor buffer
 * 
 * at: the index of the row to delete from the row array.
*/
void editorDelRow(int at) {
    // Return if index is not valid
    if (at < 0 || at >= E.numrows) return;
    // Free the row's data from memory
    editorFreeRow(&E.row[at]);
    // Overwrites the row with the remaining rows' data.
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    // Decrement the number of rows
    E.numrows--;
    // Set modified state.
    E.dirty++;
}

/* Function: editorRowInsertChar
 * ----------------------------------------------------------------------
 * Inserts a character at the given position of the given row
 * 
 * row: pointer to erow instance
 * at: position in the row to insert the character
 * c: character to insert (in the form of an integer)
*/
void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

/* Functin: editorRowAppendString
 * ------------------------------------------------------------------
 * Appends a string to an existing row (i.e. when deleting the next row)
 * 
 * row: pointer to the row you are appending to.
 * s: the string to append.
 * len: the length of the string to append.
*/
void editorRowAppendString(erow *row, char *s, size_t len) {
    // Allocates more space for the string to append
    row->chars = realloc(row->chars, row->size + len + 1);
    // Copies string to end of existing string
    memcpy(&row->chars[row->size], s, len);
    // Updates the row size
    row->size += len;
    // Appends the null terminator
    row->chars[row->size] = '\0';
    // Updates the row visually
    editorUpdateRow(row);
    // Sets modified state.
    E.dirty++;
}

/* Function: editorRowDelChar
 * ------------------------------------------------------------------
 * Deletes the character at the given position from the editor's buffer
 * 
 * row: pointer to the row instance the cursor is in
 * at: index of the character to delete.
*/
void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

/* Function: editorInsertChar
 * ----------------------------------------------------------------------
 * Inserts the given character at the cursor location
 * 
 * c: the int code of the character to insert.
*/
void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/* Function: editorDelChar
 * --------------------------------------------------------------
 * Deletes the character to the left of the cursor, if there is one
*/
void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        // Move the cursor to the end of the previous line (except down one row)
        E.cx = E.row[E.cy - 1].size;
        // Append the current row's string to hte previous row
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        // Delete the current row
        editorDelRow(E.cy);
        // Move the cursor up to the previous row.
        E.cy--;
    }
}

/*** file i/o ***/

/* Function: editorRowsToString
 * ----------------------------------------------------------------
 * Takes the current editor buffer and writes it out as a string.
 * 
 * buflen: pointer to an int that will be used to store the buffer
 * length, which is calculated in this function.
 * 
 * returns: a string of the entire editor buffer
*/
char *editorRowsToString(int *buflen)
{
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

/* Function: editorOpen
 * -------------------------------------------------------
 * Opens a file into the text edtior buffer.
 *
 * filename: string representing the name or path to a file.
*/
void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

/* Function: editorSave
 * -------------------------------------------------------------------
 * Saves the current buffer of the editor to a file
*/
void editorSave()
{
    if (E.filename == NULL)
        return;

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1)
    {
        if (ftruncate(fd, len) != -1)
        {
            if (write(fd, buf, len) == len)
            {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** input ***/

/* Function: editorMoveCursor
 * -----------------------------------------------------
 * Changes the cursor position in the global editor state
 * according to the provided key
 *
 * key: a character representing a key that was pressed.
*/
void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
        {
            E.cy++;
        }
        break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

/* Function: editorProcessKeypress
 * -----------------------------------------------------
 * Reads a keypress and performs an editor function accordingly
*/
void editorProcessKeypress()
{
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c)
    {
    case '\r':
        /*TODO*/
        break;
    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) {
            editorSetStatusMessage("WARNING!! File has unsaved changes. "
                "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    case CTRL_KEY('s'):
        editorSave();
        break;
    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
        {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
        {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    }

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    case CTRL_KEY('l'):
    case '\x1b':
        break;
    default:
        editorInsertChar(c);
        break;
    }
    quit_times = KILO_QUIT_TIMES;
}

/*** append buffer ***/
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

/* Function: abAppend
 * ---------------------------------------------------------
 * Appends a string to an append buffer and appropriately
 * allocates memory for it accordingly.
 *
 * ab: a pointer to an append buffer instance
 * s: a string to append to the buffer
 * len: the length of the new string
*/
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/* Function: abFree
 * ---------------------------------------------------------
 * The destructor for our abuf type. Frees the memory in use
 * by the given abuf
 *
 * ab: a pointer to an append buffer instance
*/
void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** output ***/

/* Function: editorScroll
 * -----------------------------------------------------------
 * Checks for if the cursor is off the screen and sets the row
 * offset accordingly.
*/
void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

/* Function: editorDrawRows
 * ----------------------------------------------------------
 * Draws a tilde on any unpopulated rows on the screen.
 *
 * ab: pointer to an abuf instance.
*/
void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);

        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[NO Name]", E.numrows,
                       E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numrows);
    if (len > E.screencols)
        len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

/* Function: editorRefreshScreen
 * ----------------------------------------------------------
 * Refreshes the screen with the editor's output
*/
void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
             (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* Function: editorSetStatusMessage
 * -----------------------------------------------------------------
 * Sets the editor status message
 * 
 * fmt: the format string for the status message
 * ...: variables to be placed into the format string.
*/
void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** init ***/

/* Function: initEditor
 * ----------------------------------------------------
 * Initializes the global editor state with things like
 * the screen size
*/
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
