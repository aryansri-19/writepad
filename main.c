/*Include headers*/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>

/*Defines*/
#define CTRL_KEY(k) ((k) & 0x1f)
#define WRITEPAD_VERSION "0.0.1"
#define TAB_STOP 8

enum keys {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DELETE,
	HOME,
	END,
	PAGE_UP,
	PAGE_DOWN,
};

/*Data*/
typedef struct wrow {
	int size;
	int rSize;
	char *characters;
	char *render;
} wrow;

struct editorConfig {
	int cursorX, cursorY;
	int renderX;
	int rowOffset, columnOffset;
	int screenRows, screenColumns;
	int numRows;
	wrow *row;
	char* file;
	char statusMessage[80];
	time_t statusMessage_time;
	struct termios orig_termios;
};
struct editorConfig E;

/*Terminal settings*/
void die(const char* s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}
void enableRawMode() {
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;

	raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
	int eread;
	char c;
	while((eread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(eread == -1 && errno != EAGAIN) die("read");
	}

	if(c == '\x1b') {
		char seq[3];

		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if(seq[0] == '[') {
			if(seq[1] >= '0' && seq[1] <= '9') {
				if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if(seq[2] == '~') {
					switch(seq[1]) {
						case '1': return HOME;
						case '3': return DELETE;
						case '4': return END;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME;
						case '8': return END;
					}
				}
			} else {
				switch(seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME;
					case 'F': return END;
				}
			}
		} else if(seq[0] == 'O') {
			switch(seq[1]) {
				case 'H': return HOME;
				case 'F': return END;
			}
		}
		return '\x1b';
	} else {
		return c;
	}
}

int getCursorPosition(int *rows, int* columns) {
	char buf[32];
	unsigned int i = 0;
	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while(i<sizeof(buf) -1) {
		if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if(buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	if(buf[0] != '\x1b' || buf[1] != '[') return -1;
	if(sscanf(&buf[2], "%d;%d", rows, columns) != 2) return -1;
	return 0;
}

int getWindowSize(int *rows, int* columns) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, columns);
	} else {
		*columns = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*Row operations*/

int editorRowCursorXToRenderX(wrow *row, int cursorX) {
	int renderX = 0;
	int i;
	for(i=0;i<cursorX;i++) {
		if(row->characters[i] == '\t')
			renderX += (TAB_STOP -1) + (renderX % TAB_STOP);
		renderX++;
	}
	return renderX;
}
void editorUpdateRows(wrow *row) {
	int tabs = 0;
	int j;
	for(j=0;j<row->size;j++) 
		if(row->characters[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

	int index = 0;
	for(j=0;j<row->size;j++) {
		if(row->characters[j] == '\t') {
			row->render[index++] = ' ';
			while(index % TAB_STOP != 0) row->render[index++] = ' ';
		} else {
			row->render[index++] = row->characters[j];
		}
	}
	row->render[index] = '\0';
	row->rSize = index;
}

void editorAppendRows(char *s, size_t length) {

	E.row = realloc(E.row, sizeof(wrow) * (E.numRows + 1));

	int i = E.numRows;
	E.row[i].size = length;
	E.row[i].characters = malloc(length + 1);
	memcpy(E.row[i].characters, s, length);
	E.row[i].characters[length] = '\0';

	E.row[i].rSize = 0;
	E.row[i].render = NULL;
	editorUpdateRows(&E.row[i]);
	E.numRows++;
}
/*I/O*/
void editorOpen(char *filename) {
	free(E.file);
	E.file = strdup(filename);
	FILE *fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char *line = NULL;
	size_t lineCap = 0;
	ssize_t linelength;
	while ((linelength = getline(&line, &lineCap, fp)) != -1) {
		while (linelength > 0 && (line[linelength - 1] == '\n' || line[linelength - 1] == '\r'))
			linelength--;
		editorAppendRows(line, linelength);
  	}
	free(line);
	fclose(fp);
}
/*Global buffer*/
struct append_buffer {
	char *b;
	int len;
};

#define APPEND_BUFFER_INIT {NULL, 0}

void aBufferAppend(struct append_buffer *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void aBufferFree(struct append_buffer *ab) {
	free(ab->b);
}

/*Input*/

void editorMoveCursor(int key) {
	wrow *row = (E.cursorY >= E.numRows) ? NULL : &E.row[E.cursorY];
	switch(key) {
		case ARROW_LEFT:
			if(E.cursorX != 0){
				E.cursorX--;
			} else if(E.cursorY > 0) {
				E.cursorY--;
				E.cursorX = E.row[E.cursorY].size;
			}
			break;
		case ARROW_RIGHT:
			if(row && E.cursorX < row->size) {
				E.cursorX++;
			} else if(row && E.cursorX == row->size) {
				E.cursorY++;
				E.cursorX = 0;
			}
			break;
		case ARROW_UP:
			if(E.cursorY != 0) {
				E.cursorY--;
			}
			break;
		case ARROW_DOWN:
			if(E.cursorY < E.numRows) {
				E.cursorY++;
			}
			break;
	}
	row = (E.cursorY >= E.numRows) ? NULL : &E.row[E.cursorY];
	int rowlength = row ? row->size : 0;
	if(E.cursorX > rowlength) {
		E.cursorX = rowlength;
	}
}
void editorProcessKeyPress() {
	int c = editorReadKey();
	switch(c) {
		case CTRL_KEY('x'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		
		case HOME:
			E.cursorX = 0;
			break;
		
		case END:
			if(E.cursorY < E.numRows)
				E.cursorX = E.row[E.cursorY].size;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if(c == PAGE_UP)
					E.cursorY = E.rowOffset;
				else if(c == PAGE_DOWN) {
					E.cursorY = E.rowOffset + E.screenRows - 1;
					if(E.cursorY > E.numRows) E.cursorY = E.numRows;
				}
				int times = E.screenRows;
				while(times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		
		case ARROW_UP:
		case ARROW_LEFT:
		case ARROW_DOWN:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*Output*/

void editorScroll() {
	E.renderX = 0;

	if(E.cursorY < E.numRows) {
		E.renderX = editorRowCursorXToRenderX(&E.row[E.cursorY], E.cursorX);
	}

	if(E.cursorY < E.rowOffset) {
		E.rowOffset = E.cursorY;
	}
	if(E.cursorY >= E.rowOffset + E.screenRows) {
		E.rowOffset = E.cursorY - E.screenRows + 1;
	}
	if(E.renderX < E.columnOffset) {
		E.columnOffset = E.renderX;
	}
	if(E.renderX >= E.columnOffset + E.screenColumns) {
		E.columnOffset = E.renderX - E.screenColumns + 1;
	}
}
void editorDrawRows(struct append_buffer *ab) {
	int i;
	for(i=0;i<E.screenRows;i++) {
		int fileRow = i + E.rowOffset;
		if(fileRow >= E.numRows) {
			if(E.numRows == 0 && i == E.screenRows / 3) {
				char welcome[80];
				int welcomeLength = snprintf(welcome, sizeof(welcome),
				"Welcome to Writepad v(%s)", WRITEPAD_VERSION);
				if(welcomeLength > E.screenColumns) welcomeLength = E.screenColumns;
				int padding = (E.screenColumns - welcomeLength) / 2;
				if(padding) {
					aBufferAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) aBufferAppend(ab, " ", 1);
				aBufferAppend(ab, welcome, welcomeLength);
			} else {
				aBufferAppend(ab, "~", 1);
			}
		} else {
			int length = E.row[fileRow].rSize - E.columnOffset;
			if(length < 0) length = 0;
			if(length > E.screenColumns)
				length = E.screenColumns;
			aBufferAppend(ab, &E.row[fileRow].render[E.columnOffset], length);
		}

		aBufferAppend(ab, "\x1b[K", 3);

		aBufferAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct append_buffer *ab) {
	aBufferAppend(ab, "\x1b[7m", 4);
	char status[80], rStatus[80];
	int length = snprintf(status, sizeof(status), "%.20s - %d lines", E.file ? E.file : "[No file]", E.numRows);
	int rLength = snprintf(rStatus, sizeof(rStatus), "%d/%d", E.cursorY + 1, E.numRows);
	if(length < E.screenColumns) length = E.screenColumns;
	aBufferAppend(ab, status, length);
	while(length < E.screenColumns) {
		if(E.screenColumns - length == rLength) {
			aBufferAppend(ab, rStatus, rLength);
			break;
		} else {
			aBufferAppend(ab, " ", 1);
			length++;
		}
	}
	aBufferAppend(ab, "\x1b[m", 3);
	aBufferAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct append_buffer *ab) {
	aBufferAppend(ab, "\x1b[K", 3);
	int messageLength = strlen(E.statusMessage);
	if(messageLength > E.screenColumns) messageLength = E.screenColumns;
	if(messageLength && time(NULL) - E.statusMessage_time < 5)
		aBufferAppend(ab, E.statusMessage, messageLength);
}

void editorRefreshScreen() {
	editorScroll();

	struct append_buffer ab = APPEND_BUFFER_INIT;

	aBufferAppend(&ab, "\x1b[?25l", 6);
	aBufferAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cursorY - E.rowOffset + 1, E.renderX - E.columnOffset + 1);
	aBufferAppend(&ab, buf, strlen(buf));
	aBufferAppend(&ab, "\x1b[?25h", 6);
	write(STDOUT_FILENO, ab.b, ab.len);
	aBufferFree(&ab);
}

void editorSetStatus(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusMessage, sizeof(E.statusMessage), fmt, ap);
	va_end(ap);
	E.statusMessage_time = time(NULL);
}

/*Init*/

void initEditor() {
	E.cursorX = 0;
	E.cursorY = 0;
	E.renderX = 0;
	E.rowOffset = 0;
	E.columnOffset = 0;
	E.numRows = 0;
	E.row = NULL;
	E.file = NULL;
	E.statusMessage[0] = '\0';
	E.statusMessage_time = 0;
	if(getWindowSize(&E.screenRows, &E.screenColumns) == -1) die("getWindowSize");
	E.screenRows -= 2;
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if(argc >= 2) {
		editorOpen(argv[1]);
	}
	editorSetStatus("HELP: Ctrl-X to exit");
	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}
	return 0;
}