

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>

#define VERSION       "2.0.0"
#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT 720
#define READ_CHUNK    (1024 * 64)
#define LINE_MAX_LEN   512
#define TEXT_MAX_LEN   256

static const char *FONT_PATHS[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/FreeSans.ttf",
    "/usr/share/fonts/gnu-free/FreeSans.ttf",
    NULL
};

typedef enum { UNIT_PX, UNIT_DP, UNIT_PCT } Unit;

static int resolve_unit(float val, Unit u, int ref_dim, int dpi) {
    switch (u) {
        case UNIT_PX:  return (int)val;
        case UNIT_DP:  return (int)(val * dpi / 160.0f);
        case UNIT_PCT: return (int)(val * ref_dim / 100.0f);
    }
    return (int)val;
}

static Unit parse_unit(const char *s) {
    if (strncmp(s, "dp",  2) == 0) return UNIT_DP;
    if (strncmp(s, "px",  2) == 0) return UNIT_PX;
    if (strncmp(s, "%",   1) == 0) return UNIT_PCT;
    if (strncmp(s, "p",   1) == 0) return UNIT_PCT; 
    return UNIT_PX;
}

static bool get_flag(const char *line, const char *flag_name,
                     char *dst, size_t max) {
    char needle[64];
    snprintf(needle, sizeof needle, "%s[", flag_name);
    const char *p = strstr(line, needle);
    if (!p) return false;
    p += strlen(needle);
    size_t i = 0;
    while (i < max - 1 && p[i] && p[i] != ']') { dst[i] = p[i]; i++; }
    dst[i] = '\0';
    return true;
}

static bool get_flag_unit(const char *line, const char *flag_name,
                          char *dst, size_t max) {
    char needle[64];
    snprintf(needle, sizeof needle, "%s[", flag_name);
    const char *p = strstr(line, needle);
    if (!p) return false;
    
    p += strlen(needle);
    while (*p && *p != ']') p++;
    if (!*p) return false;
    p++; 
    if (*p != '[') return false;
    p++;
    size_t i = 0;
    while (i < max - 1 && p[i] && p[i] != ']') { dst[i] = p[i]; i++; }
    dst[i] = '\0';
    return true;
}

static bool parse_hex(const char *s, uint8_t *r, uint8_t *g, uint8_t *b) {
    unsigned int hex;
    if (sscanf(s, "%6x", &hex) != 1) return false;
    *r = (hex >> 16) & 0xff;
    *g = (hex >>  8) & 0xff;
    *b =  hex        & 0xff;
    return true;
}

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint32_t     *pixels;
    int           width, height, dpi;
    TTF_Font     *font_cache[64];
    int           font_sizes[64];
    int           font_count;
    char          font_path[512];
} Renderer;

static uint32_t pack(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void rnd_set(Renderer *rnd, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= rnd->width || y < 0 || y >= rnd->height) return;
    rnd->pixels[y * rnd->width + x] = pack(r, g, b);
}

static void rnd_clear(Renderer *rnd, int x, int y) {
    if (x < 0 || x >= rnd->width || y < 0 || y >= rnd->height) return;
    rnd->pixels[y * rnd->width + x] = 0;
}

static void rnd_flush(Renderer *rnd) {
    SDL_UpdateTexture(rnd->texture, NULL, rnd->pixels,
                      rnd->width * (int)sizeof(uint32_t));
    SDL_RenderClear(rnd->renderer);
    SDL_RenderCopy(rnd->renderer, rnd->texture, NULL, NULL);
    SDL_RenderPresent(rnd->renderer);
}

static TTF_Font *get_font(Renderer *rnd, int size_px) {
    for (int i = 0; i < rnd->font_count; i++)
        if (rnd->font_sizes[i] == size_px)
            return rnd->font_cache[i];
    if (rnd->font_count >= 64) return rnd->font_cache[0];
    TTF_Font *f = TTF_OpenFont(rnd->font_path, size_px);
    if (!f) {
        fprintf(stderr, "[composite] TTF_OpenFont(%d): %s\n",
                size_px, TTF_GetError());
        return rnd->font_count > 0 ? rnd->font_cache[0] : NULL;
    }
    rnd->font_cache[rnd->font_count] = f;
    rnd->font_sizes[rnd->font_count] = size_px;
    rnd->font_count++;
    return f;
}

static void draw_text_at(Renderer *rnd, const char *text, int x, int y,
                          int size_px, uint8_t r, uint8_t g, uint8_t b) {
    TTF_Font *font = get_font(rnd, size_px);
    if (!font) return;
    SDL_Color col = { r, g, b, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, col);
    if (!surf) return;

    
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(surf);
    if (!conv) return;

    SDL_LockSurface(conv);
    uint32_t *src = (uint32_t *)conv->pixels;
    for (int sy = 0; sy < conv->h; sy++) {
        for (int sx = 0; sx < conv->w; sx++) {
            int dx = x + sx, dy = y + sy;
            if (dx < 0 || dx >= rnd->width || dy < 0 || dy >= rnd->height) continue;
            uint32_t p = src[sy * (conv->pitch / 4) + sx];
            uint8_t a = (p >> 24) & 0xff;
            if (a == 0) continue;
            uint8_t pr = (p >> 16) & 0xff;
            uint8_t pg = (p >>  8) & 0xff;
            uint8_t pb =  p        & 0xff;
            
            uint32_t ex = rnd->pixels[dy * rnd->width + dx];
            uint8_t er = (ex >> 16) & 0xff;
            uint8_t eg = (ex >>  8) & 0xff;
            uint8_t eb =  ex        & 0xff;
            float fa = a / 255.0f;
            uint8_t nr = (uint8_t)(pr * fa + er * (1.0f - fa));
            uint8_t ng = (uint8_t)(pg * fa + eg * (1.0f - fa));
            uint8_t nb = (uint8_t)(pb * fa + eb * (1.0f - fa));
            rnd->pixels[dy * rnd->width + dx] = pack(nr, ng, nb);
        }
    }
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
}

static void draw_rect(Renderer *rnd, int x, int y, int w, int h,
                      bool filled, uint8_t r, uint8_t g, uint8_t b,
                      int border, uint8_t br, uint8_t bg, uint8_t bb) {
    if (filled) {
        for (int py = y; py < y + h; py++)
            for (int px = x; px < x + w; px++)
                rnd_set(rnd, px, py, r, g, b);
    }
    
    if (border > 0) {
        for (int i = 0; i < border; i++) {
            for (int px = x + i; px < x + w - i; px++) {
                rnd_set(rnd, px, y + i,         br, bg, bb);
                rnd_set(rnd, px, y + h - 1 - i, br, bg, bb);
            }
            for (int py = y + i; py < y + h - i; py++) {
                rnd_set(rnd, x + i,         py, br, bg, bb);
                rnd_set(rnd, x + w - 1 - i, py, br, bg, bb);
            }
        }
    }
}

static void draw_circle(Renderer *rnd, int cx, int cy, int rx, int ry,
                         bool filled, uint8_t r, uint8_t g, uint8_t b,
                         int border, uint8_t br2, uint8_t bg2, uint8_t bb2) {
    
    if (filled) {
        for (int py = cy - ry; py <= cy + ry; py++) {
            float dy = (float)(py - cy) / ry;
            if (dy * dy > 1.0f) continue;
            int half = (int)(rx * sqrtf(1.0f - dy * dy));
            for (int px = cx - half; px <= cx + half; px++)
                rnd_set(rnd, px, py, r, g, b);
        }
    }
    if (border > 0) {
        for (int i = 0; i < border; i++) {
            float irx = rx - i, iry = ry - i;
            float orx = rx - i + 1, ory = ry - i + 1;
            if (irx <= 0 || iry <= 0) break;
            for (int angle = 0; angle < 360; angle++) {
                float rad = angle * (float)M_PI / 180.0f;
                int ox = cx + (int)(orx * cosf(rad));
                int oy = cy + (int)(ory * sinf(rad));
                rnd_set(rnd, ox, oy, br2, bg2, bb2);
            }
            (void)irx; (void)iry;
        }
    }
    if (!filled && border == 0) {
        
        for (int angle = 0; angle < 3600; angle++) {
            float rad = angle * (float)M_PI / 1800.0f;
            int ox = cx + (int)(rx * cosf(rad));
            int oy = cy + (int)(ry * sinf(rad));
            rnd_set(rnd, ox, oy, r, g, b);
        }
    }
}

typedef enum { ELEM_PIXEL, ELEM_SQUARE, ELEM_CIRCLE, ELEM_TEXT, ELEM_UNKNOWN } ElemType;

static ElemType get_elem_type(const char *line) {
    if (strstr(line, "element pixel["))  return ELEM_PIXEL;
    if (strstr(line, "element square[")) return ELEM_SQUARE;
    if (strstr(line, "element circle[")) return ELEM_CIRCLE;
    if (strstr(line, "element text["))   return ELEM_TEXT;
    return ELEM_UNKNOWN;
}

static bool parse_dims(const char *line, const char *tag,
                        int *a, int *b_out) {
    char needle[32];
    snprintf(needle, sizeof needle, "%s[", tag);
    const char *p = strstr(line, needle);
    if (!p) return false;
    p += strlen(needle);
    return sscanf(p, "x%dy%d", a, b_out) == 2;
}

static void parse_pos_flag(const char *line, const char *flag_name,
                            float *ox, float *oy, Unit *ux, Unit *uy) {
    char val[32] = {0}, unit[8] = {0};
    *ox = 0; *oy = 0; *ux = UNIT_PX; *uy = UNIT_PX;
    if (!get_flag(line, flag_name, val, sizeof val)) return;
    float fx, fy;
    if (sscanf(val, "x%fy%f", &fx, &fy) == 2) {
        *ox = fx; *oy = fy;
    }
    if (get_flag_unit(line, flag_name, unit, sizeof unit)) {
        *ux = parse_unit(unit);
        *uy = parse_unit(unit);
    }
}

static void parse_size_flag(const char *line, const char *flag_name,
                             float *val, Unit *u) {
    char sv[32] = {0}, su[8] = {0};
    *val = 16; *u = UNIT_PX;
    if (!get_flag(line, flag_name, sv, sizeof sv)) return;
    *val = atof(sv);
    if (get_flag_unit(line, flag_name, su, sizeof su))
        *u = parse_unit(su);
}

static void process_line(Renderer *rnd, const char *line) {
    ElemType et = get_elem_type(line);
    if (et == ELEM_UNKNOWN) return;

    
    char tmp[64];

    
    float px_f = 0, py_f = 0; Unit pux = UNIT_PX, puy = UNIT_PX;
    parse_pos_flag(line, "flag pos", &px_f, &py_f, &pux, &puy);
    int px = resolve_unit(px_f, pux, rnd->width,  rnd->dpi);
    int py = resolve_unit(py_f, puy, rnd->height, rnd->dpi);

    
    uint8_t fr = 255, fg = 0, fb = 0;
    if (get_flag(line, "flag color", tmp, sizeof tmp))
        parse_hex(tmp, &fr, &fg, &fb);

    
    bool filled = true;
    if (get_flag(line, "flag filled", tmp, sizeof tmp))
        filled = (strncmp(tmp, "true", 4) == 0);

    
    int border = 0;
    Unit bunit = UNIT_PX;
    if (get_flag(line, "flag border", tmp, sizeof tmp)) {
        float bv = atof(tmp);
        char bu[8] = {0};
        if (get_flag_unit(line, "flag border", bu, sizeof bu))
            bunit = parse_unit(bu);
        border = resolve_unit(bv, bunit, 0, rnd->dpi);
    }

    
    uint8_t bcr = 255, bcg = 255, bcb = 255;
    if (get_flag(line, "flag border-color", tmp, sizeof tmp))
        parse_hex(tmp, &bcr, &bcg, &bcb);

    
    if (et == ELEM_PIXEL) {
        int ex = 0, ey = 0;
        parse_dims(line, "element pixel", &ex, &ey);

        char op[8] = {0};
        get_flag(line, "operation", op, sizeof op);

        if (strncmp(op, "rm", 2) == 0) {
            rnd_clear(rnd, ex, ey);
        } else {
            uint8_t r2 = 255, g2 = 255, b2 = 255;
            if (get_flag(line, "flag color", tmp, sizeof tmp))
                parse_hex(tmp, &r2, &g2, &b2);
            rnd_set(rnd, ex, ey, r2, g2, b2);
        }
        return;
    }

    
    if (et == ELEM_SQUARE) {
        int sw = 0, sh = 0;
        parse_dims(line, "element square", &sw, &sh);

        
        char inner_text[TEXT_MAX_LEN] = {0};
        get_flag(line, "flag text", inner_text, sizeof inner_text);

        draw_rect(rnd, px, py, sw, sh, filled, fr, fg, fb,
                  border, bcr, bcg, bcb);

        if (inner_text[0]) {
            
            float tpx_f = 50, tpy_f = 50;
            Unit tpux = UNIT_PCT, tpuy = UNIT_PCT;
            parse_pos_flag(line, "flag.text pos", &tpx_f, &tpy_f, &tpux, &tpuy);

            
            uint8_t tr = 255, tg = 255, tb = 255;
            if (get_flag(line, "flag.text color", tmp, sizeof tmp))
                parse_hex(tmp, &tr, &tg, &tb);

            
            float tsz_f = 16; Unit tszu = UNIT_PX;
            parse_size_flag(line, "flag.text size", &tsz_f, &tszu);
            int tsz = resolve_unit(tsz_f, tszu, sh, rnd->dpi);
            if (tsz < 6) tsz = 6;

            int tx = px + resolve_unit(tpx_f, tpux, sw, rnd->dpi);
            int ty = py + resolve_unit(tpy_f, tpuy, sh, rnd->dpi);

            
            if (tpux == UNIT_PCT || tpux == UNIT_DP) {
                TTF_Font *f = get_font(rnd, tsz);
                if (f) {
                    int tw2, th2;
                    TTF_SizeUTF8(f, inner_text, &tw2, &th2);
                    tx -= tw2 / 2;
                    ty -= th2 / 2;
                }
            }

            draw_text_at(rnd, inner_text, tx, ty, tsz, tr, tg, tb);
        }
        return;
    }

    
    if (et == ELEM_CIRCLE) {
        int cw = 0, ch = 0;
        parse_dims(line, "element circle", &cw, &ch);
        int cx = px + cw / 2;
        int cy = py + ch / 2;

        char inner_text[TEXT_MAX_LEN] = {0};
        get_flag(line, "flag text", inner_text, sizeof inner_text);

        draw_circle(rnd, cx, cy, cw / 2, ch / 2, filled, fr, fg, fb,
                    border, bcr, bcg, bcb);

        if (inner_text[0]) {
            float tpx_f = 50, tpy_f = 50;
            Unit tpux = UNIT_PCT, tpuy = UNIT_PCT;
            parse_pos_flag(line, "flag.text pos", &tpx_f, &tpy_f, &tpux, &tpuy);

            uint8_t tr = 255, tg = 255, tb = 255;
            if (get_flag(line, "flag.text color", tmp, sizeof tmp))
                parse_hex(tmp, &tr, &tg, &tb);

            float tsz_f = 16; Unit tszu = UNIT_PX;
            parse_size_flag(line, "flag.text size", &tsz_f, &tszu);
            int tsz = resolve_unit(tsz_f, tszu, ch, rnd->dpi);
            if (tsz < 6) tsz = 6;

            int tx = px + resolve_unit(tpx_f, tpux, cw, rnd->dpi);
            int ty = py + resolve_unit(tpy_f, tpuy, ch, rnd->dpi);

            TTF_Font *f = get_font(rnd, tsz);
            if (f) {
                int tw2, th2;
                TTF_SizeUTF8(f, inner_text, &tw2, &th2);
                tx -= tw2 / 2;
                ty -= th2 / 2;
            }

            draw_text_at(rnd, inner_text, tx, ty, tsz, tr, tg, tb);
        }
        return;
    }

    
    if (et == ELEM_TEXT) {
        char text[TEXT_MAX_LEN] = {0};
        get_flag(line, "element text", text, sizeof text);

        float tsz_f = 16; Unit tszu = UNIT_PX;
        parse_size_flag(line, "flag size", &tsz_f, &tszu);
        int tsz = resolve_unit(tsz_f, tszu, rnd->height, rnd->dpi);
        if (tsz < 6) tsz = 6;

        draw_text_at(rnd, text, px, py, tsz, fr, fg, fb);
        return;
    }
}

typedef struct {
    FILE   *fp;
    char    buf[READ_CHUNK + 1];
    size_t  buf_pos, buf_len;
    bool    eof;
    long    line_no;
} Parser;

static bool parser_init(Parser *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->fp = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!p->fp) { perror(path); return false; }
    p->line_no = 1;
    return true;
}
static void parser_close(Parser *p) {
    if (p->fp && p->fp != stdin) fclose(p->fp);
}
static void refill(Parser *p) {
    if (p->eof) return;
    size_t rem = p->buf_len - p->buf_pos;
    if (rem) memmove(p->buf, p->buf + p->buf_pos, rem);
    p->buf_pos = 0;
    size_t n = fread(p->buf + rem, 1, READ_CHUNK - rem, p->fp);
    p->buf_len = rem + n;
    p->buf[p->buf_len] = '\0';
    if (!n) p->eof = true;
}
static bool next_line(Parser *p, char *dst, size_t max) {
    if (p->buf_pos >= p->buf_len && !p->eof) refill(p);
    if (p->buf_pos >= p->buf_len) return false;
    size_t i = 0;
    while (i < max - 1) {
        if (p->buf_pos >= p->buf_len) {
            if (p->eof) break;
            refill(p);
            if (p->buf_pos >= p->buf_len) break;
        }
        char c = p->buf[p->buf_pos++];
        if (c == '\n') { p->line_no++; break; }
        if (c != '\r') dst[i++] = c;
    }
    dst[i] = '\0';
    return i > 0 || !p->eof;
}
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) e--;
    *e = '\0';
    return s;
}

static bool renderer_init(Renderer *rnd, int w, int h, bool fullscreen) {
    memset(rnd, 0, sizeof *rnd);
    rnd->width = w; rnd->height = h; rnd->dpi = 96;

    
    rnd->font_path[0] = '\0';
    for (int i = 0; FONT_PATHS[i]; i++) {
        FILE *f = fopen(FONT_PATHS[i], "rb");
        if (f) { fclose(f); strncpy(rnd->font_path, FONT_PATHS[i], 511); break; }
    }
    if (!rnd->font_path[0]) {
        fprintf(stderr, "[composite] warn: no TTF font found /// text disabled\n");
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return false;
    }

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    rnd->window = SDL_CreateWindow("composite",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!rnd->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false;
    }

    rnd->renderer = SDL_CreateRenderer(rnd->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rnd->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false;
    }

    rnd->texture = SDL_CreateTexture(rnd->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!rnd->texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return false;
    }

    rnd->pixels = calloc((size_t)w * h, sizeof(uint32_t));
    if (!rnd->pixels) { fprintf(stderr, "OOM\n"); return false; }

    
    if (rnd->font_path[0]) get_font(rnd, 16);

    return true;
}

static void renderer_destroy(Renderer *rnd) {
    for (int i = 0; i < rnd->font_count; i++)
        TTF_CloseFont(rnd->font_cache[i]);
    free(rnd->pixels);
    if (rnd->texture)  SDL_DestroyTexture(rnd->texture);
    if (rnd->renderer) SDL_DestroyRenderer(rnd->renderer);
    if (rnd->window)   SDL_DestroyWindow(rnd->window);
    TTF_Quit();
    SDL_Quit();
}

static void usage(const char *prog) {
    fprintf(stderr,
        "composite v%s /// Hyrin Composite SDL renderer (Linux)\n\n"
        "Usage: %s <file.cpst> [options]\n"
        "       %s - [options]   (stdin)\n\n"
        "Options:\n"
        "  --width N      window width  (default %d)\n"
        "  --height N     window height (default %d)\n"
        "  --dpi N        dpi for dp/pct units (default 96)\n"
        "  --fullscreen   fullscreen\n"
        "  --nowait       exit after render\n"
        "  --help\n",
        VERSION, prog, prog, DEFAULT_WIDTH, DEFAULT_HEIGHT);
}

int main(int argc, char *argv[]) {
    const char *filepath = NULL;
    int width = DEFAULT_WIDTH, height = DEFAULT_HEIGHT, dpi = 96;
    bool fullscreen = false, nowait = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        } else if (!strcmp(argv[i], "--fullscreen")) { fullscreen = true;
        } else if (!strcmp(argv[i], "--nowait"))     { nowait = true;
        } else if (!strcmp(argv[i], "--width")  && i+1 < argc) { width  = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--height") && i+1 < argc) { height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dpi")    && i+1 < argc) { dpi    = atoi(argv[++i]);
        } else if (argv[i][0] != '-' || !strcmp(argv[i], "-")) { filepath = argv[i];
        } else { fprintf(stderr, "Unknown: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    if (!filepath) {
        fprintf(stderr, "Error: no input file.\n"); usage(argv[0]); return 1;
    }

    Parser parser;
    if (!parser_init(&parser, filepath)) return 1;

    Renderer rnd;
    if (!renderer_init(&rnd, width, height, fullscreen)) {
        parser_close(&parser); return 1;
    }
    rnd.dpi = dpi;

    char line[LINE_MAX_LEN];
    bool in_draw = false, compose_seen = false;
    long elems = 0, since_flush = 0;
    const long FLUSH_EVERY = 50000;

    fprintf(stderr, "[composite] v%s rendering %s\n", VERSION, filepath);

    while (next_line(&parser, line, sizeof line)) {
        char *t = trim(line);
        if (!t[0] || t[0] == ';' || t[0] == '#') continue;

        if (!in_draw && strncmp(t, "draw[", 5) == 0) { in_draw = true; continue; }

        if (strstr(t, "] & compose") || strstr(t, "]&compose")) {
            in_draw = false; compose_seen = true;
            rnd_flush(&rnd);
            fprintf(stderr, "[composite] compose /// %ld elements\n", elems);
            continue;
        }
        if (!in_draw) continue;

        process_line(&rnd, t);
        elems++; since_flush++;

        if (since_flush >= FLUSH_EVERY) {
            rnd_flush(&rnd);
            since_flush = 0;
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT ||
                    (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                    fprintf(stderr, "[composite] interrupted at %ld elements\n", elems);
                    goto done;
                }
            }
        }
    }

    if (!compose_seen) {
        rnd_flush(&rnd);
        fprintf(stderr, "[composite] done /// %ld elements\n", elems);
    }

done:
    parser_close(&parser);

    if (!nowait) {
        SDL_Event ev;
        fprintf(stderr, "[composite] Esc or close to exit\n");
        while (SDL_WaitEvent(&ev)) {
            if (ev.type == SDL_QUIT) break;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) break;
            if (ev.type == SDL_WINDOWEVENT &&
                ev.window.event == SDL_WINDOWEVENT_EXPOSED)
                rnd_flush(&rnd);
        }
    }

    renderer_destroy(&rnd);
    return 0;
}
