/* File browser and open/save dialog. A folder is read once when it is
 * entered (or reloaded) into a buffer the browser owns; sorting and
 * filtering keep an index that is rebuilt only when the listing, the sort or
 * the filter changes; the list builds only the rows in view. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "cui_internal.h"
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__wasm__) && !defined(__EMSCRIPTEN__) && !defined(CUI_NO_FILESYSTEM)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define cui__fs cui__fsz

#if defined(_WIN32)
#define CUI__SEP '\\'
#else
#define CUI__SEP '/'
#endif
#define CUI__FB_PATH 1024
#define CUI__FB_HIST 16
#define CUI__FB_ROW  30.0f

/* ------------------------------------------------------------------------ */
/* Listing a folder on the local disk                                       */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)
int cui_fs_list(void *user, const char *path, cui_file_entry *out, int cap, char *names, size_t names_cap, bool hidden) {
    (void)user;
    wchar_t wpath[CUI__FB_PATH + 4];
    int n = MultiByteToWideChar(CP_UTF8, 0, path && *path ? path : ".", -1, wpath, CUI__FB_PATH);
    if (n <= 0) return -1;
    n--;   /* without the terminator */
    if (n > 0 && wpath[n - 1] != L'\\' && wpath[n - 1] != L'/') wpath[n++] = L'\\';
    wpath[n++] = L'*'; wpath[n] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpath, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int count = 0;
    size_t used = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (!hidden && ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || fd.cFileName[0] == L'.')) continue;
        if (count >= cap) break;
        int len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, names + used, (int)(names_cap - used), NULL, NULL);
        if (len <= 0) break;   /* out of name space */
        cui_file_entry *e = &out[count++];
        e->name = names + used;
        used += (size_t)len;
        e->dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->size = e->dir ? 0 : ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        uint64_t ft = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        e->modified = ft ? (int64_t)(ft / 10000000ull) - 11644473600ll : 0;   /* 100 ns since 1601 to seconds since 1970 */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}
#elif defined(__wasm__) || defined(__EMSCRIPTEN__) || defined(CUI_NO_FILESYSTEM)
int cui_fs_list(void *user, const char *path, cui_file_entry *out, int cap, char *names, size_t names_cap, bool hidden) {
    (void)user; (void)path; (void)out; (void)cap; (void)names; (void)names_cap; (void)hidden;
    return -1;   /* no local disk */
}
#else
int cui_fs_list(void *user, const char *path, cui_file_entry *out, int cap, char *names, size_t names_cap, bool hidden) {
    (void)user;
    const char *dir = path && *path ? path : ".";
    DIR *d = opendir(dir);
    if (!d) return -1;
    int count = 0;
    size_t used = 0, dl = strlen(dir);
    char full[4096];
    struct dirent *de;
    while ((de = readdir(d)) != NULL && count < cap) {
        const char *nm = de->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
        if (!hidden && nm[0] == '.') continue;
        size_t nl = strlen(nm);
        if (used + nl + 1 > names_cap) break;
        cui_file_entry *e = &out[count];
        memcpy(names + used, nm, nl + 1);
        e->name = names + used;
        used += nl + 1;
        e->dir = false; e->size = 0; e->modified = 0;
        if (dl + nl + 2 < sizeof full) {
            memcpy(full, dir, dl);
            size_t k = dl;
            if (k && full[k - 1] != '/') full[k++] = '/';
            memcpy(full + k, nm, nl + 1);
            struct stat st;
            if (stat(full, &st) == 0) {
                e->dir = S_ISDIR(st.st_mode);
                e->size = e->dir ? 0 : (uint64_t)st.st_size;
                e->modified = (int64_t)st.st_mtime;
            }
        }
        count++;
    }
    closedir(d);
    return count;
}
#endif

/* ------------------------------------------------------------------------ */
/* Paths                                                                    */
/* ------------------------------------------------------------------------ */

static bool cui__is_sep(char c) { return c == '/' || c == '\\'; }

/* the parent of path in place; false at the root */
static bool cui__path_up(char *path) {
    size_t n = strlen(path);
    while (n > 1 && cui__is_sep(path[n - 1])) n--;   /* trailing separators */
    size_t k = n;
    while (k > 0 && !cui__is_sep(path[k - 1])) k--;
    if (k == 0) return false;
    if (k == 1 || (k == 3 && path[1] == ':')) { path[k] = 0; return k < n; }   /* "/" or "C:\" */
    path[k - 1] = 0;
    return true;
}

static void cui__path_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    bool sep = dl > 0 && !cui__is_sep(dir[dl - 1]);
    /* the separator the path already uses ("/w" stays "/w/a" on Windows too), else the system's */
    char c = strchr(dir, '/') ? '/' : strchr(dir, '\\') ? '\\' : CUI__SEP;
    snprintf(out, cap, "%s%s%s", dir, sep ? (char[2]){ c, 0 } : "", name);
}

/* a pattern list like "*.png;*.jpg": case-insensitive, * and ? only */
static bool cui__glob1(const char *p, const char *pe, const char *s) {
    while (p < pe) {
        if (*p == '*') {
            p++;
            if (p == pe) return true;
            for (; *s; s++) if (cui__glob1(p, pe, s)) return true;
            return false;
        }
        if (!*s) return false;
        char a = *p, b = *s;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != '?' && a != b) return false;
        p++; s++;
    }
    return !*s;
}
static bool cui__glob(const char *list, const char *name) {
    if (!list || !*list) return true;
    for (const char *p = list; *p;) {
        while (*p == ';' || *p == ' ' || *p == ',') p++;
        const char *e = p;
        while (*e && *e != ';' && *e != ',') e++;
        const char *t = e;
        while (t > p && t[-1] == ' ') t--;
        if (t > p && cui__glob1(p, t, name)) return true;
        p = e;
    }
    return false;
}

static bool cui__contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t k = 0;
        while (k < nl && hay[k]) {
            char a = hay[k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            k++;
        }
        if (k == nl) return true;
    }
    return false;
}

static const char *cui__file_icon(const cui_file_entry *e) {
    if (e->dir) return CUI_ICON_FOLDER;
    const char *dot = strrchr(e->name, '.');
    if (!dot) return CUI_ICON_FILE;
    static const struct { const char *ext; const char *icon; } map[] = {
        { "png", CUI_ICON_IMAGE }, { "jpg", CUI_ICON_IMAGE }, { "jpeg", CUI_ICON_IMAGE }, { "gif", CUI_ICON_IMAGE }, { "webp", CUI_ICON_IMAGE },
        { "svg", CUI_ICON_IMAGE }, { "bmp", CUI_ICON_IMAGE }, { "c", CUI_ICON_CODE }, { "h", CUI_ICON_CODE }, { "cpp", CUI_ICON_CODE },
        { "js", CUI_ICON_CODE }, { "ts", CUI_ICON_CODE }, { "py", CUI_ICON_CODE }, { "rs", CUI_ICON_CODE }, { "go", CUI_ICON_CODE },
        { "json", CUI_ICON_CODE }, { "txt", CUI_ICON_FILE_TEXT }, { "md", CUI_ICON_FILE_TEXT }, { "pdf", CUI_ICON_FILE_TEXT },
        { "zip", CUI_ICON_ARCHIVE }, { "gz", CUI_ICON_ARCHIVE }, { "tar", CUI_ICON_ARCHIVE }, { "7z", CUI_ICON_ARCHIVE },
        { "mp3", CUI_ICON_MUSIC }, { "wav", CUI_ICON_MUSIC }, { "flac", CUI_ICON_MUSIC }, { "mp4", CUI_ICON_VIDEO }, { "mov", CUI_ICON_VIDEO },
        { "db", CUI_ICON_DATABASE }, { "sqlite", CUI_ICON_DATABASE },
    };
    for (size_t k = 0; k < sizeof map / sizeof map[0]; k++) {
        const char *a = dot + 1, *b = map[k].ext;
        while (*a && *b && (*a | 32) == *b) { a++; b++; }
        if (!*a && !*b) return map[k].icon;
    }
    return CUI_ICON_FILE;
}

static const char *cui__file_size(cui_ctx *ui, const cui_file_entry *e) {
    if (e->dir) return "\xe2\x80\x94";
    double v = (double)e->size;
    if (v < 1000) return cui__frame_printf(ui, "%.0f bytes", v);
    static const char *u[] = { "KB", "MB", "GB", "TB" };
    int k = 0;
    v /= 1000;   /* at least 1 KB here */
    while (v >= 1000 && k < 3) { v /= 1000; k++; }
    return cui__frame_printf(ui, v < 10 ? "%.1f %s" : "%.0f %s", v, u[k]);
}

static const char *cui__file_date(cui_ctx *ui, int64_t t, time_t now) {
    if (t <= 0) return "";
    time_t tt = (time_t)t;
    struct tm a, b;
#if defined(_WIN32)
    localtime_s(&a, &tt); localtime_s(&b, &now);
#else
    localtime_r(&tt, &a); localtime_r(&now, &b);
#endif
    static const char *mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return cui__frame_printf(ui, "Today, %d:%02d", a.tm_hour, a.tm_min);
    if (a.tm_year == b.tm_year) return cui__frame_printf(ui, "%s %d, %d:%02d", mon[a.tm_mon], a.tm_mday, a.tm_hour, a.tm_min);
    return cui__frame_printf(ui, "%s %d, %d", mon[a.tm_mon], a.tm_mday, a.tm_year + 1900);
}

/* ------------------------------------------------------------------------ */
/* Browser                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    char loaded[CUI__FB_PATH];
    cui_file_entry *ent; int n, cap;
    char *names; size_t ncap;
    int *order; int norder;
    uint32_t order_key;
    int sort; bool desc;            /* 0 name, 1 date, 2 size */
    char query[128];
    int sel;                        /* index into ent, -1 */
    char hist[CUI__FB_HIST][CUI__FB_PATH]; int hn, hpos;
    char typed[32]; int typed_n; double typed_at;
    bool error, init, reload, scroll_to_sel;
} cui__fb_state;

static void cui__fb_dtor(cui_ctx *ui, void *data) {
    cui__fb_state *s = data;
    cui__free(ui, s->ent); cui__free(ui, s->names); cui__free(ui, s->order);
    s->ent = NULL; s->names = NULL; s->order = NULL;
}

static void cui__fb_load(cui_ctx *ui, cui__fb_state *s, const char *path, const cui_file_browser_opts *o) {
    int cap = o->max_entries > 0 ? o->max_entries : 20000;
    if (s->cap < cap) { cui__free(ui, s->ent); s->ent = cui__alloc(ui, sizeof(cui_file_entry) * (size_t)cap); s->cap = s->ent ? cap : 0; }
    size_t ncap = (size_t)cap * 40;
    if (s->ncap < ncap) { cui__free(ui, s->names); s->names = cui__alloc(ui, ncap); s->ncap = s->names ? ncap : 0; }
    cui_file_list_fn fn = o->list ? o->list : cui_fs_list;
    int n = s->ent && s->names ? fn(o->list_user, path, s->ent, s->cap, s->names, s->ncap, o->hidden) : -1;
    s->error = n < 0;
    s->n = n < 0 ? 0 : n;
    snprintf(s->loaded, sizeof s->loaded, "%s", path);
    s->order_key = 0;   /* sort again */
    s->sel = -1;
}

static cui__fb_state *cui__fb_sorting;   /* qsort has no user pointer; sorting is not reentrant */
static int cui__fb_cmp(const void *pa, const void *pb) {
    cui__fb_state *s = cui__fb_sorting;
    const cui_file_entry *a = &s->ent[*(const int *)pa], *b = &s->ent[*(const int *)pb];
    if (a->dir != b->dir) return a->dir ? -1 : 1;   /* folders first, whichever way */
    int c = 0;
    if (s->sort == 1) c = a->modified < b->modified ? -1 : a->modified > b->modified;
    else if (s->sort == 2) c = a->size < b->size ? -1 : a->size > b->size;
    if (c == 0) {
        /* names: case-insensitive, numbers by value (file2 before file10) */
        const char *x = a->name, *y = b->name;
        while (*x && *y && c == 0) {
            if (*x >= '0' && *x <= '9' && *y >= '0' && *y <= '9') {
                const char *xs = x, *ys = y;
                while (*xs == '0') xs++;
                while (*ys == '0') ys++;
                const char *xe = xs, *ye = ys;
                while (*xe >= '0' && *xe <= '9') xe++;
                while (*ye >= '0' && *ye <= '9') ye++;
                if (xe - xs != ye - ys) c = (xe - xs) < (ye - ys) ? -1 : 1;
                else c = strncmp(xs, ys, (size_t)(xe - xs));
                x = xe; y = ye;
            } else {
                char p = *x, q = *y;
                if (p >= 'A' && p <= 'Z') p = (char)(p + 32);
                if (q >= 'A' && q <= 'Z') q = (char)(q + 32);
                c = (unsigned char)p < (unsigned char)q ? -1 : (unsigned char)p > (unsigned char)q;
                x++; y++;
            }
        }
        if (c == 0) c = *x ? 1 : *y ? -1 : 0;
    }
    return s->desc ? -c : c;
}

/* the filtered, sorted index; rebuilt only when what it depends on changes */
static void cui__fb_order(cui_ctx *ui, cui__fb_state *s, const cui_file_browser_opts *o) {
    uint32_t key = (uint32_t)s->n * 2654435761u ^ (uint32_t)s->sort * 97u ^ (s->desc ? 0x51u : 0) ^ (o->folders_only ? 0x1000u : 0);
    key ^= cui__hash_str(s->query, 0x0F11u) ^ cui__hash_str(o->filter ? o->filter : "", 0x0F12u);
    if (key == 0) key = 1;
    if (key == s->order_key && s->order) return;
    cui__free(ui, s->order);
    s->order = cui__alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, s->n));
    s->norder = 0;
    if (!s->order) return;
    for (int i = 0; i < s->n; i++) {
        const cui_file_entry *e = &s->ent[i];
        if (!e->dir && (o->folders_only || !cui__glob(o->filter, e->name))) continue;
        if (!cui__contains_ci(e->name, s->query)) continue;
        s->order[s->norder++] = i;
    }
    cui__fb_sorting = s;
    qsort(s->order, (size_t)s->norder, sizeof(int), cui__fb_cmp);
    cui__fb_sorting = NULL;
    s->order_key = key;
}

static void cui__fb_go(cui__fb_state *s, char *path, size_t cap, const char *to, bool record) {
    if (!to || !*to || !strcmp(path, to)) return;
    if (record) {
        if (s->hn == 0) { snprintf(s->hist[0], CUI__FB_PATH, "%s", path); s->hn = 1; s->hpos = 0; }
        s->hn = s->hpos + 1;   /* forward history ends here */
        if (s->hn >= CUI__FB_HIST) { memmove(s->hist[0], s->hist[1], (size_t)(CUI__FB_HIST - 1) * CUI__FB_PATH); s->hn--; }
        snprintf(s->hist[s->hn], CUI__FB_PATH, "%s", to);
        s->hpos = s->hn; s->hn++;
    }
    snprintf(path, cap, "%s", to);
}

static void cui__fb_default_places(char (*out)[CUI__FB_PATH], const char **labels, int *n) {
    const char *home = getenv("HOME");
#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE");
#endif
    *n = 0;
    if (home && *home) {
        static const char *sub[] = { NULL, "Desktop", "Documents", "Downloads" };
        static const char *lab[] = { "Home", "Desktop", "Documents", "Downloads" };
        for (int k = 0; k < 4; k++) {
            if (sub[k]) cui__path_join(out[*n], CUI__FB_PATH, home, sub[k]);
            else snprintf(out[*n], CUI__FB_PATH, "%s", home);
            labels[*n] = lab[k];
            (*n)++;
        }
    }
#if defined(_WIN32)
    snprintf(out[*n], CUI__FB_PATH, "C:\\"); labels[*n] = "C:\\"; (*n)++;
#else
    snprintf(out[*n], CUI__FB_PATH, "/"); labels[*n] = "Computer"; (*n)++;
#endif
}

cui_file_browser_result cui_file_browser_ex(cui_ctx *ui, const char *id_str, char *path, size_t path_cap, const cui_file_browser_opts *o) {
    cui_file_browser_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_file_browser_result res = { NULL, NULL, false, false };
    if (!path || path_cap < 2) return res;
    cui_id id = cui_make_id(ui, id_str ? id_str : "files");
    cui__fb_state *s = cui_state(ui, id, sizeof *s);
    cui__state_set_dtor(ui, id, cui__fb_dtor);
    if (!*path) snprintf(path, path_cap, ".");
    if (!s->init) { s->sel = -1; s->init = true; }
    char before[CUI__FB_PATH];
    snprintf(before, sizeof before, "%s", path);
    if (s->reload || strcmp(s->loaded, path) != 0) { cui__fb_load(ui, s, path, o); s->reload = false; }
    cui__fb_order(ui, s, o);
    cui_push_id(ui, id_str ? id_str : "files");
    cui_rect pr = cui__prev_rect(ui, id);
    bool narrow = pr.w > 0 && pr.w < 560, tiny = pr.w > 0 && pr.w < 380;
    int col = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[col];
    cn->w = o->width.kind ? o->width : CUI_FILL;
    cn->h = o->height.kind ? o->height : CUI_PX(380);
    cn->self_align = CUI_ALIGN_STRETCH; cn->gap = 8;
    cui__open(ui, col);
    /* toolbar: back, forward, up, the path and the filter */
    int bar = cui__node_new(ui, 0);
    ui->nodes[bar].dir = CUI_ROW; ui->nodes[bar].gap = 4; ui->nodes[bar].align = CUI_ALIGN_CENTER; ui->nodes[bar].w = CUI_FILL;
    cui__open(ui, bar);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST;
    bo.tooltip = "Back"; bo.disabled = s->hpos <= 0;
    bool go_back = cui_icon_button_ex(ui, CUI_ICON_ARROW_LEFT, &bo);
    bo.tooltip = "Forward"; bo.disabled = s->hpos >= s->hn - 1;
    bool go_fwd = cui_icon_button_ex(ui, CUI_ICON_ARROW_RIGHT, &bo);
    bo.tooltip = "Up"; bo.disabled = false;
    bool go_up = cui_icon_button_ex(ui, CUI_ICON_ARROW_UP, &bo);
    /* the path as segments; the root is its own segment */
    const char *segs[64];
    int nseg = 0;
    char *pcopy = cui__frame_strdup(ui, path, -1);
    size_t plen = strlen(pcopy);
    int root_len = plen && cui__is_sep(pcopy[0]) ? 1 : (plen >= 2 && pcopy[1] == ':') ? (plen >= 3 && cui__is_sep(pcopy[2]) ? 3 : 2) : 0;
    if (root_len) segs[nseg++] = cui__frame_strdup(ui, pcopy, root_len);
    for (char *p = pcopy + root_len; *p && nseg < 64;) {
        while (*p && cui__is_sep(*p)) p++;
        if (!*p) break;
        char *e = p;
        while (*e && !cui__is_sep(*e)) e++;
        segs[nseg++] = cui__frame_strdup(ui, p, (int)(e - p));
        p = e;
    }
    if (nseg == 0) segs[nseg++] = path;
    cui_path_bar_opts po;
    memset(&po, 0, sizeof po);
    po.text = path; po.size = CUI_SIZE_SM;
    cui_path_bar_result pb = cui_path_bar_ex(ui, "path", segs, nseg, &po);
    if (!tiny) {
        cui_input_opts io;
        memset(&io, 0, sizeof io);
        io.placeholder = "Filter"; io.icon = CUI_ICON_SEARCH; io.width = CUI_PX(narrow ? 120 : 180); io.clearable = true;
        cui_input_ex(ui, "filter", s->query, sizeof s->query, &io);
    }
    bo.tooltip = "Reload (F5)";
    bool reload = cui_icon_button_ex(ui, CUI_ICON_REFRESH_CW, &bo);
    cui__close(ui);
    /* body: places and the list */
    int body = cui__node_new(ui, 0);
    ui->nodes[body].dir = CUI_ROW; ui->nodes[body].gap = 8; ui->nodes[body].w = CUI_FILL; ui->nodes[body].h = CUI_FILL;
    ui->nodes[body].align = CUI_ALIGN_STRETCH;
    cui__open(ui, body);
    const char *go_to = NULL;
    if (!o->no_places && !narrow) {
        char (*dplaces)[CUI__FB_PATH] = cui__frame_alloc(ui, sizeof(char[CUI__FB_PATH]) * 6);
        const char *dlabels[6];
        int np = 0;
        const char *const *places = o->places;
        if (!places) {
            cui__fb_default_places(dplaces, dlabels, &np);
        } else np = CUI_MIN(o->place_count, 6);
        int side = cui__node_new(ui, 0);
        ui->nodes[side].w = CUI_PX(150); ui->nodes[side].gap = 1;
        cui__open(ui, side);
        for (int k = 0; k < np; k++) {
            const char *pp = places ? places[k] : dplaces[k];
            const char *label = places ? pp : dlabels[k];
            if (places) { const char *sl = pp + strlen(pp); while (sl > pp && !cui__is_sep(sl[-1])) sl--; if (*sl) label = sl; }
            cui_id pid = cui__hash_str(pp, id ^ 0x91ACu);
            cui_interact pr2 = cui__interact(ui, pid, CUI_IF_FOCUSABLE);
            bool here = !strcmp(pp, path);
            if (pr2.clicked || pr2.keyboard_activate) go_to = pp;
            int row = cui__node_new(ui, pid);
            cui_node *rn = &ui->nodes[row];
            rn->dir = CUI_ROW; rn->gap = 8; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL; rn->h = CUI_PX(28);
            rn->pad[1] = rn->pad[3] = 8;
            rn->radius = CUI_CORNERS(ui->theme.radius_control);
            rn->bg = here ? CUI_C(ui, ACTIVE) : pr2.hovered ? CUI_C(ui, HOVER) : cui__rgba(0, 0, 0, 0);
            cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
            cui__focus_ring(ui, row, pr2.focus_visible ? 1.0f : 0.0f);
            if (pr2.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
            cui__open(ui, row);
            const char *icon = !places && k == 0 ? CUI_ICON_HOUSE : !places && k == np - 1 ? CUI_ICON_MONITOR : !places && k == 3 ? CUI_ICON_DOWNLOAD : CUI_ICON_FOLDER;
            cui__icon_node(ui, icon, 15, here ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
            int t = cui__text_node(ui, label, -1, here ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
            ui->nodes[t].w = CUI_FILL; ui->nodes[t].line_height = 1.3f;
            cui__close(ui);
        }
        cui__close(ui);
    }
    /* the list: a header that sorts, then the rows in view */
    cui_id lid = id ^ 0x115Du;
    cui_interact lr = cui__interact(ui, lid, CUI_IF_FOCUSABLE);
    int list = cui__node_new(ui, lid);
    cui_node *ln = &ui->nodes[list];
    ln->w = CUI_FILL; ln->h = CUI_FILL; ln->gap = 0;
    ln->border = 1; ln->border_color = CUI_C(ui, BORDER);
    ln->radius = CUI_CORNERS(ui->theme.radius_control);
    ln->bg = CUI_C(ui, SURFACE);
    ln->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, list, CUI_IF_FOCUSABLE);
    cui__focus_ring(ui, list, lr.focus_visible ? 1.0f : 0.0f);
    cui__open(ui, list);
    float fs = cui__fs(ui, ui->theme.text_sm), fx = cui__fs(ui, ui->theme.text_xs);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER; ui->nodes[head].w = CUI_FILL; ui->nodes[head].h = CUI_PX(30);
    ui->nodes[head].pad[1] = 12; ui->nodes[head].pad[3] = 12; ui->nodes[head].gap = 12;
    ui->nodes[head].bg = CUI_C(ui, SURFACE_MUTED);
    cui__open(ui, head);
    static const char *titles[3] = { "Name", "Date modified", "Size" };
    for (int k = 0; k < 3; k++) {
        if ((k == 1 && narrow) || (k == 2 && tiny)) continue;
        cui_id hid = id ^ (0x4EAD0u + (unsigned)k);
        cui_interact hr = cui__interact(ui, hid, 0);
        if (hr.clicked) { if (s->sort == k) s->desc = !s->desc; else { s->sort = k; s->desc = false; } }
        int c = cui__node_new(ui, hid);
        cui_node *hn = &ui->nodes[c];
        hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 4; hn->h = CUI_FILL;
        hn->w = k == 0 ? CUI_FILL : CUI_PX(k == 1 ? 150 : 76);
        hn->justify = k == 2 ? CUI_JUSTIFY_END : CUI_JUSTIFY_START;
        cui__mark_interactive(ui, c, 0);
        if (hr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        int t = cui__text_node(ui, titles[k], -1, CUI_FONT_MEDIUM, fx, s->sort == k || hr.hovered ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].line_height = 1.3f;
        if (s->sort == k) cui__icon_node(ui, s->desc ? CUI_ICON_ARROW_DOWN : CUI_ICON_ARROW_UP, 12, CUI_C(ui, FOREGROUND));
        cui__close(ui);
    }
    cui__close(ui);
    int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[line].bg = CUI_C(ui, BORDER);
    /* keys on the list */
    int pos = -1;   /* the selection's place in the order */
    for (int k = 0; k < s->norder; k++) if (s->order[k] == s->sel) { pos = k; break; }
    int picked = -1;
    if (lr.focused && s->norder > 0) {
        int np = pos, page = (int)CUI_MAX(1.0f, (pr.h - 80) / CUI__FB_ROW);
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) np = pos < 0 ? 0 : pos + 1;
        if (cui_key_pressed(ui, CUI_KEY_UP)) np = pos < 0 ? 0 : pos - 1;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) np = (pos < 0 ? 0 : pos) + page;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) np = (pos < 0 ? 0 : pos) - page;
        if (cui_key_pressed(ui, CUI_KEY_HOME)) np = 0;
        if (cui_key_pressed(ui, CUI_KEY_END)) np = s->norder - 1;
        if (cui_key_pressed(ui, CUI_KEY_ENTER) && pos >= 0) picked = s->sel;
        /* typing jumps to the next name starting with what was typed */
        if (ui->text_in_len > 0 && !(ui->mods & (CUI_MOD_CTRL | CUI_MOD_ALT))) {
            if (ui->time - s->typed_at > 1.0) s->typed_n = 0;
            for (int k = 0; k < ui->text_in_len && s->typed_n < 31; k++) s->typed[s->typed_n++] = ui->text_in[k];
            s->typed[s->typed_n] = 0;
            s->typed_at = ui->time;
            int start = s->typed_n == 1 ? pos + 1 : CUI_MAX(pos, 0);
            for (int j = 0; j < s->norder; j++) {
                int k = (start + j) % s->norder;
                const char *nm = s->ent[s->order[k]].name;
                bool ok = true;
                for (int c = 0; c < s->typed_n && ok; c++) {
                    char a = nm[c], b = s->typed[c];
                    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                    ok = a == b;
                }
                if (ok) { np = k; break; }
            }
        }
        if (np != pos) { np = CUI_CLAMP(np, 0, s->norder - 1); s->sel = s->order[np]; pos = np; s->scroll_to_sel = true; }
    }
    if (lr.focused || ui->focus == 0) {
        if (cui_key_pressed(ui, CUI_KEY_BACKSPACE) && lr.focused) go_up = true;
        if (cui_key_pressed(ui, CUI_KEY_LEFT) && (ui->mods & CUI_MOD_ALT)) go_back = true;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT) && (ui->mods & CUI_MOD_ALT)) go_fwd = true;
        if (cui_key_pressed(ui, CUI_KEY_F5)) reload = true;
    }
    /* keep the selection in view */
    if (s->scroll_to_sel && pos >= 0) {
        cui_scroll_state *ss = cui_state(ui, cui_make_id(ui, "rows"), sizeof *ss);
        float y = pos * CUI__FB_ROW, vh = ss->view_h > 0 ? ss->view_h : 300;
        if (y < ss->ty) ss->ty = y;
        else if (y + CUI__FB_ROW > ss->ty + vh) ss->ty = y + CUI__FB_ROW - vh;
        s->scroll_to_sel = false;
    }
    if (s->error || s->norder == 0) {
        int e = cui__node_new(ui, 0);
        ui->nodes[e].w = CUI_FILL; ui->nodes[e].h = CUI_FILL; ui->nodes[e].align = CUI_ALIGN_CENTER; ui->nodes[e].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, e);
        const char *msg = s->error ? "This folder could not be read" : s->query[0] ? "Nothing matches the filter" : "This folder is empty";
        cui__text_node(ui, msg, -1, CUI_FONT_BODY, fs, CUI_C(ui, TEXT_MUTED), false);
        cui__close(ui);
    } else {
        int first, last;
        cui_virtual_list_begin(ui, "rows", s->norder, CUI__FB_ROW, CUI_FILL, &first, &last);
        time_t now = time(NULL);
        for (int k = first; k < last; k++) {
            int i = s->order[k];
            const cui_file_entry *e = &s->ent[i];
            cui_id rid = cui__hash(&i, sizeof i, id ^ 0x20E5u);
            cui_interact rr = cui__interact(ui, rid, CUI_IF_KEEP_FOCUS);
            if (rr.clicked || (rr.pressed && ui->mpressed[0])) { s->sel = i; ui->focus = lid; }
            if (rr.clicked && ui->click_count >= 2) picked = i;
            bool sel = i == s->sel;
            int row = cui__node_new(ui, rid);
            cui_node *rn = &ui->nodes[row];
            rn->dir = CUI_ROW; rn->gap = 12; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL; rn->h = CUI_PX(CUI__FB_ROW);
            rn->pad[1] = rn->pad[3] = 12;
            rn->bg = sel ? (lr.focused ? cui_mix(CUI_C(ui, SURFACE), ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT), ui->accent == CUI_ACCENT_NEUTRAL ? 0.08f : 0.16f) : CUI_C(ui, ACTIVE)) : rr.hovered ? CUI_C(ui, HOVER) : cui__rgba(0, 0, 0, 0);
            cui__mark_interactive(ui, row, 0);
            cui__open(ui, row);
            int nm = cui__node_new(ui, 0);
            ui->nodes[nm].dir = CUI_ROW; ui->nodes[nm].gap = 8; ui->nodes[nm].align = CUI_ALIGN_CENTER; ui->nodes[nm].w = CUI_FILL;
            cui__open(ui, nm);
            cui__icon_node(ui, cui__file_icon(e), 15, e->dir ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
            int t = cui__text_node(ui, e->name, -1, CUI_FONT_BODY, fs, CUI_C(ui, FOREGROUND), false);
            ui->nodes[t].w = CUI_FILL; ui->nodes[t].line_height = 1.3f;
            cui__close(ui);
            if (!narrow) {
                int dt = cui__text_node(ui, cui__file_date(ui, e->modified, now), -1, CUI_FONT_BODY, fx, CUI_C(ui, TEXT_SECONDARY), false);
                ui->nodes[dt].w = CUI_PX(150); ui->nodes[dt].flags |= CUI_NF_TABULAR;
            }
            if (!tiny) {
                int sz = cui__text_node(ui, cui__file_size(ui, e), -1, CUI_FONT_BODY, fx, CUI_C(ui, TEXT_SECONDARY), false);
                ui->nodes[sz].w = CUI_PX(76); ui->nodes[sz].text_align = CUI_TEXT_RIGHT; ui->nodes[sz].flags |= CUI_NF_TABULAR;
            }
            cui__close(ui);
        }
        cui_virtual_list_end(ui);
    }
    cui__close(ui);   /* list */
    cui__close(ui);   /* body */
    cui__close(ui);   /* column */
    cui_pop_id(ui);
    /* where to go */
    char target[CUI__FB_PATH];
    target[0] = 0;
    if (picked >= 0) {
        const cui_file_entry *e = &s->ent[picked];
        cui__path_join(target, sizeof target, path, e->name);
        if (e->dir) cui__fb_go(s, path, path_cap, target, true);
        else res.picked = cui__frame_strdup(ui, target, -1);
    }
    if (pb.clicked >= 0 && pb.clicked < nseg - 1) {
        /* the path up to that segment */
        size_t k = (size_t)root_len;
        int seen = root_len ? 1 : 0;
        while (seen <= pb.clicked && k < plen) {
            while (k < plen && cui__is_sep(path[k])) k++;
            while (k < plen && !cui__is_sep(path[k])) k++;
            seen++;
        }
        snprintf(target, sizeof target, "%.*s", (int)(k ? k : 1), path);
        cui__fb_go(s, path, path_cap, target, true);
    }
    if (pb.submitted) cui__fb_go(s, path, path_cap, pb.submitted, true);
    if (go_to) cui__fb_go(s, path, path_cap, go_to, true);
    if (go_up) { snprintf(target, sizeof target, "%s", path); if (cui__path_up(target)) cui__fb_go(s, path, path_cap, target, true); }
    if (go_back && s->hpos > 0) { s->hpos--; snprintf(path, path_cap, "%s", s->hist[s->hpos]); }
    if (go_fwd && s->hpos < s->hn - 1) { s->hpos++; snprintf(path, path_cap, "%s", s->hist[s->hpos]); }
    if (reload) s->reload = true;
    res.navigated = strcmp(before, path) != 0;
    if (res.navigated) s->query[0] = 0;
    if (s->sel >= 0 && s->sel < s->n && !res.navigated) {
        cui__path_join(target, sizeof target, path, s->ent[s->sel].name);
        res.selected = cui__frame_strdup(ui, target, -1);
    }
    res.error = s->error;
    ui->last_id = lid;
    return res;
}

/* ------------------------------------------------------------------------ */
/* Open and save dialog                                                     */
/* ------------------------------------------------------------------------ */

typedef struct { char path[CUI__FB_PATH]; char name[256]; uint32_t sel_hash; bool init; } cui__fp_state;

bool cui_file_picker_ex(cui_ctx *ui, const char *id_str, bool *open, char *out, size_t out_cap, const cui_file_picker_opts *o) {
    cui_file_picker_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!open) return false;
    cui_id id = cui_make_id(ui, id_str ? id_str : "file-dialog");
    cui__fp_state *s = cui_state(ui, id, sizeof *s);
    if (!*open) { s->init = false; }
    if (*open && !s->init) {
        snprintf(s->path, sizeof s->path, "%s", o->start && *o->start ? o->start : ".");
        snprintf(s->name, sizeof s->name, "%s", o->name ? o->name : "");
        s->init = true;
    }
    bool done = false;
    const char *title = o->title ? o->title : o->save ? "Save" : o->folders ? "Choose a folder" : "Open";
    cui_dialog_opts dopt;
    memset(&dopt, 0, sizeof dopt);
    dopt.width = CUI_MIN(780.0f, ui->vw - 24);
    cui_push_id(ui, id_str ? id_str : "file-dialog");
    if (cui_dialog_begin(ui, title, open, &dopt)) {
        cui_file_browser_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.height = CUI_PX(CUI_MIN(400.0f, ui->vh - 260));
        bo.filter = o->filter; bo.folders_only = o->folders; bo.list = o->list; bo.list_user = o->list_user;
        cui_file_browser_result r = cui_file_browser_ex(ui, "browser", s->path, sizeof s->path, &bo);
        /* save: selecting a file puts its name in the field */
        uint32_t selh = r.selected ? cui__hash_str(r.selected, 0x5E1u) : 0;
        if (o->save && r.selected && selh != s->sel_hash) {
            cui__fb_state *bs = cui_state(ui, cui_make_id(ui, "browser"), sizeof(cui__fb_state));
            if (bs->sel >= 0 && bs->sel < bs->n && bs->ent && !bs->ent[bs->sel].dir) {
                const char *nm = r.selected + strlen(r.selected);
                while (nm > r.selected && !cui__is_sep(nm[-1])) nm--;
                snprintf(s->name, sizeof s->name, "%s", nm);
            }
        }
        s->sel_hash = selh;
        int foot = cui__node_new(ui, 0);
        ui->nodes[foot].dir = CUI_ROW; ui->nodes[foot].gap = 8; ui->nodes[foot].align = CUI_ALIGN_CENTER; ui->nodes[foot].w = CUI_FILL;
        ui->nodes[foot].flags |= CUI_NF_ROW_WRAP;
        cui__open(ui, foot);
        bool submit = false;
        if (o->save) {
            cui_input_opts io;
            memset(&io, 0, sizeof io);
            io.placeholder = "File name"; io.width = CUI_GROW(1);
            if (cui_input_ex(ui, "name", s->name, sizeof s->name, &io) & CUI_SUBMITTED) submit = s->name[0] != 0;
            ui->nodes[ui->last_node].min_w = 160;
        } else {
            const char *fl = o->folders ? NULL : o->filter_label ? o->filter_label : o->filter;
            int t = cui__text_node(ui, fl ? fl : "", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].w = CUI_FILL; ui->nodes[t].min_w = 0;
        }
        cui_button_opts cb;
        memset(&cb, 0, sizeof cb);
        if (cui_button_ex(ui, "Cancel", &cb)) *open = false;
        bool can = o->save ? s->name[0] != 0 : o->folders ? true : r.selected != NULL;   /* Open on a folder goes into it */
        cb.variant = CUI_BUTTON_PRIMARY; cb.disabled = !can;
        bool confirm = cui_button_ex(ui, o->save ? "Save" : o->folders ? "Choose" : "Open", &cb) || submit;
        cui__close(ui);
        if (r.picked && !o->save && !o->folders) { snprintf(out, out_cap, "%s", r.picked); done = true; }
        else if (r.picked && o->save) {
            const char *nm = r.picked + strlen(r.picked);
            while (nm > r.picked && !cui__is_sep(nm[-1])) nm--;
            snprintf(s->name, sizeof s->name, "%s", nm);
        } else if (confirm) {
            if (o->save) { cui__path_join(out, out_cap, s->path, s->name); done = true; }
            else if (o->folders) { snprintf(out, out_cap, "%s", r.selected ? r.selected : s->path); done = true; }
            else if (r.selected) {
                /* Open on a folder enters it; on a file picks it */
                cui__fb_state *bs = cui_state(ui, cui_make_id(ui, "browser"), sizeof(cui__fb_state));
                bool dir = bs->sel >= 0 && bs->sel < bs->n && bs->ent && bs->ent[bs->sel].dir;
                if (dir) cui__fb_go(bs, s->path, sizeof s->path, r.selected, true);
                else { snprintf(out, out_cap, "%s", r.selected); done = true; }
            }
        }
        if (done) *open = false;
        cui_dialog_end(ui);
    }
    cui_pop_id(ui);
    return done;
}
