/* Doom on Chinchilla (backlog item 81): what the engine is given in place of
 * the C library.
 *
 * The engine (third_party/doomgeneric, GPL 2) is built into doom.app without
 * a change to its files where that can be helped. A program kept in a file
 * reaches the system by name and may ask only for the OS calls and a short
 * list of the C library (tools/gen_exports.py): no files, no malloc, no
 * printf. This header is put in front of every file of the engine by the
 * compiler (-include) and gives those names to functions of ours (port.c),
 * which ask the system through doom.cpp. The PC builds the program the same
 * way, so its tests go through the same code as the board.
 */
#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h> /* before mkdir gets another meaning below */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- What doom.cpp does for the engine, with the OS calls ---- */

void *sys_allocate(size_t bytes); /* zeroed; NULL: there is none */
void sys_release(void *block);
/* Files by the system's handles; a path may use '/' or '\'. */
int sys_open(const char *path, int write); /* 0: it could not be opened */
long sys_read(int file, void *data, size_t size);
int sys_write(int file, const void *data, size_t size); /* 0: it failed */
int sys_seek(int file, uint32_t offset);
void sys_close(int file);
long sys_size(const char *path); /* -1: there is no such file */
int sys_remove(const char *path);
int sys_rename(const char *from, const char *to);
void sys_make_folder(const char *path);
uint32_t sys_ms(void);      /* since the machine started */
void sys_rest(uint32_t ms); /* lets the rest of the machine run */
void sys_log(const char *text);
/* The engine has a picture ready: 320 by 200 bytes, each a place in the
 * palette of 256 colours, three bytes each (red, green, blue). */
void sys_picture(const uint8_t *pixels, const uint8_t *palette, int palette_changed);

/* ---- What port.c does for doom.cpp ---- */

/* Starts the engine on this WAD file. 0: it gave up (port_error() says why). */
int port_start(const char *wad);
/* One round of the engine: the tics that are due, then a picture. 0: the
 * engine has ended, by the player's wish or with an error. */
int port_tick(void);
const char *port_error(void); /* NULL: it ended because the player quit */
/* A key by its USB HID usage; the mouse by how far it moved and its buttons. */
void port_key(int usage, int pressed);
void port_mouse(int dx, int dy, int buttons);
void port_release_keys(void); /* every key held is let go: the screen was taken away */
void port_show_again(void);   /* the next picture is handed over whole, palette too */

/* ---- The C library's names, ours ---- */

void *port_malloc(size_t bytes);
void *port_calloc(size_t count, size_t bytes);
void *port_realloc(void *block, size_t bytes);
void port_free(void *block);
char *port_strdup(const char *text);
void port_free_all(void); /* everything port_malloc gave and nobody returned */

FILE *port_fopen(const char *path, const char *mode);
int port_fclose(FILE *file);
size_t port_fread(void *data, size_t size, size_t count, FILE *file);
size_t port_fwrite(const void *data, size_t size, size_t count, FILE *file);
int port_fseek(FILE *file, long offset, int whence);
long port_ftell(FILE *file);
int port_feof(FILE *file);
int port_fflush(FILE *file);
int port_fprintf(FILE *file, const char *format, ...) __attribute__((format(printf, 2, 3)));
int port_vfprintf(FILE *file, const char *format, va_list arguments);
int port_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int port_puts(const char *text);
int port_putchar(int c);
/* Only what the settings file is read with: a word, then the rest of the line. */
int port_fscanf(FILE *file, const char *format, char *word, char *rest);
int port_remove(const char *path);
int port_rename(const char *from, const char *to);
void port_exit(int code) __attribute__((noreturn));
double port_atof(const char *text);
int port_toupper(int c);
int port_tolower(int c);
int port_isspace(int c);
int port_isprint(int c);

#ifdef __cplusplus
}
#endif

#ifndef PORT_ITSELF /* port.c and doom.cpp use the real names where they must */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#define malloc port_malloc
#define calloc port_calloc
#define realloc port_realloc
#define free port_free
#define strdup port_strdup

#undef stdout
#undef stderr
#define stdout ((FILE *)1) /* both go to the system's log */
#define stderr ((FILE *)2)
#undef feof
#undef putchar
#define fopen port_fopen
#define fclose port_fclose
#define fread port_fread
#define fwrite port_fwrite
#define fseek port_fseek
#define ftell port_ftell
#define feof port_feof
#define fflush port_fflush
#define fprintf port_fprintf
#define vfprintf port_vfprintf
#define printf port_printf
#define puts port_puts
#define putchar port_putchar
#define fscanf port_fscanf
#define remove port_remove
#define rename port_rename
#define exit port_exit
#define atof port_atof
#define getenv(name) ((char *)0)
#define mkdir(path, ...) sys_make_folder(path)

#undef toupper
#undef tolower
#undef isspace
#undef isprint
#define toupper port_toupper
#define tolower port_tolower
#define isspace port_isspace
#define isprint port_isprint
#endif
