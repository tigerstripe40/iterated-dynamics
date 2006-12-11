#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <string.h>
#include <stdlib.h>
#include "WinText.h"

/*
        WINTEXT.C handles the character-based "prompt screens",
        using a 24x80 character-window driver that I wrote originally
        for the Windows port of the DOS-based "Screen Generator"
        commercial package - Bert Tyler

		Modified for Win32 by Richard Thomson

        the subroutines and their functions are:

BOOL wintext_initialize(HANDLE hInstance, LPSTR title);
    Registers and initializes the text window - must be called
    once (and only once).  Its parameters are the handle of the application
    instance and a pointer to a string containing the title of the window.
void wintext_destroy();
    Destroys items like bitmaps that the initialization routine has
    created.  Should be called once (and only once) as your program exits.

int wintext_texton();
    Brings up and blanks out the text window.  No parameters.
int wintext_textoff();
    Removes the text window.  No parameters.

void wintext_putstring(int xpos, int ypos, int attrib, const char *string);
    Sends a character string to the screen starting at (xpos, ypos)
    using the (CGA-style and, yes, it should be a 'char') specified attribute.
void wintext_paintscreen(int xmin, int xmax, int ymin, int ymax);
    Repaints the rectangular portion of the text screen specified by
    the four parameters, which are in character co-ordinates.  This
    routine is called automatically by 'wintext_putstring()' as well as
    other internal routines whenever Windows uncovers the window.  It can
    also be called  manually by your program when it wants a portion
    of the screen updated (the actual data is kept in two arrays, which
    your program has presumably updated:)
       unsigned char wintext_chars[25][80]  holds the text
       unsigned char wintext_attrs[25][80]  holds the (CGA-style) attributes

void wintext_cursor(int xpos, int ypos, int cursor_type);
    Sets the cursor to character position (xpos, ypos) and switches to
    a cursor type specified by 'cursor_type': 0 = none, 1 = underline,
    2 = block cursor.  A cursor type of -1 means use whatever cursor
    type (0, 1, or 2) was already active.

unsigned int wintext_getkeypress(int option);
    A simple keypress-retriever that, based on the parameter, either checks
    for any keypress activity (option = 0) or waits for a keypress before
    returning (option != 0).  Returns a 0 to indicate no keystroke, or the
    keystroke itself.  Up to 80 keystrokes are queued in an internal buffer.
    If the text window is not open, returns an ESCAPE keystroke (27).
    The keystroke is returned as an integer value identical to that a
    DOS program receives in AX when it invokes INT 16H, AX = 0 or 1.

int wintext_look_for_activity(int option);
    An internal routine that handles buffered keystrokes and lets
    Windows messaging (multitasking) take place.  Called with option=0,
    it returns without waiting for the presence of a keystroke.  Called
    with option !=0, it waits for a keystroke before returning.  Returns
    1 if a keystroke is pending, 0 if none pending.  Called internally
    (and automatically) by 'wintext_getkeypress()'.
void wintext_addkeypress(unsigned int);
    An internal routine, called by 'wintext_look_for_activity()' and
    'wintext_proc()', that adds keystrokes to an internal buffer.
    Never called directly by the applications program.
long FAR PASCAL wintext_proc(HANDLE, UINT, WPARAM, LPARAM);
    An internal routine that handles all message functions while
    the text window is on the screen.  Never called directly by
    the applications program, but must be referenced as a call-back
    routine by your ".DEF" file.

        The 'wintext_textmode' flag tracks the current textmode status.
        Note that pressing Alt-F4 closes and destroys the window *and*
        resets this flag (to 1), so the main program should look at
        this flag whenever it is possible that Alt-F4 has been hit!
        ('wintext_getkeypress()' returns a 27 (ESCAPE) if this happens)
        (Note that you can use an 'WM_CLOSE' case to handle this situation.)
        The 'wintext_textmode' values are:
                0 = the initialization routine has never been called!
                1 = text mode is *not* active
                2 = text mode *is* active
        There is also a 'wintext_AltF4hit' flag that is non-zero if
        the window has been closed (by an Alt-F4, or a WM_CLOSE sequence)
        but the application program hasn't officially closed the window yet.
*/

static int wintext_textmode = 0;
static int wintext_AltF4hit = 0;

/* function prototypes */

static LRESULT CALLBACK wintext_proc(HWND, UINT, WPARAM, LPARAM);

/* Local copy of the "screen" characters and attributes */

unsigned char wintext_chars[25][80];
unsigned char wintext_attrs[25][80];
int wintext_buffer_init;     /* zero if 'screen' is uninitialized */

/* font information */

HFONT wintext_hFont;
int wintext_char_font;
int wintext_char_width;
int wintext_char_height;
int wintext_char_xchars;
int wintext_char_ychars;
int wintext_max_width;
int wintext_max_height;

/* "cursor" variables (AKA the "caret" in Window-Speak) */
int wintext_cursor_x;
int wintext_cursor_y;
int wintext_cursor_type;
int wintext_cursor_owned;
HBITMAP wintext_bitmap[3];
short wintext_cursor_pattern[3][40];

LPSTR wintext_title_text;         /* title-bar text */

/* a few Windows variables we need to remember globally */

HWND wintext_hWndCopy;                /* a Global copy of hWnd */
HWND wintext_hWndParent;              /* a Global copy of hWnd's Parent */
HINSTANCE wintext_hInstance;             /* a global copy of hInstance */

/* the keypress buffer */

#define BUFMAX 80
unsigned int  wintext_keypress_count;
unsigned int  wintext_keypress_head;
unsigned int  wintext_keypress_tail;
unsigned char wintext_keypress_initstate;
unsigned int  wintext_keypress_buffer[BUFMAX];
unsigned char wintext_keypress_state[BUFMAX];

/* EGA/VGA 16-color palette (which doesn't match Windows palette exactly) */
/*
COLORREF wintext_color[] =
{
	RGB(0, 0, 0),
	RGB(0, 0, 168),
	RGB(0, 168, 0),
	RGB(0, 168, 168),
	RGB(168, 0, 0),
	RGB(168, 0, 168),
	RGB(168, 84, 0),
	RGB(168, 168, 168),
	RGB(84, 84, 84),
	RGB(84, 84, 255),
	RGB(84, 255, 84),
	RGB(84, 255, 255),
	RGB(255, 84, 84),
	RGB(255, 84, 255),
	RGB(255, 255, 84),
	RGB(255, 255, 255)
};
*/
/* 16-color Windows Palette */

COLORREF wintext_color[] =
{
	RGB(0, 0, 0),
	RGB(0, 0, 128),
	RGB(0, 128, 0),
	RGB(0, 128, 128),
	RGB(128, 0, 0),
	RGB(128, 0, 128),
	RGB(128, 128, 0),
	RGB(192, 192, 192),
/*  RGB(128, 128, 128),  This looks lousy - make it black */
	RGB(0, 0, 0),
	RGB(0, 0, 255),
	RGB(0, 255, 0),
	RGB(0, 255, 255),
	RGB(255, 0, 0),
	RGB(255, 0, 255),
	RGB(255, 255, 0),
	RGB(255, 255, 255)
};

/*
     Register the text window - a one-time function which perfomrs
     all of the neccessary registration and initialization
*/

BOOL wintext_initialize(HINSTANCE hInstance, HWND hWndParent, LPSTR titletext)
{
    WNDCLASS  wc;
    BOOL return_value;
    HDC hDC;
    HFONT hOldFont;
    TEXTMETRIC TextMetric;
    int i, j;

	OutputDebugString("wintext_initialize\n");
    wintext_hInstance = hInstance;
    wintext_title_text = titletext;
    wintext_hWndParent = hWndParent;

    wc.style = 0;
    wc.lpfnWndProc = wintext_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName =  titletext;
    wc.lpszClassName = "FractintForWindowsV0011";

    return_value = RegisterClass(&wc);

    /* set up the font characteristics */
    wintext_char_font = OEM_FIXED_FONT;
    wintext_hFont = GetStockObject(wintext_char_font);
    hDC=GetDC(hWndParent);
    hOldFont = SelectObject(hDC, wintext_hFont);
    GetTextMetrics(hDC, &TextMetric);
    SelectObject(hDC, hOldFont);
    ReleaseDC(hWndParent, hDC);
    wintext_char_width  = TextMetric.tmMaxCharWidth;
    wintext_char_height = TextMetric.tmHeight;
    wintext_char_xchars = 80;
    wintext_char_ychars = 25;

	/* maximum screen width */
    wintext_max_width = wintext_char_xchars*wintext_char_width + GetSystemMetrics(SM_CXFRAME)*2;
    /* maximum screen height */
	wintext_max_height = wintext_char_ychars*wintext_char_height + GetSystemMetrics(SM_CYFRAME)*2
            - 1 + GetSystemMetrics(SM_CYCAPTION);

    /* set up the font and caret information */
    for (i = 0; i < 3; i++)
	{
        for (j = 0; j < wintext_char_height; j++)
		{
            wintext_cursor_pattern[i][j] = 0;
		}
	}
    for (j = wintext_char_height-2; j < wintext_char_height; j++)
	{
        wintext_cursor_pattern[1][j] = 0x00ff;
	}
    for (j = 0; j < wintext_char_height; j++)
	{
        wintext_cursor_pattern[2][j] = 0x00ff;
	}
    wintext_bitmap[0] = CreateBitmap(8, wintext_char_height, 1, 1, (LPSTR)wintext_cursor_pattern[0]);
    wintext_bitmap[1] = CreateBitmap(8, wintext_char_height, 1, 1, (LPSTR)wintext_cursor_pattern[1]);
    wintext_bitmap[2] = CreateBitmap(8, wintext_char_height, 1, 1, (LPSTR)wintext_cursor_pattern[2]);

    wintext_textmode = 1;
    wintext_AltF4hit = 0;

    return return_value;
}

/*
        clean-up routine
*/
void wintext_destroy(void)
{
	int i;

	OutputDebugString("wintext_destroy\n");

	if (wintext_textmode == 2)  /* text is still active! */
	{
        wintext_textoff();
	}
    if (wintext_textmode != 1)  /* not in the right mode */
	{
        return;
	}

    for (i = 0; i < 3; i++)
	{
        DeleteObject((HANDLE) wintext_bitmap[i]);
	}
    wintext_textmode = 0;
    wintext_AltF4hit = 0;
}


/*
     Set up the text window and clear it
*/
int wintext_texton(void)
{
    HWND hWnd;

	OutputDebugString("wintext_texton\n");

    if (wintext_textmode != 1)  /* not in the right mode */
	{
        return 0;
	}

    /* initialize the cursor */
    wintext_cursor_x    = 0;
    wintext_cursor_y    = 0;
    wintext_cursor_type = 0;
    wintext_cursor_owned = 0;

    /* clear the keyboard buffer */
    wintext_keypress_count = 0;
    wintext_keypress_head  = 0;
    wintext_keypress_tail  = 0;
    wintext_keypress_initstate = 0;
    wintext_buffer_init = 0;

    hWnd = CreateWindow("FractintForWindowsV0011",
        wintext_title_text,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,               /* default horizontal position */
        CW_USEDEFAULT,               /* default vertical position */
        wintext_max_width,
        wintext_max_height,
        wintext_hWndParent,
        NULL,
        wintext_hInstance,
        NULL);

    /* squirrel away a global copy of 'hWnd' for later */
    wintext_hWndCopy = hWnd;

    wintext_textmode = 2;
    wintext_AltF4hit = 0;

    ShowWindow(wintext_hWndCopy, SW_SHOWNORMAL);
    UpdateWindow(wintext_hWndCopy);
    InvalidateRect(wintext_hWndCopy, NULL, FALSE);

    return 0;
}

/*
     Remove the text window
*/

int wintext_textoff(void)
{
	OutputDebugString("wintext_textoff\n");
    wintext_AltF4hit = 0;
    if (wintext_textmode != 2)  /* not in the right mode */
	{
        return 0;
	}
    DestroyWindow(wintext_hWndCopy);
    wintext_textmode = 1;
    return 0;
}

static void wintext_OnClose(HWND window)
{
	OutputDebugString("wintext_OnClose\n");
	wintext_textmode = 1;
	wintext_AltF4hit = 1;
}

static void wintext_OnSetFocus(HWND window, HWND old_focus)
{
	OutputDebugString("wintext_OnSetFocus\n");
	/* get focus - display caret */
	/* create caret & display */
	wintext_cursor_owned = 1;
	CreateCaret(wintext_hWndCopy, wintext_bitmap[wintext_cursor_type], wintext_char_width, wintext_char_height);
	SetCaretPos(wintext_cursor_x*wintext_char_width, wintext_cursor_y*wintext_char_height);
	SetCaretBlinkTime(500);
	ShowCaret(wintext_hWndCopy);
}

static void wintext_OnKillFocus(HWND window, HWND old_focus)
{
	/* kill focus - hide caret */
	OutputDebugString("wintext_OnKillFocus\n");
	wintext_cursor_owned = 0;
	DestroyCaret();
}

static void wintext_OnPaint(HWND window)
{
    PAINTSTRUCT ps;
    HDC hDC = BeginPaint(window, &ps);
    RECT tempRect;

	OutputDebugString("wintext_OnPaint\n");

	/* "Paint" routine - call the worker routine */
	GetUpdateRect(window, &tempRect, FALSE);
	ValidateRect(window, &tempRect);

	/* the routine below handles *all* window updates */
	wintext_paintscreen(0, wintext_char_xchars-1, 0, wintext_char_ychars-1);
	EndPaint(window, &ps);
}

static void wintext_OnKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
	/* KEYUP, KEYDOWN, and CHAR msgs go to the 'keypressed' code */
	/* a key has been pressed - maybe ASCII, maybe not */
	/* if it's an ASCII key, 'WM_CHAR' will handle it  */
	unsigned int i, j, k;
	OutputDebugString("wintext_OnKeyDown\n");
	i = MapVirtualKey(vk, 0);
	j = MapVirtualKey(vk, 2);
	k = (i << 8) + j;
	if (vk == 0x10 || vk == 0x11) /* shift or ctl key */
	{
		j = 0;       /* send flag: special key down */
		k = 0xff00 + (unsigned int) vk;
	}
	if (j == 0)        /* use this call only for non-ASCII keys */
	{
		wintext_addkeypress(k);
	}
}

static void wintext_OnKeyUp(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
	/* KEYUP, KEYDOWN, and CHAR msgs go to the SG code */
	/* a key has been released - maybe ASCII, maybe not */
	/* Watch for Shift, Ctl keys  */
	unsigned int i, j, k;
	OutputDebugString("wintext_OnKeyUp\n");
	i = MapVirtualKey(vk, 0);
	j = MapVirtualKey(vk, 2);
	k = (i << 8) + j;
	j = 1;
	if (vk == 0x10 || vk == 0x11) /* shift or ctl key */
	{
		j = 0;       /* send flag: special key up */
		k = 0xfe00 + vk;
	}
	if (j == 0)        /* use this call only for non-ASCII keys */
	{
		wintext_addkeypress(k);
	}
}

static void wintext_OnChar(HWND hwnd, TCHAR ch, int cRepeat)
{
	/* KEYUP, KEYDOWN, and CHAR msgs go to the SG code */
	/* an ASCII key has been pressed */
	unsigned int i, j, k;
	OutputDebugString("wintext_OnChar\n");
	i = (unsigned int)((cRepeat & 0x00ff0000) >> 16);
	j = ch;
	k = (i << 8) + j;
	wintext_addkeypress(k);
}

static void wintext_OnSize(HWND window, UINT state, int cx, int cy)
{
	OutputDebugString("wintext_OnSize\n");
	if (cx > (WORD)wintext_max_width ||
		cy > (WORD)wintext_max_height)
	{
		SetWindowPos(window,
			GetNextWindow(window, GW_HWNDPREV),
			0, 0, wintext_max_width, wintext_max_height, SWP_NOMOVE);
	}
}

static void wintext_OnGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo)
{
	OutputDebugString("wintext_OnGetMinMaxInfo\n");
	lpMinMaxInfo->ptMaxSize.x = wintext_max_width;
	lpMinMaxInfo->ptMaxSize.y = wintext_max_height;
}

/*
        Window-handling procedure
*/
LRESULT CALLBACK wintext_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (wintext_hWndCopy == NULL)
	{
		wintext_hWndCopy = hWnd;
	} else if (hWnd != wintext_hWndCopy)  /* ??? not the text-mode window! */
	{
         return DefWindowProc(hWnd, message, wParam, lParam);
	}

    switch (message)
	{
	case WM_GETMINMAXINFO:	HANDLE_WM_GETMINMAXINFO(hWnd, wParam, lParam, wintext_OnGetMinMaxInfo); break;
	case WM_CLOSE:			HANDLE_WM_CLOSE(hWnd, wParam, lParam, wintext_OnClose);			break;
	case WM_SIZE:			HANDLE_WM_SIZE(hWnd, wParam, lParam, wintext_OnSize);			break;
	case WM_SETFOCUS:		HANDLE_WM_SETFOCUS(hWnd, wParam, lParam, wintext_OnSetFocus);	break;
	case WM_KILLFOCUS:		HANDLE_WM_KILLFOCUS(hWnd, wParam, lParam, wintext_OnKillFocus); break;
	case WM_PAINT:			HANDLE_WM_PAINT(hWnd, wParam, lParam, wintext_OnPaint);			break;
	case WM_KEYDOWN:		HANDLE_WM_KEYDOWN(hWnd, wParam, lParam, wintext_OnKeyDown);		break;
	case WM_KEYUP:			HANDLE_WM_KEYUP(hWnd, wParam, lParam, wintext_OnKeyUp);			break;
	case WM_CHAR:			HANDLE_WM_CHAR(hWnd, wParam, lParam, wintext_OnChar);			break;
	default:				return DefWindowProc(hWnd, message, wParam, lParam);			break;
    }
    return 0;
}

/*
     simple keyboard logic capable of handling 80
     typed-ahead keyboard characters (more, if BUFMAX is changed)
          wintext_addkeypress() inserts a new keypress
          wintext_getkeypress(0) returns keypress-available info
          wintext_getkeypress(1) takes away the oldest keypress
*/

void wintext_addkeypress(unsigned int keypress)
{
	OutputDebugString("wintext_addkeypress\n");

	if (wintext_textmode != 2)  /* not in the right mode */
		return;

	if (wintext_keypress_count >= BUFMAX)
		/* no room */
		return;

	if ((keypress & 0xfe00) == 0xfe00)
	{
		if (keypress == 0xff10) wintext_keypress_initstate |= 0x01;
		if (keypress == 0xfe10) wintext_keypress_initstate &= 0xfe;
		if (keypress == 0xff11) wintext_keypress_initstate |= 0x02;
		if (keypress == 0xfe11) wintext_keypress_initstate &= 0xfd;
		return;
    }

	if (wintext_keypress_initstate != 0)              /* shift/ctl key down */
	{
		if ((wintext_keypress_initstate & 1) != 0)    /* shift key down */
		{
			if ((keypress & 0x00ff) == 9)             /* TAB key down */
			{
                keypress = (15 << 8);             /* convert to shift-tab */
			}
			if ((keypress & 0x00ff) == 0)           /* special character */
			{
				int i;
				i = (keypress >> 8) & 0xff;
				if (i >= 59 && i <= 68)               /* F1 thru F10 */
				{
					keypress = ((i + 25) << 8);       /* convert to Shifted-Fn */
				}
            }
        }
		else                                        /* control key down */
		{
			if ((keypress & 0x00ff) == 0)           /* special character */
			{
				int i;
				i = ((keypress & 0xff00) >> 8);
				if (i >= 59 && i <= 68)               /* F1 thru F10 */
				{
					keypress = ((i + 35) << 8);       /* convert to Ctl-Fn */
				}
				if (i == 71) keypress = (119 << 8);
				if (i == 73) keypress = (unsigned int)(132 << 8);
				if (i == 75) keypress = (115 << 8);
	            if (i == 77) keypress = (116 << 8);
				if (i == 79) keypress = (117 << 8);
				if (i == 81) keypress = (118 << 8);
            }
        }
    }

	wintext_keypress_buffer[wintext_keypress_head] = keypress;
	wintext_keypress_state[wintext_keypress_head] = wintext_keypress_initstate;
	if (++wintext_keypress_head >= BUFMAX)
	{
		wintext_keypress_head = 0;
	}
	wintext_keypress_count++;
}

unsigned int wintext_getkeypress(int option)
{
	int i;

	OutputDebugString("wintext_getkeypress\n");
	wintext_look_for_activity(option);

	if (wintext_textmode != 2)  /* not in the right mode */
	{
		return 27;
	}

	if (wintext_keypress_count == 0)
	{
		return 0;
	}

	i = wintext_keypress_buffer[wintext_keypress_tail];

	if (option != 0)
	{
		if (++wintext_keypress_tail >= BUFMAX)
		{
			wintext_keypress_tail = 0;
		}
		wintext_keypress_count--;
	}

	return i;
}


/*
     simple event-handler and look-for-keyboard-activity process

     called with parameter:
          0 = tell me if a key was pressed
          1 = wait for a keypress
     returns:
          0 = no activity
          1 = key pressed
*/

int wintext_look_for_activity(int wintext_waitflag)
{
	MSG msg;

	OutputDebugString("wintext_look_for_activity\n");

	if (wintext_textmode != 2)  /* not in the right mode */
	{
		return 0;
	}

	for (;;)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) == 0)
		{
			if (wintext_waitflag == 0 || wintext_keypress_count != 0 || wintext_textmode != 2)
			{
				return (wintext_keypress_count == 0) ? 0 : 1;
			}
		}

		if (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return wintext_keypress_count == 0 ? 0 : 1;
}

/*
        general routine to send a string to the screen
*/

void wintext_putstring(int xpos, int ypos, int attrib, const char *string)
{
	int i, j, k, maxrow, maxcol;
    char xc, xa;

	OutputDebugString("wintext_putstring\n");

	xa = (attrib & 0x0ff);
    j = maxrow = ypos;
    k = maxcol = xpos-1;

    for (i = 0; (xc = string[i]) != 0; i++)
	{
        if (xc == 13 || xc == 10)
		{
            if (j < 24) j++;
            k = -1;
		}
        else
		{
            if ((++k) >= 80)
			{
                if (j < 24) j++;
                k = 0;
            }
            if (maxrow < j) maxrow = j;
            if (maxcol < k) maxcol = k;
            wintext_chars[j][k] = xc;
            wintext_attrs[j][k] = xa;
        }
    }
    if (i > 0)
	{
		wintext_paintscreen(xpos, maxcol, ypos, maxrow);
    }
}

/*
     general routine to repaint the screen
*/

void wintext_paintscreen(
    int xmin,       /* update this rectangular section */
    int xmax,       /* of the 'screen'                 */
    int ymin,
    int ymax)
{
	int i, j, k;
	int istart, jstart, length, foreground, background;
	unsigned char wintext_oldbk;
	unsigned char wintext_oldfg;
	HDC hDC;

	OutputDebugString("wintext_paintscreen\n");

	if (wintext_textmode != 2)  /* not in the right mode */
		return;

	/* first time through?  Initialize the 'screen' */
	if (wintext_buffer_init == 0)
	{
		wintext_buffer_init = 1;
		wintext_oldbk = 0x00;
		wintext_oldfg = 0x0f;
		k = (wintext_oldbk << 4) + wintext_oldfg;
		wintext_buffer_init = 1;
		for (i = 0; i < wintext_char_xchars; i++)
		{
			for (j = 0; j < wintext_char_ychars; j++)
			{
				wintext_chars[j][i] = ' ';
				wintext_attrs[j][i] = k;
            }
        }
    }

	if (xmin < 0) xmin = 0;
	if (xmax >= wintext_char_xchars) xmax = wintext_char_xchars-1;
	if (ymin < 0) ymin = 0;
	if (ymax >= wintext_char_ychars) ymax = wintext_char_ychars-1;

	hDC=GetDC(wintext_hWndCopy);
	SelectObject(hDC, wintext_hFont);
	SetBkMode(hDC, OPAQUE);
	SetTextAlign(hDC, TA_LEFT | TA_TOP);

	if (wintext_cursor_owned != 0)
	{
		HideCaret( wintext_hWndCopy );
	}

	/*
	the following convoluted code minimizes the number of
	discrete calls to the Windows interface by locating
	'strings' of screen locations with common foreground
	and background colors
	*/
	for (j = ymin; j <= ymax; j++)
	{
		length = 0;
		wintext_oldbk = 99;
		wintext_oldfg = 99;
		for (i = xmin; i <= xmax+1; i++)
		{
			k = -1;
			if (i <= xmax)
			{
				k = wintext_attrs[j][i];
			}
			foreground = (k & 15);
			background = (k >> 4);
			if (i > xmax || foreground != (int)wintext_oldfg || background != (int)wintext_oldbk)
			{
				if (length > 0)
				{
					SetBkColor(hDC, wintext_color[wintext_oldbk]);
					SetTextColor(hDC, wintext_color[wintext_oldfg]);
					TextOut(hDC,
						istart*wintext_char_width,
						jstart*wintext_char_height,
						&wintext_chars[jstart][istart],
						length);
                }
				wintext_oldbk = background;
				wintext_oldfg = foreground;
				istart = i;
				jstart = j;
				length = 0;
			}
			length++;
		}
    }

	if (wintext_cursor_owned != 0)
	{
		ShowCaret( wintext_hWndCopy );
	}

	ReleaseDC(wintext_hWndCopy, hDC);
}

void wintext_cursor(int xpos, int ypos, int cursor_type)
{
	OutputDebugString("wintext_cursor\n");

	if (wintext_textmode != 2)  /* not in the right mode */
	{
		return;
	}

    wintext_cursor_x = xpos;
    wintext_cursor_y = ypos;
    if (cursor_type >= 0) wintext_cursor_type = cursor_type;
    if (wintext_cursor_type < 0) wintext_cursor_type = 0;
    if (wintext_cursor_type > 2) wintext_cursor_type = 2;
    if (wintext_cursor_owned != 0)
    {
        CreateCaret( wintext_hWndCopy, wintext_bitmap[wintext_cursor_type],
              wintext_char_width, wintext_char_height);
              SetCaretPos( wintext_cursor_x*wintext_char_width,
                  wintext_cursor_y*wintext_char_height);
              SetCaretBlinkTime(500);
              ShowCaret( wintext_hWndCopy );
	}
}
