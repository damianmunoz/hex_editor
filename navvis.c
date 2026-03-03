/*
 * NavVis - Navegador y Visor de Archivos
 * Sistemas Operativos
 *
 * Compilar: gcc -o navvis navvis.c -lncurses
 * Uso: ./navvis [directorio]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <curses.h>
#include <errno.h>

/* ─── Constantes ─────────────────────────────────────────── */
#define MAX_ENTRIES   4096
#define MAX_PATH      4096
#define BYTES_PER_ROW 16
#define MAX_LINES     1000000

/* ─── Pares de color ──────────────────────────────────────── */
#define COL_HEADER  1
#define COL_DIR     2
#define COL_FILE    3
#define COL_SELECT  4
#define COL_STATUS  5
#define COL_KEYS    6

/* ─── Estructura de entrada ───────────────────────────────── */
typedef struct {
    char  name[NAME_MAX + 1];
    int   is_dir;
    off_t size;
    time_t mtime;
} Entry;

/* ─── Prototipos ──────────────────────────────────────────── */
void navigator(const char *start_dir);
void hex_viewer(const char *filepath);
void text_viewer(const char *filepath);
int  entry_cmp(const void *a, const void *b);
void draw_status_bar(const char *msg);
void draw_key_bar(const char **keys, int n);
void format_size(off_t size, char *buf, size_t bufsz);

/* ════════════════════════════════════════════════════════════
 *  UTILIDADES
 * ════════════════════════════════════════════════════════════ */

int entry_cmp(const void *a, const void *b) {
    const Entry *ea = (const Entry *)a;
    const Entry *eb = (const Entry *)b;
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return  1;
    return strcmp(ea->name, eb->name);
}

void format_size(off_t size, char *buf, size_t bufsz) {
    if (size < 1024)
        snprintf(buf, bufsz, "%lld B", (long long)size);
    else if (size < 1024*1024)
        snprintf(buf, bufsz, "%.1f K", size/1024.0);
    else if (size < 1024LL*1024*1024)
        snprintf(buf, bufsz, "%.1f M", size/(1024.0*1024));
    else
        snprintf(buf, bufsz, "%.1f G", size/(1024.0*1024*1024));
}

void draw_status_bar(const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    attron(COLOR_PAIR(COL_STATUS) | A_BOLD);
    mvhline(rows - 2, 0, ' ', cols);
    mvprintw(rows - 2, 1, "%s", msg);
    attroff(COLOR_PAIR(COL_STATUS) | A_BOLD);
}

void draw_key_bar(const char **keys, int n) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    mvhline(rows - 1, 0, ' ', cols);
    int x = 0;
    for (int i = 0; i < n && x + 9 <= cols; i++) {
        attron(COLOR_PAIR(COL_SELECT) | A_BOLD);
        mvprintw(rows - 1, x, "%d", i + 1);
        attroff(COLOR_PAIR(COL_SELECT) | A_BOLD);
        attron(COLOR_PAIR(COL_KEYS));
        mvprintw(rows - 1, x + 1, "%-7s", keys[i]);
        attroff(COLOR_PAIR(COL_KEYS));
        x += 9;
    }
}

/* ════════════════════════════════════════════════════════════
 *  NAVEGADOR
 * ════════════════════════════════════════════════════════════ */

void navigator(const char *start_dir) {
    char cwd[MAX_PATH];
    strncpy(cwd, start_dir, MAX_PATH - 1);
    cwd[MAX_PATH - 1] = '\0';

    Entry *entries = malloc(sizeof(Entry) * MAX_ENTRIES);
    if (!entries) return;

    int n_entries = 0, selected = 0, offset = 0;
    const char *nav_keys[] = {"Help","Texto","Hex","Salir","","","","","","Salir"};

    for (;;) {
        /* Leer directorio */
        n_entries = 0;
        DIR *dp = opendir(cwd);
        if (!dp) {
            draw_status_bar("No se puede abrir el directorio");
            refresh(); getch();
            free(entries);
            return;
        }
        struct dirent *de;
        while ((de = readdir(dp)) != NULL && n_entries < MAX_ENTRIES) {
            if (strcmp(de->d_name, ".") == 0) continue;
            char full[MAX_PATH];
            snprintf(full, MAX_PATH, "%s/%s", cwd, de->d_name);
            struct stat st;
            if (stat(full, &st) < 0) continue;
            strncpy(entries[n_entries].name, de->d_name, NAME_MAX);
            entries[n_entries].name[NAME_MAX] = '\0';
            entries[n_entries].is_dir = S_ISDIR(st.st_mode);
            entries[n_entries].size   = st.st_size;
            entries[n_entries].mtime  = st.st_mtime;
            n_entries++;
        }
        closedir(dp);
        qsort(entries, n_entries, sizeof(Entry), entry_cmp);

        if (selected >= n_entries) selected = n_entries > 0 ? n_entries - 1 : 0;
        if (selected < 0) selected = 0;

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int list_rows = rows - 4;

        if (selected < offset) offset = selected;
        if (selected >= offset + list_rows) offset = selected - list_rows + 1;

        clear();

        /* Cabecera */
        attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvhline(0, 0, ' ', cols);
        mvprintw(0, 1, " NavVis  [%s]", cwd);
        attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);

        attron(COLOR_PAIR(COL_HEADER));
        mvhline(1, 0, ' ', cols);
        int name_width = cols - 32 > 10 ? cols - 32 : 10;
        mvprintw(1, 1, "%-*s %9s  %-17s", name_width, "Nombre", "Tamano", "Modificacion");
        attroff(COLOR_PAIR(COL_HEADER));

        /* Lista */
        for (int i = 0; i < list_rows && (i + offset) < n_entries; i++) {
            int idx = i + offset;
            Entry *e = &entries[idx];
            char size_str[16];
            if (e->is_dir) snprintf(size_str, sizeof(size_str), "     DIR");
            else format_size(e->size, size_str, sizeof(size_str));

            char time_str[20];
            struct tm *tm = localtime(&e->mtime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm);

            char name_buf[NAME_MAX + 3];
            snprintf(name_buf, sizeof(name_buf), "%s%s",
                     e->is_dir ? "/" : " ", e->name);

            if (idx == selected)      attron(COLOR_PAIR(COL_SELECT) | A_BOLD);
            else if (e->is_dir)       attron(COLOR_PAIR(COL_DIR));
            else                      attron(COLOR_PAIR(COL_FILE));

            mvhline(2 + i, 0, ' ', cols);
            mvprintw(2 + i, 1, "%-*s %9s  %s", name_width, name_buf, size_str, time_str);

            if (idx == selected)      attroff(COLOR_PAIR(COL_SELECT) | A_BOLD);
            else if (e->is_dir)       attroff(COLOR_PAIR(COL_DIR));
            else                      attroff(COLOR_PAIR(COL_FILE));
        }

        /* Barra estado */
        char status[512];
        if (n_entries > 0) {
            Entry *e = &entries[selected];
            char sz[16];
            format_size(e->size, sz, sizeof(sz));
            snprintf(status, sizeof(status), "%d/%d  [%s]  %s",
                     selected + 1, n_entries,
                     e->is_dir ? "DIR" : sz, e->name);
        } else {
            snprintf(status, sizeof(status), "Directorio vacio");
        }
        draw_status_bar(status);
        draw_key_bar(nav_keys, 10);
        refresh();

        int c = getch();
        switch (c) {
            case KEY_UP:
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
                if (selected < n_entries - 1) selected++;
                break;
            case KEY_PPAGE:
                selected -= list_rows;
                if (selected < 0) selected = 0;
                break;
            case KEY_NPAGE:
                selected += list_rows;
                if (selected >= n_entries) selected = n_entries > 0 ? n_entries - 1 : 0;
                break;
            case KEY_HOME:
                selected = 0;
                break;
            case KEY_END:
                if (n_entries > 0) selected = n_entries - 1;
                break;
            case '\n': case KEY_RIGHT: {
                if (n_entries > 0) {
                    Entry *e = &entries[selected];
                    char full[MAX_PATH];
                    snprintf(full, MAX_PATH, "%s/%s", cwd, e->name);
                    if (e->is_dir) {
                        char resolved[MAX_PATH];
                        if (realpath(full, resolved)) {
                            strncpy(cwd, resolved, MAX_PATH - 1);
                        }
                        selected = 0; offset = 0;
                    } else {
                        text_viewer(full);
                    }
                }
                break;
            }
            case KEY_F(2): {
                if (n_entries > 0 && !entries[selected].is_dir) {
                    char full[MAX_PATH];
                    snprintf(full, MAX_PATH, "%s/%s", cwd, entries[selected].name);
                    text_viewer(full);
                }
                break;
            }
            case KEY_F(3): {
                if (n_entries > 0 && !entries[selected].is_dir) {
                    char full[MAX_PATH];
                    snprintf(full, MAX_PATH, "%s/%s", cwd, entries[selected].name);
                    hex_viewer(full);
                }
                break;
            }
            case KEY_LEFT: case KEY_BACKSPACE: case 127: {
                char parent[MAX_PATH];
                snprintf(parent, MAX_PATH, "%s/..", cwd);
                char resolved[MAX_PATH];
                if (realpath(parent, resolved)) {
                    strncpy(cwd, resolved, MAX_PATH - 1);
                }
                selected = 0; offset = 0;
                break;
            }
            case 'q': case 'Q': case KEY_F(10): case 27:
                free(entries);
                return;
        }
    }
}

/* ════════════════════════════════════════════════════════════
 *  VISOR HEXADECIMAL
 * ════════════════════════════════════════════════════════════ */

static void build_hex_line(const unsigned char *base, off_t off, off_t fsize,
                            char *buf, size_t bufsz) {
    int o = 0;
    o += snprintf(buf + o, bufsz - o, "%08llx ", (unsigned long long)off);
    for (int g = 0; g < 4; g++) {
        for (int b = 0; b < 4; b++) {
            off_t p = off + g*4 + b;
            if (p < fsize)
                o += snprintf(buf + o, bufsz - o, "%02x ", (unsigned char)base[p]);
            else
                o += snprintf(buf + o, bufsz - o, "   ");
        }
        if (o < (int)bufsz) buf[o++] = ' ';
    }
    for (int i = 0; i < BYTES_PER_ROW; i++) {
        off_t p = off + i;
        unsigned char ch = (p < fsize) ? (unsigned char)base[p] : ' ';
        if (o < (int)bufsz) buf[o++] = isprint(ch) ? ch : '.';
    }
    if (o < (int)bufsz) buf[o] = '\0';
}

void hex_viewer(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) { draw_status_bar("No se puede abrir el archivo"); getch(); return; }

    struct stat st;
    fstat(fd, &st);
    off_t fsize = st.st_size;

    unsigned char *map = NULL;
    if (fsize > 0) {
        map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { close(fd); draw_status_bar("Error mmap"); getch(); return; }
    }

    off_t row_offset = 0;
    const char *keys[] = {"Help","Texto","Salir","","Goto","","","","","Salir"};

    int rows, cols;
    for (;;) {
        getmaxyx(stdscr, rows, cols);
        int list_rows = rows - 3;
        off_t total_rows = fsize > 0 ? (fsize + BYTES_PER_ROW - 1) / BYTES_PER_ROW : 1;

        clear();

        attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvhline(0, 0, ' ', cols);
        double pct = fsize > 0 ? (double)(row_offset * BYTES_PER_ROW) * 100.0 / fsize : 0;
        mvprintw(0, 1, "%-*s  Linea %lld/%lld  %.0f%%",
                 cols - 28, filepath,
                 (long long)(row_offset + 1), (long long)total_rows, pct);
        attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);

        char linebuf[256];
        for (int i = 0; i < list_rows; i++) {
            off_t off = (row_offset + i) * BYTES_PER_ROW;
            if (off >= fsize && fsize > 0) break;
            build_hex_line(map ? map : (unsigned char *)"", off, fsize, linebuf, sizeof(linebuf));
            attron(COLOR_PAIR(COL_FILE));
            mvprintw(1 + i, 0, "%s", linebuf);
            attroff(COLOR_PAIR(COL_FILE));
        }

        char status[128];
        snprintf(status, sizeof(status),
                 "HEX | Offset: 0x%08llx  Tamano: %lld bytes | F2:Texto F5:Goto q:Salir",
                 (unsigned long long)(row_offset * BYTES_PER_ROW), (long long)fsize);
        draw_status_bar(status);
        draw_key_bar(keys, 10);
        refresh();

        int c = getch();
        switch (c) {
            case KEY_DOWN:
                if ((row_offset + 1) * BYTES_PER_ROW < fsize) row_offset++;
                break;
            case KEY_UP:
                if (row_offset > 0) row_offset--;
                break;
            case KEY_NPAGE:
                row_offset += list_rows;
                if (row_offset * BYTES_PER_ROW >= fsize)
                    row_offset = fsize > 0 ? (fsize - 1) / BYTES_PER_ROW : 0;
                break;
            case KEY_PPAGE:
                row_offset -= list_rows;
                if (row_offset < 0) row_offset = 0;
                break;
            case KEY_HOME: case 'g':
                row_offset = 0;
                break;
            case KEY_END: case 'G': {
                off_t last = fsize > 0 ? (fsize - 1) / BYTES_PER_ROW - list_rows + 1 : 0;
                row_offset = last < 0 ? 0 : last;
                break;
            }
            case KEY_F(5): case ':': {
                echo(); curs_set(1);
                draw_status_bar("Ir a offset (hex): ");
                refresh();
                char input[32] = {0};
                mvgetnstr(rows - 2, 20, input, sizeof(input) - 1);
                noecho(); curs_set(0);
                unsigned long long target = 0;
                if (sscanf(input, "%llx", &target) == 1) {
                    off_t trow = (off_t)(target / BYTES_PER_ROW);
                    off_t mrow = fsize > 0 ? (fsize - 1) / BYTES_PER_ROW : 0;
                    row_offset = trow > mrow ? mrow : trow;
                }
                break;
            }
            case KEY_F(2): case 't': case 'T':
                if (map && fsize > 0) munmap(map, fsize);
                close(fd);
                text_viewer(filepath);
                return;
            case 'q': case 'Q': case KEY_F(3): case KEY_F(10): case 27:
                goto done_hex;
        }
    }
done_hex:
    if (map && fsize > 0) munmap(map, fsize);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  VISOR DE TEXTO
 * ════════════════════════════════════════════════════════════ */

void text_viewer(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) { draw_status_bar("No se puede abrir el archivo"); getch(); return; }

    struct stat st;
    fstat(fd, &st);
    off_t fsize = st.st_size;

    char *map = NULL;
    if (fsize > 0) {
        map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { close(fd); draw_status_bar("Error mmap"); getch(); return; }
    }

    /* Índice de líneas */
    off_t *line_starts = malloc(sizeof(off_t) * MAX_LINES);
    if (!line_starts) {
        if (map) munmap(map, fsize);
        close(fd);
        return;
    }
    long n_lines = 0;
    line_starts[n_lines++] = 0;
    for (off_t i = 0; i < fsize && n_lines < MAX_LINES; i++) {
        if (map[i] == '\n' && i + 1 < fsize)
            line_starts[n_lines++] = i + 1;
    }
    if (n_lines == 0) n_lines = 1;

    long top_line = 0;
    int  wrap     = 1;
    const char *keys[] = {"Help","Wrap","Salir","Hex","Goto","","","","","Salir"};

    int rows, cols;
    for (;;) {
        getmaxyx(stdscr, rows, cols);
        int list_rows = rows - 3;

        clear();

        attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvhline(0, 0, ' ', cols);
        double pct = n_lines > 1 ? (double)top_line * 100.0 / (n_lines - 1) : 100.0;
        mvprintw(0, 1, "%-*s  %ld/%ld  %.0f%%",
                 cols - 20, filepath, top_line + 1, n_lines, pct);
        attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);

        for (int i = 0; i < list_rows; i++) {
            long ln = top_line + i;
            if (ln >= n_lines) break;
            off_t start = line_starts[ln];
            off_t end   = (ln + 1 < n_lines) ? line_starts[ln + 1] - 1 : fsize;
            int len = (int)(end - start);
            if (len < 0) len = 0;
            int show = (wrap && len > cols - 1) ? cols - 1 : len;
            attron(COLOR_PAIR(COL_FILE));
            for (int k = 0; k < show; k++) {
                char ch = map[start + k];
                if (ch == '\t') ch = ' ';
                if (!isprint((unsigned char)ch) && ch != ' ') ch = '.';
                mvaddch(1 + i, k, ch);
            }
            attroff(COLOR_PAIR(COL_FILE));
        }

        char status[256];
        snprintf(status, sizeof(status),
                 "TEXTO | Linea: %ld/%ld  Modo: %s | F2:Wrap F4:Hex F5:Goto q:Salir",
                 top_line + 1, n_lines, wrap ? "Ajuste" : "Sin ajuste");
        draw_status_bar(status);
        draw_key_bar(keys, 10);
        refresh();

        int c = getch();
        switch (c) {
            case KEY_DOWN:
                if (top_line + 1 < n_lines) top_line++;
                break;
            case KEY_UP:
                if (top_line > 0) top_line--;
                break;
            case KEY_NPAGE:
                top_line += list_rows;
                if (top_line >= n_lines) top_line = n_lines - 1;
                break;
            case KEY_PPAGE:
                top_line -= list_rows;
                if (top_line < 0) top_line = 0;
                break;
            case KEY_HOME: case 'g':
                top_line = 0;
                break;
            case KEY_END: case 'G': {
                long last = n_lines - list_rows;
                top_line = last < 0 ? 0 : last;
                break;
            }
            case KEY_F(2): case 'w': case 'W':
                wrap = !wrap;
                break;
            case KEY_F(5): case ':': {
                echo(); curs_set(1);
                draw_status_bar("Ir a linea: ");
                refresh();
                char input[32] = {0};
                mvgetnstr(rows - 2, 13, input, sizeof(input) - 1);
                noecho(); curs_set(0);
                long target = 0;
                if (sscanf(input, "%ld", &target) == 1 && target > 0) {
                    top_line = target - 1;
                    if (top_line >= n_lines) top_line = n_lines - 1;
                }
                break;
            }
            case KEY_F(4): case 'h': case 'H':
                free(line_starts);
                if (map) munmap(map, fsize);
                close(fd);
                hex_viewer(filepath);
                return;
            case 'q': case 'Q': case KEY_F(3): case KEY_F(10): case 27:
                goto done_text;
        }
    }
done_text:
    free(line_starts);
    if (map && fsize > 0) munmap(map, fsize);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    const char *start = (argc >= 2) ? argv[1] : ".";

    struct stat st;
    if (stat(start, &st) < 0 || !S_ISDIR(st.st_mode)) {
        /* Si el argumento es un archivo, abrirlo directamente */
        if (argc >= 2 && stat(start, &st) == 0 && S_ISREG(st.st_mode)) {
            initscr(); cbreak(); noecho();
            keypad(stdscr, TRUE); curs_set(0);
            if (has_colors()) {
                start_color(); use_default_colors();
                init_pair(COL_HEADER, COLOR_BLACK, COLOR_CYAN);
                init_pair(COL_DIR,    COLOR_CYAN,  -1);
                init_pair(COL_FILE,   COLOR_WHITE, -1);
                init_pair(COL_SELECT, COLOR_BLACK, COLOR_WHITE);
                init_pair(COL_STATUS, COLOR_BLACK, COLOR_CYAN);
                init_pair(COL_KEYS,   COLOR_BLACK, COLOR_WHITE);
            }
            text_viewer(start);
            endwin();
            return 0;
        }
        fprintf(stderr, "Error: '%s' no es un directorio o archivo valido\n", start);
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COL_HEADER, COLOR_BLACK,  COLOR_CYAN);
        init_pair(COL_DIR,    COLOR_CYAN,   -1);
        init_pair(COL_FILE,   COLOR_WHITE,  -1);
        init_pair(COL_SELECT, COLOR_BLACK,  COLOR_WHITE);
        init_pair(COL_STATUS, COLOR_BLACK,  COLOR_CYAN);
        init_pair(COL_KEYS,   COLOR_BLACK,  COLOR_WHITE);
    }

    char resolved[MAX_PATH];
    if (!realpath(start, resolved)) strncpy(resolved, start, MAX_PATH - 1);

    navigator(resolved);

    endwin();
    return 0;
}