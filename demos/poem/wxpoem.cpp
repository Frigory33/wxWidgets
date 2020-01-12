/////////////////////////////////////////////////////////////////////////////
// Name:        wxpoem.cpp
// Purpose:     A small C++ program which displays a random poem on
//              execution. It also allows search for poems containing a
//              string.
//              It requires winpoem.dat and creates winpoem.idx.
//              Original version (WinPoem) written in 1994.
//              This has not been rewritten in a long time so
//              beware, inelegant code!
// Author:      Julian Smart
// Created:     12/12/98
// Copyright:   (c) 1998 Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include "wx/config.h"

#include "wxpoem.h"

#include "corner1.xpm"
#include "corner2.xpm"
#include "corner3.xpm"
#include "corner4.xpm"
#include "wxpoem.xpm"

#define         BUFFER_SIZE 10000
#define         DEFAULT_POETRY_DAT "wxpoem"
#define         DEFAULT_POETRY_IND "wxpoem"
#define         DEFAULT_CHAR_HEIGHT 18
#define         DEFAULT_FONT "Swiss"
#define         DEFAULT_X_POS 0
#define         DEFAULT_Y_POS 0
#define         BORDER_SIZE 30
#define         THIN_LINE_BORDER 10
#define         THICK_LINE_BORDER 16
#define         THICK_LINE_WIDTH 2
#define         SHADOW_OFFSET 1
#define         X_SIZE 30
#define         Y_SIZE 20

static wxChar   *poem_buffer;                   // Storage for each poem
static wxChar   line[150];                      // Storage for a line
static int      pages[30];                      // For multipage poems -
                                                // store the start of each page
static long     last_poem_start = 0;            // Start of last found poem
static long     last_find = -1;                 // Point in file of last found
                                                // search string
static bool     search_ok = false;              // Search was successful
static bool     same_search = false;            // Searching on same string

static long     poem_index[600];                // Index of poem starts
static long     nitems = 0;                     // Number of poems
static int      char_height = DEFAULT_CHAR_HEIGHT; // Actual height
static int      index_ptr = -1;                 // Pointer into index
static int      poem_height, poem_width;        // Size of poem
static wxPoint  framePos;                       // Startup position
static int      pointSize = 12;                 // Font size
static int      anchorHoriz = POEM_ANCHOR_HCENTRE; // Horizontal anchor of frame (for resizing)
static int      anchorVert = POEM_ANCHOR_VCENTRE;  // Vertical anchor of frame
static bool     borderRemoved = false;          // Frame border is removed

static const wxChar *index_filename = NULL;     // Index filename
static const wxChar *data_filename = NULL;      // Data filename
static wxChar   error_buf[300];                 // Error message buffer
static bool     loaded_ok = false;              // Poem loaded ok
static bool     index_ok = false;               // Index loaded ok

static bool     paging = false;                 // Are we paging?
static int      current_page = 0;               // Currently viewed page

// Backing bitmap
wxBitmap        *backingBitmap = NULL;

void            PoetryError(const wxChar *, const wxChar *caption=wxT("wxPoem Error"));
void            PoetryNotify(const wxChar *Msg, const wxChar *caption=wxT("wxPoem"));
void            TryLoadIndex();
bool            LoadPoem(const wxChar *, long);
int             GetIndex();
int             LoadIndex(const wxChar *);
bool            Compile(void);
void            FindMax(int *max_thing, int thing);

#if wxUSE_CLIPBOARD
    #include "wx/dataobj.h"
    #include "wx/clipbrd.h"
#endif

#ifdef __WXWINCE__
    STDAPI_(__int64) CeGetRandomSeed();
#endif

IMPLEMENT_APP(MyApp)

MainWindow *TheMainWindow = NULL;

// Create the fonts
void MainWindow::CreateFonts()
{
    m_normalFont = wxTheFontList->FindOrCreateFont(pointSize, wxSWISS, wxNORMAL, wxNORMAL);
    m_boldFont =   wxTheFontList->FindOrCreateFont(pointSize, wxSWISS, wxNORMAL, wxBOLD);
    m_italicFont = wxTheFontList->FindOrCreateFont(pointSize, wxSWISS, wxITALIC, wxNORMAL);
}

BEGIN_EVENT_TABLE(MainWindow, wxFrame)
    EVT_CLOSE(MainWindow::OnCloseWindow)
    EVT_MENU(wxID_ANY, MainWindow::OnPopup)
END_EVENT_TABLE()

MainWindow::MainWindow(wxWindowID id, const wxString& title, long style):
     wxFrame(NULL, id, title, wxDefaultPosition, wxDefaultSize, style)
{
    m_corners[0] = m_corners[1] = m_corners[2] = m_corners[3] = NULL;

    ReadPreferences();
    Move(framePos - GetAnchorPos());
    if (borderRemoved)
        SetWindowStyle(GetWindowStyle() ^ wxCAPTION ^ wxBORDER_NONE);
    CreateFonts();

    SetIcon(wxpoem_xpm);

    m_corners[0] = new wxIcon( corner1_xpm );
    m_corners[1] = new wxIcon( corner2_xpm );
    m_corners[2] = new wxIcon( corner3_xpm );
    m_corners[3] = new wxIcon( corner4_xpm );
}

MainWindow::~MainWindow()
{
    for (int i=0;i<4;i++)
    {
        if(m_corners[i])
        {
            delete m_corners[i];
        }
    }
}

// Read the poetry buffer, either for finding the size
// or for writing to a bitmap (not to the window directly,
// since that displays messily)
// If DrawIt is true, we draw, otherwise we just determine the
// size the window should be.
void MainWindow::ScanBuffer(wxDC *dc, bool DrawIt, int *max_x, int *max_y)
{
    int i = pages[current_page];
    int ch = -1;
    int y = 0;
    int j;
    wxChar *line_ptr;
    int curr_width = 0;
    bool page_break = false;

    int width = 0;
    int height = 0;

    if (DrawIt)
    {
        y = (*max_y - poem_height)/2;
        width = *max_x;
        height = *max_y;
    }

    if (DrawIt && wxColourDisplay())
    {
        dc->SetBrush(*wxLIGHT_GREY_BRUSH);
        dc->SetPen(*wxGREY_PEN);
        dc->DrawRectangle(0, 0, width, height);
        dc->SetBackgroundMode(wxTRANSPARENT);
    }

    // See what ACTUAL char height is
    if(m_normalFont)
        dc->SetFont(*m_normalFont);
    wxCoord xx;
    wxCoord yy;
    dc->GetTextExtent(wxT("X"), &xx, &yy);
    char_height = (int)yy;

    if (current_page == 0)
    {
        m_title = wxEmptyString;
    }
    else if (!m_title.empty())
    {
        dc->SetFont(* m_boldFont);
        dc->GetTextExtent(m_title, &xx, &yy);
        FindMax(&curr_width, (int)xx);

        if (DrawIt)
        {
            int x = (width - xx)/2;
            dc->SetFont(* m_boldFont);

            // Change text to BLACK!
            dc->SetTextForeground(* wxBLACK);
            dc->DrawText(m_title, x, y);
            // Change text to WHITE!
            dc->SetTextForeground(* wxWHITE);
            dc->DrawText(m_title, x-SHADOW_OFFSET, y-SHADOW_OFFSET);
        }
        y += char_height;
        y += char_height;
    }

    while (ch != 0 && !page_break)
    {
        j = 0;
#if defined(__WXMSW__) || defined(__WXMAC__)
        while (((ch = poem_buffer[i]) != 13) && (ch != 0))
#else
        while (((ch = poem_buffer[i]) != 10) && (ch != 0))
#endif
        {
            line[j] = (wxChar)ch;
            j ++;
            i ++;
        }

#if defined(__WXMSW__) || defined(__WXMAC__)
        if (ch == 13)
#else
        if (ch == 10)
#endif
        {
            ch = -1;
            i ++;
#if defined(__WXMSW__) || defined(__WXMAC__)
            // Add another to skip the linefeed
            i ++;
#endif
            // If a single newline on its own, put a space in
            if (j == 0)
            {
                line[j] = ' ';
                j ++;
                line[j] = 0;
            }
        }

        if (j > 0)
        {
            line[j] = 0;
            if (line[0] == '@')
            {
                switch (line[1])
                {
                    case 'P':
                        paging = true;
                        page_break = true;
                        break;

                    case 'T':
                        dc->SetFont(* m_boldFont);
                        line_ptr = line+3;

                        m_title = line_ptr;
                        m_title << wxT(" (cont'd)");

                        dc->GetTextExtent(line_ptr, &xx, &yy);
                        FindMax(&curr_width, (int)xx);

                        if (DrawIt)
                        {
                            int x = (width - xx)/2;
                            dc->SetFont(* m_boldFont);

                            // Change text to BLACK!
                            dc->SetTextForeground(* wxBLACK);
                            dc->DrawText(line_ptr, x, y);

                            // Change text to WHITE!
                            dc->SetTextForeground(* wxWHITE);
                            dc->DrawText(line_ptr, x-SHADOW_OFFSET, y-SHADOW_OFFSET);
                            dc->SetTextForeground(* wxWHITE);
                        }
                        break;

                    case 'A':
                        line_ptr = line+3;
                        dc->SetFont(* m_italicFont);

                        dc->GetTextExtent(line_ptr, &xx, &yy);
                        FindMax(&curr_width, (int)xx);

                        if (DrawIt)
                        {
                            int x = (width - xx)/2;
                            dc->SetTextForeground(* wxBLACK);
                            dc->DrawText(line_ptr, x, y);
                        }
                        break;

                    // Default: just ignore this line
                    default:
                    y -= char_height;
                }
            }
            else
            {
                dc->SetFont(* m_normalFont);

                dc->GetTextExtent(line, &xx, &yy);
                FindMax(&curr_width, (int)xx);

                if (DrawIt)
                {
                    int x = (int)((width - xx)/2.0);
                    dc->SetFont(* m_normalFont);
                    dc->SetTextForeground(* wxBLACK);
                    dc->DrawText(line, x, y);
                }
            }
        }
        y += char_height;
    }

    // Write (cont'd)
    if (page_break)
    {
        const wxChar *cont = wxT("(cont'd)");

        dc->SetFont(* m_normalFont);

        dc->GetTextExtent(cont, &xx, &yy);
        FindMax(&curr_width, (int)xx);
        if (DrawIt)
        {
            int x = (int)((width - xx)/2.0);
            dc->SetFont(* m_normalFont);
            dc->SetTextForeground(* wxBLACK);
            dc->DrawText(cont, x, y);
        }
        y += 2*char_height;
    }

    *max_x = (int)curr_width;
    *max_y = (int)(y-char_height);

    if (page_break)
        pages[current_page+1] = i;
    else
        paging = false;

    if (DrawIt)
    {
        // Draw dark grey thick border
        if (wxColourDisplay())
        {
            dc->SetBrush(*wxGREY_BRUSH);
            dc->SetPen(*wxGREY_PEN);

            // Left side
            dc->DrawRectangle(0, 0, THIN_LINE_BORDER, height);
            // Top side
            dc->DrawRectangle(THIN_LINE_BORDER, 0, width-THIN_LINE_BORDER, THIN_LINE_BORDER);
            // Right side
            dc->DrawRectangle(width-THIN_LINE_BORDER, THIN_LINE_BORDER, width, height-THIN_LINE_BORDER);
            // Bottom side
            dc->DrawRectangle(THIN_LINE_BORDER, height-THIN_LINE_BORDER, width-THIN_LINE_BORDER, height);
        }
        // Draw border
        // Have grey background, plus 3-d border -
        // One black rectangle.
        // Inside this, left and top sides - dark grey. Bottom and right -
        // white.

        // Change pen to black
        dc->SetPen(*wxBLACK_PEN);
        dc->DrawLine(THIN_LINE_BORDER, THIN_LINE_BORDER, width-THIN_LINE_BORDER, THIN_LINE_BORDER);
        dc->DrawLine(width-THIN_LINE_BORDER, THIN_LINE_BORDER, width-THIN_LINE_BORDER, height-THIN_LINE_BORDER);
        dc->DrawLine(width-THIN_LINE_BORDER, height-THIN_LINE_BORDER, THIN_LINE_BORDER, height-THIN_LINE_BORDER);
        dc->DrawLine(THIN_LINE_BORDER, height-THIN_LINE_BORDER, THIN_LINE_BORDER, THIN_LINE_BORDER);

        // Right and bottom white lines - 'grey' (black!) if
        // we're running on a mono display.
        if (wxColourDisplay())
            dc->SetPen(*wxWHITE_PEN);
        else
            dc->SetPen(*wxBLACK_PEN);

        dc->DrawLine(width-THICK_LINE_BORDER, THICK_LINE_BORDER,
                     width-THICK_LINE_BORDER, height-THICK_LINE_BORDER);
        dc->DrawLine(width-THICK_LINE_BORDER, height-THICK_LINE_BORDER,
                     THICK_LINE_BORDER, height-THICK_LINE_BORDER);

        // Left and top grey lines
        dc->SetPen(*wxBLACK_PEN);
        dc->DrawLine(THICK_LINE_BORDER, height-THICK_LINE_BORDER,
                     THICK_LINE_BORDER, THICK_LINE_BORDER);
        dc->DrawLine(THICK_LINE_BORDER, THICK_LINE_BORDER,
                     width-THICK_LINE_BORDER, THICK_LINE_BORDER);

        // Draw icons
        dc->DrawIcon(* m_corners[0], 0, 0);
        dc->DrawIcon(* m_corners[1], int(width-32), 0);

        int y2 = height - 32;
        int x2 = (width-32);
        dc->DrawIcon(* m_corners[2], 0, y2);
        dc->DrawIcon(* m_corners[3], x2, y2);
    }
}

// Get an index (randomly generated) and load the poem
void MainWindow::GetIndexLoadPoem(void)
{
    if (index_ok)
        index_ptr = GetIndex();

    if (index_ptr > -1)
        loaded_ok = LoadPoem(data_filename, -1);
}

// Find the size of the poem and resize the window accordingly
void MainWindow::Resize(void)
{
    wxClientDC dc(canvas);

    wxPoint pos = GetPosition() + GetAnchorPos();

    // Get the poem size
    ScanBuffer(& dc, false, &poem_width, &poem_height);
    int x = poem_width + (2*BORDER_SIZE);
    int y = poem_height + (2*BORDER_SIZE);

    SetClientSize(x, y);

    Move(pos - GetAnchorPos());

    // In case client size isn't what we set it to...
    int xx, yy;
    GetClientSize(&xx, &yy);

    wxMemoryDC memDC;
    if (backingBitmap) delete backingBitmap;
    backingBitmap = new wxBitmap(x, yy);
    memDC.SelectObject(* backingBitmap);

    memDC.Clear();
    ScanBuffer(&memDC, true, &xx, &yy);
}

// Get the point on which the window is anchored, relative to the window
wxPoint MainWindow::GetAnchorPos(void)
{
    wxPoint pos(0, 0);
    wxSize size = GetSize();
    if (anchorHoriz == POEM_ANCHOR_RIGHT)
        pos.x = size.GetWidth();
    else if (anchorHoriz == POEM_ANCHOR_HCENTRE)
        pos.x = size.GetWidth() / 2;
    if (anchorVert == POEM_ANCHOR_BOTTOM)
        pos.y = size.GetHeight();
    else if (anchorVert == POEM_ANCHOR_VCENTRE)
        pos.y = size.GetHeight() / 2;
    return pos;
}

// Which is more?
void FindMax(int *max_thing, int thing)
{
    if (thing > *max_thing)
        *max_thing = thing;
}

// Next page/poem
void MainWindow::NextPage(void)
{
    if (paging)
        current_page ++;
    else
    {
        current_page = 0;
        GetIndexLoadPoem();
    }
    Resize();
}

// Previous page
void MainWindow::PreviousPage(void)
{
    if (current_page > 0)
    {
        current_page --;
        Resize();
    }
    else if (paging)
        PoetryNotify(wxT("Already at first page of poem."));
    else
        PoetryNotify(wxT("This poem has only one page."));
}

// Search for a string
void MainWindow::Search(bool ask)
{
    long position;

    if (ask || m_searchString.empty())
    {
        wxString s = wxGetTextFromUser( wxT("Enter search string:"), wxT("Search"), m_searchString);
        if (!s.empty())
        {
            s.MakeLower();
            m_searchString = s;
            search_ok = true;
        }
        else
        {
            search_ok = false;
        }
    }
    else
    {
        same_search = true;
        search_ok = true;
    }

    if (!m_searchString.empty() && search_ok)
    {
        position = DoSearch();
        if (position > -1)
        {
            loaded_ok = LoadPoem(data_filename, position);
            Resize();
        }
        else
        {
            last_poem_start = 0;
            PoetryNotify(wxT("Search string not found till end."));
        }
    }
}

bool MyApp::OnInit()
{
    poem_buffer = new wxChar[BUFFER_SIZE];

    // Seed the random number generator
#ifdef __WXWINCE__
    srand((unsigned) CeGetRandomSeed());
#else
    time_t current_time;

    (void)time(&current_time);
    srand((unsigned int)current_time);
#endif

//    randomize();
    pages[0] = 0;

    TheMainWindow = new MainWindow(wxID_ANY,
                                   wxT("wxPoem"),
                                   wxCAPTION|wxMINIMIZE_BOX|wxSYSTEM_MENU|wxCLOSE_BOX|wxFULL_REPAINT_ON_RESIZE
                                   );

    TheMainWindow->canvas = new MyCanvas(TheMainWindow);

    if (argc > 1)
    {
        index_filename = wxStrcpy(new wxChar[wxStrlen(argv[1]) + 1], argv[1]);
        data_filename = wxStrcpy(new wxChar[wxStrlen(argv[1]) + 1], argv[1]);
    }
    else
    {
        index_filename = wxT(DEFAULT_POETRY_IND);
        data_filename = wxT(DEFAULT_POETRY_DAT);
    }
    TryLoadIndex();

    TheMainWindow->GetIndexLoadPoem();
    TheMainWindow->Resize();
    TheMainWindow->Show(true);

    return true;
}

int MyApp::OnExit()
{
    if (backingBitmap)
        delete backingBitmap;

    delete[] poem_buffer;

    return 0;
}

void MainWindow::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
    WritePreferences();
    this->Destroy();
}

BEGIN_EVENT_TABLE(MyCanvas, wxWindow)
    EVT_MOUSE_EVENTS(MyCanvas::OnMouseEvent)
    EVT_PAINT(MyCanvas::OnPaint)
END_EVENT_TABLE()

// Define a constructor for my canvas
MyCanvas::MyCanvas(wxFrame *frame):
          wxWindow(frame, wxID_ANY)
{
    m_popupMenu = new wxMenu;
    m_popupMenu->Append(POEM_NEXT, wxT("Next poem/page\tPgDn"));
    m_popupMenu->Append(POEM_PREVIOUS, wxT("Previous page\tPgUp"));
    m_popupMenu->AppendSeparator();
    m_popupMenu->Append(POEM_SEARCH, wxT("Search\tS"));
    m_popupMenu->Append(POEM_NEXT_MATCH, wxT("Next match\tN"));
    m_popupMenu->Append(POEM_COPY, wxT("Copy to clipboard\tC"));
    m_popupMenu->Append(POEM_MINIMIZE, wxT("Minimize"));
    m_popupMenu->AppendSeparator();
    m_popupMenu->Append(POEM_BIGGER_TEXT, wxT("Bigger text\t+"));
    m_popupMenu->Append(POEM_SMALLER_TEXT, wxT("Smaller text\t-"));
    {
        wxMenu *anchorMenu = new wxMenu;
        anchorMenu->Append(POEM_ANCHOR_LEFT, wxT("Left"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->Append(POEM_ANCHOR_HCENTRE, wxT("Centre"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->Append(POEM_ANCHOR_RIGHT, wxT("Right"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->AppendSeparator();
        anchorMenu->Append(POEM_ANCHOR_TOP, wxT("Top"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->Append(POEM_ANCHOR_VCENTRE, wxT("Centre"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->Append(POEM_ANCHOR_BOTTOM, wxT("Bottom"), wxEmptyString, wxITEM_RADIO);
        anchorMenu->Check(anchorVert, true);
        anchorMenu->Check(anchorHoriz, true);
        m_popupMenu->AppendSubMenu(anchorMenu, wxT("Anchor"));
    }
    m_popupMenu->Append(POEM_TOGGLE_BORDER, wxT("No border"), wxEmptyString, wxITEM_CHECK);
    m_popupMenu->Check(POEM_TOGGLE_BORDER, borderRemoved);
    m_popupMenu->AppendSeparator();
    m_popupMenu->Append(POEM_ABOUT, wxT("About wxPoem"));
    m_popupMenu->AppendSeparator();
    m_popupMenu->Append(POEM_EXIT, wxT("Exit\tEsc"));

    wxAcceleratorEntry entries[] = {
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_SPACE, POEM_NEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_RIGHT, POEM_NEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_DOWN, POEM_NEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_PAGEDOWN, POEM_NEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_LEFT, POEM_PREVIOUS),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_UP, POEM_PREVIOUS),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_PAGEUP, POEM_PREVIOUS),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'S', POEM_SEARCH),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'N', POEM_NEXT_MATCH),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'C', POEM_COPY),
        wxAcceleratorEntry(wxACCEL_NORMAL, '+', POEM_BIGGER_TEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, '-', POEM_SMALLER_TEXT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_ESCAPE, POEM_EXIT)
    };
    wxAcceleratorTable accel(WXSIZEOF(entries), entries);
    SetAcceleratorTable(accel);
}

MyCanvas::~MyCanvas()
{
    // Note: this must be done before the main window/canvas are destroyed
    // or we get an error (no parent window for menu item button)
    delete m_popupMenu;
    m_popupMenu = NULL;
}

// Define the repainting behaviour
void MyCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dc(this);

    if (backingBitmap)
    {
        dc.DrawBitmap(* backingBitmap, 0, 0);
#if 0
        wxMemoryDC memDC;
        memDC.SelectObject(* backingBitmap);
        dc.Blit(0, 0, backingBitmap->GetWidth(), backingBitmap->GetHeight(), &memDC, 0, 0);
#endif
  }
}

void MyCanvas::OnMouseEvent(wxMouseEvent& event)
{
    static int startPosX, startPosY, startFrameX, startFrameY;

    long x, y;
    event.GetPosition(&x, &y);

    if (event.RightDown())
    {
        // Versions from wxWin 1.67 are probably OK
        PopupMenu(m_popupMenu, (int)x, (int)y );
    }
    else if (event.LeftDown())
    {
        this->CaptureMouse();
        int x1 = (int)x;
        int y1 = (int)y;
        ClientToScreen(&x1, &y1);
        startPosX = x1;
        startPosY = y1;
        GetParent()->GetPosition(&startFrameX, &startFrameY);
    }
    else if (event.LeftUp())
    {
        if (GetCapture() == this) this->ReleaseMouse();
    }
    else if (event.Dragging() && event.LeftIsDown())
    {
        int x1 = (int)x;
        int y1 = (int)y;
        ClientToScreen(&x1, &y1);

        int dX = x1 - startPosX;
        int dY = y1 - startPosY;
        GetParent()->Move(startFrameX + dX, startFrameY + dY);
    }
}

// Load index file
int LoadIndex(const wxChar *file_name)
{
    long data;
    FILE *index_file;

    wxChar buf[100];

    if (file_name == NULL)
        return 0;

    wxSprintf(buf, wxT("%s.idx"), file_name);

    index_file = wxFopen(buf, wxT("r"));
    if (index_file == NULL)
        return 0;

    wxFscanf(index_file, wxT("%ld"), &nitems);

    for (int i = 0; i < nitems; i++)
    {
        wxFscanf(index_file, wxT("%ld"), &data);
        poem_index[i] = data;
    }

    fclose(index_file);

    return 1;
}

// Get index
int GetIndex()
{
    int indexn = (int)(rand() % nitems);

    if ((indexn < 0) || (indexn > nitems))
    {
        PoetryError(wxT("No such poem!"));
        return -1;
    }
    else
        return indexn;
}

void AnchorFromConf(int *horiz, int *vert)
{
    *horiz += POEM_ANCHOR_LEFT;
    *vert += POEM_ANCHOR_TOP;
    if (*horiz < POEM_ANCHOR_LEFT || POEM_ANCHOR_RIGHT < *horiz)
        *horiz = POEM_ANCHOR_HCENTRE;
    if (*vert < POEM_ANCHOR_TOP || POEM_ANCHOR_BOTTOM < *vert)
        *vert = POEM_ANCHOR_VCENTRE;
}

void AnchorToConf(int *horiz, int *vert)
{
    *horiz -= POEM_ANCHOR_LEFT;
    *vert -= POEM_ANCHOR_TOP;
}

// Read preferences
void MainWindow::ReadPreferences()
{
    wxConfig config(wxT("wxPoem"));
    config.Read(wxT("FontSize"), &pointSize);
    config.Read(wxT("X"), &framePos.x);
    config.Read(wxT("Y"), &framePos.y);
    AnchorToConf(&anchorHoriz, &anchorVert);
    config.Read(wxT("AnchorHoriz"), &anchorHoriz);
    config.Read(wxT("AnchorVert"), &anchorVert);
    AnchorFromConf(&anchorHoriz, &anchorVert);
    config.Read(wxT("NoBorder"), &borderRemoved);
}

// Write preferences to disk
void MainWindow::WritePreferences()
{
    wxConfig config(wxT("wxPoem"));
    config.Write(wxT("FontSize"), pointSize);
    framePos = GetPosition() + GetAnchorPos();
    config.Write(wxT("X"), framePos.x);
    config.Write(wxT("Y"), framePos.y);
    int horiz = anchorHoriz, vert = anchorVert;
    AnchorToConf(&horiz, &vert);
    config.Write(wxT("AnchorHoriz"), horiz);
    config.Write(wxT("AnchorVert"), vert);
    config.Write(wxT("NoBorder"), borderRemoved);
}

// Load a poem from given file, at given point in file.
// If position is > -1, use this for the position in the
// file, otherwise use index[index_ptr] to find the correct position.
bool LoadPoem(const wxChar *file_name, long position)
{
    wxChar buf[100];
    long data;
    FILE *data_file;

    paging = false;
    current_page = 0;

    if (file_name == NULL)
    {
      wxSprintf(error_buf, wxT("Error in Poem loading."));
      PoetryError(error_buf);
      return false;
    }

    wxSprintf(buf, wxT("%s.dat"), file_name);
    data_file = wxFopen(buf, wxT("rb"));

    if (data_file == NULL)
    {
      wxSprintf(error_buf, wxT("Data file %s not found."), buf);
      PoetryError(error_buf);
      return false;
    }

    if (position > -1)
        data = position;
    else
        data = poem_index[index_ptr];

    fseek(data_file, data, SEEK_SET);

    int ch = 0;
    int i = 0;
    while ((ch != EOF) && (ch != '#'))
    {
        ch = getc(data_file);
        // Add a linefeed so it will copy to the clipboard ok
        if (ch == 10)
        {
            poem_buffer[i] = 13;
            i++;
        }

        poem_buffer[i] = (wxChar)ch;
        i ++;

        if (i == BUFFER_SIZE)
        {
            wxSprintf(error_buf, wxT("%s"), wxT("Poetry buffer exceeded."));
            PoetryError(error_buf);
            return false;
        }
    }
    fclose(data_file);
    poem_buffer[i-1] = 0;
    return true;
}

// Do the search
long MainWindow::DoSearch(void)
{
    if (m_searchString.empty())
        return false;

    FILE *file;
    size_t i = 0;
    int ch = 0;
    wxChar buf[100];
    long find_start;
    long previous_poem_start;

    bool found = false;
    size_t search_length = m_searchString.length();

    if (same_search)
    {
        find_start = last_find + 1;
        previous_poem_start = last_poem_start;
    }
    else
    {
        find_start = 0;
        last_poem_start = 0;
        previous_poem_start = -1;
    }

    if (data_filename)
        wxSprintf(buf, wxT("%s.dat"), data_filename);

    file = wxFopen(buf, wxT("rb"));
    if (! (data_filename && file))
    {
        wxSprintf(error_buf, wxT("Poetry data file %s not found\n"), buf);
        PoetryError(error_buf);
        return false;
    }

    fseek(file, find_start, SEEK_SET);

    while ((ch != EOF) && !found)
    {
        ch = getc(file);
        if (ch != EOF)
            ch = wxTolower(ch);   // Make lower case

        // Only match if we're looking at a different poem
        // (no point in displaying the same poem again)
        if ((m_searchString[i] == ch) && (last_poem_start != previous_poem_start))
        {
            if (i == 0)
                last_find = ftell(file);
            if (i == search_length-1)
                found = true;
            i ++;
        }
        else
        {
            i = 0;
        }

        if (ch == '#')
        {
            ch = getc(file);
            last_poem_start = ftell(file);
        }
    }
    fclose(file);
    if (ch == EOF)
    {
        last_find = -1;
    }

    if (found)
    {
        return last_poem_start;
    }

    return -1;
}

// Set up poetry filenames, preferences, load the index
// Load index (or compile it if none found)
void TryLoadIndex()
{
    index_ok = (LoadIndex(index_filename) != 0);
    if (!index_ok || (nitems == 0))
    {
        PoetryError(wxT("Index file not found; will compile new one"), wxT("wxPoem"));
        index_ok = Compile();
    }
}

// Error message
void PoetryError(const wxChar *msg, const wxChar *caption)
{
    wxMessageBox(msg, caption, wxOK|wxICON_EXCLAMATION);
}

// Notification (change icon to something appropriate!)
void PoetryNotify(const wxChar *Msg, const wxChar *caption)
{
    wxMessageBox(Msg, caption, wxOK | wxICON_INFORMATION);
}

// Build up and save an index into the poetry data file, for
// rapid random access
bool Compile(void)
{
    FILE *file;
    int j;
    int ch;
    wxChar buf[100];

    if (data_filename)
        wxSprintf(buf, wxT("%s.dat"), data_filename);

    file = wxFopen(buf, wxT("rb"));
    if (! (data_filename && file))
    {
        wxSprintf(error_buf, wxT("Poetry data file %s not found\n"), buf);
        PoetryError(error_buf);
        return false;
    }

    nitems = 0;

    // Do first one (?)
    poem_index[nitems] = 0;
    nitems ++;

    // Do rest

    do {
        ch = getc(file);
        if (ch == '#')
        {
            ch = getc(file);
            long data;
            data = ftell(file);
            poem_index[nitems] = data;
            nitems ++;
        }
    } while (ch != EOF);
    fclose(file);

    if (index_filename)
      wxSprintf(buf, wxT("%s.idx"), index_filename);

    file = wxFopen(buf, wxT("w"));
    if (! (data_filename && file))
    {
        wxSprintf(error_buf, wxT("Poetry index file %s cannot be created\n"), buf);
        PoetryError(error_buf);
        return false;
    }

    wxFprintf(file, wxT("%ld\n\n"), nitems);
    for (j = 0; j < nitems; j++)
        wxFprintf(file, wxT("%ld\n"), poem_index[j]);

    fclose(file);
    PoetryNotify(wxT("Poetry index compiled. Use context menu to navigate."));
    return true;
}

void MainWindow::ChangeTextSize(int offset)
{
    if (offset > 0 || pointSize > 2)
    {
        pointSize += offset;
        FindMax(&pointSize, 2);
        CreateFonts();
        Resize();
    }
}

void MainWindow::OnPopup(wxCommandEvent& event)
{
    switch (event.GetId())
    {
        case POEM_NEXT:
            // Another poem/page
            NextPage();
            break;
        case POEM_PREVIOUS:
            // Previous page
            PreviousPage();
            break;
        case POEM_SEARCH:
            // Search - with dialog
            Search(true);
            break;
        case POEM_NEXT_MATCH:
            // Search - without dialog (next match)
            Search(false);
            break;
        case POEM_MINIMIZE:
            Iconize(true);
            break;
#if wxUSE_CLIPBOARD
        case POEM_COPY:
            wxTheClipboard->UsePrimarySelection();
            if (wxTheClipboard->Open())
            {
                static wxString s;
                s = poem_buffer;
                s.Replace( wxT("@P"),wxEmptyString);
                s.Replace( wxT("@A "),wxEmptyString);
                s.Replace( wxT("@A"),wxEmptyString);
                s.Replace( wxT("@T "),wxEmptyString);
                s.Replace( wxT("@T"),wxEmptyString);
                wxTextDataObject *data = new wxTextDataObject( s.c_str() );
                if (!wxTheClipboard->SetData( data ))
                    wxMessageBox(wxT("Error while copying to the clipboard."));
            }
            else
            {
                wxMessageBox(wxT("Error opening the clipboard."));
            }
            wxTheClipboard->Close();
            break;
#endif
        case POEM_BIGGER_TEXT:
            ChangeTextSize(1);
            break;
        case POEM_SMALLER_TEXT:
            ChangeTextSize(-1);
            break;
        case POEM_ANCHOR_LEFT:
        case POEM_ANCHOR_HCENTRE:
        case POEM_ANCHOR_RIGHT:
            anchorHoriz = event.GetId();
            break;
        case POEM_ANCHOR_TOP:
        case POEM_ANCHOR_VCENTRE:
        case POEM_ANCHOR_BOTTOM:
            anchorVert = event.GetId();
            break;
        case POEM_TOGGLE_BORDER:
            borderRemoved = !borderRemoved;
            SetWindowStyle(GetWindowStyle() ^ wxCAPTION ^ wxBORDER_NONE);
            Resize();
            break;
        case POEM_ABOUT:
            (void)wxMessageBox(wxT("wxPoem Version 1.1\nJulian Smart (c) 1995"),
                               wxT("About wxPoem"), wxOK, this);
            break;
        case POEM_EXIT:
            // Exit
            Close(true);
            break;
        default:
            break;
    }
}
