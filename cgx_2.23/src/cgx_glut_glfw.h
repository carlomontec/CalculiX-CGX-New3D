#ifndef CGX_GLUT_GLFW_H
#define CGX_GLUT_GLFW_H

/* --------------------------------------------------------------------  */
/* CalculiX GraphiX (GLFW Edition)                                       */
/* File: cgx_glut_glfw.h - Modern GLFW3 Backend Header for CGX           */
/*                                                                       */
/* Created by Carlo Monjaraz-Tec (2026) with AI pair-programming         */
/* assistance as an academic exercise based on the original work of      */
/* Klaus Wittig and contributors.                                        */
/*                                                                       */
/* Licensed under GNU General Public License v2 (GPL-2.0 or later).      */
/* --------------------------------------------------------------------  */

#ifdef __APPLE__
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl.h>
  #include <OpenGL/glu.h>
#else
  #if defined(_WIN32) || defined(WIN32)
    #include <windows.h>
  #endif
  #include <GL/gl.h>
  #include <GL/glu.h>
#endif

#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLUT Display Mode Bit Flags */
#define GLUT_RGB            0x0000
#define GLUT_RGBA           0x0000
#define GLUT_INDEX          0x0001
#define GLUT_SINGLE         0x0000
#define GLUT_DOUBLE         0x0002
#define GLUT_ACCUM          0x0004
#define GLUT_ALPHA          0x0008
#define GLUT_DEPTH          0x0010
#define GLUT_STENCIL        0x0020
#define GLUT_MULTISAMPLE    0x0080
#define GLUT_STEREO         0x0100
#define GLUT_LUMINANCE      0x0200

/* Mouse Buttons */
#define GLUT_LEFT_BUTTON    0
#define GLUT_MIDDLE_BUTTON  1
#define GLUT_RIGHT_BUTTON   2
#define GLUT_WEEL_UP        3
#define GLUT_WEEL_DOWN      4

/* Mouse States */
#define GLUT_DOWN           0
#define GLUT_UP             1

/* Window Visibility */
#define GLUT_NOT_VISIBLE    0
#define GLUT_VISIBLE        1

/* Entry/Exit States */
#define GLUT_LEFT           0
#define GLUT_ENTERED        1

/* Special Keys */
#define GLUT_KEY_F1         1
#define GLUT_KEY_F2         2
#define GLUT_KEY_F3         3
#define GLUT_KEY_F4         4
#define GLUT_KEY_F5         5
#define GLUT_KEY_F6         6
#define GLUT_KEY_F7         7
#define GLUT_KEY_F8         8
#define GLUT_KEY_F9         9
#define GLUT_KEY_F10        10
#define GLUT_KEY_F11        11
#define GLUT_KEY_F12        12
#define GLUT_KEY_LEFT       100
#define GLUT_KEY_UP         101
#define GLUT_KEY_RIGHT      102
#define GLUT_KEY_DOWN       103
#define GLUT_KEY_PAGE_UP    104
#define GLUT_KEY_PAGE_DOWN  105
#define GLUT_KEY_HOME       106
#define GLUT_KEY_END        107
#define GLUT_KEY_INSERT     108

/* glutGet Parameters */
#define GLUT_WINDOW_X                   100
#define GLUT_WINDOW_Y                   101
#define GLUT_WINDOW_WIDTH               102
#define GLUT_WINDOW_HEIGHT              103
#define GLUT_WINDOW_BUFFER_SIZE         104
#define GLUT_WINDOW_STENCIL_SIZE        105
#define GLUT_WINDOW_DEPTH_SIZE          106
#define GLUT_WINDOW_RED_SIZE            107
#define GLUT_WINDOW_GREEN_SIZE          108
#define GLUT_WINDOW_BLUE_SIZE           109
#define GLUT_WINDOW_ALPHA_SIZE          110
#define GLUT_WINDOW_ACCUM_RED_SIZE      111
#define GLUT_WINDOW_ACCUM_GREEN_SIZE    112
#define GLUT_WINDOW_ACCUM_BLUE_SIZE     113
#define GLUT_WINDOW_ACCUM_ALPHA_SIZE    114
#define GLUT_WINDOW_DOUBLEBUFFER        115
#define GLUT_WINDOW_RGBA                116
#define GLUT_WINDOW_PARENT              117
#define GLUT_WINDOW_NUM_CHILDREN        118
#define GLUT_WINDOW_COLORMAP_SIZE       119
#define GLUT_SCREEN_WIDTH               200
#define GLUT_SCREEN_HEIGHT              201
#define GLUT_SCREEN_WIDTH_MM            202
#define GLUT_SCREEN_HEIGHT_MM           203
#define GLUT_MENU_NUM_ITEMS             300
#define GLUT_ELAPSED_TIME               700
#define GLUT_INIT_WINDOW_WIDTH          800
#define GLUT_INIT_WINDOW_HEIGHT         801

/* Modern Typography Font Handles */
#define GLUT_BITMAP_9_BY_15             ((void*)1)
#define GLUT_BITMAP_8_BY_13             ((void*)2)
#define GLUT_BITMAP_TIMES_ROMAN_10      ((void*)3)
#define GLUT_BITMAP_TIMES_ROMAN_24      ((void*)4)
#define GLUT_BITMAP_HELVETICA_10        ((void*)5)
#define GLUT_BITMAP_HELVETICA_12        ((void*)6)
#define GLUT_BITMAP_HELVETICA_18        ((void*)7)

/* GLUT API Function Prototypes */
void glutInit(int *argcp, char **argv);
void glutInitDisplayMode(unsigned int mode);
void glutInitWindowPosition(int x, int y);
void glutInitWindowSize(int width, int height);
void glutMainLoop(void);

int  glutCreateWindow(const char *title);
int  glutCreateSubWindow(int win, int x, int y, int width, int height);
void glutDestroyWindow(int win);
void glutPostRedisplay(void);
void glutSwapBuffers(void);
int  glutGetWindow(void);
void glutSetWindow(int win);
void glutSetWindowTitle(const char *title);
void glutSetIconTitle(const char *title);
void glutPositionWindow(int x, int y);
void glutReshapeWindow(int width, int height);
void glutPopWindow(void);
void glutPushWindow(void);
void glutIconifyWindow(void);
void glutShowWindow(void);
void glutHideWindow(void);
void glutFullScreen(void);
void glutSetCursor(int cursor);

void glutDisplayFunc(void (*func)(void));
void glutReshapeFunc(void (*func)(int width, int height));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutMouseFunc(void (*func)(int button, int state, int x, int y));
void glutMotionFunc(void (*func)(int x, int y));
void glutPassiveMotionFunc(void (*func)(int x, int y));
void glutEntryFunc(void (*func)(int state));
void glutVisibilityFunc(void (*func)(int state));
void glutIdleFunc(void (*func)(void));
void glutSpecialFunc(void (*func)(int key, int x, int y));

int  glutCreateMenu(void (*func)(int value));
void glutDestroyMenu(int menu);
int  glutGetMenu(void);
void glutSetMenu(int menu);
void glutAddMenuEntry(const char *label, int value);
void glutAddSubMenu(const char *label, int submenu);
void glutChangeToMenuEntry(int item, const char *label, int value);
void glutChangeToSubMenu(int item, const char *label, int submenu);
void glutRemoveMenuItem(int item);
void glutAttachMenu(int button);
void glutDetachMenu(int button);

void glutBitmapCharacter(void *font, int character);
int  glutBitmapWidth(void *font, int character);
int  glutBitmapLength(void *font, const unsigned char *string);

void glutSetColor(int cell, GLfloat red, GLfloat green, GLfloat blue);
GLfloat glutGetColor(int cell, int component);
void glutCopyColormap(int win);

int  glutGet(GLenum type);
int  glutDeviceGet(GLenum type);
int  glutGetModifiers(void);
int  glutLayerGet(GLenum type);

/* Internal helper: returns the underlying GLFWwindow */
GLFWwindow *cgx_glfw_get_window(void);
void cgx_glfw_toggle_command_bar(void);
int  cgx_glfw_is_command_bar_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* CGX_GLUT_GLFW_H */
