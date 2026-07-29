/*
 * termios.h - Windows compatibility shim.
 */

#ifndef POKER_EVAL_TERMIOS_H
#define POKER_EVAL_TERMIOS_H

#if defined(_WIN32)
#include <string.h>

#ifndef NCCS
#define NCCS 32
#endif

struct termios
{
    unsigned int c_lflag;
    unsigned char c_cc[NCCS];
};

#ifndef ICANON
#define ICANON 0x0002
#endif
#ifndef ECHO
#define ECHO 0x0008
#endif

#ifndef VMIN
#define VMIN 6
#endif
#ifndef VTIME
#define VTIME 5
#endif

#ifndef TCSANOW
#define TCSANOW 0
#endif

static __inline int tcgetattr(int fd, struct termios *term)
{
    (void)fd;
    if (term)
        memset(term, 0, sizeof(*term));
    return -1;
}

static __inline int tcsetattr(int fd, int actions, const struct termios *term)
{
    (void)fd;
    (void)actions;
    (void)term;
    return -1;
}
#else
#include_next <termios.h>
#endif

#endif /* POKER_EVAL_TERMIOS_H */
