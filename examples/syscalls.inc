#define SYS_EXIT            0x01
#define SYS_YIELD           0x02
#define SYS_GET_TICKS       0x03

#define SYS_PRINT_CHAR      0x10
#define SYS_PRINT_STR       0x11
#define SYS_READ_CHAR       0x12

#define SYS_OPEN            0x20
#define SYS_READ            0x21
#define SYS_WRITE           0x22
#define SYS_CLOSE           0x23
#define SYS_SEEK            0x24

#define O_RDONLY            0
#define O_WRONLY            1
#define O_APPEND            2

#define SEEK_SET            0
#define SEEK_CUR            1
#define SEEK_END            2

#define FD_STDIN            0
#define FD_STDOUT           1
#define FD_STDERR           2

#define SYS_SBRK            0x30

#define SYS_GFX_SET_MODE    0x40
#define SYS_GFX_CLEAR       0x41
#define SYS_GFX_PLOT        0x42
#define SYS_GFX_LINE        0x43
#define SYS_GFX_FILL_RECT   0x44

#define VIDEO_MODE_TEXT     0
#define VIDEO_MODE_GFX      1

#define VGA_GFX_BASE        0x000A0000
#define VGA_TEXT_BASE       0x000B8000

#define COLOR_BLACK         0
#define COLOR_BLUE          1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GRAY    7
#define COLOR_DARK_GRAY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW        14
#define COLOR_WHITE         15

#define PORT_COM1_DATA      0x03F8
#define PORT_BOCHS_DEBUG    0x00E9
