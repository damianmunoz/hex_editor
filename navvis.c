/*
2 * NavVis - Navegador y Visor de Archivos
3 * Sistemas Operativos
4 *
5 * Compilar: gcc -o navvis navvis.c -lncurses
6 * Uso:      ./navvis [directorio]
7 *
8 * CONTROLES NAVEGADOR:
9 *   Flechas Arriba/Abajo  — seleccionar archivo
10 *   Enter                 — entrar a directorio / abrir visor
11 *   Flecha Izquierda / u  — subir directorio padre
12 *   q / ESC               — salir
13 *
14 * CONTROLES VISOR (texto y hex):
15 *   Flechas Arriba/Abajo  — línea anterior / siguiente
16 *   PgUp / PgDn           — página anterior / siguiente
17 *   < o Home              — inicio del archivo
18 *   > o End               — final del archivo
19 *   g                     — ir a línea/offset específico
20 *   t / h                 — alternar entre vista Texto y Hex
21 *   q / ESC               — volver al navegador
22 */
23
24#include <stdio.h>
25#include <stdlib.h>
26#include <string.h>
27#include <unistd.h>
28#include <fcntl.h>
29#include <ctype.h>
30#include <dirent.h>
31#include <sys/stat.h>
32#include <sys/mman.h>
33#include <time.h>
34#include <curses.h>
35
36/* ─── Constantes ──────────────────────────────────────────── */
37#define MAX_ENTRIES   4096
38#define MAX_PATH      4096
39#define BYTES_PER_ROW 16
40#define MAX_LINES     1000000
41
42/* ─── Pares de color ──────────────────────────────────────── */
43#define COL_HEADER 1
44#define COL_DIR    2
45#define COL_FILE   3
46#define COL_SELECT 4
47#define COL_STATUS 5
48#define COL_KEYS   6
49
50typedef struct {
51    char  name[NAME_MAX + 1];
52    int   is_dir;
53    off_t size;
54    time_t mtime;
55} Entry;
56
57/* ─── Prototipos ──────────────────────────────────────────── */
58void navigator(const char *start_dir);
59void viewer(const char *filepath, int start_in_hex);
60int  entry_cmp(const void *a, const void *b);
61void format_size(off_t size, char *buf, size_t bufsz);
62void draw_header(const char *left, const char *right);
63void draw_status(const char *msg);
64void draw_keys(const char *keys[], int n);
65
66/* ════════════════════════════════════════════════════════════
67 *  UTILIDADES
68 * ════════════════════════════════════════════════════════════ */
69
70int entry_cmp(const void *a, const void *b) {
71    const Entry *ea = a, *eb = b;
72    if (ea->is_dir && !eb->is_dir) return -1;
73    if (!ea->is_dir && eb->is_dir) return  1;
74    return strcmp(ea->name, eb->name);
75}
76
77void format_size(off_t sz, char *buf, size_t n) {
78    if      (sz < 1024)           snprintf(buf, n, "%lld B",  (long long)sz);
79    else if (sz < 1024*1024)      snprintf(buf, n, "%.1fK",   sz/1024.0);
80    else if (sz < 1024LL*1024*1024) snprintf(buf, n, "%.1fM", sz/(1024.0*1024));
81    else                          snprintf(buf, n, "%.1fG",   sz/(1024.0*1024*1024));
82}
83
84void draw_header(const char *left, const char *right) {
85    int rows, cols; getmaxyx(stdscr, rows, cols); (void)rows;
86    attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
87    mvhline(0, 0, ' ', cols);
88    mvprintw(0, 1, "%s", left);
89    if (right) mvprintw(0, cols - (int)strlen(right) - 1, "%s", right);
90    attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);
91}
92
93void draw_status(const char *msg) {
94    int rows, cols; getmaxyx(stdscr, rows, cols);
95    attron(COLOR_PAIR(COL_STATUS) | A_BOLD);
96    mvhline(rows - 2, 0, ' ', cols);
97    mvprintw(rows - 2, 1, "%s", msg);
98    attroff(COLOR_PAIR(COL_STATUS) | A_BOLD);
99}
100
101void draw_keys(const char *keys[], int n) {
102    int rows, cols; getmaxyx(stdscr, rows, cols);
103    attron(COLOR_PAIR(COL_KEYS));
104    mvhline(rows - 1, 0, ' ', cols);
105    int x = 0;
106    for (int i = 0; i < n && x + 2 < cols; i++) {
107        attroff(COLOR_PAIR(COL_KEYS));
108        attron(COLOR_PAIR(COL_SELECT) | A_BOLD);
109        mvprintw(rows - 1, x, "%s", keys[i*2]);
110        attroff(COLOR_PAIR(COL_SELECT) | A_BOLD);
111        attron(COLOR_PAIR(COL_KEYS));
112        x += strlen(keys[i*2]);
113        mvprintw(rows - 1, x, "%-8s", keys[i*2+1]);
114        x += 8;
115    }
116    attroff(COLOR_PAIR(COL_KEYS));
117}
118
119/* ════════════════════════════════════════════════════════════
120 *  NAVEGADOR
121 * ════════════════════════════════════════════════════════════ */
122
123void navigator(const char *start_dir) {
124    char cwd[MAX_PATH];
125    strncpy(cwd, start_dir, MAX_PATH - 1);
126
127    Entry *entries = malloc(sizeof(Entry) * MAX_ENTRIES);
128    if (!entries) return;
129
130    int n = 0, sel = 0, off = 0;
131
132    /* Teclas: par (etiqueta, descripción) */
133    const char *keys[] = {
134        "Enter","Abrir",  "u/←","Subir",  "q","Salir"
135    };
136
137    for (;;) {
138        /* ── Leer directorio ── */
139        n = 0;
140        DIR *dp = opendir(cwd);
141        if (!dp) { draw_status("No se puede abrir el directorio"); getch(); break; }
142        struct dirent *de;
143        while ((de = readdir(dp)) && n < MAX_ENTRIES) {
144            if (strcmp(de->d_name, ".") == 0) continue;
145            char full[MAX_PATH];
146            snprintf(full, sizeof(full) - 1, "%s/%s", cwd, de->d_name);
147            struct stat st;
148            if (stat(full, &st) < 0) continue;
149            strncpy(entries[n].name, de->d_name, NAME_MAX);
150            entries[n].is_dir = S_ISDIR(st.st_mode);
151            entries[n].size   = st.st_size;
152            entries[n].mtime  = st.st_mtime;
153            n++;
154        }
155        closedir(dp);
156        qsort(entries, n, sizeof(Entry), entry_cmp);
157
158        if (sel >= n) sel = n > 0 ? n - 1 : 0;
159
160        /* ── Dibujar ── */
161        int rows, cols;
162        getmaxyx(stdscr, rows, cols);
163        int list_rows = rows - 4;   /* header(2) + status(1) + keys(1) */
164
165        if (sel < off)              off = sel;
166        if (sel >= off + list_rows) off = sel - list_rows + 1;
167
168        clear();
169
170        /* Título */
171        char right[32]; snprintf(right, sizeof(right), "%d entradas", n);
172        draw_header(cwd, right);
173
174        /* Columnas */
175        attron(COLOR_PAIR(COL_HEADER));
176        mvhline(1, 0, ' ', cols);
177        int nw = cols - 30 > 12 ? cols - 30 : 12;
178        mvprintw(1, 1, "%-*s %9s  %-17s", nw, "Nombre", "Tamano", "Modificacion");
179        attroff(COLOR_PAIR(COL_HEADER));
180
181        for (int i = 0; i < list_rows && (i + off) < n; i++) {
182            int idx = i + off;
183            Entry *e = &entries[idx];
184
185            char sz[16];
186            if (e->is_dir) snprintf(sz, sizeof(sz), "     DIR");
187            else           format_size(e->size, sz, sizeof(sz));
188
189            char ts[20];
190            struct tm *tm = localtime(&e->mtime);
191            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
192
193            char nm[NAME_MAX + 3];
194            snprintf(nm, sizeof(nm), "%s%s", e->is_dir ? "/" : " ", e->name);
195
196            if (idx == sel)    attron(COLOR_PAIR(COL_SELECT) | A_BOLD);
197            else if (e->is_dir) attron(COLOR_PAIR(COL_DIR));
198            else               attron(COLOR_PAIR(COL_FILE));
199
200            mvhline(2 + i, 0, ' ', cols);
201            mvprintw(2 + i, 1, "%-*s %9s  %s", nw, nm, sz, ts);
202
203            if (idx == sel)    attroff(COLOR_PAIR(COL_SELECT) | A_BOLD);
204            else if (e->is_dir) attroff(COLOR_PAIR(COL_DIR));
205            else               attroff(COLOR_PAIR(COL_FILE));
206        }
207
208        char status[256];
209        if (n > 0) {
210            char sz[16]; format_size(entries[sel].size, sz, sizeof(sz));
211            snprintf(status, sizeof(status), "%d/%d  %s  %s",
212                     sel+1, n, entries[sel].is_dir ? "[DIR]" : sz, entries[sel].name);
213        } else {
214            snprintf(status, sizeof(status), "Directorio vacio");
215        }
216        draw_status(status);
217        draw_keys(keys, 3);
218        refresh();
219
220        /* ── Input ── */
221        int c = getch();
222        switch (c) {
223            case KEY_UP:
224                if (sel > 0) sel--;
225                break;
226            case KEY_DOWN:
227                if (sel < n - 1) sel++;
228                break;
229            case KEY_PPAGE:
230                sel -= list_rows; if (sel < 0) sel = 0;
231                break;
232            case KEY_NPAGE:
233                sel += list_rows; if (sel >= n) sel = n > 0 ? n-1 : 0;
234                break;
235            case KEY_HOME:
236                sel = 0;
237                break;
238            case KEY_END:
239                if (n > 0) sel = n - 1;
240                break;
241            case '\n': case KEY_RIGHT: {
242                if (n <= 0) break;
243                Entry *e = &entries[sel];
244                char full[MAX_PATH];
245                snprintf(full, sizeof(full) - 1, "%s/%s", cwd, e->name);
246                if (e->is_dir) {
247                    char resolved[MAX_PATH];
248                    if (realpath(full, resolved)) strncpy(cwd, resolved, MAX_PATH-1);
249                    sel = 0; off = 0;
250                } else {
251                    viewer(full, 0);   /* abre en texto por defecto */
252                }
253                break;
254            }
255            case 'u': case KEY_LEFT: case KEY_BACKSPACE: case 127: {
256                char parent[MAX_PATH];
257                snprintf(parent, sizeof(parent) - 1, "%s/..", cwd);
258                char resolved[MAX_PATH];
259                if (realpath(parent, resolved)) strncpy(cwd, resolved, MAX_PATH-1);
260                sel = 0; off = 0;
261                break;
262            }
263            case 'q': case 'Q': case 27:
264                free(entries);
265                return;
266        }
267    }
268    free(entries);
269}
270
271/* ════════════════════════════════════════════════════════════
272 *  VISOR  (texto + hex en la misma función, toggle con t/h)
273 * ════════════════════════════════════════════════════════════ */
274
275/* Construye una línea hexadecimal de 16 bytes */
276static void hex_line(const unsigned char *base, off_t row_off, off_t fsize,
277                     char *buf, size_t bufsz) {
278    int o = 0;
279    o += snprintf(buf+o, bufsz-o, "%08llx ", (unsigned long long)row_off);
280    for (int g = 0; g < 4; g++) {
281        for (int b = 0; b < 4; b++) {
282            off_t p = row_off + g*4 + b;
283            if (p < fsize) o += snprintf(buf+o, bufsz-o, "%02x ", (unsigned char)base[p]);
284            else           o += snprintf(buf+o, bufsz-o, "   ");
285        }
286        if (o < (int)bufsz) buf[o++] = ' ';
287    }
288    for (int i = 0; i < BYTES_PER_ROW; i++) {
289        off_t p = row_off + i;
290        unsigned char ch = p < fsize ? (unsigned char)base[p] : ' ';
291        if (o < (int)bufsz) buf[o++] = isprint(ch) ? ch : '.';
292    }
293    if (o < (int)bufsz) buf[o] = '\0';
294}
295
296void viewer(const char *filepath, int start_in_hex) {
297    /* ── Abrir y mapear ── */
298    int fd = open(filepath, O_RDONLY);
299    if (fd < 0) { draw_status("No se puede abrir el archivo"); getch(); return; }
300
301    struct stat st;
302    fstat(fd, &st);
303    off_t fsize = st.st_size;
304
305    unsigned char *map = NULL;
306    if (fsize > 0) {
307        map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
308        if (map == MAP_FAILED) {
309            close(fd); draw_status("Error al mapear el archivo"); getch(); return;
310        }
311    }
312
313    /* ── Índice de líneas para modo texto ── */
314    off_t *line_off = malloc(sizeof(off_t) * MAX_LINES);
315    if (!line_off) { if (map) munmap(map, fsize); close(fd); return; }
316
317    long n_lines = 0;
318    line_off[n_lines++] = 0;
319    for (off_t i = 0; i < fsize && n_lines < MAX_LINES; i++)
320        if (map && map[i] == '\n' && i + 1 < fsize)
321            line_off[n_lines++] = i + 1;
322    if (n_lines == 0) n_lines = 1;
323
324    int hex_mode = start_in_hex;   /* 0 = texto, 1 = hex */
325    long top     = 0;              /* primera fila visible (línea o fila hex) */
326
327    /* Teclas del visor:
328     *   ↑↓ PgUp PgDn — navegar
329     *   < / Home     — inicio
330     *   > / End      — final
331     *   g            — ir a posición
332     *   t            — modo texto
333     *   h            — modo hex
334     *   q / ESC      — volver
335     */
336    const char *keys_txt[] = {
337        "↑↓PgUpDn","Navegar", "</>","Inicio/Fin", "g","Ir a...", "h","Hex", "q","Salir"
338    };
339    const char *keys_hex[] = {
340        "↑↓PgUpDn","Navegar", "</>","Inicio/Fin", "g","Ir a...", "t","Texto", "q","Salir"
341    };
342
343    int rows, cols;
344    for (;;) {
345        getmaxyx(stdscr, rows, cols);
346        int list_rows = rows - 3;   /* header(1) + status(1) + keys(1) */
347
348        /* ── Calcular total de filas según modo ── */
349        long total;
350        if (hex_mode) {
351            total = fsize > 0 ? (fsize + BYTES_PER_ROW - 1) / BYTES_PER_ROW : 1;
352        } else {
353            total = n_lines;
354        }
355        if (top >= total) top = total - 1;
356        if (top < 0)      top = 0;
357
358        clear();
359
360        /* Cabecera */
361        char right[48];
362        double pct = total > 1 ? (double)top * 100.0 / (total - 1) : 100.0;
363        snprintf(right, sizeof(right), "[%s]  %ld/%ld  %.0f%%",
364                 hex_mode ? "HEX" : "TEXTO", top + 1, total, pct);
365        draw_header(filepath, right);
366
367        /* Contenido */
368        if (hex_mode) {
369            char linebuf[256];
370            for (int i = 0; i < list_rows; i++) {
371                off_t row_off = (top + i) * BYTES_PER_ROW;
372                if (row_off >= fsize && fsize > 0) break;
373                hex_line(map, row_off, fsize, linebuf, sizeof(linebuf));
374                attron(COLOR_PAIR(COL_FILE));
375                mvprintw(1 + i, 0, "%s", linebuf);
376                attroff(COLOR_PAIR(COL_FILE));
377            }
378        } else {
379            for (int i = 0; i < list_rows; i++) {
380                long ln = top + i;
381                if (ln >= n_lines) break;
382                off_t start = line_off[ln];
383                off_t end   = (ln + 1 < n_lines) ? line_off[ln+1] - 1 : fsize;
384                int len = (int)(end - start);
385                if (len < 0) len = 0;
386                int show = len < cols ? len : cols - 1;
387                attron(COLOR_PAIR(COL_FILE));
388                for (int k = 0; k < show; k++) {
389                    char ch = map[start + k];
390                    if (ch == '\t') ch = ' ';
391                    if (!isprint((unsigned char)ch) && ch != ' ') ch = '.';
392                    mvaddch(1 + i, k, ch);
393                }
394                attroff(COLOR_PAIR(COL_FILE));
395            }
396        }
397
398        /* Barra de estado */
399        char status[256];
400        if (hex_mode) {
401            snprintf(status, sizeof(status),
402                     "Offset: 0x%08llx  |  Tamano: %lld bytes  |  t:Texto  g:Ir a offset  q:Salir",
403                     (unsigned long long)(top * BYTES_PER_ROW), (long long)fsize);
404        } else {
405            snprintf(status, sizeof(status),
406                     "Linea: %ld/%ld  |  Tamano: %lld bytes  |  h:Hex  g:Ir a linea  q:Salir",
407                     top + 1, n_lines, (long long)fsize);
408        }
409        draw_status(status);
410        draw_keys(hex_mode ? keys_hex : keys_txt, 5);
411        refresh();
412
413        /* ── Input ── */
414        int c = getch();
415        switch (c) {
416            /* Navegar línea a línea */
417            case KEY_UP:
418                if (top > 0) top--;
419                break;
420            case KEY_DOWN:
421                if (top + 1 < total) top++;
422                break;
423
424            /* Página */
425            case KEY_PPAGE:
426                top -= list_rows; if (top < 0) top = 0;
427                break;
428            case KEY_NPAGE:
429                top += list_rows;
430                if (top >= total) top = total - 1;
431                break;
432
433            /* Inicio del archivo */
434            case '<': case KEY_HOME:
435                top = 0;
436                break;
437
438            /* Final del archivo */
439            case '>': case KEY_END:
440                top = total - list_rows;
441                if (top < 0) top = 0;
442                break;
443
444            /* Ir a posición */
445            case 'g': case 'G': {
446                echo(); curs_set(1);
447                if (hex_mode) {
448                    draw_status("Ir a offset (hex, ej: 1A2F): ");
449                } else {
450                    draw_status("Ir a linea: ");
451                }
452                refresh();
453                char input[32] = {0};
454                mvgetnstr(rows - 2, hex_mode ? 30 : 13, input, sizeof(input) - 1);
455                noecho(); curs_set(0);
456
457                if (hex_mode) {
458                    unsigned long long target = 0;
459                    if (sscanf(input, "%llx", &target) == 1) {
460                        long trow = (long)(target / BYTES_PER_ROW);
461                        top = trow >= total ? total - 1 : trow;
462                    }
463                } else {
464                    long target = 0;
465                    if (sscanf(input, "%ld", &target) == 1 && target > 0) {
466                        top = target - 1;
467                        if (top >= n_lines) top = n_lines - 1;
468                    }
469                }
470                break;
471            }
472
473            /* Alternar modos */
474            case 't': case 'T':
475                if (hex_mode) {
476                    /* convertir fila hex → línea de texto aproximada */
477                    off_t byte_pos = top * BYTES_PER_ROW;
478                    long best = 0;
479                    for (long i = 0; i < n_lines; i++)
480                        if (line_off[i] <= byte_pos) best = i;
481                    top = best;
482                    hex_mode = 0;
483                }
484                break;
485            case 'h': case 'H':
486                if (!hex_mode) {
487                    /* convertir línea de texto → fila hex */
488                    off_t byte_pos = line_off[top];
489                    top = byte_pos / BYTES_PER_ROW;
490                    hex_mode = 1;
491                }
492                break;
493
494            /* Salir */
495            case 'q': case 'Q': case 27:
496                goto done;
497        }
498    }
499done:
500    free(line_off);
501    if (map && fsize > 0) munmap(map, fsize);
502    close(fd);
503}
504
505/* ════════════════════════════════════════════════════════════
506 *  MAIN
507 * ════════════════════════════════════════════════════════════ */
508int main(int argc, char *argv[]) {
509    const char *start = argc >= 2 ? argv[1] : ".";
510
511    struct stat st;
512    int is_dir = stat(start, &st) == 0 && S_ISDIR(st.st_mode);
513    int is_file = stat(start, &st) == 0 && S_ISREG(st.st_mode);
514
515    if (!is_dir && !is_file) {
516        fprintf(stderr, "Uso: %s [directorio|archivo]\n", argv[0]);
517        return 1;
518    }
519
520    /* Inicializar ncurses */
521    initscr();
522    cbreak();
523    noecho();
524    keypad(stdscr, TRUE);
525    curs_set(0);
526
527    if (has_colors()) {
528        start_color();
529        use_default_colors();
530        init_pair(COL_HEADER, COLOR_BLACK, COLOR_CYAN);
531        init_pair(COL_DIR,    COLOR_CYAN,  -1);
532        init_pair(COL_FILE,   COLOR_WHITE, -1);
533        init_pair(COL_SELECT, COLOR_BLACK, COLOR_WHITE);
534        init_pair(COL_STATUS, COLOR_BLACK, COLOR_CYAN);
535        init_pair(COL_KEYS,   COLOR_BLACK, COLOR_WHITE);
536    }
537
538    if (is_file) {
539        viewer(start, 0);
540    } else {
541        char resolved[MAX_PATH];
542        if (!realpath(start, resolved)) strncpy(resolved, start, MAX_PATH - 1);
543        navigator(resolved);
544    }
545
546    endwin();
547    return 0;
548}
