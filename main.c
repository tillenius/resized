//
// heavily based on wmctrl 1.07-6. (http://wmctrl.sourcearchive.com/documentation/1.07-6/main_8c-source.html)
//
// This program is free software which I release under the GNU General Public
// License. You may redistribute and/or modify this program under the terms
// of that license as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// To get a copy of the GNU General Puplic License,  write to the
// Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */
#define MAX_PROPERTY_VALUE_LEN 4096
#define PIPENAME "/tmp/resized"

int wildcmp(const char *wild, const char *string) {
    // Written by Jack Handy - jakkhandy@hotmail.com

    const char *cp = NULL, *mp = NULL;

    while ((*string) && (*wild != '*')) {
        if ((*wild != *string) && (*wild != '?'))
            return 0;
        wild++;
        string++;
    }

    while (*string) {
        if (*wild == '*') {
            if (!*++wild)
                return 1;
            mp = wild;
            cp = string+1;
        } else if ((*wild == *string) || (*wild == '?')) {
            wild++;
            string++;
        } else {
            wild = mp;
            string = cp++;
        }
    }

    while (*wild == '*')
        wild++;

    return !*wild;
}

static char *get_property (Display *disp, Window win,
        Atom xa_prop_type, char *prop_name, unsigned long *size) {
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    char *ret;

    xa_prop_name = XInternAtom(disp, prop_name, False);

    /* MAX_PROPERTY_VALUE_LEN / 4 explanation (XGetWindowProperty manpage):
     *
     * long_length = Specifies the length in 32-bit multiples of the
     *               data to be retrieved.
     *
     * NOTE:  see
     * http://mail.gnome.org/archives/wm-spec-list/2003-March/msg00067.html
     * In particular:
     *
     *      When the X window system was ported to 64-bit architectures, a
     * rather peculiar design decision was made. 32-bit quantities such
     * as Window IDs, atoms, etc, were kept as longs in the client side
     * APIs, even when long was changed to 64 bits.
     *
     */
    if (XGetWindowProperty(disp, win, xa_prop_name, 0, MAX_PROPERTY_VALUE_LEN / 4, False,
            xa_prop_type, &xa_ret_type, &ret_format,
            &ret_nitems, &ret_bytes_after, &ret_prop) != Success) {
//        p_verbose("Cannot get %s property.\n", prop_name);
        return NULL;
    }

    if (xa_ret_type != xa_prop_type) {
        XFree(ret_prop);
        return NULL;
    }

    /* null terminate the result to make string handling easier */
    tmp_size = (ret_format / 8) * ret_nitems;
    /* Correct 64 Architecture implementation of 32 bit data */
    if(ret_format==32) tmp_size *= sizeof(long)/4;
    ret = malloc(tmp_size + 1);
    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if (size) {
        *size = tmp_size;
    }

    XFree(ret_prop);
    return ret;
}

static int wm_supports (Display *disp, const char *prop) {
    Atom xa_prop = XInternAtom(disp, prop, False);
    Atom *list;
    unsigned long size;
    int i;

    if (! (list = (Atom *)get_property(disp, DefaultRootWindow(disp),
            XA_ATOM, "_NET_SUPPORTED", &size)))
        return 0;

    for (i = 0; i < size / sizeof(Atom); i++) {
        if (list[i] == xa_prop) {
            free(list);
            return 1;
        }
    }

    free(list);
    return 0;
}

static int client_msg(Display *disp, Window win, char *msg,
        unsigned long data0, unsigned long data1,
        unsigned long data2, unsigned long data3,
        unsigned long data4) {
    XEvent event;
    long mask = SubstructureRedirectMask | SubstructureNotifyMask;

    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = XInternAtom(disp, msg, False);
    event.xclient.window = win;
    event.xclient.format = 32;
    event.xclient.data.l[0] = data0;
    event.xclient.data.l[1] = data1;
    event.xclient.data.l[2] = data2;
    event.xclient.data.l[3] = data3;
    event.xclient.data.l[4] = data4;

    if (XSendEvent(disp, DefaultRootWindow(disp), False, mask, &event))
        return EXIT_SUCCESS;

    //fprintf(stderr, "Cannot send %s event.\n", msg);
    return EXIT_FAILURE;
}

static int window_move_resize (Display *disp, Window win,
        signed long grav,
        signed long x, signed long y,
        signed long w, signed long h) {
    unsigned long grflags;

    grflags = grav;
    if (x != -1) grflags |= (1 << 8);
    if (y != -1) grflags |= (1 << 9);
    if (w != -1) grflags |= (1 << 10);
    if (h != -1) grflags |= (1 << 11);

    if (wm_supports(disp, "_NET_MOVERESIZE_WINDOW")){
        return client_msg(disp, win, "_NET_MOVERESIZE_WINDOW",
            grflags, (unsigned long)x, (unsigned long)y, (unsigned long)w, (unsigned long)h);
    }

    if ((w < 1 || h < 1) && (x >= 0 && y >= 0))
        XMoveWindow(disp, win, x, y);
    else if ((x < 0 || y < 0) && (w >= 1 && h >= -1))
        XResizeWindow(disp, win, w, h);
    else if (x >= 0 && y >= 0 && w >= 1 && h >= 1)
        XMoveResizeWindow(disp, win, x, y, w, h);

    return EXIT_SUCCESS;
}

static int window_maximize(Display *disp, Window win, unsigned long int mode) {
    // mode = _NET_WM_STATE_TOGGLE
    Atom prop1 = 0;
    Atom prop2 = 0;
    prop1 = XInternAtom(disp, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    prop2 = XInternAtom(disp, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    return client_msg(disp, win, "_NET_WM_STATE", mode,
                      (unsigned long) prop1, (unsigned long) prop2, 0, 0);
}

static int window_fullscreen(Display *disp, Window win, unsigned long int mode) {
    // mode = _NET_WM_STATE_TOGGLE
    Atom prop1 = 0;
    prop1 = XInternAtom(disp, "_NET_WM_STATE_FULLSCREEN", False);
    return client_msg(disp, win, "_NET_WM_STATE", mode,
                      (unsigned long) prop1, 0, 0, 0);
}

static int window_activate(Display *disp, Window win) {
    unsigned long *desktop;

    /* desktop ID */
    desktop = (unsigned long *)get_property(disp, win,
            XA_CARDINAL, "_NET_WM_DESKTOP", NULL);
    if (desktop == NULL) {
        desktop = (unsigned long *)get_property(disp, win,
                XA_CARDINAL, "_WIN_WORKSPACE", NULL);
    }

    if (desktop) {
        if (client_msg(disp, DefaultRootWindow(disp),
                    "_NET_CURRENT_DESKTOP",
                    *desktop, 0, 0, 0, 0) != EXIT_SUCCESS) {
//            p_verbose("Cannot switch desktop.\n");
        }
        free(desktop);
    }

    client_msg(disp, win, "_NET_ACTIVE_WINDOW",
            0, 0, 0, 0, 0);
    XMapRaised(disp, win);

    return EXIT_SUCCESS;
}

static Window get_active_window(Display *disp) {
    char *prop;
    unsigned long size;
    Window ret = (Window)0;

    prop = get_property(disp, DefaultRootWindow(disp), XA_WINDOW,
                        "_NET_ACTIVE_WINDOW", &size);
    if (prop) {
        ret = *((Window*)prop);
        free(prop);
    }

    return(ret);
}

static char *get_window_title(Display *disp, Window win) {
    return get_property(disp, win, XA_STRING, "WM_NAME", NULL);
}

//static char *get_window_class(Display *disp, Window win) {
//    char *wm_class;
//    unsigned long size;
//
//    wm_class = get_property(disp, win, XA_STRING, "WM_CLASS", &size);
//    if (wm_class) {
//        char *p_0 = strchr(wm_class, '\0');
//        if (wm_class + size - 1 > p_0) {
//            *(p_0) = '.';
//        }
//        return wm_class;
//    }
//    return 0;
//}


static Window *get_client_list (Display *disp, unsigned long *size) {
    Window *client_list;

    if ((client_list = (Window *)get_property(disp, DefaultRootWindow(disp),
                    XA_WINDOW, "_NET_CLIENT_LIST", size)) == NULL) {
        if ((client_list = (Window *)get_property(disp, DefaultRootWindow(disp),
                        XA_CARDINAL, "_WIN_CLIENT_LIST", size)) == NULL) {
            fputs("Cannot get client list properties. \n"
                  "(_NET_CLIENT_LIST or _WIN_CLIENT_LIST)"
                  "\n", stderr);
            return NULL;
        }
    }

    return client_list;
}

static Window get_window_by_title(Display *disp, const char *title) {
    Window *client_list;
    unsigned long client_list_size;
    int i;

    client_list = get_client_list(disp, &client_list_size);
    if (client_list == NULL)
        return 0;

    for (i = 0; i < client_list_size / sizeof(Window); i++) {
        char *name = get_window_title(disp, client_list[i]);
        //fprintf(stderr, "title: \"%s\" vs \"%s\"", name, title);
        if (!name)
            continue;
        if (wildcmp(title, name)) {
            Window win = client_list[i];
            free(name);
            free(client_list);
            return win;
        }
        free(name);
    }
    free(client_list);
    return 0;
}

// -----------------------------------------------------------------------------------

int state = 0;
Window activeWin = 0;

void command(Display *disp, const char *cmd) {

    // left screen:
    const int lw = 1280; // width
    const int lh = 1024; // height
    const int lt = 24;   // padding at top

    // right screen:
    const int rw = 1280; // width
    const int rh = 1024; // height
    const int rt = 0;    // padding at top

    // need two characters
    if (cmd[0] == '\0')
        return;

    Window prevWin = activeWin;
    activeWin = get_active_window(disp);

    int dx, dy, w, h;
    if (cmd[0] == 'l') {
        dx = 0; dy = lt; w = lw; h = lh-lt;
    }
    else if (cmd[0] == 'r') {
        dx = lw; dy = rt; w = rw; h = rh-rt;
    }
    else if (cmd[0] == '+') {
        window_maximize(disp, activeWin, _NET_WM_STATE_TOGGLE);
        XFlush(disp);
        state = 0;
        return;
    }
    else if (cmd[0] == 'a') {
        Window win = get_window_by_title(disp, &cmd[1]);
        if (win) {
            window_activate(disp, win);
            XFlush(disp);
        }
        state = 0;
        return;
    }
    else
        return;

    if (cmd[1] == '0') {
        state = 0;
        return;
    }
    if (state == 0 || activeWin != prevWin) {
        state = cmd[1]-'0';
        return;
    }


    int ystart;
    switch (state) {
    case 7: case 8: case 9: ystart = dy; break;
    case 4: case 5: case 6: ystart = dy+h/3; break;
    case 1: case 2: case 3: ystart = dy+2*h/3; break;
    default:
        return;
    }
    int xstart;
    switch (state) {
    case 7: case 4: case 1: xstart = dx; break;
    case 8: case 5: case 2: xstart = dx+lw/3; break;
    case 9: case 6: case 3: xstart = dx+2*lw/3; break;
    default:
        return;
    }

    state = 0;
    int endstate = cmd[1]-'0';

    int endy;
    switch (endstate) {
    case 7: case 8: case 9: endy = dy+h/3; break;
    case 4: case 5: case 6: endy = dy+2*h/3; break;
    case 1: case 2: case 3: endy = dy+h; break;
    default:
        return;
    }
    int endx;
    switch (endstate) {
    case 7: case 4: case 1: endx = dx+w/3; break;
    case 8: case 5: case 2: endx = dx+2*w/3; break;
    case 9: case 6: case 3: endx = dx+w; break;
    default:
        return;
    }

    if (endx <= xstart || endy <= ystart) {
        state = endstate;
        return;
    }

    char *name = get_window_title(disp, activeWin);
    if (strcmp("Terminal", name) == 0) {
        endy -= 16;
    }

    window_fullscreen(disp, activeWin, _NET_WM_STATE_REMOVE);
    window_maximize(disp, activeWin, _NET_WM_STATE_REMOVE);
    window_move_resize(disp, activeWin, 1, xstart, ystart, endx-xstart, endy-ystart);
    XFlush(disp);
}

int main(void) {

    int i;
    pid_t pid, sid;
    FILE *fp;

    // Fork off the parent process
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork() failed");
        exit(EXIT_FAILURE);
    }

    // If we got a good PID, then we can exit the parent process.
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // Change the file mode mask
    umask(0);

    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0) {
        fprintf(stderr, "setsid() failed");
        exit(EXIT_FAILURE);
    }

    // Create named pipe
    if (mknod(PIPENAME, S_IFIFO|0666, 0) != 0) {
        remove(PIPENAME);
        if (mknod(PIPENAME, S_IFIFO|0666, 0) != 0) {
            fprintf(stderr, "mknod failed");
            exit(EXIT_FAILURE);
        }
    }

    // Close out the standard file descriptors
    for (i = getdtablesize(); i >= 0; --i)
        close(i);

    Display *disp;
    if (! (disp = XOpenDisplay(NULL))) {
        //fprintf(stderr, "Cannot open display.\n");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        char readbuf[80];
        //fprintf(stderr, "ready to open\n"); fflush(stderr);
        fp = fopen(PIPENAME, "r");
        //fprintf(stderr, "fgets\n"); fflush(stderr);

        if (fgets(readbuf, 80, fp) == 0) {
            //fprintf(stderr, "Error?\n");
            readbuf[0] = '\0';
//            break;
        }
        else {
            const size_t len = strlen(readbuf);
            if (len > 1 && readbuf[len-1] == 0x0a)
                readbuf[len-1] = '\0';
        }
        //fprintf(stderr, "Received string: \"%s\"\n", readbuf);

        //fprintf(stderr, "close\n"); fflush(stderr);
        fclose(fp);

        if (strncmp(readbuf, "QUIT", 4) == 0)
            break;

        command(disp, readbuf);

        //fprintf(stderr, "processed string: %s\n", readbuf); fflush(stderr);
    }

    //fprintf(stderr, "Goodbye\n");
    XCloseDisplay(disp);
    remove(PIPENAME);
    exit(EXIT_SUCCESS);
}
