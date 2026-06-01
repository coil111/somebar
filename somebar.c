#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pixman.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>
#include <cjson/cJSON.h>                      /* JSON IPC 解析 */

#include "utf8.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "xdg-output-unstable-v1-protocol.h"
#include "xdg-shell-protocol.h"

#define DIE(fmt, ...)                                                          \
  do {                                                                         \
    cleanup();                                                                 \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__);                                  \
    exit(1);                                                                   \
  } while (0)
#define EDIE(fmt, ...) DIE(fmt ": %s", ##__VA_ARGS__, strerror(errno));

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LENGTH(x) (sizeof x / sizeof x[0])

#define ARRAY_INIT_CAP 16
#define ARRAY_EXPAND(arr, len, cap, inc)                                       \
  do {                                                                         \
    uint32_t new_len, new_cap;                                                 \
    new_len = (len) + (inc);                                                   \
    if (new_len > (cap)) {                                                     \
      new_cap = new_len * 2;                                                   \
      if (new_cap < ARRAY_INIT_CAP)                                            \
        new_cap = ARRAY_INIT_CAP;                                              \
      if (!((arr) = realloc((arr), sizeof(*(arr)) * new_cap)))                 \
        EDIE("realloc");                                                       \
      (cap) = new_cap;                                                         \
    }                                                                          \
    (len) = new_len;                                                           \
  } while (0)
#define ARRAY_APPEND(arr, len, cap, ptr)                                       \
  do {                                                                         \
    ARRAY_EXPAND((arr), (len), (cap), 1);                                      \
    (ptr) = &(arr)[(len) - 1];                                                 \
  } while (0)

#define PROGRAM "somebar"
#define VERSION "0.2"
#define USAGE                                                                  \
  "usage: somebar [OPTIONS]\n"                                                \
  "Ipc\n"                                                                      \
  "	-ipc				allow commands ipc "                                           \
  "	-no-ipc				disable ipc\n"                                                  \
  "Bar Config\n"                                                               \
  "	-hidden				bars will initially be hidden\n"                                \
  "	-no-hidden			bars will not initially be hidden\n"                          \
  "	-bottom				bars will initially be drawn at the "                           \
  "bottom\n"                                                                   \
  "	-no-bottom			bars will initially be drawn at the "                         \
  "top\n"                                                                      \
  "	-hide-vacant-tags		do not display empty and inactive "                     \
  "tags\n"                                                                     \
  "	-no-hide-vacant-tags		display empty and inactive tags\n"                   \
  "	-status-commands		enable in-line commands in status "                      \
  "text\n"                                                                     \
  "	-no-status-commands		disable in-line commands in status "                  \
  "text\n"                                                                     \
  "	-center-title			center title text on bar\n"                                \
  "	-no-center-title		do not center title text on bar\n"                       \
  "	-custom-title			do not display window title and "                          \
  "treat the area as another status text element; see -title command\n"        \
  "	-no-custom-title		display current window title as "                        \
  "normal\n"                                                                   \
  "	-active-color-title		title colors will use active colors\n"                \
  "	-no-active-color-title		title colors will use inactive "                   \
  "colors\n"                                                                   \
  "	-font [FONT]			specify a font\n"                                           \
  "	-tags [NUMBER] [FIRST]...[LAST]	if ipc is disabled, specify custom "       \
  "tag names. If NUMBER is 0, then no tag names should be given \n"            \
  "	-vertical-padding [PIXELS]	specify vertical pixel padding above "          \
  "and below text\n"                                                           \
  "	-active-fg-color [COLOR]	specify text color of active tags or "            \
  "monitors\n"                                                                 \
  "	-active-bg-color [COLOR]	specify background color of active "              \
  "tags or monitors\n"                                                         \
  "	-occupied-fg-color [COLOR]	specify text color of occupied tags\n"          \
  "	-occupied-bg-color [COLOR]	specify background color of occupied "          \
  "tags\n"                                                                     \
  "	-inactive-fg-color [COLOR]	specify text color of inactive tags "           \
  "or monitors\n"                                                              \
  "	-inactive-bg-color [COLOR]	specify background color of inactive "          \
  "tags or monitors\n"                                                         \
  "	-urgent-fg-color [COLOR]	specify text color of urgent tags\n"              \
  "	-urgent-bg-color [COLOR]	specify background color of urgent "              \
  "tags\n"                                                                     \
  "	-middle-bg-color [COLOR]	specify background color of the "                 \
  "color in the middle of the bar\n"                                           \
  "	-middle-bg-color-selected [COLOR]	specify background color of "            \
  "the color in the middle of the bar, when selected\n"                        \
  "	-scale [BUFFER_SCALE]		specify buffer scale value for "                    \
  "integer scaling\n"                                                          \
  "Commands\n"                                                                 \
  "	-target-socket [SOCKET-NAME]	set the socket to send command to. "          \
  "Sockets can be found in `$MANGO_INSTANCE_SIGNATURE`\n"                         \
  "	-status	[OUTPUT] [TEXT]		set status text\n"                                \
  "	-status-stdin	[OUTPUT]		set status text from stdin\n"                      \
  "	-title	[OUTPUT] [TEXT]		set title text, if -custom-title is "              \
  "enabled\n"                                                                  \
  "	-show [OUTPUT]			show bar\n"                                               \
  "	-hide [OUTPUT]			hide bar\n"                                               \
  "	-toggle-visibility [OUTPUT]	toggle bar visibility\n"                       \
  "	-set-top [OUTPUT]		draw bar at the top\n"                                  \
  "	-set-bottom [OUTPUT]		draw bar at the bottom\n"                            \
  "	-toggle-location [OUTPUT]	toggle bar location\n"                           \
  "Other\n"                                                                    \
  "	-v				get version information\n"                                           \
  "	-h				view this help text\n"

#define TEXT_MAX 2048

enum { WheelUp, WheelDown };

typedef struct {
  pixman_color_t color;
  bool bg;
  char *start;
} Color;

typedef struct {
  uint32_t btn;
  uint32_t x1;
  uint32_t x2;
  char command[128];
} Button;

typedef struct {
  char text[TEXT_MAX];
  Color *colors;
  uint32_t colors_l, colors_c;
  Button *buttons;
  uint32_t buttons_l, buttons_c;
} CustomText;

typedef struct {
  struct wl_output *wl_output;
  struct wl_surface *wl_surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct zxdg_output_v1 *xdg_output;

  uint32_t registry_name;
  char *xdg_output_name;

  bool configured;
  uint32_t width, height;
  uint32_t textpadding;
  uint32_t stride, bufsize;

  uint32_t mtags, ctags, urg, sel;
  char *layout, *window_title;
  char *appid;
  uint32_t layout_idx, last_layout_idx;
  CustomText title, status;

  bool hidden, bottom;
  bool overview;
  bool redraw;

  int ipc_fd;

  struct wl_list link;
} Bar;

typedef struct {
  struct wl_seat *wl_seat;
  struct wl_pointer *wl_pointer;
  uint32_t registry_name;

  Bar *bar;
  uint32_t pointer_x, pointer_y;
  uint32_t pointer_button;

  struct wl_list link;
} Seat;

static int sock_fd;
static char socketdir[256];
static char *socketpath;
static char sockbuf[4096];

static char *stdinbuf;
static size_t stdinbuf_cap;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zxdg_output_manager_v1 *output_manager;

static struct wl_cursor_image *cursor_image;
static struct wl_surface *cursor_surface;

static struct wl_list bar_list, seat_list;

static char **tags;
static uint32_t tags_l, tags_c;
static char **layouts;
static uint32_t layouts_l, layouts_c;

static struct fcft_font *font;
static uint32_t height, textpadding, buffer_scale;

static bool run_display;

/* ---- JSON IPC 相关 ---- */
static int ipc_fd = -1;
static char ipc_buf[65536];
static size_t ipc_buf_len = 0;

#include "config.h"

static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
  wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

static int allocate_shm_file(size_t size) {
  int fd = memfd_create("surface", MFD_CLOEXEC);
  if (fd == -1)
    return -1;
  int ret;
  do {
    ret = ftruncate(fd, size);
  } while (ret == -1 && errno == EINTR);
  if (ret == -1) {
    close(fd);
    return -1;
  }
  return fd;
}

static uint32_t draw_text(char *text, uint32_t x, uint32_t y,
                          pixman_image_t *foreground,
                          pixman_image_t *foreground_mask,
                          pixman_image_t *background, pixman_color_t *fg_color,
                          pixman_color_t *bg_color, uint32_t max_x,
                          uint32_t buf_height, uint32_t padding, Color *colors,
                          uint32_t colors_l) {
  if (!text || !*text || !max_x)
    return x;

  uint32_t ix = x, nx;

  if ((nx = x + padding) + padding >= max_x)
    return x;
  x = nx;

  bool draw_fg = foreground && fg_color;
  bool draw_bg = background && bg_color;

  pixman_image_t *fg_mask_fill;
  pixman_color_t *cur_fg_color;
  pixman_color_t *cur_bg_color;
  if (draw_fg) {
    cur_fg_color = fg_color;
    fg_mask_fill = pixman_image_create_solid_fill(
        &(pixman_color_t){0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF});
  }
  if (draw_bg)
    cur_bg_color = bg_color;

  uint32_t color_ind = 0, codepoint, state = UTF8_ACCEPT, last_cp = 0;
  for (char *p = text; *p; p++) {
    if (state == UTF8_ACCEPT && colors && (draw_fg || draw_bg)) {
      while (color_ind < colors_l && p == colors[color_ind].start) {
        if (colors[color_ind].bg) {
          if (draw_bg)
            cur_bg_color = &colors[color_ind].color;
        } else if (draw_fg) {
          cur_fg_color = &colors[color_ind].color;
        }
        color_ind++;
      }
    }

    if (utf8decode(&state, &codepoint, *p))
      continue;

    const struct fcft_glyph *glyph =
        fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE);
    if (!glyph)
      continue;

    long kern = 0;
    if (last_cp)
      fcft_kerning(font, last_cp, codepoint, &kern, NULL);
    if ((nx = x + kern + glyph->advance.x) + padding > max_x)
      break;
    last_cp = codepoint;
    x += kern;

    if (draw_fg) {
      if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
        pixman_image_composite32(PIXMAN_OP_OVER, glyph->pix, NULL, foreground,
                                 0, 0, 0, 0, x + glyph->x, y - glyph->y,
                                 glyph->width, glyph->height);
      } else {
        pixman_image_fill_boxes(
            PIXMAN_OP_OVER, foreground, cur_fg_color, 1,
            &(pixman_box32_t){.x1 = x, .x2 = nx, .y1 = 0, .y2 = buf_height});
      }
      pixman_image_composite32(PIXMAN_OP_OVER, glyph->pix, fg_mask_fill,
                               foreground_mask, 0, 0, 0, 0, x + glyph->x,
                               y - glyph->y, glyph->width, glyph->height);
    }

    if (draw_bg) {
      pixman_image_fill_boxes(
          PIXMAN_OP_OVER, background, cur_bg_color, 1,
          &(pixman_box32_t){.x1 = x, .x2 = nx, .y1 = 0, .y2 = buf_height});
    }

    x = nx;
  }

  if (draw_fg)
    pixman_image_unref(fg_mask_fill);
  if (!last_cp)
    return ix;

  nx = x + padding;
  if (draw_bg) {
    pixman_image_fill_boxes(
        PIXMAN_OP_OVER, background, bg_color, 1,
        &(pixman_box32_t){
            .x1 = ix, .x2 = ix + padding, .y1 = 0, .y2 = buf_height});
    pixman_image_fill_boxes(
        PIXMAN_OP_OVER, background, bg_color, 1,
        &(pixman_box32_t){.x1 = x, .x2 = nx, .y1 = 0, .y2 = buf_height});
  }

  return nx;
}

#define TEXT_WIDTH(text, maxwidth, padding)                                    \
  draw_text(text, 0, 0, NULL, NULL, NULL, NULL, NULL, maxwidth, 0, padding,    \
            NULL, 0)

static int draw_frame(Bar *bar) {
  int fd = allocate_shm_file(bar->bufsize);
  if (fd == -1)
    return -1;

  uint32_t *data =
      mmap(NULL, bar->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return -1;
  }

  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, bar->bufsize);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, bar->width, bar->height, bar->stride, WL_SHM_FORMAT_ARGB8888);
  wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
  wl_shm_pool_destroy(pool);
  close(fd);

  pixman_image_t *final = pixman_image_create_bits(
      PIXMAN_a8r8g8b8, bar->width, bar->height, data, bar->width * 4);

  pixman_image_t *foreground = pixman_image_create_bits(
      PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);
  pixman_image_t *foreground_mask = pixman_image_create_bits(
      PIXMAN_a8, bar->width, bar->height, NULL, bar->width * 4);
  pixman_image_t *background = pixman_image_create_bits(
      PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);

  uint32_t x = 0;
  uint32_t y = (bar->height + font->ascent - font->descent) / 2;
  uint32_t boxs = font->height / 9;
  uint32_t boxw = font->height / 6 + 2;

  if (bar->overview) {
    x = draw_text("OVERVIEW", x, y, foreground, foreground_mask, background,
                  &active_fg_color, &active_bg_color, bar->width, bar->height,
                  bar->textpadding, NULL, 0);
  } else {
    for (uint32_t i = 0; i < tags_l; i++) {
      const bool active = bar->mtags & 1 << i;
      const bool occupied = bar->ctags & 1 << i;
      const bool urgent = bar->urg & 1 << i;

      if (hide_vacant && !active && !occupied && !urgent)
        continue;

      pixman_color_t *fg_color =
          urgent
              ? &urgent_fg_color
              : (active ? &active_fg_color
                        : (occupied ? &occupied_fg_color : &inactive_fg_color));
      pixman_color_t *bg_color =
          urgent
              ? &urgent_bg_color
              : (active ? &active_bg_color
                        : (occupied ? &occupied_bg_color : &inactive_bg_color));

      if (!hide_vacant && occupied) {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, fg_color, 1,
                                &(pixman_box32_t){.x1 = x + boxs,
                                                  .x2 = x + boxs + boxw,
                                                  .y1 = boxs,
                                                  .y2 = boxs + boxw});
        pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground_mask,
                                &(pixman_color_t){0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
                                1,
                                &(pixman_box32_t){.x1 = x + boxs,
                                                  .x2 = x + boxs + boxw,
                                                  .y1 = boxs,
                                                  .y2 = boxs + boxw});
        if ((!bar->sel || !active) && boxw >= 3) {
          pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, &(pixman_color_t){0},
                                  1,
                                  &(pixman_box32_t){.x1 = x + boxs + 1,
                                                    .x2 = x + boxs + boxw - 1,
                                                    .y1 = boxs + 1,
                                                    .y2 = boxs + boxw - 1});
          pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground_mask,
                                  &(pixman_color_t){0}, 1,
                                  &(pixman_box32_t){.x1 = x + boxs + 1,
                                                    .x2 = x + boxs + boxw - 1,
                                                    .y1 = boxs + 1,
                                                    .y2 = boxs + boxw - 1});
        }
      }

      x = draw_text(tags[i], x, y, foreground, foreground_mask, background,
                    fg_color, bg_color, bar->width, bar->height, bar->textpadding,
                    NULL, 0);
    }
  }

  x = draw_text(bar->layout, x, y, foreground, foreground_mask, background,
                &inactive_fg_color, &inactive_bg_color, bar->width, bar->height,
                bar->textpadding, NULL, 0);

  uint32_t status_width =
      TEXT_WIDTH(bar->status.text, bar->width - x, bar->textpadding);
  draw_text(bar->status.text, bar->width - status_width, y, foreground,
            foreground_mask, background, &inactive_fg_color, &inactive_bg_color,
            bar->width, bar->height, bar->textpadding, bar->status.colors,
            bar->status.colors_l);

  uint32_t nx;
  if (center_title) {
    uint32_t title_width =
        TEXT_WIDTH(custom_title ? bar->title.text : bar->window_title,
                   bar->width - status_width - x, 0);
    nx = MAX(x, MIN((bar->width - title_width) / 2,
                    bar->width - status_width - title_width));
  } else {
    nx = MIN(x + bar->textpadding, bar->width - status_width);
  }
  pixman_image_fill_boxes(
      PIXMAN_OP_SRC, background,
      bar->sel ? &middle_bg_color_selected : &middle_bg_color, 1,
      &(pixman_box32_t){.x1 = x, .x2 = nx, .y1 = 0, .y2 = bar->height});
  x = nx;

  x = draw_text(
    custom_title ? bar->title.text : bar->window_title, x, y, foreground,
    foreground_mask, background,
    bar->sel ? &title_fg_color_selected : &title_fg_color,  
    &middle_bg_color,                                        
    bar->width - status_width, bar->height, 0,
    custom_title ? bar->title.colors : NULL,
    custom_title ? bar->title.colors_l : 0);

  pixman_image_fill_boxes(
      PIXMAN_OP_SRC, background,
      bar->sel ? &middle_bg_color_selected : &middle_bg_color, 1,
      &(pixman_box32_t){.x1 = x,
                        .x2 = bar->width - status_width,
                        .y1 = 0,
                        .y2 = bar->height});

  pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, final, 0, 0, 0, 0,
                           0, 0, bar->width, bar->height);
  pixman_image_set_alpha_map(foreground, foreground_mask, 0, 0);
  pixman_image_composite32(PIXMAN_OP_OVER, foreground, foreground_mask, final,
                           0, 0, 0, 0, 0, 0, bar->width, bar->height);

  pixman_image_unref(foreground);
  pixman_image_unref(foreground_mask);
  pixman_image_unref(background);
  pixman_image_unref(final);

  munmap(data, bar->bufsize);

  wl_surface_set_buffer_scale(bar->wl_surface, buffer_scale);
  wl_surface_attach(bar->wl_surface, buffer, 0, 0);
  wl_surface_damage_buffer(bar->wl_surface, 0, 0, bar->width, bar->height);
  wl_surface_commit(bar->wl_surface);

  return 0;
}

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  w = w * buffer_scale;
  h = h * buffer_scale;
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  Bar *bar = (Bar *)data;
  if (bar->configured && w == bar->width && h == bar->height)
    return;
  bar->width = w;
  bar->height = h;
  bar->stride = bar->width * 4;
  bar->bufsize = bar->stride * bar->height;
  bar->configured = true;
  draw_frame(bar);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void cleanup(void) {
  if (socketpath)
    unlink(socketpath);
}

static void output_name(void *data, struct zxdg_output_v1 *xdg_output,
                        const char *name) {
  Bar *bar = (Bar *)data;
  if (bar->xdg_output_name)
    free(bar->xdg_output_name);
  if (!(bar->xdg_output_name = strdup(name)))
    EDIE("strdup");
}

static void output_logical_position(void *data,
                                    struct zxdg_output_v1 *xdg_output,
                                    int32_t x, int32_t y) {}
static void output_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                                int32_t width, int32_t height) {}
static void output_done(void *data, struct zxdg_output_v1 *xdg_output) {}
static void output_description(void *data, struct zxdg_output_v1 *xdg_output,
                               const char *description) {}

static const struct zxdg_output_v1_listener output_listener = {
    .name = output_name,
    .logical_position = output_logical_position,
    .logical_size = output_logical_size,
    .done = output_done,
    .description = output_description};

static void shell_command(char *command) {
  if (fork() == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", command, NULL);
    exit(EXIT_SUCCESS);
  }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t surface_x, wl_fixed_t surface_y) {
  Seat *seat = (Seat *)data;
  seat->bar = NULL;
  Bar *bar;
  wl_list_for_each(bar, &bar_list, link) {
    if (bar->wl_surface == surface) {
      seat->bar = bar;
      break;
    }
  }
  if (!cursor_image) {
    const char *size_str = getenv("XCURSOR_SIZE");
    int size = size_str ? atoi(size_str) : 0;
    if (size == 0) size = 24;
    struct wl_cursor_theme *cursor_theme =
        wl_cursor_theme_load(getenv("XCURSOR_THEME"), size * buffer_scale, shm);
    cursor_image =
        wl_cursor_theme_get_cursor(cursor_theme, "left_ptr")->images[0];
    cursor_surface = wl_compositor_create_surface(compositor);
    wl_surface_set_buffer_scale(cursor_surface, buffer_scale);
    wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(cursor_image),
                      0, 0);
    wl_surface_commit(cursor_surface);
  }
  wl_pointer_set_cursor(pointer, serial, cursor_surface,
                        cursor_image->hotspot_x, cursor_image->hotspot_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
  Seat *seat = (Seat *)data;
  seat->bar = NULL;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  Seat *seat = (Seat *)data;
  seat->pointer_button = state == WL_POINTER_BUTTON_STATE_PRESSED ? button : 0;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t surface_x,
                           wl_fixed_t surface_y) {
  Seat *seat = (Seat *)data;
  seat->pointer_x = wl_fixed_to_int(surface_x);
  seat->pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
  Seat *seat = (Seat *)data;
  if (!seat->pointer_button || !seat->bar)
    return;

  Bar *bar = seat->bar;
  uint32_t x = 0, i = 0;

  if (!bar->overview) {
    do {
      if (hide_vacant) {
        const bool active = bar->mtags & 1 << i;
        const bool occupied = bar->ctags & 1 << i;
        const bool urgent = bar->urg & 1 << i;
        if (!active && !occupied && !urgent)
          continue;
      }
      x += TEXT_WIDTH(tags[i], bar->width - x, bar->textpadding) / buffer_scale;
    } while (seat->pointer_x >= x && ++i < tags_l);

    if (i < tags_l) {
      if (ipc && ipc_fd >= 0) {
        char cmd[256];
        if (seat->pointer_button == BTN_LEFT)
          snprintf(cmd, sizeof cmd, "dispatch set_tags %s %u 1\n",
                   bar->xdg_output_name, 1 << i);
        else if (seat->pointer_button == BTN_MIDDLE)
          snprintf(cmd, sizeof cmd, "dispatch set_tags %s %u 1\n",
                   bar->xdg_output_name, (unsigned)~0);
        else if (seat->pointer_button == BTN_RIGHT)
          snprintf(cmd, sizeof cmd, "dispatch set_tags %s %u 0\n",
                   bar->xdg_output_name, bar->mtags ^ (1 << i));
        send(ipc_fd, cmd, strlen(cmd), MSG_NOSIGNAL);
      }
      seat->pointer_button = 0;
      return;
    }
  }

  x += TEXT_WIDTH(bar->layout, bar->width - x, bar->textpadding) / buffer_scale;
  if (seat->pointer_x < x) {
    /* 点击布局 -> JSON IPC dispatch */
    if (ipc && ipc_fd >= 0) {
      char cmd[256];
      if (seat->pointer_button == BTN_LEFT)
        snprintf(cmd, sizeof cmd, "dispatch set_layout %s %u\n",
                 bar->xdg_output_name, bar->last_layout_idx);
      else if (seat->pointer_button == BTN_RIGHT)
        snprintf(cmd, sizeof cmd, "dispatch set_layout %s 2\n",
                 bar->xdg_output_name);
      send(ipc_fd, cmd, strlen(cmd), MSG_NOSIGNAL);
    }
  } else {
    uint32_t status_x = bar->width / buffer_scale -
                        TEXT_WIDTH(bar->status.text, bar->width - x,
                                   bar->textpadding) / buffer_scale;
    if (seat->pointer_x < status_x) {
      if (custom_title) {
        if (center_title) {
          uint32_t title_width =
              TEXT_WIDTH(bar->title.text, status_x - x, 0);
          x = MAX(x, MIN((bar->width - title_width) / 2,
                         status_x - title_width));
        } else {
          x = MIN(x + bar->textpadding, status_x);
        }
        for (i = 0; i < bar->title.buttons_l; i++) {
          if (seat->pointer_button == bar->title.buttons[i].btn &&
              seat->pointer_x >= x + bar->title.buttons[i].x1 &&
              seat->pointer_x < x + bar->title.buttons[i].x2) {
            shell_command(bar->title.buttons[i].command);
            break;
          }
        }
      }
    } else {
      for (i = 0; i < bar->status.buttons_l; i++) {
        if (seat->pointer_button == bar->status.buttons[i].btn &&
            seat->pointer_x >=
                status_x + bar->textpadding +
                    bar->status.buttons[i].x1 / buffer_scale &&
            seat->pointer_x <
                status_x + bar->textpadding +
                    bar->status.buttons[i].x2 / buffer_scale) {
          shell_command(bar->status.buttons[i].command);
          break;
        }
      }
    }
  }
  seat->pointer_button = 0;
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete) {
  uint32_t i;
  uint32_t btn = discrete < 0 ? WheelUp : WheelDown;
  Seat *seat = (Seat *)data;
  if (!seat->bar) return;
  uint32_t status_x = seat->bar->width / buffer_scale -
                      TEXT_WIDTH(seat->bar->status.text, seat->bar->width,
                                 seat->bar->textpadding) / buffer_scale;
  if (seat->pointer_x > status_x) {
    for (i = 0; i < seat->bar->status.buttons_l; i++) {
      if (btn == seat->bar->status.buttons[i].btn &&
          seat->pointer_x >=
              status_x + seat->bar->textpadding +
                  seat->bar->status.buttons[i].x1 / buffer_scale &&
          seat->pointer_x <
              status_x + seat->bar->textpadding +
                  seat->bar->status.buttons[i].x2 / buffer_scale) {
        shell_command(seat->bar->status.buttons[i].command);
        break;
      }
    }
  }
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis) {}
static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .axis = pointer_axis,
    .axis_discrete = pointer_axis_discrete,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_value120 = pointer_axis_value120,
    .button = pointer_button,
    .enter = pointer_enter,
    .frame = pointer_frame,
    .leave = pointer_leave,
    .motion = pointer_motion,
};

static void seat_capabilities(void *data, struct wl_seat *wl_seat,
                              uint32_t capabilities) {
  Seat *seat = (Seat *)data;
  uint32_t has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (has_pointer && !seat->wl_pointer) {
    seat->wl_pointer = wl_seat_get_pointer(seat->wl_seat);
    wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
  } else if (!has_pointer && seat->wl_pointer) {
    wl_pointer_destroy(seat->wl_pointer);
    seat->wl_pointer = NULL;
  }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void show_bar(Bar *bar) {
  bar->wl_surface = wl_compositor_create_surface(compositor);
  if (!bar->wl_surface)
    DIE("Could not create wl_surface");
  bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, bar->wl_surface, bar->wl_output,
      ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, PROGRAM);
  if (!bar->layer_surface)
    DIE("Could not create layer_surface");
  zwlr_layer_surface_v1_add_listener(bar->layer_surface,
                                     &layer_surface_listener, bar);
  zwlr_layer_surface_v1_set_size(bar->layer_surface, 0,
                                 bar->height / buffer_scale);
  zwlr_layer_surface_v1_set_anchor(
      bar->layer_surface, (bar->bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                       : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface,
                                           bar->height / buffer_scale);
  wl_surface_commit(bar->wl_surface);
  bar->hidden = false;
}

static void hide_bar(Bar *bar) {
  zwlr_layer_surface_v1_destroy(bar->layer_surface);
  wl_surface_destroy(bar->wl_surface);
  bar->configured = false;
  bar->hidden = true;
}

static void check_auto_hide(Bar *bar) {
  if (!bar->window_title || !bar->appid) {
    if (bar->hidden) show_bar(bar);
    return;
  }
  bool is_terminal =
      (strcmp(bar->appid, "foot") == 0 || strcmp(bar->appid, "kitty") == 0);
  bool is_tmux = (strstr(bar->window_title, "tmux") != NULL);
  if (is_terminal && is_tmux) {
    if (!bar->hidden) hide_bar(bar);
  } else {
    if (bar->hidden) show_bar(bar);
  }
}

/* ========== JSON IPC  ========== */

static Bar *find_bar_by_name(const char *name) {
  Bar *bar;
  wl_list_for_each(bar, &bar_list, link) {
    if (bar->xdg_output_name && strcmp(bar->xdg_output_name, name) == 0)
      return bar;
  }
  return NULL;
}

static void ipc_update_bar_from_json(Bar *bar, cJSON *json) {
  if (!bar || !json) return;

  /* active */
  cJSON *active = cJSON_GetObjectItem(json, "active");
  if (cJSON_IsBool(active))
    bar->sel = cJSON_IsTrue(active) ? 1 : 0;

  /* layout_symbol */
  cJSON *layout_sym = cJSON_GetObjectItem(json, "layout_symbol");
  if (cJSON_IsString(layout_sym)) {
    free(bar->layout);
	const char *sym = layout_sym->valuestring;
    if      (!strcmp(sym, "DW")) sym = "[]=";
    else if (!strcmp(sym, "M"))  sym = "[M]";
    bar->layout = strdup(sym);
  }

  /* layout_index */
  cJSON *layout_idx = cJSON_GetObjectItem(json, "layout_index");
  if (cJSON_IsNumber(layout_idx))
    bar->layout_idx = layout_idx->valueint;

  /* active_client */
  cJSON *active_client = cJSON_GetObjectItem(json, "active_client");
  if (active_client && !cJSON_IsNull(active_client)) {
    cJSON *title = cJSON_GetObjectItem(active_client, "title");
    cJSON *appid = cJSON_GetObjectItem(active_client, "appid");
    if (cJSON_IsString(title)) {
      free(bar->window_title);
      bar->window_title = strdup(title->valuestring);
    } else {
      free(bar->window_title);
      bar->window_title = NULL;
    }
    if (cJSON_IsString(appid)) {
      free(bar->appid);
      bar->appid = strdup(appid->valuestring);
    } else {
      free(bar->appid);
      bar->appid = NULL;
    }
  } else {
    free(bar->window_title);
    bar->window_title = NULL;
    free(bar->appid);
    bar->appid = NULL;
  }

  /* tags */
  cJSON *tags_array = cJSON_GetObjectItem(json, "tags");
  if (cJSON_IsArray(tags_array)) {
    bar->mtags = bar->ctags = bar->urg = 0;
    cJSON *tag_obj;
    cJSON_ArrayForEach(tag_obj, tags_array) {
      int tag = cJSON_GetObjectItem(tag_obj, "index")->valueint - 1;
      if (tag < 0 || tag >= 32) continue;
      if (cJSON_IsTrue(cJSON_GetObjectItem(tag_obj, "is_active")))
        bar->mtags |= (1 << tag);
      if (cJSON_IsTrue(cJSON_GetObjectItem(tag_obj, "is_urgent")))
        bar->urg |= (1 << tag);
      if (cJSON_GetObjectItem(tag_obj, "client_count")->valueint > 0)
        bar->ctags |= (1 << tag);
    }
  }

  /* active_tags，OVERVIEW */
  cJSON *active_tags = cJSON_GetObjectItem(json, "active_tags");
  bar->overview = false;
  if (cJSON_IsArray(active_tags)) {
    cJSON *tag_idx;
    cJSON_ArrayForEach(tag_idx, active_tags) {
      if (cJSON_IsNumber(tag_idx) && tag_idx->valueint == 0) {
        bar->overview = true;
        break;
      }
    }
  }

  check_auto_hide(bar);
  bar->redraw = true;
}

static void ipc_handle_message(const char *msg) {
  cJSON *json = cJSON_Parse(msg);
  if (!json) return;

  cJSON *name = cJSON_GetObjectItem(json, "name");
  if (cJSON_IsString(name)) {
    Bar *bar = find_bar_by_name(name->valuestring);
    if (bar) ipc_update_bar_from_json(bar, json);
  }

  cJSON_Delete(json);
}

static void ipc_process_data(void) {
  char *line = ipc_buf, *end;
  while ((end = memchr(line, '\n', ipc_buf_len - (line - ipc_buf)))) {
    *end = '\0';
    ipc_handle_message(line);
    line = end + 1;
  }
  size_t remaining = ipc_buf_len - (line - ipc_buf);
  memmove(ipc_buf, line, remaining);
  ipc_buf_len = remaining;
}

static void ipc_connect(void) {
  const char *sock_path = getenv("MANGO_INSTANCE_SIGNATURE");
  if (!sock_path) {
    fprintf(stderr, "IPC: MANGO_INSTANCE_SIGNATURE not set\n");
    return;
  }

  ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (ipc_fd < 0) {
    perror("IPC socket");
    return;
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (connect(ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("IPC connect");
    close(ipc_fd);
    ipc_fd = -1;
    return;
  }

  int flags = fcntl(ipc_fd, F_GETFL, 0);
  fcntl(ipc_fd, F_SETFL, flags | O_NONBLOCK);
}

static void ipc_send_watch(Bar *bar) {
  if (!bar->xdg_output_name) return;

  const char *sock_path = getenv("MANGO_INSTANCE_SIGNATURE");
  if (!sock_path) return;

  bar->ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (bar->ipc_fd < 0) { perror("bar ipc socket"); return; }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (connect(bar->ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bar ipc connect");
    close(bar->ipc_fd);
    bar->ipc_fd = -1;
    return;
  }

  int flags = fcntl(bar->ipc_fd, F_GETFL, 0);
  fcntl(bar->ipc_fd, F_SETFL, flags | O_NONBLOCK);

  char msg[256];
  snprintf(msg, sizeof msg, "watch monitor %s\n", bar->xdg_output_name);
  send(bar->ipc_fd, msg, strlen(msg), MSG_NOSIGNAL);
}

static void setup_bar(Bar *bar) {
  bar->height = height * buffer_scale;
  bar->textpadding = textpadding;
  bar->bottom = bottom;
  bar->hidden = hidden;

  bar->xdg_output =
      zxdg_output_manager_v1_get_xdg_output(output_manager, bar->wl_output);
  if (!bar->xdg_output)
    DIE("Could not create xdg_output");
  zxdg_output_v1_add_listener(bar->xdg_output, &output_listener, bar);

  if (!bar->hidden)
    show_bar(bar);
}

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  if (!strcmp(interface, wl_compositor_interface.name)) {
    compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (!strcmp(interface, wl_shm_interface.name)) {
    shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
    layer_shell =
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (!strcmp(interface, zxdg_output_manager_v1_interface.name)) {
    output_manager =
        wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 2);
  } else if (!strcmp(interface, wl_output_interface.name)) {
    Bar *bar = calloc(1, sizeof(Bar));
    if (!bar) EDIE("calloc");
    bar->registry_name = name;
	bar->ipc_fd = -1;
    bar->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    if (run_display) setup_bar(bar);
    wl_list_insert(&bar_list, &bar->link);
  } else if (!strcmp(interface, wl_seat_interface.name)) {
    Seat *seat = calloc(1, sizeof(Seat));
    if (!seat) EDIE("calloc");
    seat->registry_name = name;
    seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
    wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
    wl_list_insert(&seat_list, &seat->link);
  }
}

static void teardown_bar(Bar *bar) {
  if (bar->status.colors) free(bar->status.colors);
  if (bar->status.buttons) free(bar->status.buttons);
  if (bar->title.colors) free(bar->title.colors);
  if (bar->title.buttons) free(bar->title.buttons);
  if (bar->window_title) free(bar->window_title);
  if (bar->appid) free(bar->appid);
  if (bar->layout) free(bar->layout);
  if (bar->xdg_output_name) free(bar->xdg_output_name);
  if (!bar->hidden) {
    zwlr_layer_surface_v1_destroy(bar->layer_surface);
    wl_surface_destroy(bar->wl_surface);
  }
  zxdg_output_v1_destroy(bar->xdg_output);
  wl_output_destroy(bar->wl_output);
  free(bar);
}

static void teardown_seat(Seat *seat) {
  if (seat->wl_pointer) wl_pointer_destroy(seat->wl_pointer);
  wl_seat_destroy(seat->wl_seat);
  free(seat);
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  Bar *bar;
  Seat *seat;
  wl_list_for_each(bar, &bar_list, link) {
    if (bar->registry_name == name) {
      wl_list_remove(&bar->link);
      teardown_bar(bar);
      return;
    }
  }
  wl_list_for_each(seat, &seat_list, link) {
    if (seat->registry_name == name) {
      wl_list_remove(&seat->link);
      teardown_seat(seat);
      return;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static int advance_word(char **beg, char **end) {
  for (*beg = *end; **beg == ' '; (*beg)++)
    ;
  for (*end = *beg; **end && **end != ' '; (*end)++)
    ;
  if (!**end)
    return -1;
  **end = '\0';
  (*end)++;
  return 0;
}

#define ADVANCE() advance_word(&wordbeg, &wordend)
#define ADVANCE_IF_LAST_CONT() if (ADVANCE() == -1) continue
#define ADVANCE_IF_LAST_RET()  if (ADVANCE() == -1) return

static void read_stdin(void) {
  size_t len = 0;
  for (;;) {
    ssize_t rv = read(STDIN_FILENO, stdinbuf + len, stdinbuf_cap - len);
    if (rv == -1) {
      if (errno == EWOULDBLOCK)
        break;
      EDIE("read");
    }
    if (rv == 0) {
      run_display = false;
      return;
    }

    if ((len += rv) > stdinbuf_cap / 2)
      if (!(stdinbuf = realloc(stdinbuf, (stdinbuf_cap *= 2))))
        EDIE("realloc");
  }

  char *linebeg, *lineend;
  char *wordbeg, *wordend;

  for (linebeg = stdinbuf;
       (lineend = memchr(linebeg, '\n', stdinbuf + len - linebeg));
       linebeg = lineend) {
    *lineend++ = '\0';
    wordend = linebeg;

    ADVANCE_IF_LAST_CONT();

    Bar *it, *bar = NULL;
    wl_list_for_each(it, &bar_list, link) {
      if (it->xdg_output_name && !strcmp(wordbeg, it->xdg_output_name)) {
        bar = it;
        break;
      }
    }
    if (!bar)
      continue;

    ADVANCE_IF_LAST_CONT();

    uint32_t val;
    if (!strcmp(wordbeg, "tags")) {
      ADVANCE_IF_LAST_CONT();
      if ((val = atoi(wordbeg)) != bar->ctags) {
        bar->ctags = val;
        bar->redraw = true;
      }
      ADVANCE_IF_LAST_CONT();
      if ((val = atoi(wordbeg)) != bar->mtags) {
        bar->mtags = val;
        bar->redraw = true;
      }
      ADVANCE_IF_LAST_CONT();
      /* skip sel */
      ADVANCE();
      if ((val = atoi(wordbeg)) != bar->urg) {
        bar->urg = val;
        bar->redraw = true;
      }
    } else if (!strcmp(wordbeg, "layout")) {
      if (bar->layout)
        free(bar->layout);
      if (!(bar->layout = strdup(wordend)))
        EDIE("strdup");
      bar->redraw = true;
    } else if (!strcmp(wordbeg, "title")) {
      if (custom_title)
        continue;
      if (bar->window_title)
        free(bar->window_title);
      if (!(bar->window_title = strdup(wordend)))
        EDIE("strdup");
      bar->redraw = true;
    } else if (!strcmp(wordbeg, "selmon")) {
      ADVANCE();
      if ((val = atoi(wordbeg)) != bar->sel) {
        bar->sel = val;
        bar->redraw = true;
      }
    }
  }
}

static void set_top(Bar *bar) {
  if (!bar->hidden) {
    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    bar->redraw = true;
  }
  bar->bottom = false;
}

static void set_bottom(Bar *bar) {
  if (!bar->hidden) {
    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    bar->redraw = true;
  }
  bar->bottom = true;
}

static int parse_color(const char *str, pixman_color_t *clr) {
  if (*str == '#')
    str++;
  int len = strlen(str);
  if ((len != 6 && len != 8) || !isxdigit(str[0]) || !isxdigit(str[1]))
    return -1;
  char *ptr;
  uint32_t parsed = strtoul(str, &ptr, 16);
  if (*ptr)
    return -1;
  if (len == 8) {
    clr->alpha = (parsed & 0xff) * 0x101;
    parsed >>= 8;
  } else {
    clr->alpha = 0xffff;
  }
  clr->red = ((parsed >> 16) & 0xff) * 0x101;
  clr->green = ((parsed >> 8) & 0xff) * 0x101;
  clr->blue = ((parsed >> 0) & 0xff) * 0x101;
  return 0;
}

static void parse_into_customtext(CustomText *ct, char *text) {
  ct->colors_l = ct->buttons_l = 0;
  if (status_commands) {
    uint32_t codepoint;
    uint32_t state = UTF8_ACCEPT;
    uint32_t last_cp = 0;
    uint32_t x = 0;
    size_t str_pos = 0;
    Button *left_button = NULL;
    Button *middle_button = NULL;
    Button *right_button = NULL;
    Button *scrollup_button = NULL;
    Button *scrolldown_button = NULL;
    for (char *p = text; *p && str_pos < sizeof(ct->text) - 1; p++) {
      if (state == UTF8_ACCEPT && *p == '^') {
        p++;
        if (*p != '^') {
          char *arg, *end;
          if (!(arg = strchr(p, '(')) || !(end = strchr(arg + 1, ')')))
            continue;
          *arg++ = '\0';
          *end = '\0';
          if (!strcmp(p, "bg")) {
            Color *color;
            ARRAY_APPEND(ct->colors, ct->colors_l, ct->colors_c, color);
            if (!*arg)
              color->color = inactive_bg_color;
            else
              parse_color(arg, &color->color);
            color->bg = true;
            color->start = ct->text + str_pos;
          } else if (!strcmp(p, "fg")) {
            Color *color;
            ARRAY_APPEND(ct->colors, ct->colors_l, ct->colors_c, color);
            if (!*arg)
              color->color = inactive_fg_color;
            else
              parse_color(arg, &color->color);
            color->bg = false;
            color->start = ct->text + str_pos;
          } else if (!strcmp(p, "lm")) {
            if (left_button) {
              left_button->x2 = x;
              left_button = NULL;
            } else if (*arg) {
              ARRAY_APPEND(ct->buttons, ct->buttons_l, ct->buttons_c,
                           left_button);
              left_button->btn = BTN_LEFT;
              snprintf(left_button->command, sizeof left_button->command, "%s",
                       arg);
              left_button->x1 = x;
            }
          } else if (!strcmp(p, "mm")) {
            if (middle_button) {
              middle_button->x2 = x;
              middle_button = NULL;
            } else if (*arg) {
              ARRAY_APPEND(ct->buttons, ct->buttons_l, ct->buttons_c,
                           middle_button);
              middle_button->btn = BTN_MIDDLE;
              snprintf(middle_button->command, sizeof middle_button->command,
                       "%s", arg);
              middle_button->x1 = x;
            }
          } else if (!strcmp(p, "rm")) {
            if (right_button) {
              right_button->x2 = x;
              right_button = NULL;
            } else if (*arg) {
              ARRAY_APPEND(ct->buttons, ct->buttons_l, ct->buttons_c,
                           right_button);
              right_button->btn = BTN_RIGHT;
              snprintf(right_button->command, sizeof right_button->command,
                       "%s", arg);
              right_button->x1 = x;
            }
          } else if (!strcmp(p, "us")) {
            if (scrollup_button) {
              scrollup_button->x2 = x;
              scrollup_button = NULL;
            } else if (*arg) {
              ARRAY_APPEND(ct->buttons, ct->buttons_l, ct->buttons_c,
                           scrollup_button);
              scrollup_button->btn = WheelUp;
              snprintf(scrollup_button->command,
                       sizeof scrollup_button->command, "%s", arg);
              scrollup_button->x1 = x;
            }
          } else if (!strcmp(p, "ds")) {
            if (scrolldown_button) {
              scrolldown_button->x2 = x;
              scrolldown_button = NULL;
            } else if (*arg) {
              ARRAY_APPEND(ct->buttons, ct->buttons_l, ct->buttons_c,
                           scrolldown_button);
              scrolldown_button->btn = WheelDown;
              snprintf(scrolldown_button->command,
                       sizeof scrolldown_button->command, "%s", arg);
              scrolldown_button->x1 = x;
            }
          }
          *--arg = '(';
          *end = ')';
          p = end;
          continue;
        }
      }
      ct->text[str_pos++] = *p;
      if (utf8decode(&state, &codepoint, *p))
        continue;
      const struct fcft_glyph *glyph =
          fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE);
      if (!glyph)
        continue;
      long kern = 0;
      if (last_cp)
        fcft_kerning(font, last_cp, codepoint, &kern, NULL);
      last_cp = codepoint;
      x += kern + glyph->advance.x;
    }
    if (left_button) left_button->x2 = x;
    if (middle_button) middle_button->x2 = x;
    if (right_button) right_button->x2 = x;
    if (scrollup_button) scrollup_button->x2 = x;
    if (scrolldown_button) scrolldown_button->x2 = x;
    ct->text[str_pos] = '\0';
  } else {
    snprintf(ct->text, sizeof ct->text, "%s", text);
  }
}

static void copy_customtext(CustomText *from, CustomText *to) {
  snprintf(to->text, sizeof to->text, "%s", from->text);
  to->colors_l = to->buttons_l = 0;
  for (uint32_t i = 0; i < from->colors_l; i++) {
    Color *color;
    ARRAY_APPEND(to->colors, to->colors_l, to->colors_c, color);
    color->color = from->colors[i].color;
    color->bg = from->colors[i].bg;
    color->start =
        from->colors[i].start - (char *)&from->text + (char *)&to->text;
  }
  for (uint32_t i = 0; i < from->buttons_l; i++) {
    Button *button;
    ARRAY_APPEND(to->buttons, to->buttons_l, to->buttons_c, button);
    *button = from->buttons[i];
  }
}

static void read_socket(void) {
  int cli_fd;
  if ((cli_fd = accept(sock_fd, NULL, 0)) == -1)
    EDIE("accept");
  ssize_t len = recv(cli_fd, sockbuf, sizeof sockbuf - 1, 0);
  if (len == -1)
    EDIE("recv");
  close(cli_fd);
  if (len == 0)
    return;
  sockbuf[len] = '\0';

  char *wordbeg, *wordend;
  wordend = (char *)&sockbuf;
  ADVANCE_IF_LAST_RET();

  Bar *bar = NULL, *it;
  bool all = false;

  if (!strcmp(wordbeg, "all")) {
    all = true;
  } else if (!strcmp(wordbeg, "selected")) {
    wl_list_for_each(it, &bar_list, link) {
      if (it->sel) {
        bar = it;
        break;
      }
    }
  } else {
    wl_list_for_each(it, &bar_list, link) {
      if (it->xdg_output_name && !strcmp(wordbeg, it->xdg_output_name)) {
        bar = it;
        break;
      }
    }
  }

  if (!all && !bar)
    return;

  ADVANCE();

  if (!strcmp(wordbeg, "status")) {
    if (!*wordend)
      return;
    if (all) {
      Bar *first = NULL;
      wl_list_for_each(bar, &bar_list, link) {
        if (first) {
          copy_customtext(&first->status, &bar->status);
        } else {
          parse_into_customtext(&bar->status, wordend);
          first = bar;
        }
        bar->redraw = true;
      }
    } else {
      parse_into_customtext(&bar->status, wordend);
      bar->redraw = true;
    }
  } else if (!strcmp(wordbeg, "title")) {
    if (!custom_title || !*wordend)
      return;
    if (all) {
      Bar *first = NULL;
      wl_list_for_each(bar, &bar_list, link) {
        if (first) {
          copy_customtext(&first->title, &bar->title);
        } else {
          parse_into_customtext(&bar->title, wordend);
          first = bar;
        }
        bar->redraw = true;
      }
    } else {
      parse_into_customtext(&bar->title, wordend);
      bar->redraw = true;
    }
  } else if (!strcmp(wordbeg, "show")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (bar->hidden) show_bar(bar);
    } else {
      if (bar->hidden) show_bar(bar);
    }
  } else if (!strcmp(wordbeg, "hide")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (!bar->hidden) hide_bar(bar);
    } else {
      if (!bar->hidden) hide_bar(bar);
    }
  } else if (!strcmp(wordbeg, "toggle-visibility")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (bar->hidden) show_bar(bar);
      else hide_bar(bar);
    } else {
      if (bar->hidden) show_bar(bar);
      else hide_bar(bar);
    }
  } else if (!strcmp(wordbeg, "set-top")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (bar->bottom) set_top(bar);
    } else {
      if (bar->bottom) set_top(bar);
    }
  } else if (!strcmp(wordbeg, "set-bottom")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (!bar->bottom) set_bottom(bar);
    } else {
      if (!bar->bottom) set_bottom(bar);
    }
  } else if (!strcmp(wordbeg, "toggle-location")) {
    if (all) {
      wl_list_for_each(bar, &bar_list, link) if (bar->bottom) set_top(bar);
      else set_bottom(bar);
    } else {
      if (bar->bottom) set_top(bar);
      else set_bottom(bar);
    }
  }
}

static void event_loop(void) {
  int wl_fd = wl_display_get_fd(display);
  while (run_display) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(wl_fd, &rfds);
    FD_SET(sock_fd, &rfds);
    if (!ipc)
      FD_SET(STDIN_FILENO, &rfds);

    int max_fd = MAX(sock_fd, wl_fd);

    Bar *bar;
    if (ipc) {
      wl_list_for_each(bar, &bar_list, link) {
        if (bar->ipc_fd >= 0) {
          FD_SET(bar->ipc_fd, &rfds);
          max_fd = MAX(max_fd, bar->ipc_fd);
        }
      }
    }

    wl_display_flush(display);

    if (select(max_fd + 1, &rfds, NULL, NULL, NULL) == -1) {
      if (errno == EINTR) continue;
      EDIE("select");
    }

    if (FD_ISSET(wl_fd, &rfds))
      if (wl_display_dispatch(display) == -1)
        break;
    if (FD_ISSET(sock_fd, &rfds))
      read_socket();
    if (!ipc && FD_ISSET(STDIN_FILENO, &rfds))
      read_stdin();

    if (ipc) {
      wl_list_for_each(bar, &bar_list, link) {
        if (bar->ipc_fd >= 0 && FD_ISSET(bar->ipc_fd, &rfds)) {
          ssize_t n = read(bar->ipc_fd, ipc_buf + ipc_buf_len,
                           sizeof(ipc_buf) - ipc_buf_len - 1);
          if (n > 0) {
            ipc_buf_len += n;
            ipc_buf[ipc_buf_len] = '\0';
            ipc_process_data();
          } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(bar->ipc_fd);
            bar->ipc_fd = -1;
            ipc_buf_len = 0;
            fprintf(stderr, "IPC connection lost for %s\n", bar->xdg_output_name);
          }
        }
      }
    }

    wl_list_for_each(bar, &bar_list, link) {
      if (bar->redraw) {
        if (!bar->hidden) draw_frame(bar);
        bar->redraw = false;
      }
    }
  }
}

static void client_send_command(struct sockaddr_un *sock_address,
                                const char *output, const char *cmd,
                                const char *data, const char *target_socket) {
  DIR *dir;
  if (!(dir = opendir(socketdir)))
    EDIE("Could not open directory '%s'", socketdir);
  if (data)
    snprintf(sockbuf, sizeof sockbuf, "%s %s %s", output, cmd, data);
  else
    snprintf(sockbuf, sizeof sockbuf, "%s %s", output, cmd);
  size_t len = strlen(sockbuf);
  struct dirent *de;
  bool newfd = true;
  while ((de = readdir(dir))) {
    if (!strncmp(de->d_name, "somebar-", 9)) {
      if (!target_socket || !strncmp(de->d_name, target_socket, 6)) {
        if (newfd && (sock_fd = socket(AF_UNIX, SOCK_STREAM, 1)) == -1)
          EDIE("socket");
        snprintf(sock_address->sun_path, sizeof sock_address->sun_path, "%s/%s",
                 socketdir, de->d_name);
        if (connect(sock_fd, (struct sockaddr *)sock_address,
                    sizeof(*sock_address)) == -1) {
          newfd = false;
          continue;
        }
        if (send(sock_fd, sockbuf, len, 0) == -1)
          fprintf(stderr, "Could not send status data to '%s'\n",
                  sock_address->sun_path);
        close(sock_fd);
        newfd = true;
      }
    }
  }
  closedir(dir);
}

void sig_handler(int sig) {
  if (sig == SIGINT || sig == SIGHUP || sig == SIGTERM)
    run_display = false;
}

int main(int argc, char **argv) {
  char *xdgruntimedir;
  struct sockaddr_un sock_address;
  Bar *bar, *bar2;
  Seat *seat, *seat2;

  if (!(xdgruntimedir = getenv("XDG_RUNTIME_DIR")))
    DIE("Could not retrieve XDG_RUNTIME_DIR");
  snprintf(socketdir, sizeof socketdir, "%s/somebar", xdgruntimedir);
  if (mkdir(socketdir, S_IRWXU) == -1)
    if (errno != EEXIST)
      EDIE("Could not create directory '%s'", socketdir);
  sock_address.sun_family = AF_UNIX;

  char *target_socket = NULL;
  int i = 1;
  if (argc > 1 && !strcmp(argv[1], "-target-socket")) {
    if (2 >= argc) DIE("Option -socket requires an argument");
    target_socket = argv[2];
    i += 2;
  }
  for (; i < argc; i++) {
    if (!strcmp(argv[i], "-status")) {
      if (++i + 1 >= argc) DIE("Option -status requires two arguments");
      client_send_command(&sock_address, argv[i], "status", argv[i + 1],
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-status-stdin")) {
      if (++i >= argc) DIE("Option -status-stdin requires an argument");
      char *status = malloc(TEXT_MAX * sizeof(char));
      while (fgets(status, TEXT_MAX - 1, stdin)) {
        status[strlen(status) - 1] = '\0';
        client_send_command(&sock_address, argv[i], "status", status,
                            target_socket);
      }
      free(status);
      return 0;
    } else if (!strcmp(argv[i], "-title")) {
      if (++i + 1 >= argc) DIE("Option -title requires two arguments");
      client_send_command(&sock_address, argv[i], "title", argv[i + 1],
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-show")) {
      if (++i >= argc) DIE("Option -show requires an argument");
      client_send_command(&sock_address, argv[i], "show", NULL, target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-hide")) {
      if (++i >= argc) DIE("Option -hide requires an argument");
      client_send_command(&sock_address, argv[i], "hide", NULL, target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-toggle-visibility")) {
      if (++i >= argc) DIE("Option -toggle requires an argument");
      client_send_command(&sock_address, argv[i], "toggle-visibility", NULL,
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-set-top")) {
      if (++i >= argc) DIE("Option -set-top requires an argument");
      client_send_command(&sock_address, argv[i], "set-top", NULL,
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-set-bottom")) {
      if (++i >= argc) DIE("Option -set-bottom requires an argument");
      client_send_command(&sock_address, argv[i], "set-bottom", NULL,
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-toggle-location")) {
      if (++i >= argc) DIE("Option -toggle-location requires an argument");
      client_send_command(&sock_address, argv[i], "toggle-location", NULL,
                          target_socket);
      return 0;
    } else if (!strcmp(argv[i], "-ipc")) {
      ipc = true;
    } else if (!strcmp(argv[i], "-no-ipc")) {
      ipc = false;
    } else if (!strcmp(argv[i], "-hide-vacant-tags")) {
      hide_vacant = true;
    } else if (!strcmp(argv[i], "-no-hide-vacant-tags")) {
      hide_vacant = false;
    } else if (!strcmp(argv[i], "-bottom")) {
      bottom = true;
    } else if (!strcmp(argv[i], "-no-bottom")) {
      bottom = false;
    } else if (!strcmp(argv[i], "-hidden")) {
      hidden = true;
    } else if (!strcmp(argv[i], "-no-hidden")) {
      hidden = false;
    } else if (!strcmp(argv[i], "-status-commands")) {
      status_commands = true;
    } else if (!strcmp(argv[i], "-no-status-commands")) {
      status_commands = false;
    } else if (!strcmp(argv[i], "-center-title")) {
      center_title = true;
    } else if (!strcmp(argv[i], "-no-center-title")) {
      center_title = false;
    } else if (!strcmp(argv[i], "-custom-title")) {
      custom_title = true;
    } else if (!strcmp(argv[i], "-no-custom-title")) {
      custom_title = false;
    } else if (!strcmp(argv[i], "-active-color-title")) {
      active_color_title = true;
    } else if (!strcmp(argv[i], "-no-active-color-title")) {
      active_color_title = false;
    } else if (!strcmp(argv[i], "-font")) {
      if (++i >= argc) DIE("Option -font requires an argument");
      fontstr = argv[i];
    } else if (!strcmp(argv[i], "-vertical-padding")) {
      if (++i >= argc) DIE("Option -vertical-padding requires an argument");
      vertical_padding = MAX(MIN(atoi(argv[i]), 100), 0);
    } else if (!strcmp(argv[i], "-active-fg-color")) {
      if (++i >= argc) DIE("Option -active-fg-color requires an argument");
      if (parse_color(argv[i], &active_fg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-active-bg-color")) {
      if (++i >= argc) DIE("Option -active-bg-color requires an argument");
      if (parse_color(argv[i], &active_bg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-occupied-fg-color")) {
      if (++i >= argc) DIE("Option -occupied-fg-color requires an argument");
      if (parse_color(argv[i], &occupied_fg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-occupied-bg-color")) {
      if (++i >= argc) DIE("Option -occupied-bg-color requires an argument");
      if (parse_color(argv[i], &occupied_bg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-inactive-fg-color")) {
      if (++i >= argc) DIE("Option -inactive-fg-color requires an argument");
      if (parse_color(argv[i], &inactive_fg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-inactive-bg-color")) {
      if (++i >= argc) DIE("Option -inactive-bg-color requires an argument");
      if (parse_color(argv[i], &inactive_bg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-urgent-fg-color")) {
      if (++i >= argc) DIE("Option -urgent-fg-color requires an argument");
      if (parse_color(argv[i], &urgent_fg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-urgent-bg-color")) {
      if (++i >= argc) DIE("Option -urgent-bg-color requires an argument");
      if (parse_color(argv[i], &urgent_bg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-middle-bg-color-selected")) {
      if (++i >= argc)
        DIE("Option -middle-bg-color-selected requires an argument");
      if (parse_color(argv[i], &middle_bg_color_selected) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-middle-bg-color")) {
      if (++i >= argc) DIE("Option -middle-bg-color requires an argument");
      if (parse_color(argv[i], &middle_bg_color) == -1)
        DIE("malformed color string");
    } else if (!strcmp(argv[i], "-tags")) {
      if (++i >= argc) DIE("Option -tags requires at least one argument");
      int v;
      if ((v = atoi(argv[i])) < 0 || i + v >= argc)
        DIE("-tags: invalid arguments");
      if (tags) {
        for (uint32_t j = 0; j < tags_l; j++) free(tags[j]);
        free(tags);
      }
      if (!(tags = malloc(v * sizeof(char *)))) EDIE("malloc");
      for (int j = 0; j < v; j++)
        if (!(tags[j] = strdup(argv[i + 1 + j]))) EDIE("strdup");
      tags_l = tags_c = v;
      i += v;
    } else if (!strcmp(argv[i], "-scale")) {
      if (++i >= argc) DIE("Option -scale requires an argument");
      char *end;
      buffer_scale = strtoul(argv[i], &end, 10);
      if (*end) DIE("Invalid number for -scale");
    } else if (!strcmp(argv[i], "-v")) {
      fprintf(stderr, PROGRAM " " VERSION "\n");
      return 0;
    } else if (!strcmp(argv[i], "-h")) {
      fprintf(stderr, USAGE);
      return 0;
    } else {
      DIE("Option '%s' not recognized\n" USAGE, argv[i]);
    }
  }

  display = wl_display_connect(NULL);
  if (!display) DIE("Failed to create display");

  wl_list_init(&bar_list);
  wl_list_init(&seat_list);

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!compositor || !shm || !layer_shell || !output_manager)
    DIE("Compositor does not support all needed protocols");

  fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
  unsigned int dpi = 96 * buffer_scale;
  char buf[10];
  snprintf(buf, sizeof buf, "dpi=%u", dpi);
  struct fcft_font_options *options = fcft_font_options_create();
  options->scaling_filter = FCFT_SCALING_FILTER_LANCZOS3;
  if (!(font = fcft_from_name2(1, (const char *[]){fontstr}, buf, options)))
    DIE("Could not load font");
  fcft_font_options_destroy(options);
  textpadding = font->height / 2;
  height = font->height / buffer_scale + vertical_padding * 2;

  if (!tags) {
    if (!(tags = malloc(LENGTH(tags_names) * sizeof(char *))))
      EDIE("malloc");
    tags_l = tags_c = LENGTH(tags_names);
    for (uint32_t i = 0; i < tags_l; i++)
      if (!(tags[i] = strdup(tags_names[i]))) EDIE("strdup");
  }

  wl_list_for_each(bar, &bar_list, link) setup_bar(bar);
  wl_display_roundtrip(display);

  if (ipc) {
    wl_display_roundtrip(display);
    wl_list_for_each(bar, &bar_list, link) {
        ipc_send_watch(bar);
		}
	}

  bool found = false;
  for (uint32_t i = 0; i < 50; i++) {
    if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 1)) == -1)
      DIE("socket");
    snprintf(sock_address.sun_path, sizeof sock_address.sun_path,
             "%s/somebar-%i", socketdir, i);
    if (connect(sock_fd, (struct sockaddr *)&sock_address,
                sizeof sock_address) == -1) {
      found = true;
      break;
    }
    close(sock_fd);
  }
  if (!found) DIE("Could not secure a socket path");
  socketpath = (char *)&sock_address.sun_path;
  unlink(socketpath);
  if (bind(sock_fd, (struct sockaddr *)&sock_address, sizeof sock_address) ==
      -1)
    EDIE("bind");
  if (listen(sock_fd, SOMAXCONN) == -1)
    EDIE("listen");
  fcntl(sock_fd, F_SETFD, FD_CLOEXEC | fcntl(sock_fd, F_GETFD));

  signal(SIGINT, sig_handler);
  signal(SIGHUP, sig_handler);
  signal(SIGTERM, sig_handler);
  signal(SIGCHLD, SIG_IGN);

  run_display = true;
  event_loop();

  close(sock_fd);
  unlink(socketpath);
  if (ipc && ipc_fd >= 0) close(ipc_fd);
  if (!ipc) free(stdinbuf);
  if (tags) {
    for (uint32_t i = 0; i < tags_l; i++) free(tags[i]);
    free(tags);
  }
  if (layouts) {
    for (uint32_t i = 0; i < layouts_l; i++) free(layouts[i]);
    free(layouts);
  }
  wl_list_for_each_safe(bar, bar2, &bar_list, link) teardown_bar(bar);
  wl_list_for_each_safe(seat, seat2, &seat_list, link) teardown_seat(seat);
  zwlr_layer_shell_v1_destroy(layer_shell);
  zxdg_output_manager_v1_destroy(output_manager);
  fcft_destroy(font);
  fcft_fini();
  wl_shm_destroy(shm);
  wl_compositor_destroy(compositor);
  wl_registry_destroy(registry);
  wl_display_disconnect(display);

  return 0;
}
