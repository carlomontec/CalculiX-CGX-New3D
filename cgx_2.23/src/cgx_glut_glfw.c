/* --------------------------------------------------------------------  */
/* CalculiX GraphiX (GLFW Edition)                                       */
/* File: cgx_glut_glfw.c - Modern GLFW3 & OpenGL Backend for CGX         */
/*                                                                       */
/* Created by Carlo Monjaraz-Tec (2026) with AI pair-programming         */
/* assistance as an academic exercise based on the original work of      */
/* Klaus Wittig and contributors.                                        */
/*                                                                       */
/* Licensed under GNU General Public License v2 (GPL-2.0 or later).      */
/* --------------------------------------------------------------------  */

#include "cgx_glut_glfw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define MAX_WINDOWS 16
#define MAX_MENUS   128
#define MAX_MENU_ITEMS 128
#define MAX_MENU_DEPTH 8
#define CMD_BAR_HEIGHT 38
#define MENU_ITEM_HEIGHT 25
#define MENU_BOX_WIDTH 250

/* External references from CGX */
extern int mainmenu;
extern int activWindow;
extern void cgx_execute_command_string(const char *cmd_str);

/* Window definition */
typedef struct {
  int id;
  int parent_id; /* 0 for root */
  int x, y, width, height; /* coordinates in window units */
  int visible;
  
  /* Callbacks */
  void (*display_func)(void);
  void (*reshape_func)(int width, int height);
  void (*keyboard_func)(unsigned char key, int x, int y);
  void (*special_func)(int key, int x, int y);
  void (*mouse_func)(int button, int state, int x, int y);
  void (*motion_func)(int x, int y);
  void (*passive_motion_func)(int x, int y);
  void (*visibility_func)(int state);
  void (*entry_func)(int state);
  
  /* Attached menu (button -> menu_id) */
  int attached_menu[5];
} CGXWindow;

/* Menu item definition */
typedef struct {
  char label[128];
  int value;
  int submenu_id; /* -1 if regular entry */
  int is_submenu;
} CGXMenuItem;

/* Menu definition */
typedef struct {
  int id;
  void (*callback)(int value);
  CGXMenuItem items[MAX_MENU_ITEMS];
  int num_items;
} CGXMenu;

/* Cascade menu level */
typedef struct {
  int menu_id;
  int x, y;
  int width, height;
  int hovered_item;
} CGXMenuCascade;

/* Global state */
static GLFWwindow *g_glfw_window = NULL;
static int g_init_w = 800, g_init_h = 600;
static unsigned int g_display_mode = GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH;
static void (*g_idle_func)(void) = NULL;

static CGXWindow g_windows[MAX_WINDOWS];
static int g_num_windows = 0;
static int g_current_window_id = 1;
static volatile int g_need_redisplay = 1;

static CGXMenu g_menus[MAX_MENUS];
static int g_num_menus = 0;
static int g_current_menu_id = 0;

/* Multi-level recursive cascade popup menu */
static CGXMenuCascade g_cascade[MAX_MENU_DEPTH];
static int g_cascade_depth = 0;

/* Interactive Command Bar state */
static int  g_cmd_bar_visible = 1;
static char g_cmd_buf[256] = "";
static int  g_cmd_len = 0;
static int  g_cmd_focused = 1;
static int  g_send_hovered = 0;
static char g_last_cmd_echo[256] = "Ready";
static void init_truetype_fonts(void);

/* Command History */
#define MAX_CMD_HIST 64
static char g_cmd_history[MAX_CMD_HIST][256];
static int  g_cmd_hist_count = 0;
static int  g_cmd_hist_pos = 0;

/* Mouse state */
static double g_last_mouse_x = 0.0;
static double g_last_mouse_y = 0.0;
static int g_mouse_buttons[5] = { GLUT_UP, GLUT_UP, GLUT_UP, GLUT_UP, GLUT_UP };

/* Helper: find window by id */
static CGXWindow *get_window(int id)
{
  for (int i = 0; i < g_num_windows; i++)
  {
    if (g_windows[i].id == id) return &g_windows[i];
  }
  return NULL;
}

/* Helper: find menu by id */
static CGXMenu *get_menu(int id)
{
  for (int i = 0; i < g_num_menus; i++)
  {
    if (g_menus[i].id == id) return &g_menus[i];
  }
  return NULL;
}

/* Helper: compute absolute screen rectangle for hierarchical windows */
static void get_window_screen_rect(CGXWindow *win, int *out_x, int *out_y, int *out_w, int *out_h)
{
  int x = win->x;
  int y = win->y;
  int parent_id = win->parent_id;
  while (parent_id > 0)
  {
    CGXWindow *parent = get_window(parent_id);
    if (parent)
    {
      x += parent->x;
      y += parent->y;
      parent_id = parent->parent_id;
    }
    else break;
  }
  *out_x = x;
  *out_y = y;
  *out_w = win->width;
  *out_h = win->height;
}

/* Helper: find window at screen position (x, y) */
static CGXWindow *find_window_at(int x, int y)
{
  for (int i = g_num_windows - 1; i >= 0; i--)
  {
    if (g_windows[i].parent_id != 0 && g_windows[i].visible)
    {
      int sx, sy, sw, sh;
      get_window_screen_rect(&g_windows[i], &sx, &sy, &sw, &sh);
      if (x >= sx && x < sx + sw && y >= sy && y < sy + sh)
      {
        return &g_windows[i];
      }
    }
  }
  for (int i = 0; i < g_num_windows; i++)
  {
    if (g_windows[i].parent_id == 0 && g_windows[i].visible) return &g_windows[i];
  }
  return (g_num_windows > 0) ? &g_windows[0] : NULL;
}

GLFWwindow *cgx_glfw_get_window(void)
{
  return g_glfw_window;
}

void cgx_glfw_toggle_command_bar(void)
{
  g_cmd_bar_visible = !g_cmd_bar_visible;
  if (g_cmd_bar_visible) printf("\n Command Line Bar shown.\n\n");
  else printf("\n Command Line Bar hidden.\n\n");
  g_need_redisplay = 1;
}

int cgx_glfw_is_command_bar_visible(void)
{
  return g_cmd_bar_visible;
}

/* --------------------------------------------------------------------  */
/* Stdin Background Listener Thread for Interactive CLI Commands         */
/* --------------------------------------------------------------------  */
static void *stdin_listener_thread(void *arg)
{
  (void)arg;
  char line[512];

  while (fgets(line, sizeof(line), stdin))
  {
    cgx_execute_command_string(line);

    strncpy(g_last_cmd_echo, line, sizeof(g_last_cmd_echo) - 1);
    int elen = (int)strlen(g_last_cmd_echo);
    while (elen > 0 && (g_last_cmd_echo[elen-1] == '\n' || g_last_cmd_echo[elen-1] == '\r'))
    {
      g_last_cmd_echo[--elen] = '\0';
    }

    g_need_redisplay = 1;
    if (g_glfw_window) glfwPostEmptyEvent();
  }
  return NULL;
}

/* GLUT Initialization */
void glutInit(int *argcp, char **argv)
{
  (void)argcp; (void)argv;
  if (!glfwInit())
  {
    fprintf(stderr, "ERROR: Failed to initialize GLFW\n");
    exit(EXIT_FAILURE);
  }
}

void glutInitDisplayMode(unsigned int mode)
{
  g_display_mode = mode;
}

void glutInitWindowPosition(int x, int y) { (void)x; (void)y; }

void glutInitWindowSize(int width, int height)
{
  g_init_w = (width > 0) ? width : 800;
  g_init_h = (height > 0) ? height : 600;
}

/* Window Creation & Management */
int glutCreateWindow(const char *title)
{
  int id;
  CGXWindow *win;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
  glfwWindowHint(GLFW_DEPTH_BITS, 24);

  g_glfw_window = glfwCreateWindow(g_init_w, g_init_h, title, NULL, NULL);
  if (!g_glfw_window)
  {
    /* Fallback for remote X11 / XQuartz / software rasterizers: relax framebuffer hints */
    glfwDefaultWindowHints();
    g_glfw_window = glfwCreateWindow(g_init_w, g_init_h, title, NULL, NULL);
  }

  if (!g_glfw_window)
  {
    fprintf(stderr, "\n===============================================================\n");
    fprintf(stderr, "ERROR: Failed to create GLFW window.\n");
    fprintf(stderr, "If using SSH with XQuartz, try running on your Mac:\n");
    fprintf(stderr, "  defaults write org.xquartz.X11 enable_iglx 1\n");
    fprintf(stderr, "and on Linux before running:\n");
    fprintf(stderr, "  export LIBGL_ALWAYS_INDIRECT=1\n");
    fprintf(stderr, "===============================================================\n\n");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  glfwMakeContextCurrent(g_glfw_window);
  glfwSwapInterval(1);
  init_truetype_fonts();

  id = 1;
  win = &g_windows[g_num_windows++];
  memset(win, 0, sizeof(CGXWindow));
  win->id = id;
  win->parent_id = 0;
  win->x = 0;
  win->y = 0;
  win->width = g_init_w;
  win->height = g_init_h;
  win->visible = 1;
  for (int b = 0; b < 5; b++) win->attached_menu[b] = 0;

  g_current_window_id = id;

  pthread_t tid;
  pthread_create(&tid, NULL, stdin_listener_thread, NULL);
  pthread_detach(tid);

  return id;
}

int glutCreateSubWindow(int parent, int x, int y, int width, int height)
{
  int id = g_num_windows + 1;
  CGXWindow *win = &g_windows[g_num_windows++];
  memset(win, 0, sizeof(CGXWindow));
  win->id = id;
  win->parent_id = parent;
  win->x = x;
  win->y = y;
  win->width = width;
  win->height = height;
  win->visible = 1;
  for (int b = 0; b < 5; b++) win->attached_menu[b] = 0;

  g_current_window_id = id;
  return id;
}

void glutDestroyWindow(int win) { (void)win; }
int glutGetWindow(void) { return g_current_window_id; }

void glutSetWindow(int win)
{
  CGXWindow *w = get_window(win);
  if (w) g_current_window_id = win;
}

void glutPostRedisplay(void)
{
  g_need_redisplay = 1;
}

void glutSwapBuffers(void)
{
  g_need_redisplay = 1;
}

void glutPositionWindow(int x, int y)
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win && win->parent_id != 0)
  {
    win->x = x;
    win->y = y;
    g_need_redisplay = 1;
  }
  else if (g_glfw_window)
  {
    glfwSetWindowPos(g_glfw_window, x, y);
  }
}

void glutReshapeWindow(int width, int height)
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win && win->parent_id != 0)
  {
    win->width = width;
    win->height = height;
    if (win->reshape_func) win->reshape_func(width, height);
    g_need_redisplay = 1;
  }
  else if (g_glfw_window)
  {
    glfwSetWindowSize(g_glfw_window, width, height);
  }
}

void glutSetWindowTitle(const char *title)
{
  if (g_glfw_window) glfwSetWindowTitle(g_glfw_window, title);
}

void glutSetIconTitle(const char *title) { (void)title; }
void glutPopWindow(void) {}
void glutPushWindow(void) {}
void glutIconifyWindow(void) {}
void glutShowWindow(void) {}
void glutHideWindow(void) {}
void glutFullScreen(void) {}
void glutSetCursor(int cursor) { (void)cursor; }

/* Callback Registrations */
void glutDisplayFunc(void (*func)(void))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->display_func = func;
}

void glutReshapeFunc(void (*func)(int width, int height))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->reshape_func = func;
}

void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->keyboard_func = func;
}

void glutSpecialFunc(void (*func)(int key, int x, int y))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->special_func = func;
}

void glutMouseFunc(void (*func)(int button, int state, int x, int y))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->mouse_func = func;
}

void glutMotionFunc(void (*func)(int x, int y))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->motion_func = func;
}

void glutPassiveMotionFunc(void (*func)(int x, int y))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->passive_motion_func = func;
}

void glutEntryFunc(void (*func)(int state))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->entry_func = func;
}

void glutVisibilityFunc(void (*func)(int state))
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win) win->visibility_func = func;
}

void glutIdleFunc(void (*func)(void))
{
  g_idle_func = func;
}

/* Menu Management */
int glutCreateMenu(void (*func)(int value))
{
  int id = g_num_menus + 1;
  CGXMenu *menu = &g_menus[g_num_menus++];
  memset(menu, 0, sizeof(CGXMenu));
  menu->id = id;
  menu->callback = func;
  menu->num_items = 0;
  g_current_menu_id = id;
  return id;
}

void glutDestroyMenu(int menu_id) { (void)menu_id; }
int glutGetMenu(void) { return g_current_menu_id; }
void glutSetMenu(int menu) { g_current_menu_id = menu; }

void glutAddMenuEntry(const char *label, int value)
{
  CGXMenu *menu = get_menu(g_current_menu_id);
  if (menu && menu->num_items < MAX_MENU_ITEMS)
  {
    CGXMenuItem *item = &menu->items[menu->num_items++];
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->value = value;
    item->submenu_id = -1;
    item->is_submenu = 0;
  }
}

void glutAddSubMenu(const char *label, int submenu)
{
  CGXMenu *menu = get_menu(g_current_menu_id);
  if (menu && menu->num_items < MAX_MENU_ITEMS)
  {
    CGXMenuItem *item = &menu->items[menu->num_items++];
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->value = 0;
    item->submenu_id = submenu;
    item->is_submenu = 1;
  }
}

void glutChangeToMenuEntry(int item_idx, const char *label, int value)
{
  CGXMenu *menu = get_menu(g_current_menu_id);
  if (menu && item_idx >= 1 && item_idx <= menu->num_items)
  {
    CGXMenuItem *item = &menu->items[item_idx - 1];
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->value = value;
    item->submenu_id = -1;
    item->is_submenu = 0;
  }
}

void glutChangeToSubMenu(int item_idx, const char *label, int submenu)
{
  CGXMenu *menu = get_menu(g_current_menu_id);
  if (menu && item_idx >= 1 && item_idx <= menu->num_items)
  {
    CGXMenuItem *item = &menu->items[item_idx - 1];
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->value = 0;
    item->submenu_id = submenu;
    item->is_submenu = 1;
  }
}

void glutRemoveMenuItem(int item_idx)
{
  CGXMenu *menu = get_menu(g_current_menu_id);
  if (menu && item_idx >= 1 && item_idx <= menu->num_items)
  {
    for (int i = item_idx - 1; i < menu->num_items - 1; i++)
    {
      menu->items[i] = menu->items[i + 1];
    }
    menu->num_items--;
  }
}

void glutAttachMenu(int button)
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win && button >= 0 && button < 5)
  {
    win->attached_menu[button] = g_current_menu_id;
  }
}

void glutDetachMenu(int button)
{
  CGXWindow *win = get_window(g_current_window_id);
  if (win && button >= 0 && button < 5)
  {
    win->attached_menu[button] = 0;
  }
}

void glutSetColor(int cell, GLfloat red, GLfloat green, GLfloat blue)
{
  (void)cell; (void)red; (void)green; (void)blue;
}

GLfloat glutGetColor(int cell, int component) { (void)cell; (void)component; return 0.0f; }
void glutCopyColormap(int win) { (void)win; }

int glutGet(GLenum type)
{
  CGXWindow *win = get_window(g_current_window_id);
  switch (type)
  {
    case GLUT_WINDOW_X: return win ? win->x : 0;
    case GLUT_WINDOW_Y: return win ? win->y : 0;
    case GLUT_WINDOW_WIDTH: return win ? win->width : g_init_w;
    case GLUT_WINDOW_HEIGHT: return win ? win->height : g_init_h;
    case GLUT_WINDOW_RGBA: return 1;
    case GLUT_WINDOW_DOUBLEBUFFER: return 1;
    case GLUT_WINDOW_DEPTH_SIZE: return 24;
    case GLUT_SCREEN_WIDTH: return 1920;
    case GLUT_SCREEN_HEIGHT: return 1080;
    case GLUT_INIT_WINDOW_WIDTH: return g_init_w;
    case GLUT_INIT_WINDOW_HEIGHT: return g_init_h;
    case GLUT_ELAPSED_TIME: return (int)(glfwGetTime() * 1000.0);
    default: return 0;
  }
}

int glutDeviceGet(GLenum type) { (void)type; return 0; }
int glutGetModifiers(void) { return 0; }
int glutLayerGet(GLenum type) { (void)type; return 0; }

/* --------------------------------------------------------------------  */
/* Execute Command from Command Bar                                      */
/* --------------------------------------------------------------------  */
static void submit_command_bar(void)
{
  if (g_cmd_len > 0)
  {
    if (g_cmd_hist_count < MAX_CMD_HIST)
    {
      strncpy(g_cmd_history[g_cmd_hist_count++], g_cmd_buf, 255);
    }
    else
    {
      for (int i = 0; i < MAX_CMD_HIST - 1; i++)
      {
        strcpy(g_cmd_history[i], g_cmd_history[i + 1]);
      }
      strncpy(g_cmd_history[MAX_CMD_HIST - 1], g_cmd_buf, 255);
    }
    g_cmd_hist_pos = g_cmd_hist_count;

    strncpy(g_last_cmd_echo, g_cmd_buf, sizeof(g_last_cmd_echo) - 1);

    cgx_execute_command_string(g_cmd_buf);

    g_cmd_buf[0] = '\0';
    g_cmd_len = 0;
    g_need_redisplay = 1;
  }
}

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct {
  GLuint tex_id;
  stbtt_bakedchar cdata[96];
  float font_size;
} CGXFontAtlas;

static CGXFontAtlas g_font_atlas[3]; /* 0: 14pt (Small), 1: 20pt (Medium / Default), 2: 32pt (Big) */
static int g_font_atlas_ready = 0;

static int get_ui_font_tier(void)
{
  extern int draw_font;
  if (draw_font <= 4) return 0; /* Small (14pt) */
  if (draw_font == 5) return 1; /* Medium / Default (20pt) */
  return 2; /* Big (32pt) */
}

static int get_menu_item_height(void)
{
  int t = get_ui_font_tier();
  if (t == 0) return 24;
  if (t == 1) return 30;
  return 44;
}

static int get_menu_box_width(void)
{
  int t = get_ui_font_tier();
  if (t == 0) return 240;
  if (t == 1) return 280;
  return 400;
}

static int get_cmd_bar_height(void)
{
  int t = get_ui_font_tier();
  if (t == 0) return 34;
  if (t == 1) return 42;
  return 58;
}

static void init_truetype_fonts(void)
{
  if (g_font_atlas_ready) return;

  const char *paths[] = {
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/SFNS.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
    "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    NULL
  };

  FILE *f = NULL;
  for (int i = 0; paths[i] != NULL; i++)
  {
    f = fopen(paths[i], "rb");
    if (f) break;
  }
  if (!f) return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); return; }

  unsigned char *ttf_buffer = (unsigned char *)malloc(size);
  if (!ttf_buffer) { fclose(f); return; }

  if (fread(ttf_buffer, 1, size, f) != (size_t)size)
  {
    free(ttf_buffer);
    fclose(f);
    return;
  }
  fclose(f);

  int offset = stbtt_GetFontOffsetForIndex(ttf_buffer, 0);
  if (offset < 0) offset = 0;

  int win_w = 800, win_h = 600, fb_w = 800, fb_h = 600;
  if (g_glfw_window)
  {
    glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
    glfwGetFramebufferSize(g_glfw_window, &fb_w, &fb_h);
  }
  float fb_scale = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;
  if (fb_scale < 1.0f) fb_scale = 1.0f;

  float sizes[3] = { 14.0f * fb_scale, 20.0f * fb_scale, 32.0f * fb_scale };
  int tex_dim = 1024;

  for (int tier = 0; tier < 3; tier++)
  {
    unsigned char *temp_bitmap = (unsigned char *)calloc(tex_dim * tex_dim, 1);
    if (!temp_bitmap) continue;

    int res = stbtt_BakeFontBitmap(ttf_buffer, offset, sizes[tier], temp_bitmap, tex_dim, tex_dim, 32, 96, g_font_atlas[tier].cdata);
    if (res > 0)
    {
      glGenTextures(1, &g_font_atlas[tier].tex_id);
      glBindTexture(GL_TEXTURE_2D, g_font_atlas[tier].tex_id);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, tex_dim, tex_dim, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      g_font_atlas[tier].font_size = sizes[tier];
    }
    free(temp_bitmap);
  }

  free(ttf_buffer);
  g_font_atlas_ready = 1;
}

/* --------------------------------------------------------------------  */
/* Modern Anti-Aliased Vector Typography Helpers                         */
/* --------------------------------------------------------------------  */

void glutBitmapCharacter(void *font, int character)
{
  (void)font;
  if (character < 32 || character >= 128) return;

  GLboolean valid = GL_FALSE;
  glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, &valid);
  if (!valid) return;

  GLfloat rpos[4];
  glGetFloatv(GL_CURRENT_RASTER_POSITION, rpos);

  int tier = get_ui_font_tier();
  if (!g_font_atlas_ready || g_font_atlas[tier].tex_id == 0) return;

  int tex_dim = 1024;

  int win_w = 800, win_h = 600, fb_w = 800, fb_h = 600;
  if (g_glfw_window)
  {
    glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
    glfwGetFramebufferSize(g_glfw_window, &fb_w, &fb_h);
  }
  if (fb_w <= 0 || fb_h <= 0) return;

  glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_VIEWPORT_BIT | GL_POLYGON_BIT);
  glDisable(GL_LIGHTING);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_font_atlas[tier].tex_id);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport(0, 0, fb_w, fb_h);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0, fb_w, 0, fb_h);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  float cur_x = rpos[0];
  float cur_y = (float)fb_h - rpos[1];

  stbtt_aligned_quad q;
  stbtt_GetBakedQuad(g_font_atlas[tier].cdata, tex_dim, tex_dim, character - 32, &cur_x, &cur_y, &q, 1);

  glBegin(GL_QUADS);
    glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, (float)fb_h - q.y0);
    glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, (float)fb_h - q.y0);
    glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, (float)fb_h - q.y1);
    glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, (float)fb_h - q.y1);
  glEnd();

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  glPopAttrib();

  /* Advance raster position in framebuffer pixels for next character */
  float adv = g_font_atlas[tier].cdata[character - 32].xadvance;
  glBitmap(0, 0, 0.0f, 0.0f, adv, 0.0f, NULL);
}

int glutBitmapWidth(void *font, int character)
{
  (void)font;
  if (character < 32 || character >= 128) return 8;
  int tier = get_ui_font_tier();
  if (!g_font_atlas_ready) return 10;
  return (int)ceil(g_font_atlas[tier].cdata[character - 32].xadvance);
}

int glutBitmapLength(void *font, const unsigned char *string)
{
  (void)font;
  if (!string || !*string) return 0;
  int tier = get_ui_font_tier();
  if (!g_font_atlas_ready) return (int)strlen((const char*)string) * 10;
  float width = 0.0f;
  while (*string)
  {
    int ch = (unsigned char)*string;
    if (ch >= 32 && ch < 128)
      width += g_font_atlas[tier].cdata[ch - 32].xadvance;
    else
      width += g_font_atlas[tier].font_size * 0.5f;
    string++;
  }
  return (int)ceil(width);
}

static void draw_ui_text_dynamic(float x, float y, const char *str, float r, float g, float b, int win_h)
{
  if (!str || !*str) return;
  int tier = get_ui_font_tier();
  if (!g_font_atlas_ready || g_font_atlas[tier].tex_id == 0) return;

  int win_w = 800, fb_w = 800, fb_h = 600;
  if (g_glfw_window)
  {
    glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
    glfwGetFramebufferSize(g_glfw_window, &fb_w, &fb_h);
  }
  float scale_x = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;
  float scale_y = (win_h > 0) ? (float)fb_h / (float)win_h : 1.0f;

  int tex_dim = 1024;

  glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT | GL_POLYGON_BIT);
  glViewport(0, 0, fb_w, fb_h);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_font_atlas[tier].tex_id);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, 1.0f);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0, fb_w, 0, fb_h);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  float cur_x = x * scale_x;
  float cur_y = y * scale_y;

  glBegin(GL_QUADS);
  while (*str)
  {
    int ch = (unsigned char)*str;
    if (ch >= 32 && ch < 128)
    {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_font_atlas[tier].cdata, tex_dim, tex_dim, ch - 32, &cur_x, &cur_y, &q, 1);

      glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, (float)fb_h - q.y0);
      glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, (float)fb_h - q.y0);
      glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, (float)fb_h - q.y1);
      glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, (float)fb_h - q.y1);
    }
    else
    {
      cur_x += g_font_atlas[tier].font_size * 0.5f;
    }
    str++;
  }
  glEnd();

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  glPopAttrib();
}

static float get_text_width_dynamic(const char *str)
{
  if (!str || !*str) return 0.0f;
  int tier = get_ui_font_tier();
  if (!g_font_atlas_ready || g_font_atlas[tier].tex_id == 0) return 0.0f;

  int win_w = 800, fb_w = 800;
  if (g_glfw_window)
  {
    int win_h;
    glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
    glfwGetFramebufferSize(g_glfw_window, &fb_w, &win_h);
  }
  float scale_x = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;

  float width = 0.0f;
  while (*str)
  {
    int ch = (unsigned char)*str;
    if (ch >= 32 && ch < 128)
      width += g_font_atlas[tier].cdata[ch - 32].xadvance;
    else
      width += g_font_atlas[tier].font_size * 0.5f;
    str++;
  }
  return width / scale_x;
}

/* --------------------------------------------------------------------  */
/* Multi-Level Recursive Popup Menu Engine                               */
/* --------------------------------------------------------------------  */

static void open_cascade_root(int menu_id, int x, int y, int win_w, int win_h)
{
  CGXMenu *menu = get_menu(menu_id);
  if (!menu) return;

  int item_h = get_menu_item_height();
  int menu_w = get_menu_box_width();
  int bar_h = g_cmd_bar_visible ? get_cmd_bar_height() : 0;
  int total_h = menu->num_items * item_h;
  if (x + menu_w > win_w) x = win_w - menu_w - 6;
  if (y + total_h > win_h - bar_h) y = win_h - bar_h - total_h - 6;
  if (x < 6) x = 6;
  if (y < 6) y = 6;

  g_cascade[0].menu_id = menu_id;
  g_cascade[0].x = x;
  g_cascade[0].y = y;
  g_cascade[0].width = menu_w;
  g_cascade[0].height = total_h;
  g_cascade[0].hovered_item = -1;
  g_cascade_depth = 1;
  g_need_redisplay = 1;
}

static void update_cascade_hover(int mouse_x, int mouse_y, int win_w, int win_h)
{
  if (g_cascade_depth <= 0) return;

  int item_h = get_menu_item_height();
  int menu_w = get_menu_box_width();
  int bar_h = g_cmd_bar_visible ? get_cmd_bar_height() : 0;

  int match_lvl = -1;
  int match_item = -1;

  for (int lvl = g_cascade_depth - 1; lvl >= 0; lvl--)
  {
    CGXMenuCascade *c = &g_cascade[lvl];
    if (mouse_x >= c->x && mouse_x <= c->x + c->width &&
        mouse_y >= c->y && mouse_y <= c->y + c->height)
    {
      match_lvl = lvl;
      match_item = (mouse_y - c->y) / item_h;
      break;
    }
  }

  if (match_lvl >= 0)
  {
    CGXMenuCascade *c = &g_cascade[match_lvl];
    CGXMenu *menu = get_menu(c->menu_id);
    if (menu && match_item >= 0 && match_item < menu->num_items)
    {
      c->hovered_item = match_item;
      g_cascade_depth = match_lvl + 1;

      if (menu->items[match_item].is_submenu && match_lvl + 1 < MAX_MENU_DEPTH)
      {
        int sub_id = menu->items[match_item].submenu_id;
        CGXMenu *sub_menu = get_menu(sub_id);
        if (sub_menu && sub_menu->num_items > 0)
        {
          int next_lvl = match_lvl + 1;
          int sub_w = menu_w;
          int sub_h = sub_menu->num_items * item_h;

          int sub_x = c->x + c->width;
          if (sub_x + sub_w > win_w - 6)
          {
            sub_x = c->x - sub_w;
            if (sub_x < 6) sub_x = 6;
          }

          int sub_y = c->y + match_item * item_h;
          if (sub_y + sub_h > win_h - bar_h)
          {
            sub_y = win_h - bar_h - sub_h - 6;
          }
          if (sub_y < 6) sub_y = 6;

          g_cascade[next_lvl].menu_id = sub_id;
          g_cascade[next_lvl].x = sub_x;
          g_cascade[next_lvl].y = sub_y;
          g_cascade[next_lvl].width = sub_w;
          g_cascade[next_lvl].height = sub_h;
          g_cascade[next_lvl].hovered_item = -1;
          g_cascade_depth = next_lvl + 1;
        }
      }
      g_need_redisplay = 1;
    }
  }
}

static int handle_cascade_click(int mouse_x, int mouse_y)
{
  if (g_cascade_depth <= 0) return 0;
  int item_h = get_menu_item_height();

  for (int lvl = g_cascade_depth - 1; lvl >= 0; lvl--)
  {
    CGXMenuCascade *c = &g_cascade[lvl];
    if (mouse_x >= c->x && mouse_x <= c->x + c->width &&
        mouse_y >= c->y && mouse_y <= c->y + c->height)
    {
      int item_idx = (mouse_y - c->y) / item_h;
      CGXMenu *menu = get_menu(c->menu_id);
      if (menu && item_idx >= 0 && item_idx < menu->num_items)
      {
        if (!menu->items[item_idx].is_submenu && menu->callback)
        {
          int val = menu->items[item_idx].value;
          void (*cb)(int) = menu->callback;

          g_cascade_depth = 0;

          glutSetWindow(2);
          activWindow = 2;

          cb(val);

          g_need_redisplay = 1;
          return 1;
        }
      }
    }
  }

  g_cascade_depth = 0;
  g_need_redisplay = 1;
  return 1;
}

static void draw_cascade_menu(CGXMenuCascade *c, int win_h)
{
  CGXMenu *menu = get_menu(c->menu_id);
  if (!menu) return;

  int item_h = get_menu_item_height();
  int start_x = c->x;
  int start_y = c->y;
  int menu_w = c->width;
  int total_h = c->height;

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  /* Drop Shadow */
  glColor4f(0.0f, 0.0f, 0.0f, 0.50f);
  glRectf(start_x + 6, win_h - (start_y + total_h + 6), start_x + menu_w + 6, win_h - (start_y + 6));

  /* Dark Teal / Slate Glass Background (#0E151E) */
  glColor4f(0.05f, 0.07f, 0.10f, 0.98f);
  glRectf(start_x, win_h - (start_y + total_h), start_x + menu_w, win_h - start_y);

  /* Sleek Border (#223A52) */
  glColor4f(0.18f, 0.28f, 0.38f, 1.0f);
  glLineWidth(1.2f);
  glBegin(GL_LINE_LOOP);
    glVertex2f(start_x, win_h - start_y);
    glVertex2f(start_x + menu_w, win_h - start_y);
    glVertex2f(start_x + menu_w, win_h - (start_y + total_h));
    glVertex2f(start_x, win_h - (start_y + total_h));
  glEnd();

  int text_baseline_offset = (int)(item_h * 0.70f);

  for (int i = 0; i < menu->num_items; i++)
  {
    int item_y = start_y + i * item_h;
    
    if (i == c->hovered_item)
    {
      /* Electric Blue / Teal Hover Gradient */
      glColor4f(0.12f, 0.38f, 0.70f, 1.0f);
      glRectf(start_x + 2, win_h - (item_y + item_h), start_x + menu_w - 2, win_h - item_y);

      /* Left Accent Pill */
      glColor4f(0.25f, 0.75f, 1.0f, 1.0f);
      glRectf(start_x + 2, win_h - (item_y + item_h), start_x + 5, win_h - item_y);

      draw_ui_text_dynamic(start_x + 12, item_y + text_baseline_offset, menu->items[i].label, 1.0f, 1.0f, 1.0f, win_h);
      if (menu->items[i].is_submenu)
      {
        draw_ui_text_dynamic(start_x + menu_w - 22, item_y + text_baseline_offset, ">", 0.40f, 0.85f, 1.0f, win_h);
      }
    }
    else
    {
      draw_ui_text_dynamic(start_x + 12, item_y + text_baseline_offset, menu->items[i].label, 0.90f, 0.94f, 0.98f, win_h);
      if (menu->items[i].is_submenu)
      {
        draw_ui_text_dynamic(start_x + menu_w - 22, item_y + text_baseline_offset, ">", 0.45f, 0.58f, 0.72f, win_h);
      }
    }
  }
}

/* --------------------------------------------------------------------  */
/* Modern In-Window Command Bar (Bottom Strip)                           */
/* --------------------------------------------------------------------  */
static void render_command_bar(int win_w, int win_h)
{
  int bar_h = get_cmd_bar_height();
  int bar_y = win_h - bar_h;
  int baseline_y = bar_y + (int)(bar_h * 0.68f);

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  /* Bar Background - Deep Charcoal Dark Slate (#05070A) */
  glColor4f(0.05f, 0.07f, 0.10f, 0.98f);
  glRectf(0, 0, win_w, bar_h);

  /* Top Border (#1E2D3E) */
  glColor4f(0.18f, 0.28f, 0.38f, 1.0f);
  glLineWidth(1.4f);
  glBegin(GL_LINES);
    glVertex2f(0, bar_h);
    glVertex2f(win_w, bar_h);
  glEnd();

  /* Prompt Symbol (Bright Cyan >) */
  draw_ui_text_dynamic(12, baseline_y, ">", 0.25f, 0.80f, 1.0f, win_h);

  /* Input Text or Placeholder */
  if (g_cmd_len > 0)
  {
    draw_ui_text_dynamic(30, baseline_y, g_cmd_buf, 1.0f, 1.0f, 1.0f, win_h);
  }
  else
  {
    draw_ui_text_dynamic(30, baseline_y, "Type command (e.g. ds 4 e 4, plot fv all, anim real, view persp)...", 0.38f, 0.46f, 0.56f, win_h);
  }

  /* Send Button */
  int btn_w = (int)(get_text_width_dynamic("SEND") + 28);
  if (btn_w < 80) btn_w = 80;
  int btn_x1 = win_w - btn_w - 10;
  int btn_x2 = win_w - 10;
  int btn_y1 = bar_y + 4;
  int btn_y2 = bar_y + bar_h - 4;

  if (g_send_hovered)
  {
    glColor4f(0.18f, 0.52f, 0.95f, 1.0f);
  }
  else
  {
    glColor4f(0.12f, 0.35f, 0.68f, 1.0f);
  }
  glRectf(btn_x1, win_h - btn_y2, btn_x2, win_h - btn_y1);

  /* Button Border */
  if (g_send_hovered)
    glColor4f(0.40f, 0.75f, 1.0f, 1.0f);
  else
    glColor4f(0.30f, 0.65f, 1.0f, 1.0f);
  glLineWidth(1.4f);
  glBegin(GL_LINE_LOOP);
    glVertex2f(btn_x1, win_h - btn_y1);
    glVertex2f(btn_x2, win_h - btn_y1);
    glVertex2f(btn_x2, win_h - btn_y2);
    glVertex2f(btn_x1, win_h - btn_y2);
  glEnd();

  float send_tw = get_text_width_dynamic("SEND");
  draw_ui_text_dynamic(btn_x1 + (btn_w - send_tw) / 2.0f, baseline_y, "SEND", 1.0f, 1.0f, 1.0f, win_h);
}

/* --------------------------------------------------------------------  */
/* GLFW Callback Handlers & Event Translation                            */
/* --------------------------------------------------------------------  */

static void glfw_key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
  (void)window; (void)scancode; (void)mods;
  if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

  if (g_cascade_depth > 0 && key == GLFW_KEY_ESCAPE)
  {
    g_cascade_depth = 0;
    g_need_redisplay = 1;
    return;
  }

  if (g_cmd_bar_visible && g_cmd_focused && g_cascade_depth <= 0)
  {
    if (key == GLFW_KEY_ENTER)
    {
      submit_command_bar();
      return;
    }
    else if (key == GLFW_KEY_BACKSPACE)
    {
      if (g_cmd_len > 0)
      {
        g_cmd_buf[--g_cmd_len] = '\0';
        g_need_redisplay = 1;
      }
      return;
    }
    else if (key == GLFW_KEY_ESCAPE)
    {
      g_cmd_buf[0] = '\0';
      g_cmd_len = 0;
      g_need_redisplay = 1;
      return;
    }
    else if (key == GLFW_KEY_UP)
    {
      if (g_cmd_hist_count > 0 && g_cmd_hist_pos > 0)
      {
        g_cmd_hist_pos--;
        strncpy(g_cmd_buf, g_cmd_history[g_cmd_hist_pos], sizeof(g_cmd_buf) - 1);
        g_cmd_len = (int)strlen(g_cmd_buf);
        g_need_redisplay = 1;
      }
      return;
    }
    else if (key == GLFW_KEY_DOWN)
    {
      if (g_cmd_hist_pos < g_cmd_hist_count - 1)
      {
        g_cmd_hist_pos++;
        strncpy(g_cmd_buf, g_cmd_history[g_cmd_hist_pos], sizeof(g_cmd_buf) - 1);
        g_cmd_len = (int)strlen(g_cmd_buf);
      }
      else
      {
        g_cmd_hist_pos = g_cmd_hist_count;
        g_cmd_buf[0] = '\0';
        g_cmd_len = 0;
      }
      g_need_redisplay = 1;
      return;
    }
  }

  CGXWindow *win = get_window(g_current_window_id);
  if (!win) win = get_window(1);
  if (!win) return;

  int special = 0;
  switch (key)
  {
    case GLFW_KEY_F1: special = GLUT_KEY_F1; break;
    case GLFW_KEY_F2: special = GLUT_KEY_F2; break;
    case GLFW_KEY_F3: special = GLUT_KEY_F3; break;
    case GLFW_KEY_F4: special = GLUT_KEY_F4; break;
    case GLFW_KEY_F5: special = GLUT_KEY_F5; break;
    case GLFW_KEY_F6: special = GLUT_KEY_F6; break;
    case GLFW_KEY_F7: special = GLUT_KEY_F7; break;
    case GLFW_KEY_F8: special = GLUT_KEY_F8; break;
    case GLFW_KEY_F9: special = GLUT_KEY_F9; break;
    case GLFW_KEY_F10: special = GLUT_KEY_F10; break;
    case GLFW_KEY_F11: special = GLUT_KEY_F11; break;
    case GLFW_KEY_F12: special = GLUT_KEY_F12; break;
    case GLFW_KEY_LEFT: special = GLUT_KEY_LEFT; break;
    case GLFW_KEY_UP: special = GLUT_KEY_UP; break;
    case GLFW_KEY_RIGHT: special = GLUT_KEY_RIGHT; break;
    case GLFW_KEY_DOWN: special = GLUT_KEY_DOWN; break;
    case GLFW_KEY_PAGE_UP: special = GLUT_KEY_PAGE_UP; break;
    case GLFW_KEY_PAGE_DOWN: special = GLUT_KEY_PAGE_DOWN; break;
    case GLFW_KEY_HOME: special = GLUT_KEY_HOME; break;
    case GLFW_KEY_END: special = GLUT_KEY_END; break;
    case GLFW_KEY_INSERT: special = GLUT_KEY_INSERT; break;
    default: break;
  }

  int sx, sy, sw, sh;
  get_window_screen_rect(win, &sx, &sy, &sw, &sh);
  int mx = (int)g_last_mouse_x - sx;
  int my = (int)g_last_mouse_y - sy;

  if (special && win->special_func)
  {
    win->special_func(special, mx, my);
    g_need_redisplay = 1;
  }
}

static void glfw_char_callback(GLFWwindow *window, unsigned int codepoint)
{
  (void)window;
  if (codepoint >= 32 && codepoint < 127)
  {
    if (g_cmd_bar_visible && g_cmd_focused && g_cascade_depth <= 0)
    {
      if (g_cmd_len < (int)sizeof(g_cmd_buf) - 2)
      {
        g_cmd_buf[g_cmd_len++] = (char)codepoint;
        g_cmd_buf[g_cmd_len] = '\0';
        g_need_redisplay = 1;
      }
      return;
    }

    CGXWindow *win = get_window(g_current_window_id);
    if (!win) win = get_window(1);
    if (win && win->keyboard_func)
    {
      int sx, sy, sw, sh;
      get_window_screen_rect(win, &sx, &sy, &sw, &sh);
      int mx = (int)g_last_mouse_x - sx;
      int my = (int)g_last_mouse_y - sy;
      win->keyboard_func((unsigned char)codepoint, mx, my);
      g_need_redisplay = 1;
    }
  }
}

static void glfw_cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
  (void)window;
  g_last_mouse_x = xpos;
  g_last_mouse_y = ypos;

  int win_w, win_h;
  glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
  int bar_h = get_cmd_bar_height();

  /* Send button hover check */
  if (g_cmd_bar_visible && ypos >= win_h - bar_h && xpos >= win_w - 96 && xpos <= win_w - 10)
  {
    g_send_hovered = 1;
    g_need_redisplay = 1;
  }
  else if (g_send_hovered)
  {
    g_send_hovered = 0;
    g_need_redisplay = 1;
  }

  /* Multi-level menu hover update */
  if (g_cascade_depth > 0)
  {
    update_cascade_hover((int)xpos, (int)ypos, win_w, win_h);
    return;
  }

  if (g_cmd_bar_visible && ypos >= win_h - bar_h) return;

  CGXWindow *win = find_window_at((int)xpos, (int)ypos);
  if (!win) return;

  int sx, sy, sw, sh;
  get_window_screen_rect(win, &sx, &sy, &sw, &sh);
  int local_x = (int)xpos - sx;
  int local_y = (int)ypos - sy;

  int prev_win_id = g_current_window_id;
  g_current_window_id = win->id;

  int any_btn_down = (g_mouse_buttons[0] == GLUT_DOWN ||
                      g_mouse_buttons[1] == GLUT_DOWN ||
                      g_mouse_buttons[2] == GLUT_DOWN);

  if (any_btn_down && win->motion_func)
  {
    win->motion_func(local_x, local_y);
    g_need_redisplay = 1;
  }
  else if (!any_btn_down && win->passive_motion_func)
  {
    win->passive_motion_func(local_x, local_y);
    g_need_redisplay = 1;
  }

  g_current_window_id = prev_win_id;
}

static void glfw_mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
  (void)window; (void)mods;
  int glut_btn = GLUT_LEFT_BUTTON;
  if (button == GLFW_MOUSE_BUTTON_RIGHT) glut_btn = GLUT_RIGHT_BUTTON;
  else if (button == GLFW_MOUSE_BUTTON_MIDDLE) glut_btn = GLUT_MIDDLE_BUTTON;

  int glut_state = (action == GLFW_PRESS) ? GLUT_DOWN : GLUT_UP;
  g_mouse_buttons[glut_btn] = glut_state;

  int win_w, win_h;
  glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
  int bar_h = get_cmd_bar_height();

  /* Handle Cascade Popup Menu Click */
  if (g_cascade_depth > 0 && action == GLFW_PRESS)
  {
    if (handle_cascade_click((int)g_last_mouse_x, (int)g_last_mouse_y))
    {
      return;
    }
  }

  /* Handle Command Bar Click */
  if (g_cmd_bar_visible && action == GLFW_PRESS && g_last_mouse_y >= win_h - bar_h)
  {
    if (g_last_mouse_x >= win_w - 96 && g_last_mouse_x <= win_w - 10)
    {
      submit_command_bar();
      return;
    }
    g_cmd_focused = 1;
    g_cascade_depth = 0;
    g_need_redisplay = 1;
    return;
  }

  CGXWindow *win = find_window_at((int)g_last_mouse_x, (int)g_last_mouse_y);
  if (!win) return;

  /* Right-Click anywhere triggers Multi-Level Cascade Menu */
  if (glut_btn == GLUT_RIGHT_BUTTON && glut_state == GLUT_DOWN)
  {
    int menu_to_open = (mainmenu > 0) ? mainmenu : win->attached_menu[glut_btn];
    if (menu_to_open <= 0 && g_num_menus > 0) menu_to_open = g_menus[g_num_menus - 1].id;

    if (menu_to_open > 0)
    {
      open_cascade_root(menu_to_open, (int)g_last_mouse_x, (int)g_last_mouse_y, win_w, win_h);
      return;
    }
  }

  int sx, sy, sw, sh;
  get_window_screen_rect(win, &sx, &sy, &sw, &sh);
  int local_x = (int)g_last_mouse_x - sx;
  int local_y = (int)g_last_mouse_y - sy;

  int prev_win_id = g_current_window_id;
  g_current_window_id = win->id;

  if (win->mouse_func)
  {
    win->mouse_func(glut_btn, glut_state, local_x, local_y);
    g_need_redisplay = 1;
  }

  g_current_window_id = prev_win_id;
}

static void glfw_scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
  (void)window; (void)xoffset;
  CGXWindow *win = find_window_at((int)g_last_mouse_x, (int)g_last_mouse_y);
  if (!win) win = get_window(2);
  if (!win || !win->mouse_func) return;

  int btn = (yoffset > 0) ? GLUT_WEEL_UP : GLUT_WEEL_DOWN;
  int sx, sy, sw, sh;
  get_window_screen_rect(win, &sx, &sy, &sw, &sh);
  int local_x = (int)g_last_mouse_x - sx;
  int local_y = (int)g_last_mouse_y - sy;

  int prev_win_id = g_current_window_id;
  g_current_window_id = win->id;

  win->mouse_func(btn, GLUT_DOWN, local_x, local_y);
  win->mouse_func(btn, GLUT_UP, local_x, local_y);
  g_need_redisplay = 1;

  g_current_window_id = prev_win_id;
}

static void glfw_framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
  (void)window;
  int win_w, win_h;
  glfwGetWindowSize(g_glfw_window, &win_w, &win_h);

  CGXWindow *root = get_window(1);
  if (root)
  {
    root->width = win_w;
    root->height = win_h;
    if (root->reshape_func) root->reshape_func(win_w, win_h);
  }
  g_need_redisplay = 1;
}

/* --------------------------------------------------------------------  */
/* Main Render Loop                                                      */
/* --------------------------------------------------------------------  */

void glutMainLoop(void)
{
  if (!g_glfw_window) return;

  glfwSetKeyCallback(g_glfw_window, glfw_key_callback);
  glfwSetCharCallback(g_glfw_window, glfw_char_callback);
  glfwSetCursorPosCallback(g_glfw_window, glfw_cursor_pos_callback);
  glfwSetMouseButtonCallback(g_glfw_window, glfw_mouse_button_callback);
  glfwSetScrollCallback(g_glfw_window, glfw_scroll_callback);
  glfwSetFramebufferSizeCallback(g_glfw_window, glfw_framebuffer_size_callback);

  int win_w, win_h, fb_w, fb_h;

  while (!glfwWindowShouldClose(g_glfw_window))
  {
    glfwPollEvents();

    if (g_idle_func)
    {
      g_idle_func();
      g_need_redisplay = 1;
    }

    if (g_need_redisplay)
    {
      g_need_redisplay = 0;

      glfwGetWindowSize(g_glfw_window, &win_w, &win_h);
      glfwGetFramebufferSize(g_glfw_window, &fb_w, &fb_h);
      double scale_x = (win_w > 0) ? (double)fb_w / (double)win_w : 1.0;
      double scale_y = (win_h > 0) ? (double)fb_h / (double)win_h : 1.0;

      /* Render root window (w0) */
      CGXWindow *root = get_window(1);
      if (root && root->display_func)
      {
        g_current_window_id = 1;
        glViewport(0, 0, fb_w, fb_h);
        glDisable(GL_SCISSOR_TEST);
        root->display_func();
      }

      /* Render subwindows (w1 3D model, w2 color legend/axes triad) */
      for (int i = 0; i < g_num_windows; i++)
      {
        CGXWindow *win = &g_windows[i];
        if (win->id == 1 || !win->visible || !win->display_func) continue;

        g_current_window_id = win->id;

        int sx, sy, sw, sh;
        get_window_screen_rect(win, &sx, &sy, &sw, &sh);

        int vp_x = (int)(sx * scale_x);
        int vp_y = (int)((win_h - (sy + sh)) * scale_y);
        int vp_w = (int)(sw * scale_x);
        int vp_h = (int)(sh * scale_y);

        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glViewport(vp_x, vp_y, vp_w, vp_h);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vp_x, vp_y, vp_w, vp_h);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        win->display_func();

        glDisable(GL_SCISSOR_TEST);
        glPopAttrib();
      }

      /* 2D UI Overlay Pass: Command Bar & Multi-Level Cascade Popup Menu */
      glViewport(0, 0, fb_w, fb_h);
      glDisable(GL_SCISSOR_TEST);

      glPushAttrib(GL_ALL_ATTRIB_BITS);
      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      gluOrtho2D(0, win_w, 0, win_h);

      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();

      glDisable(GL_DEPTH_TEST);
      glDisable(GL_LIGHTING);
      glDisable(GL_CULL_FACE);
      glDisable(GL_TEXTURE_2D);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      /* Render bottom Command Bar if visible */
      if (g_cmd_bar_visible)
      {
        render_command_bar(win_w, win_h);
      }

      /* Render all active Cascade Menu Levels */
      for (int lvl = 0; lvl < g_cascade_depth; lvl++)
      {
        draw_cascade_menu(&g_cascade[lvl], win_h);
      }

      glPopMatrix();
      glMatrixMode(GL_PROJECTION);
      glPopMatrix();
      glMatrixMode(GL_MODELVIEW);
      glPopAttrib();

      glfwSwapBuffers(g_glfw_window);
      /* Poll events once after swapping to drain the queue */
      glfwPollEvents();
    }
    else
    {
      /* Wait for events instead of spinning CPU/GPU, with a 50ms timeout to maintain responsiveness */
      glfwWaitEventsTimeout(0.05);
    }
  }

  glfwDestroyWindow(g_glfw_window);
  glfwTerminate();
}
