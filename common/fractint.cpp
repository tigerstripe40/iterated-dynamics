/*
        FRACTINT - The Ultimate Fractal Generator
                        Main Routine
*/
#include <vector>

#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#if defined(XFRACT)
#include <unistd.h>
#else
#include <io.h>
#endif

#include "port.h"
#include "prototyp.h"
#include "fractype.h"
#include "helpdefs.h"
#include "drivers.h"
#include "helpcom.h"

VIDEOINFO g_video_entry;
int helpmode;

int lookatmouse = 0;  /* see notes at mouseread routine */

long timer_start, timer_interval;        /* timer(...) start & total */
int     g_adapter;                /* Video Adapter chosen from list in ...h */
const char *fract_dir1 = "";
const char *fract_dir2 = "";

/*
   the following variables are out here only so
   that the calcfract() and assembler routines can get at them easily
*/
int     dotmode;                /* video access method      */
int     textsafe2;              /* textsafe override from g_video_table */
int     sxdots, sydots;          /* # of dots on the physical screen    */
int     sxoffs, syoffs;          /* physical top left of logical screen */
int     xdots, ydots;           /* # of dots on the logical screen     */
double  d_x_size, d_y_size;     /* xdots-1, ydots-1         */
int     colors = 256;           /* maximum colors available */
long    maxit;                  /* try this many iterations */
int     boxcount;               /* 0 if no zoom-box yet     */
int     zrotate;                /* zoombox rotation         */
double  zbx, zby;                /* topleft of zoombox       */
double  zwidth, zdepth, zskew;    /* zoombox size & shape     */

fractal_type fractype;               /* if == 0, use Mandelbrot  */
char    stdcalcmode;            /* '1', '2', 'g', 'b'       */
long    c_real, c_imag;           /* real, imag'ry parts of C */
long    delx, dely;             /* screen pixel increments  */
long    delx2, dely2;           /* screen pixel increments  */
LDBL    delxx, delyy;           /* screen pixel increments  */
LDBL    delxx2, delyy2;         /* screen pixel increments  */
long    delmin;                 /* for calcfrac/calcmand    */
double  ddelmin;                /* same as a double         */
double  param[MAXPARAMS];       /* parameters               */
double  potparam[3];            /* three potential parameters*/
long    fudge;                  /* 2**fudgefactor           */
long    l_at_rad;               /* finite attractor radius  */
double  f_at_rad;               /* finite attractor radius  */
int     bitshift;               /* fudgefactor              */

int     g_bad_config = 0;          /* 'fractint.cfg' ok?       */
bool hasinverse = false;
/* note that integer grid is set when integerfractal && !invert;    */
/* otherwise the floating point grid is set; never both at once     */
std::vector<long> lx0;              /* x, y grid                */
std::vector<long> ly0;
std::vector<long> lx1;              /* adjustment for rotate    */
std::vector<long> ly1;
/* note that lx1 & ly1 values can overflow into sign bit; since     */
/* they're used only to add to lx0/ly0, 2s comp straightens it out  */
std::vector<double> dx0;            /* floating pt equivs */
std::vector<double> dy0;
std::vector<double> dx1;
std::vector<double> dy1;
int     integerfractal;         /* TRUE if fractal uses integer math */

/* usr_xxx is what the user wants, vs what we may be forced to do */
char    usr_stdcalcmode;
int     usr_periodicitycheck;
long    usr_distest;
bool    usr_floatflag;

bool    viewwindow = false;     /* false for full screen, true for window */
float   viewreduction;          /* window auto-sizing */
bool    viewcrop = false;       /* true to crop default coords */
float   finalaspectratio;       /* for view shape and rotation */
int     viewxdots, viewydots;    /* explicit view sizing */
bool    video_cutboth = false;  /* true to keep virtual aspect */
bool    zscroll = false;        /* screen/zoombox false fixed, true relaxed */

/*      HISTORY  *history = nullptr; */
U16 history = 0;
int maxhistory = 10;

/* variables defined by the command line/files processor */
bool    comparegif = false;             /* compare two gif files flag */
int     timedsave = 0;                    /* when doing a timed save */
int     resave_flag = 0;                  /* tells encoder not to incr filename */
bool    started_resaves = false;        /* but incr on first resave */
int     save_system;                    /* from and for save files */
bool    tabmode = true;                 /* tab display enabled */

/* for historical reasons (before rotation):         */
/*    top    left  corner of screen is (xxmin,yymax) */
/*    bottom left  corner of screen is (xx3rd,yy3rd) */
/*    bottom right corner of screen is (xxmax,yymin) */
double  xxmin, xxmax, yymin, yymax, xx3rd, yy3rd; /* selected screen corners  */
long    xmin, xmax, ymin, ymax, x3rd, y3rd;  /* integer equivs           */
double  sxmin, sxmax, symin, symax, sx3rd, sy3rd; /* displayed screen corners */
double  plotmx1, plotmx2, plotmy1, plotmy2;     /* real->screen multipliers */

calc_status_value calc_status = calc_status_value::NO_FRACTAL;
/* -1 no fractal                   */
/*  0 parms changed, recalc reqd   */
/*  1 actively calculating         */
/*  2 interrupted, resumable       */
/*  3 interrupted, not resumable   */
/*  4 completed                    */
long calctime;

int max_colors;                         /* maximum palette size */
bool zoomoff = false;                   /* false when zoom is disabled */
int        savedac;                     /* save-the-Video DAC flag      */
bool browsing = false;                  /* browse mode flag */
char file_name_stack[16][13]; /* array of file names used while browsing */
int name_stack_ptr ;
double toosmall;
int  minbox;
bool no_sub_images = false;
bool autobrowse = false;
bool doublecaution = false;
bool brwscheckparms = false;
bool brwschecktype = false;
char browsemask[13];
int scale_map[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}; /* array for mapping notes to a (user defined) scale */


void check_samename()
{
    char drive[FILE_MAX_DRIVE];
    char dir[FILE_MAX_DIR];
    char fname[FILE_MAX_FNAME];
    char ext[FILE_MAX_EXT];
    char path[FILE_MAX_PATH];
    splitpath(savename, drive, dir, fname, ext);
    if (strcmp(fname, "fract001"))
    {
        makepath(path, drive, dir, fname, "gif");
        if (access(path, 0) == 0)
            exit(0);
    }
}

/* Do nothing if math error */
static void my_floating_point_err(int sig)
{
    if (sig != 0)
        overflow = true;
}

/*
; ****************** Function initasmvars() *****************************
*/
void initasmvars()
{
    overflow = false;
}

int main(int argc, char **argv)
{
    bool resumeflag = false;
    int kbdchar;                        /* keyboard key-hit value       */
    bool kbdmore = false;               /* continuation variable        */
    bool stacked = false;               /* flag to indicate screen stacked */

    /* this traps non-math library floating point errors */
    signal(SIGFPE, my_floating_point_err);

    initasmvars();                       /* initialize ASM stuff */
    InitMemory();

    /* let drivers add their video modes */
    if (! init_drivers(&argc, argv))
    {
        init_failure("Sorry, I couldn't find any working video drivers for your system\n");
        exit(-1);
    }
    /* load fractint.cfg, match against driver supplied modes */
    load_fractint_config();
    init_help();


restart:   /* insert key re-starts here */
#if defined(_WIN32)
    _ASSERTE(_CrtCheckMemory());
#endif
    autobrowse     = false;
    brwschecktype  = false;
    brwscheckparms = true;
    doublecaution  = true;
    no_sub_images = false;
    toosmall = 6;
    minbox   = 3;
    strcpy(browsemask, "*.gif");
    strcpy(browsename, "            ");
    name_stack_ptr = -1; /* init loaded files stack */

    evolving = 0;
    paramrangex = 4;
    newopx = -2.0;
    opx = newopx;
    paramrangey = 3;
    newopy = -1.5;
    opy = newopy;
    odpy = 0;
    odpx = odpy;
    gridsz = 9;
    fiddlefactor = 1;
    fiddle_reduction = 1.0;
    this_gen_rseed = (unsigned int)clock_ticks();
    srand(this_gen_rseed);
    initgene(); /*initialise pointers to lots of fractint variables for the evolution engine*/
    start_showorbit = false;
    showdot = -1; /* turn off showdot if entered with <g> command */
    calc_status = calc_status_value::NO_FRACTAL;                    /* no active fractal image */

    fract_dir1 = getenv("FRACTDIR");
    if (fract_dir1 == nullptr)
    {
        fract_dir1 = ".";
    }
#ifdef SRCDIR
    fract_dir2 = SRCDIR;
#else
    fract_dir2 = ".";
#endif

    cmdfiles(argc, argv);         /* process the command-line */
    dopause(0);                  /* pause for error msg if not batch */
    init_msg("", nullptr, cmd_file::AT_CMD_LINE);  /* this causes driver_get_key if init_msg called on runup */

    while (maxhistory > 0) /* decrease history if necessary */
    {
        history = MemoryAlloc((U16) sizeof(HISTORY), (long) maxhistory, MEMORY);
        if (history)
        {
            break;
        }
        maxhistory--;
    }

    if (debugflag == debug_flags::prevent_overwrite_savename && initbatch == 1)   /* abort if savename already exists */
    {
        check_samename();
    }
    driver_window();
    memcpy(olddacbox, g_dac_box, 256*3);      /* save in case colors= present */

    driver_set_for_text();                      /* switch to text mode */
    savedac = 0;                         /* don't save the VGA DAC */

#ifndef XFRACT
    if (g_bad_config < 0)                   /* fractint.cfg bad, no msg yet */
    {
        bad_fractint_cfg_msg();
    }
#endif

    max_colors = 256;                   /* the Windows version is lower */
    max_kbdcount = 80;                  /* check the keyboard this often */

    if (showfile && g_init_mode < 0)
    {
        intro();                          /* display the credits screen */
        if (driver_key_pressed() == FIK_ESC)
        {
            driver_get_key();
            goodbye();
        }
    }

    browsing = false;

    if (!functionpreloaded)
    {
        set_if_old_bif();
    }
    stacked = 0;

restorestart:
#if defined(_WIN32)
    _ASSERTE(_CrtCheckMemory());
#endif

    if (colorpreloaded)
    {
        memcpy(g_dac_box, olddacbox, 256*3);   /* restore in case colors= present */
    }

    lookatmouse = 0;                     /* ignore mouse */

    while (showfile <= 0)              /* image is to be loaded */
    {
        const char *hdg;
        tabmode = false;
        if (!browsing)      /*RB*/
        {
            if (overlay3d)
            {
                hdg = "Select File for 3D Overlay";
                helpmode = HELP3DOVLY;
            }
            else if (display3d)
            {
                hdg = "Select File for 3D Transform";
                helpmode = HELP3D;
            }
            else
            {
                hdg = "Select File to Restore";
                helpmode = HELPSAVEREST;
            }
            if (showfile < 0 && getafilename(hdg, gifmask, readname))
            {
                showfile = 1;               /* cancelled */
                g_init_mode = -1;
                break;
            }

            name_stack_ptr = 0; /* 'r' reads first filename for browsing */
            strcpy(file_name_stack[name_stack_ptr], browsename);
        }

        evolving = 0;
        viewwindow = false;
        showfile = 0;
        helpmode = -1;
        tabmode = true;
        if (stacked)
        {
            driver_discard_screen();
            driver_set_for_text();
            stacked = 0;
        }
        if (read_overlay() == 0)       /* read hdr, get video mode */
        {
            break;                      /* got it, exit */
        }
        if (browsing) /* break out of infinite loop, but lose your mind */
        {
            showfile = 1;
        }
        else
        {
            showfile = -1;                 /* retry */
        }
    }

    helpmode = HELPMENU;                 /* now use this help mode */
    tabmode = true;
    lookatmouse = 0;                     /* ignore mouse */

    if (((overlay3d && !initbatch) || stacked) && g_init_mode < 0)        /* overlay command failed */
    {
        driver_unstack_screen();                  /* restore the graphics screen */
        stacked = 0;
        overlay3d = false;              /* forget overlays */
        display3d = 0;                    /* forget 3D */
        if (calc_status == calc_status_value::NON_RESUMABLE)
            calc_status = calc_status_value::PARAMS_CHANGED;
        resumeflag = true;
        goto resumeloop;                  /* ooh, this is ugly */
    }

    savedac = 0;                         /* don't save the VGA DAC */

imagestart:                             /* calc/display a new image */
#if defined(_WIN32)
    _ASSERTE(_CrtCheckMemory());
#endif

    if (stacked)
    {
        driver_discard_screen();
        stacked = 0;
    }
#ifdef XFRACT
    usr_floatflag = true;
#endif
    got_status = -1;                     /* for tab_display */

    if (showfile)
        if (calc_status > calc_status_value::PARAMS_CHANGED)              /* goto imagestart implies re-calc */
            calc_status = calc_status_value::PARAMS_CHANGED;

    if (initbatch == 0)
        lookatmouse = -FIK_PAGE_UP;           /* just mouse left button, == pgup */

    cyclelimit = initcyclelimit;         /* default cycle limit   */
    g_adapter = g_init_mode;                  /* set the video adapter up */
    g_init_mode = -1;                       /* (once)                   */

    while (g_adapter < 0)                /* cycle through instructions */
    {
        if (initbatch)                          /* batch, nothing to do */
        {
            initbatch = 4;                 /* exit with error condition set */
            goodbye();
        }
        kbdchar = main_menu(0);
        if (kbdchar == FIK_INSERT)
            goto restart;      /* restart pgm on Insert Key */
        if (kbdchar == FIK_DELETE)                    /* select video mode list */
            kbdchar = select_video_mode(-1);
        g_adapter = check_vidmode_key(0, kbdchar);
        if (g_adapter >= 0)
            break;                                 /* got a video mode now */
#ifndef XFRACT
        if ('A' <= kbdchar && kbdchar <= 'Z')
            kbdchar = tolower(kbdchar);
#endif
        if (kbdchar == 'd')
        {                     /* shell to DOS */
            driver_set_clear();
            driver_shell();
            goto imagestart;
        }

#ifndef XFRACT
        if (kbdchar == '@' || kbdchar == '2')
        {    /* execute commands */
#else
        if (kbdchar == FIK_F2 || kbdchar == '@')
        {     /* We mapped @ to F2 */
#endif
            if ((get_commands() & CMDARG_3D_YES) == 0)
                goto imagestart;
            kbdchar = '3';                         /* 3d=y so fall thru '3' code */
        }
#ifndef XFRACT
        if (kbdchar == 'r' || kbdchar == '3' || kbdchar == '#')
        {
#else
        if (kbdchar == 'r' || kbdchar == '3' || kbdchar == FIK_F3)
        {
#endif
            display3d = 0;
            if (kbdchar == '3' || kbdchar == '#' || kbdchar == FIK_F3)
                display3d = 1;
            if (colorpreloaded)
                memcpy(olddacbox, g_dac_box, 256*3); /* save in case colors= present */
            driver_set_for_text(); /* switch to text mode */
            showfile = -1;
            goto restorestart;
        }
        if (kbdchar == 't')
        {                   /* set fractal type */
            julibrot = false;
            get_fracttype();
            goto imagestart;
        }
        if (kbdchar == 'x')
        {                   /* generic toggle switch */
            get_toggles();
            goto imagestart;
        }
        if (kbdchar == 'y')
        {                   /* generic toggle switch */
            get_toggles2();
            goto imagestart;
        }
        if (kbdchar == 'z')
        {                   /* type specific parms */
            get_fract_params(1);
            goto imagestart;
        }
        if (kbdchar == 'v')
        {                   /* view parameters */
            get_view_params();
            goto imagestart;
        }
        if (kbdchar == FIK_CTL_B)
        {             /* ctrl B = browse parms*/
            get_browse_params();
            goto imagestart;
        }
        if (kbdchar == FIK_CTL_F)
        {             /* ctrl f = sound parms*/
            get_sound_params();
            goto imagestart;
        }
        if (kbdchar == 'f')
        {                   /* floating pt toggle */
            if (!usr_floatflag)
                usr_floatflag = true;
            else
                usr_floatflag = false;
            goto imagestart;
        }
        if (kbdchar == 'i')
        {                     /* set 3d fractal parms */
            get_fract3d_params(); /* get the parameters */
            goto imagestart;
        }
        if (kbdchar == 'g')
        {
            get_cmd_string(); /* get command string */
            goto imagestart;
        }
        /* buzzer(2); */                          /* unrecognized key */
    }

    zoomoff = true;                     /* zooming is enabled */
    helpmode = HELPMAIN;                /* now use this help mode */
    resumeflag = false;                 /* allows taking goto inside big_while_loop() */

resumeloop:
#if defined(_WIN32)
    _ASSERTE(_CrtCheckMemory());
#endif

    param_history(0); /* save old history */
    /* this switch processes gotos that are now inside function */
    switch (big_while_loop(&kbdmore, &stacked, resumeflag))
    {
    case big_while_loop_result::RESTART:
        goto restart;
    case big_while_loop_result::IMAGE_START:
        goto imagestart;
    case big_while_loop_result::RESTORE_START:
        goto restorestart;
    default:
        break;
    }

    return 0;
}

bool check_key()
{
    int key = driver_key_pressed();
    if (key != 0)
    {
        if (show_orbit)
        {
            scrub_orbit();
        }
        if (key != 'o' && key != 'O')
        {
            return true;
        }
        driver_get_key();
        if (!driver_diskp())
        {
            show_orbit = !show_orbit;
        }
    }
    return false;
}

/* timer function:
     timer(0,(*fractal)())              fractal engine
     timer(1,nullptr,int width)         decoder
     timer(2)                           encoder
  */
int timer(int timertype, int(*subrtn)(), ...)
{
    va_list arg_marker;  /* variable arg list */
    char *timestring;
    time_t ltime;
    FILE *fp = nullptr;
    int out = 0;
    int i;

    va_start(arg_marker, subrtn);

    bool do_bench = timerflag; /* record time? */
    if (timertype == 2)   /* encoder, record time only if debug flag set */
        do_bench = (debugflag == debug_flags::benchmark_encoder);
    if (do_bench)
        fp = dir_fopen(workdir, "bench", "a");
    timer_start = clock_ticks();
    switch (timertype)
    {
    case 0:
        out = (*(int(*)())subrtn)();
        break;
    case 1:
        i = va_arg(arg_marker, int);
        out = (int)decoder((short)i); /* not indirect, safer with overlays */
        break;
    case 2:
        out = encoder();            /* not indirect, safer with overlays */
        break;
    }
    /* next assumes CLOCKS_PER_SEC is 10^n, n>=2 */
    timer_interval = (clock_ticks() - timer_start) / (CLOCKS_PER_SEC/100);

    if (do_bench)
    {
        time(&ltime);
        timestring = ctime(&ltime);
        timestring[24] = 0; /*clobber newline in time string */
        switch (timertype)
        {
        case 1:
            fprintf(fp, "decode ");
            break;
        case 2:
            fprintf(fp, "encode ");
            break;
        }
        fprintf(fp, "%s type=%s resolution = %dx%d maxiter=%ld",
                timestring,
                curfractalspecific->name,
                xdots,
                ydots,
                maxit);
        fprintf(fp, " time= %ld.%02ld secs\n", timer_interval/100, timer_interval%100);
        if (fp != nullptr)
            fclose(fp);
    }
    return out;
}
