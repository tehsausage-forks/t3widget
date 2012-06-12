/* Copyright (C) 2011-2012 G.P. Halkes
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <csignal>
#include <cstring>
#include <climits>
#include <stdint.h>
#include <transcript/transcript.h>
#include <cerrno>

#ifdef HAS_SELECT_H
#include <sys/select.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef HAS_SCHED_FUNCS
#include <sched.h>
#endif

#include <t3window/window.h>
#include <t3key/key.h>

#include "util.h"
#include "main.h"
#include "key.h"
#include "keybuffer.h"
#include "internal.h"
#include "log.h"

using namespace std;
namespace t3_widget {

#define MAX_SEQUENCE 100

enum {
	WINCH_SIGNAL,
	QUIT_SIGNAL
};

typedef struct {
	const char *string;
	key_t code;
} key_string_t;

typedef struct {
	const char *string;
	size_t string_length;
	key_t key;
} mapping_t;

typedef struct {
	key_t data[MAX_SEQUENCE];
	size_t idx;
} key_sequence_t;

typedef struct {
	key_t kp;
	key_t mapped;
} kp_mapping_t;

static const key_string_t key_strings[] = {
	{ "insert", EKEY_INS },
	{ "delete", EKEY_DEL },
	{ "home", EKEY_HOME },
	{ "end", EKEY_END },
	{ "page_up", EKEY_PGUP },
	{ "page_down", EKEY_PGDN },
	{ "up", EKEY_UP },
	{ "left", EKEY_LEFT },
	{ "down", EKEY_DOWN },
	{ "right", EKEY_RIGHT },
	{ "kp_home", EKEY_KP_HOME },
	{ "kp_up", EKEY_KP_UP },
	{ "kp_page_up", EKEY_KP_PGUP },
	{ "kp_page_down", EKEY_KP_PGDN },
	{ "kp_left", EKEY_KP_LEFT },
	{ "kp_center", EKEY_KP_CENTER },
	{ "kp_right", EKEY_KP_RIGHT },
	{ "kp_end", EKEY_KP_END },
	{ "kp_down", EKEY_KP_DOWN },
	{ "kp_insert", EKEY_KP_INS },
	{ "kp_delete", EKEY_KP_DEL },
	{ "kp_enter", EKEY_KP_NL },
	{ "kp_div", EKEY_KP_DIV },
	{ "kp_mul", EKEY_KP_MUL },
	{ "kp_minus", EKEY_KP_MINUS },
	{ "kp_plus", EKEY_KP_PLUS },
	{ "tab", '\t' },
	{ "backspace", EKEY_BS }
};

static const kp_mapping_t kp_mappings[] = {
	{ EKEY_KP_HOME, EKEY_HOME },
	{ EKEY_KP_PGUP, EKEY_PGUP },
	{ EKEY_KP_PGDN, EKEY_PGDN },
	{ EKEY_KP_LEFT, EKEY_LEFT },
	{ EKEY_KP_RIGHT, EKEY_RIGHT },
	{ EKEY_KP_UP, EKEY_UP },
	{ EKEY_KP_DOWN, EKEY_DOWN },
	{ EKEY_KP_DEL, EKEY_DEL },
	{ EKEY_KP_INS, EKEY_INS },
	{ EKEY_KP_NL, EKEY_NL },
	{ EKEY_KP_DIV, '/' },
	{ EKEY_KP_MUL, '*' },
	{ EKEY_KP_PLUS, '+' },
	{ EKEY_KP_MINUS, '-' }
};

#define ARRAY_SIZE(_x) ((int) (sizeof(_x) / sizeof(_x[0])))

static mapping_t *map;
static int map_count;
static key_t map_single[128];

static const char *leave, *enter;
static const char *disable_mouse = "\033[?1005l\033[?1002l\033[?1000l";
static const char *enable_mouse = "\033[?1000h\033[?1002h\033[?1005h";

static const t3_key_node_t *keymap;
static int signal_pipe[2] = { -1, -1 };

static key_buffer_t key_buffer;
static mouse_event_buffer_t mouse_event_buffer;
static int mouse_button_state;
static pthread_t read_key_thread;

static char char_buffer[32];
static int char_buffer_fill;
static uint32_t unicode_buffer[16];
static int unicode_buffer_fill;
static transcript_t *conversion_handle;

static pthread_mutex_t key_timeout_lock = PTHREAD_MUTEX_INITIALIZER;
static int key_timeout = -1;
static bool drop_single_esc = true;

/* Used in decode_sequence and comparison routine to communicate whether the current
   sequence is a prefix of any known sequence. */
static bool is_prefix;
static enum { // Mouse reporting states:
	XTERM_MOUSE_NONE, // disabled
	XTERM_MOUSE_SINGLE_BYTE, // using a single byte for each coordinate and button state
	XTERM_MOUSE_COORD_UTF, // using UTF-like encoding for coordinates, but not for button state (xterm 262-267)
	XTERM_MOUSE_ALL_UTF // using UTF-like encoding for coordinates and button state
} xterm_mouse_reporting;

static key_t decode_sequence(bool outer);
static int decode_xterm_mouse(void);
static void stop_keys(void);

static void convert_next_key(void) {
	const char *char_buffer_ptr;
	uint32_t *unicode_buffer_ptr;

	char_buffer_ptr = char_buffer;
	unicode_buffer_ptr = unicode_buffer;

	while (1) {
		switch (transcript_to_unicode(conversion_handle, &char_buffer_ptr, char_buffer + char_buffer_fill,
				(char **) &unicode_buffer_ptr, ((const char *) unicode_buffer) + sizeof(unicode_buffer),
				TRANSCRIPT_ALLOW_FALLBACK | TRANSCRIPT_SINGLE_CONVERSION))
		{
			case TRANSCRIPT_SUCCESS:
			case TRANSCRIPT_NO_SPACE:
			case TRANSCRIPT_INCOMPLETE:
				char_buffer_fill -= char_buffer_ptr - char_buffer;
				if (char_buffer_fill != 0)
					memmove(char_buffer, char_buffer_ptr, char_buffer_fill);
				unicode_buffer_fill = unicode_buffer_ptr - unicode_buffer;
				return;

			case TRANSCRIPT_FALLBACK: // NOTE: we allow fallbacks, so this should not even occur!!!

			case TRANSCRIPT_UNASSIGNED:
			case TRANSCRIPT_ILLEGAL:
			case TRANSCRIPT_ILLEGAL_END:
			case TRANSCRIPT_INTERNAL_ERROR:
			case TRANSCRIPT_PRIVATE_USE:
				transcript_to_unicode_skip(conversion_handle, &char_buffer_ptr, char_buffer + char_buffer_fill);
				break;
			default:
				// This shouldn't happen, and we can't really do anything with this.
				return;
		}
	}
}

static key_t get_next_converted_key(void) {
	if (unicode_buffer_fill == 0)
		convert_next_key();

	if (unicode_buffer_fill > 0) {
		key_t c = unicode_buffer[0];
		unicode_buffer_fill--;
		memmove(unicode_buffer, unicode_buffer + 1, sizeof(unicode_buffer[0]) * unicode_buffer_fill);
		return c;
	}
	return -1;
}

static void unget_key(key_t c) {
	memmove(unicode_buffer + 1, unicode_buffer, sizeof(unicode_buffer[0]) * unicode_buffer_fill);
	unicode_buffer[0] = c;
	unicode_buffer_fill++;
}

static int get_next_keychar(void) {
	if (char_buffer_fill > 0) {
		int c = char_buffer[0];
		char_buffer_fill--;
		memmove(char_buffer, char_buffer + 1, char_buffer_fill);
		return (unsigned char) c;
	}
	return -1;
}

static void unget_keychar(int c) {
	memmove(char_buffer + 1, char_buffer, sizeof(char_buffer[0]) * char_buffer_fill);
	char_buffer[0] = c;
	char_buffer_fill++;
}

static bool read_keychar(int timeout) {
	key_t c;

	while ((c = t3_term_get_keychar(timeout)) == T3_WARN_UPDATE_TERMINAL) {
		transcript_t *new_conversion_handle;
		transcript_error_t transcript_error;
		/* Open new conversion handle, but make sure we actually succeed in opening it,
		   before we close the old one. */
		if ((new_conversion_handle = transcript_open_converter(t3_term_get_codeset(), TRANSCRIPT_UTF32, 0, &transcript_error)) != NULL) {
			transcript_close_converter(conversion_handle);
			conversion_handle = new_conversion_handle;
		} else {
			lprintf("Error opening new convertor '%s': %s\n", t3_term_get_codeset(), transcript_strerror(transcript_error));
		}
		lprintf("New codeset: %s\n", t3_term_get_codeset());
		key_buffer.push_back_unique(EKEY_UPDATE_TERMINAL);
	}

	if (c < T3_WARN_MIN)
		return false;

	char_buffer[char_buffer_fill++] = (char) c;
	return true;
}

static void *read_keys(void *arg) {
	int retval;
	key_t c;
	fd_set readset;

	(void) arg;

	while (1) {
		FD_ZERO(&readset);
		FD_SET(0, &readset);
		FD_SET(signal_pipe[0], &readset);

		retval = select(signal_pipe[0] + 1, &readset, NULL, NULL, NULL);

		if (retval < 0)
			continue;

		if (FD_ISSET(signal_pipe[0], &readset)) {
			char command;

			nosig_read(signal_pipe[0], &command, 1);
			switch (command) {
				case QUIT_SIGNAL:
					/* Exit thread */
					close(signal_pipe[0]);
					signal_pipe[0] = -1;
					leave = NULL;
					return NULL;
				case WINCH_SIGNAL:
					key_buffer.push_back_unique(EKEY_RESIZE);
					break;
				default:
					// This should be impossible, so just ignore
					continue;
			}
		}

		if (FD_ISSET(0, &readset))
			read_keychar(-1);

		while ((c = get_next_converted_key()) >= 0) {
			if (c == EKEY_ESC) {
				pthread_mutex_lock(&key_timeout_lock);
				c = decode_sequence(true);
				pthread_mutex_unlock(&key_timeout_lock);
				if (c < 0)
					continue;
				else if (drop_single_esc && c == (EKEY_ESC | EKEY_META))
					c = EKEY_ESC;
				else if ((c & EKEY_KEY_MASK) < 128 && map_single[c & EKEY_KEY_MASK] != 0)
					c = (c & ~EKEY_KEY_MASK) | map_single[c & EKEY_KEY_MASK];
			} else if (c > 0 && c < 128 && map_single[c] != 0) {
				c = map_single[c];
			}
			if (c >= 0)
				key_buffer.push_back(c);
		}
	}
}

key_t read_key(void) {
	return key_buffer.pop_front();
}

mouse_event_t read_mouse_event(void) {
	return mouse_event_buffer.pop_front();
}

static int compare_sequence_with_mapping(const void *key, const void *mapping) {
	const key_sequence_t *_key;
	const mapping_t *_mapping;
	size_t i;

	_key = (const key_sequence_t *) key;
	_mapping = (const mapping_t *) mapping;

	for (i = 0; i < _key->idx && i < _mapping->string_length; i++) {
		if (_key->data[i] != _mapping->string[i]) {
			if ((char) _key->data[i] < _mapping->string[i])
				return -1;
			return 1;
		}
	}

	if (i < _mapping->string_length) {
		is_prefix = true;
		return -1;
	}

	if (i < _key->idx)
		return 1;
	return 0;
}

static key_t decode_sequence(bool outer) {
	key_sequence_t sequence;
	mapping_t *matched;
	int c;

	sequence.idx = 1;
	sequence.data[0] = EKEY_ESC;

	while (sequence.idx < MAX_SEQUENCE) {
		while ((c = get_next_keychar()) >= 0) {
			if (c == EKEY_ESC) {
				if (sequence.idx == 1 && outer) {
					key_t alted = decode_sequence(false);
					return alted >= 0 ? alted | EKEY_META : (alted == -2 ? EKEY_ESC : -1);
				}
				unget_keychar(c);
				goto unknown_sequence;
			}

			sequence.data[sequence.idx++] = c;

			is_prefix = false;
			if ((matched = (mapping_t *) bsearch(&sequence, map, map_count,
					sizeof(mapping_t), compare_sequence_with_mapping)) != NULL)
				return matched->key;

			/* Detect and ignore ANSI CSI sequences, regardless of whether they are recognised. */
			if (sequence.data[1] == '[' && !is_prefix) {
				if (sequence.idx == 3 && xterm_mouse_reporting && c == 'M') {
					if (!outer) {
						/* If this is not the outer decode_sequence call, push everything
						   back onto the character list, and do nothing. A next call to
						   decode_sequence will take care of the mouse handling. */
						unget_keychar(c);
						unget_keychar('[');
						unget_keychar(EKEY_ESC);
						return -1;
					}
					return decode_xterm_mouse();
				} else if (sequence.idx > 2 && c >= 0x40 && c < 0x7f) {
					return -1;
				} else if (c < 0x20 || c > 0x7f) {
					/* Drop unknown leading sequence if some non-CSI byte is found. */
					unget_key(c);
					return -1;
				}
				continue;
			}

			if (!is_prefix)
				goto unknown_sequence;
		}

		if (!read_keychar(outer ? key_timeout : 50))
			break;
	}

unknown_sequence:
	if (sequence.idx == 2) {
		key_t alted_key;
		unget_keychar(sequence.data[1]);
		/* It is quite possible that we only read a partial character here. So if we haven't
		   read a complete character yet (i.e. get_next_converted_key returns -1), we simply
		   keep asking to read one more character. We use a one milisecond timeout, to ensure
		   we don't get stuck waiting here. If reading the keychar times out, we need to skip
		   this input. */
		while ((alted_key = get_next_converted_key()) < 0 && read_keychar(1)) {}
		if (alted_key < 0)
			return -1;
		/* We need to do single key mapping as well here. */
		if ((alted_key & EKEY_KEY_MASK) < 128 && map_single[alted_key & EKEY_KEY_MASK] != 0)
			alted_key = map_single[alted_key & EKEY_KEY_MASK];
		return alted_key | EKEY_META;
	} else if (sequence.idx == 1) {
		return drop_single_esc ? -2 : EKEY_ESC;
	}

	/* Something unwanted has happened here: the character sequence we encoutered was not
	   in our key map. Because it will give some undesired result to just return
	   <alt><first character> we ignore the whole sequence */
	return -1;
}

#define get_next_byte(x) do { \
	while ((x = get_next_keychar()) < 0 && read_keychar(1)) {} \
	if (x < 0) \
		return -1; \
} while (0)

#define ensure_buffer_fill() do { \
	while (char_buffer_fill == idx) { \
		if (!read_keychar(1)) { \
			xterm_mouse_reporting = XTERM_MOUSE_SINGLE_BYTE; \
			goto convert_mouse_event; \
		} \
	} \
} while (0)

/** Decode an XTerm mouse event.

    This routine would have been simple, had it not been for the fact that the
    original XTerm mouse protocol is a little broken, and it is hard to detect
    whether the fix is present. First of all, the original protocol can not
    handle coordinates above 223. This is due to the fact that a coordinate is
    encoded as coordinate + 32 (and coordinates start at 1).

    Once screens got big enough to make terminals more than 223 columns wide,
    a fix was implemented, which uses UTF-8 encoding (but only using at most
    2 bytes, instead of simply using the full range). However, to accomodate
    clients which simply assumed all input was UTF-8 encoded, several versions
    later the buttons were encoded as UTF-8 as well. Thus we now have three
    different versions of the XTerm mouse reporting protocol.

    This wouldn't be so bad, if it wasn't for all those terminal emulators out
    there claiming to be XTerm. They make detection of the specific version of
    the protocol practically impossible, because they may report any version
    number in response to a "Send Device Attributes" request. Thus, this
    routine tries to do automatic switching, based on the received mouse
    reports.

    Autodetection logic:
    - start by assuming full UTF-8 encoded mode
    - if the buttons have the top bit set, but are not properly UTF-8 encoded,
      assume that only the coordinates have been UTF-8 encoded
    - if a coordinate is not properly UTF-8 encoded, assume single byte coding
    - if during the examination of the coordinate we find that not enough bytes
      are available to decode, assume single byte coding. (This situation can
      not be caused by incorrecly assuming that the buttons are UTF-8 encoded,
      because then the first coordinate byte would be invalid.)
*/
static int decode_xterm_mouse(void) {
	mouse_event_t event;
	int buttons, idx, i;

	while (char_buffer_fill < 3) {
		if (!read_keychar(1))
			return -1;
	}

	if (xterm_mouse_reporting > XTERM_MOUSE_SINGLE_BYTE) {
		idx = 1;
		if ((char_buffer[0] & 0x80) && xterm_mouse_reporting == XTERM_MOUSE_ALL_UTF) {
			if ((char_buffer[0] & 0xc0) != 0xc0 || (char_buffer[1] & 0xc0) != 0x80)
				xterm_mouse_reporting = XTERM_MOUSE_COORD_UTF;
			else
				idx = 2;
		}
		for (i = 0; i < 2; i++) {
			ensure_buffer_fill();
			if ((char_buffer[idx] & 0x80) && xterm_mouse_reporting >= XTERM_MOUSE_COORD_UTF) {
				idx++;
				ensure_buffer_fill();
				if ((char_buffer[idx - 1] & 0xc0) != 0xc0 || (char_buffer[idx] & 0xc0) != 0x80) {
					xterm_mouse_reporting = XTERM_MOUSE_SINGLE_BYTE;
					goto convert_mouse_event;
				}
			}
			idx++;
		}
	}

#define DECODE_UTF() (char_buffer[idx] & 0x80 ? (idx += 2, ((((unsigned char) char_buffer[idx - 2] & ~0xc0) << 6) | \
	((unsigned char) char_buffer[idx - 1] & ~0xc0))) : char_buffer[idx++])

convert_mouse_event:
	idx = 0;
	switch (xterm_mouse_reporting) {
		case XTERM_MOUSE_SINGLE_BYTE:
			buttons = (unsigned char) char_buffer[0];
			event.x = (unsigned char) char_buffer[1];
			event.y = (unsigned char) char_buffer[2];
			idx = 3;
			break;
		case XTERM_MOUSE_COORD_UTF:
			buttons = (unsigned char) char_buffer[0];
			idx = 1;
			goto convert_coordinates;
		case XTERM_MOUSE_ALL_UTF:
			buttons = DECODE_UTF();
		convert_coordinates:
			event.x = DECODE_UTF();
			event.y = DECODE_UTF();
			break;
		default:
			return -1;
	}
	char_buffer_fill -= idx;
	memmove(char_buffer, char_buffer + idx, char_buffer_fill);

	event.x = event.x <= 32 ? event.x = -1 : event.x - 33;
	event.y = event.y <= 32 ? event.y = -1 : event.y - 33;
	event.previous_button_state = mouse_button_state;
	buttons -= 32;

	if (buttons & 64) {
		event.type = EMOUSE_BUTTON_PRESS;
		switch (buttons & 3) {
			case 0:
				event.button_state = mouse_button_state | EMOUSE_SCROLL_UP;
				break;
			case 1:
				event.button_state = mouse_button_state | EMOUSE_SCROLL_DOWN;
				break;
			default:
				event.button_state = mouse_button_state;
				break;
		}
	} else if (buttons & 32) {
		event.type = EMOUSE_MOTION;
		/* Trying to decode the button state here is pretty much useless, because
		   it can only encode a single button. The saved mouse_button_state is
		   more accurate. */
		event.button_state = mouse_button_state;
	} else {
		event.type = EMOUSE_BUTTON_PRESS;
		switch (buttons & 3) {
			case 0:
				event.button_state = mouse_button_state |= EMOUSE_BUTTON_LEFT;
				break;
			case 1:
				event.button_state = mouse_button_state |= EMOUSE_BUTTON_MIDDLE;
				break;
			case 2:
				event.button_state = mouse_button_state |= EMOUSE_BUTTON_RIGHT;
				break;
			default:
				event.type = EMOUSE_BUTTON_RELEASE;
				event.button_state = mouse_button_state = 0;
				break;
		}
	}
	/* Due to the fact that the XTerm mouse protocol doesn't say which mouse button
	   was released, we assume that all buttons are released on a  button release
	   event. However, when multiple buttons are pressed simultaneously, this may
	   result in button release events where no buttons where previously thought
	   to be depressed. We filter those out here. */
	if (event.type == EMOUSE_BUTTON_RELEASE && event.previous_button_state == 0)
		return -1;

	event.modifier_state = (buttons >> 2) & 7;
	event.window = NULL;
	mouse_event_buffer.push_back(event);
	return EKEY_MOUSE_EVENT;
}

void insert_protected_key(key_t key) {
	if (key >= 0)
		key_buffer.push_back(key | EKEY_PROTECT);
}

static void sigwinch_handler(int param) {
	char winch_signal = WINCH_SIGNAL;
	(void) param;
	nosig_write(signal_pipe[1], &winch_signal, 1);
}

static key_t map_kp(key_t kp) {
	size_t i;
	for (i = 0; i < ARRAY_SIZE(kp_mappings); i++)
		if (kp_mappings[i].kp == kp)
			return kp_mappings[i].mapped;
	return kp;
}

static int compare_mapping(const void *a, const void *b) {
	const mapping_t *_a, *_b;
	int result;

	_a = (const mapping_t *) a;
	_b = (const mapping_t *) b;

	if ((result = memcmp(_a->string, _b->string, min(_a->string_length, _b->string_length))) != 0)
		return result;
	if (_a->string_length < _b->string_length)
		return -1;
	else if (_a->string_length > _b->string_length)
		return 1;
	return 0;
}

static bool is_function_key(const char *str) {
	/* First character must be f, second a digit ... */
	if (str[0] != 'f' || !(str[1] >= '0' && str[1] <= '9'))
		return false;

	/* ... third either a digit, - or nothing ... */
	if (str[2] == 0 || str[2] == '-')
		return true;
	if (!(str[2] >= '0' && str[2] <= '9'))
		return false;

	/* ... fourth either - or nothing. */
	if (str[3] == 0 || str[3] == '-')
		return true;

	return false;
}

#define RETURN_ERROR(_s, _x) do { result.set_error(_s, _x, __FILE__, __LINE__); goto return_error; } while (0)
/* Initialize the key map */
complex_error_t init_keys(const char *term, bool separate_keypad) {
	complex_error_t result;
	struct sigaction sa;
	sigset_t sigs;
	const t3_key_node_t *key_node;
	int i, j, error, idx;
	transcript_error_t transcript_error;
#ifdef HAS_SCHED_FUNCS
	struct sched_param sched_param;
#endif
	const char *shiftfn = NULL;

	/* Start with things most likely to fail */
	if ((conversion_handle = transcript_open_converter(transcript_get_codeset(), TRANSCRIPT_UTF32, 0, &transcript_error)) == NULL)
		RETURN_ERROR(complex_error_t::SRC_TRANSCRIPT, transcript_error);

	if ((keymap = t3_key_load_map(term, NULL, &error)) == NULL)
		RETURN_ERROR(complex_error_t::SRC_T3_KEY, error);

	if (pipe(signal_pipe) < 0)
		RETURN_ERROR(complex_error_t::SRC_ERRNO, errno);

	sa.sa_handler = sigwinch_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGWINCH);
	sa.sa_flags = 0;

	if (sigaction(SIGWINCH, &sa, NULL) < 0)
		RETURN_ERROR(complex_error_t::SRC_ERRNO, errno);

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGWINCH);
	if (sigprocmask(SIG_UNBLOCK, &sigs, NULL) < 0)
		RETURN_ERROR(complex_error_t::SRC_ERRNO, errno);

	for (i = 1; i <= 26; i++)
		map_single[i] = EKEY_CTRL | ('a' + i - 1);
	/* "unmap" TAB */
	map_single[(int) '\t'] = 0;
	/* EKEY_ESC is defined as 27, so no need to map */
	map_single[28] = EKEY_CTRL | '\\';
	map_single[29] = EKEY_CTRL | ']';
	map_single[30] = EKEY_CTRL | '_';
	map_single[31] = EKEY_CTRL | '^';

	/* Add a few default keys which will help on terminals with little information
	   available. */
	map_single[8] = EKEY_BS;
	map_single[127] = EKEY_BS;
	map_single[10] = EKEY_NL;
	map_single[13] = EKEY_NL;

	if ((key_node = t3_key_get_named_node(keymap, "_enter")) != NULL) {
		t3_term_putp(key_node->string);
		enter = key_node->string;
	}
	if ((key_node = t3_key_get_named_node(keymap, "_leave")) != NULL)
		leave = key_node->string;
	if ((key_node = t3_key_get_named_node(keymap, "_shiftfn")) != NULL)
		shiftfn = key_node->string;
	if (t3_key_get_named_node(keymap, "_xterm_mouse")) {
		/* Start out in ALL_UTF mode. The decode_xterm_mouse routine will switch back
		   to a different mode if necessary. */
		xterm_mouse_reporting = XTERM_MOUSE_ALL_UTF;
		t3_term_putp(enable_mouse);
	}

	/* Load all the known keys from the terminfo database.
	   - find out how many sequences there are
	   - allocate an appropriate amount of memory
	   - fill the map
	   - sort the map for quick searching
	*/
	for (key_node = keymap; key_node != NULL; key_node = key_node->next) {
		if (key_node->key[0] == '_')
			continue;
		if (key_node->string[0] == 27)
			map_count++;
	}

	if ((map = (mapping_t *) malloc(sizeof(mapping_t) * map_count)) == NULL)
		RETURN_ERROR(complex_error_t::SRC_ERRNO, ENOMEM);

	for (key_node = keymap, idx = 0; key_node != NULL; key_node = key_node->next) {
		if (key_node->key[0] == '_')
			continue;

		if (key_node->string[0] == 27) {
			map[idx].string = key_node->string;
			map[idx].string_length = key_node->string_length;
		}

		for (i = 0; i < ARRAY_SIZE(key_strings); i++) {
			/* Check if this is a sequence we know. */
			for (j = 0; key_strings[i].string[j] == key_node->key[j] &&
					key_strings[i].string[j] != 0 && key_node->key[j] != 0; j++)
			{}

			if (!(key_strings[i].string[j] == 0 && (key_node->key[j] == '-' || key_node->key[j] == 0)))
				continue;

			if (key_node->string[0] != 27) {
				map_single[(unsigned char) key_node->string[0]] = key_strings[i].code;
				break;
			}

			map[idx].key = separate_keypad ? key_strings[i].code : map_kp(key_strings[i].code);
			for (; key_node->key[j] != 0; j++) {
				switch (key_node->key[j]) {
					case 'c':
						map[idx].key |= EKEY_CTRL;
						break;
					case 'm':
						map[idx].key |= EKEY_META;
						break;
					case 's':
						map[idx].key |= EKEY_SHIFT;
						break;
					default:
						break;
				}
			}
			break;
		}

		if (i == ARRAY_SIZE(key_strings)) {
			if (is_function_key(key_node->key)) {
				key_t key = EKEY_F1 + atoi(key_node->key + 1) - 1;
				for (j = 2; key_node->key[j] != 0; j++) {
					switch (key_node->key[j]) {
						case 'c':
							key |= EKEY_CTRL;
							break;
						case 'm':
							key |= EKEY_META;
							break;
						case 's':
							key |= EKEY_SHIFT;
							break;
						default:
							break;
					}
				}
				if (shiftfn != NULL && key >= EKEY_F1 + shiftfn[2] - 1 && key < EKEY_F1 + shiftfn[2] + shiftfn[1] - shiftfn[0]) {
					key -= shiftfn[2] - 1;
					key |= EKEY_SHIFT;
				}
				if (key_node->string[0] == 27)
					map[idx].key = key;
				else
					map_single[(unsigned char) key_node->string[0]] = key;
			} else {
				if (key_node->string[0] == 27)
					map[idx].key = EKEY_IGNORE;
			}
		}
		if (key_node->string[0] == 27)
			idx++;
	}
	qsort(map, map_count, sizeof(mapping_t), compare_mapping);

	if ((error = pthread_create(&read_key_thread, NULL, read_keys, NULL)) != 0)
		RETURN_ERROR(complex_error_t::SRC_ERRNO, error);

#ifdef HAS_SCHED_FUNCS
	/* Set the priority for the key reading thread to max, such that we can be sure
	   that when a key is available it will be able to get it. */
	sched_param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (sched_param.sched_priority == -1)
		sched_param.sched_priority = 0;
	pthread_setschedparam(read_key_thread, SCHED_FIFO, &sched_param);
#endif

	return result;

return_error:
	cleanup_keys();
	leave = NULL;
	if (signal_pipe[0] != -1) {
		close(signal_pipe[0]);
		close(signal_pipe[1]);
		signal_pipe[0] = -1;
		signal_pipe[1] = -1;
	}
	return result;
}
#undef RETURN_ERROR

void deinit_keys(void) {
	if (xterm_mouse_reporting)
		t3_term_putp(disable_mouse);
	t3_term_putp(leave);
}

void reinit_keys(void) {
	t3_term_putp(enter);
	if (xterm_mouse_reporting)
		t3_term_putp(enable_mouse);
}

void cleanup_keys(void) {
	stop_keys();
	if (conversion_handle != NULL) {
		transcript_close_converter(conversion_handle);
		conversion_handle = NULL;
	}
	if (keymap != NULL) {
		t3_key_free_map(keymap);
		keymap = NULL;
	}
	if (map != NULL) {
		free(map);
		map = NULL;
	}
	memset(map_single, 0, sizeof(map_single));
	leave = NULL;
	enter = NULL;
}

static void stop_keys(void) {
	void *retval;
	char quit_signal = QUIT_SIGNAL;
	nosig_write(signal_pipe[1], &quit_signal, 1);
	close(signal_pipe[1]);
	signal_pipe[1] = -1;
	pthread_join(read_key_thread, &retval);
	if (xterm_mouse_reporting)
		t3_term_putp(disable_mouse);
	t3_term_putp(leave);
}

void set_key_timeout(int msec) {
	pthread_mutex_lock(&key_timeout_lock);
	if (msec == 0) {
		key_timeout = -1;
		drop_single_esc = true;
	} else if (msec < 0) {
		key_timeout = -msec;
		drop_single_esc = true;
	} else {
		key_timeout = msec;
		drop_single_esc = false;
	}
	pthread_mutex_unlock(&key_timeout_lock);
}

int get_key_timeout(void) {
	int result;
	pthread_mutex_lock(&key_timeout_lock);
	result = key_timeout < 0 ? 0 : (drop_single_esc ? -key_timeout : key_timeout);
	pthread_mutex_unlock(&key_timeout_lock);
	return result;
}

void signal_update(void) {
	key_buffer.push_back_unique(EKEY_EXTERNAL_UPDATE);
}

}; // namespace
