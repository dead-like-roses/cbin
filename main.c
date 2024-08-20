//#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

/*** defines ***/

#define CBIN_VERION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	DELETE_KEY
};

/*** data ***/

struct editor_state {
	struct termios orig_termios;
	int columns;
	int rows;

	int cx, cy;
};

struct editor_state state;
/*** terminal ***/

void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disable_rawmode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.orig_termios) == -1)
		die("tcsetattr");
}

void enable_rawmode() {
	if (tcgetattr(STDIN_FILENO, &state.orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_rawmode);

	struct termios raw = state.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int editor_read_key() {
	int nread = 0;
	char c;
	while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DELETE_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY; 
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}


int get_cursor_position(int *rows, int *cols) {
	char buf[32];
	unsigned short i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	
	while (i <sizeof(buf) -1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int get_terminal_dimensions(int *rows, int *cols) {
	struct winsize w;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return get_cursor_position(rows, cols);
	} else {
		*rows = w.ws_row;
		*cols = w.ws_col;
		return 0;
	}
}

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void ab_append(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new ==NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(struct abuf *ab) {
	free(ab->b);
}

/*** input ***/ 

void editor_move_cursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (state.cx != 0) {
				state.cx--;
			}
			break;
		case ARROW_DOWN:
			if (state.cy != state.rows -1) {
				state.cy++;
			}
			break;
		case ARROW_UP:
			if (state.cy != 0) {
				state.cy--;
			}
			break;
		case ARROW_RIGHT:
			if (state.cx != state.columns -1) {
				state.cx++;
			}
			break;
	}
}

void editor_process_key() {
	int c = editor_read_key();
	switch (c) {
		case 'q':
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		case HOME_KEY:
			state.cx = 0;
			break;
		case END_KEY:
			state.cx = state.columns - 1;
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = state.rows;
				while (times--) {
					editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;
		case ARROW_LEFT:
		case ARROW_DOWN:
		case ARROW_UP:
		case ARROW_RIGHT:
			editor_move_cursor(c);
			break;
	}
}

/*** output ***/ 

void editor_draw_rows(struct abuf *ab) {
	int y;
	for (y=0; y <= state.rows - 1; y++) {
		if (y == state.rows/3) {
		char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), "Cbin editor -- version %s", CBIN_VERION);
			if (welcomelen > state.columns) welcomelen = state.columns;

			int padding = (state.columns - welcomelen) / 2;
			if (padding) {
				ab_append(ab, "~", 1);
				padding--;
			}
			while (padding--) ab_append(ab, " ", 1);

			ab_append(ab, welcome, welcomelen);

		} else {
			ab_append(ab, "~", 1);
		}
		ab_append(ab, "\x1b[K", 3);
		if (y < state.rows -1) {
			ab_append(ab, "\r\n", 2);
		}
	}
}

void editor_refresh_screen() {
	struct abuf ab = ABUF_INIT;

	ab_append(&ab, "\x1b[?25l", 6);
	ab_append(&ab, "\x1b[H", 3);

	editor_draw_rows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", state.cy+1, state.cx+1);
	ab_append(&ab, buf, strlen(buf));

	ab_append(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	ab_free(&ab);
}

/*** editor ***/

void init_editor() {
	state.cx = 0;
	state.cy = 0;

	if(get_terminal_dimensions(&state.rows, &state.columns) == -1) die("get terminal dimensions");
}

/*** init ***/

int main() {	
	enable_rawmode();
	init_editor();

	while(1) {
		editor_refresh_screen();
		editor_process_key();
	}

	return 0;
}
