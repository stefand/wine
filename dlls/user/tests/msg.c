/*
 * Unit tests for window message handling
 *
 * Copyright 1999 Ove Kaaven
 * Copyright 2003 Dimitrie O. Paun
 * Copyright 2004 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#define _WIN32_WINNT 0x0500 /* For WM_CHANGEUISTATE */

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "wine/test.h"

#define MDI_FIRST_CHILD_ID 2004

/* undocumented SWP flags - from SDK 3.1 */
#define SWP_NOCLIENTSIZE	0x0800
#define SWP_NOCLIENTMOVE	0x1000

/*
FIXME: add tests for these
Window Edge Styles (Win31/Win95/98 look), in order of precedence:
 WS_EX_DLGMODALFRAME: double border, WS_CAPTION allowed
 WS_THICKFRAME: thick border
 WS_DLGFRAME: double border, WS_CAPTION not allowed (but possibly shown anyway)
 WS_BORDER (default for overlapped windows): single black border
 none (default for child (and popup?) windows): no border
*/

typedef enum {
    sent=0x1,
    posted=0x2,
    parent=0x4,
    wparam=0x8,
    lparam=0x10,
    defwinproc=0x20,
    beginpaint=0x40,
    optional=0x80,
    hook=0x100
} msg_flags_t;

struct message {
    UINT message;          /* the WM_* code */
    msg_flags_t flags;     /* message props */
    WPARAM wParam;         /* expected value of wParam */
    LPARAM lParam;         /* expected value of lParam */
};

/* Empty message sequence */
static const struct message WmEmptySeq[] =
{
    { 0 }
};
/* CreateWindow (for overlapped window, not initially visible) (16/32) */
static const struct message WmCreateOverlappedSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_GETMINMAXINFO, sent },
    { WM_NCCREATE, sent },
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE)
 * for a not visible overlapped window.
 */
static const struct message WmSWP_ShowOverlappedSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { HCBT_ACTIVATE, hook },
    { WM_QUERYNEWPALETTE, sent|wparam|lparam|optional, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|optional, SWP_NOSIZE|SWP_NOMOVE }, /* Win9x: SWP_NOSENDCHANGING */
    { WM_ACTIVATEAPP, sent|wparam, 1 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ACTIVATE, sent|wparam, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_IME_NOTIFY, sent|defwinproc|optional },
    { WM_SETFOCUS, sent|wparam|defwinproc, 0 },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    /* Win9x adds SWP_NOZORDER below */
    { WM_WINDOWPOSCHANGED, sent, /*|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE*/ },
    { WM_NCCALCSIZE, sent|wparam|optional, 1 },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_ERASEBKGND, sent|optional },
    { 0 }
};
/* SetWindowPos(SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE)
 * for a visible overlapped window.
 */
static const struct message WmSWP_HideOverlappedSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_SHOW) for a not visible overlapped window */
static const struct message WmShowOverlappedSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { HCBT_ACTIVATE, hook },
    { WM_QUERYNEWPALETTE, sent|wparam|lparam|optional, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOSIZE|SWP_NOMOVE },
    { WM_ACTIVATEAPP, sent|wparam, 1 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ACTIVATE, sent|wparam, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_IME_NOTIFY, sent|defwinproc|optional },
    { WM_SETFOCUS, sent|wparam|defwinproc, 0 },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    /* Win9x adds SWP_NOZORDER below */
    { WM_WINDOWPOSCHANGED, sent, /*|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE*/ },
    { WM_NCCALCSIZE, sent|optional },
    { WM_NCPAINT, sent|optional },
    { WM_ERASEBKGND, sent|optional },
#if 0 /* CreateWindow/ShowWindow(SW_SHOW) also generates WM_SIZE/WM_MOVE
       * messages. Does that mean that CreateWindow doesn't set initial
       * window dimensions for overlapped windows?
       */
    { WM_SIZE, sent },
    { WM_MOVE, sent },
#endif
    { 0 }
};
/* ShowWindow(SW_HIDE) for a visible overlapped window */
static const struct message WmHideOverlappedSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_ACTIVATE, sent|wparam, 0 },
    { WM_ACTIVATEAPP, sent|wparam, 0 },
    { WM_KILLFOCUS, sent|wparam, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_NOTIFY, sent|optional|defwinproc },
    { 0 }
};
/* ShowWindow(SW_HIDE) for an invisible overlapped window */
static const struct message WmHideInvisibleOverlappedSeq[] = {
    { 0 }
};
/* DestroyWindow for a visible overlapped window */
static const struct message WmDestroyOverlappedSeq[] = {
    { HCBT_DESTROYWND, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_ACTIVATE, sent|wparam, 0 },
    { WM_ACTIVATEAPP, sent|wparam, 0 },
    { WM_KILLFOCUS, sent|wparam, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_NOTIFY, sent|optional|defwinproc },
    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* CreateWindow (for a child popup window, not initially visible) */
static const struct message WmCreateChildPopupSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { 0 }
};
/* CreateWindow (for a popup window, not initially visible,
 * which sets WS_VISIBLE in WM_CREATE handler)
 */
static const struct message WmCreateInvisiblePopupSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_STYLECHANGING, sent },
    { WM_STYLECHANGED, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER)
 * for a popup window with WS_VISIBLE style set
 */
static const struct message WmShowVisiblePopupSeq_2[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE)
 * for a popup window with WS_VISIBLE style set
 */
static const struct message WmShowVisiblePopupSeq_3[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { HCBT_ACTIVATE, hook },
    { WM_QUERYNEWPALETTE, sent|wparam|lparam|optional, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_ACTIVATE, sent|wparam, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|parent },
    { WM_IME_SETCONTEXT, sent|parent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_IME_NOTIFY, sent|defwinproc|optional },
    { WM_SETFOCUS, sent|defwinproc },
    { 0 }
};
/* CreateWindow (for child window, not initially visible) */
static const struct message WmCreateChildSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    /* child is inserted into parent's child list after WM_NCCREATE returns */
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { WM_PARENTNOTIFY, sent|parent|wparam, WM_CREATE },
    { 0 }
};
/* CreateWindow (for maximized child window, not initially visible) */
static const struct message WmCreateMaximizedChildSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { HCBT_MINMAX, hook|lparam, 0, SW_MAXIMIZE },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
    { WM_PARENTNOTIFY, sent|parent|wparam, WM_CREATE },
    { 0 }
};
/* CreateWindow (for a child window, initially visible) */
static const struct message WmCreateVisibleChildSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    /* child is inserted into parent's child list after WM_NCCREATE returns */
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { WM_PARENTNOTIFY, sent|parent|wparam, WM_CREATE },
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_SHOW) for a not visible child window */
static const struct message WmShowChildSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_HIDE) for a visible child window */
static const struct message WmHideChildSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE)
 * for a not visible child window
 */
static const struct message WmShowChildSeq_2[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE)
 * for a not visible child window
 */
static const struct message WmShowChildSeq_3[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE)
 * for a visible child window with a caption
 */
static const struct message WmShowChildSeq_4[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent },
    { 0 }
};
/* ShowWindow(SW_SHOW) for child with invisible parent */
static const struct message WmShowChildInvisibleParentSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { 0 }
};
/* ShowWindow(SW_HIDE) for child with invisible parent */
static const struct message WmHideChildInvisibleParentSeq[] = {
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { 0 }
};
/* SetWindowPos(SWP_SHOWWINDOW) for child with invisible parent */
static const struct message WmShowChildInvisibleParentSeq_2[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetWindowPos(SWP_HIDEWINDOW) for child with invisible parent */
static const struct message WmHideChildInvisibleParentSeq_2[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* DestroyWindow for a visible child window */
static const struct message WmDestroyChildSeq[] = {
    { HCBT_DESTROYWND, hook },
    { WM_PARENTNOTIFY, sent|parent|wparam, WM_DESTROY },
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { HCBT_SETFOCUS, hook }, /* set focus to a parent */
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|parent|optional, 1 },
    { WM_SETFOCUS, sent|parent },
    { WM_DESTROY, sent },
    { WM_DESTROY, sent|optional }, /* a bug in win2k sp4 ? */
    { WM_NCDESTROY, sent },
    { WM_NCDESTROY, sent|optional }, /* a bug in win2k sp4 ? */
    { 0 }
};
/* Moving the mouse in nonclient area */
static const struct message WmMouseMoveInNonClientAreaSeq[] = { /* FIXME: add */
    { WM_NCHITTEST, sent },
    { WM_SETCURSOR, sent },
    { WM_NCMOUSEMOVE, posted },
    { 0 }
};
/* Moving the mouse in client area */
static const struct message WmMouseMoveInClientAreaSeq[] = { /* FIXME: add */
    { WM_NCHITTEST, sent },
    { WM_SETCURSOR, sent },
    { WM_MOUSEMOVE, posted },
    { 0 }
};
/* Moving by dragging the title bar (after WM_NCHITTEST and WM_SETCURSOR) (outline move) */
static const struct message WmDragTitleBarSeq[] = { /* FIXME: add */
    { WM_NCLBUTTONDOWN, sent|wparam, HTCAPTION },
    { WM_SYSCOMMAND, sent|defwinproc|wparam, SC_MOVE+2 },
    { WM_GETMINMAXINFO, sent|defwinproc },
    { WM_ENTERSIZEMOVE, sent|defwinproc },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, 0 },
    { WM_MOVE, sent|defwinproc },
    { WM_EXITSIZEMOVE, sent|defwinproc },
    { 0 }
};
/* Sizing by dragging the thick borders (after WM_NCHITTEST and WM_SETCURSOR) (outline move) */
static const struct message WmDragThickBordersBarSeq[] = { /* FIXME: add */
    { WM_NCLBUTTONDOWN, sent|wparam, 0xd },
    { WM_SYSCOMMAND, sent|defwinproc|wparam, 0xf004 },
    { WM_GETMINMAXINFO, sent|defwinproc },
    { WM_ENTERSIZEMOVE, sent|defwinproc },
    { WM_SIZING, sent|defwinproc|wparam, 4}, /* one for each mouse movement */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, 0 },
    { WM_GETMINMAXINFO, sent|defwinproc },
    { WM_NCCALCSIZE, sent|defwinproc|wparam, 1 },
    { WM_NCPAINT, sent|defwinproc|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc },
    { WM_ERASEBKGND, sent|defwinproc },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, 0 },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },
    { WM_EXITSIZEMOVE, sent|defwinproc },
    { 0 }
};
/* Resizing child window with MoveWindow (32) */
static const struct message WmResizingChildWithMoveWindowSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_ERASEBKGND, sent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },
    { 0 }
};
/* Clicking on inactive button */
static const struct message WmClickInactiveButtonSeq[] = { /* FIXME: add */
    { WM_NCHITTEST, sent },
    { WM_PARENTNOTIFY, sent|parent|wparam, WM_LBUTTONDOWN },
    { WM_MOUSEACTIVATE, sent },
    { WM_MOUSEACTIVATE, sent|parent|defwinproc },
    { WM_SETCURSOR, sent },
    { WM_SETCURSOR, sent|parent|defwinproc },
    { WM_LBUTTONDOWN, posted },
    { WM_KILLFOCUS, posted|parent },
    { WM_SETFOCUS, posted },
    { WM_CTLCOLORBTN, posted|parent },
    { BM_SETSTATE, posted },
    { WM_CTLCOLORBTN, posted|parent },
    { WM_LBUTTONUP, posted },
    { BM_SETSTATE, posted },
    { WM_CTLCOLORBTN, posted|parent },
    { WM_COMMAND, posted|parent },
    { 0 }
};
/* Reparenting a button (16/32) */
/* The last child (button) reparented gets topmost for its new parent. */
static const struct message WmReparentButtonSeq[] = { /* FIXME: add */
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOSIZE|SWP_NOZORDER },
    { WM_CHILDACTIVATE, sent },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOSIZE|SWP_NOREDRAW|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { 0 }
};
/* Creation of a custom dialog (32) */
static const struct message WmCreateCustomDialogSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_GETMINMAXINFO, sent },
    { WM_NCCREATE, sent },
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { HCBT_ACTIVATE, hook },
    { WM_QUERYNEWPALETTE, sent|wparam|lparam|optional, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_ACTIVATE, sent|wparam, 1 },
    { WM_KILLFOCUS, sent|parent },
    { WM_IME_SETCONTEXT, sent|parent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_IME_NOTIFY, sent|optional|defwinproc },
    { WM_SETFOCUS, sent },
    { WM_GETDLGCODE, sent|defwinproc|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_ERASEBKGND, sent },
    { WM_CTLCOLORDLG, sent|defwinproc },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_GETTEXT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_NCCALCSIZE, sent|optional },
    { WM_NCPAINT, sent|optional },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_ERASEBKGND, sent|optional },
    { WM_CTLCOLORDLG, sent|optional|defwinproc },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { 0 }
};
/* Calling EndDialog for a custom dialog (32) */
static const struct message WmEndCustomDialogSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_GETTEXT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { HCBT_ACTIVATE, hook },
    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETTEXT, sent|optional|defwinproc },
    { WM_ACTIVATE, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|optional, 0 },
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|parent|wparam|defwinproc|optional, 1 },
    { WM_IME_NOTIFY, sent|optional },
    { WM_SETFOCUS, sent|parent|defwinproc },
    { 0 }
};
/* Creation and destruction of a modal dialog (32) */
static const struct message WmModalDialogSeq[] = {
    { WM_CANCELMODE, sent|parent },
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|parent },
    { WM_IME_SETCONTEXT, sent|parent|wparam|optional, 0 },
    { WM_ENABLE, sent|parent|wparam, 0 },
    { HCBT_CREATEWND, hook },
    { WM_SETFONT, sent },
    { WM_INITDIALOG, sent },
    { WM_CHANGEUISTATE, sent|optional },
    { WM_SHOWWINDOW, sent },
    { HCBT_ACTIVATE, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_ACTIVATE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCPAINT, sent },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_ERASEBKGND, sent },
    { WM_CTLCOLORDLG, sent },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_NCCALCSIZE, sent|optional },
    { WM_NCPAINT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_CTLCOLORDLG, sent|optional },
    { WM_PAINT, sent|optional },
    { WM_CTLCOLORBTN, sent },
    { WM_ENTERIDLE, sent|parent|optional },
    { WM_TIMER, sent },
    { WM_ENABLE, sent|parent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { HCBT_ACTIVATE, hook },
    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_ACTIVATE, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|optional },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|parent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|parent|defwinproc },
    { HCBT_DESTROYWND, hook },
    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* Creation of a modal dialog that is resized inside WM_INITDIALOG (32) */
static const struct message WmCreateModalDialogResizeSeq[] = { /* FIXME: add */
    /* (inside dialog proc, handling WM_INITDIALOG) */
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCCALCSIZE, sent },
    { WM_NCACTIVATE, sent|parent|wparam, 0 },
    { WM_GETTEXT, sent|defwinproc },
    { WM_ACTIVATE, sent|parent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|parent },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_ACTIVATE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_SIZE, sent|defwinproc },
    /* (setting focus) */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, 0 },
    { WM_NCPAINT, sent },
    { WM_GETTEXT, sent|defwinproc },
    { WM_ERASEBKGND, sent },
    { WM_CTLCOLORDLG, sent|defwinproc },
    { WM_WINDOWPOSCHANGED, sent|wparam, 0 },
    { WM_PAINT, sent },
    /* (bunch of WM_CTLCOLOR* for each control) */
    { WM_PAINT, sent|parent },
    { WM_ENTERIDLE, sent|parent|wparam, 0 },
    { WM_SETCURSOR, sent|parent },
    { 0 }
};
/* SetMenu for NonVisible windows with size change*/
static const struct message WmSetMenuNonVisibleSizeChangeSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_NCCALCSIZE, sent|wparam|optional, 1 },
    { 0 }
};
/* SetMenu for NonVisible windows with no size change */
static const struct message WmSetMenuNonVisibleNoSizeChangeSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetMenu for Visible windows with size change */
static const struct message WmSetMenuVisibleSizeChangeSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_ACTIVATE, sent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },
    { WM_NCCALCSIZE, sent|wparam|optional, 1 },
    { WM_NCPAINT, sent|wparam|optional, 1 },
    { WM_ERASEBKGND, sent|optional },
    { 0 }
};
/* SetMenu for Visible windows with no size change */
static const struct message WmSetMenuVisibleNoSizeChangeSeq[] = {
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_ACTIVATE, sent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* DrawMenuBar for a visible window */
static const struct message WmDrawMenuBarSeq[] =
{
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};

static const struct message WmSetRedrawFalseSeq[] =
{
    { WM_SETREDRAW, sent|wparam, 0 },
    { 0 }
};

static const struct message WmSetRedrawTrueSeq[] =
{
    { WM_SETREDRAW, sent|wparam, 1 },
    { 0 }
};

static const struct message WmEnableWindowSeq[] =
{
    { WM_CANCELMODE, sent },
    { WM_ENABLE, sent },
    { 0 }
};

static const struct message WmGetScrollRangeSeq[] =
{
    { SBM_GETRANGE, sent },
    { 0 }
};
static const struct message WmGetScrollInfoSeq[] =
{
    { SBM_GETSCROLLINFO, sent },
    { 0 }
};
static const struct message WmSetScrollRangeSeq[] =
{
    /* MSDN claims that Windows sends SBM_SETRANGE message, but win2k SP4
       sends SBM_SETSCROLLINFO.
     */
    { SBM_SETSCROLLINFO, sent },
    { 0 }
};
/* SetScrollRange for a window without a non-client area */
static const struct message WmSetScrollRangeHVSeq[] =
{
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* SetScrollRange for a window with a non-client area */
static const struct message WmSetScrollRangeHV_NC_Seq[] =
{
    { WM_WINDOWPOSCHANGING, sent, /*|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER*/ },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_NCPAINT, sent|optional },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETICON, sent|optional|defwinproc },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent|optional },
    { WM_CTLCOLORDLG, sent|defwinproc|optional }, /* sent to a parent of the dialog */
    { WM_WINDOWPOSCHANGED, sent, /*|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|0x1000*/ },
    { WM_SIZE, sent|defwinproc },
    { WM_GETTEXT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETICON, sent|optional },
    { WM_GETTEXT, sent|optional },
    { 0 }
};

static int after_end_dialog;
static int sequence_cnt, sequence_size;
static struct message* sequence;
static int log_all_parent_messages;

static void add_message(const struct message *msg)
{
    if (!sequence) 
    {
	sequence_size = 10;
	sequence = HeapAlloc( GetProcessHeap(), 0, sequence_size * sizeof (struct message) );
    }
    if (sequence_cnt == sequence_size) 
    {
	sequence_size *= 2;
	sequence = HeapReAlloc( GetProcessHeap(), 0, sequence, sequence_size * sizeof (struct message) );
    }
    assert(sequence);

    sequence[sequence_cnt].message = msg->message;
    sequence[sequence_cnt].flags = msg->flags;
    sequence[sequence_cnt].wParam = msg->wParam;
    sequence[sequence_cnt].lParam = msg->lParam;

    sequence_cnt++;
}

static void flush_sequence()
{
    HeapFree(GetProcessHeap(), 0, sequence);
    sequence = 0;
    sequence_cnt = sequence_size = 0;
}

#define ok_sequence( exp, contx, todo) \
        ok_sequence_( (exp), (contx), (todo), __FILE__, __LINE__)


static void ok_sequence_(const struct message *expected, const char *context, int todo,
        const char *file, int line)
{
    static const struct message end_of_sequence = { 0, 0, 0, 0 };
    const struct message *actual;
    int failcount = 0;
    
    add_message(&end_of_sequence);

    actual = sequence;

    while (expected->message && actual->message)
    {
	trace_( file, line)("expected %04x - actual %04x\n", expected->message, actual->message);

	if (expected->message == actual->message)
	{
	    if (expected->flags & wparam)
	    {
		if (expected->wParam != actual->wParam && todo)
		{
		    todo_wine {
                        failcount ++;
                        ok_( file, line) (FALSE,
			    "%s: in msg 0x%04x expecting wParam 0x%x got 0x%x\n",
			    context, expected->message, expected->wParam, actual->wParam);
		    }
		}
		else
		ok_( file, line) (expected->wParam == actual->wParam,
		     "%s: in msg 0x%04x expecting wParam 0x%x got 0x%x\n",
		     context, expected->message, expected->wParam, actual->wParam);
	    }
	    if (expected->flags & lparam)
		 ok_( file, line) (expected->lParam == actual->lParam,
		     "%s: in msg 0x%04x expecting lParam 0x%lx got 0x%lx\n",
		     context, expected->message, expected->lParam, actual->lParam);
	    ok_( file, line) ((expected->flags & defwinproc) == (actual->flags & defwinproc),
		"%s: the msg 0x%04x should %shave been sent by DefWindowProc\n",
		context, expected->message, (expected->flags & defwinproc) ? "" : "NOT ");
	    ok_( file, line) ((expected->flags & beginpaint) == (actual->flags & beginpaint),
		"%s: the msg 0x%04x should %shave been sent by BeginPaint\n",
		context, expected->message, (expected->flags & beginpaint) ? "" : "NOT ");
	    ok_( file, line) ((expected->flags & (sent|posted)) == (actual->flags & (sent|posted)),
		"%s: the msg 0x%04x should have been %s\n",
		context, expected->message, (expected->flags & posted) ? "posted" : "sent");
	    ok_( file, line) ((expected->flags & parent) == (actual->flags & parent),
		"%s: the msg 0x%04x was expected in %s\n",
		context, expected->message, (expected->flags & parent) ? "parent" : "child");
	    ok_( file, line) ((expected->flags & hook) == (actual->flags & hook),
		"%s: the msg 0x%04x should have been sent by a hook\n",
		context, expected->message);
	    expected++;
	    actual++;
	}
	else if (expected->flags & optional)
	    expected++;
	else if (todo)
	{
            failcount++;
            todo_wine {
                ok_( file, line) (FALSE, "%s: the msg 0x%04x was expected, but got msg 0x%04x instead\n",
                    context, expected->message, actual->message);
            }
            flush_sequence();
            return;
        }
        else
        {
            ok_( file, line) (FALSE, "%s: the msg 0x%04x was expected, but got msg 0x%04x instead\n",
                context, expected->message, actual->message);
            expected++;
            actual++;
        }
    }

    /* skip all optional trailing messages */
    while (expected->message && (expected->flags & optional))
	expected++;

    if (todo)
    {
        todo_wine {
            if (expected->message || actual->message) {
                failcount++;
                ok_( file, line) (FALSE, "%s: the msg sequence is not complete: expected %04x - actual %04x\n",
                    context, expected->message, actual->message);
            }
        }
    }
    else
    {
        if (expected->message || actual->message)
            ok_( file, line) (FALSE, "%s: the msg sequence is not complete: expected %04x - actual %04x\n",
                context, expected->message, actual->message);
    }
    if( todo && !failcount) /* succeeded yet marked todo */
        todo_wine {
            ok_( file, line)( TRUE, "%s: marked \"todo_wine\" but succeeds\n", context);
        }

    flush_sequence();
}

/******************************** MDI test **********************************/

/* CreateWindow for MDI frame window, initially visible */
static const struct message WmCreateMDIframeSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_GETMINMAXINFO, sent },
    { WM_NCCREATE, sent },
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { HCBT_ACTIVATE, hook },
    { WM_QUERYNEWPALETTE, sent|wparam|lparam|optional, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOSIZE|SWP_NOMOVE },
    { WM_WINDOWPOSCHANGED, sent|wparam|optional, SWP_NOSIZE|SWP_NOMOVE|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE }, /* Win9x */
    { WM_ACTIVATEAPP, sent|wparam, 1 },
    { WM_NCACTIVATE, sent|wparam, 1 },
    { WM_ACTIVATE, sent|wparam, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|wparam|defwinproc, 0 },
    /* Win9x adds SWP_NOZORDER below */
    { WM_WINDOWPOSCHANGED, sent, /*|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE*/ },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { 0 }
};
/* DestroyWindow for MDI frame window, initially visible */
static const struct message WmDestroyMDIframeSeq[] = {
    { HCBT_DESTROYWND, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_ACTIVATE, sent|wparam|optional, 0 }, /* Win9x */
    { WM_ACTIVATEAPP, sent|wparam|optional, 0 }, /* Win9x */
    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* CreateWindow for MDI client window, initially visible */
static const struct message WmCreateMDIclientSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent },
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { WM_PARENTNOTIFY, sent|wparam, WM_CREATE }, /* in MDI frame */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOSIZE|SWP_NOMOVE },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOSIZE|SWP_NOMOVE|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* DestroyWindow for MDI client window, initially visible */
static const struct message WmDestroyMDIclientSeq[] = {
    { HCBT_DESTROYWND, hook },
    { WM_PARENTNOTIFY, sent|wparam, WM_DESTROY }, /* in MDI frame */
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* CreateWindow for MDI child window, initially visible */
static const struct message WmCreateMDIchildVisibleSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_CREATE, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_CREATE*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_MDIREFRESHMENU, sent/*|wparam|lparam, 0, 0*/ },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },

    /* Win9x: message sequence terminates here. */

    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },
    { WM_MDIACTIVATE, sent|defwinproc },
    { 0 }
};
/* DestroyWindow for MDI child window, initially visible */
static const struct message WmDestroyMDIchildVisibleSeq[] = {
    { HCBT_DESTROYWND, hook },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_DESTROY, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_DESTROY*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    /* { WM_DESTROY, sent }
     * Win9x: message sequence terminates here.
     */

    { HCBT_SETFOCUS, hook }, /* set focus to MDI client */
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */

    { HCBT_SETFOCUS, hook }, /* MDI client sets focus back to MDI child */
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent }, /* in MDI client */

    { HCBT_SETFOCUS, hook }, /* set focus to MDI client */
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */

    { HCBT_SETFOCUS, hook }, /* MDI client sets focus back to MDI child */
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent }, /* in MDI client */

    { WM_DESTROY, sent },

    { HCBT_SETFOCUS, hook }, /* set focus to MDI client */
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */

    { HCBT_SETFOCUS, hook }, /* MDI client sets focus back to MDI child */
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent }, /* in MDI client */

    { WM_NCDESTROY, sent },
    { 0 }
};
/* CreateWindow for MDI child window, initially invisible */
static const struct message WmCreateMDIchildInvisibleSeq[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_CREATE, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_CREATE*/ }, /* in MDI client */
    { 0 }
};
/* DestroyWindow for MDI child window, initially invisible */
static const struct message WmDestroyMDIchildInvisibleSeq[] = {
    { HCBT_DESTROYWND, hook },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_DESTROY, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_DESTROY*/ }, /* in MDI client */
    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* CreateWindow for the 1st MDI child window, initially visible and maximized */
static const struct message WmCreateMDIchildVisibleMaxSeq1[] = {
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { HCBT_MINMAX, hook|lparam, 0, SW_MAXIMIZE },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_CREATE, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_CREATE*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_MDIREFRESHMENU, sent/*|wparam|lparam, 0, 0*/ },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },

    /* Win9x: message sequence terminates here. */

    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },
    { WM_MDIACTIVATE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* CreateWindow for the 2nd MDI child window, initially visible and maximized */
static const struct message WmCreateMDIchildVisibleMaxSeq2[] = {
    /* restore the 1st MDI child */
    { WM_SETREDRAW, sent|wparam, 0 },
    { HCBT_MINMAX, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_SETREDRAW, sent|wparam, 1 }, /* in the 1st MDI child */
    /* create the 2nd MDI child */
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { HCBT_MINMAX, hook|lparam, 0, SW_MAXIMIZE },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_CREATE, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_CREATE*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_MDIREFRESHMENU, sent/*|wparam|lparam, 0, 0*/ },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },

    { WM_NCACTIVATE, sent|wparam|defwinproc, 0 }, /* in the 1st MDI child */
    { WM_MDIACTIVATE, sent|defwinproc }, /* in the 1st MDI child */

    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },

    /* Win9x: message sequence terminates here. */

    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|defwinproc }, /* in the 1st MDI child */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 0 }, /* in the 1st MDI child */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },

    { WM_MDIACTIVATE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* WM_MDICREATE MDI child window, initially visible and maximized */
static const struct message WmCreateMDIchildVisibleMaxSeq3[] = {
    { WM_MDICREATE, sent },
    { HCBT_CREATEWND, hook },
    { WM_NCCREATE, sent }, 
    { WM_NCCALCSIZE, sent|wparam, 0 },
    { WM_CREATE, sent },
    { WM_SIZE, sent },
    { WM_MOVE, sent },
    { HCBT_MINMAX, hook|lparam, 0, SW_MAXIMIZE },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_CREATE, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_CREATE*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_MDIREFRESHMENU, sent/*|wparam|lparam, 0, 0*/ },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },

    /* Win9x: message sequence terminates here. */

    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },

    { WM_MDIACTIVATE, sent|defwinproc },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },

     /* in MDI client */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent|defwinproc },

    { 0 }
};
/* WM_SYSCOMMAND/SC_CLOSE for the 2nd MDI child window, initially visible and maximized */
static const struct message WmDestroyMDIchildVisibleMaxSeq2[] = {
    { WM_SYSCOMMAND, sent|wparam, SC_CLOSE },
    { HCBT_SYSCOMMAND, hook },
    { WM_CLOSE, sent|defwinproc },
    { WM_MDIDESTROY, sent }, /* in MDI client */

    /* bring the 1st MDI child to top */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOSIZE|SWP_NOMOVE }, /* in the 1st MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE }, /* in the 2nd MDI child */
    { WM_CHILDACTIVATE, sent|defwinproc|wparam|lparam, 0, 0 }, /* in the 1st MDI child */
    { WM_NCACTIVATE, sent|wparam|defwinproc, 0 }, /* in the 1st MDI child */
    { WM_MDIACTIVATE, sent|defwinproc }, /* in the 1st MDI child */

    /* maximize the 1st MDI child */
    { HCBT_MINMAX, hook },
    { WM_GETMINMAXINFO, sent|defwinproc },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|defwinproc|wparam, 1 },
    { WM_CHILDACTIVATE, sent|defwinproc|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },

    /* restore the 2nd MDI child */
    { WM_SETREDRAW, sent|defwinproc|wparam, 0 },
    { HCBT_MINMAX, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOZORDER|0x8000 },
    { WM_NCCALCSIZE, sent|defwinproc|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
    { WM_SETREDRAW, sent|defwinproc|wparam, 1 },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    /* bring the 1st MDI child to top */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },
    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|defwinproc },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },
    { WM_MDIACTIVATE, sent|defwinproc },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_NOSIZE|SWP_NOMOVE|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    /* apparently ShowWindow(SW_SHOW) on an MDI client */
    { WM_SHOWWINDOW, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { WM_MDIREFRESHMENU, sent },

    { HCBT_DESTROYWND, hook },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_DESTROY, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_DESTROY*/ }, /* in MDI client */
    { WM_SHOWWINDOW, sent|defwinproc|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    { WM_DESTROY, sent|defwinproc },
    { WM_NCDESTROY, sent|defwinproc },
    { 0 }
};
/* WM_MDIDESTROY for the single MDI child window, initially visible and maximized */
static const struct message WmDestroyMDIchildVisibleMaxSeq1[] = {
    { WM_MDIDESTROY, sent }, /* in MDI client */
    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },

     /* in MDI client */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent|defwinproc },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam|defwinproc, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam|defwinproc, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },

     /* in MDI client */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam|defwinproc, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent|defwinproc },

     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    { WM_NCACTIVATE, sent|wparam, 0 },
    { WM_MDIACTIVATE, sent },

    { HCBT_MINMAX, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_SHOWWINDOW|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },

     /* in MDI child */
    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam|defwinproc, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam|defwinproc, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_MOVE, sent|defwinproc },
    { WM_SIZE, sent|defwinproc },

     /* in MDI client */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_NOACTIVATE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTMOVE },
    { WM_SIZE, sent },

    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */

    { WM_MDIREFRESHMENU, sent }, /* in MDI client */

    { HCBT_DESTROYWND, hook },
    /* Win2k sends wparam set to
     * MAKEWPARAM(WM_DESTROY, MDI_FIRST_CHILD_ID + nTotalCreated),
     * while Win9x doesn't bother to set child window id according to
     * CLIENTCREATESTRUCT.idFirstChild
     */
    { WM_PARENTNOTIFY, sent /*|wparam, WM_DESTROY*/ }, /* in MDI client */

    { WM_SHOWWINDOW, sent|wparam, 0 },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_ERASEBKGND, sent|parent|optional },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },

    { WM_DESTROY, sent },
    { WM_NCDESTROY, sent },
    { 0 }
};
/* ShowWindow(SW_MAXIMIZE) for a not visible MDI child window */
static const struct message WmMaximizeMDIchildInvisibleSeq[] = {
    { HCBT_MINMAX, hook },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },

    { WM_WINDOWPOSCHANGING, sent|wparam|defwinproc, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE },
    { WM_NCACTIVATE, sent|wparam|defwinproc, 1 },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 }, /* in MDI client */
    { WM_SETFOCUS, sent }, /* in MDI client */
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 }, /* in MDI client */
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|defwinproc },
    { WM_MDIACTIVATE, sent|defwinproc },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_MAXIMIZE) for a visible MDI child window */
static const struct message WmMaximizeMDIchildVisibleSeq[] = {
    { HCBT_MINMAX, hook },
    { WM_GETMINMAXINFO, sent },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_RESTORE) for a visible MDI child window */
static const struct message WmRestoreMDIchildVisibleSeq[] = {
    { HCBT_MINMAX, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};
/* ShowWindow(SW_RESTORE) for a not visible MDI child window */
static const struct message WmRestoreMDIchildInisibleSeq[] = {
    { HCBT_MINMAX, hook },
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_SHOWWINDOW|SWP_FRAMECHANGED|0x8000 },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_CHILDACTIVATE, sent|wparam|lparam, 0, 0 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE|0x8000 },
    { WM_SIZE, sent|defwinproc },
     /* in MDI frame */
    { WM_WINDOWPOSCHANGING, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER },
    { WM_NCCALCSIZE, sent|wparam, 1 },
    { WM_WINDOWPOSCHANGED, sent|wparam, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOCLIENTSIZE|SWP_NOCLIENTMOVE },
    { 0 }
};

static HWND mdi_client;
static WNDPROC old_mdi_client_proc;

static LRESULT WINAPI mdi_client_hook_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct message msg;

    /* do not log painting messages */
    if (message != WM_PAINT &&
        message != WM_ERASEBKGND &&
        message != WM_NCPAINT &&
        message != WM_GETTEXT &&
        message != WM_MDIGETACTIVE)
    {
        trace("mdi client: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

        switch (message)
        {
            case WM_WINDOWPOSCHANGING:
            case WM_WINDOWPOSCHANGED:
            {
                WINDOWPOS *winpos = (WINDOWPOS *)lParam;

                trace("%s\n", (message == WM_WINDOWPOSCHANGING) ? "WM_WINDOWPOSCHANGING" : "WM_WINDOWPOSCHANGED");
                trace("%p after %p, x %d, y %d, cx %d, cy %d flags %08x\n",
                      winpos->hwnd, winpos->hwndInsertAfter,
                      winpos->x, winpos->y, winpos->cx, winpos->cy, winpos->flags);

                /* Log only documented flags, win2k uses 0x1000 and 0x2000
                 * in the high word for internal purposes
                 */
                wParam = winpos->flags & 0xffff;
                break;
            }
        }

        msg.message = message;
        msg.flags = sent|wparam|lparam;
        msg.wParam = wParam;
        msg.lParam = lParam;
        add_message(&msg);
    }

    return CallWindowProcA(old_mdi_client_proc, hwnd, message, wParam, lParam);
}

static LRESULT WINAPI mdi_child_wnd_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    LRESULT ret;
    struct message msg;

    /* do not log painting messages */
    if (message != WM_PAINT &&
        message != WM_ERASEBKGND &&
        message != WM_NCPAINT &&
        message != WM_GETTEXT)
    {
        trace("mdi child: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

        switch (message)
        {
            case WM_WINDOWPOSCHANGING:
            case WM_WINDOWPOSCHANGED:
            {
                WINDOWPOS *winpos = (WINDOWPOS *)lParam;

                trace("%s\n", (message == WM_WINDOWPOSCHANGING) ? "WM_WINDOWPOSCHANGING" : "WM_WINDOWPOSCHANGED");
                trace("%p after %p, x %d, y %d, cx %d, cy %d flags %08x\n",
                      winpos->hwnd, winpos->hwndInsertAfter,
                      winpos->x, winpos->y, winpos->cx, winpos->cy, winpos->flags);

                /* Log only documented flags, win2k uses 0x1000 and 0x2000
                 * in the high word for internal purposes
                 */
                wParam = winpos->flags & 0xffff;
                break;
            }

            case WM_MDIACTIVATE:
            {
                HWND active, client = GetParent(hwnd);

                active = (HWND)SendMessageA(client, WM_MDIGETACTIVE, 0, 0);

                if (hwnd == (HWND)lParam) /* if we are being activated */
                    ok (active == (HWND)lParam, "new active %p != active %p\n", (HWND)lParam, active);
                else
                    ok (active == (HWND)wParam, "old active %p != active %p\n", (HWND)wParam, active);
                break;
            }
        }

        msg.message = message;
        msg.flags = sent|wparam|lparam;
        if (defwndproc_counter) msg.flags |= defwinproc;
        msg.wParam = wParam;
        msg.lParam = lParam;
        add_message(&msg);
    }

    defwndproc_counter++;
    ret = DefMDIChildProcA(hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static LRESULT WINAPI mdi_frame_wnd_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    LRESULT ret;
    struct message msg;

    /* do not log painting messages */
    if (message != WM_PAINT &&
        message != WM_ERASEBKGND &&
        message != WM_NCPAINT &&
        message != WM_GETTEXT)
    {
        trace("mdi frame: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

        switch (message)
        {
            case WM_WINDOWPOSCHANGING:
            case WM_WINDOWPOSCHANGED:
            {
                WINDOWPOS *winpos = (WINDOWPOS *)lParam;

                trace("%s\n", (message == WM_WINDOWPOSCHANGING) ? "WM_WINDOWPOSCHANGING" : "WM_WINDOWPOSCHANGED");
                trace("%p after %p, x %d, y %d, cx %d, cy %d flags %08x\n",
                      winpos->hwnd, winpos->hwndInsertAfter,
                      winpos->x, winpos->y, winpos->cx, winpos->cy, winpos->flags);

                /* Log only documented flags, win2k uses 0x1000 and 0x2000
                 * in the high word for internal purposes
                 */
                wParam = winpos->flags & 0xffff;
                break;
            }
        }

        msg.message = message;
        msg.flags = sent|wparam|lparam;
        if (defwndproc_counter) msg.flags |= defwinproc;
        msg.wParam = wParam;
        msg.lParam = lParam;
        add_message(&msg);
    }

    defwndproc_counter++;
    ret = DefFrameProcA(hwnd, mdi_client, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static BOOL mdi_RegisterWindowClasses(void)
{
    WNDCLASSA cls;

    cls.style = 0;
    cls.lpfnWndProc = mdi_frame_wnd_proc;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = GetModuleHandleA(0);
    cls.hIcon = 0;
    cls.hCursor = LoadCursorA(0, (LPSTR)IDC_ARROW);
    cls.hbrBackground = GetStockObject(WHITE_BRUSH);
    cls.lpszMenuName = NULL;
    cls.lpszClassName = "MDI_frame_class";
    if (!RegisterClassA(&cls)) return FALSE;

    cls.lpfnWndProc = mdi_child_wnd_proc;
    cls.lpszClassName = "MDI_child_class";
    if (!RegisterClassA(&cls)) return FALSE;

    if (!GetClassInfoA(0, "MDIClient", &cls)) assert(0);
    old_mdi_client_proc = cls.lpfnWndProc;
    cls.hInstance = GetModuleHandleA(0);
    cls.lpfnWndProc = mdi_client_hook_proc;
    cls.lpszClassName = "MDI_client_class";
    if (!RegisterClassA(&cls)) assert(0);

    return TRUE;
}

static void test_mdi_messages(void)
{
    MDICREATESTRUCTA mdi_cs;
    CLIENTCREATESTRUCT client_cs;
    HWND mdi_frame, mdi_child, mdi_child2, active_child;
    BOOL zoomed;
    HMENU hMenu = CreateMenu();

    assert(mdi_RegisterWindowClasses());

    flush_sequence();

    trace("creating MDI frame window\n");
    mdi_frame = CreateWindowExA(0, "MDI_frame_class", "MDI frame window",
                                WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                WS_MAXIMIZEBOX | WS_VISIBLE,
                                100, 100, CW_USEDEFAULT, CW_USEDEFAULT,
                                GetDesktopWindow(), hMenu,
                                GetModuleHandleA(0), NULL);
    assert(mdi_frame);
    ok_sequence(WmCreateMDIframeSeq, "Create MDI frame window", TRUE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_frame, "wrong focus window %p\n", GetFocus());

    trace("creating MDI client window\n");
    client_cs.hWindowMenu = 0;
    client_cs.idFirstChild = MDI_FIRST_CHILD_ID;
    mdi_client = CreateWindowExA(0, "MDI_client_class",
                                 NULL,
                                 WS_CHILD | WS_VISIBLE | MDIS_ALLCHILDSTYLES,
                                 0, 0, 0, 0,
                                 mdi_frame, 0, GetModuleHandleA(0), &client_cs);
    assert(mdi_client);
    ok_sequence(WmCreateMDIclientSeq, "Create visible MDI client window", FALSE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_frame, "input focus should be on MDI frame not on %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(!active_child, "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);

    SetFocus(0);
    flush_sequence();

    trace("creating invisible MDI child window\n");
    mdi_child = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child);

    flush_sequence();
    ShowWindow(mdi_child, SW_SHOWNORMAL);
    ok_sequence(WmShowChildSeq, "ShowWindow(SW_SHOWNORMAL) MDI child window", FALSE);

    ok(GetWindowLongA(mdi_child, GWL_STYLE) & WS_VISIBLE, "MDI child should be visible\n");
    ok(IsWindowVisible(mdi_child), "MDI child should be visible\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(!active_child, "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);

    ShowWindow(mdi_child, SW_HIDE);
    ok_sequence(WmHideChildSeq, "ShowWindow(SW_HIDE) MDI child window", FALSE);
    flush_sequence();

    ShowWindow(mdi_child, SW_SHOW);
    ok_sequence(WmShowChildSeq, "ShowWindow(SW_SHOW) MDI child window", FALSE);

    ok(GetWindowLongA(mdi_child, GWL_STYLE) & WS_VISIBLE, "MDI child should be visible\n");
    ok(IsWindowVisible(mdi_child), "MDI child should be visible\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(!active_child, "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);

    DestroyWindow(mdi_child);
    flush_sequence();

    trace("creating visible MDI child window\n");
    mdi_child = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD | WS_VISIBLE,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child);
    ok_sequence(WmCreateMDIchildVisibleSeq, "Create visible MDI child window", TRUE);

    ok(GetWindowLongA(mdi_child, GWL_STYLE) & WS_VISIBLE, "MDI child should be visible\n");
    ok(IsWindowVisible(mdi_child), "MDI child should be visible\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child, "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    DestroyWindow(mdi_child);
    ok_sequence(WmDestroyMDIchildVisibleSeq, "Destroy visible MDI child window", TRUE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    /* Win2k: MDI client still returns a just destroyed child as active
     * Win9x: MDI client returns 0
     */
    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child || /* win2k */
       !active_child, /* win9x */
       "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);

    flush_sequence();

    trace("creating invisible MDI child window\n");
    mdi_child2 = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child2);
    ok_sequence(WmCreateMDIchildInvisibleSeq, "Create invisible MDI child window", FALSE);

    ok(!(GetWindowLongA(mdi_child2, GWL_STYLE) & WS_VISIBLE), "MDI child should not be visible\n");
    ok(!IsWindowVisible(mdi_child2), "MDI child should not be visible\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    /* Win2k: MDI client still returns a just destroyed child as active
     * Win9x: MDI client returns mdi_child2
     */
    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child || /* win2k */
       active_child == mdi_child2, /* win9x */
       "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    ShowWindow(mdi_child2, SW_MAXIMIZE);
    ok_sequence(WmMaximizeMDIchildInvisibleSeq, "ShowWindow(SW_MAXIMIZE):invisible MDI child", TRUE);

    ok(GetWindowLongA(mdi_child2, GWL_STYLE) & WS_VISIBLE, "MDI child should be visible\n");
    ok(IsWindowVisible(mdi_child2), "MDI child should be visible\n");

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child2, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child2 || /* win2k */
       GetFocus() == 0, /* win9x */
       "wrong focus window %p\n", GetFocus());

    SetFocus(0);
    flush_sequence();

    ShowWindow(mdi_child2, SW_HIDE);
    ok_sequence(WmHideChildSeq, "ShowWindow(SW_HIDE):MDI child", FALSE);

    ShowWindow(mdi_child2, SW_RESTORE);
    ok_sequence(WmRestoreMDIchildInisibleSeq, "ShowWindow(SW_RESTORE):invisible MDI child", TRUE);
    flush_sequence();

    ok(GetWindowLongA(mdi_child2, GWL_STYLE) & WS_VISIBLE, "MDI child should be visible\n");
    ok(IsWindowVisible(mdi_child2), "MDI child should be visible\n");

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child2, "wrong active MDI child %p\n", active_child);
    ok(!zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    SetFocus(0);
    flush_sequence();

    ShowWindow(mdi_child2, SW_HIDE);
    ok_sequence(WmHideChildSeq, "ShowWindow(SW_HIDE):MDI child", FALSE);

    ShowWindow(mdi_child2, SW_SHOW);
    ok_sequence(WmShowChildSeq, "ShowWindow(SW_SHOW):MDI child", FALSE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    ShowWindow(mdi_child2, SW_MAXIMIZE);
    ok_sequence(WmMaximizeMDIchildVisibleSeq, "ShowWindow(SW_MAXIMIZE):MDI child", TRUE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    ShowWindow(mdi_child2, SW_RESTORE);
    ok_sequence(WmRestoreMDIchildVisibleSeq, "ShowWindow(SW_RESTORE):MDI child", TRUE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    SetFocus(0);
    flush_sequence();

    ShowWindow(mdi_child2, SW_HIDE);
    ok_sequence(WmHideChildSeq, "ShowWindow(SW_HIDE):MDI child", FALSE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    DestroyWindow(mdi_child2);
    ok_sequence(WmDestroyMDIchildInvisibleSeq, "Destroy invisible MDI child window", FALSE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    /* test for maximized MDI children */
    trace("creating maximized visible MDI child window 1\n");
    mdi_child = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD | WS_VISIBLE | WS_MAXIMIZEBOX | WS_MAXIMIZE,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child);
    ok_sequence(WmCreateMDIchildVisibleMaxSeq1, "Create maximized visible 1st MDI child window", TRUE);
    ok(IsZoomed(mdi_child), "1st MDI child should be maximized\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child || /* win2k */
       GetFocus() == 0, /* win9x */
       "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    trace("creating maximized visible MDI child window 2\n");
    mdi_child2 = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD | WS_VISIBLE | WS_MAXIMIZEBOX | WS_MAXIMIZE,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child2);
    ok_sequence(WmCreateMDIchildVisibleMaxSeq2, "Create maximized visible 2nd MDI child 2 window", TRUE);
    ok(IsZoomed(mdi_child2), "2nd MDI child should be maximized\n");
    ok(!IsZoomed(mdi_child), "1st MDI child should NOT be maximized\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child2, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child2, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    trace("destroying maximized visible MDI child window 2\n");
    DestroyWindow(mdi_child2);
    ok_sequence(WmDestroyMDIchildVisibleSeq, "Destroy visible MDI child window", TRUE);

    ok(!IsZoomed(mdi_child), "1st MDI child should NOT be maximized\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    /* Win2k: MDI client still returns a just destroyed child as active
     * Win9x: MDI client returns 0
     */
    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child2 || /* win2k */
       !active_child, /* win9x */
       "wrong active MDI child %p\n", active_child);
    flush_sequence();

    ShowWindow(mdi_child, SW_MAXIMIZE);
    ok(IsZoomed(mdi_child), "1st MDI child should be maximized\n");
    flush_sequence();

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child, "wrong focus window %p\n", GetFocus());

    trace("re-creating maximized visible MDI child window 2\n");
    mdi_child2 = CreateWindowExA(WS_EX_MDICHILD, "MDI_child_class", "MDI child",
                                WS_CHILD | WS_VISIBLE | WS_MAXIMIZEBOX | WS_MAXIMIZE,
                                0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                mdi_client, 0, GetModuleHandleA(0), NULL);
    assert(mdi_child2);
    ok_sequence(WmCreateMDIchildVisibleMaxSeq2, "Create maximized visible 2nd MDI child 2 window", TRUE);
    ok(IsZoomed(mdi_child2), "2nd MDI child should be maximized\n");
    ok(!IsZoomed(mdi_child), "1st MDI child should NOT be maximized\n");

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child2, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child2, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    SendMessageA(mdi_child2, WM_SYSCOMMAND, SC_CLOSE, 0);
    ok_sequence(WmDestroyMDIchildVisibleMaxSeq2, "WM_SYSCOMMAND/SC_CLOSE on a visible maximized MDI child window", TRUE);
    ok(!IsWindow(mdi_child2), "MDI child 2 should be destroyed\n");

    ok(IsZoomed(mdi_child), "1st MDI child should be maximized\n");
    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    DestroyWindow(mdi_child);
    ok_sequence(WmDestroyMDIchildVisibleSeq, "Destroy visible MDI child window", TRUE);

    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == 0, "wrong focus window %p\n", GetFocus());

    /* Win2k: MDI client still returns a just destroyed child as active
     * Win9x: MDI client returns 0
     */
    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child || /* win2k */
       !active_child, /* win9x */
       "wrong active MDI child %p\n", active_child);
    flush_sequence();
    /* end of test for maximized MDI children */

    mdi_cs.szClass = "MDI_child_Class";
    mdi_cs.szTitle = "MDI child";
    mdi_cs.hOwner = GetModuleHandleA(0);
    mdi_cs.x = 0;
    mdi_cs.y = 0;
    mdi_cs.cx = CW_USEDEFAULT;
    mdi_cs.cy = CW_USEDEFAULT;
    mdi_cs.style = WS_CHILD | WS_SYSMENU | WS_VISIBLE | WS_MAXIMIZEBOX | WS_MAXIMIZE;
    mdi_cs.lParam = 0;
    mdi_child = (HWND)SendMessageA(mdi_client, WM_MDICREATE, 0, (LPARAM)&mdi_cs);
    ok(mdi_child != 0, "MDI child creation failed\n");
    ok_sequence(WmCreateMDIchildVisibleMaxSeq3, "WM_MDICREATE for maximized visible MDI child window", TRUE);

    ok(GetMenuItemID(hMenu, GetMenuItemCount(hMenu) - 1) == SC_CLOSE, "SC_CLOSE menu item not found\n");

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child, "wrong active MDI child %p\n", active_child);

    ok(IsZoomed(mdi_child), "MDI child should be maximized\n");
    ok(GetActiveWindow() == mdi_frame, "wrong active window %p\n", GetActiveWindow());
    ok(GetFocus() == mdi_child, "wrong focus window %p\n", GetFocus());

    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(active_child == mdi_child, "wrong active MDI child %p\n", active_child);
    ok(zoomed, "wrong zoomed state %d\n", zoomed);
    flush_sequence();

    SendMessageA(mdi_client, WM_MDIDESTROY, (WPARAM)mdi_child, 0);
    ok_sequence(WmDestroyMDIchildVisibleMaxSeq1, "Destroy visible maximized MDI child window", TRUE);

    ok(!IsWindow(mdi_child), "MDI child should be destroyed\n");
    active_child = (HWND)SendMessageA(mdi_client, WM_MDIGETACTIVE, 0, (LPARAM)&zoomed);
    ok(!active_child, "wrong active MDI child %p\n", active_child);

    SetFocus(0);
    flush_sequence();

    DestroyWindow(mdi_client);
    ok_sequence(WmDestroyMDIclientSeq, "Destroy MDI client window", FALSE);

    DestroyWindow(mdi_frame);
    ok_sequence(WmDestroyMDIframeSeq, "Destroy MDI frame window", FALSE);
}
/************************* End of MDI test **********************************/

static void test_WM_SETREDRAW(HWND hwnd)
{
    DWORD style = GetWindowLongA(hwnd, GWL_STYLE);

    flush_sequence();

    SendMessageA(hwnd, WM_SETREDRAW, FALSE, 0);
    ok_sequence(WmSetRedrawFalseSeq, "SetRedraw:FALSE", FALSE);

    ok(!(GetWindowLongA(hwnd, GWL_STYLE) & WS_VISIBLE), "WS_VISIBLE should NOT be set\n");
    ok(!IsWindowVisible(hwnd), "IsWindowVisible() should return FALSE\n");

    flush_sequence();
    SendMessageA(hwnd, WM_SETREDRAW, TRUE, 0);
    ok_sequence(WmSetRedrawTrueSeq, "SetRedraw:TRUE", FALSE);

    ok(GetWindowLongA(hwnd, GWL_STYLE) & WS_VISIBLE, "WS_VISIBLE should be set\n");
    ok(IsWindowVisible(hwnd), "IsWindowVisible() should return TRUE\n");

    /* restore original WS_VISIBLE state */
    SetWindowLongA(hwnd, GWL_STYLE, style);

    flush_sequence();
}

static INT_PTR CALLBACK TestModalDlgProcA(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct message msg;

    trace("dialog: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    msg.wParam = wParam;
    msg.lParam = lParam;
    add_message(&msg);

    if (message == WM_INITDIALOG) SetTimer( hwnd, 1, 100, NULL );
    if (message == WM_TIMER) EndDialog( hwnd, 0 );
    return 0;
}

static void test_hv_scroll_1(HWND hwnd, INT ctl, DWORD clear, DWORD set, INT min, INT max)
{
    DWORD style, exstyle;
    INT xmin, xmax;

    exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    style = GetWindowLongA(hwnd, GWL_STYLE);
    /* do not be confused by WS_DLGFRAME set */
    if ((style & WS_CAPTION) == WS_CAPTION) style &= ~WS_CAPTION;

    if (clear) ok(style & clear, "style %08lx should be set\n", clear);
    if (set) ok(!(style & set), "style %08lx should not be set\n", set);

    ok(SetScrollRange(hwnd, ctl, min, max, FALSE), "SetScrollRange(%d) error %ld\n", ctl, GetLastError());
    if ((style & (WS_DLGFRAME | WS_BORDER | WS_THICKFRAME)) || (exstyle & WS_EX_DLGMODALFRAME))
        ok_sequence(WmSetScrollRangeHV_NC_Seq, "SetScrollRange(SB_HORZ/SB_VERT) NC", FALSE);
    else
        ok_sequence(WmSetScrollRangeHVSeq, "SetScrollRange(SB_HORZ/SB_VERT)", FALSE);

    style = GetWindowLongA(hwnd, GWL_STYLE);
    if (set) ok(style & set, "style %08lx should be set\n", set);
    if (clear) ok(!(style & clear), "style %08lx should not be set\n", clear);

    /* a subsequent call should do nothing */
    ok(SetScrollRange(hwnd, ctl, min, max, FALSE), "SetScrollRange(%d) error %ld\n", ctl, GetLastError());
    ok_sequence(WmEmptySeq, "SetScrollRange(SB_HORZ/SB_VERT)", FALSE);

    xmin = 0xdeadbeef;
    xmax = 0xdeadbeef;
    trace("Ignore GetScrollRange error below if you are on Win9x\n");
    ok(GetScrollRange(hwnd, ctl, &xmin, &xmax), "GetScrollRange(%d) error %ld\n", ctl, GetLastError());
    ok_sequence(WmEmptySeq, "GetScrollRange(SB_HORZ/SB_VERT)", FALSE);
    ok(xmin == min, "unexpected min scroll value %d\n", xmin);
    ok(xmax == max, "unexpected max scroll value %d\n", xmax);
}

static void test_hv_scroll_2(HWND hwnd, INT ctl, DWORD clear, DWORD set, INT min, INT max)
{
    DWORD style, exstyle;
    SCROLLINFO si;

    exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    style = GetWindowLongA(hwnd, GWL_STYLE);
    /* do not be confused by WS_DLGFRAME set */
    if ((style & WS_CAPTION) == WS_CAPTION) style &= ~WS_CAPTION;

    if (clear) ok(style & clear, "style %08lx should be set\n", clear);
    if (set) ok(!(style & set), "style %08lx should not be set\n", set);

    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE;
    si.nMin = min;
    si.nMax = max;
    SetScrollInfo(hwnd, ctl, &si, TRUE);
    if ((style & (WS_DLGFRAME | WS_BORDER | WS_THICKFRAME)) || (exstyle & WS_EX_DLGMODALFRAME))
        ok_sequence(WmSetScrollRangeHV_NC_Seq, "SetScrollInfo(SB_HORZ/SB_VERT) NC", FALSE);
    else
        ok_sequence(WmSetScrollRangeHVSeq, "SetScrollInfo(SB_HORZ/SB_VERT)", FALSE);

    style = GetWindowLongA(hwnd, GWL_STYLE);
    if (set) ok(style & set, "style %08lx should be set\n", set);
    if (clear) ok(!(style & clear), "style %08lx should not be set\n", clear);

    /* a subsequent call should do nothing */
    SetScrollInfo(hwnd, ctl, &si, TRUE);
    ok_sequence(WmEmptySeq, "SetScrollInfo(SB_HORZ/SB_VERT)", FALSE);

    si.fMask = SIF_PAGE;
    si.nPage = 5;
    SetScrollInfo(hwnd, ctl, &si, FALSE);
    ok_sequence(WmEmptySeq, "SetScrollInfo(SB_HORZ/SB_VERT)", FALSE);

    si.fMask = SIF_POS;
    si.nPos = max - 1;
    SetScrollInfo(hwnd, ctl, &si, FALSE);
    ok_sequence(WmEmptySeq, "SetScrollInfo(SB_HORZ/SB_VERT)", FALSE);

    si.fMask = SIF_RANGE;
    si.nMin = 0xdeadbeef;
    si.nMax = 0xdeadbeef;
    ok(GetScrollInfo(hwnd, ctl, &si), "GetScrollInfo error %ld\n", GetLastError());
    ok_sequence(WmEmptySeq, "GetScrollRange(SB_HORZ/SB_VERT)", FALSE);
    ok(si.nMin == min, "unexpected min scroll value %d\n", si.nMin);
    ok(si.nMax == max, "unexpected max scroll value %d\n", si.nMax);
}

/* Win9x sends WM_USER+xxx while and NT versions send SBM_xxx messages */
static void test_scroll_messages(HWND hwnd)
{
    SCROLLINFO si;
    INT min, max;

    min = 0xdeadbeef;
    max = 0xdeadbeef;
    ok(GetScrollRange(hwnd, SB_CTL, &min, &max), "GetScrollRange error %ld\n", GetLastError());
    if (sequence->message != WmGetScrollRangeSeq[0].message)
        trace("GetScrollRange(SB_CTL) generated unknown message %04x\n", sequence->message);
    /* values of min and max are undefined */
    flush_sequence();

    ok(SetScrollRange(hwnd, SB_CTL, 10, 150, FALSE), "SetScrollRange error %ld\n", GetLastError());
    if (sequence->message != WmSetScrollRangeSeq[0].message)
        trace("SetScrollRange(SB_CTL) generated unknown message %04x\n", sequence->message);
    flush_sequence();

    min = 0xdeadbeef;
    max = 0xdeadbeef;
    ok(GetScrollRange(hwnd, SB_CTL, &min, &max), "GetScrollRange error %ld\n", GetLastError());
    if (sequence->message != WmGetScrollRangeSeq[0].message)
        trace("GetScrollRange(SB_CTL) generated unknown message %04x\n", sequence->message);
    /* values of min and max are undefined */
    flush_sequence();

    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE;
    si.nMin = 20;
    si.nMax = 160;
    SetScrollInfo(hwnd, SB_CTL, &si, FALSE);
    if (sequence->message != WmSetScrollRangeSeq[0].message)
        trace("SetScrollInfo(SB_CTL) generated unknown message %04x\n", sequence->message);
    flush_sequence();

    si.fMask = SIF_PAGE;
    si.nPage = 10;
    SetScrollInfo(hwnd, SB_CTL, &si, FALSE);
    if (sequence->message != WmSetScrollRangeSeq[0].message)
        trace("SetScrollInfo(SB_CTL) generated unknown message %04x\n", sequence->message);
    flush_sequence();

    si.fMask = SIF_POS;
    si.nPos = 20;
    SetScrollInfo(hwnd, SB_CTL, &si, FALSE);
    if (sequence->message != WmSetScrollRangeSeq[0].message)
        trace("SetScrollInfo(SB_CTL) generated unknown message %04x\n", sequence->message);
    flush_sequence();

    si.fMask = SIF_RANGE;
    si.nMin = 0xdeadbeef;
    si.nMax = 0xdeadbeef;
    ok(GetScrollInfo(hwnd, SB_CTL, &si), "GetScrollInfo error %ld\n", GetLastError());
    if (sequence->message != WmGetScrollInfoSeq[0].message)
        trace("GetScrollInfo(SB_CTL) generated unknown message %04x\n", sequence->message);
    /* values of min and max are undefined */
    flush_sequence();

    /* set WS_HSCROLL */
    test_hv_scroll_1(hwnd, SB_HORZ, 0, WS_HSCROLL, 10, 150);
    /* clear WS_HSCROLL */
    test_hv_scroll_1(hwnd, SB_HORZ, WS_HSCROLL, 0, 0, 0);

    /* set WS_HSCROLL */
    test_hv_scroll_2(hwnd, SB_HORZ, 0, WS_HSCROLL, 10, 150);
    /* clear WS_HSCROLL */
    test_hv_scroll_2(hwnd, SB_HORZ, WS_HSCROLL, 0, 0, 0);

    /* set WS_VSCROLL */
    test_hv_scroll_1(hwnd, SB_VERT, 0, WS_VSCROLL, 10, 150);
    /* clear WS_VSCROLL */
    test_hv_scroll_1(hwnd, SB_VERT, WS_VSCROLL, 0, 0, 0);

    /* set WS_VSCROLL */
    test_hv_scroll_2(hwnd, SB_VERT, 0, WS_VSCROLL, 10, 150);
    /* clear WS_VSCROLL */
    test_hv_scroll_2(hwnd, SB_VERT, WS_VSCROLL, 0, 0, 0);
}

/* test if we receive the right sequence of messages */
static void test_messages(void)
{
    HWND hwnd, hparent, hchild;
    HWND hchild2, hbutton;
    HMENU hmenu;
    MSG msg;

    hwnd = CreateWindowExA(0, "TestWindowClass", "Test overlapped", WS_OVERLAPPEDWINDOW,
                           100, 100, 200, 200, 0, 0, 0, NULL);
    ok (hwnd != 0, "Failed to create overlapped window\n");
    ok_sequence(WmCreateOverlappedSeq, "CreateWindow:overlapped", FALSE);

    /* test ShowWindow(SW_HIDE) on a newly created invisible window */
    ok( ShowWindow(hwnd, SW_HIDE) == FALSE, "ShowWindow: window was visible\n" );
    ok_sequence(WmHideInvisibleOverlappedSeq, "ShowWindow(SW_HIDE):overlapped, invisible", FALSE);

    /* test WM_SETREDRAW on a not visible top level window */
    test_WM_SETREDRAW(hwnd);

    SetWindowPos(hwnd, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE);
    ok_sequence(WmSWP_ShowOverlappedSeq, "SetWindowPos:SWP_SHOWWINDOW:overlapped", FALSE);
    ok(IsWindowVisible(hwnd), "window should be visible at this point\n");

    ok(GetActiveWindow() == hwnd, "window should be active\n");
    ok(GetFocus() == hwnd, "window should have input focus\n");
    ShowWindow(hwnd, SW_HIDE);
    ok_sequence(WmHideOverlappedSeq, "ShowWindow(SW_HIDE):overlapped", TRUE);
    
    ShowWindow(hwnd, SW_SHOW);
    ok_sequence(WmShowOverlappedSeq, "ShowWindow(SW_SHOW):overlapped", TRUE);

    ok(GetActiveWindow() == hwnd, "window should be active\n");
    ok(GetFocus() == hwnd, "window should have input focus\n");
    SetWindowPos(hwnd, 0,0,0,0,0, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE);
    ok_sequence(WmSWP_HideOverlappedSeq, "SetWindowPos:SWP_HIDEWINDOW:overlapped", FALSE);
    ok(!IsWindowVisible(hwnd), "window should not be visible at this point\n");

    /* test WM_SETREDRAW on a visible top level window */
    ShowWindow(hwnd, SW_SHOW);
    test_WM_SETREDRAW(hwnd);

    trace("testing scroll APIs on a visible top level window %p\n", hwnd);
    test_scroll_messages(hwnd);

    DestroyWindow(hwnd);
    ok_sequence(WmDestroyOverlappedSeq, "DestroyWindow:overlapped", FALSE);

    hparent = CreateWindowExA(0, "TestParentClass", "Test parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              100, 100, 200, 200, 0, 0, 0, NULL);
    ok (hparent != 0, "Failed to create parent window\n");
    flush_sequence();

    hchild = CreateWindowExA(0, "TestWindowClass", "Test child", WS_CHILD | WS_MAXIMIZE,
                             0, 0, 10, 10, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");
    ok_sequence(WmCreateMaximizedChildSeq, "CreateWindow:maximized child", TRUE);
    DestroyWindow(hchild);
    flush_sequence();

    /* visible child window with a caption */
    hchild = CreateWindowExA(0, "TestWindowClass", "Test child",
                             WS_CHILD | WS_VISIBLE | WS_CAPTION,
                             0, 0, 10, 10, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");
    ok_sequence(WmCreateVisibleChildSeq, "CreateWindow:visible child", FALSE);

    trace("testing scroll APIs on a visible child window %p\n", hchild);
    test_scroll_messages(hchild);

    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE);
    ok_sequence(WmShowChildSeq_4, "SetWindowPos(SWP_SHOWWINDOW):child with a caption", FALSE);

    DestroyWindow(hchild);
    flush_sequence();

    hchild = CreateWindowExA(0, "TestWindowClass", "Test child", WS_CHILD,
                             0, 0, 10, 10, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");
    ok_sequence(WmCreateChildSeq, "CreateWindow:child", FALSE);
    
    hchild2 = CreateWindowExA(0, "SimpleWindowClass", "Test child2", WS_CHILD,
                               100, 100, 50, 50, hparent, 0, 0, NULL);
    ok (hchild2 != 0, "Failed to create child2 window\n");
    flush_sequence();

    hbutton = CreateWindowExA(0, "TestWindowClass", "Test button", WS_CHILD,
                              0, 100, 50, 50, hchild, 0, 0, NULL);
    ok (hbutton != 0, "Failed to create button window\n");

    /* test WM_SETREDRAW on a not visible child window */
    test_WM_SETREDRAW(hchild);

    ShowWindow(hchild, SW_SHOW);
    ok_sequence(WmShowChildSeq, "ShowWindow(SW_SHOW):child", FALSE);

    ShowWindow(hchild, SW_HIDE);
    ok_sequence(WmHideChildSeq, "ShowWindow(SW_HIDE):child", FALSE);

    ShowWindow(hchild, SW_SHOW);
    ok_sequence(WmShowChildSeq, "ShowWindow(SW_SHOW):child", FALSE);

    /* test WM_SETREDRAW on a visible child window */
    test_WM_SETREDRAW(hchild);

    MoveWindow(hchild, 10, 10, 20, 20, TRUE);
    ok_sequence(WmResizingChildWithMoveWindowSeq, "MoveWindow:child", FALSE);

    ShowWindow(hchild, SW_HIDE);
    flush_sequence();
    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE);
    ok_sequence(WmShowChildSeq_2, "SetWindowPos:show_child_2", FALSE);

    ShowWindow(hchild, SW_HIDE);
    flush_sequence();
    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
    ok_sequence(WmShowChildSeq_3, "SetWindowPos:show_child_3", FALSE);

    /* DestroyWindow sequence below expects that a child has focus */
    SetFocus(hchild);
    flush_sequence();

    DestroyWindow(hchild);
    ok_sequence(WmDestroyChildSeq, "DestroyWindow:child", FALSE);
    DestroyWindow(hchild2);
    DestroyWindow(hbutton);

    flush_sequence();
    hchild = CreateWindowExA(0, "TestWindowClass", "Test Child Popup", WS_CHILD | WS_POPUP,
                             0, 0, 100, 100, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child popup window\n");
    ok_sequence(WmCreateChildPopupSeq, "CreateWindow:child_popup", FALSE);
    DestroyWindow(hchild);

    /* test what happens to a window which sets WS_VISIBLE in WM_CREATE */
    flush_sequence();
    hchild = CreateWindowExA(0, "TestPopupClass", "Test Popup", WS_POPUP,
                             0, 0, 100, 100, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create popup window\n");
    ok_sequence(WmCreateInvisiblePopupSeq, "CreateWindow:invisible_popup", FALSE);
    ok(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE, "WS_VISIBLE should be set\n");
    ok(IsWindowVisible(hchild), "IsWindowVisible() should return TRUE\n");
    flush_sequence();
    ShowWindow(hchild, SW_SHOW);
    ok_sequence(WmEmptySeq, "ShowWindow:show_visible_popup", FALSE);
    flush_sequence();
    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    ok_sequence(WmShowVisiblePopupSeq_2, "SetWindowPos:show_visible_popup_2", FALSE);
    flush_sequence();
    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE);
    ok_sequence(WmShowVisiblePopupSeq_3, "SetWindowPos:show_visible_popup_3", FALSE);
    DestroyWindow(hchild);

    /* this time add WS_VISIBLE for CreateWindowEx, but this fact actually
     * changes nothing in message sequences.
     */
    flush_sequence();
    hchild = CreateWindowExA(0, "TestPopupClass", "Test Popup", WS_POPUP | WS_VISIBLE,
                             0, 0, 100, 100, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create popup window\n");
    ok_sequence(WmCreateInvisiblePopupSeq, "CreateWindow:invisible_popup", FALSE);
    ok(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE, "WS_VISIBLE should be set\n");
    ok(IsWindowVisible(hchild), "IsWindowVisible() should return TRUE\n");
    flush_sequence();
    ShowWindow(hchild, SW_SHOW);
    ok_sequence(WmEmptySeq, "ShowWindow:show_visible_popup", FALSE);
    flush_sequence();
    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    ok_sequence(WmShowVisiblePopupSeq_2, "SetWindowPos:show_visible_popup_2", FALSE);
    DestroyWindow(hchild);

    flush_sequence();
    hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "TestDialogClass", NULL, WS_VISIBLE|WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,
                           0, 0, 100, 100, hparent, 0, 0, NULL);
    ok(hwnd != 0, "Failed to create custom dialog window\n");
    ok_sequence(WmCreateCustomDialogSeq, "CreateCustomDialog", TRUE);

    trace("testing scroll APIs on a visible dialog %p\n", hwnd);
    test_scroll_messages(hwnd);

    flush_sequence();
    after_end_dialog = 1;
    EndDialog( hwnd, 0 );
    ok_sequence(WmEndCustomDialogSeq, "EndCustomDialog", FALSE);

    DestroyWindow(hwnd);
    after_end_dialog = 0;

    flush_sequence();
    DialogBoxA( 0, "TEST_DIALOG", hparent, TestModalDlgProcA );
    ok_sequence(WmModalDialogSeq, "ModalDialog", TRUE);

    /* test showing child with hidden parent */
    ShowWindow( hparent, SW_HIDE );
    flush_sequence();

    hchild = CreateWindowExA(0, "TestWindowClass", "Test child", WS_CHILD,
                             0, 0, 10, 10, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");
    ok_sequence(WmCreateChildSeq, "CreateWindow:child", FALSE);

    ShowWindow( hchild, SW_SHOW );
    ok_sequence(WmShowChildInvisibleParentSeq, "ShowWindow:show child with invisible parent", TRUE);
    ok(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE, "WS_VISIBLE should be set\n");
    ok(!IsWindowVisible(hchild), "IsWindowVisible() should return FALSE\n");

    ShowWindow( hchild, SW_HIDE );
    ok_sequence(WmHideChildInvisibleParentSeq, "ShowWindow:hide child with invisible parent", TRUE);
    ok(!(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE), "WS_VISIBLE should be not set\n");
    ok(!IsWindowVisible(hchild), "IsWindowVisible() should return FALSE\n");

    SetWindowPos(hchild, 0,0,0,0,0, SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    ok_sequence(WmShowChildInvisibleParentSeq_2, "SetWindowPos:show child with invisible parent", TRUE);
    ok(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE, "WS_VISIBLE should be set\n");
    ok(!IsWindowVisible(hchild), "IsWindowVisible() should return FALSE\n");

    SetWindowPos(hchild, 0,0,0,0,0, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    ok_sequence(WmHideChildInvisibleParentSeq_2, "SetWindowPos:hide child with invisible parent", TRUE);
    ok(!(GetWindowLongA(hchild, GWL_STYLE) & WS_VISIBLE), "WS_VISIBLE should not be set\n");
    ok(!IsWindowVisible(hchild), "IsWindowVisible() should return FALSE\n");

    DestroyWindow(hchild);
    DestroyWindow(hparent);
    flush_sequence();

    /* Message sequence for SetMenu */
    ok(!DrawMenuBar(hwnd), "DrawMenuBar should return FALSE for a window without a menu\n");
    ok_sequence(WmEmptySeq, "DrawMenuBar for a window without a menu", FALSE);

    hmenu = CreateMenu();
    ok (hmenu != 0, "Failed to create menu\n");
    ok (InsertMenuA(hmenu, -1, MF_BYPOSITION, 0x1000, "foo"), "InsertMenu failed\n");
    hwnd = CreateWindowExA(0, "TestWindowClass", "Test overlapped", WS_OVERLAPPEDWINDOW,
                           100, 100, 200, 200, 0, hmenu, 0, NULL);
    ok_sequence(WmCreateOverlappedSeq, "CreateWindow:overlapped", FALSE);
    ok (SetMenu(hwnd, 0), "SetMenu\n");
    ok_sequence(WmSetMenuNonVisibleSizeChangeSeq, "SetMenu:NonVisibleSizeChange", FALSE);
    ok (SetMenu(hwnd, 0), "SetMenu\n");
    ok_sequence(WmSetMenuNonVisibleNoSizeChangeSeq, "SetMenu:NonVisibleNoSizeChange", FALSE);
    ShowWindow(hwnd, SW_SHOW);
    flush_sequence();
    ok (SetMenu(hwnd, 0), "SetMenu\n");
    ok_sequence(WmSetMenuVisibleNoSizeChangeSeq, "SetMenu:VisibleNoSizeChange", TRUE);
    ok (SetMenu(hwnd, hmenu), "SetMenu\n");
    ok_sequence(WmSetMenuVisibleSizeChangeSeq, "SetMenu:VisibleSizeChange", TRUE);

    ok(DrawMenuBar(hwnd), "DrawMenuBar\n");
    ok_sequence(WmDrawMenuBarSeq, "DrawMenuBar", TRUE);

    DestroyWindow(hwnd);
    flush_sequence();

    /* Message sequence for EnableWindow */
    hparent = CreateWindowExA(0, "TestWindowClass", "Test parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              100, 100, 200, 200, 0, 0, 0, NULL);
    ok (hparent != 0, "Failed to create parent window\n");
    hchild = CreateWindowExA(0, "TestWindowClass", "Test child", WS_CHILD | WS_VISIBLE,
                             0, 0, 10, 10, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");

    SetFocus(hchild);
    flush_sequence();

    EnableWindow(hparent, FALSE);
    ok_sequence(WmEnableWindowSeq, "EnableWindow", FALSE);

    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    flush_sequence();
    PostMessage( hparent, WM_USER, 0, 0 );
    PostMessage( hparent, WM_USER+1, 0, 0 );
    /* PeekMessage(NULL) fails, but still removes the message */
    SetLastError(0xdeadbeef);
    ok( !PeekMessage( NULL, 0, 0, 0, PM_REMOVE ), "PeekMessage(NULL) should fail\n" );
    ok( GetLastError() == ERROR_NOACCESS, "last error is %ld\n", GetLastError() );
    ok( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ), "PeekMessage should succeed\n" );
    ok( msg.message == WM_USER+1, "got %x instead of WM_USER+1\n", msg.message );

    DestroyWindow(hchild);
    DestroyWindow(hparent);
    flush_sequence();
}

/****************** button message test *************************/
static const struct message WmSetFocusButtonSeq[] =
{
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent|wparam, 0 },
    { WM_CTLCOLORBTN, sent|defwinproc },
    { 0 }
};
static const struct message WmKillFocusButtonSeq[] =
{
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|wparam, 0 },
    { WM_CTLCOLORBTN, sent|defwinproc },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { 0 }
};
static const struct message WmSetFocusStaticSeq[] =
{
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 1 },
    { WM_SETFOCUS, sent|wparam, 0 },
    { WM_CTLCOLORSTATIC, sent|defwinproc },
    { 0 }
};
static const struct message WmKillFocusStaticSeq[] =
{
    { HCBT_SETFOCUS, hook },
    { WM_KILLFOCUS, sent|wparam, 0 },
    { WM_CTLCOLORSTATIC, sent|defwinproc },
    { WM_IME_SETCONTEXT, sent|wparam|optional, 0 },
    { 0 }
};
static const struct message WmLButtonDownSeq[] =
{
    { WM_LBUTTONDOWN, sent|wparam|lparam, 0, 0 },
    { HCBT_SETFOCUS, hook },
    { WM_IME_SETCONTEXT, sent|wparam|defwinproc|optional, 1 },
    { WM_SETFOCUS, sent|wparam|defwinproc, 0 },
    { WM_CTLCOLORBTN, sent|defwinproc },
    { BM_SETSTATE, sent|wparam|defwinproc, TRUE },
    { WM_CTLCOLORBTN, sent|defwinproc },
    { 0 }
};
static const struct message WmLButtonUpSeq[] =
{
    { WM_LBUTTONUP, sent|wparam|lparam, 0, 0 },
    { BM_SETSTATE, sent|wparam|defwinproc, FALSE },
    { WM_CTLCOLORBTN, sent|defwinproc },
    { WM_CAPTURECHANGED, sent|wparam|defwinproc, 0 },
    { 0 }
};

static WNDPROC old_button_proc;

static LRESULT CALLBACK button_hook_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    LRESULT ret;
    struct message msg;

    trace("button: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    if (defwndproc_counter) msg.flags |= defwinproc;
    msg.wParam = wParam;
    msg.lParam = lParam;
    add_message(&msg);

    if (message == BM_SETSTATE)
	ok(GetCapture() == hwnd, "GetCapture() = %p\n", GetCapture());

    defwndproc_counter++;
    ret = CallWindowProcA(old_button_proc, hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static void subclass_button(void)
{
    WNDCLASSA cls;

    if (!GetClassInfoA(0, "button", &cls)) assert(0);

    old_button_proc = cls.lpfnWndProc;

    cls.hInstance = GetModuleHandle(0);
    cls.lpfnWndProc = button_hook_proc;
    cls.lpszClassName = "my_button_class";
    if (!RegisterClassA(&cls)) assert(0);
}

static void test_button_messages(void)
{
    static const struct
    {
	DWORD style;
	DWORD dlg_code;
	const struct message *setfocus;
	const struct message *killfocus;
    } button[] = {
	{ BS_PUSHBUTTON, DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON,
	  WmSetFocusButtonSeq, WmKillFocusButtonSeq },
	{ BS_DEFPUSHBUTTON, DLGC_BUTTON | DLGC_DEFPUSHBUTTON,
	  WmSetFocusButtonSeq, WmKillFocusButtonSeq },
	{ BS_CHECKBOX, DLGC_BUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_AUTOCHECKBOX, DLGC_BUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_RADIOBUTTON, DLGC_BUTTON | DLGC_RADIOBUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_3STATE, DLGC_BUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_AUTO3STATE, DLGC_BUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_GROUPBOX, DLGC_STATIC,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_USERBUTTON, DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON,
	  WmSetFocusButtonSeq, WmKillFocusButtonSeq },
	{ BS_AUTORADIOBUTTON, DLGC_BUTTON | DLGC_RADIOBUTTON,
	  WmSetFocusStaticSeq, WmKillFocusStaticSeq },
	{ BS_OWNERDRAW, DLGC_BUTTON,
	  WmSetFocusButtonSeq, WmKillFocusButtonSeq }
    };
    unsigned int i;
    HWND hwnd;
    DWORD dlg_code;

    subclass_button();

    for (i = 0; i < sizeof(button)/sizeof(button[0]); i++)
    {
	hwnd = CreateWindowExA(0, "my_button_class", "test", button[i].style | WS_POPUP,
			       0, 0, 50, 14, 0, 0, 0, NULL);
	ok(hwnd != 0, "Failed to create button window\n");

	dlg_code = SendMessageA(hwnd, WM_GETDLGCODE, 0, 0);
	ok(dlg_code == button[i].dlg_code, "%d: wrong dlg_code %08lx\n", i, dlg_code);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	SetFocus(0);
	flush_sequence();

	trace("button style %08lx\n", button[i].style);
	SetFocus(hwnd);
	ok_sequence(button[i].setfocus, "SetFocus(hwnd) on a button", FALSE);

	SetFocus(0);
	ok_sequence(button[i].killfocus, "SetFocus(0) on a button", FALSE);

	DestroyWindow(hwnd);
    }

    hwnd = CreateWindowExA(0, "my_button_class", "test", BS_PUSHBUTTON | WS_POPUP | WS_VISIBLE,
			   0, 0, 50, 14, 0, 0, 0, NULL);
    ok(hwnd != 0, "Failed to create button window\n");

    SetFocus(0);
    flush_sequence();

    SendMessageA(hwnd, WM_LBUTTONDOWN, 0, 0);
    ok_sequence(WmLButtonDownSeq, "WM_LBUTTONDOWN on a button", FALSE);

    SendMessageA(hwnd, WM_LBUTTONUP, 0, 0);
    ok_sequence(WmLButtonUpSeq, "WM_LBUTTONDOWN on a button", FALSE);
    DestroyWindow(hwnd);
}

/************* painting message test ********************/

static void dump_region(HRGN hrgn)
{
    DWORD i, size;
    RGNDATA *data = NULL;
    RECT *rect;

    if (!hrgn)
    {
        printf( "null region\n" );
        return;
    }
    if (!(size = GetRegionData( hrgn, 0, NULL ))) return;
    if (!(data = HeapAlloc( GetProcessHeap(), 0, size ))) return;
    GetRegionData( hrgn, size, data );
    printf("%ld rects:", data->rdh.nCount );
    for (i = 0, rect = (RECT *)data->Buffer; i < data->rdh.nCount; i++, rect++)
        printf( " (%ld,%ld)-(%ld,%ld)", rect->left, rect->top, rect->right, rect->bottom );
    printf("\n");
    HeapFree( GetProcessHeap(), 0, data );
}

static void check_update_rgn( HWND hwnd, HRGN hrgn )
{
    INT ret;
    RECT r1, r2;
    HRGN tmp = CreateRectRgn( 0, 0, 0, 0 );
    HRGN update = CreateRectRgn( 0, 0, 0, 0 );

    ret = GetUpdateRgn( hwnd, update, FALSE );
    ok( ret != ERROR, "GetUpdateRgn failed\n" );
    if (ret == NULLREGION)
    {
        ok( !hrgn, "Update region shouldn't be empty\n" );
    }
    else
    {
        if (CombineRgn( tmp, hrgn, update, RGN_XOR ) != NULLREGION)
        {
            ok( 0, "Regions are different\n" );
            if (winetest_debug > 0)
            {
                printf( "Update region: " );
                dump_region( update );
                printf( "Wanted region: " );
                dump_region( hrgn );
            }
        }
    }
    GetRgnBox( update, &r1 );
    GetUpdateRect( hwnd, &r2, FALSE );
    ok( r1.left == r2.left && r1.top == r2.top && r1.right == r2.right && r1.bottom == r2.bottom,
        "Rectangles are different: %ld,%ld-%ld,%ld / %ld,%ld-%ld,%ld\n",
        r1.left, r1.top, r1.right, r1.bottom, r2.left, r2.top, r2.right, r2.bottom );

    DeleteObject( tmp );
    DeleteObject( update );
}

static const struct message WmInvalidateRgn[] = {
    { WM_NCPAINT, sent },
    { WM_GETTEXT, sent|defwinproc|optional },
    { 0 }
};

static const struct message WmGetUpdateRect[] = {
    { WM_NCPAINT, sent },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_PAINT, sent },
    { 0 }
};

static const struct message WmInvalidateFull[] = {
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { 0 }
};

static const struct message WmInvalidateErase[] = {
    { WM_NCPAINT, sent|wparam, 1 },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent },
    { 0 }
};

static const struct message WmInvalidatePaint[] = {
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|wparam|beginpaint, 1 },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { 0 }
};

static const struct message WmInvalidateErasePaint[] = {
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|wparam|beginpaint, 1 },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|beginpaint },
    { 0 }
};

static const struct message WmInvalidateErasePaint2[] = {
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|beginpaint },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|beginpaint },
    { 0 }
};

static const struct message WmErase[] = {
    { WM_ERASEBKGND, sent },
    { 0 }
};

static const struct message WmPaint[] = {
    { WM_PAINT, sent },
    { 0 }
};

static const struct message WmParentOnlyPaint[] = {
    { WM_PAINT, sent|parent },
    { 0 }
};

static const struct message WmInvalidateParent[] = {
    { WM_NCPAINT, sent|parent },
    { WM_GETTEXT, sent|defwinproc|parent|optional },
    { WM_ERASEBKGND, sent|parent },
    { 0 }
};

static const struct message WmInvalidateParentChild[] = {
    { WM_NCPAINT, sent|parent },
    { WM_GETTEXT, sent|defwinproc|parent|optional },
    { WM_ERASEBKGND, sent|parent },
    { WM_NCPAINT, sent },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent },
    { 0 }
};

static const struct message WmInvalidateParentChild2[] = {
    { WM_ERASEBKGND, sent|parent },
    { WM_NCPAINT, sent },
    { WM_GETTEXT, sent|defwinproc|optional },
    { WM_ERASEBKGND, sent },
    { 0 }
};

static const struct message WmParentPaint[] = {
    { WM_PAINT, sent|parent },
    { WM_PAINT, sent },
    { 0 }
};

static const struct message WmParentPaintNc[] = {
    { WM_PAINT, sent|parent },
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|beginpaint },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|beginpaint },
    { 0 }
};

static const struct message WmChildPaintNc[] = {
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|beginpaint },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|beginpaint },
    { 0 }
};

static const struct message WmParentErasePaint[] = {
    { WM_PAINT, sent|parent },
    { WM_NCPAINT, sent|parent|beginpaint },
    { WM_GETTEXT, sent|parent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|parent|beginpaint },
    { WM_PAINT, sent },
    { WM_NCPAINT, sent|beginpaint },
    { WM_GETTEXT, sent|beginpaint|defwinproc|optional },
    { WM_ERASEBKGND, sent|beginpaint },
    { 0 }
};

static const struct message WmParentOnlyNcPaint[] = {
    { WM_PAINT, sent|parent },
    { WM_NCPAINT, sent|parent|beginpaint },
    { WM_GETTEXT, sent|parent|beginpaint|defwinproc|optional },
    { 0 }
};

static const struct message WmSetParentStyle[] = {
    { WM_STYLECHANGING, sent|parent },
    { WM_STYLECHANGED, sent|parent },
    { 0 }
};

static void test_paint_messages(void)
{
    RECT rect;
    POINT pt;
    MSG msg;
    HWND hparent, hchild;
    HRGN hrgn = CreateRectRgn( 0, 0, 0, 0 );
    HRGN hrgn2 = CreateRectRgn( 0, 0, 0, 0 );
    HWND hwnd = CreateWindowExA(0, "TestWindowClass", "Test overlapped", WS_OVERLAPPEDWINDOW,
                                100, 100, 200, 200, 0, 0, 0, NULL);
    ok (hwnd != 0, "Failed to create overlapped window\n");

    ShowWindow( hwnd, SW_SHOW );
    UpdateWindow( hwnd );

    /* try to flush pending X expose events */
    MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );

    check_update_rgn( hwnd, 0 );
    SetRectRgn( hrgn, 10, 10, 20, 20 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE );
    check_update_rgn( hwnd, hrgn );
    SetRectRgn( hrgn2, 20, 20, 30, 30 );
    RedrawWindow( hwnd, NULL, hrgn2, RDW_INVALIDATE );
    CombineRgn( hrgn, hrgn, hrgn2, RGN_OR );
    check_update_rgn( hwnd, hrgn );
    /* validate everything */
    RedrawWindow( hwnd, NULL, NULL, RDW_VALIDATE );
    check_update_rgn( hwnd, 0 );
    /* now with frame */
    SetRectRgn( hrgn, -5, -5, 20, 20 );

    /* flush pending messages */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );

    flush_sequence();
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME );
    ok_sequence( WmEmptySeq, "EmptySeq", FALSE );

    SetRectRgn( hrgn, 0, 0, 20, 20 );  /* GetUpdateRgn clips to client area */
    check_update_rgn( hwnd, hrgn );

    flush_sequence();
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME | RDW_ERASENOW );
    ok_sequence( WmInvalidateRgn, "InvalidateRgn", FALSE );

    flush_sequence();
    RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASENOW );
    ok_sequence( WmInvalidateFull, "InvalidateFull", FALSE );

    GetClientRect( hwnd, &rect );
    SetRectRgn( hrgn, rect.left, rect.top, rect.right, rect.bottom );
    check_update_rgn( hwnd, hrgn );

    flush_sequence();
    RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ERASENOW );
    ok_sequence( WmInvalidateErase, "InvalidateErase", FALSE );

    flush_sequence();
    RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASENOW | RDW_UPDATENOW );
    ok_sequence( WmInvalidatePaint, "InvalidatePaint", FALSE );
    check_update_rgn( hwnd, 0 );

    flush_sequence();
    RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_UPDATENOW );
    ok_sequence( WmInvalidateErasePaint, "InvalidateErasePaint", FALSE );
    check_update_rgn( hwnd, 0 );

    flush_sequence();
    SetRectRgn( hrgn, 0, 0, 100, 100 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE );
    SetRectRgn( hrgn, 0, 0, 50, 100 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE );
    SetRectRgn( hrgn, 50, 0, 100, 100 );
    check_update_rgn( hwnd, hrgn );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE | RDW_ERASENOW );
    ok_sequence( WmEmptySeq, "EmptySeq", FALSE );  /* must not generate messages, everything is valid */
    check_update_rgn( hwnd, 0 );

    flush_sequence();
    SetRectRgn( hrgn, 0, 0, 100, 100 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_ERASE );
    SetRectRgn( hrgn, 0, 0, 100, 50 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE | RDW_ERASENOW );
    ok_sequence( WmErase, "Erase", FALSE );
    SetRectRgn( hrgn, 0, 50, 100, 100 );
    check_update_rgn( hwnd, hrgn );

    flush_sequence();
    SetRectRgn( hrgn, 0, 0, 100, 100 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_ERASE );
    SetRectRgn( hrgn, 0, 0, 50, 50 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE | RDW_NOERASE | RDW_UPDATENOW );
    ok_sequence( WmPaint, "Paint", FALSE );

    flush_sequence();
    SetRectRgn( hrgn, -4, -4, -2, -2 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME );
    SetRectRgn( hrgn, -200, -200, -198, -198 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE | RDW_NOFRAME | RDW_ERASENOW );
    ok_sequence( WmEmptySeq, "EmptySeq", FALSE );

    flush_sequence();
    SetRectRgn( hrgn, -4, -4, -2, -2 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME );
    SetRectRgn( hrgn, -4, -4, -3, -3 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE | RDW_NOFRAME );
    SetRectRgn( hrgn, 0, 0, 1, 1 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_UPDATENOW );
    ok_sequence( WmPaint, "Paint", FALSE );

    flush_sequence();
    SetRectRgn( hrgn, -4, -4, -1, -1 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME );
    RedrawWindow( hwnd, NULL, 0, RDW_ERASENOW );
    /* make sure no WM_PAINT was generated */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmInvalidateRgn, "InvalidateRgn", FALSE );

    flush_sequence();
    SetRectRgn( hrgn, -4, -4, -1, -1 );
    RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
    {
        if (msg.hwnd == hwnd && msg.message == WM_PAINT)
        {
            /* GetUpdateRgn must return empty region since only nonclient area is invalidated */
            INT ret = GetUpdateRgn( hwnd, hrgn, FALSE );
            ok( ret == NULLREGION, "Invalid GetUpdateRgn result %d\n", ret );
            ret = GetUpdateRect( hwnd, &rect, FALSE );
            ok( ret, "Invalid GetUpdateRect result %d\n", ret );
            /* this will send WM_NCPAINT and validate the non client area */
            ret = GetUpdateRect( hwnd, &rect, TRUE );
            ok( !ret, "Invalid GetUpdateRect result %d\n", ret );
        }
        DispatchMessage( &msg );
    }
    ok_sequence( WmGetUpdateRect, "GetUpdateRect", FALSE );

    DestroyWindow( hwnd );

    /* now test with a child window */

    hparent = CreateWindowExA(0, "TestParentClass", "Test parent", WS_OVERLAPPEDWINDOW,
                              100, 100, 200, 200, 0, 0, 0, NULL);
    ok (hparent != 0, "Failed to create parent window\n");

    hchild = CreateWindowExA(0, "TestWindowClass", "Test child", WS_CHILD | WS_VISIBLE | WS_BORDER,
                           10, 10, 100, 100, hparent, 0, 0, NULL);
    ok (hchild != 0, "Failed to create child window\n");

    ShowWindow( hparent, SW_SHOW );
    UpdateWindow( hparent );
    UpdateWindow( hchild );
    /* try to flush pending X expose events */
    MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );

    flush_sequence();
    log_all_parent_messages++;

    SetRect( &rect, 0, 0, 50, 50 );
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW | RDW_ALLCHILDREN );
    ok_sequence( WmInvalidateParentChild, "InvalidateParentChild", FALSE );

    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    pt.x = pt.y = 0;
    MapWindowPoints( hchild, hparent, &pt, 1 );
    SetRectRgn( hrgn, 0, 0, 50 - pt.x, 50 - pt.y );
    check_update_rgn( hchild, hrgn );
    SetRectRgn( hrgn, 0, 0, 50, 50 );
    check_update_rgn( hparent, hrgn );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmInvalidateParent, "InvalidateParent", FALSE );
    RedrawWindow( hchild, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmEmptySeq, "EraseNow child", FALSE );

    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentPaintNc, "WmParentPaintNc", FALSE );

    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmInvalidateParent, "InvalidateParent2", FALSE );
    RedrawWindow( hchild, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmEmptySeq, "EraseNow child", FALSE );

    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW | RDW_ALLCHILDREN );
    ok_sequence( WmInvalidateParentChild2, "InvalidateParentChild2", FALSE );

    SetWindowLong( hparent, GWL_STYLE, GetWindowLong(hparent,GWL_STYLE) | WS_CLIPCHILDREN );
    flush_sequence();
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmInvalidateParentChild, "InvalidateParentChild3", FALSE );

    /* flush all paint messages */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    flush_sequence();

    /* RDW_UPDATENOW on child with WS_CLIPCHILDREN doesn't change corresponding parent area */
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
    SetRectRgn( hrgn, 0, 0, 50, 50 );
    check_update_rgn( hparent, hrgn );
    RedrawWindow( hchild, NULL, 0, RDW_UPDATENOW );
    ok_sequence( WmInvalidateErasePaint2, "WmInvalidateErasePaint2", FALSE );
    SetRectRgn( hrgn, 0, 0, 50, 50 );
    check_update_rgn( hparent, hrgn );

    /* flush all paint messages */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    SetWindowLong( hparent, GWL_STYLE, GetWindowLong(hparent,GWL_STYLE) & ~WS_CLIPCHILDREN );
    flush_sequence();

    /* RDW_UPDATENOW on child without WS_CLIPCHILDREN will validate corresponding parent area */
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    SetRectRgn( hrgn, 0, 0, 50, 50 );
    check_update_rgn( hparent, hrgn );
    RedrawWindow( hchild, NULL, 0, RDW_UPDATENOW );
    ok_sequence( WmInvalidateErasePaint2, "WmInvalidateErasePaint2", FALSE );
    SetRectRgn( hrgn2, 10, 10, 50, 50 );
    CombineRgn( hrgn, hrgn, hrgn2, RGN_DIFF );
    check_update_rgn( hparent, hrgn );
    /* flush all paint messages */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    flush_sequence();

    /* same as above but parent gets completely validated */
    SetRect( &rect, 20, 20, 30, 30 );
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    SetRectRgn( hrgn, 20, 20, 30, 30 );
    check_update_rgn( hparent, hrgn );
    RedrawWindow( hchild, NULL, 0, RDW_UPDATENOW );
    ok_sequence( WmInvalidateErasePaint2, "WmInvalidateErasePaint2", FALSE );
    check_update_rgn( hparent, 0 );  /* no update region */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmEmptySeq, "WmEmpty", FALSE );  /* and no paint messages */

    /* make sure RDW_VALIDATE on child doesn't have the same effect */
    flush_sequence();
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    SetRectRgn( hrgn, 20, 20, 30, 30 );
    check_update_rgn( hparent, hrgn );
    RedrawWindow( hchild, NULL, 0, RDW_VALIDATE | RDW_NOERASE );
    SetRectRgn( hrgn, 20, 20, 30, 30 );
    check_update_rgn( hparent, hrgn );

    /* same as above but normal WM_PAINT doesn't validate parent */
    flush_sequence();
    SetRect( &rect, 20, 20, 30, 30 );
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    SetRectRgn( hrgn, 20, 20, 30, 30 );
    check_update_rgn( hparent, hrgn );
    /* no WM_PAINT in child while parent still pending */
    while (PeekMessage( &msg, hchild, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmEmptySeq, "No WM_PAINT", FALSE );
    while (PeekMessage( &msg, hparent, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentErasePaint, "WmParentErasePaint", FALSE );

    flush_sequence();
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );
    /* no WM_PAINT in child while parent still pending */
    while (PeekMessage( &msg, hchild, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmEmptySeq, "No WM_PAINT", FALSE );
    RedrawWindow( hparent, &rect, 0, RDW_VALIDATE | RDW_NOERASE | RDW_NOCHILDREN );
    /* now that parent is valid child should get WM_PAINT */
    while (PeekMessage( &msg, hchild, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmInvalidateErasePaint2, "WmInvalidateErasePaint2", FALSE );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmEmptySeq, "No other message", FALSE );

    /* same thing with WS_CLIPCHILDREN in parent */
    flush_sequence();
    SetWindowLong( hparent, GWL_STYLE, GetWindowLong(hparent,GWL_STYLE) | WS_CLIPCHILDREN );
    ok_sequence( WmSetParentStyle, "WmSetParentStyle", FALSE );
    /* changing style invalidates non client area, but we need to invalidate something else to see it */
    RedrawWindow( hparent, &rect, 0, RDW_UPDATENOW );
    ok_sequence( WmEmptySeq, "No message", FALSE );
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_UPDATENOW );
    ok_sequence( WmParentOnlyNcPaint, "WmParentOnlyNcPaint", FALSE );

    flush_sequence();
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN );
    SetRectRgn( hrgn, 20, 20, 30, 30 );
    check_update_rgn( hparent, hrgn );
    /* no WM_PAINT in child while parent still pending */
    while (PeekMessage( &msg, hchild, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmEmptySeq, "No WM_PAINT", FALSE );
    /* WM_PAINT in parent first */
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentPaintNc, "WmParentPaintNc2", FALSE );

    /* no RDW_ERASE in parent still causes RDW_ERASE and RDW_FRAME in child */
    flush_sequence();
    SetRect( &rect, 0, 0, 30, 30 );
    RedrawWindow( hparent, &rect, 0, RDW_INVALIDATE | RDW_ALLCHILDREN );
    SetRectRgn( hrgn, 0, 0, 30, 30 );
    check_update_rgn( hparent, hrgn );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentPaintNc, "WmParentPaintNc3", FALSE );

    /* validate doesn't cause RDW_NOERASE or RDW_NOFRAME in child */
    flush_sequence();
    SetRect( &rect, -10, 0, 30, 30 );
    RedrawWindow( hchild, &rect, 0, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE );
    SetRect( &rect, 0, 0, 20, 20 );
    RedrawWindow( hparent, &rect, 0, RDW_VALIDATE | RDW_ALLCHILDREN );
    RedrawWindow( hparent, NULL, 0, RDW_UPDATENOW );
    ok_sequence( WmChildPaintNc, "WmChildPaintNc", FALSE );

    /* validate doesn't cause RDW_NOERASE or RDW_NOFRAME in child */
    flush_sequence();
    SetRect( &rect, -10, 0, 30, 30 );
    RedrawWindow( hchild, &rect, 0, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE );
    SetRect( &rect, 0, 0, 100, 100 );
    RedrawWindow( hparent, &rect, 0, RDW_VALIDATE | RDW_ALLCHILDREN );
    RedrawWindow( hparent, NULL, 0, RDW_UPDATENOW );
    ok_sequence( WmEmptySeq, "WmChildPaintNc2", FALSE );
    RedrawWindow( hparent, NULL, 0, RDW_ERASENOW );
    ok_sequence( WmEmptySeq, "WmChildPaintNc3", FALSE );

    /* test RDW_INTERNALPAINT behavior */

    flush_sequence();
    RedrawWindow( hparent, NULL, 0, RDW_INTERNALPAINT | RDW_NOCHILDREN );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentOnlyPaint, "WmParentOnlyPaint", FALSE );

    RedrawWindow( hparent, NULL, 0, RDW_INTERNALPAINT | RDW_ALLCHILDREN );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentPaint, "WmParentPaint", FALSE );

    RedrawWindow( hparent, NULL, 0, RDW_INTERNALPAINT );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentOnlyPaint, "WmParentOnlyPaint", FALSE );

    SetWindowLong( hparent, GWL_STYLE, GetWindowLong(hparent,GWL_STYLE) & ~WS_CLIPCHILDREN );
    ok_sequence( WmSetParentStyle, "WmSetParentStyle", FALSE );
    RedrawWindow( hparent, NULL, 0, RDW_INTERNALPAINT );
    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessage( &msg );
    ok_sequence( WmParentPaint, "WmParentPaint", FALSE );

    log_all_parent_messages--;
    DestroyWindow( hparent );

    DeleteObject( hrgn );
    DeleteObject( hrgn2 );
}

struct wnd_event
{
    HWND hwnd;
    HANDLE event;
};

static DWORD WINAPI thread_proc(void *param)
{
    MSG msg;
    struct wnd_event *wnd_event = (struct wnd_event *)param;

    wnd_event->hwnd = CreateWindowExA(0, "TestWindowClass", "window caption text", WS_OVERLAPPEDWINDOW,
                                      100, 100, 200, 200, 0, 0, 0, NULL);
    ok(wnd_event->hwnd != 0, "Failed to create overlapped window\n");

    SetEvent(wnd_event->event);

    while (GetMessage(&msg, 0, 0, 0))
    {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }

    return 0;
}

static void test_interthread_messages(void)
{
    HANDLE hThread;
    DWORD tid;
    WNDPROC proc;
    MSG msg;
    char buf[256];
    int len, expected_len;
    struct wnd_event wnd_event;

    wnd_event.event = CreateEventW(NULL, 0, 0, NULL);
    if (!wnd_event.event)
    {
        trace("skipping interthread message test under win9x\n");
        return;
    }

    hThread = CreateThread(NULL, 0, thread_proc, &wnd_event, 0, &tid);
    ok(hThread != NULL, "CreateThread failed, error %ld\n", GetLastError());

    ok(WaitForSingleObject(wnd_event.event, INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject failed\n");

    CloseHandle(wnd_event.event);

    SetLastError(0xdeadbeef);
    ok(!DestroyWindow(wnd_event.hwnd), "DestroyWindow succeded\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error code %ld\n", GetLastError());

    proc = (WNDPROC)GetWindowLongPtrA(wnd_event.hwnd, GWLP_WNDPROC);
    ok(proc != NULL, "GetWindowLongPtrA(GWLP_WNDPROC) error %ld\n", GetLastError());

    expected_len = lstrlenA("window caption text");
    memset(buf, 0, sizeof(buf));
    SetLastError(0xdeadbeef);
    len = CallWindowProcA(proc, wnd_event.hwnd, WM_GETTEXT, sizeof(buf), (LPARAM)buf);
    ok(len == expected_len, "CallWindowProcA(WM_GETTEXT) error %ld, len %d, expected len %d\n", GetLastError(), len, expected_len);
    ok(!lstrcmpA(buf, "window caption text"), "window text mismatch\n");

    msg.hwnd = wnd_event.hwnd;
    msg.message = WM_GETTEXT;
    msg.wParam = sizeof(buf);
    msg.lParam = (LPARAM)buf;
    memset(buf, 0, sizeof(buf));
    SetLastError(0xdeadbeef);
    len = DispatchMessageA(&msg);
    ok(!len && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
       "DispatchMessageA(WM_GETTEXT) succeded on another thread window: ret %d, error %ld\n", len, GetLastError());

    /* the following test causes an exception in user.exe under win9x */
    msg.hwnd = wnd_event.hwnd;
    msg.message = WM_TIMER;
    msg.wParam = 0;
    msg.lParam = GetWindowLongPtrA(wnd_event.hwnd, GWLP_WNDPROC);
    SetLastError(0xdeadbeef);
    len = DispatchMessageA(&msg);
    ok(!len && GetLastError() == 0xdeadbeef,
       "DispatchMessageA(WM_TIMER) failed on another thread window: ret %d, error %ld\n", len, GetLastError());

    ok(PostMessageA(wnd_event.hwnd, WM_QUIT, 0, 0), "PostMessageA(WM_QUIT) error %ld\n", GetLastError());

    ok(WaitForSingleObject(hThread, INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject failed\n");
    CloseHandle(hThread);
}


static const struct message WmVkN[] = {
    { WM_KEYDOWN, wparam|lparam, 'N', 1 },
    { WM_KEYDOWN, sent|wparam|lparam, 'N', 1 },
    { WM_CHAR, wparam|lparam, 'n', 1 },
    { WM_COMMAND, sent|wparam|lparam, MAKEWPARAM(1002,1), 0 },
    { WM_KEYUP, wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, 'N', 0xc0000001 },
    { 0 }
};
static const struct message WmShiftVkN[] = {
    { WM_KEYDOWN, wparam|lparam, VK_SHIFT, 1 },
    { WM_KEYDOWN, sent|wparam|lparam, VK_SHIFT, 1 },
    { WM_KEYDOWN, wparam|lparam, 'N', 1 },
    { WM_KEYDOWN, sent|wparam|lparam, 'N', 1 },
    { WM_CHAR, wparam|lparam, 'N', 1 },
    { WM_COMMAND, sent|wparam|lparam, MAKEWPARAM(1001,1), 0 },
    { WM_KEYUP, wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, wparam|lparam, VK_SHIFT, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_SHIFT, 0xc0000001 },
    { 0 }
};
static const struct message WmCtrlVkN[] = {
    { WM_KEYDOWN, wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, sent|wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, wparam|lparam, 'N', 1 },
    { WM_KEYDOWN, sent|wparam|lparam, 'N', 1 },
    { WM_CHAR, wparam|lparam, 0x000e, 1 },
    { WM_COMMAND, sent|wparam|lparam, MAKEWPARAM(1000,1), 0 },
    { WM_KEYUP, wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, wparam|lparam, VK_CONTROL, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_CONTROL, 0xc0000001 },
    { 0 }
};
static const struct message WmCtrlVkN_2[] = {
    { WM_KEYDOWN, wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, sent|wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, wparam|lparam, 'N', 1 },
    { WM_COMMAND, sent|wparam|lparam, MAKEWPARAM(1000,1), 0 },
    { WM_KEYUP, wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, 'N', 0xc0000001 },
    { WM_KEYUP, wparam|lparam, VK_CONTROL, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_CONTROL, 0xc0000001 },
    { 0 }
};
static const struct message WmAltVkN[] = {
    { WM_SYSKEYDOWN, wparam|lparam, VK_MENU, 0x20000001 },
    { WM_SYSKEYDOWN, sent|wparam|lparam, VK_MENU, 0x20000001 },
    { WM_SYSKEYDOWN, wparam|lparam, 'N', 0x20000001 },
    { WM_SYSKEYDOWN, sent|wparam|lparam, 'N', 0x20000001 },
    { WM_SYSCHAR, wparam|lparam, 'n', 0x20000001 },
    { WM_SYSCHAR, sent|wparam|lparam, 'n', 0x20000001 },
    { WM_SYSCOMMAND, sent|defwinproc|wparam|lparam, SC_KEYMENU, 'n' },
    { HCBT_SYSCOMMAND, hook },
    { WM_ENTERMENULOOP, sent|defwinproc|wparam|lparam, 0, 0 },
    { WM_SETCURSOR, sent|defwinproc },
    { WM_INITMENU, sent|defwinproc },
    { WM_MENUCHAR, sent|defwinproc|wparam, MAKEWPARAM('n',MF_SYSMENU) },
    { WM_CAPTURECHANGED, sent|defwinproc },
    { WM_MENUSELECT, sent|defwinproc|wparam, MAKEWPARAM(0,0xffff) },
    { WM_EXITMENULOOP, sent|defwinproc },
    { WM_MENUSELECT, sent|defwinproc|wparam|optional, MAKEWPARAM(0,0xffff) }, /* Win95 bug */
    { WM_EXITMENULOOP, sent|defwinproc|optional }, /* Win95 bug */
    { WM_SYSKEYUP, wparam|lparam, 'N', 0xe0000001 },
    { WM_SYSKEYUP, sent|wparam|lparam, 'N', 0xe0000001 },
    { WM_KEYUP, wparam|lparam, VK_MENU, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_MENU, 0xc0000001 },
    { 0 }
};
static const struct message WmAltVkN_2[] = {
    { WM_SYSKEYDOWN, wparam|lparam, VK_MENU, 0x20000001 },
    { WM_SYSKEYDOWN, sent|wparam|lparam, VK_MENU, 0x20000001 },
    { WM_SYSKEYDOWN, wparam|lparam, 'N', 0x20000001 },
    { WM_COMMAND, sent|wparam|lparam, MAKEWPARAM(1003,1), 0 },
    { WM_SYSKEYUP, wparam|lparam, 'N', 0xe0000001 },
    { WM_SYSKEYUP, sent|wparam|lparam, 'N', 0xe0000001 },
    { WM_KEYUP, wparam|lparam, VK_MENU, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_MENU, 0xc0000001 },
    { 0 }
};
static const struct message WmCtrlAltVkN[] = {
    { WM_KEYDOWN, wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, sent|wparam|lparam, VK_CONTROL, 1 },
    { WM_KEYDOWN, wparam|lparam, VK_MENU, 0x20000001 },
    { WM_KEYDOWN, sent|wparam|lparam, VK_MENU, 0x20000001 },
    { WM_KEYDOWN, wparam|lparam, 'N', 0x20000001 },
    { WM_KEYDOWN, sent|wparam|lparam, 'N', 0x20000001 },
    { WM_KEYUP, wparam|lparam, 'N', 0xe0000001 },
    { WM_KEYUP, sent|wparam|lparam, 'N', 0xe0000001 },
    { WM_KEYUP, wparam|lparam, VK_MENU, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_MENU, 0xc0000001 },
    { WM_KEYUP, wparam|lparam, VK_CONTROL, 0xc0000001 },
    { WM_KEYUP, sent|wparam|lparam, VK_CONTROL, 0xc0000001 },
    { 0 }
};

static void pump_msg_loop(HWND hwnd, HACCEL hAccel)
{
    MSG msg;

    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
    {
        struct message log_msg;

        trace("accel: %p, %04x, %08x, %08lx\n", msg.hwnd, msg.message, msg.wParam, msg.lParam);

        log_msg.message = msg.message;
        log_msg.flags = wparam|lparam;
        log_msg.wParam = msg.wParam;
        log_msg.lParam = msg.lParam;
        add_message(&log_msg);

        if (!TranslateAccelerator(hwnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

static void test_accelerators(void)
{
    SHORT state;
    HACCEL hAccel;
    HWND hwnd = CreateWindowExA(0, "TestWindowClass", NULL, WS_OVERLAPPEDWINDOW,
                                100, 100, 200, 200, 0, 0, 0, NULL);
    assert(hwnd != 0);

    SetFocus(hwnd);
    ok(GetFocus() == hwnd, "wrong focus window %p\n", GetFocus());

    state = GetKeyState(VK_SHIFT);
    ok(!(state & 0x8000), "wrong Shift state %04x\n", state);
    state = GetKeyState(VK_CAPITAL);
    ok(state == 0, "wrong CapsLock state %04x\n", state);

    hAccel = LoadAccelerators(GetModuleHandleA(0), MAKEINTRESOURCE(1));
    assert(hAccel != 0);

    trace("testing VK_N press/release\n");
    flush_sequence();
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmVkN, "VK_N press/release", FALSE);

    trace("testing Shift+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmShiftVkN, "Shift+VK_N press/release", FALSE);

    trace("testing Ctrl+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmCtrlVkN, "Ctrl+VK_N press/release", FALSE);

    trace("testing Alt+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmAltVkN, "Alt+VK_N press/release", FALSE);

    trace("testing Ctrl+Alt+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmCtrlAltVkN, "Ctrl+Alt+VK_N press/release 1", FALSE);

    ok(DestroyAcceleratorTable(hAccel), "DestroyAcceleratorTable error %ld\n", GetLastError());

    hAccel = LoadAccelerators(GetModuleHandleA(0), MAKEINTRESOURCE(2));
    assert(hAccel != 0);

    trace("testing VK_N press/release\n");
    flush_sequence();
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmVkN, "VK_N press/release", FALSE);

    trace("testing Shift+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmShiftVkN, "Shift+VK_N press/release", FALSE);

    trace("testing Ctrl+VK_N press/release 2\n");
    flush_sequence();
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmCtrlVkN_2, "Ctrl+VK_N press/release 2", FALSE);

    trace("testing Alt+VK_N press/release 2\n");
    flush_sequence();
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmAltVkN_2, "Alt+VK_N press/release 2", FALSE);

    trace("testing Ctrl+Alt+VK_N press/release\n");
    flush_sequence();
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event('N', 0, 0, 0);
    keybd_event('N', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    pump_msg_loop(hwnd, hAccel);
    ok_sequence(WmCtrlAltVkN, "Ctrl+Alt+VK_N press/release 2", FALSE);

    ok(DestroyAcceleratorTable(hAccel), "DestroyAcceleratorTable error %ld\n", GetLastError());

    DestroyWindow(hwnd);
}

/************* window procedures ********************/

static LRESULT WINAPI MsgCheckProcA(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    static long beginpaint_counter = 0;
    LRESULT ret;
    struct message msg;

    trace("%p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    switch (message)
    {
        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS *winpos = (WINDOWPOS *)lParam;

            trace("%s\n", (message == WM_WINDOWPOSCHANGING) ? "WM_WINDOWPOSCHANGING" : "WM_WINDOWPOSCHANGED");
            trace("%p after %p, x %d, y %d, cx %d, cy %d flags %08x\n",
                  winpos->hwnd, winpos->hwndInsertAfter,
                  winpos->x, winpos->y, winpos->cx, winpos->cy, winpos->flags);

            /* Log only documented flags, win2k uses 0x1000 and 0x2000
             * in the high word for internal purposes
             */
            wParam = winpos->flags & 0xffff;
            break;
        }
    }

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    if (defwndproc_counter) msg.flags |= defwinproc;
    if (beginpaint_counter) msg.flags |= beginpaint;
    msg.wParam = wParam;
    msg.lParam = lParam;
    add_message(&msg);

    if (message == WM_GETMINMAXINFO && (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD))
    {
	HWND parent = GetParent(hwnd);
	RECT rc;
	MINMAXINFO *minmax = (MINMAXINFO *)lParam;

	GetClientRect(parent, &rc);
	trace("parent %p client size = (%ld x %ld)\n", parent, rc.right, rc.bottom);

	trace("ptReserved = (%ld,%ld)\n"
	      "ptMaxSize = (%ld,%ld)\n"
	      "ptMaxPosition = (%ld,%ld)\n"
	      "ptMinTrackSize = (%ld,%ld)\n"
	      "ptMaxTrackSize = (%ld,%ld)\n",
	      minmax->ptReserved.x, minmax->ptReserved.y,
	      minmax->ptMaxSize.x, minmax->ptMaxSize.y,
	      minmax->ptMaxPosition.x, minmax->ptMaxPosition.y,
	      minmax->ptMinTrackSize.x, minmax->ptMinTrackSize.y,
	      minmax->ptMaxTrackSize.x, minmax->ptMaxTrackSize.y);

	ok(minmax->ptMaxSize.x == rc.right, "default width of maximized child %ld != %ld\n",
	   minmax->ptMaxSize.x, rc.right);
	ok(minmax->ptMaxSize.y == rc.bottom, "default height of maximized child %ld != %ld\n",
	   minmax->ptMaxSize.y, rc.bottom);
    }

    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps;
        beginpaint_counter++;
        BeginPaint( hwnd, &ps );
        beginpaint_counter--;
        EndPaint( hwnd, &ps );
        return 0;
    }

    defwndproc_counter++;
    ret = DefWindowProcA(hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static LRESULT WINAPI PopupMsgCheckProcA(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    LRESULT ret;
    struct message msg;

    trace("popup: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    if (defwndproc_counter) msg.flags |= defwinproc;
    msg.wParam = wParam;
    msg.lParam = lParam;
    add_message(&msg);

    if (message == WM_CREATE)
    {
	DWORD style = GetWindowLongA(hwnd, GWL_STYLE) | WS_VISIBLE;
	SetWindowLongA(hwnd, GWL_STYLE, style);
    }

    defwndproc_counter++;
    ret = DefWindowProcA(hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static LRESULT WINAPI ParentMsgCheckProcA(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    static long beginpaint_counter = 0;
    LRESULT ret;
    struct message msg;

    trace("parent: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    if (log_all_parent_messages ||
        message == WM_PARENTNOTIFY || message == WM_CANCELMODE ||
	message == WM_SETFOCUS || message == WM_KILLFOCUS ||
	message == WM_ENABLE ||	message == WM_ENTERIDLE ||
	message == WM_IME_SETCONTEXT)
    {
        msg.message = message;
        msg.flags = sent|parent|wparam|lparam;
        if (defwndproc_counter) msg.flags |= defwinproc;
        if (beginpaint_counter) msg.flags |= beginpaint;
        msg.wParam = wParam;
        msg.lParam = lParam;
        add_message(&msg);
    }

    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps;
        beginpaint_counter++;
        BeginPaint( hwnd, &ps );
        beginpaint_counter--;
        EndPaint( hwnd, &ps );
        return 0;
    }

    defwndproc_counter++;
    ret = DefWindowProcA(hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static LRESULT WINAPI TestDlgProcA(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static long defwndproc_counter = 0;
    LRESULT ret;
    struct message msg;

    trace("dialog: %p, %04x, %08x, %08lx\n", hwnd, message, wParam, lParam);

    DefDlgProcA(hwnd, DM_SETDEFID, 1, 0);
    ret = DefDlgProcA(hwnd, DM_GETDEFID, 0, 0);
    if (after_end_dialog)
        ok( ret == 0, "DM_GETDEFID should return 0 after EndDialog, got %lx\n", ret );
    else
        ok(HIWORD(ret) == DC_HASDEFID, "DM_GETDEFID should return DC_HASDEFID, got %lx\n", ret);

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    if (defwndproc_counter) msg.flags |= defwinproc;
    msg.wParam = wParam;
    msg.lParam = lParam;
    add_message(&msg);

    defwndproc_counter++;
    ret = DefDlgProcA(hwnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static BOOL RegisterWindowClasses(void)
{
    WNDCLASSA cls;

    cls.style = 0;
    cls.lpfnWndProc = MsgCheckProcA;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = GetModuleHandleA(0);
    cls.hIcon = 0;
    cls.hCursor = LoadCursorA(0, (LPSTR)IDC_ARROW);
    cls.hbrBackground = GetStockObject(WHITE_BRUSH);
    cls.lpszMenuName = NULL;
    cls.lpszClassName = "TestWindowClass";
    if(!RegisterClassA(&cls)) return FALSE;

    cls.lpfnWndProc = PopupMsgCheckProcA;
    cls.lpszClassName = "TestPopupClass";
    if(!RegisterClassA(&cls)) return FALSE;

    cls.lpfnWndProc = ParentMsgCheckProcA;
    cls.lpszClassName = "TestParentClass";
    if(!RegisterClassA(&cls)) return FALSE;

    cls.lpfnWndProc = DefWindowProcA;
    cls.lpszClassName = "SimpleWindowClass";
    if(!RegisterClassA(&cls)) return FALSE;

    ok(GetClassInfoA(0, "#32770", &cls), "GetClassInfo failed\n");
    cls.lpfnWndProc = TestDlgProcA;
    cls.lpszClassName = "TestDialogClass";
    if(!RegisterClassA(&cls)) return FALSE;

    return TRUE;
}

static HHOOK hCBT_hook;

static LRESULT CALLBACK cbt_hook_proc(int nCode, WPARAM wParam, LPARAM lParam) 
{ 
    char buf[256];

    trace("CBT: %d, %08x, %08lx\n", nCode, wParam, lParam);

    if (nCode == HCBT_SYSCOMMAND)
    {
	struct message msg;

	msg.message = nCode;
	msg.flags = hook;
	msg.wParam = wParam;
	msg.lParam = lParam;
	add_message(&msg);

	return CallNextHookEx(hCBT_hook, nCode, wParam, lParam);
    }

    /* Log also SetFocus(0) calls */
    if (!wParam) wParam = lParam;

    if (GetClassNameA((HWND)wParam, buf, sizeof(buf)))
    {
	if (!strcmp(buf, "TestWindowClass") ||
	    !strcmp(buf, "TestParentClass") ||
	    !strcmp(buf, "TestPopupClass") ||
	    !strcmp(buf, "SimpleWindowClass") ||
	    !strcmp(buf, "TestDialogClass") ||
	    !strcmp(buf, "MDI_frame_class") ||
	    !strcmp(buf, "MDI_client_class") ||
	    !strcmp(buf, "MDI_child_class") ||
	    !strcmp(buf, "my_button_class") ||
	    !strcmp(buf, "#32770"))
	{
	    struct message msg;

	    msg.message = nCode;
	    msg.flags = hook;
	    msg.wParam = wParam;
	    msg.lParam = lParam;
	    add_message(&msg);
	}
    }
    return CallNextHookEx(hCBT_hook, nCode, wParam, lParam);
}

static const WCHAR wszUnicode[] = {'U','n','i','c','o','d','e',0};
static const WCHAR wszAnsi[] = {'U',0};

static LRESULT CALLBACK MsgConversionProcW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case CB_FINDSTRINGEXACT:
        trace("String: %p\n", (LPCWSTR)lParam);
        if (!lstrcmpW((LPCWSTR)lParam, wszUnicode))
            return 1;
        if (!lstrcmpW((LPCWSTR)lParam, wszAnsi))
            return 0;
        return -1;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static void test_message_conversion(void)
{
    static const WCHAR wszMsgConversionClass[] =
        {'M','s','g','C','o','n','v','e','r','s','i','o','n','C','l','a','s','s',0};
    WNDCLASSW cls;
    LRESULT lRes;
    HWND hwnd;
    WNDPROC wndproc;

    cls.style = 0;
    cls.lpfnWndProc = MsgConversionProcW;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hIcon = NULL;
    cls.hCursor = LoadCursorW(NULL, (LPWSTR)IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    cls.lpszMenuName = NULL;
    cls.lpszClassName = wszMsgConversionClass;
    /* this call will fail on Win9x, but that doesn't matter as this test is
     * meaningless on those platforms */
    if(!RegisterClassW(&cls)) return;

    hwnd = CreateWindowExW(0, wszMsgConversionClass, NULL, WS_OVERLAPPED,
                           100, 100, 200, 200, 0, 0, 0, NULL);
    ok(hwnd != NULL, "Window creation failed\n");

    /* {W, A} -> A */

    wndproc = (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
    lRes = CallWindowProcA(wndproc, hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0, "String should have been converted\n");
    lRes = CallWindowProcW(wndproc, hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 1, "String shouldn't have been converted\n");

    /* {W, A} -> W */

    wndproc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    lRes = CallWindowProcA(wndproc, hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 1, "String shouldn't have been converted\n");
    lRes = CallWindowProcW(wndproc, hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 1, "String shouldn't have been converted\n");

    /* Synchronous messages */

    lRes = SendMessageA(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0, "String should have been converted\n");
    lRes = SendMessageW(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 1, "String shouldn't have been converted\n");

    /* Asynchronous messages */

    SetLastError(0);
    lRes = PostMessageA(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "PostMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = PostMessageW(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "PostMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = PostThreadMessageA(GetCurrentThreadId(), CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "PosThreadtMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = PostThreadMessageW(GetCurrentThreadId(), CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "PosThreadtMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = SendNotifyMessageA(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "SendNotifyMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = SendNotifyMessageW(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "SendNotifyMessage on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = SendMessageCallbackA(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode, NULL, 0);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "SendMessageCallback on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
    SetLastError(0);
    lRes = SendMessageCallbackW(hwnd, CB_FINDSTRINGEXACT, 0, (LPARAM)wszUnicode, NULL, 0);
    ok(lRes == 0 && GetLastError() == ERROR_MESSAGE_SYNC_ONLY,
        "SendMessageCallback on sync only message returned %ld, last error %ld\n", lRes, GetLastError());
}

typedef struct _thread_info
{
    HWND hWnd;
    HANDLE handles[2];
    DWORD id;
} thread_info;

static VOID CALLBACK tfunc(HWND hwnd, UINT uMsg, UINT id, DWORD dwTime)
{
}

#define TIMER_ID  0x19

static DWORD WINAPI timer_thread_proc(LPVOID x)
{
    thread_info *info = x;
    DWORD r;

    r = KillTimer(info->hWnd, 0x19);
    ok(r,"KillTimer failed in thread\n");
    r = SetTimer(info->hWnd,TIMER_ID,10000,tfunc);
    ok(r,"SetTimer failed in thread\n");
    ok(r==TIMER_ID,"SetTimer id different\n");
    r = SetEvent(info->handles[0]);
    ok(r,"SetEvent failed in thread\n");
    return 0;
}

static void test_timers(void)
{
    thread_info info;
    DWORD id;

    info.hWnd = CreateWindow ("TestWindowClass", NULL,
       WS_OVERLAPPEDWINDOW ,
       CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, 0,
       NULL, NULL, 0);

    info.id = SetTimer(info.hWnd,TIMER_ID,10000,tfunc);
    ok(info.id, "SetTimer failed\n");
    ok(info.id==TIMER_ID, "SetTimer timer ID different\n");
    info.handles[0] = CreateEvent(NULL,0,0,NULL);
    info.handles[1] = CreateThread(NULL,0,timer_thread_proc,&info,0,&id);

    WaitForMultipleObjects(2, info.handles, FALSE, INFINITE);

    WaitForSingleObject(info.handles[1], INFINITE);

    ok( KillTimer(info.hWnd, TIMER_ID), "KillTimer failed\n");

    ok(DestroyWindow(info.hWnd), "failed to destroy window\n");
}

START_TEST(msg)
{
    if (!RegisterWindowClasses()) assert(0);

    hCBT_hook = SetWindowsHookExA(WH_CBT, cbt_hook_proc, 0, GetCurrentThreadId());
    assert(hCBT_hook);

    test_messages();
    test_mdi_messages();
    test_button_messages();
    test_paint_messages();
    test_interthread_messages();
    test_message_conversion();
    test_accelerators();
    test_timers();

    UnhookWindowsHookEx(hCBT_hook);
}
