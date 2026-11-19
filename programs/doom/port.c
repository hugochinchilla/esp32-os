/* Doom on Chinchilla (backlog item 81): the engine's side of the port.
 *
 * Everything the engine (third_party/doomgeneric) asks of the machine it
 * runs on is here: the C library's names (port.h), and the engine's own
 * "i_" files for the system, the clock, the picture and the keys, which
 * take the place of doomgeneric's i_system.c, i_timer.c, i_video.c and
 * i_input.c. Parts follow those files and are, like them, under the GNU
 * General Public License, version 2 (third_party/doomgeneric/LICENSE).
 *
 * The engine is not left running: doom.cpp calls port_tick() from a timer,
 * and what would end a program on a PC (exit(), I_Error) comes back to
 * port_start() or port_tick() with a longjmp.
 */
#define PORT_ITSELF 1
#include "port.h"

#include <setjmp.h>

#include "config.h"
#include "d_event.h"
#include "d_main.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_endoom.h"
#include "i_joystick.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

void D_DoomMain(void);
void doomgeneric_Tick(void);

/* ---- Memory ------------------------------------------------------------ */

/* Every block is on a list, so that all of it can be given back when the
 * engine has ended wherever it was. */
typedef struct block_s {
    struct block_s *prev, *next;
    size_t bytes;
    uint32_t pad; /* the block itself begins on 16 bytes */
} block_t;

static block_t *blocks;

void *port_malloc(size_t bytes)
{
    block_t *b = (block_t *)sys_allocate(sizeof(block_t) + bytes);
    if (b == NULL) {
        return NULL;
    }
    b->bytes = bytes;
    b->prev = NULL;
    b->next = blocks;
    if (blocks != NULL) {
        blocks->prev = b;
    }
    blocks = b;
    return b + 1;
}

void *port_calloc(size_t count, size_t bytes)
{
    return port_malloc(count * bytes); /* the system's memory comes zeroed */
}

void port_free(void *block)
{
    block_t *b;
    if (block == NULL) {
        return;
    }
    b = (block_t *)block - 1;
    if (b->prev != NULL) {
        b->prev->next = b->next;
    } else {
        blocks = b->next;
    }
    if (b->next != NULL) {
        b->next->prev = b->prev;
    }
    sys_release(b);
}

void *port_realloc(void *block, size_t bytes)
{
    void *bigger = port_malloc(bytes);
    if (bigger != NULL && block != NULL) {
        const size_t had = ((block_t *)block - 1)->bytes;
        memcpy(bigger, block, had < bytes ? had : bytes);
        port_free(block);
    }
    return bigger;
}

char *port_strdup(const char *text)
{
    const size_t size = strlen(text) + 1;
    char *copy = (char *)port_malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

void port_free_all(void)
{
    while (blocks != NULL) {
        port_free(blocks + 1);
    }
}

/* ---- Characters ---------------------------------------------------------- */

int port_toupper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
int port_tolower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
int port_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int port_isprint(int c) { return c >= ' ' && c < 127; }

/* Only the settings file's numbers with a point: "0.5", "2.000000". */
double port_atof(const char *text)
{
    double value = 0, scale = 1;
    int negative = 0;
    while (port_isspace(*text)) {
        text++;
    }
    if (*text == '-') {
        negative = 1;
        text++;
    }
    for (; *text >= '0' && *text <= '9'; text++) {
        value = value * 10 + (*text - '0');
    }
    if (*text == '.') {
        for (text++; *text >= '0' && *text <= '9'; text++) {
            scale /= 10;
            value += (*text - '0') * scale;
        }
    }
    return negative ? -value : value;
}

/* ---- Text out: the system's log, a line at a time ---------------------------- */

static char log_line[160];
static int log_used;

static void log_text(const char *text)
{
    for (; *text != 0; text++) {
        if (*text == '\n' || log_used == (int)sizeof(log_line) - 1) {
            log_line[log_used] = 0;
            if (log_used > 0) {
                sys_log(log_line);
            }
            log_used = 0;
        }
        if (*text != '\n' && *text != '\r') {
            log_line[log_used++] = *text;
        }
    }
}

/* ---- Files --------------------------------------------------------------- */

/* A FILE of ours behind the C library's name: the system's handle, and the
 * place in the file, which the system's calls do not tell. */
typedef struct {
    int handle;
    uint32_t place, size;
    uint32_t at; /* where the system's handle is: it is moved only when a read or write needs it */
} file_t;

/* The handle where the engine thinks it is. The engine seeks before nearly
 * every read, mostly to where it already is, and every call into the
 * system costs. */
static int file_in_place(file_t *f)
{
    if (f->at != f->place) {
        if (!sys_seek(f->handle, f->place)) {
            return 0;
        }
        f->at = f->place;
    }
    return 1;
}

static int is_log(FILE *file) { return file == (FILE *)1 || file == (FILE *)2; }

FILE *port_fopen(const char *path, const char *mode)
{
    const int write = mode[0] == 'w';
    long size = 0;
    file_t *f;
    if (!write) {
        size = sys_size(path);
        if (size < 0) {
            return NULL;
        }
    }
    f = (file_t *)port_malloc(sizeof(file_t));
    if (f == NULL) {
        return NULL;
    }
    f->handle = sys_open(path, write);
    if (f->handle == 0) {
        port_free(f);
        return NULL;
    }
    f->place = f->at = 0;
    f->size = (uint32_t)size;
    return (FILE *)f;
}

int port_fclose(FILE *file)
{
    file_t *f = (file_t *)file;
    if (file == NULL || is_log(file)) {
        return 0;
    }
    sys_close(f->handle);
    port_free(f);
    return 0;
}

size_t port_fread(void *data, size_t size, size_t count, FILE *file)
{
    file_t *f = (file_t *)file;
    long got;
    if (is_log(file) || size == 0 || count == 0) {
        return 0;
    }
    if (!file_in_place(f)) {
        return 0;
    }
    got = sys_read(f->handle, data, size * count);
    if (got <= 0) {
        return 0;
    }
    f->place += (uint32_t)got;
    f->at = f->place;
    return (size_t)got / size;
}

size_t port_fwrite(const void *data, size_t size, size_t count, FILE *file)
{
    file_t *f = (file_t *)file;
    if (is_log(file) || size == 0 || count == 0) {
        return 0;
    }
    if (!file_in_place(f) || !sys_write(f->handle, data, size * count)) {
        return 0;
    }
    f->place += (uint32_t)(size * count);
    f->at = f->place;
    if (f->place > f->size) {
        f->size = f->place;
    }
    return count;
}

int port_fseek(FILE *file, long offset, int whence)
{
    file_t *f = (file_t *)file;
    long to;
    if (is_log(file)) {
        return -1;
    }
    to = whence == SEEK_SET ? offset : whence == SEEK_CUR ? (long)f->place + offset : (long)f->size + offset;
    if (to < 0) {
        return -1;
    }
    f->place = (uint32_t)to;
    return 0;
}

long port_ftell(FILE *file) { return is_log(file) ? 0 : (long)((file_t *)file)->place; }
int port_feof(FILE *file) { return is_log(file) || ((file_t *)file)->place >= ((file_t *)file)->size; }
int port_fflush(FILE *file)
{
    (void)file;
    return 0;
}

int port_vfprintf(FILE *file, const char *format, va_list arguments)
{
    char text[512];
    const int size = vsnprintf(text, sizeof(text), format, arguments);
    if (is_log(file)) {
        log_text(text);
    } else if (size > 0) {
        port_fwrite(text, 1, size < (int)sizeof(text) ? (size_t)size : sizeof(text) - 1, file);
    }
    return size;
}

int port_fprintf(FILE *file, const char *format, ...)
{
    va_list arguments;
    int size;
    va_start(arguments, format);
    size = port_vfprintf(file, format, arguments);
    va_end(arguments);
    return size;
}

int port_printf(const char *format, ...)
{
    va_list arguments;
    int size;
    va_start(arguments, format);
    size = port_vfprintf((FILE *)1, format, arguments);
    va_end(arguments);
    return size;
}

int port_puts(const char *text)
{
    log_text(text);
    log_text("\n");
    return 0;
}

int port_putchar(int c)
{
    const char text[2] = {(char)c, 0};
    log_text(text);
    return c;
}

/* The engine reads its settings file with one pattern, a word and the rest
 * of the line: the next line is taken whole and given to sscanf. */
int port_fscanf(FILE *file, const char *format, char *word, char *rest)
{
    file_t *f = (file_t *)file;
    char line[200];
    long got;
    int length = 0;
    (void)format;
    if (is_log(file) || f->place >= f->size) {
        return EOF;
    }
    got = file_in_place(f) ? sys_read(f->handle, line, sizeof(line) - 1) : 0;
    if (got <= 0) {
        f->place = f->size;
        return EOF;
    }
    while (length < got && line[length] != '\n') {
        length++;
    }
    line[length] = 0;
    /* Past the end of the line: the next call begins with the next one. */
    f->at = f->place + (uint32_t)got;
    f->place += (uint32_t)(length < got ? length + 1 : length);
    return sscanf(line, "%79s %99[^\n]", word, rest);
}

int port_remove(const char *path) { return sys_remove(path) ? 0 : -1; }
int port_rename(const char *from, const char *to) { return sys_rename(from, to) ? 0 : -1; }

/* ---- Leaving ---------------------------------------------------------------- */

static jmp_buf leave;
static int running;        /* inside the engine: `leave` is good */
static int ended;
static char error_text[256];
static int with_error;

void port_exit(int code)
{
    (void)code;
    ended = 1;
    if (!running) {
        for (;;) {
            sys_rest(1000); /* nowhere to go back to: cannot happen */
        }
    }
    longjmp(leave, 1);
}

const char *port_error(void) { return with_error ? error_text : NULL; }

/* ---- i_system ---------------------------------------------------------------- */

typedef struct atexit_listentry_s atexit_listentry_t;
struct atexit_listentry_s {
    atexit_func_t func;
    boolean run_on_error;
    atexit_listentry_t *next;
};
static atexit_listentry_t *exit_funcs;

void I_AtExit(atexit_func_t func, boolean run_on_error)
{
    atexit_listentry_t *entry = (atexit_listentry_t *)port_malloc(sizeof(*entry));
    entry->func = func;
    entry->run_on_error = run_on_error;
    entry->next = exit_funcs;
    exit_funcs = entry;
}

void I_Tactile(int on, int off, int total)
{
    (void)on;
    (void)off;
    (void)total;
}

/* The zone: all the memory the game itself uses, taken once. The board has
 * 5 to 6 MB of external RAM free with the desktop empty; a megabyte or so
 * of it must stay for the rest of the machine and for what the engine asks
 * for outside the zone. The game ran in 4 MB machines in 1993. */
byte *I_ZoneBase(int *size)
{
    static const int kTries[] = {4096, 3584, 3072, 2560};
    unsigned i;
    for (i = 0; i < sizeof(kTries) / sizeof(kTries[0]); i++) {
        byte *zone = (byte *)port_malloc((size_t)kTries[i] * 1024);
        if (zone != NULL) {
            *size = kTries[i] * 1024;
            port_printf("zone memory: %d KB\n", kTries[i]);
            return zone;
        }
    }
    I_Error("There is not enough memory for Doom: it needs 2.5 MB free.");
    return NULL;
}

void I_PrintBanner(char *msg) { port_puts(msg); }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(char *gamedescription) { port_puts(gamedescription); }
boolean I_ConsoleStdout(void) { return false; }
void I_Endoom(byte *data) { (void)data; } /* the DOS text screen shown on leaving: there is no text screen */

void I_Quit(void)
{
    atexit_listentry_t *entry;
    for (entry = exit_funcs; entry != NULL; entry = entry->next) {
        entry->func();
    }
    port_exit(0);
}

void I_Error(char *error, ...)
{
    static boolean already_quitting = false;
    va_list arguments;
    atexit_listentry_t *entry;
    va_start(arguments, error);
    vsnprintf(error_text, sizeof(error_text), error, arguments);
    va_end(arguments);
    with_error = 1;
    port_printf("Doom: %s\n", error_text);
    if (!already_quitting) {
        already_quitting = true;
        for (entry = exit_funcs; entry != NULL; entry = entry->next) {
            if (entry->run_on_error) {
                entry->func();
            }
        }
    }
    port_exit(-1);
}

/* What a DOS machine had in its first bytes of memory (DOS 6.22), which a
 * few maps read through a null pointer: from Chocolate Doom, by way of
 * PrBoom+. */
boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
    static const unsigned char kDos[10] = {0x57, 0x92, 0x19, 0x00, 0xF4, 0x06, 0x70, 0x00, 0x16, 0x00};
    switch (size) {
    case 1:
        *((unsigned char *)value) = kDos[offset];
        return true;
    case 2:
        *((unsigned short *)value) = (unsigned short)(kDos[offset] | (kDos[offset + 1] << 8));
        return true;
    case 4:
        *((unsigned int *)value) = kDos[offset] | (kDos[offset + 1] << 8) | (kDos[offset + 2] << 16) |
                                   ((unsigned int)kDos[offset + 3] << 24);
        return true;
    }
    return false;
}

/* ---- i_timer ---------------------------------------------------------------- */

static uint32_t base_ms;
static int base_set;

int I_GetTimeMS(void)
{
    const uint32_t now = sys_ms();
    if (!base_set) {
        base_set = 1;
        base_ms = now;
    }
    return (int)(now - base_ms);
}

int I_GetTime(void) { return (int)(((uint32_t)I_GetTimeMS() * TICRATE) / 1000); }
void I_Sleep(int ms) { sys_rest(ms > 0 ? (uint32_t)ms : 1); }
void I_WaitVBL(int count) { (void)count; }
void I_InitTimer(void) {}

/* ---- i_video ---------------------------------------------------------------- */

byte *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible = true;
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int usegamma = 0;
int usemouse = 1;
int vanilla_keyboard_mapping = 0; /* a saved game's name takes what a key types, not the key: W is a letter there */

static byte palette_now[768];
static int palette_changed = 1;

void I_InitGraphics(void)
{
    I_VideoBuffer = (byte *)Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    screenvisible = true;
}

void I_ShutdownGraphics(void) {}
void I_StartFrame(void) {}
void I_StartTic(void) {} /* keys and the mouse are handed in as they come (port_key) */
void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
    sys_picture(I_VideoBuffer, palette_now, palette_changed);
    palette_changed = 0;
}

void I_ReadScreen(byte *scr) { memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT); }

void I_SetPalette(byte *palette)
{
    int i;
    for (i = 0; i < 768; i++) {
        palette_now[i] = gammatable[usegamma][palette[i]];
    }
    palette_changed = 1;
}

void port_show_again(void) { palette_changed = 1; }

/* The colour of the palette nearest to this one. */
int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = INT32_MAX, i;
    for (i = 0; i < 256; i++) {
        const int dr = r - palette_now[i * 3], dg = g - palette_now[i * 3 + 1], db = b - palette_now[i * 3 + 2];
        const int diff = dr * dr + dg * dg + db * db;
        if (diff < best_diff) {
            best = i;
            best_diff = diff;
        }
    }
    return best;
}

void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *title) { (void)title; }
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_EnableLoadingDisk(void) {}
void I_BindVideoVariables(void) {}
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_CheckIsScreensaver(void) {}

/* ---- i_input ---------------------------------------------------------------- */

/* A USB HID keyboard usage as the engine's key. The classic keys: arrows,
 * Ctrl fires, Space opens, Shift runs, Alt strafes, comma and full stop
 * step sideways; and W, A, S, D, Q and E beside them. */
static int doom_key(int usage)
{
    static const unsigned char kRow[] = {
        /* 0x28 */ KEY_ENTER, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_USE, KEY_MINUS, KEY_EQUALS,
        /* 0x2F */ '[', ']', '\\', 0, ';', '\'', '`', KEY_STRAFE_L, KEY_STRAFE_R, '/', KEY_CAPSLOCK,
        /* 0x3A */ KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
        /* 0x46 */ KEY_PRTSCR, KEY_SCRLCK, KEY_PAUSE, KEY_INS, KEY_HOME, KEY_PGUP, KEY_DEL, KEY_END, KEY_PGDN,
        /* 0x4F */ KEY_RIGHTARROW, KEY_LEFTARROW, KEY_DOWNARROW, KEY_UPARROW,
    };
    /* The left hand's keys as well (the owner's request, 2026-11-19): W, A,
     * S and D are the arrows, Q and E step sideways. What they type, in the
     * name of a saved game or a cheat code, is still the letter. */
    switch (usage) {
    case 0x1A: /* W */
        return KEY_UPARROW;
    case 0x16: /* S */
        return KEY_DOWNARROW;
    case 0x04: /* A */
        return KEY_LEFTARROW;
    case 0x07: /* D */
        return KEY_RIGHTARROW;
    case 0x14: /* Q */
        return KEY_STRAFE_L;
    case 0x08: /* E */
        return KEY_STRAFE_R;
    default:
        break;
    }
    if (usage >= 0x04 && usage <= 0x1D) {
        return 'a' + usage - 0x04;
    }
    if (usage >= 0x1E && usage <= 0x26) {
        return '1' + usage - 0x1E;
    }
    if (usage == 0x27) {
        return '0';
    }
    if (usage >= 0x28 && usage < 0x28 + (int)sizeof(kRow)) {
        return kRow[usage - 0x28];
    }
    switch (usage) {
    case 0xE0:
    case 0xE4:
        return KEY_FIRE;
    case 0xE1:
    case 0xE5:
        return KEY_RSHIFT;
    case 0xE2:
    case 0xE6:
        return KEY_RALT;
    case 0x58: /* the number pad's Enter */
        return KEY_ENTER;
    default:
        return 0;
    }
}

/* What the key types, for the names of saved games and the cheat codes. */
static int typed_char(int usage, int shift)
{
    static const char kPlain[] = "\r\0\0\0 -=[]\\\0;'`,./";
    static const char kShifted[] = "\r\0\0\0 _+{}|\0:\"~<>?";
    static const char kDigits[] = ")!@#$%^&*(";
    if (usage >= 0x04 && usage <= 0x1D) {
        return (shift ? 'A' : 'a') + usage - 0x04;
    }
    if (usage >= 0x1E && usage <= 0x27) {
        const int digit = usage == 0x27 ? 0 : usage - 0x1E + 1;
        return shift ? kDigits[digit] : '0' + digit;
    }
    if (usage >= 0x28 && usage <= 0x38) {
        return shift ? kShifted[usage - 0x28] : kPlain[usage - 0x28];
    }
    return 0;
}

static unsigned char held[256]; /* by the engine's key: so that all can be let go */
static int shift_held;

void port_key(int usage, int pressed)
{
    event_t event;
    const int key = doom_key(usage);
    if (usage == 0xE1 || usage == 0xE5) {
        shift_held = pressed;
    }
    if (key == 0) {
        return;
    }
    held[key & 255] = (unsigned char)(pressed != 0);
    event.type = pressed ? ev_keydown : ev_keyup;
    event.data1 = key;
    event.data2 = pressed ? typed_char(usage, shift_held) : 0;
    event.data3 = 0;
    D_PostEvent(&event);
}

static int buttons_held;

/* The mouse turns, and does not walk: its movement away from and towards
 * the player, which the game of 1993 took as steps, is left out. */
void port_mouse(int dx, int dy, int buttons)
{
    event_t event;
    (void)dy;
    /* The system's bits are left, right, middle; so are the engine's. */
    buttons_held = buttons & 7;
    event.type = ev_mouse;
    event.data1 = buttons_held;
    event.data2 = dx * 8; /* about what the DOS game made of a mouse's counts */
    event.data3 = 0;
    D_PostEvent(&event);
}

void port_release_keys(void)
{
    event_t event;
    int key;
    for (key = 0; key < 256; key++) {
        if (held[key]) {
            held[key] = 0;
            event.type = ev_keyup;
            event.data1 = key;
            event.data2 = event.data3 = 0;
            D_PostEvent(&event);
        }
    }
    shift_held = 0;
    if (buttons_held != 0) {
        port_mouse(0, 0, 0);
    }
}

void I_GetEvent(void) {}
void I_InitInput(void) {}

/* ---- The way in ---------------------------------------------------------------- */

static char wad_path[128];
static char *arguments[] = {"doom", "-iwad", wad_path, NULL};

int port_start(const char *wad)
{
    snprintf(wad_path, sizeof(wad_path), "%s", wad);
    myargc = 3;
    myargv = arguments;
    if (setjmp(leave) != 0) {
        running = 0;
        return 0;
    }
    running = 1;
    D_DoomMain(); /* comes back when the first picture is made */
    running = 0;
    return 1;
}

int port_tick(void)
{
    if (ended) {
        return 0;
    }
    if (setjmp(leave) != 0) {
        running = 0;
        return 0;
    }
    running = 1;
    doomgeneric_Tick();
    running = 0;
    return 1;
}
