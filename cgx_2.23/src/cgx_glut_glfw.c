/* --------------------------------------------------------------------  */
/* cgx_glut_glfw.c - Modern GLFW & OpenGL Backend for CalculiX CGX      */
/* Cross-platform implementation replacing legacy X11 / GLUT 3.5         */
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
    fprintf(stderr, "ERROR: Failed to create GLFW window\n");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  glfwMakeContextCurrent(g_glfw_window);
  glfwSwapInterval(1);

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

/* --------------------------------------------------------------------  */
/* Modern Typography Helper (Helvetica 18)                               */
/* --------------------------------------------------------------------  */
static void draw_ui_text_large(float x, float y, const char *str, float r, float g, float b, int win_h)
{
  if (!str) return;
  glColor4f(r, g, b, 1.0f);
  glRasterPos2f(x, (float)win_h - y);
  while (*str)
  {
    glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *str);
    str++;
  }
}

/* --------------------------------------------------------------------  */
/* Multi-Level Recursive Popup Menu Engine                               */
/* --------------------------------------------------------------------  */

static void open_cascade_root(int menu_id, int x, int y, int win_w, int win_h)
{
  CGXMenu *menu = get_menu(menu_id);
  if (!menu) return;

  int bar_h = g_cmd_bar_visible ? CMD_BAR_HEIGHT : 0;
  int total_h = menu->num_items * MENU_ITEM_HEIGHT;
  if (x + MENU_BOX_WIDTH > win_w) x = win_w - MENU_BOX_WIDTH - 6;
  if (y + total_h > win_h - bar_h) y = win_h - bar_h - total_h - 6;
  if (x < 6) x = 6;
  if (y < 6) y = 6;

  g_cascade[0].menu_id = menu_id;
  g_cascade[0].x = x;
  g_cascade[0].y = y;
  g_cascade[0].width = MENU_BOX_WIDTH;
  g_cascade[0].height = total_h;
  g_cascade[0].hovered_item = -1;
  g_cascade_depth = 1;
  g_need_redisplay = 1;
}

static void update_cascade_hover(int mouse_x, int mouse_y, int win_w, int win_h)
{
  if (g_cascade_depth <= 0) return;

  int match_lvl = -1;
  int match_item = -1;

  for (int lvl = g_cascade_depth - 1; lvl >= 0; lvl--)
  {
    CGXMenuCascade *c = &g_cascade[lvl];
    if (mouse_x >= c->x && mouse_x <= c->x + c->width &&
        mouse_y >= c->y && mouse_y <= c->y + c->height)
    {
      match_lvl = lvl;
      match_item = (mouse_y - c->y) / MENU_ITEM_HEIGHT;
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
          int sub_w = MENU_BOX_WIDTH;
          int sub_h = sub_menu->num_items * MENU_ITEM_HEIGHT;
          int bar_h = g_cmd_bar_visible ? CMD_BAR_HEIGHT : 0;

          int sub_x = c->x + c->width;
          if (sub_x + sub_w > win_w - 6)
          {
            sub_x = c->x - sub_w;
            if (sub_x < 6) sub_x = 6;
          }

          int sub_y = c->y + match_item * MENU_ITEM_HEIGHT;
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

  for (int lvl = g_cascade_depth - 1; lvl >= 0; lvl--)
  {
    CGXMenuCascade *c = &g_cascade[lvl];
    if (mouse_x >= c->x && mouse_x <= c->x + c->width &&
        mouse_y >= c->y && mouse_y <= c->y + c->height)
    {
      int item_idx = (mouse_y - c->y) / MENU_ITEM_HEIGHT;
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

  int start_x = c->x;
  int start_y = c->y;
  int menu_w = c->width;
  int total_h = c->height;

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

  for (int i = 0; i < menu->num_items; i++)
  {
    int item_y = start_y + i * MENU_ITEM_HEIGHT;
    
    if (i == c->hovered_item)
    {
      /* Electric Blue / Teal Hover Gradient */
      glColor4f(0.12f, 0.38f, 0.70f, 1.0f);
      glRectf(start_x + 2, win_h - (item_y + MENU_ITEM_HEIGHT), start_x + menu_w - 2, win_h - item_y);

      /* Left Accent Pill */
      glColor4f(0.25f, 0.75f, 1.0f, 1.0f);
      glRectf(start_x + 2, win_h - (item_y + MENU_ITEM_HEIGHT), start_x + 5, win_h - item_y);

      draw_ui_text_large(start_x + 12, item_y + 18, menu->items[i].label, 1.0f, 1.0f, 1.0f, win_h);
      if (menu->items[i].is_submenu)
      {
        draw_ui_text_large(start_x + menu_w - 18, item_y + 18, ">", 0.40f, 0.85f, 1.0f, win_h);
      }
    }
    else
    {
      draw_ui_text_large(start_x + 12, item_y + 18, menu->items[i].label, 0.90f, 0.94f, 0.98f, win_h);
      if (menu->items[i].is_submenu)
      {
        draw_ui_text_large(start_x + menu_w - 18, item_y + 18, ">", 0.45f, 0.58f, 0.72f, win_h);
      }
    }
  }
}

/* --------------------------------------------------------------------  */
/* Modern In-Window Command Bar (Bottom Strip)                           */
/* --------------------------------------------------------------------  */
static void render_command_bar(int win_w, int win_h)
{
  int bar_y = win_h - CMD_BAR_HEIGHT;

  /* Bar Background - Deep Charcoal Dark Slate (#05070A) */
  glColor4f(0.05f, 0.07f, 0.10f, 0.98f);
  glRectf(0, 0, win_w, CMD_BAR_HEIGHT);

  /* Top Border (#1E2D3E) */
  glColor4f(0.18f, 0.28f, 0.38f, 1.0f);
  glLineWidth(1.4f);
  glBegin(GL_LINES);
    glVertex2f(0, CMD_BAR_HEIGHT);
    glVertex2f(win_w, CMD_BAR_HEIGHT);
  glEnd();

  /* Prompt Symbol (Bright Cyan >) */
  draw_ui_text_large(12, bar_y + 24, ">", 0.25f, 0.80f, 1.0f, win_h);

  /* Input Text or Placeholder */
  if (g_cmd_len > 0)
  {
    draw_ui_text_large(28, bar_y + 24, g_cmd_buf, 1.0f, 1.0f, 1.0f, win_h);

    /* Blinking Cursor at the precise proportional text end */
    if ((clock() / (CLOCKS_PER_SEC / 3)) % 2 == 0)
    {
      int text_w = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)g_cmd_buf);
      int cur_x = 28 + text_w + 1;
      glColor4f(0.25f, 0.80f, 1.0f, 0.95f);
      glRectf(cur_x, win_h - (bar_y + 27), cur_x + 2, win_h - (bar_y + 9));
    }
  }
  else
  {
    draw_ui_text_large(28, bar_y + 24, "Type command (e.g. ds 4 e 4, plot fv all, anim real, view persp)...", 0.38f, 0.46f, 0.56f, win_h);

    /* Blinking Cursor when empty */
    if ((clock() / (CLOCKS_PER_SEC / 3)) % 2 == 0)
    {
      int cur_x = 28;
      glColor4f(0.25f, 0.80f, 1.0f, 0.95f);
      glRectf(cur_x, win_h - (bar_y + 27), cur_x + 2, win_h - (bar_y + 9));
    }
  }

  /* Send Button */
  int btn_x1 = win_w - 88;
  int btn_x2 = win_w - 10;
  int btn_y1 = bar_y + 5;
  int btn_y2 = bar_y + 33;
  int btn_w = btn_x2 - btn_x1;

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

  int btn_text_w = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)"SEND");
  draw_ui_text_large(btn_x1 + (btn_w - btn_text_w) / 2, bar_y + 24, "SEND", 1.0f, 1.0f, 1.0f, win_h);
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

  /* Send button hover check */
  if (g_cmd_bar_visible && ypos >= win_h - CMD_BAR_HEIGHT && xpos >= win_w - 96 && xpos <= win_w - 12)
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

  if (g_cmd_bar_visible && ypos >= win_h - CMD_BAR_HEIGHT) return;

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

  /* Handle Cascade Popup Menu Click */
  if (g_cascade_depth > 0 && action == GLFW_PRESS)
  {
    if (handle_cascade_click((int)g_last_mouse_x, (int)g_last_mouse_y))
    {
      return;
    }
  }

  /* Handle Command Bar Click */
  if (g_cmd_bar_visible && action == GLFW_PRESS && g_last_mouse_y >= win_h - CMD_BAR_HEIGHT)
  {
    if (g_last_mouse_x >= win_w - 96 && g_last_mouse_x <= win_w - 12)
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

        glViewport(vp_x, vp_y, vp_w, vp_h);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vp_x, vp_y, vp_w, vp_h);

        win->display_func();

        glDisable(GL_SCISSOR_TEST);
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
    }
    else
    {
      usleep(8000);
    }
  }

  glfwDestroyWindow(g_glfw_window);
  glfwTerminate();
}
