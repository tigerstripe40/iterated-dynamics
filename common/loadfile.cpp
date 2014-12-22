/*
        loadfile.c - load an existing fractal image, control level
*/
#include <algorithm>

#include <errno.h>
#include <string.h>
#include <time.h>
#if defined(XFRACT)
#include <unistd.h>
#endif

#include "port.h"
#include "prototyp.h"
#include "fractype.h"
#include "helpdefs.h"
#include "targa_lc.h"
#include "drivers.h"

// routines in this module

static int  find_fractal_info(char *, FRACTAL_INFO *,
                              ext_blk_2 *,
                              ext_blk_3 *,
                              ext_blk_4 *,
                              ext_blk_5 *,
                              ext_blk_6 *,
                              ext_blk_7 *);
static void load_ext_blk(char *loadptr, int loadlen);
static void skip_ext_blk(int *, int *);
static void backwardscompat(FRACTAL_INFO *info);
static bool fix_bof();
static bool fix_period_bof();

int filetype;
bool loaded3d = false;
static FILE *fp;
int fileydots, filexdots, filecolors;
float fileaspectratio;
short skipxdots, skipydots;      // for decoder, when reducing image
bool bad_outside = false;
bool ldcheck = false;

int read_overlay()      // read overlay/3D files, if reqr'd
{
    FRACTAL_INFO read_info;
    char msg[110];
    ext_blk_2 blk_2_info;
    ext_blk_3 blk_3_info;
    ext_blk_4 blk_4_info;
    ext_blk_5 blk_5_info;
    ext_blk_6 blk_6_info;
    ext_blk_7 blk_7_info;

    showfile = 1;                // for any abort exit, pretend done
    g_init_mode = -1;               // no viewing mode set yet
    bool oldfloatflag = usr_floatflag;
    loaded3d = false;
    if (fastrestore)
    {
        viewwindow = false;
    }
    if (has_ext(readname) == nullptr)
    {
        strcat(readname, ".gif");
    }

    if (find_fractal_info(readname, &read_info, &blk_2_info, &blk_3_info,
                          &blk_4_info, &blk_5_info, &blk_6_info, &blk_7_info))
    {
        // didn't find a useable file
        sprintf(msg, "Sorry, %s isn't a file I can decode.", readname);
        stopmsg(STOPMSG_NONE, msg);
        return -1;
    }

    maxit        = read_info.iterationsold;
    int const read_fractype = read_info.fractal_type;
    if (read_fractype < 0 || read_fractype >= num_fractal_types)
    {
        sprintf(msg, "Warning: %s has a bad fractal type; using 0", readname);
        fractype = fractal_type::MANDEL;
    }
    fractype = static_cast<fractal_type>(read_fractype);
    curfractalspecific = &fractalspecific[read_fractype];
    xxmin        = read_info.xmin;
    xxmax        = read_info.xmax;
    yymin        = read_info.ymin;
    yymax        = read_info.ymax;
    param[0]     = read_info.creal;
    param[1]     = read_info.cimag;
    save_release = 1100; // unless we find out better later on

    invert = 0;
    if (read_info.version > 0)
    {
        param[2]      = read_info.parm3;
        roundfloatd(&param[2]);
        param[3]      = read_info.parm4;
        roundfloatd(&param[3]);
        potparam[0]   = read_info.potential[0];
        potparam[1]   = read_info.potential[1];
        potparam[2]   = read_info.potential[2];
        if (*s_makepar == '\0')
        {
            colors = read_info.colors;
        }
        potflag       = (potparam[0] != 0.0);
        rflag         = read_info.rflag != 0;
        rseed         = read_info.rseed;
        inside        = read_info.inside;
        LogFlag       = read_info.logmapold;
        inversion[0]  = read_info.invert[0];
        inversion[1]  = read_info.invert[1];
        inversion[2]  = read_info.invert[2];
        if (inversion[0] != 0.0)
        {
            invert = 3;
        }
        decomp[0]     = read_info.decomp[0];
        decomp[1]     = read_info.decomp[1];
        usr_biomorph  = read_info.biomorph;
        forcesymmetry = static_cast<symmetry_type>(read_info.symmetry);
    }

    if (read_info.version > 1)
    {
        save_release  = 1200;
        if (!display3d
                && (read_info.version <= 4 || read_info.flag3d > 0
                    || (curfractalspecific->flags & PARMS3D)))
        {
            for (int i = 0; i < 16; i++)
            {
                init3d[i] = read_info.init3d[i];
            }
            previewfactor   = read_info.previewfactor;
            xtrans          = read_info.xtrans;
            ytrans          = read_info.ytrans;
            red_crop_left   = read_info.red_crop_left;
            red_crop_right  = read_info.red_crop_right;
            blue_crop_left  = read_info.blue_crop_left;
            blue_crop_right = read_info.blue_crop_right;
            red_bright      = read_info.red_bright;
            blue_bright     = read_info.blue_bright;
            xadjust         = read_info.xadjust;
            g_eye_separation   = read_info.eyeseparation;
            g_glasses_type     = read_info.glassestype;
        }
    }

    if (read_info.version > 2)
    {
        save_release = 1300;
        outside      = read_info.outside;
    }

    calc_status = calc_status_value::PARAMS_CHANGED;       // defaults if version < 4
    xx3rd = xxmin;
    yy3rd = yymin;
    usr_distest = 0;
    calctime = 0;
    if (read_info.version > 3)
    {
        save_release = 1400;
        xx3rd       = read_info.x3rd;
        yy3rd       = read_info.y3rd;
        calc_status = static_cast<calc_status_value>(read_info.calc_status);
        usr_stdcalcmode = read_info.stdcalcmode;
        three_pass = false;
        if (usr_stdcalcmode == 127)
        {
            three_pass = true;
            usr_stdcalcmode = '3';
        }
        usr_distest     = read_info.distestold;
        usr_floatflag   = read_info.floatflag != 0;
        bailout     = read_info.bailoutold;
        calctime    = read_info.calctime;
        trigndx[0]  = static_cast<trig_fn>(read_info.trigndx[0]);
        trigndx[1]  = static_cast<trig_fn>(read_info.trigndx[1]);
        trigndx[2]  = static_cast<trig_fn>(read_info.trigndx[2]);
        trigndx[3]  = static_cast<trig_fn>(read_info.trigndx[3]);
        finattract  = read_info.finattract != 0;
        initorbit.x = read_info.initorbit[0];
        initorbit.y = read_info.initorbit[1];
        useinitorbit = read_info.useinitorbit;
        usr_periodicitycheck = read_info.periodicity;
    }

    pot16bit = false;
    save_system = 0;
    if (read_info.version > 4)
    {
        pot16bit = read_info.pot16bit != 0;
        if (pot16bit)
        {
            filexdots >>= 1;
        }
        fileaspectratio = read_info.faspectratio;
        if (fileaspectratio < 0.01)       // fix files produced in early v14.1
        {
            fileaspectratio = screenaspect;
        }
        save_system  = read_info.system;
        save_release = read_info.release; // from fmt 5 on we know real number
        if (read_info.version == 5        // except a few early fmt 5 cases:
                && (save_release <= 0 || save_release >= 4000))
        {
            save_release = 1410;
            save_system = 0;
        }
        if (!display3d && read_info.flag3d > 0)
        {
            loaded3d       = true;
            Ambient        = read_info.ambient;
            RANDOMIZE      = read_info.randomize;
            haze           = read_info.haze;
            transparent[0] = read_info.transparent[0];
            transparent[1] = read_info.transparent[1];
        }
    }

    rotate_lo = 1;
    rotate_hi = 255;
    distestwidth = 71;
    if (read_info.version > 5)
    {
        rotate_lo         = read_info.rotate_lo;
        rotate_hi         = read_info.rotate_hi;
        distestwidth      = read_info.distestwidth;
    }

    if (read_info.version > 6)
    {
        param[2]          = read_info.dparm3;
        param[3]          = read_info.dparm4;
    }

    if (read_info.version > 7)
    {
        fillcolor         = read_info.fillcolor;
    }

    if (read_info.version > 8)
    {
        mxmaxfp   =  read_info.mxmaxfp        ;
        mxminfp   =  read_info.mxminfp        ;
        mymaxfp   =  read_info.mymaxfp        ;
        myminfp   =  read_info.myminfp        ;
        zdots     =  read_info.zdots          ;
        originfp  =  read_info.originfp       ;
        depthfp   =  read_info.depthfp        ;
        heightfp  =  read_info.heightfp       ;
        widthfp   =  read_info.widthfp        ;
        distfp    =  read_info.distfp         ;
        eyesfp    =  read_info.eyesfp         ;
        neworbittype = static_cast<fractal_type>(read_info.orbittype);
        juli3Dmode   = read_info.juli3Dmode   ;
        maxfn    = (char)read_info.maxfn          ;
        major_method = static_cast<Major>(read_info.inversejulia >> 8);
        minor_method = static_cast<Minor>(read_info.inversejulia & 255);
        param[4] = read_info.dparm5;
        param[5] = read_info.dparm6;
        param[6] = read_info.dparm7;
        param[7] = read_info.dparm8;
        param[8] = read_info.dparm9;
        param[9] = read_info.dparm10;
    }

    if (read_info.version < 4 && read_info.version != 0) // pre-version 14.0?
    {
        backwardscompat(&read_info); // translate obsolete types
        if (LogFlag)
        {
            LogFlag = 2;
        }
        usr_floatflag = curfractalspecific->isinteger ? false : true;
    }

    if (read_info.version < 5 && read_info.version != 0) // pre-version 15.0?
    {
        if (LogFlag == 2) // logmap=old changed again in format 5!
        {
            LogFlag = -1;
        }
        if (decomp[0] > 0 && decomp[1] > 0)
        {
            bailout = decomp[1];
        }
    }
    if (potflag) // in version 15.x and 16.x logmap didn't work with pot
    {
        if (read_info.version == 6 || read_info.version == 7)
        {
            LogFlag = 0;
        }
    }
    set_trig_pointers(-1);

    if (read_info.version < 9 && read_info.version != 0) // pre-version 18.0?
    {
        /* forcesymmetry==1000 means we want to force symmetry but don't
            know which symmetry yet, will find out in setsymmetry() */
        if (outside == REAL || outside == IMAG || outside == MULT || outside == SUM
                || outside == ATAN)
        {
            if (forcesymmetry == symmetry_type::NOT_FORCED)
            {
                forcesymmetry = static_cast<symmetry_type>(1000);
            }
        }
    }
    if (save_release < 1725 && read_info.version != 0) // pre-version 17.25
    {
        set_if_old_bif(); // translate bifurcation types
        functionpreloaded = true;
    }

    if (read_info.version > 9)
    {   // post-version 18.22
        bailout     = read_info.bailout; // use long bailout
        bailoutest = static_cast<bailouts>(read_info.bailoutest);
    }
    else
    {
        bailoutest = bailouts::Mod;
    }
    setbailoutformula(bailoutest);

    if (read_info.version > 9)
    {
        // post-version 18.23
        maxit = read_info.iterations; // use long maxit
        // post-version 18.27
        old_demm_colors = read_info.old_demm_colors != 0;
    }

    if (read_info.version > 10) // post-version 19.20
    {
        LogFlag = read_info.logmap;
        usr_distest = read_info.distest;
    }

    if (read_info.version > 11) // post-version 19.20, inversion fix
    {
        inversion[0] = read_info.dinvert[0];
        inversion[1] = read_info.dinvert[1];
        inversion[2] = read_info.dinvert[2];
        Log_Fly_Calc = read_info.logcalc;
        stoppass     = read_info.stoppass;
    }

    if (read_info.version > 12) // post-version 19.60
    {
        quick_calc   = read_info.quick_calc != 0;
        closeprox    = read_info.closeprox;
        if (fractype == fractal_type::FPPOPCORN || fractype == fractal_type::LPOPCORN ||
                fractype == fractal_type::FPPOPCORNJUL || fractype == fractal_type::LPOPCORNJUL ||
                fractype == fractal_type::LATOO)
        {
            functionpreloaded = true;
        }
    }

    nobof = false;
    if (read_info.version > 13) // post-version 20.1.2
    {
        nobof = read_info.nobof != 0;
    }

    // if (read_info.version > 14)  post-version 20.1.12
    Log_Auto_Calc = false;              // make sure it's turned off

    orbit_interval = 1;
    if (read_info.version > 15) // post-version 20.3.2
    {
        orbit_interval = read_info.orbit_interval;
    }

    orbit_delay = 0;
    math_tol[0] = 0.05;
    math_tol[1] = 0.05;
    if (read_info.version > 16) // post-version 20.4.0
    {
        orbit_delay = read_info.orbit_delay;
        math_tol[0] = read_info.math_tol[0];
        math_tol[1] = read_info.math_tol[1];
    }

    backwards_v18();
    backwards_v19();
    backwards_v20();

    if (display3d)
    {
        usr_floatflag = oldfloatflag;
    }

    if (overlay3d)
    {
        g_init_mode = g_adapter;          // use previous adapter mode for overlays
        if (filexdots > xdots || fileydots > ydots)
        {
            stopmsg(STOPMSG_NONE, "Can't overlay with a larger image");
            g_init_mode = -1;
            return -1;
        }
    }
    else
    {
        int const olddisplay3d = display3d;
        bool const oldfloatflag = floatflag;
        display3d = loaded3d ? 1 : 0;   // for <tab> display during next
        floatflag = usr_floatflag; // ditto
        int i = get_video_mode(&read_info, &blk_3_info);
#if defined(_WIN32)
        _ASSERTE(_CrtCheckMemory());
#endif
        display3d = olddisplay3d;
        floatflag = oldfloatflag;
        if (i)
        {
            if (blk_2_info.got_data == 1)
            {
                MemoryRelease((U16) blk_2_info.resume_data);
                blk_2_info.length = 0;
            }
            g_init_mode = -1;
            return -1;
        }
    }

    if (display3d)
    {
        calc_status = calc_status_value::PARAMS_CHANGED;
        fractype = fractal_type::PLASMA;
        curfractalspecific = &fractalspecific[static_cast<int>(fractal_type::PLASMA)];
        param[0] = 0;
        if (!initbatch)
        {
            if (get_3d_params() < 0)
            {
                g_init_mode = -1;
                return -1;
            }
        }
    }

    if (resume_info != 0) // free the prior area if there is one
    {
        MemoryRelease(resume_info);
        resume_info = 0;
    }

    if (blk_2_info.got_data == 1)
    {
        resume_info = (U16) blk_2_info.resume_data;
        resume_len = blk_2_info.length;
    }

    if (blk_3_info.got_data == 1)
    {
        char *nameptr;
        switch (static_cast<fractal_type>(read_info.fractal_type))
        {
        case fractal_type::LSYSTEM:
            nameptr = LName;
            break;

        case fractal_type::IFS:
        case fractal_type::IFS3D:
            nameptr = IFSName;
            break;

        default:
            nameptr = FormName;
            uses_p1 = blk_3_info.uses_p1 != 0;
            uses_p2 = blk_3_info.uses_p2 != 0;
            uses_p3 = blk_3_info.uses_p3 != 0;
            uses_ismand = blk_3_info.uses_ismand != 0;
            ismand = blk_3_info.ismand != 0;
            uses_p4 = blk_3_info.uses_p4 != 0;
            uses_p5 = blk_3_info.uses_p5 != 0;
            break;
        }
        blk_3_info.form_name[ITEMNAMELEN] = 0;
        strcpy(nameptr, blk_3_info.form_name);
        // perhaps in future add more here, check block_len for backward compatibility
    }

    if (rangeslen) // free prior ranges
    {
        ranges.clear();
        rangeslen = 0;
    }

    if (blk_4_info.got_data == 1)
    {
        rangeslen = blk_4_info.length;
        ranges.resize(rangeslen);
        int const *range_data = blk_4_info.range_data;
        std::copy(range_data, range_data + rangeslen, &ranges[0]);
#ifdef XFRACT
        fix_ranges(&ranges[0], rangeslen, 1);
#endif
    }

    if (blk_5_info.got_data == 1)
    {
        bf_math = bf_math_type::BIGNUM;
        init_bf_length(read_info.bflength);
        memcpy((char *) bfxmin, blk_5_info.apm_data, blk_5_info.length);
        free(blk_5_info.apm_data);
    }
    else
    {
        bf_math = bf_math_type::NONE;
    }

    if (blk_6_info.got_data == 1)
    {
        EVOLUTION_INFO resume_e_info;
        GENEBASE gene[NUMGENES];

        // TODO: MemoryAlloc
        if (gene_handle == 0)
        {
            gene_handle = MemoryAlloc((U16) sizeof(gene), 1L, MEMORY);
        }
        MoveFromMemory((BYTE *)&gene, (U16) sizeof(gene), 1L, 0L, gene_handle);
        if (read_info.version < 15)
        {
            // Increasing NUMGENES moves ecount in the data structure
            // We added 4 to NUMGENES, so ecount is at NUMGENES-4
            blk_6_info.ecount = blk_6_info.mutate[NUMGENES - 4];
        }
        if (blk_6_info.ecount != blk_6_info.gridsz*blk_6_info.gridsz
                && calc_status != calc_status_value::COMPLETED)
        {
            calc_status = calc_status_value::RESUMABLE;
            // TODO: MemoryAlloc
            if (evolve_handle == 0)
            {
                evolve_handle = MemoryAlloc((U16) sizeof(resume_e_info), 1L, MEMORY);
            }
            resume_e_info.paramrangex  = blk_6_info.paramrangex;
            resume_e_info.paramrangey  = blk_6_info.paramrangey;
            resume_e_info.opx          = blk_6_info.opx;
            resume_e_info.opy          = blk_6_info.opy;
            resume_e_info.odpx         = blk_6_info.odpx;
            resume_e_info.odpy         = blk_6_info.odpy;
            resume_e_info.px           = blk_6_info.px;
            resume_e_info.py           = blk_6_info.py;
            resume_e_info.sxoffs       = blk_6_info.sxoffs;
            resume_e_info.syoffs       = blk_6_info.syoffs;
            resume_e_info.xdots        = blk_6_info.xdots;
            resume_e_info.ydots        = blk_6_info.ydots;
            resume_e_info.gridsz       = blk_6_info.gridsz;
            resume_e_info.evolving     = blk_6_info.evolving;
            resume_e_info.this_gen_rseed = blk_6_info.this_gen_rseed;
            resume_e_info.fiddlefactor = blk_6_info.fiddlefactor;
            resume_e_info.ecount       = blk_6_info.ecount;
            MoveToMemory((BYTE *) &resume_e_info, (U16) sizeof(resume_e_info), 1L, 0L, evolve_handle);
        }
        else
        {
            if (evolve_handle != 0)  // Image completed, release it.
            {
                MemoryRelease(evolve_handle);
            }
            evolve_handle = 0;
            calc_status = calc_status_value::COMPLETED;
        }
        paramrangex  = blk_6_info.paramrangex;
        paramrangey  = blk_6_info.paramrangey;
        newopx = blk_6_info.opx;
        opx = newopx;
        newopy = blk_6_info.opy;
        opy = newopy;
        newodpx = (char) blk_6_info.odpx;
        odpx = newodpx;
        newodpy = (char) blk_6_info.odpy;
        odpy = newodpy;
        px           = blk_6_info.px;
        py           = blk_6_info.py;
        sxoffs       = blk_6_info.sxoffs;
        syoffs       = blk_6_info.syoffs;
        xdots        = blk_6_info.xdots;
        ydots        = blk_6_info.ydots;
        gridsz       = blk_6_info.gridsz;
        this_gen_rseed = blk_6_info.this_gen_rseed;
        fiddlefactor   = blk_6_info.fiddlefactor;
        evolving = (int) blk_6_info.evolving;
        viewwindow = evolving != 0;
        dpx = paramrangex/(gridsz - 1);
        dpy = paramrangey/(gridsz - 1);
        if (read_info.version > 14)
        {
            for (int i = 0; i < NUMGENES; i++)
            {
                gene[i].mutate = static_cast<variations>(blk_6_info.mutate[i]);
            }
        }
        else
        {
            for (int i = 0; i < 6; i++)
            {
                gene[i].mutate = static_cast<variations>(blk_6_info.mutate[i]);
            }
            for (int i = 6; i < 10; i++)
            {
                gene[i].mutate = variations::NONE;
            }
            for (int i = 10; i < NUMGENES; i++)
            {
                gene[i].mutate = static_cast<variations>(blk_6_info.mutate[i-4]);
            }
        }
        MoveToMemory((BYTE *) &gene, (U16) sizeof(gene), 1L, 0L, gene_handle);
        param_history(0); // store history
    }
    else
    {
        evolving = 0;
    }

    if (blk_7_info.got_data == 1)
    {
        oxmin       = blk_7_info.oxmin;
        oxmax       = blk_7_info.oxmax;
        oymin       = blk_7_info.oymin;
        oymax       = blk_7_info.oymax;
        ox3rd       = blk_7_info.ox3rd;
        oy3rd       = blk_7_info.oy3rd;
        keep_scrn_coords = blk_7_info.keep_scrn_coords != 0;
        drawmode    = blk_7_info.drawmode;
        if (keep_scrn_coords)
        {
            set_orbit_corners = true;
        }
    }

    showfile = 0;                   // trigger the file load
    return 0;
}

static int find_fractal_info(char *gif_file, FRACTAL_INFO *info,
                             ext_blk_2 *blk_2_info,
                             ext_blk_3 *blk_3_info,
                             ext_blk_4 *blk_4_info,
                             ext_blk_5 *blk_5_info,
                             ext_blk_6 *blk_6_info,
                             ext_blk_7 *blk_7_info)
{
    BYTE gifstart[18];
    char temp1[81];
    int block_len;
    int data_len;
    int fractinf_len;
    int hdr_offset;
    formula_info fload_info;
    EVOLUTION_INFO eload_info;
    ORBITS_INFO oload_info;

    blk_2_info->got_data = 0; // initialize to no data
    blk_3_info->got_data = 0; // initialize to no data
    blk_4_info->got_data = 0; // initialize to no data
    blk_5_info->got_data = 0; // initialize to no data
    blk_6_info->got_data = 0; // initialize to no data
    blk_7_info->got_data = 0; // initialize to no data

    fp = fopen(gif_file, "rb");
    if (fp == nullptr)
        return (-1);
    fread(gifstart, 13, 1, fp);
    if (strncmp((char *)gifstart, "GIF", 3) != 0)
    { // not GIF, maybe old .tga?
        fclose(fp);
        return (-1);
    }

    filetype = 0; // GIF
    GET16(gifstart[6], filexdots);
    GET16(gifstart[8], fileydots);
    filecolors = 2 << (gifstart[10] & 7);
    fileaspectratio = 0; // unknown
    if (gifstart[12])
    { // calc reasonably close value from gif header
        fileaspectratio = (float)((64.0 / ((double)(gifstart[12]) + 15.0))
                                  * (double)fileydots / (double)filexdots);
        if (fileaspectratio > screenaspect-0.03
                && fileaspectratio < screenaspect+0.03)
            fileaspectratio = screenaspect;
    }
    else if (fileydots * 4 == filexdots * 3) // assume the common square pixels
        fileaspectratio = screenaspect;

    if (*s_makepar == 0 && (gifstart[10] & 0x80) != 0)
    {
        for (int i = 0; i < filecolors; i++)
        {
            int k = 0;
            for (int j = 0; j < 3; j++)
            {
                k = getc(fp);
                if (k < 0)
                    break;
                g_dac_box[i][j] = (BYTE)(k >> 2);
            }
            if (k < 0)
                break;
        }
    }

    /* Format of .gif extension blocks is:
           1 byte    '!', extension block identifier
           1 byte    extension block number, 255
           1 byte    length of id, 11
          11 bytes   alpha id, "fractintnnn" with fractint, nnn is secondary id
        n * {
           1 byte    length of block info in bytes
           x bytes   block info
            }
           1 byte    0, extension terminator
       To scan extension blocks, we first look in file at length of FRACTAL_INFO
       (the main extension block) from end of file, looking for a literal known
       to be at start of our block info.  Then we scan forward a bit, in case
       the file is from an earlier fractint vsn with shorter FRACTAL_INFO.
       If FRACTAL_INFO is found and is from vsn>=14, it includes the total length
       of all extension blocks; we then scan them all first to last to load
       any optional ones which are present.
       Defined extension blocks:
         fractint001     header, always present
         fractint002     resume info for interrupted resumable image
         fractint003     additional formula type info
         fractint004     ranges info
         fractint005     extended precision parameters
         fractint006     evolver params
    */

    memset(info, 0, FRACTAL_INFO_SIZE);
    fractinf_len = FRACTAL_INFO_SIZE + (FRACTAL_INFO_SIZE+254)/255;
    fseek(fp, (long)(-1-fractinf_len), SEEK_END);
    /* TODO: revise this to read members one at a time so we get natural alignment
       of fields within the FRACTAL_INFO structure for the platform */
    fread(info, 1, FRACTAL_INFO_SIZE, fp);
    if (strcmp(INFO_ID, info->info_id) == 0)
    {
#ifdef XFRACT
        decode_fractal_info(info, 1);
#endif
        hdr_offset = -1-fractinf_len;
    }
    else
    {
        // didn't work 1st try, maybe an older vsn, maybe junk at eof, scan:
        int offset;
        char tmpbuf[110];
        hdr_offset = 0;
        offset = 80; // don't even check last 80 bytes of file for id
        while (offset < fractinf_len+513)
        { // allow 512 garbage at eof
            offset += 100; // go back 100 bytes at a time
            fseek(fp, (long)(0-offset), SEEK_END);
            fread(tmpbuf, 1, 110, fp); // read 10 extra for string compare
            for (int i = 0; i < 100; ++i)
                if (!strcmp(INFO_ID, &tmpbuf[i]))
                { // found header?
                    strcpy(info->info_id, INFO_ID);
                    fseek(fp, (long)(hdr_offset = i-offset), SEEK_END);
                    /* TODO: revise this to read members one at a time so we get natural alignment
                        of fields within the FRACTAL_INFO structure for the platform */
                    fread(info, 1, FRACTAL_INFO_SIZE, fp);
#ifdef XFRACT
                    decode_fractal_info(info, 1);
#endif
                    offset = 10000; // force exit from outer loop
                    break;
                }
        }
    }

    if (hdr_offset)
    { // we found INFO_ID

        if (info->version >= 4)
        {
            /* first reload main extension block, reasons:
                 might be over 255 chars, and thus earlier load might be bad
                 find exact endpoint, so scan back to start of ext blks works
               */
            fseek(fp, (long)(hdr_offset-15), SEEK_END);
            int scan_extend = 1;
            while (scan_extend)
            {
                if (fgetc(fp) != '!' // if not what we expect just give up
                        || fread(temp1, 1, 13, fp) != 13
                        || strncmp(&temp1[2], "fractint", 8))
                    break;
                temp1[13] = 0;
                switch (atoi(&temp1[10]))   // e.g. "fractint002"
                {
                case 1: // "fractint001", the main extension block
                    if (scan_extend == 2)
                    { // we've been here before, done now
                        scan_extend = 0;
                        break;
                    }
                    load_ext_blk((char *)info, FRACTAL_INFO_SIZE);
#ifdef XFRACT
                    decode_fractal_info(info, 1);
#endif
                    scan_extend = 2;
                    // now we know total extension len, back up to first block
                    fseek(fp, 0L-info->tot_extend_len, SEEK_CUR);
                    break;
                case 2: // resume info
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    // TODO: MemoryAlloc
                    blk_2_info->resume_data = MemoryAlloc((U16)1, (long)data_len, MEMORY);
                    if (blk_2_info->resume_data == 0)
                        info->calc_status = static_cast<short>(calc_status_value::NON_RESUMABLE); // not resumable after all
                    else
                    {
                        fseek(fp, (long)(0-block_len), SEEK_CUR);
                        load_ext_blk((char *)block, data_len);
                        MoveToMemory((BYTE *)block, (U16)1, (long)data_len, 0, (U16)blk_2_info->resume_data);
                        blk_2_info->length = data_len;
                        blk_2_info->got_data = 1; // got data
                    }
                    break;
                case 3: // formula info
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    // check data_len for backward compatibility
                    fseek(fp, (long)(0-block_len), SEEK_CUR);
                    load_ext_blk((char *)&fload_info, data_len);
                    strcpy(blk_3_info->form_name, fload_info.form_name);
                    blk_3_info->length = data_len;
                    blk_3_info->got_data = 1; // got data
                    if (data_len < sizeof(fload_info))
                    { // must be old GIF
                        blk_3_info->uses_p1 = 1;
                        blk_3_info->uses_p2 = 1;
                        blk_3_info->uses_p3 = 1;
                        blk_3_info->uses_ismand = 0;
                        blk_3_info->ismand = 1;
                        blk_3_info->uses_p4 = 0;
                        blk_3_info->uses_p5 = 0;
                    }
                    else
                    {
                        blk_3_info->uses_p1 = fload_info.uses_p1;
                        blk_3_info->uses_p2 = fload_info.uses_p2;
                        blk_3_info->uses_p3 = fload_info.uses_p3;
                        blk_3_info->uses_ismand = fload_info.uses_ismand;
                        blk_3_info->ismand = fload_info.ismand;
                        blk_3_info->uses_p4 = fload_info.uses_p4;
                        blk_3_info->uses_p5 = fload_info.uses_p5;
                    }
                    break;
                case 4: // ranges info
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    blk_4_info->range_data = (int *)malloc((long)data_len);
                    if (blk_4_info->range_data != nullptr)
                    {
                        fseek(fp, (long)(0-block_len), SEEK_CUR);
                        load_ext_blk((char *)blk_4_info->range_data, data_len);
                        blk_4_info->length = data_len/2;
                        blk_4_info->got_data = 1; // got data
                    }
                    break;
                case 5: // extended precision parameters
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    blk_5_info->apm_data = (char *)malloc((long)data_len);
                    if (blk_5_info->apm_data != nullptr)
                    {
                        fseek(fp, (long)(0-block_len), SEEK_CUR);
                        load_ext_blk(blk_5_info->apm_data, data_len);
                        blk_5_info->length = data_len;
                        blk_5_info->got_data = 1; // got data
                    }
                    break;
                case 6: // evolver params
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    fseek(fp, (long)(0-block_len), SEEK_CUR);
                    load_ext_blk((char *)&eload_info, data_len);
                    // XFRACT processing of doubles here
#ifdef XFRACT
                    decode_evolver_info(&eload_info, 1);
#endif
                    blk_6_info->length = data_len;
                    blk_6_info->got_data = 1; // got data

                    blk_6_info->paramrangex     = eload_info.paramrangex;
                    blk_6_info->paramrangey     = eload_info.paramrangey;
                    blk_6_info->opx             = eload_info.opx;
                    blk_6_info->opy             = eload_info.opy;
                    blk_6_info->odpx            = (char)eload_info.odpx;
                    blk_6_info->odpy            = (char)eload_info.odpy;
                    blk_6_info->px              = eload_info.px;
                    blk_6_info->py              = eload_info.py;
                    blk_6_info->sxoffs          = eload_info.sxoffs;
                    blk_6_info->syoffs          = eload_info.syoffs;
                    blk_6_info->xdots           = eload_info.xdots;
                    blk_6_info->ydots           = eload_info.ydots;
                    blk_6_info->gridsz          = eload_info.gridsz;
                    blk_6_info->evolving        = eload_info.evolving;
                    blk_6_info->this_gen_rseed  = eload_info.this_gen_rseed;
                    blk_6_info->fiddlefactor    = eload_info.fiddlefactor;
                    blk_6_info->ecount          = eload_info.ecount;
                    for (int i = 0; i < NUMGENES; i++)
                        blk_6_info->mutate[i]    = eload_info.mutate[i];
                    break;
                case 7: // orbits parameters
                    skip_ext_blk(&block_len, &data_len); // once to get lengths
                    fseek(fp, (long)(0-block_len), SEEK_CUR);
                    load_ext_blk((char *)&oload_info, data_len);
                    // XFRACT processing of doubles here
#ifdef XFRACT
                    decode_orbits_info(&oload_info, 1);
#endif
                    blk_7_info->length = data_len;
                    blk_7_info->got_data = 1; // got data
                    blk_7_info->oxmin           = oload_info.oxmin;
                    blk_7_info->oxmax           = oload_info.oxmax;
                    blk_7_info->oymin           = oload_info.oymin;
                    blk_7_info->oymax           = oload_info.oymax;
                    blk_7_info->ox3rd           = oload_info.ox3rd;
                    blk_7_info->oy3rd           = oload_info.oy3rd;
                    blk_7_info->keep_scrn_coords= oload_info.keep_scrn_coords;
                    blk_7_info->drawmode        = oload_info.drawmode;
                    break;
                default:
                    skip_ext_blk(&block_len, &data_len);
                }
            }
        }

        fclose(fp);
        fileaspectratio = screenaspect; // if not >= v15, this is correct
        return (0);
    }

    strcpy(info->info_id, "GIFFILE");
    info->iterations = 150;
    info->iterationsold = 150;
    info->fractal_type = static_cast<short>(fractal_type::PLASMA);
    info->xmin = -1;
    info->xmax = 1;
    info->ymin = -1;
    info->ymax = 1;
    info->x3rd = -1;
    info->y3rd = -1;
    info->creal = 0;
    info->cimag = 0;
    info->videomodeax = 255;
    info->videomodebx = 255;
    info->videomodecx = 255;
    info->videomodedx = 255;
    info->dotmode = 0;
    info->xdots = (short)filexdots;
    info->ydots = (short)fileydots;
    info->colors = (short)filecolors;
    info->version = 0; // this forces lots more init at calling end too

    // zero means we won
    fclose(fp);
    return (0);
}

static void load_ext_blk(char *loadptr, int loadlen)
{
    int len;
    while ((len = fgetc(fp)) > 0)
    {
        while (--len >= 0)
        {
            if (--loadlen >= 0)
                *(loadptr++) = (char)fgetc(fp);
            else
                fgetc(fp); // discard excess characters
        }
    }
}

static void skip_ext_blk(int *block_len, int *data_len)
{
    int len;
    *data_len = 0;
    *block_len = 1;
    while ((len = fgetc(fp)) > 0)
    {
        fseek(fp, (long)len, SEEK_CUR);
        *data_len += len;
        *block_len += len + 1;
    }
}


// switch obsolete fractal types to new generalizations
static void backwardscompat(FRACTAL_INFO *info)
{
    switch (fractype)
    {
    case fractal_type::LAMBDASINE:
        fractype = fractal_type::LAMBDATRIGFP;
        trigndx[0] = trig_fn::SIN;
        break;
    case fractal_type::LAMBDACOS:
        fractype = fractal_type::LAMBDATRIGFP;
        trigndx[0] = trig_fn::COSXX;
        break;
    case fractal_type::LAMBDAEXP:
        fractype = fractal_type::LAMBDATRIGFP;
        trigndx[0] = trig_fn::EXP;
        break;
    case fractal_type::MANDELSINE:
        fractype = fractal_type::MANDELTRIGFP;
        trigndx[0] = trig_fn::SIN;
        break;
    case fractal_type::MANDELCOS:
        fractype = fractal_type::MANDELTRIGFP;
        trigndx[0] = trig_fn::COSXX;
        break;
    case fractal_type::MANDELEXP:
        fractype = fractal_type::MANDELTRIGFP;
        trigndx[0] = trig_fn::EXP;
        break;
    case fractal_type::MANDELSINH:
        fractype = fractal_type::MANDELTRIGFP;
        trigndx[0] = trig_fn::SINH;
        break;
    case fractal_type::LAMBDASINH:
        fractype = fractal_type::LAMBDATRIGFP;
        trigndx[0] = trig_fn::SINH;
        break;
    case fractal_type::MANDELCOSH:
        fractype = fractal_type::MANDELTRIGFP;
        trigndx[0] = trig_fn::COSH;
        break;
    case fractal_type::LAMBDACOSH:
        fractype = fractal_type::LAMBDATRIGFP;
        trigndx[0] = trig_fn::COSH;
        break;
    case fractal_type::LMANDELSINE:
        fractype = fractal_type::MANDELTRIG;
        trigndx[0] = trig_fn::SIN;
        break;
    case fractal_type::LLAMBDASINE:
        fractype = fractal_type::LAMBDATRIG;
        trigndx[0] = trig_fn::SIN;
        break;
    case fractal_type::LMANDELCOS:
        fractype = fractal_type::MANDELTRIG;
        trigndx[0] = trig_fn::COSXX;
        break;
    case fractal_type::LLAMBDACOS:
        fractype = fractal_type::LAMBDATRIG;
        trigndx[0] = trig_fn::COSXX;
        break;
    case fractal_type::LMANDELSINH:
        fractype = fractal_type::MANDELTRIG;
        trigndx[0] = trig_fn::SINH;
        break;
    case fractal_type::LLAMBDASINH:
        fractype = fractal_type::LAMBDATRIG;
        trigndx[0] = trig_fn::SINH;
        break;
    case fractal_type::LMANDELCOSH:
        fractype = fractal_type::MANDELTRIG;
        trigndx[0] = trig_fn::COSH;
        break;
    case fractal_type::LLAMBDACOSH:
        fractype = fractal_type::LAMBDATRIG;
        trigndx[0] = trig_fn::COSH;
        break;
    case fractal_type::LMANDELEXP:
        fractype = fractal_type::MANDELTRIG;
        trigndx[0] = trig_fn::EXP;
        break;
    case fractal_type::LLAMBDAEXP:
        fractype = fractal_type::LAMBDATRIG;
        trigndx[0] = trig_fn::EXP;
        break;
    case fractal_type::DEMM:
        fractype = fractal_type::MANDELFP;
        usr_distest = (info->ydots - 1) * 2;
        break;
    case fractal_type::DEMJ:
        fractype = fractal_type::JULIAFP;
        usr_distest = (info->ydots - 1) * 2;
        break;
    case fractal_type::MANDELLAMBDA:
        useinitorbit = 2;
        break;
    default:
        break;
    }
    curfractalspecific = &fractalspecific[static_cast<int>(fractype)];
}

// switch old bifurcation fractal types to new generalizations
void set_if_old_bif()
{
    /* set functions if not set already, may need to check 'functionpreloaded'
       before calling this routine. */

    switch (fractype)
    {
    case fractal_type::BIFURCATION:
    case fractal_type::LBIFURCATION:
    case fractal_type::BIFSTEWART:
    case fractal_type::LBIFSTEWART:
    case fractal_type::BIFLAMBDA:
    case fractal_type::LBIFLAMBDA:
        set_trig_array(0, "ident");
        break;

    case fractal_type::BIFEQSINPI:
    case fractal_type::LBIFEQSINPI:
    case fractal_type::BIFADSINPI:
    case fractal_type::LBIFADSINPI:
        set_trig_array(0, "sin");
        break;

    default:
        break;
    }
}

// miscellaneous function variable defaults
void set_function_parm_defaults()
{
    switch (fractype)
    {
    case fractal_type::FPPOPCORN:
    case fractal_type::LPOPCORN:
    case fractal_type::FPPOPCORNJUL:
    case fractal_type::LPOPCORNJUL:
        set_trig_array(0, "sin");
        set_trig_array(1, "tan");
        set_trig_array(2, "sin");
        set_trig_array(3, "tan");
        break;

    case fractal_type::LATOO:
        set_trig_array(0, "sin");
        set_trig_array(1, "sin");
        set_trig_array(2, "sin");
        set_trig_array(3, "sin");
        break;

    default:
        break;
    }
}

void backwards_v18()
{
    if (!functionpreloaded)
        set_if_old_bif(); // old bifs need function set
    if (fractype == fractal_type::MANDELTRIG && usr_floatflag
            && save_release < 1800 && bailout == 0)
        bailout = 2500;
    if (fractype == fractal_type::LAMBDATRIG && usr_floatflag
            && save_release < 1800 && bailout == 0)
        bailout = 2500;
}

void backwards_v19()
{
    if (fractype == fractal_type::MARKSJULIA && save_release < 1825)
    {
        if (param[2] == 0)
            param[2] = 2;
        else
            param[2] += 1;
    }
    if (fractype == fractal_type::MARKSJULIAFP && save_release < 1825)
    {
        if (param[2] == 0)
            param[2] = 2;
        else
            param[2] += 1;
    }
    if ((fractype == fractal_type::FORMULA || fractype == fractal_type::FFORMULA) && save_release < 1824)
    {
        invert = 0;
        inversion[2] = invert;
        inversion[1] = inversion[2];
        inversion[0] = inversion[1];
    }
    if (fix_bof())
        no_mag_calc = true; // fractal has old bof60/61 problem with magnitude
    else
        no_mag_calc = false;
    if (fix_period_bof())
        use_old_period = true; // fractal uses old periodicity method
    else
        use_old_period = false;
    if (save_release < 1827 && distest)
        use_old_distest = true;         // use old distest code
    else
        use_old_distest = false;        // use new distest code
}

void backwards_v20()
{   // Fractype == FP type is not seen from PAR file ?????
    if ((fractype == fractal_type::MANDELFP || fractype == fractal_type::JULIAFP ||
            fractype == fractal_type::MANDEL || fractype == fractal_type::JULIA) &&
            (outside <= REAL && outside >= SUM) && save_release <= 1960)
        bad_outside = true;
    else
        bad_outside = false;
    if ((fractype == fractal_type::FORMULA || fractype == fractal_type::FFORMULA) &&
            (save_release < 1900 || debugflag == debug_flags::force_ld_check))
        ldcheck = true;
    else
        ldcheck = false;
    if (inside == EPSCROSS && save_release < 1961)
        closeprox = 0.01;
    if (!functionpreloaded)
        set_function_parm_defaults();
}

bool check_back()
{
    /*
       put the features that need to save the value in save_release for backwards
       compatibility in this routine
    */
    bool ret = false;
    if (fractype == fractal_type::LYAPUNOV ||
            fractype == fractal_type::FROTH || fractype == fractal_type::FROTHFP ||
            fix_bof() || fix_period_bof() || use_old_distest || decomp[0] == 2 ||
            (fractype == fractal_type::FORMULA && save_release <= 1920) ||
            (fractype == fractal_type::FFORMULA && save_release <= 1920) ||
            (LogFlag != 0 && save_release <= 2001) ||
            (fractype == fractal_type::TRIGSQR && save_release < 1900) ||
            (inside == STARTRAIL && save_release < 1825) ||
            (maxit > 32767 && save_release <= 1950) ||
            (distest && save_release <=1950) ||
            ((outside <= REAL && outside >= ATAN) &&
             save_release <= 1960) ||
            (fractype == fractal_type::FPPOPCORN && save_release <= 1960) ||
            (fractype == fractal_type::LPOPCORN && save_release <= 1960) ||
            (fractype == fractal_type::FPPOPCORNJUL && save_release <= 1960) ||
            (fractype == fractal_type::LPOPCORNJUL && save_release <= 1960) ||
            (inside == FMODI && save_release <= 2000) ||
            ((inside == ATANI || outside == ATAN) && save_release <= 2002) ||
            (fractype == fractal_type::LAMBDATRIGFP && trigndx[0] == trig_fn::EXP && save_release <= 2002) ||
            ((fractype == fractal_type::JULIBROT || fractype == fractal_type::JULIBROTFP) &&
             (neworbittype == fractal_type::QUATFP || neworbittype == fractal_type::HYPERCMPLXFP) &&
             save_release <= 2002)
       )
        ret = true;
    return ret;
}

static bool fix_bof()
{
    bool ret = false;
    if (inside <= BOF60 && inside >= BOF61 && save_release < 1826)
        if ((curfractalspecific->calctype == StandardFractal &&
                (curfractalspecific->flags & BAILTEST) == 0) ||
                (fractype == fractal_type::FORMULA || fractype == fractal_type::FFORMULA))
            ret = true;
    return ret;
}

static bool fix_period_bof()
{
    bool ret = false;
    if (inside <= BOF60 && inside >= BOF61 && save_release < 1826)
        ret = true;
    return ret;
}

// browse code RB

#define MAX_WINDOWS_OPEN 450

struct window
{                       // for fgetwindow on screen browser
    coords itl;  // screen coordinates
    coords ibl;
    coords itr;
    coords ibr;
    double win_size;   // box size for drawindow()
    char name[13];     // for filename
    int boxcount;      // bytes of saved screen info
};

// prototypes
static void drawindow(int, window *);
static bool is_visible_window(window *, FRACTAL_INFO *, ext_blk_5 *);
static void transform(dblcoords *);
static bool paramsOK(FRACTAL_INFO *);
static bool typeOK(FRACTAL_INFO *, ext_blk_3 *);
static bool functionOK(FRACTAL_INFO *, int);
static void check_history(char *, char *);
static void bfsetup_convert_to_screen();
static void bftransform(bf_t, bf_t, dblcoords *);

char browsename[13]; // name for browse file
U16 browsehandle;
U16 boxxhandle;
U16 boxyhandle;
U16 boxvalueshandle;

// here because must be visible inside several routines
static affine *cvt;
static bf_t   bt_a, bt_b, bt_c, bt_d, bt_e, bt_f;
static bf_t   n_a, n_b, n_c, n_d, n_e, n_f;
bf_math_type oldbf_math;

// fgetwindow reads all .GIF files and draws window outlines on the screen
int fgetwindow()
{
    affine stack_cvt;
    FRACTAL_INFO read_info;
    ext_blk_2 blk_2_info;
    ext_blk_3 blk_3_info;
    ext_blk_4 blk_4_info;
    ext_blk_5 blk_5_info;
    ext_blk_6 blk_6_info;
    ext_blk_7 blk_7_info;
    time_t thistime, lastime;
    char mesg[40];
    char newname[60];
    char oldname[60];
    int c;
    int done;
    int wincount;
    int toggle;
    int color_of_box;
    window winlist;
    char drive[FILE_MAX_DRIVE];
    char dir[FILE_MAX_DIR];
    char fname[FILE_MAX_FNAME];
    char ext[FILE_MAX_EXT];
    char tmpmask[FILE_MAX_PATH];
    int vid_too_big = 0;
    bool no_memory = false;
    U16 vidlength;
    BYTE *winlistptr = (BYTE *)&winlist;
    int saved;

    oldbf_math = bf_math;
    bf_math = bf_math_type::BIGFLT;
    if (oldbf_math == bf_math_type::NONE)
    {
        calc_status_value oldcalc_status = calc_status; // kludge because next sets it = 0
        fractal_floattobf();
        calc_status = oldcalc_status;
    }
    saved = save_stack();
    bt_a = alloc_stack(rbflength+2);
    bt_b = alloc_stack(rbflength+2);
    bt_c = alloc_stack(rbflength+2);
    bt_d = alloc_stack(rbflength+2);
    bt_e = alloc_stack(rbflength+2);
    bt_f = alloc_stack(rbflength+2);

    vidlength = (U16)(sxdots + sydots);
    if (vidlength > (U16)4096)
        vid_too_big = 2;
    // 4096 based on 4096B in boxx... max 1/4 pixels plotted, and need words
    // 4096 = 10240/2.5 based on size of boxx+boxy+boxvalues
#ifdef XFRACT
    vidlength = 4; // Xfractint only needs the 4 corners saved.
#endif
    // TODO: MemoryAlloc
    browsehandle = MemoryAlloc((U16)sizeof(window), (long)MAX_WINDOWS_OPEN, MEMORY);
    boxxhandle = MemoryAlloc((U16)(vidlength), (long)MAX_WINDOWS_OPEN, MEMORY);
    boxyhandle = MemoryAlloc((U16)(vidlength), (long)MAX_WINDOWS_OPEN, MEMORY);
    boxvalueshandle = MemoryAlloc((U16)(vidlength >> 1), (long)MAX_WINDOWS_OPEN, MEMORY);
    if (!browsehandle || !boxxhandle || !boxyhandle || !boxvalueshandle)
        no_memory = true;

    // set up complex-plane-to-screen transformation
    if (oldbf_math != bf_math_type::NONE)
    {
        bfsetup_convert_to_screen();
    }
    else
    {
        cvt = &stack_cvt; // use stack
        setup_convert_to_screen(cvt);
        // put in bf variables
        floattobf(bt_a, cvt->a);
        floattobf(bt_b, cvt->b);
        floattobf(bt_c, cvt->c);
        floattobf(bt_d, cvt->d);
        floattobf(bt_e, cvt->e);
        floattobf(bt_f, cvt->f);
    }
    find_special_colors();
    color_of_box = g_color_medium;
rescan:  // entry for changed browse parms
    time(&lastime);
    toggle = 0;
    wincount = 0;
    no_sub_images = false;
    splitpath(readname, drive, dir, nullptr, nullptr);
    splitpath(browsemask, nullptr, nullptr, fname, ext);
    makepath(tmpmask, drive, dir, fname, ext);
    done = (vid_too_big == 2) || no_memory || fr_findfirst(tmpmask);
    // draw all visible windows
    while (!done)
    {
        if (driver_key_pressed())
        {
            driver_get_key();
            break;
        }
        splitpath(DTA.filename, nullptr, nullptr, fname, ext);
        makepath(tmpmask, drive, dir, fname, ext);
        if (!find_fractal_info(tmpmask, &read_info, &blk_2_info, &blk_3_info,
                               &blk_4_info, &blk_5_info, &blk_6_info,
                               &blk_7_info) &&
                (typeOK(&read_info, &blk_3_info) || !brwschecktype) &&
                (paramsOK(&read_info) || !brwscheckparms) &&
                stricmp(browsename, DTA.filename) &&
                blk_6_info.got_data != 1 &&
                is_visible_window(&winlist, &read_info, &blk_5_info)
           )
        {
            strcpy(winlist.name, DTA.filename);
            drawindow(color_of_box, &winlist);
            boxcount *= 2; // double for byte count
            winlist.boxcount = boxcount;
            MoveToMemory(winlistptr, (U16)sizeof(window), 1L, (long)wincount, browsehandle);
            MoveToMemory((BYTE *)boxx, vidlength, 1L, (long)wincount, boxxhandle);
            MoveToMemory((BYTE *)boxy, vidlength, 1L, (long)wincount, boxyhandle);
            MoveToMemory((BYTE *)boxvalues, (U16)(vidlength >> 1), 1L, (long)wincount, boxvalueshandle);
            wincount++;
        }

        if (blk_2_info.got_data == 1) // Clean up any memory allocated
            MemoryRelease((U16)blk_2_info.resume_data);
        if (blk_4_info.got_data == 1) // Clean up any memory allocated
            free(blk_4_info.range_data);
        if (blk_5_info.got_data == 1) // Clean up any memory allocated
            free(blk_5_info.apm_data);

        done = (fr_findnext() || wincount >= MAX_WINDOWS_OPEN);
    }

    if (no_memory)
    {
        texttempmsg("Sorry...not enough memory to browse.");// doesn't work if NO memory available, go figure
    }
    if (wincount >= MAX_WINDOWS_OPEN)
    {   // hard code message at MAX_WINDOWS_OPEN = 450
        texttempmsg("Sorry...no more space, 450 displayed.");
    }
    if (vid_too_big == 2)
    {
        texttempmsg("Xdots + Ydots > 4096.");
    }
    c = 0;
    if (wincount)
    {
        driver_buzzer(buzzer_codes::COMPLETE); //let user know we've finished
        int index = 0;
        done = 0;
        MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)index, browsehandle);
        MoveFromMemory((BYTE *)boxx, vidlength, 1L, (long)index, boxxhandle);
        MoveFromMemory((BYTE *)boxy, vidlength, 1L, (long)index, boxyhandle);
        MoveFromMemory((BYTE *)boxvalues, (U16)(vidlength >> 1), 1L, (long)index, boxvalueshandle);
        showtempmsg(winlist.name);
        while (!done)  /* on exit done = 1 for quick exit,
                                 done = 2 for erase boxes and  exit
                                 done = 3 for rescan
                                 done = 4 for set boxes and exit to save image */
        {
#ifdef XFRACT
            U32 blinks = 1;
#endif
            while (!driver_key_pressed())
            {
                time(&thistime);
                if (difftime(thistime, lastime) > .2)
                {
                    lastime = thistime;
                    toggle = 1- toggle;
                }
                if (toggle)
                    drawindow(g_color_bright, &winlist);   // flash current window
                else
                    drawindow(g_color_dark, &winlist);
#ifdef XFRACT
                blinks++;
#endif
            }
#ifdef XFRACT
            if ((blinks & 1) == 1)   // Need an odd # of blinks, so next one leaves box turned off
                drawindow(g_color_bright, &winlist);
#endif

            c = driver_get_key();
            switch (c)
            {
            case FIK_RIGHT_ARROW:
            case FIK_LEFT_ARROW:
            case FIK_DOWN_ARROW:
            case FIK_UP_ARROW:
                cleartempmsg();
                drawindow(color_of_box, &winlist);// dim last window
                if (c == FIK_RIGHT_ARROW || c == FIK_UP_ARROW)
                {
                    index++;                     // shift attention to next window
                    if (index >= wincount)
                        index = 0;
                }
                else
                {
                    index -- ;
                    if (index < 0)
                        index = wincount -1 ;
                }
                MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)index, browsehandle);
                MoveFromMemory((BYTE *)boxx, vidlength, 1L, (long)index, boxxhandle);
                MoveFromMemory((BYTE *)boxy, vidlength, 1L, (long)index, boxyhandle);
                MoveFromMemory((BYTE *)boxvalues, (U16)(vidlength >> 1), 1L, (long)index, boxvalueshandle);
                showtempmsg(winlist.name);
                break;
#ifndef XFRACT
            case FIK_CTL_INSERT:
                color_of_box += key_count(FIK_CTL_INSERT);
                for (int i = 0; i < wincount ; i++)
                {
                    MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)i, browsehandle);
                    drawindow(color_of_box, &winlist);
                }
                MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)index, browsehandle);
                drawindow(color_of_box, &winlist);
                break;

            case FIK_CTL_DEL:
                color_of_box -= key_count(FIK_CTL_DEL);
                for (int i = 0; i < wincount ; i++)
                {
                    MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)i, browsehandle);
                    drawindow(color_of_box, &winlist);
                }
                MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)index, browsehandle);
                drawindow(color_of_box, &winlist);
                break;
#endif
            case FIK_ENTER:
            case FIK_ENTER_2:   // this file please
                strcpy(browsename, winlist.name);
                done = 1;
                break;

            case FIK_ESC:
            case 'l':
            case 'L':
#ifdef XFRACT
                // Need all boxes turned on, turn last one back on.
                drawindow(g_color_bright, &winlist);
#endif
                autobrowse = false;
                done = 2;
                break;

            case 'D': // delete file
                cleartempmsg();
                _snprintf(mesg, NUM_OF(mesg), "Delete %s? (Y/N)", winlist.name);
                showtempmsg(mesg);
                driver_wait_key_pressed(0);
                cleartempmsg();
                c = driver_get_key();
                if (c == 'Y' && doublecaution)
                {
                    texttempmsg("ARE YOU SURE???? (Y/N)");
                    if (driver_get_key() != 'Y')
                        c = 'N';
                }
                if (c == 'Y')
                {
                    splitpath(readname, drive, dir, nullptr, nullptr);
                    splitpath(winlist.name, nullptr, nullptr, fname, ext);
                    makepath(tmpmask, drive, dir, fname, ext);
                    if (!unlink(tmpmask))
                    {
                        // do a rescan
                        done = 3;
                        strcpy(oldname, winlist.name);
                        tmpmask[0] = '\0';
                        check_history(oldname, tmpmask);
                        break;
                    }
                    else if (errno == EACCES)
                    {
                        texttempmsg("Sorry...it's a read only file, can't del");
                        showtempmsg(winlist.name);
                        break;
                    }
                }
                {
                    texttempmsg("file not deleted (phew!)");
                }
                showtempmsg(winlist.name);
                break;

            case 'R':
                cleartempmsg();
                driver_stack_screen();
                newname[0] = 0;
                strcpy(mesg, "Enter the new filename for ");
                splitpath(readname, drive, dir, nullptr, nullptr);
                splitpath(winlist.name, nullptr, nullptr, fname, ext);
                makepath(tmpmask, drive, dir, fname, ext);
                strcpy(newname, tmpmask);
                strcat(mesg, tmpmask);
                {
                    int i = field_prompt(mesg, nullptr, newname, 60, nullptr);
                    driver_unstack_screen();
                    if (i != -1)
                        if (!rename(tmpmask, newname))
                        {
                            if (errno == EACCES)
                            {
                                texttempmsg("Sorry....can't rename");
                            }
                            else
                            {
                                splitpath(newname, nullptr, nullptr, fname, ext);
                                makepath(tmpmask, nullptr, nullptr, fname, ext);
                                strcpy(oldname, winlist.name);
                                check_history(oldname, tmpmask);
                                strcpy(winlist.name, tmpmask);
                            }
                        }
                    MoveToMemory(winlistptr, (U16)sizeof(window), 1L, (long)index, browsehandle);
                    showtempmsg(winlist.name);
                }
                break;

            case FIK_CTL_B:
                cleartempmsg();
                driver_stack_screen();
                done = abs(get_browse_params());
                driver_unstack_screen();
                showtempmsg(winlist.name);
                break;

            case 's': // save image with boxes
                autobrowse = false;
                drawindow(color_of_box, &winlist); // current window white
                done = 4;
                break;

            case '\\': //back out to last image
                done = 2;
                break;

            default:
                break;
            } //switch
        } //while

        // now clean up memory (and the screen if necessary)
        cleartempmsg();
        if (done >= 1 && done < 4)
        {
            for (int i = wincount-1; i >= 0; i--)
            {
                MoveFromMemory(winlistptr, (U16)sizeof(window), 1L, (long)i, browsehandle);
                boxcount = winlist.boxcount;
                MoveFromMemory((BYTE *)boxx, vidlength, 1L, (long)i, boxxhandle);
                MoveFromMemory((BYTE *)boxy, vidlength, 1L, (long)i, boxyhandle);
                MoveFromMemory((BYTE *)boxvalues, (U16)(vidlength >> 1), 1L, (long)i, boxvalueshandle);
                boxcount >>= 1;
                if (boxcount > 0)
#ifdef XFRACT
                    // Turn all boxes off
                    drawindow(g_color_bright, &winlist);
#else
                    clearbox();
#endif
            }
        }
        if (done == 3)
        {
            goto rescan; // hey everybody I just used the g word!
        }
    }//if
    else
    {
        driver_buzzer(buzzer_codes::INTERRUPT); //no suitable files in directory!
        texttempmsg("Sorry.. I can't find anything");
        no_sub_images = true;
    }

    MemoryRelease(browsehandle);
    MemoryRelease(boxxhandle);
    MemoryRelease(boxyhandle);
    MemoryRelease(boxvalueshandle);
    restore_stack(saved);
    if (oldbf_math == bf_math_type::NONE)
        free_bf_vars();
    bf_math = oldbf_math;
    floatflag = usr_floatflag;

    return (c);
}


static void drawindow(int colour, window *info)
{
#ifndef XFRACT
    coords ibl, itr;
#endif

    boxcolor = colour;
    boxcount = 0;
    if (info->win_size >= minbox)
    {
        // big enough on screen to show up as a box so draw it
        // corner pixels
#ifndef XFRACT
        addbox(info->itl);
        addbox(info->itr);
        addbox(info->ibl);
        addbox(info->ibr);
        drawlines(info->itl, info->itr, info->ibl.x-info->itl.x, info->ibl.y-info->itl.y); // top & bottom lines
        drawlines(info->itl, info->ibl, info->itr.x-info->itl.x, info->itr.y-info->itl.y); // left & right lines
#else
        boxx[0] = info->itl.x + sxoffs;
        boxy[0] = info->itl.y + syoffs;
        boxx[1] = info->itr.x + sxoffs;
        boxy[1] = info->itr.y + syoffs;
        boxx[2] = info->ibr.x + sxoffs;
        boxy[2] = info->ibr.y + syoffs;
        boxx[3] = info->ibl.x + sxoffs;
        boxy[3] = info->ibl.y + syoffs;
        boxcount = 4;
#endif
        dispbox();
    }
    else
    { // draw crosshairs
#ifndef XFRACT
        int cross_size = ydots / 45;
        if (cross_size < 2)
            cross_size = 2;
        itr.x = info->itl.x - cross_size;
        itr.y = info->itl.y;
        ibl.y = info->itl.y - cross_size;
        ibl.x = info->itl.x;
        drawlines(info->itl, itr, ibl.x-itr.x, 0); // top & bottom lines
        drawlines(info->itl, ibl, 0, itr.y-ibl.y); // left & right lines
        dispbox();
#endif
    }
}

// maps points onto view screen
static void transform(dblcoords *point)
{
    double tmp_pt_x;
    tmp_pt_x = cvt->a * point->x + cvt->b * point->y + cvt->e;
    point->y = cvt->c * point->x + cvt->d * point->y + cvt->f;
    point->x = tmp_pt_x;
}

static bool is_visible_window(
    window *list,
    FRACTAL_INFO *info,
    ext_blk_5 *blk_5_info)
{
    dblcoords tl, tr, bl, br;
    bf_t bt_x, bt_y;
    bf_t bt_xmin, bt_xmax, bt_ymin, bt_ymax, bt_x3rd, bt_y3rd;
    int saved;
    int two_len;
    int cornercount;
    bool cant_see;
    int  orig_bflength,
         orig_bnlength,
         orig_padding,
         orig_rlength,
         orig_shiftfactor,
         orig_rbflength;
    double toobig, tmp_sqrt;
    toobig = sqrt(sqr((double)sxdots)+sqr((double)sydots)) * 1.5;
    // arbitrary value... stops browser zooming out too far
    cornercount = 0;
    cant_see = false;

    saved = save_stack();
    // Save original values.
    orig_bflength      = bflength;
    orig_bnlength      = bnlength;
    orig_padding       = padding;
    orig_rlength       = rlength;
    orig_shiftfactor   = shiftfactor;
    orig_rbflength     = rbflength;
    /*
       if (oldbf_math && info->bf_math && (bnlength+4 < info->bflength)) {
          bnlength = info->bflength;
          calc_lengths();
       }
    */
    two_len = bflength + 2;
    bt_x = alloc_stack(two_len);
    bt_y = alloc_stack(two_len);
    bt_xmin = alloc_stack(two_len);
    bt_xmax = alloc_stack(two_len);
    bt_ymin = alloc_stack(two_len);
    bt_ymax = alloc_stack(two_len);
    bt_x3rd = alloc_stack(two_len);
    bt_y3rd = alloc_stack(two_len);

    if (info->bf_math)
    {
        bf_t   bt_t1, bt_t2, bt_t3, bt_t4, bt_t5, bt_t6;
        int di_bflength, two_di_len, two_rbf;

        di_bflength = info->bflength + bnstep;
        two_di_len = di_bflength + 2;
        two_rbf = rbflength + 2;

        n_a     = alloc_stack(two_rbf);
        n_b     = alloc_stack(two_rbf);
        n_c     = alloc_stack(two_rbf);
        n_d     = alloc_stack(two_rbf);
        n_e     = alloc_stack(two_rbf);
        n_f     = alloc_stack(two_rbf);

        convert_bf(n_a, bt_a, rbflength, orig_rbflength);
        convert_bf(n_b, bt_b, rbflength, orig_rbflength);
        convert_bf(n_c, bt_c, rbflength, orig_rbflength);
        convert_bf(n_d, bt_d, rbflength, orig_rbflength);
        convert_bf(n_e, bt_e, rbflength, orig_rbflength);
        convert_bf(n_f, bt_f, rbflength, orig_rbflength);

        bt_t1   = alloc_stack(two_di_len);
        bt_t2   = alloc_stack(two_di_len);
        bt_t3   = alloc_stack(two_di_len);
        bt_t4   = alloc_stack(two_di_len);
        bt_t5   = alloc_stack(two_di_len);
        bt_t6   = alloc_stack(two_di_len);

        memcpy((char *)bt_t1, blk_5_info->apm_data, (two_di_len));
        memcpy((char *)bt_t2, blk_5_info->apm_data+two_di_len, (two_di_len));
        memcpy((char *)bt_t3, blk_5_info->apm_data+2*two_di_len, (two_di_len));
        memcpy((char *)bt_t4, blk_5_info->apm_data+3*two_di_len, (two_di_len));
        memcpy((char *)bt_t5, blk_5_info->apm_data+4*two_di_len, (two_di_len));
        memcpy((char *)bt_t6, blk_5_info->apm_data+5*two_di_len, (two_di_len));

        convert_bf(bt_xmin, bt_t1, two_len, two_di_len);
        convert_bf(bt_xmax, bt_t2, two_len, two_di_len);
        convert_bf(bt_ymin, bt_t3, two_len, two_di_len);
        convert_bf(bt_ymax, bt_t4, two_len, two_di_len);
        convert_bf(bt_x3rd, bt_t5, two_len, two_di_len);
        convert_bf(bt_y3rd, bt_t6, two_len, two_di_len);
    }

    /* tranform maps real plane co-ords onto the current screen view
      see above */
    if (oldbf_math != bf_math_type::NONE || info->bf_math != 0)
    {
        if (!info->bf_math)
        {
            floattobf(bt_x, info->xmin);
            floattobf(bt_y, info->ymax);
        }
        else
        {
            copy_bf(bt_x, bt_xmin);
            copy_bf(bt_y, bt_ymax);
        }
        bftransform(bt_x, bt_y, &tl);
    }
    else
    {
        tl.x = info->xmin;
        tl.y = info->ymax;
        transform(&tl);
    }
    list->itl.x = (int)(tl.x + 0.5);
    list->itl.y = (int)(tl.y + 0.5);
    if (oldbf_math != bf_math_type::NONE || info->bf_math)
    {
        if (!info->bf_math)
        {
            floattobf(bt_x, (info->xmax)-(info->x3rd-info->xmin));
            floattobf(bt_y, (info->ymax)+(info->ymin-info->y3rd));
        }
        else
        {
            neg_a_bf(sub_bf(bt_x, bt_x3rd, bt_xmin));
            add_a_bf(bt_x, bt_xmax);
            sub_bf(bt_y, bt_ymin, bt_y3rd);
            add_a_bf(bt_y, bt_ymax);
        }
        bftransform(bt_x, bt_y, &tr);
    }
    else
    {
        tr.x = (info->xmax)-(info->x3rd-info->xmin);
        tr.y = (info->ymax)+(info->ymin-info->y3rd);
        transform(&tr);
    }
    list->itr.x = (int)(tr.x + 0.5);
    list->itr.y = (int)(tr.y + 0.5);
    if (oldbf_math != bf_math_type::NONE || info->bf_math)
    {
        if (!info->bf_math)
        {
            floattobf(bt_x, info->x3rd);
            floattobf(bt_y, info->y3rd);
        }
        else
        {
            copy_bf(bt_x, bt_x3rd);
            copy_bf(bt_y, bt_y3rd);
        }
        bftransform(bt_x, bt_y, &bl);
    }
    else
    {
        bl.x = info->x3rd;
        bl.y = info->y3rd;
        transform(&bl);
    }
    list->ibl.x = (int)(bl.x + 0.5);
    list->ibl.y = (int)(bl.y + 0.5);
    if (oldbf_math != bf_math_type::NONE || info->bf_math)
    {
        if (!info->bf_math)
        {
            floattobf(bt_x, info->xmax);
            floattobf(bt_y, info->ymin);
        }
        else
        {
            copy_bf(bt_x, bt_xmax);
            copy_bf(bt_y, bt_ymin);
        }
        bftransform(bt_x, bt_y, &br);
    }
    else
    {
        br.x = info->xmax;
        br.y = info->ymin;
        transform(&br);
    }
    list->ibr.x = (int)(br.x + 0.5);
    list->ibr.y = (int)(br.y + 0.5);

    tmp_sqrt = sqrt(sqr(tr.x-bl.x) + sqr(tr.y-bl.y));
    list->win_size = tmp_sqrt; // used for box vs crosshair in drawindow()
    // reject anything too small or too big on screen
    if ((tmp_sqrt < toosmall) || (tmp_sqrt > toobig))
        cant_see = true;

    // restore original values
    bflength      = orig_bflength;
    bnlength      = orig_bnlength;
    padding       = orig_padding;
    rlength       = orig_rlength;
    shiftfactor   = orig_shiftfactor;
    rbflength     = orig_rbflength;

    restore_stack(saved);
    if (cant_see) // do it this way so bignum stack is released
        return false;

    // now see how many corners are on the screen, accept if one or more
    if (tl.x >= (0-sxoffs) && tl.x <= (sxdots-sxoffs) && tl.y >= (0-syoffs) && tl.y <= (sydots-syoffs))
        cornercount++;
    if (bl.x >= (0-sxoffs) && bl.x <= (sxdots-sxoffs) && bl.y >= (0-syoffs) && bl.y <= (sydots-syoffs))
        cornercount++;
    if (tr.x >= (0-sxoffs) && tr.x <= (sxdots-sxoffs) && tr.y >= (0-syoffs) && tr.y <= (sydots-syoffs))
        cornercount++;
    if (br.x >= (0-sxoffs) && br.x <= (sxdots-sxoffs) && br.y >= (0-syoffs) && br.y <= (sydots-syoffs))
        cornercount++;

    if (cornercount >= 1)
        return true;
    else
        return false;
}

static bool paramsOK(FRACTAL_INFO *info)
{
    double tmpparm3, tmpparm4;
    double tmpparm5, tmpparm6;
    double tmpparm7, tmpparm8;
    double tmpparm9, tmpparm10;
#define MINDIF 0.001

    if (info->version > 6)
    {
        tmpparm3 = info->dparm3;
        tmpparm4 = info->dparm4;
    }
    else
    {
        tmpparm3 = info->parm3;
        roundfloatd(&tmpparm3);
        tmpparm4 = info->parm4;
        roundfloatd(&tmpparm4);
    }
    if (info->version > 8)
    {
        tmpparm5 = info->dparm5;
        tmpparm6 = info->dparm6;
        tmpparm7 = info->dparm7;
        tmpparm8 = info->dparm8;
        tmpparm9 = info->dparm9;
        tmpparm10 = info->dparm10;
    }
    else
    {
        tmpparm5 = 0.0;
        tmpparm6 = 0.0;
        tmpparm7 = 0.0;
        tmpparm8 = 0.0;
        tmpparm9 = 0.0;
        tmpparm10 = 0.0;
    }
    if (fabs(info->creal - param[0]) < MINDIF &&
            fabs(info->cimag - param[1]) < MINDIF &&
            fabs(tmpparm3 - param[2]) < MINDIF &&
            fabs(tmpparm4 - param[3]) < MINDIF &&
            fabs(tmpparm5 - param[4]) < MINDIF &&
            fabs(tmpparm6 - param[5]) < MINDIF &&
            fabs(tmpparm7 - param[6]) < MINDIF &&
            fabs(tmpparm8 - param[7]) < MINDIF &&
            fabs(tmpparm9 - param[8]) < MINDIF &&
            fabs(tmpparm10 - param[9]) < MINDIF &&
            info->invert[0] - inversion[0] < MINDIF)
        return true; // parameters are in range
    else
        return false;
}

static bool functionOK(FRACTAL_INFO *info, int numfn)
{
    int mzmatch = 0;
    for (int i = 0; i < numfn; i++)
    {
        if (static_cast<trig_fn>(info->trigndx[i]) != trigndx[i])
            mzmatch++;
    }
    if (mzmatch > 0)
        return false;
    else
        return true; // they all match
}

static bool typeOK(FRACTAL_INFO *info, ext_blk_3 *blk_3_info)
{
    int numfn;
    if ((fractype == fractal_type::FORMULA || fractype == fractal_type::FFORMULA) &&
            (info->fractal_type == static_cast<int>(fractal_type::FORMULA) || info->fractal_type == static_cast<int>(fractal_type::FFORMULA)))
    {
        if (!stricmp(blk_3_info->form_name, FormName))
        {
            numfn = maxfn;
            if (numfn > 0)
                return functionOK(info, numfn);
            else
                return true; // match up formula names with no functions
        }
        else
            return false; // two formulas but names don't match
    }
    else if (info->fractal_type == static_cast<int>(fractype) ||
             info->fractal_type == static_cast<int>(curfractalspecific->tofloat))
    {
        numfn = (curfractalspecific->flags >> 6) & 7;
        if (numfn > 0)
            return functionOK(info, numfn);
        else
            return true; // match types with no functions
    }
    else
        return false; // no match
}

static void check_history(char *oldname, char *newname)
{
    // file_name_stack[] is maintained in framain2.c.  It is the history
    //  file for the browser and holds a maximum of 16 images.  The history
    //  file needs to be adjusted if the rename or delete functions of the
    //  browser are used.
    // name_stack_ptr is also maintained in framain2.c.  It is the index into
    //  file_name_stack[].
    for (int i = 0; i < name_stack_ptr; i++)
    {
        if (stricmp(file_name_stack[i], oldname) == 0) // we have a match
            strcpy(file_name_stack[i], newname);    // insert the new name
    }
}

static void bfsetup_convert_to_screen()
{
    // setup_convert_to_screen() in LORENZ.C, converted to bf_math
    // Call only from within fgetwindow()
    bf_t   bt_det, bt_xd, bt_yd, bt_tmp1, bt_tmp2;
    bf_t   bt_inter1, bt_inter2;
    int saved;

    saved = save_stack();
    bt_inter1 = alloc_stack(rbflength+2);
    bt_inter2 = alloc_stack(rbflength+2);
    bt_det = alloc_stack(rbflength+2);
    bt_xd  = alloc_stack(rbflength+2);
    bt_yd  = alloc_stack(rbflength+2);
    bt_tmp1 = alloc_stack(rbflength+2);
    bt_tmp2 = alloc_stack(rbflength+2);

    // xx3rd-xxmin
    sub_bf(bt_inter1, bfx3rd, bfxmin);
    // yymin-yymax
    sub_bf(bt_inter2, bfymin, bfymax);
    // (xx3rd-xxmin)*(yymin-yymax)
    mult_bf(bt_tmp1, bt_inter1, bt_inter2);

    // yymax-yy3rd
    sub_bf(bt_inter1, bfymax, bfy3rd);
    // xxmax-xxmin
    sub_bf(bt_inter2, bfxmax, bfxmin);
    // (yymax-yy3rd)*(xxmax-xxmin)
    mult_bf(bt_tmp2, bt_inter1, bt_inter2);

    // det = (xx3rd-xxmin)*(yymin-yymax) + (yymax-yy3rd)*(xxmax-xxmin)
    add_bf(bt_det, bt_tmp1, bt_tmp2);

    // xd = d_x_size/det
    floattobf(bt_tmp1, d_x_size);
    div_bf(bt_xd, bt_tmp1, bt_det);

    // a =  xd*(yymax-yy3rd)
    sub_bf(bt_inter1, bfymax, bfy3rd);
    mult_bf(bt_a, bt_xd, bt_inter1);

    // b =  xd*(xx3rd-xxmin)
    sub_bf(bt_inter1, bfx3rd, bfxmin);
    mult_bf(bt_b, bt_xd, bt_inter1);

    // e = -(a*xxmin + b*yymax)
    mult_bf(bt_tmp1, bt_a, bfxmin);
    mult_bf(bt_tmp2, bt_b, bfymax);
    neg_a_bf(add_bf(bt_e, bt_tmp1, bt_tmp2));

    // xx3rd-xxmax
    sub_bf(bt_inter1, bfx3rd, bfxmax);
    // yymin-yymax
    sub_bf(bt_inter2, bfymin, bfymax);
    // (xx3rd-xxmax)*(yymin-yymax)
    mult_bf(bt_tmp1, bt_inter1, bt_inter2);

    // yymin-yy3rd
    sub_bf(bt_inter1, bfymin, bfy3rd);
    // xxmax-xxmin
    sub_bf(bt_inter2, bfxmax, bfxmin);
    // (yymin-yy3rd)*(xxmax-xxmin)
    mult_bf(bt_tmp2, bt_inter1, bt_inter2);

    // det = (xx3rd-xxmax)*(yymin-yymax) + (yymin-yy3rd)*(xxmax-xxmin)
    add_bf(bt_det, bt_tmp1, bt_tmp2);

    // yd = d_y_size/det
    floattobf(bt_tmp2, d_y_size);
    div_bf(bt_yd, bt_tmp2, bt_det);

    // c =  yd*(yymin-yy3rd)
    sub_bf(bt_inter1, bfymin, bfy3rd);
    mult_bf(bt_c, bt_yd, bt_inter1);

    // d =  yd*(xx3rd-xxmax)
    sub_bf(bt_inter1, bfx3rd, bfxmax);
    mult_bf(bt_d, bt_yd, bt_inter1);

    // f = -(c*xxmin + d*yymax)
    mult_bf(bt_tmp1, bt_c, bfxmin);
    mult_bf(bt_tmp2, bt_d, bfymax);
    neg_a_bf(add_bf(bt_f, bt_tmp1, bt_tmp2));

    restore_stack(saved);
}

// maps points onto view screen
static void bftransform(bf_t bt_x, bf_t bt_y, dblcoords *point)
{
    bf_t   bt_tmp1, bt_tmp2;
    int saved;

    saved = save_stack();
    bt_tmp1 = alloc_stack(rbflength+2);
    bt_tmp2 = alloc_stack(rbflength+2);

    //  point->x = cvt->a * point->x + cvt->b * point->y + cvt->e;
    mult_bf(bt_tmp1, n_a, bt_x);
    mult_bf(bt_tmp2, n_b, bt_y);
    add_a_bf(bt_tmp1, bt_tmp2);
    add_a_bf(bt_tmp1, n_e);
    point->x = (double)bftofloat(bt_tmp1);

    //  point->y = cvt->c * point->x + cvt->d * point->y + cvt->f;
    mult_bf(bt_tmp1, n_c, bt_x);
    mult_bf(bt_tmp2, n_d, bt_y);
    add_a_bf(bt_tmp1, bt_tmp2);
    add_a_bf(bt_tmp1, n_f);
    point->y = (double)bftofloat(bt_tmp1);

    restore_stack(saved);
}
