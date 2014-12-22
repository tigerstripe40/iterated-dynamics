/*
 * JIIM.C
 *
 * Generates Inverse Julia in real time, lets move a cursor which determines
 * the J-set.
 *
 *  The J-set is generated in a fixed-size window, a third of the screen.
 */
#include <algorithm>

#include <string.h>

#include <stdarg.h>

// see Fractint.c for a description of the "include"  hierarchy
#include "port.h"
#include "prototyp.h"
#include "helpdefs.h"
#include "fractype.h"
#include "drivers.h"

#define MAXRECT         1024      // largest width of SaveRect/RestoreRect

#define newx(size)     mem_alloc(size)
#define delete(block)  block = nullptr

int show_numbers =0;              // toggle for display of coords
U16 memory_handle = 0;
FILE *file;
int windows = 0;               // windows management system

int xc, yc;                       // corners of the window
int xd, yd;                       // dots in the window
double xcjul = BIG;
double ycjul = BIG;

// circle routines from Dr. Dobbs June 1990
int xbase, ybase;
unsigned int xAspect, yAspect;

void SetAspect(double aspect)
{
    xAspect = 0;
    yAspect = 0;
    aspect = fabs(aspect);
    if (aspect != 1.0)
    {
        if (aspect > 1.0)
            yAspect = (unsigned int)(65536.0 / aspect);
        else
            xAspect = (unsigned int)(65536.0 * aspect);
    }
}

void c_putcolor(int x, int y, int color)
{
    // avoid writing outside window
    if (x < xc || y < yc || x >= xc + xd || y >= yc + yd)
        return ;
    if (y >= sydots - show_numbers) // avoid overwriting coords
        return;
    if (windows == 2) // avoid overwriting fractal
        if (0 <= x && x < xdots && 0 <= y && y < ydots)
            return;
    putcolor(x, y, color);
}


int  c_getcolor(int x, int y)
{
    // avoid reading outside window
    if (x < xc || y < yc || x >= xc + xd || y >= yc + yd)
        return 1000;
    if (y >= sydots - show_numbers) // avoid overreading coords
        return 1000;
    if (windows == 2) // avoid overreading fractal
        if (0 <= x && x < xdots && 0 <= y && y < ydots)
            return 1000;
    return getcolor(x, y);
}

void circleplot(int x, int y, int color)
{
    if (xAspect == 0)
        if (yAspect == 0)
            c_putcolor(x+xbase, y+ybase, color);
        else
            c_putcolor(x+xbase, (short)(ybase + (((long) y * (long) yAspect) >> 16)), color);
    else
        c_putcolor((int)(xbase + (((long) x * (long) xAspect) >> 16)), y+ybase, color);
}

void plot8(int x, int y, int color)
{
    circleplot(x, y, color);
    circleplot(-x, y, color);
    circleplot(x, -y, color);
    circleplot(-x, -y, color);
    circleplot(y, x, color);
    circleplot(-y, x, color);
    circleplot(y, -x, color);
    circleplot(-y, -x, color);
}

void circle(int radius, int color)
{
    int x, y, sum;

    x = 0;
    y = radius << 1;
    sum = 0;

    while (x <= y)
    {
        if (!(x & 1))     // plot if x is even
            plot8(x >> 1, (y+1) >> 1, color);
        sum += (x << 1) + 1;
        x++;
        if (sum > 0)
        {
            sum -= (y << 1) - 1;
            y--;
        }
    }
}


/*
 * MIIM section:
 *
 * Global variables and service functions used for computing
 * MIIM Julias will be grouped here (and shared by code in LORENZ.C)
 *
 */


long   ListFront, ListBack, ListSize;  // head, tail, size of MIIM Queue
long   lsize, lmax;                    // how many in queue (now, ever)
int    maxhits = 1;
bool   OKtoMIIM = false;
int    SecretExperimentalMode;
float  luckyx = 0, luckyy = 0;

static void fillrect(int x, int y, int width, int depth, int color)
{
    // fast version of fillrect
    if (!hasinverse)
        return;
    memset(dstack, color % colors, width);
    while (depth-- > 0)
    {
        if (driver_key_pressed()) // we could do this less often when in fast modes
            return;
        putrow(x, y++, width, (char *)dstack);
    }
}

/*
 * Queue/Stack Section:
 *
 * Defines a buffer that can be used as a FIFO queue or LIFO stack.
 */

int
QueueEmpty()            // True if NO points remain in queue
{
    return (ListFront == ListBack);
}

int
QueueFullAlmost()       // True if room for ONE more point in queue
{
    return (((ListFront + 2) % ListSize) == ListBack);
}

void
ClearQueue()
{
    lmax = 0;
    lsize = lmax;
    ListBack = lsize;
    ListFront = ListBack;
}


/*
 * Queue functions for MIIM julia:
 * move to JIIM.C when done
 */

bool Init_Queue(unsigned long request)
{
    if (driver_diskp())
    {
        stopmsg(STOPMSG_NONE, "Don't try this in disk video mode, kids...\n");
        ListSize = 0;
        return false;
    }

    for (ListSize = request; ListSize > 1024; ListSize /= 2)
        switch (common_startdisk(ListSize * 8, 1, 256))
        {
        case 0:                        // success
            ListBack = 0;
            ListFront = ListBack;
            lmax = 0;
            lsize = lmax;
            return true;
        case -1:
            continue;                   // try smaller queue size
        case -2:
            ListSize = 0;               // cancelled by user
            return false;
        }

    // failed to get memory for MIIM Queue
    ListSize = 0;
    return false;
}

void
Free_Queue()
{
    enddisk();
    lmax = 0;
    lsize = lmax;
    ListSize = lsize;
    ListBack = ListSize;
    ListFront = ListBack;
}

int
PushLong(long x, long y)
{
    if (((ListFront + 1) % ListSize) != ListBack)
    {
        if (ToMemDisk(8*ListFront, sizeof(x), &x) &&
                ToMemDisk(8*ListFront +sizeof(x), sizeof(y), &y))
        {
            ListFront = (ListFront + 1) % ListSize;
            if (++lsize > lmax)
            {
                lmax   = lsize;
                luckyx = (float)x;
                luckyy = (float)y;
            }
            return 1;
        }
    }
    return 0;                    // fail
}

int
PushFloat(float x, float y)
{
    if (((ListFront + 1) % ListSize) != ListBack)
    {
        if (ToMemDisk(8*ListFront, sizeof(x), &x) &&
                ToMemDisk(8*ListFront +sizeof(x), sizeof(y), &y))
        {
            ListFront = (ListFront + 1) % ListSize;
            if (++lsize > lmax)
            {
                lmax   = lsize;
                luckyx = x;
                luckyy = y;
            }
            return 1;
        }
    }
    return 0;                    // fail
}

DComplex
PopFloat()
{
    DComplex pop;
    float  popx, popy;

    if (!QueueEmpty())
    {
        ListFront--;
        if (ListFront < 0)
            ListFront = ListSize - 1;
        if (FromMemDisk(8*ListFront, sizeof(popx), &popx) &&
                FromMemDisk(8*ListFront +sizeof(popx), sizeof(popy), &popy))
        {
            pop.x = popx;
            pop.y = popy;
            --lsize;
        }
        return pop;
    }
    pop.x = 0;
    pop.y = 0;
    return pop;
}

LComplex
PopLong()
{
    LComplex pop;

    if (!QueueEmpty())
    {
        ListFront--;
        if (ListFront < 0)
            ListFront = ListSize - 1;
        if (FromMemDisk(8*ListFront, sizeof(pop.x), &pop.x) &&
                FromMemDisk(8*ListFront +sizeof(pop.x), sizeof(pop.y), &pop.y))
            --lsize;
        return pop;
    }
    pop.x = 0;
    pop.y = 0;
    return pop;
}

int
EnQueueFloat(float x, float y)
{
    return PushFloat(x, y);
}

int
EnQueueLong(long x, long y)
{
    return PushLong(x, y);
}

DComplex
DeQueueFloat()
{
    DComplex out;
    float outx, outy;

    if (ListBack != ListFront)
    {
        if (FromMemDisk(8*ListBack, sizeof(outx), &outx) &&
                FromMemDisk(8*ListBack +sizeof(outx), sizeof(outy), &outy))
        {
            ListBack = (ListBack + 1) % ListSize;
            out.x = outx;
            out.y = outy;
            lsize--;
        }
        return out;
    }
    out.x = 0;
    out.y = 0;
    return out;
}

LComplex
DeQueueLong()
{
    LComplex out;
    out.x = 0;
    out.y = 0;

    if (ListBack != ListFront)
    {
        if (FromMemDisk(8*ListBack, sizeof(out.x), &out.x) &&
                FromMemDisk(8*ListBack +sizeof(out.x), sizeof(out.y), &out.y))
        {
            ListBack = (ListBack + 1) % ListSize;
            lsize--;
        }
        return out;
    }
    out.x = 0;
    out.y = 0;
    return out;
}



/*
 * End MIIM section;
 */

static void SaveRect(int x, int y, int width, int depth)
{
    char buff[MAXRECT];
    if (!hasinverse)
        return;
    // first, do any de-allocationg

    if (memory_handle != 0)
        MemoryRelease(memory_handle);

    // allocate space and store the rect

    memset(dstack, g_color_dark, width);
    // TODO: MemoryAlloc
    memory_handle = MemoryAlloc((U16)width, (long)depth, MEMORY);
    if (memory_handle != 0)
    {
        Cursor_Hide();
        for (int yoff = 0; yoff < depth; yoff++)
        {
            getrow(x, y+yoff, width, buff);
            putrow(x, y+yoff, width, (char *)dstack);
            MoveToMemory((BYTE *)buff, (U16)width, 1L, (long)(yoff), memory_handle);
        }
        Cursor_Show();
    }
}


static void RestoreRect(int x, int y, int width, int depth)
{
    char buff[MAXRECT];
    if (!hasinverse)
        return;

    Cursor_Hide();
    for (int yoff = 0; yoff < depth; yoff++)
    {
        MoveFromMemory((BYTE *)buff, (U16)width, 1L, (long)(yoff), memory_handle);
        putrow(x, y+yoff, width, buff);
    }
    Cursor_Show();
}

/*
 * interface to FRACTINT
 */

DComplex SaveC = {-3000.0, -3000.0};

void Jiim(int which)         // called by fractint
{
    affine cvt;
    bool exact = false;
    int oldhelpmode;
    int count = 0;            // coloring julia
    static int mode = 0;      // point, circle, ...
    int       oldlookatmouse = lookatmouse;
    double cr, ci, r;
    int xfactor, yfactor;             // aspect ratio

    int xoff, yoff;                   // center of the window
    int x, y;
    int kbdchar = -1;

    long iter;
    int color;
    float zoom;
    int oldsxoffs, oldsyoffs;
    int (*oldcalctype)();
    int old_x, old_y;
    double aspect;
    static int randir = 0;
    static int rancnt = 0;
    bool actively_computing = true;
    bool first_time = true;
    int old_debugflag;

    old_debugflag = debugflag;
    // must use standard fractal or be calcfroth
    if (fractalspecific[static_cast<int>(fractype)].calctype != StandardFractal
            && fractalspecific[static_cast<int>(fractype)].calctype != calcfroth)
        return;
    oldhelpmode = helpmode;
    if (which == JIIM)
        helpmode = HELP_JIIM;
    else
    {
        helpmode = HELP_ORBITS;
        hasinverse = true;
    }
    oldsxoffs = sxoffs;
    oldsyoffs = syoffs;
    oldcalctype = calctype;
    show_numbers = 0;
    using_jiim = true;
    mem_init(strlocn, 10*1024);
    line_buff = static_cast<BYTE *>(newx(std::max(sxdots, sydots)));
    aspect = ((double)xdots*3)/((double)ydots*4);  // assumes 4:3
    actively_computing = true;
    SetAspect(aspect);
    lookatmouse = 3;

    if (which == ORBIT)
        (*PER_IMAGE)();

    Cursor_Construct();

    /*
     * MIIM code:
     * Grab memory for Queue/Stack before SaveRect gets it.
     */
    OKtoMIIM  = false;
    if (which == JIIM && debugflag != debug_flags::prevent_miim)
        OKtoMIIM = Init_Queue(8*1024UL); // Queue Set-up Successful?

    maxhits = 1;
    if (which == ORBIT)
        plot = c_putcolor;                // for line with clipping

    /*
     * end MIIM code.
     */

    if (!g_video_scroll)
    {
        g_vesa_x_res = sxdots;
        g_vesa_y_res = sydots;
    }

    if (sxoffs != 0 || syoffs != 0) // we're in view windows
    {
        bool const savehasinverse = hasinverse;
        hasinverse = true;
        SaveRect(0, 0, xdots, ydots);
        sxoffs = g_video_start_x;
        syoffs = g_video_start_y;
        RestoreRect(0, 0, xdots, ydots);
        hasinverse = savehasinverse;
    }

    if (xdots == g_vesa_x_res || ydots == g_vesa_y_res ||
            g_vesa_x_res-xdots < g_vesa_x_res/3 ||
            g_vesa_y_res-ydots < g_vesa_y_res/3 ||
            xdots >= MAXRECT)
    {
        /* this mode puts orbit/julia in an overlapping window 1/3 the size of
           the physical screen */
        windows = 0; // full screen or large view window
        xd = g_vesa_x_res / 3;
        yd = g_vesa_y_res / 3;
        xc = g_video_start_x + xd * 2;
        yc = g_video_start_y + yd * 2;
        xoff = g_video_start_x + xd * 5 / 2;
        yoff = g_video_start_y + yd * 5 / 2;
    }
    else if (xdots > g_vesa_x_res/3 && ydots > g_vesa_y_res/3)
    {
        // Julia/orbit and fractal don't overlap
        windows = 1;
        xd = g_vesa_x_res - xdots;
        yd = g_vesa_y_res - ydots;
        xc = g_video_start_x + xdots;
        yc = g_video_start_y + ydots;
        xoff = xc + xd/2;
        yoff = yc + yd/2;

    }
    else
    {
        // Julia/orbit takes whole screen
        windows = 2;
        xd = g_vesa_x_res;
        yd = g_vesa_y_res;
        xc = g_video_start_x;
        yc = g_video_start_y;
        xoff = g_video_start_x + xd/2;
        yoff = g_video_start_y + yd/2;
    }

    xfactor = (int)(xd/5.33);
    yfactor = (int)(-yd/4);

    if (windows == 0)
        SaveRect(xc, yc, xd, yd);
    else if (windows == 2)  // leave the fractal
    {
        fillrect(xdots, yc, xd-xdots, yd, g_color_dark);
        fillrect(xc   , ydots, xdots, yd-ydots, g_color_dark);
    }
    else  // blank whole window
        fillrect(xc, yc, xd, yd, g_color_dark);

    setup_convert_to_screen(&cvt);

    // reuse last location if inside window
    col = (int)(cvt.a*SaveC.x + cvt.b*SaveC.y + cvt.e + .5);
    row = (int)(cvt.c*SaveC.x + cvt.d*SaveC.y + cvt.f + .5);
    if (col < 0 || col >= xdots ||
            row < 0 || row >= ydots)
    {
        cr = (xxmax + xxmin) / 2.0;
        ci = (yymax + yymin) / 2.0;
    }
    else
    {
        cr = SaveC.x;
        ci = SaveC.y;
    }

    old_y = -1;
    old_x = old_y;

    col = (int)(cvt.a*cr + cvt.b*ci + cvt.e + .5);
    row = (int)(cvt.c*cr + cvt.d*ci + cvt.f + .5);

    // possible extraseg arrays have been trashed, so set up again
    if (integerfractal)
        fill_lx_array();
    else
        fill_dx_array();

    Cursor_SetPos(col, row);
    Cursor_Show();
    color = g_color_bright;

    iter = 1;
    bool still = true;
    zoom = 1;

#ifdef XFRACT
    Cursor_StartMouseTracking();
#endif

    while (still)
    {
        if (actively_computing)
        {
            Cursor_CheckBlink();
        }
        else
        {
            Cursor_WaitKey();
        }
        if (driver_key_pressed() || first_time) // prevent burning up UNIX CPU
        {
            first_time = false;
            while (driver_key_pressed())
            {
                Cursor_WaitKey();
                kbdchar = driver_get_key();

                int dcol = 0;
                int drow = 0;
                xcjul = BIG;
                ycjul = BIG;
                switch (kbdchar)
                {
                case FIK_CTL_KEYPAD_5:      // ctrl - keypad 5
                case FIK_KEYPAD_5:          // keypad 5
                    break;                  // do nothing
                case FIK_CTL_PAGE_UP:
                    dcol = 4;
                    drow = -4;
                    break;
                case FIK_CTL_PAGE_DOWN:
                    dcol = 4;
                    drow = 4;
                    break;
                case FIK_CTL_HOME:
                    dcol = -4;
                    drow = -4;
                    break;
                case FIK_CTL_END:
                    dcol = -4;
                    drow = 4;
                    break;
                case FIK_PAGE_UP:
                    dcol = 1;
                    drow = -1;
                    break;
                case FIK_PAGE_DOWN:
                    dcol = 1;
                    drow = 1;
                    break;
                case FIK_HOME:
                    dcol = -1;
                    drow = -1;
                    break;
                case FIK_END:
                    dcol = -1;
                    drow = 1;
                    break;
                case FIK_UP_ARROW:
                    drow = -1;
                    break;
                case FIK_DOWN_ARROW:
                    drow = 1;
                    break;
                case FIK_LEFT_ARROW:
                    dcol = -1;
                    break;
                case FIK_RIGHT_ARROW:
                    dcol = 1;
                    break;
                case FIK_CTL_UP_ARROW:
                    drow = -4;
                    break;
                case FIK_CTL_DOWN_ARROW:
                    drow = 4;
                    break;
                case FIK_CTL_LEFT_ARROW:
                    dcol = -4;
                    break;
                case FIK_CTL_RIGHT_ARROW:
                    dcol = 4;
                    break;
                case 'z':
                case 'Z':
                    zoom = 1.0F;
                    break;
                case '<':
                case ',':
                    zoom /= 1.15F;
                    break;
                case '>':
                case '.':
                    zoom *= 1.15F;
                    break;
                case FIK_SPACE:
                    xcjul = cr;
                    ycjul = ci;
                    goto finish;
                case 'c':   // circle toggle
                case 'C':   // circle toggle
                    mode = mode ^ 1;
                    break;
                case 'l':
                case 'L':
                    mode = mode ^ 2;
                    break;
                case 'n':
                case 'N':
                    show_numbers = 8 - show_numbers;
                    if (windows == 0 && show_numbers == 0)
                    {
                        Cursor_Hide();
                        cleartempmsg();
                        Cursor_Show();
                    }
                    break;
                case 'p':
                case 'P':
                    get_a_number(&cr, &ci);
                    exact = true;
                    col = (int)(cvt.a*cr + cvt.b*ci + cvt.e + .5);
                    row = (int)(cvt.c*cr + cvt.d*ci + cvt.f + .5);
                    drow = 0;
                    dcol = drow;
                    break;
                case 'h':   // hide fractal toggle
                case 'H':   // hide fractal toggle
                    if (windows == 2)
                        windows = 3;
                    else if (windows == 3 && xd == g_vesa_x_res)
                    {
                        RestoreRect(g_video_start_x, g_video_start_y, xdots, ydots);
                        windows = 2;
                    }
                    break;
#ifdef XFRACT
                case FIK_ENTER:
                    break;
#endif
                case '0':
                case '1':
                case '2':
                    // don't use '3', it's already meaningful
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    if (which == JIIM)
                    {
                        SecretExperimentalMode = kbdchar - '0';
                        break;
                    }
                default:
                    still = false;
                }  // switch
                if (kbdchar == 's' || kbdchar == 'S')
                    goto finish;
                if (dcol > 0 || drow > 0)
                    exact = false;
                col += dcol;
                row += drow;
#ifdef XFRACT
                if (kbdchar == FIK_ENTER)
                {
                    // We want to use the position of the cursor
                    exact = false;
                    col = Cursor_GetX();
                    row = Cursor_GetY();
                }
#endif

                // keep cursor in logical screen
                if (col >= xdots)
                {
                    col = xdots -1;
                    exact = false;
                }
                if (row >= ydots)
                {
                    row = ydots -1;
                    exact = false;
                }
                if (col < 0)
                {
                    col = 0;
                    exact = false;
                }
                if (row < 0)
                {
                    row = 0;
                    exact = false;
                }

                Cursor_SetPos(col, row);
            }  // end while (driver_key_pressed)

            if (!exact)
            {
                if (integerfractal)
                {
                    cr = lxpixel();
                    ci = lypixel();
                    cr /= (1L << bitshift);
                    ci /= (1L << bitshift);
                }
                else
                {
                    cr = dxpixel();
                    ci = dypixel();
                }
            }
            actively_computing = true;
            if (show_numbers) // write coordinates on screen
            {
                char str[41];
                sprintf(str, "%16.14f %16.14f %3d", cr, ci, getcolor(col, row));
                if (windows == 0)
                {
                    /* show temp msg will clear self if new msg is a
                       different length - pad to length 40*/
                    while ((int)strlen(str) < 40)
                        strcat(str, " ");
                    str[40] = 0;
                    Cursor_Hide();
                    actively_computing = true;
                    showtempmsg(str);
                    Cursor_Show();
                }
                else
                    driver_display_string(5, g_vesa_y_res-show_numbers, WHITE, BLACK, str);
            }
            iter = 1;
            lold.y = 0;
            lold.x = lold.y;
            old.y = lold.x;
            old.x = old.y;
            init.x = cr;
            SaveC.x = init.x;
            init.y = ci;
            SaveC.y = init.y;
            linit.x = (long)(init.x*fudge);
            linit.y = (long)(init.y*fudge);

            old_y = -1;
            old_x = old_y;
            /*
             * MIIM code:
             * compute fixed points and use them as starting points of JIIM
             */
            if (which == JIIM && OKtoMIIM)
            {
                DComplex f1, f2, Sqrt;        // Fixed points of Julia

                Sqrt = ComplexSqrtFloat(1 - 4 * cr, -4 * ci);
                f1.x = (1 + Sqrt.x) / 2;
                f2.x = (1 - Sqrt.x) / 2;
                f1.y =  Sqrt.y / 2;
                f2.y = -Sqrt.y / 2;

                ClearQueue();
                maxhits = 1;
                EnQueueFloat((float)f1.x, (float)f1.y);
                EnQueueFloat((float)f2.x, (float)f2.y);
            }
            /*
             * End MIIM code.
             */
            if (which == ORBIT)
            {
                PER_PIXEL();
            }
            // move window if bumped
            if (windows == 0 && col > xc && col < xc+xd && row > yc && row < yc+yd)
            {
                RestoreRect(xc, yc, xd, yd);
                if (xc == g_video_start_x + xd*2)
                    xc = g_video_start_x + 2;
                else
                    xc = g_video_start_x + xd*2;
                xoff = xc + xd /  2;
                SaveRect(xc, yc, xd, yd);
            }
            if (windows == 2)
            {
                fillrect(xdots, yc, xd-xdots, yd-show_numbers, g_color_dark);
                fillrect(xc   , ydots, xdots, yd-ydots-show_numbers, g_color_dark);
            }
            else
                fillrect(xc, yc, xd, yd, g_color_dark);

        } // end if (driver_key_pressed)

        if (which == JIIM)
        {
            if (!hasinverse)
                continue;
            /*
             * MIIM code:
             * If we have MIIM queue allocated, then use MIIM method.
             */
            if (OKtoMIIM)
            {
                if (QueueEmpty())
                {
                    if (maxhits < colors - 1 && maxhits < 5 &&
                            (luckyx != 0.0 || luckyy != 0.0))
                    {
                        lmax = 0;
                        lsize = lmax;
                        g_new.x = luckyx;
                        old.x = g_new.x;
                        g_new.y = luckyy;
                        old.y = g_new.y;
                        luckyy = 0.0F;
                        luckyx = luckyy;
                        for (int i = 0; i < 199; i++)
                        {
                            old = ComplexSqrtFloat(old.x - cr, old.y - ci);
                            g_new = ComplexSqrtFloat(g_new.x - cr, g_new.y - ci);
                            EnQueueFloat((float)g_new.x, (float)g_new.y);
                            EnQueueFloat((float)-old.x, (float)-old.y);
                        }
                        maxhits++;
                    }
                    else
                        continue;             // loop while (still)
                }

                old = DeQueueFloat();

                x = (int)(old.x * xfactor * zoom + xoff);
                y = (int)(old.y * yfactor * zoom + yoff);
                color = c_getcolor(x, y);
                if (color < maxhits)
                {
                    c_putcolor(x, y, color + 1);
                    g_new = ComplexSqrtFloat(old.x - cr, old.y - ci);
                    EnQueueFloat((float)g_new.x, (float)g_new.y);
                    EnQueueFloat((float)-g_new.x, (float)-g_new.y);
                }
            }
            else
            {
                /*
                 * end Msnyder code, commence if not MIIM code.
                 */
                old.x -= cr;
                old.y -= ci;
                r = old.x*old.x + old.y*old.y;
                if (r > 10.0)
                {
                    old.y = 0.0;
                    old.x = old.y; // avoids math error
                    iter = 1;
                    r = 0;
                }
                iter++;
                color = ((count++) >> 5)%colors; // chg color every 32 pts
                if (color == 0)
                    color = 1;

                //       r = sqrt(old.x*old.x + old.y*old.y); calculated above
                r = sqrt(r);
                g_new.x = sqrt(fabs((r + old.x)/2));
                if (old.y < 0)
                    g_new.x = -g_new.x;

                g_new.y = sqrt(fabs((r - old.x)/2));


                switch (SecretExperimentalMode)
                {
                case 0:                     // unmodified random walk
                default:
                    if (rand() % 2)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    }
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    break;
                case 1:                     // always go one direction
                    if (SaveC.y < 0)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    }
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    break;
                case 2:                     // go one dir, draw the other
                    if (SaveC.y < 0)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    }
                    x = (int)(-g_new.x * xfactor * zoom + xoff);
                    y = (int)(-g_new.y * yfactor * zoom + yoff);
                    break;
                case 4:                     // go negative if max color
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    if (c_getcolor(x, y) == colors - 1)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                        x = (int)(g_new.x * xfactor * zoom + xoff);
                        y = (int)(g_new.y * yfactor * zoom + yoff);
                    }
                    break;
                case 5:                     // go positive if max color
                    g_new.x = -g_new.x;
                    g_new.y = -g_new.y;
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    if (c_getcolor(x, y) == colors - 1)
                    {
                        x = (int)(g_new.x * xfactor * zoom + xoff);
                        y = (int)(g_new.y * yfactor * zoom + yoff);
                    }
                    break;
                case 7:
                    if (SaveC.y < 0)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    }
                    x = (int)(-g_new.x * xfactor * zoom + xoff);
                    y = (int)(-g_new.y * yfactor * zoom + yoff);
                    if (iter > 10)
                    {
                        if (mode == 0)                        // pixels
                            c_putcolor(x, y, color);
                        else if (mode & 1)            // circles
                        {
                            xbase = x;
                            ybase = y;
                            circle((int)(zoom*(xd >> 1)/iter), color);
                        }
                        if ((mode & 2) && x > 0 && y > 0 && old_x > 0 && old_y > 0)
                        {
                            driver_draw_line(x, y, old_x, old_y, color);
                        }
                        old_x = x;
                        old_y = y;
                    }
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    break;
                case 8:                     // go in long zig zags
                    if (rancnt >= 300)
                        rancnt = -300;
                    if (rancnt < 0)
                    {
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    }
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    break;
                case 9:                     // "random run"
                    switch (randir)
                    {
                    case 0:             // go random direction for a while
                        if (rand() % 2)
                        {
                            g_new.x = -g_new.x;
                            g_new.y = -g_new.y;
                        }
                        if (++rancnt > 1024)
                        {
                            rancnt = 0;
                            if (rand() % 2)
                                randir =  1;
                            else
                                randir = -1;
                        }
                        break;
                    case 1:             // now go negative dir for a while
                        g_new.x = -g_new.x;
                        g_new.y = -g_new.y;
                    // fall through
                    case -1:            // now go positive dir for a while
                        if (++rancnt > 512)
                        {
                            rancnt = 0;
                            randir = rancnt;
                        }
                        break;
                    }
                    x = (int)(g_new.x * xfactor * zoom + xoff);
                    y = (int)(g_new.y * yfactor * zoom + yoff);
                    break;
                } // end switch SecretMode (sorry about the indentation)
            } // end if not MIIM
        }
        else // orbits
        {
            if (iter < maxit)
            {
                color = (int)iter%colors;
                if (integerfractal)
                {
                    old.x = lold.x;
                    old.x /= fudge;
                    old.y = lold.y;
                    old.y /= fudge;
                }
                x = (int)((old.x - init.x) * xfactor * 3 * zoom + xoff);
                y = (int)((old.y - init.y) * yfactor * 3 * zoom + yoff);
                if ((*ORBITCALC)())
                    iter = maxit;
                else
                    iter++;
            }
            else
            {
                y = -1;
                x = y;
                actively_computing = false;
            }
        }
        if (which == ORBIT || iter > 10)
        {
            if (mode == 0)                  // pixels
                c_putcolor(x, y, color);
            else if (mode & 1)            // circles
            {
                xbase = x;
                ybase = y;
                circle((int)(zoom*(xd >> 1)/iter), color);
            }
            if ((mode & 2) && x > 0 && y > 0 && old_x > 0 && old_y > 0)
            {
                driver_draw_line(x, y, old_x, old_y, color);
            }
            old_x = x;
            old_y = y;
        }
        old = g_new;
        lold = lnew;
    } // end while (still)
finish:
    Free_Queue();

    if (kbdchar != 's' && kbdchar != 'S')
    {
        Cursor_Hide();
        if (windows == 0)
            RestoreRect(xc, yc, xd, yd);
        else if (windows >= 2)
        {
            if (windows == 2)
            {
                fillrect(xdots, yc, xd-xdots, yd, g_color_dark);
                fillrect(xc   , ydots, xdots, yd-ydots, g_color_dark);
            }
            else
                fillrect(xc, yc, xd, yd, g_color_dark);
            if (windows == 3 && xd == g_vesa_x_res) // unhide
            {
                RestoreRect(0, 0, xdots, ydots);
                windows = 2;
            }
            Cursor_Hide();
            bool const savehasinverse = hasinverse;
            hasinverse = true;
            SaveRect(0, 0, xdots, ydots);
            sxoffs = oldsxoffs;
            syoffs = oldsyoffs;
            RestoreRect(0, 0, xdots, ydots);
            hasinverse = savehasinverse;
        }
    }
    Cursor_Destroy();
#ifdef XFRACT
    Cursor_EndMouseTracking();
#endif
    delete(line_buff);

    if (memory_handle != 0)
    {
        MemoryRelease(memory_handle);
        memory_handle = 0;
    }

    lookatmouse = oldlookatmouse;
    using_jiim = false;
    calctype = oldcalctype;
    debugflag = old_debugflag;
    helpmode = oldhelpmode;
    if (kbdchar == 's' || kbdchar == 'S')
    {
        viewwindow = false;
        viewxdots = 0;
        viewydots = 0;
        viewreduction = 4.2F;
        viewcrop = true;
        finalaspectratio = screenaspect;
        xdots = sxdots;
        ydots = sydots;
        d_x_size = xdots - 1;
        d_y_size = ydots - 1;
        sxoffs = 0;
        syoffs = 0;
        freetempmsg();
    }
    else
        cleartempmsg();
    if (file != nullptr)
    {
        fclose(file);
        file = nullptr;
        dir_remove(tempdir, scrnfile);
    }
    show_numbers = 0;
    driver_unget_key(kbdchar);

    if (curfractalspecific->calctype == calcfroth)
        froth_cleanup();
}
