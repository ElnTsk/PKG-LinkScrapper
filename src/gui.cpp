#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../src/config.h"
#include "../src/crawl.h"
#include "../src/deadstore.h"
#include "../src/download.h"
#include "../src/http.h"
#include "../src/match.h"
#include "../src/output.h"
#include "../src/parts.h"
#include "../src/hosts.h"
#include "../src/report.h"
#include "../src/resolve.h"
#include "../src/trust.h"
#include "../src/util.h"
#include "../src/verify.h"

namespace {

enum Ids {
  IDC_GAMES = 1001,
  IDC_CONSOLE,
  IDC_LIMIT,
  IDC_THRESHOLD,
  IDC_VERIFY,
  IDC_FILTER,
  IDC_LIST,
  IDC_STATUS,
  IDC_HUNT,
  IDC_STOP,
  IDC_EXPORT,
  IDC_RELOAD,
  IDC_RECOMMENDED,
  IDC_DARK,
  IDC_REPROBE,
  IDC_LIMIT_LABEL,
  IDC_THRESHOLD_LABEL,
  IDC_DETAILS,
  IDC_DOWNLOAD,
};

enum DlIds {
  IDC_DL_LIST = 2101,
  IDC_DL_HEAD,
  IDC_DL_PAUSE,
  IDC_DL_CANCEL,
  IDC_DL_CLEAR,
  IDC_DL_FOLDER,
};

enum SxIds {
  IDC_SX_LIST = 2201,
  IDC_SX_HEAD,
  IDC_SX_TOGGLE,
  IDC_SX_COPY,
  IDC_SX_WRITE,
  IDC_SX_OPEN,
  IDC_SX_RELOAD,
  IDC_SX_RESET,
};

constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_RESULTS = WM_APP + 2;
constexpr UINT WM_APP_DONE = WM_APP + 3;
constexpr UINT WM_APP_HIT = WM_APP + 4;
constexpr UINT WM_APP_REPORT_DONE = WM_APP + 5;
constexpr UINT_PTR PAINT_TIMER = 1;
constexpr UINT_PTR DETAILS_TIMER = 2;
constexpr UINT_PTR DOWNLOAD_TIMER = 3;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// the expander glyphs a tree view uses, drawn by putting 1 or 2 in the item's
// state image - mingw's headers do not define the macro
#ifndef IMAGE_STATE
#define IMAGE_STATE(n) ((n) << 12)
#endif

#ifndef LVIS_EXPANDED
#define LVIS_EXPANDED 0x0020
#endif

#ifndef LVIS_FOCUS
#define LVIS_FOCUS 0x0008
#endif

#ifndef LVM_SUBHITTEST
#define LVM_SUBHITTEST (WM_USER + 63)
#endif

// uxtheme exports these by ordinal; this is what Explorer uses to render its
// common controls dark, and it reaches parts SetWindowTheme alone cannot
enum ThemeAppuMode { kModeDefault = 0, kModeAllowDark = 1, kModeForceDark = 2, kModeForceLight = 3 };
typedef BOOL(WINAPI* SetPreferredAppModeFn)(int mode);
typedef BOOL(WINAPI* FlushMenuThemesFn)();

void applyProcessTheme(bool dark) {
  static SetPreferredAppModeFn setMode = reinterpret_cast<SetPreferredAppModeFn>(
      GetProcAddress(LoadLibraryA("uxtheme.dll"), MAKEINTRESOURCEA(135)));
  static FlushMenuThemesFn flushMenus = reinterpret_cast<FlushMenuThemesFn>(
      GetProcAddress(LoadLibraryA("uxtheme.dll"), MAKEINTRESOURCEA(136)));
  if (setMode) setMode(dark ? kModeForceDark : kModeForceLight);
  if (flushMenus) flushMenus();
}

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

COLORREF systemAccent() {
  DWORD color = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)) && color != 0) {
    return RGB(GetRValue(color), GetGValue(color), GetBValue(color));
  }
  return RGB(0, 122, 204);
}
constexpr int kPartsColumn = 4;

const char* kColumnHeaders[] = {"Console", "State", "Trust",  "Size",  "Parts",
                                "Ping",    "Host",  "Title",  "Source", "URL"};
const int kColumnWidths[] = {78, 100, 104, 88, 84, 72, 158, 300, 104, 420};
constexpr int kColumnCount = 10;

struct Palette {
  COLORREF window;
  COLORREF text;
  COLORREF muted;
  COLORREF field;
  COLORREF listBg;
  COLORREF rowAlt;
  COLORREF selection;
  COLORREF border;
  COLORREF separator;
  COLORREF headerBg;
  COLORREF hover;
  COLORREF pressed;
  COLORREF alive;
  COLORREF stub;
  COLORREF dead;
  COLORREF blocked;
  COLORREF page;
  COLORREF recommended;
};

constexpr Palette kLight{
    RGB(243, 243, 243), RGB(26, 26, 26),   RGB(110, 110, 110), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(248, 249, 250), RGB(225, 240, 252), RGB(215, 217, 220),
    RGB(236, 238, 240), RGB(247, 248, 249), RGB(238, 240, 242), RGB(225, 228, 231),
    RGB(0, 110, 40),    RGB(176, 108, 0),  RGB(176, 32, 32),   RGB(112, 56, 156),
    RGB(104, 104, 104), RGB(0, 90, 158)};

constexpr Palette kDark{
    RGB(32, 32, 32),     RGB(242, 242, 242), RGB(160, 164, 170), RGB(43, 43, 43),
    RGB(31, 31, 31),     RGB(35, 35, 36),    RGB(42, 65, 89),    RGB(58, 58, 60),
    RGB(42, 42, 43),     RGB(38, 38, 39),    RGB(45, 45, 46),    RGB(56, 56, 58),
    RGB(78, 201, 124),   RGB(222, 170, 78),  RGB(233, 96, 96),   RGB(176, 132, 222),
    RGB(140, 148, 156),  RGB(96, 168, 224)};

struct Ui {
  HWND window = nullptr;
  HWND hint = nullptr;
  HWND games = nullptr;
  HWND console = nullptr;
  HWND limitLabel = nullptr;
  HWND limit = nullptr;
  HWND thresholdLabel = nullptr;
  HWND threshold = nullptr;
  HWND verify = nullptr;
  HWND recommended = nullptr;
  HWND dark = nullptr;
  HWND reprobe = nullptr;
  HWND filter = nullptr;
  HWND list = nullptr;
  HWND details = nullptr;
  HWND status = nullptr;
  HWND hunt = nullptr;
  HWND stop = nullptr;
  HWND exportBtn = nullptr;
  HWND reload = nullptr;
  HWND download = nullptr;
  HFONT font = nullptr;
  HFONT bold = nullptr;
  HFONT mono = nullptr;
  HBRUSH windowBrush = nullptr;
  HBRUSH fieldBrush = nullptr;
  HIMAGELIST rowSpacer = nullptr;
  WNDPROC gamesProc = nullptr;

  bool darkMode = false;
  bool checkVerify = true;
  bool checkRecommended = false;
  bool checkDark = false;

  std::vector<Hit> hits;
  std::vector<Hit> display;
  std::vector<Hit> view;
  std::vector<int> viewParent;
  std::vector<int> rows;
  // keyed by the set's first part url, so a live re-sort cannot move the
  // expansion onto a different row
  std::set<std::string> expanded;
  std::vector<std::vector<std::wstring>> painted;
  std::map<std::string, size_t> indexByUrl;
  std::vector<std::string> dead;
  std::vector<GoodEntry> good;
  std::vector<std::string> trustedHosts;
  std::vector<std::string> trustedSources;
  long long freshMs = 7LL * 24 * 3600 * 1000;
  bool popupShown = false;
  size_t liveCap = 40;
  bool dirty = false;
  int sortColumn = -1;
  bool sortAscending = true;
  volatile bool running = false;
  std::string statusText = "ready";
  std::string detailsText;
};

Ui g;

// the modeless transfers window; nullptr until the first download starts
struct Downloads {
  HWND window = nullptr;
  HWND list = nullptr;
  HWND head = nullptr;
  HWND pause = nullptr;
  HWND cancel = nullptr;
  HWND clear = nullptr;
  HWND folder = nullptr;
  std::vector<std::string> ids;
  std::vector<DownloadItem> items;
  bool allPaused = false;
};

Downloads dl;

// the modeless sources window; nullptr until Sources is pressed
struct SourcesState {
  HWND window = nullptr;
  HWND list = nullptr;
  HWND head = nullptr;
  HWND toggle = nullptr;
  HWND copy = nullptr;
  HWND write = nullptr;
  HWND open = nullptr;
  HWND reload = nullptr;
  HWND reset = nullptr;
  std::vector<Source> rows;
  std::string note;
};

SourcesState sx;

const Palette& palette() { return g.darkMode ? kDark : kLight; }

void rebuildRows();
void buildView(const std::string& needle);
void populateList();
void showDetails();
void toggleParts(int position);
void setPartsOpen(int position, bool open);
const Hit* focusedHit();
void refreshDownloads();
void showDownloadsWindow();
void themeDownloads();
void themeSources();
void showSourcesWindow();
bool grabbableState(const std::string& state);

std::wstring widen(const std::string& value) {
  if (value.empty()) return std::wstring();
  int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
  return out;
}

std::string narrow(const std::wstring& value) {
  if (value.empty()) return std::string();
  int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string out(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count,
                      nullptr, nullptr);
  return out;
}

std::string editText(HWND control) {
  int length = static_cast<int>(SendMessageW(control, WM_GETTEXTLENGTH, 0, 0));
  std::wstring buffer(static_cast<size_t>(length + 1), L'\0');
  SendMessageW(control, WM_GETTEXT, length + 1, reinterpret_cast<LPARAM>(buffer.data()));
  buffer.resize(static_cast<size_t>(length));
  return narrow(buffer);
}

void setEditText(HWND control, const std::string& value) {
  SendMessageW(control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(widen(value).c_str()));
}

void setStatusText(int part, const std::string& text) {
  if (g.status) SendMessageW(g.status, SB_SETTEXTA, static_cast<WPARAM>(part),
                             reinterpret_cast<LPARAM>(text.c_str()));
}

int comboIndex(HWND combo) {
  int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  return index < 0 ? 0 : index;
}

std::string selectedConsole() {
  static const char* values[] = {"both", "ps4", "ps5"};
  return values[comboIndex(g.console)];
}

int selectedLimit() {
  int value = std::atoi(editText(g.limit).c_str());
  return value > 0 ? value : 40;
}

double selectedThreshold() {
  double value = std::atof(editText(g.threshold).c_str());
  if (value <= 0 || value > 1) value = 0.72;
  return value;
}

// the dropdown and the grid say PS4/PS5/Any; the wire values stay lowercase
std::string consoleLabel(const std::string& value) {
  std::string lower = toLowerAscii(value);
  if (lower == "ps4") return "PS4";
  if (lower == "ps5") return "PS5";
  if (lower.empty() || lower == "?" || lower == "both") return "Any";
  return value;
}

// last path segment of a url, decoded, for labelling a part row
std::string fileNameOf(const std::string& url) {
  UrlParts parts = crackUrl(url);
  std::string path = parts.valid ? parts.path : url;
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  name = percentDecode(name);
  size_t query = name.find_first_of("?#");
  if (query != std::string::npos) name = name.substr(0, query);
  return collapseSpaces(trim(name));
}

COLORREF rowColor(const Hit& hit) {  const Palette& p = palette();
  if (hit.state == "alive") return p.alive;
  if (hit.state == "stub") return p.stub;
  if (hit.state == "gated") return p.stub;
  if (hit.state == "shell") return p.stub;
  if (hit.state == "dead" || hit.state == "unreachable") return p.dead;
  if (hit.state == "blocked" || hit.state == "rate-limited" || hit.state == "partial") return p.blocked;
  if (hit.state == "page" || hit.state == "container" || hit.state == "unverified") return p.page;
  return p.text;
}

std::string themeFile() { return stateDirectory() + "\\theme.txt"; }

bool systemPrefersDark() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  LSTATUS result = RegGetValueA(HKEY_CURRENT_USER,
                                "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                "AppsUseLightTheme", RRF_RT_DWORD, nullptr, &value, &size);
  return result == ERROR_SUCCESS && value == 0;
}

void loadThemePreference() {
  std::ifstream stream(themeFile());
  std::string line;
  if (stream && std::getline(stream, line)) {
    g.darkMode = trim(line) == "dark";
    return;
  }
  g.darkMode = systemPrefersDark();
}

void saveThemePreference() {
  std::ofstream stream(themeFile(), std::ios::binary | std::ios::trunc);
  if (stream) stream << (g.darkMode ? "dark" : "light") << "\n";
}

void themeWindow(HWND control, const wchar_t* scheme) {
  if (control) SetWindowTheme(control, scheme, nullptr);
}

// The header is a child of the list view, so its NM_CUSTOMDRAW is answered by
// the list view and never reaches our window proc. Subclass the header itself.
void paintHeaderCells(HWND header, HDC dc) {
  const Palette& p = palette();
  RECT client{};
  GetClientRect(header, &client);
  HBRUSH face = CreateSolidBrush(p.headerBg);
  FillRect(dc, &client, face);
  DeleteObject(face);

  int count = static_cast<int>(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0));
  HWND list = reinterpret_cast<HWND>(GetPropA(header, "PKG-LinkScrapperList"));
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, p.muted);
  HGDIOBJ oldFont = SelectObject(dc, g.font);
  for (int index = 0; index < count; ++index) {
    RECT cell{};
    if (!SendMessageW(header, HDM_GETITEMRECT, static_cast<WPARAM>(index),
                      reinterpret_cast<LPARAM>(&cell))) {
      continue;
    }
    if (cell.right <= cell.left) continue;
    wchar_t label[64] = {};
    HDITEMW item{};
    item.mask = HDI_TEXT;
    item.pszText = label;
    item.cchTextMax = 63;
    SendMessageW(header, HDM_GETITEMW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item));
    RECT text = cell;
    InflateRect(&text, -8, 0);
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    flags |= (list == dl.list && index >= 2 && index <= 4) ? DT_RIGHT : DT_LEFT;
    DrawTextW(dc, label, -1, &text, flags);
    HPEN pen = CreatePen(PS_SOLID, 1, p.border);
    HPEN previous = reinterpret_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, cell.right - 1, cell.top + 4, nullptr);
    LineTo(dc, cell.right - 1, cell.bottom - 4);
    SelectObject(dc, previous);
    DeleteObject(pen);
  }

  if (list == g.list && g.sortColumn >= 0 && g.sortColumn < count) {
    RECT cell{};
    if (SendMessageW(header, HDM_GETITEMRECT, static_cast<WPARAM>(g.sortColumn),
                     reinterpret_cast<LPARAM>(&cell))) {
      HBRUSH accent = CreateSolidBrush(systemAccent());
      RECT bar{cell.left + 4, cell.bottom - 3, cell.right - 4, cell.bottom - 1};
      FillRect(dc, &bar, accent);
      DeleteObject(accent);
      int midX = cell.right - 18;
      int midY = (cell.top + cell.bottom) / 2;
      POINT arrow[3] = {{midX - 4, g.sortAscending ? midY + 3 : midY - 3},
                        {midX, g.sortAscending ? midY - 3 : midY + 3},
                        {midX + 4, g.sortAscending ? midY + 3 : midY - 3}};
      HBRUSH ink = CreateSolidBrush(p.muted);
      HGDIOBJ oldInk = SelectObject(dc, ink);
      HGDIOBJ oldLine = SelectObject(dc, GetStockObject(NULL_PEN));
      Polygon(dc, arrow, 3);
      SelectObject(dc, oldLine);
      SelectObject(dc, oldInk);
      DeleteObject(ink);
    }
  }
  if (oldFont) SelectObject(dc, oldFont);
}

LRESULT CALLBACK headerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  WNDPROC previous = reinterpret_cast<WNDPROC>(GetPropA(hwnd, "PKG-LinkScrapperHeader"));
  if (message == WM_PRINTCLIENT) {
    paintHeaderCells(hwnd, reinterpret_cast<HDC>(wParam));
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    paintHeaderCells(hwnd, dc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (message == WM_ERASEBKGND) {
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH face = CreateSolidBrush(palette().headerBg);
    FillRect(reinterpret_cast<HDC>(wParam), &client, face);
    DeleteObject(face);
    return 1;
  }
  if (previous) return CallWindowProcA(previous, hwnd, message, wParam, lParam);
  return DefWindowProcA(hwnd, message, wParam, lParam);
}

void paintListHeader(HWND list) {
  if (!list) return;
  HWND header = reinterpret_cast<HWND>(SendMessageW(list, LVM_GETHEADER, 0, 0));
  if (!header) return;
  SetPropA(header, "PKG-LinkScrapperList", reinterpret_cast<HANDLE>(list));
  if (!GetPropA(header, "PKG-LinkScrapperHeader")) {
    WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
        header, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(headerProc)));
    SetPropA(header, "PKG-LinkScrapperHeader", reinterpret_cast<HANDLE>(previous));
  }
  InvalidateRect(header, nullptr, TRUE);
  UpdateWindow(header);
}

// The shell draws a sunken client edge and a static edge in light grey on
// Windows 10 no matter what theme is set, so the controls carry no border
// style at all and the frame is painted here in the palette colour.
void paintFrame(HWND hwnd) {
  RECT window{};
  if (!GetWindowRect(hwnd, &window)) return;
  HDC dc = GetWindowDC(hwnd);
  if (!dc) return;
  HPEN pen = CreatePen(PS_SOLID, 1, palette().border);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, 0, 0, window.right - window.left, window.bottom - window.top);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(pen);
  ReleaseDC(hwnd, dc);
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  WNDPROC previous = reinterpret_cast<WNDPROC>(GetPropA(hwnd, "PKG-LinkScrapperFrame"));
  LRESULT result = previous ? CallWindowProcA(previous, hwnd, message, wParam, lParam)
                            : DefWindowProcA(hwnd, message, wParam, lParam);
  if (message == WM_NCPAINT || message == WM_NCACTIVATE || message == WM_SIZE ||
      message == WM_WINDOWPOSCHANGED) {
    paintFrame(hwnd);
  }
  return result;
}

void installFlatFrame(HWND control) {
  if (!control) return;
  if (!GetPropA(control, "PKG-LinkScrapperFrame")) {
    WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
        control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(frameProc)));
    SetPropA(control, "PKG-LinkScrapperFrame", reinterpret_cast<HANDLE>(previous));
  }
  SetWindowPos(control, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

// The shell's focus rectangle is black dots on a white xor - on a dark row or a
// dark button that reads as a stray outline, so it is drawn in the palette.
void drawFocusOutline(HDC dc, const RECT& bounds, COLORREF ink, int round) {
  HPEN pen = CreatePen(PS_SOLID, 1, ink);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  if (round > 0) {
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, round, round);
  } else {
    Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
  }
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(pen);
}

void applyTheme() {
  const Palette& p = palette();
  applyProcessTheme(g.darkMode);

  BOOL immersive = g.darkMode ? TRUE : FALSE;
  DwmSetWindowAttribute(g.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));
  DWORD corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(g.window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  // the caption only repaints when the frame is told to change
  SetWindowPos(g.window, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

  const wchar_t* scheme = g.darkMode ? L"DarkMode_Explorer" : L"";
  const wchar_t* items = g.darkMode ? L"DarkMode_ItemsView" : L"";

  for (HWND control : {g.hint, g.games, g.console, g.limitLabel, g.limit, g.thresholdLabel, g.threshold,
                       g.verify, g.recommended, g.dark, g.reprobe, g.filter, g.details, g.status, g.hunt,
                       g.stop, g.exportBtn, g.reload, g.download}) {
    themeWindow(control, scheme);
  }
  themeWindow(g.list, items);

  // the shell draws both edge styles light on Windows 10, so these controls
  // carry no border style at all and the frame is painted in the palette
  for (HWND control : {g.games, g.limit, g.threshold, g.filter, g.details, g.list}) {
    installFlatFrame(control);
    if (control != g.list) {
      SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                   reinterpret_cast<LPARAM>(MAKELPARAM(5, 5)));
    }
  }

  if (g.windowBrush) DeleteObject(g.windowBrush);
  if (g.fieldBrush) DeleteObject(g.fieldBrush);
  g.windowBrush = CreateSolidBrush(p.window);
  g.fieldBrush = CreateSolidBrush(p.field);

  SendMessageW(g.list, LVM_SETBKCOLOR, 0, static_cast<LPARAM>(p.listBg));
  SendMessageW(g.list, LVM_SETTEXTCOLOR, 0, static_cast<LPARAM>(p.text));
  SendMessageW(g.list, LVM_SETTEXTBKCOLOR, 0, static_cast<LPARAM>(p.listBg));

  // a 1px wide, 26px tall empty image list is the only way to make report rows
  // taller than the font
  if (g.rowSpacer) {
    SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_SMALL, 0);
    ImageList_Destroy(g.rowSpacer);
    g.rowSpacer = nullptr;
  }
  g.rowSpacer = ImageList_Create(1, 26, ILC_COLOR32, 1, 1);
  if (g.rowSpacer) SendMessageW(g.list, LVM_SETIMAGELIST, LVSIL_SMALL,
                                reinterpret_cast<LPARAM>(g.rowSpacer));

  paintListHeader(g.list);
  SendMessageW(g.status, SB_SETBKCOLOR, 0, static_cast<LPARAM>(p.window));

  InvalidateRect(g.window, nullptr, TRUE);
  RedrawWindow(g.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

  // row colours and cell text both change with the theme, so repaint the lot
  g.painted.clear();
  rebuildRows();
  populateList();
  SendMessageW(g.list, LVM_REDRAWITEMS, 0, static_cast<LPARAM>(g.rows.size()));
  if (dl.window) themeDownloads();
  if (sx.window) themeSources();
}

std::string pingText(const Hit& hit) {
  if (hit.pingMs > 0) return std::to_string(hit.pingMs) + "ms";
  if (hit.pingMs == 0) return "no route";
  return std::string();
}

std::string columnText(const Hit& hit, int column) {
  if (hit.isChild) {
    // a member row reads as a sub-row: no console of its own, and the title
    // carries the branch glyph because themed lists ignore item indent
    switch (column) {
      case 0: return std::string();
      case 4: return std::to_string(hit.partIndex) + " of " + std::to_string(hit.partCount);
      case 7: return "\xE2\x94\x94\xE2\x94\x80 " + fileNameOf(hit.url);
      default: break;
    }
  }
  switch (column) {
    case 0: return consoleLabel(hit.console.empty() ? "both" : hit.console);
    case 1: return hit.state;
    case 2: return hit.trust;
    case 3: return hit.size;
    case 4: return partsLabel(hit);
    case 5: return pingText(hit);
    case 6: return hit.host;
    case 7: return hit.title;
    case 8: return hit.source;
    default: return hit.url;
  }
}

bool recommendedOnly() { return g.checkRecommended; }

void rebuildRows() {
  std::vector<Hit> usable;
  usable.reserve(g.hits.size());
  for (const Hit& hit : g.hits) {
    if (!deadStoreContains(g.dead, hit.url)) usable.push_back(hit);
  }

  // a 21-part set is one download, so it gets one row
  g.display = groupParts(usable);

  if (recommendedOnly()) {
    std::vector<Hit> trusted;
    for (const Hit& row : g.display) {
      if (!row.trust.empty()) trusted.push_back(row);
    }
    g.display.swap(trusted);
  }

  g.rows.clear();
  std::string needle = toLowerAscii(collapseSpaces(trim(editText(g.filter))));

  for (size_t index = 0; index < g.display.size(); ++index) {
    const Hit& hit = g.display[index];
    if (!needle.empty()) {
      std::string haystack = toLowerAscii(hit.game + " " + hit.console + " " + hit.state + " " +
                                          hit.trust + " " + hit.host + " " + hit.title + " " +
                                          hit.source + " " + hit.url);
      if (haystack.find(needle) == std::string::npos) continue;
    }
    g.rows.push_back(static_cast<int>(index));
  }

  if (g.sortColumn >= 0) {
    int column = g.sortColumn;
    bool ascending = g.sortAscending;
    std::stable_sort(g.rows.begin(), g.rows.end(), [&](int a, int b) {
      int result = columnText(g.display[a], column).compare(columnText(g.display[b], column));
      return ascending ? result < 0 : result > 0;
    });
  }

  buildView(needle);
}

// one grid row per part-set, and the set's members as indented children under
// it whenever that row is opened
void buildView(const std::string& needle) {
  std::map<std::string, const Hit*> members;
  for (const Hit& hit : g.hits) members[normalizeUrl(hit.url)] = &hit;

  g.view.clear();
  g.viewParent.clear();

  for (size_t slot = 0; slot < g.rows.size(); ++slot) {
    int parent = g.rows[slot];
    const Hit& row = g.display[parent];
    g.view.push_back(row);
    g.viewParent.push_back(parent);

    if (row.partCount < 2) continue;

    bool open = g.expanded.count(row.url) != 0;
    // a filter that only matches a member still shows the set, opened
    if (!open && !needle.empty()) {
      for (const std::string& url : row.partUrls) {
        if (toLowerAscii(url).find(needle) != std::string::npos) {
          open = true;
          break;
        }
      }
    }
    if (!open) continue;

    int index = 0;
    for (const std::string& url : row.partUrls) {
      ++index;
      Hit child;
      const Hit* found = nullptr;
      auto member = members.find(normalizeUrl(url));
      if (member != members.end()) found = member->second;

      child.game = row.game;
      child.console = row.console;
      child.kind = found ? found->kind : row.kind;
      child.source = found ? found->source : row.source;
      child.host = found ? found->host : row.host;
      child.url = url;
      child.state = found ? found->state : std::string("unverified");
      child.trust = found ? found->trust : row.trust;
      child.http = found ? found->http : 0;
      child.sizeBytes = found ? found->sizeBytes : 0;
      child.size = found ? found->size : std::string();
      child.error = found ? found->error : std::string();
      child.matchScore = found ? found->matchScore : row.matchScore;
      child.title = child.host + " " + fileNameOf(url);
      child.partCount = row.partCount;
      child.partIndex = index;
      child.isChild = true;

      g.view.push_back(child);
      g.viewParent.push_back(parent);
    }
  }

  g.rows.clear();
  for (size_t index = 0; index < g.view.size(); ++index) g.rows.push_back(static_cast<int>(index));
}

void populateList() {
  // paint in place when only the text changed, but always trust the control's
  // own item count over the cached one - if they disagree, start clean
  int actual = static_cast<int>(SendMessageW(g.list, LVM_GETITEMCOUNT, 0, 0));
  if (actual < 0 || static_cast<size_t>(actual) != g.painted.size()) {
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    g.painted.clear();
    actual = 0;
  }

  while (static_cast<size_t>(actual) > g.rows.size()) {
    --actual;
    SendMessageW(g.list, LVM_DELETEITEM, static_cast<WPARAM>(actual), 0);
    g.painted.resize(static_cast<size_t>(actual));
  }

  for (size_t position = 0; position < g.rows.size(); ++position) {
    const Hit& hit = g.view[static_cast<size_t>(position)];

    std::vector<std::wstring> cells;
    cells.reserve(kColumnCount);
    for (int column = 0; column < kColumnCount; ++column) {
      cells.push_back(widen(columnText(hit, column)));
    }

    bool branch = !hit.isChild && hit.partCount >= 2;
    bool open = branch && g.expanded.count(hit.url) != 0;

    if (position >= g.painted.size()) {
      LVITEMW item{};
      item.mask = LVIF_TEXT | (branch ? LVIF_STATE : 0) | (hit.isChild ? LVIF_INDENT : 0);
      item.state = branch ? (open ? IMAGE_STATE(2) : IMAGE_STATE(1)) : 0;
      item.stateMask = branch ? LVIS_STATEIMAGEMASK : 0;
      item.iItem = static_cast<int>(position);
      item.iSubItem = 0;
      item.iIndent = hit.isChild ? 1 : 0;
      item.pszText = cells[0].data();
      SendMessageW(g.list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
      g.painted.push_back(std::vector<std::wstring>());
      g.painted.back().resize(static_cast<size_t>(kColumnCount));
    }

    if (branch) {
      LVITEMW glyph{};
      glyph.mask = LVIF_STATE;
      glyph.state = open ? IMAGE_STATE(2) : IMAGE_STATE(1);
      glyph.stateMask = LVIS_STATEIMAGEMASK;
      SendMessageW(g.list, LVM_SETITEMSTATE, static_cast<WPARAM>(position),
                   reinterpret_cast<LPARAM>(&glyph));
    }

    for (int column = 0; column < kColumnCount; ++column) {
      std::wstring& cell = g.painted[position][static_cast<size_t>(column)];
      if (cell == cells[static_cast<size_t>(column)]) continue;
      cell = cells[static_cast<size_t>(column)];
      LVITEMW sub{};
      sub.mask = LVIF_TEXT;
      sub.iItem = static_cast<int>(position);
      sub.iSubItem = column;
      sub.pszText = cell.data();
      SendMessageW(g.list, LVM_SETITEMTEXTW, static_cast<WPARAM>(position),
                   reinterpret_cast<LPARAM>(&sub));
    }
  }

  size_t sets = 0;
  size_t trusted = 0;
  size_t pending = 0;
  size_t children = 0;
  size_t grabbable = 0;
  for (const Hit& hit : g.view) {
    if (hit.isChild) {
      ++children;
      continue;
    }
    if (hit.partCount >= 2) ++sets;
    if (!hit.trust.empty()) ++trusted;
    if (hit.state == "unverified") ++pending;
    if (hit.partCount >= 2 || grabbableState(hit.state)) ++grabbable;
  }

  std::string summary = std::to_string(g.rows.size());
  if (g.rows.size() != g.hits.size()) summary += " of " + std::to_string(g.hits.size());
  summary += " links";
  if (trusted) summary += " - " + std::to_string(trusted) + " recommended";
  if (sets) summary += " - " + std::to_string(sets) + " in parts";
  if (children) summary += " - " + std::to_string(children) + " part rows shown";
  if (pending) summary += " - " + std::to_string(pending) + " unverified";
  if (grabbable) summary += " - " + std::to_string(grabbable) + " downloadable";
  if (g.rows.empty() && !g.hits.empty() && recommendedOnly()) {
    summary = "no recommended links in this run - untick to see everything";
  }
  setStatusText(1, summary);
  EnableWindow(g.reprobe, !g.running && pending > 0);
  EnableWindow(g.download, !g.running && grabbable > 0);
  showDetails();
}

std::string detailsText(const Hit& hit) {
  std::string out;
  if (hit.isChild) {
    out += "part " + std::to_string(hit.partIndex) + " of " + std::to_string(hit.partCount) +
           " - double-click opens this file alone\r\n";
  }
  out += hit.game + "  " + consoleLabel(hit.console) + "  " + hit.state;
  if (!hit.trust.empty()) out += "  (" + hit.trust + ")";
  out += "\r\n";

  out += "size " + (hit.size.empty() ? std::string("unknown") : hit.size);
  if (hit.sizeBytes > 0) out += " (" + std::to_string(hit.sizeBytes) + " bytes)";
  if (hit.http) out += "  http " + std::to_string(hit.http);
  out += "  kind " + (hit.kind.empty() ? std::string("link") : hit.kind);
  out += "\r\n";

  out += "source " + hit.source + "  host " + hit.host;
  if (hit.pingMs >= 0) {
    out += "  ping ";
    out += hit.pingMs > 0 ? std::to_string(hit.pingMs) + "ms" : std::string("no route");
    if (!hit.pingNote.empty()) out += " (" + hit.pingNote + ")";
  }
  if (hit.matchScore > 0) {
    char score[16];
    std::snprintf(score, sizeof(score), "  match %.2f", hit.matchScore);
    out += score;
  }
  if (hit.partCount >= 2) {
    out += "  parts " + std::to_string(hit.partCount);
    out += "  first part " + std::to_string(hit.partIndex);
  }
  out += "\r\n";

  out += hit.url;
  if (!hit.error.empty()) out += "\r\nnote " + hit.error;

  if (hit.partCount >= 2) {
    out += "\r\nall parts:";
    for (const std::string& url : hit.partUrls) out += "\r\n  " + url;
  }
  return out;
}

void setPartsOpen(int position, bool open) {
  if (position < 0 || static_cast<size_t>(position) >= g.view.size()) return;
  const Hit& row = g.view[static_cast<size_t>(position)];
  if (row.isChild || row.partCount < 2) return;
  if (open) {
    if (!g.expanded.insert(row.url).second) return;
    setStatusText(0, "parts shown - " + std::to_string(row.partCount) + " files");
  } else {
    if (!g.expanded.erase(row.url)) return;
    setStatusText(0, "parts hidden");
  }
  rebuildRows();
  populateList();
}

void toggleParts(int position) {
  if (position < 0 || static_cast<size_t>(position) >= g.view.size()) return;
  setPartsOpen(position, g.expanded.count(g.view[static_cast<size_t>(position)].url) == 0);
}

// the five command buttons are painted here: rounded, flat, with the primary
// action carrying the system accent
bool isCommandButton(int id) {
  return id == IDC_HUNT || id == IDC_STOP || id == IDC_REPROBE || id == IDC_EXPORT ||
         id == IDC_RELOAD || id == IDC_DOWNLOAD;
}

COLORREF buttonFace(const DRAWITEMSTRUCT* item) {
  const Palette& p = palette();
  if (item->itemID == static_cast<UINT_PTR>(IDC_HUNT) && !g.running) return systemAccent();
  if (item->itemState & ODS_SELECTED) return p.pressed;
  if (item->itemState & ODS_HOTLIGHT) return p.hover;
  return p.field;
}

BOOL drawButton(const DRAWITEMSTRUCT* item) {
  const Palette& p = palette();
  bool primary = item->itemID == static_cast<UINT_PTR>(IDC_HUNT) && !g.running;
  bool disabled = (item->itemState & ODS_DISABLED) != 0;

  COLORREF face = buttonFace(item);
  if (disabled) face = p.window;

  HBRUSH brush = CreateSolidBrush(face);
  HPEN pen = CreatePen(PS_SOLID, 1, disabled ? p.separator : (primary ? face : p.border));
  HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
  HGDIOBJ oldPen = SelectObject(item->hDC, pen);
  RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right - 1,
            item->rcItem.bottom - 1, 11, 9);
  SelectObject(item->hDC, oldPen);
  SelectObject(item->hDC, oldBrush);
  DeleteObject(pen);
  DeleteObject(brush);

  wchar_t label[64] = L"";
  GetWindowTextW(item->hwndItem, label, 64);
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, disabled ? p.muted : (primary ? RGB(255, 255, 255) : p.text));
  HFONT previous = reinterpret_cast<HFONT>(SelectObject(item->hDC, primary ? g.bold : g.font));
  RECT centre = item->rcItem;
  DrawTextW(item->hDC, label, -1, &centre, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  if (previous) SelectObject(item->hDC, previous);
  if (item->itemState & ODS_FOCUS) {
    RECT ring = item->rcItem;
    InflateRect(&ring, -3, -3);
    drawFocusOutline(item->hDC, ring, p.muted, 9);
  }
  return TRUE;
}

// every grid row is drawn here: alternating tint, accent selection, a status
// dot instead of coloured text, hairline separators and the tree branch
BOOL drawRow(HDC dc, int position, const RECT& bounds, UINT itemState) {
  const Palette& p = palette();
  if (position < 0 || static_cast<size_t>(position) >= g.view.size()) return FALSE;
  const Hit& hit = g.view[static_cast<size_t>(position)];
  bool selected = (itemState & (LVIS_SELECTED | LVIS_FOCUSED)) == LVIS_SELECTED ||
                  (itemState & LVIS_SELECTED) != 0;

  COLORREF face = selected ? p.selection
                           : (static_cast<size_t>(position) % 2 ? p.rowAlt : p.listBg);
  HBRUSH brush = CreateSolidBrush(face);
  FillRect(dc, &bounds, brush);
  DeleteObject(brush);

  RECT line = bounds;
  line.top = line.bottom - 1;
  HBRUSH hair = CreateSolidBrush(p.separator);
  FillRect(dc, &line, hair);
  DeleteObject(hair);

  COLORREF state = rowColor(hit);
  int dot = 7;
  int midY = bounds.top + (bounds.bottom - bounds.top) / 2;
  RECT dotRect{bounds.left + 8, midY - dot / 2, bounds.left + 8 + dot, midY + dot / 2};
  HBRUSH dotBrush = CreateSolidBrush(hit.isChild ? p.muted : state);
  HPEN dotPen = CreatePen(PS_SOLID, 1, p.border);
  HGDIOBJ oldDotBrush = SelectObject(dc, dotBrush);
  HGDIOBJ oldDotPen = SelectObject(dc, dotPen);
  Ellipse(dc, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
  SelectObject(dc, oldDotPen);
  SelectObject(dc, oldDotBrush);
  DeleteObject(dotPen);
  DeleteObject(dotBrush);

  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(dc, g.font);

  // walk the widths ourselves: LVM_GETSUBITEMRECT hands back the whole row for
  // subitem 0 and unreliable rects for the rest under double buffering
  int left = bounds.left;
  for (int column = 0; column < kColumnCount; ++column) {
    int width = static_cast<int>(SendMessageW(g.list, LVM_GETCOLUMNWIDTH,
                                              static_cast<WPARAM>(column), 0));
    if (width <= 0) break;
    RECT cell{left, bounds.top, left + width, bounds.bottom};
    left += width;
    if (cell.left >= bounds.right) break;
    if (cell.right > bounds.right) cell.right = bounds.right;

    std::wstring text = widen(columnText(hit, column));
    if (text.empty()) continue;

    COLORREF ink = p.text;
    if (column == 1 && !selected) ink = state;
    else if (column == 2 && !selected) ink = p.recommended;
    else if (column == 5 && hit.pingMs == 0 && !selected) ink = p.dead;

    SetTextColor(dc, ink);
    RECT pad = cell;
    if (column == 0) pad.left += 20;  // room for the status dot
    InflateRect(&pad, -5, 0);
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (column >= 3 && column <= 5) flags |= DT_RIGHT;
    DrawTextW(dc, text.c_str(), -1, &pad, flags);
  }

  SelectObject(dc, oldFont);
  if (itemState & LVIS_FOCUS) {
    RECT focus = bounds;
    InflateRect(&focus, -2, -2);
    drawFocusOutline(dc, focus, selected ? p.text : p.muted, 0);
  }  return TRUE;
}

void showDetails() {
  if (!g.details) return;
  std::string text;
  const Hit* hit = focusedHit();
  if (hit) {
    text = detailsText(*hit);
  } else if (!g.display.empty()) {
    text = "select a row to see its full record - size, http code, source, parts and any note";
  } else {
    text = "no links yet - enter a game name and press Hunt";
  }
  if (text == g.detailsText) return;
  g.detailsText = text;
  setEditText(g.details, text);
}

void setBusy(bool busy) {
  g.running = busy;
  EnableWindow(g.hunt, !busy);
  EnableWindow(g.reload, !busy);
  EnableWindow(g.console, !busy);
  EnableWindow(g.games, !busy);
  EnableWindow(g.dark, !busy);
  EnableWindow(g.stop, busy);
  if (busy) EnableWindow(g.reprobe, FALSE);
  for (HWND button : {g.hunt, g.stop, g.reprobe, g.exportBtn, g.reload, g.download}) {
    if (button) InvalidateRect(button, nullptr, TRUE);
  }
  setStatusText(0, busy ? g.statusText : (g.hits.empty() ? "ready" : "done"));
}

void layoutControls() {
  RECT client{};
  GetClientRect(g.window, &client);
  const int margin = 14;
  const int height = 28;
  const int gap = 10;
  const int statusHeight = 26;
  const int labelRow = margin;
  const int gamesTop = labelRow + 20;
  const int gamesHeight = 72;
  const int controlRow = gamesTop + gamesHeight + gap + 2;
  const int buttonRow = controlRow + height + gap;
  const int listTop = buttonRow + height + gap + 2;
  const int detailsHeight = 96;
  const int width = client.right - margin * 2;

  MoveWindow(g.hint, margin, labelRow, width, 16, TRUE);
  MoveWindow(g.games, margin, gamesTop, width, gamesHeight, TRUE);

  // row one: what to hunt and how strict to be
  int x = margin;
  MoveWindow(g.console, x, controlRow, 96, 200, TRUE);
  x += 96 + gap + gap;
  MoveWindow(g.limitLabel, x, controlRow + 4, 70, 16, TRUE);
  x += 70 + 4;
  MoveWindow(g.limit, x, controlRow, 52, height, TRUE);
  x += 52 + gap + gap;
  MoveWindow(g.thresholdLabel, x, controlRow + 4, 46, 16, TRUE);
  x += 46 + 4;
  MoveWindow(g.threshold, x, controlRow, 56, height, TRUE);
  x += 56 + gap + gap;
  MoveWindow(g.verify, x, controlRow + 2, 104, height - 2, TRUE);
  x += 104 + gap;
  MoveWindow(g.recommended, x, controlRow + 2, 132, height - 2, TRUE);
  x += 132 + gap;
  MoveWindow(g.dark, x, controlRow + 2, 82, height - 2, TRUE);

  // row two: actions on the left, the live filter on the right
  x = margin;
  const int buttonWidth = 108;
  MoveWindow(g.hunt, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap;
  MoveWindow(g.stop, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap;
  MoveWindow(g.reprobe, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap;
  MoveWindow(g.exportBtn, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap;
  MoveWindow(g.reload, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap;
  MoveWindow(g.download, x, buttonRow, buttonWidth, height, TRUE);
  x += buttonWidth + gap + gap;

  const int filterWidth = std::min(260, std::max(150, static_cast<int>(client.right) - margin - x));
  int filterLeft = std::max(x, static_cast<int>(client.right) - margin - filterWidth);
  MoveWindow(g.filter, filterLeft, buttonRow, filterWidth, height, TRUE);

  MoveWindow(g.list, margin, listTop, width, client.bottom - listTop - statusHeight - detailsHeight - margin - gap, TRUE);
  MoveWindow(g.details, margin, client.bottom - statusHeight - detailsHeight - margin, width, detailsHeight, TRUE);
  MoveWindow(g.status, 0, client.bottom - statusHeight, client.right, statusHeight, TRUE);

  int rightEdge = static_cast<int>(client.right);
  const int creditWidth = 180;
  int textEnd = std::max<int>(300, rightEdge - creditWidth - 430);
  int countsEnd = std::min<int>(rightEdge - creditWidth - 40, textEnd + 240);
  int sourcesEnd = std::max<int>(countsEnd + 80, rightEdge - creditWidth);
  int parts[4] = {textEnd, countsEnd, sourcesEnd, -1};
  SendMessageW(g.status, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
  // the bench signature, always on the rail
  setStatusText(3, "made by </ElonTusk>");
}

HWND createControl(HWND parent, const char* className, const char* text, DWORD style, DWORD exStyle,
                   int id, HINSTANCE instance) {
  HWND control = CreateWindowExA(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                 parent, reinterpret_cast<HMENU>(id), instance, nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

LRESULT CALLBACK gamesSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
      (wParam == VK_RETURN || wParam == '\n')) {
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_HUNT, BN_CLICKED), 0);
    return 0;
  }
  return CallWindowProcA(g.gamesProc, hwnd, message, wParam, lParam);
}

void progressSink(const char* line, int level) {
  if (!g.window || !line || level != 0) return;
  auto* copy = new std::string(line);
  if (!PostMessageA(g.window, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

const Hit* focusedHit() {
  int row = static_cast<int>(SendMessageW(g.list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_FOCUSED));
  if (row < 0 || row >= static_cast<int>(g.rows.size())) return nullptr;
  return &g.view[static_cast<size_t>(row)];
}

void copyToClipboard(const std::string& text) {
  if (!OpenClipboard(g.window)) return;
  EmptyClipboard();
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
  if (memory) {
    char* pointer = static_cast<char*>(GlobalLock(memory));
    if (pointer) {
      std::memcpy(pointer, text.c_str(), text.size());
      pointer[text.size()] = '\0';
      GlobalUnlock(memory);
      SetClipboardData(CF_TEXT, memory);
    }
  }
  CloseClipboard();
}

void rememberDead(const Hit& hit, bool force = false) {
  if (!hit.isChild && hit.partCount >= 2) {
    // a row is a set here - every member gets remembered, then the row itself
    std::vector<Hit> singles;
    for (const std::string& url : hit.partUrls) {
      Hit member = hit;
      member.partUrls.clear();
      member.partCount = 0;
      member.url = url;
      rememberDead(member, force);
    }
    return;
  }

  if (!force && hit.state != "dead" && hit.state != "unreachable" && hit.state != "stub") return;
  std::string needle = normalizeUrl(hit.url);
  if (std::find(g.dead.begin(), g.dead.end(), needle) != g.dead.end()) return;

  std::string error;
  if (!appendDeadStore(hit.url, hit.state, hit.host, hit.title, error)) {
    setStatusText(0, "could not write dead list: " + error);
    return;
  }
  g.dead.push_back(needle);
  std::sort(g.dead.begin(), g.dead.end());
}

void showHelpPopup() {
  if (g.popupShown) return;
  g.popupShown = true;
  MessageBoxW(g.window,
              L"To download double click to open link, If its blocked you can still access them by "
              L"double clicking.",
              L"PKG-LinkScrapper", MB_OK | MB_ICONINFORMATION);
}

void workerRun(std::vector<std::string> games, std::string console, Config config, HWND window) {
  httpInit(config.settings.userAgent, config.settings.concurrency, config.settings.timeoutMs, 1,
           config.settings.minHostDelayMs);

  std::mutex mutex;
  std::vector<Hit> accumulated;
  size_t finished = 0;

  huntMany(games, console, config, [&](size_t index, std::vector<Hit> hits) {
    std::vector<Hit> snapshot;
    std::string label;
    {
      std::lock_guard<std::mutex> lock(mutex);
      accumulated.insert(accumulated.end(), hits.begin(), hits.end());
      sortHits(accumulated, games);
      snapshot = accumulated;
      ++finished;
      label = games[index] + " done - " + std::to_string(hits.size()) + " links, " +
              std::to_string(finished) + " of " + std::to_string(games.size());
    }

    auto* partial = new std::vector<Hit>(snapshot);
    if (!PostMessageA(window, WM_APP_RESULTS, 0, reinterpret_cast<LPARAM>(partial))) delete partial;

    auto* copy = new std::string(label);
    if (!PostMessageA(window, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
  }, [&](const Hit& hit) {
    // fired from crawl threads: hand a copy to the window so rows land live
    if (abortRequested() && hit.state == "unverified") return;
    auto* copy = new Hit(hit);
    if (!PostMessageA(window, WM_APP_HIT, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
  });

  httpShutdown();

  std::vector<Hit> final;
  {
    std::lock_guard<std::mutex> lock(mutex);
    final = std::move(accumulated);
  }

  auto* pointer = new std::vector<Hit>(final);
  if (!PostMessageA(window, WM_APP_DONE, abortRequested() ? 1 : 0, reinterpret_cast<LPARAM>(pointer))) {
    delete pointer;
  }
}

std::string sourceSummary() {
  Config probe;
  std::string error;
  if (!loadConfig(probe, exeDirectory(), "", error)) return "config error: " + error;
  std::string names;
  for (const Source& source : probe.sources) names += (names.empty() ? "" : " ") + source.name;
  return std::to_string(probe.sources.size()) + " sources: " + names;
}

// the curated host and source lists live in the config layers, so the Sources
// button picks up edits without a restart
void applyTrustLists() {
  Config probe;
  std::string error;
  g.trustedHosts.clear();
  g.trustedSources.clear();
  if (loadConfig(probe, exeDirectory(), "", error)) {
    g.trustedHosts = probe.settings.trustedHosts;
    g.trustedSources = probe.settings.trustedSources;
    g.freshMs = static_cast<long long>(probe.settings.goodTtlDays) * 24 * 3600 * 1000;
  }
  setTrustLists(&g.good, &g.trustedHosts, &g.trustedSources, g.freshMs, false);
}

void startHuntFromUi() {
  if (g.running) return;

  std::string raw = editText(g.games);
  std::vector<std::string> games;
  std::set<std::string> seen;
  size_t pos = 0;
  while (pos <= raw.size()) {
    size_t next = raw.find_first_of("\n;", pos);
    if (next == std::string::npos) next = raw.size();
    std::string name = collapseSpaces(trim(raw.substr(pos, next - pos)));
    pos = next + 1;
    if (name.empty() || name[0] == '#') continue;
    if (!seen.insert(toLowerAscii(name)).second) continue;
    games.push_back(name);
  }

  if (games.empty()) {
    setStatusText(0, "no game names entered");
    return;
  }

  Config config;
  std::string error;
  if (!loadConfig(config, exeDirectory(), "", error)) {
    setStatusText(0, "config error: " + error);
    return;
  }

  config.settings.verify = g.checkVerify;
  config.settings.maxPerGame = selectedLimit();
  config.settings.threshold = selectedThreshold();
  // the live view holds raw urls; a part-set contributes one row but many urls
  g.liveCap = static_cast<size_t>(config.settings.maxPerGame) * games.size() * 4;

  g.hits.clear();
  g.display.clear();
  g.view.clear();
  g.viewParent.clear();
  g.expanded.clear();
  g.rows.clear();
  g.indexByUrl.clear();
  g.dirty = false;
  populateList();

  g.running = true;
  EnableWindow(g.hunt, FALSE);
  EnableWindow(g.reload, FALSE);
  EnableWindow(g.console, FALSE);
  EnableWindow(g.games, FALSE);
  EnableWindow(g.stop, TRUE);
  EnableWindow(g.reprobe, FALSE);
  SetTimer(g.window, PAINT_TIMER, 300, nullptr);

  g.statusText = "searching " + std::to_string(games.size()) + " game(s)";
  setStatusText(0, g.statusText);
  resetAbort();

  std::string console = selectedConsole();
  std::thread(workerRun, games, console, config, g.window).detach();
}

// finish the probe pass a Stop cut short, without crawling anything again
void recheckWorker(std::vector<Hit> pending, HWND window) {
  Config config;
  std::string error;
  loadConfig(config, exeDirectory(), "", error);

  httpInit(config.settings.userAgent, config.settings.concurrency, config.settings.timeoutMs, 1,
           config.settings.minHostDelayMs);

  ThreadPool pool(config.settings.concurrency);
  verifyAll(pending, pool);

  httpShutdown();

  auto* pointer = new std::vector<Hit>(pending);
  if (!PostMessageA(window, WM_APP_RESULTS, 1, reinterpret_cast<LPARAM>(pointer))) delete pointer;
}

void recheckFromUi() {
  if (g.running) return;
  std::vector<Hit> pending;
  for (const Hit& hit : g.hits) {
    if (hit.state == "unverified" || hit.state == "skipped") pending.push_back(hit);
  }
  if (pending.empty()) {
    setStatusText(0, "nothing left to check");
    return;
  }
  g.running = true;
  EnableWindow(g.hunt, FALSE);
  EnableWindow(g.reprobe, FALSE);
  setStatusText(0, "re-checking " + std::to_string(pending.size()) + " links");
  resetAbort();
  std::thread(recheckWorker, pending, g.window).detach();
}

void exportResults() {
  char file[MAX_PATH] = "PKG-LinkScrapper-results.tsv";

  OPENFILENAMEA dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = g.window;
  dialog.lpstrFilter = "TSV\0*.tsv\0JSON\0*.json\0CSV\0*.csv\0Text\0*.txt\0All files\0*.*\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = sizeof(file);
  dialog.lpstrDefExt = "tsv";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

  if (!GetSaveFileNameA(&dialog)) return;

  std::string format = "tsv";
  std::string name(file);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    std::string suffix = toLowerAscii(name.substr(dot + 1));
    if (suffix == "json") format = "json";
    else if (suffix == "csv") format = "csv";
    else if (suffix == "tsv") format = "tsv";
    else if (suffix == "txt") format = "table";
  }

  std::vector<Hit> visible;
  visible.reserve(g.rows.size());
  for (size_t position = 0; position < g.rows.size(); ++position) {
    visible.push_back(g.view[position]);
  }

  std::ofstream stream(name, std::ios::binary | std::ios::trunc);
  if (!stream) {
    setStatusText(0, "could not write " + name);
    return;
  }
  stream << renderHits(visible, format) << "\n";
  setStatusText(0, "wrote " + std::to_string(visible.size()) + " rows to " + name);
}

void fillCombo(HWND combo, const std::vector<std::string>& items, int selection) {
  for (const std::string& item : items) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(item).c_str()));
  }
  SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

void insertColumn(int index) {
  std::wstring label = widen(kColumnHeaders[index]);
  LVCOLUMNW column{};
  column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
  column.iSubItem = index;
  column.pszText = label.data();
  column.cx = kColumnWidths[index];
  column.fmt = (index == 2 || index == 3 || index == 4 || index == 5) ? LVCFMT_RIGHT : LVCFMT_LEFT;
  SendMessageW(g.list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&column));
}

struct CheckSpec {
  int id;
  const wchar_t* label;
};

const CheckSpec kChecks[] = {{IDC_VERIFY, L"verify links"},
                             {IDC_RECOMMENDED, L"recommended only"},
                             {IDC_DARK, L"dark"}};

HWND checkWindow(int id) {
  switch (id) {
    case IDC_VERIFY: return g.verify;
    case IDC_RECOMMENDED: return g.recommended;
    case IDC_DARK: return g.dark;
    default: return nullptr;
  }
}

// an owner-draw button stores nothing, so the state lives here
bool isChecked(int id) {
  switch (id) {
    case IDC_VERIFY: return g.checkVerify;
    case IDC_RECOMMENDED: return g.checkRecommended;
    case IDC_DARK: return g.checkDark;
    default: return false;
  }
}

void setChecked(int id, bool value) {
  switch (id) {
    case IDC_VERIFY: g.checkVerify = value; break;
    case IDC_RECOMMENDED: g.checkRecommended = value; break;
    case IDC_DARK: g.checkDark = value; break;
    default: return;
  }
  if (HWND control = checkWindow(id)) InvalidateRect(control, nullptr, TRUE);
}

const wchar_t* checkLabel(int id) {
  for (const CheckSpec& spec : kChecks) {
    if (spec.id == id) return spec.label;
  }
  return L"";
}

// Windows 10 has no dark checkbox glyph, so the whole control is painted here
BOOL drawCheck(const DRAWITEMSTRUCT* item) {
  const Palette& p = palette();
  const wchar_t* label = checkLabel(static_cast<int>(item->CtlID));
  bool checked = isChecked(static_cast<int>(item->CtlID));

  HBRUSH background = CreateSolidBrush(p.window);
  FillRect(item->hDC, &item->rcItem, background);
  DeleteObject(background);

  int box = 14;
  int top = item->rcItem.top + ((item->rcItem.bottom - item->rcItem.top - box) / 2);
  RECT glyph{item->rcItem.left + 2, top, item->rcItem.left + 2 + box, top + box};
  HBRUSH field = CreateSolidBrush(p.field);
  FillRect(item->hDC, &glyph, field);
  DeleteObject(field);
  HPEN pen = CreatePen(PS_SOLID, 1, p.border);
  HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(item->hDC, pen));
  HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(item->hDC, GetStockObject(NULL_BRUSH)));
  Rectangle(item->hDC, glyph.left, glyph.top, glyph.right, glyph.bottom);
  SelectObject(item->hDC, oldBrush);
  SelectObject(item->hDC, oldPen);
  DeleteObject(pen);

  if (checked) {
    HPEN mark = CreatePen(PS_SOLID, 2, p.recommended);
    HPEN previous = reinterpret_cast<HPEN>(SelectObject(item->hDC, mark));
    MoveToEx(item->hDC, glyph.left + 3, glyph.top + 7, nullptr);
    LineTo(item->hDC, glyph.left + 6, glyph.top + 10);
    MoveToEx(item->hDC, glyph.left + 6, glyph.top + 10, nullptr);
    LineTo(item->hDC, glyph.left + 11, glyph.top + 4);
    SelectObject(item->hDC, previous);
    DeleteObject(mark);
  }

  SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? p.muted : p.text);
  SetBkMode(item->hDC, TRANSPARENT);
  RECT text{glyph.right + 5, item->rcItem.top, item->rcItem.right, item->rcItem.bottom};
  DrawTextW(item->hDC, label, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
  if (item->itemState & ODS_FOCUS) {
    RECT ring = item->rcItem;
    InflateRect(&ring, -2, -2);
    drawFocusOutline(item->hDC, ring, p.muted, 0);
  }
  return TRUE;
}

void toggleCheck(int id) { setChecked(id, !isChecked(id)); }

// ---- downloads -------------------------------------------------------------

// worth pulling: not a listing page, and the probe never called it dead
bool grabbableState(const std::string& state) {
  return state != "page" && state != "container" && state != "gated" && state != "dead" &&
         state != "stub" && state != "shell" && state != "unreachable";
}

constexpr int kDlColumnCount = 6;
const wchar_t* kDlColumnHeaders[] = {L"File", L"State", L"Done", L"Total", L"MB/s", L"Note"};
const int kDlColumnWidths[] = {330, 88, 118, 96, 62, 250};

// {DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7}
const GUID kFileOpenDialogClsid = {0xdc1c5a9c, 0xe88a, 0x4dde,
                                   {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
// IID_IFileOpenDialog {d57c7288-d4ad-4768-be02-9d969532d960}
const GUID kFileOpenDialogIid = {0xd57c7288, 0xd4ad, 0x4768,
                                 {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};
// IID_IShellItem {43826d1e-e718-42ee-bc55-a1e261c37bfe}
const GUID kShellItemIid = {0x43826d1e, 0xe718, 0x42ee,
                            {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};

std::string mbpsText(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

std::string percentOf(long long done, long long total) {
  if (total <= 0) return std::string();
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f%%", done * 100.0 / static_cast<double>(total));
  return buffer;
}

std::string dlCell(const DownloadItem& item, int column) {
  switch (column) {
    case 0: {
      // the name the file actually landed under, so two pulls of the same url
      // read differently once the folder has both
      std::string saved = item.path;
      size_t slash = saved.find_last_of("\\/");
      return slash == std::string::npos ? item.name : saved.substr(slash + 1);
    }
    case 1:
      return item.state;
    case 2: {
      std::string pct = percentOf(item.doneBytes, item.totalBytes);
      return pct.empty() ? formatBytes(item.doneBytes) : formatBytes(item.doneBytes) + " " + pct;
    }
    case 3:
      return item.totalBytes > 0 ? formatBytes(item.totalBytes) : std::string("?");
    case 4:
      return item.speedMBs > 0.005 ? mbpsText(item.speedMBs) : std::string();
    default:
      return item.error;
  }
}

COLORREF dlColor(const DownloadItem& item) {
  const Palette& p = palette();
  if (item.state == "done") return p.alive;
  if (item.state == "failed") return p.dead;
  if (item.state == "paused") return p.stub;
  if (item.state == "running") return p.recommended;
  return p.muted;
}

std::string hexResult(HRESULT hr) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
  return buffer;
}

std::string gPickerNote;
std::string gComNote;

bool pickFolderModern(HWND owner, const std::string& initial, std::string& out) {
  out.clear();
  void* raw = nullptr;
  HRESULT created = CoCreateInstance(kFileOpenDialogClsid, nullptr, CLSCTX_INPROC_SERVER,
                                    kFileOpenDialogIid, &raw);
  if (FAILED(created) || !raw) {
    gPickerNote = "CoCreateInstance " + hexResult(created);
    return false;
  }
  IFileOpenDialog* dialog = static_cast<IFileOpenDialog*>(raw);
  dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dialog->SetTitle(L"Save downloads to");

  if (!initial.empty()) {
    std::wstring path = widen(initial);
    void* seedRaw = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, kShellItemIid, &seedRaw)) && seedRaw) {
      dialog->SetFolder(static_cast<IShellItem*>(seedRaw));
      static_cast<IShellItem*>(seedRaw)->Release();
    }
  }

  bool shipped = false;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* result = nullptr;
    if (SUCCEEDED(dialog->GetResult(&result)) && result) {
      PWSTR display = nullptr;
      if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &display)) && display) {
        out = narrow(display);
        shipped = true;
        CoTaskMemFree(display);
      }
      result->Release();
    }
  } else {
    shipped = true;  // the dialog ran and the user closed it
  }
  dialog->Release();
  return shipped;
}

int CALLBACK browseCallback(HWND dialog, UINT message, LPARAM lParam, LPARAM param) {
  if (message == BFFM_INITIALIZED && param) {
    SendMessageA(dialog, BFFM_SETSELECTIONA, TRUE, param);
  }
  (void)lParam;
  return 0;
}

std::string pickFolderLegacy(HWND owner, const std::string& initial) {
  BROWSEINFOA info{};
  info.hwndOwner = owner;
  info.lpszTitle = "Save downloads to";
  info.ulFlags = BIF_NEWDIALOGSTYLE | BIF_EDITBOX | BIF_RETURNONLYFSDIRS;
  if (!initial.empty()) {
    info.lpfn = browseCallback;
    info.lParam = reinterpret_cast<LPARAM>(initial.c_str());
  }
  LPITEMIDLIST picked = SHBrowseForFolderA(&info);
  if (!picked) return std::string();
  char path[1024] = {0};
  std::string out;
  if (SHGetPathFromIDListA(picked, path)) out = path;
  CoTaskMemFree(picked);
  return out;
}

std::string pickFolder(HWND owner) {
  std::string initial = downloadsFolderDefault();
  std::string picked;
  if (pickFolderModern(owner, initial, picked)) return picked;
  setStatusText(0, "shell picker unavailable (" + gPickerNote + ", com " + gComNote +
                       ") - using the classic one");
  return pickFolderLegacy(owner, initial);
}

// a host that answers every share with a page or a challenge only hands the
// file to a browser click; queueing it can only end in a failed row
bool pullable(const std::string& url, const std::string& state) {
  if (shareServesPage(registrable(bareHost(url))) && !resolvableHost(url)) return false;
  return grabbableState(state) || (state == "gated" && resolvableHost(url));
}

std::vector<std::string> downloadUrlsFor(const Hit& hit) {
  std::vector<std::string> urls;
  auto stateOf = [](const std::string& url) {
    auto found = g.indexByUrl.find(url);
    if (found == g.indexByUrl.end()) return true;
    return pullable(url, g.hits[found->second].state);
  };
  if (!hit.isChild && hit.partCount >= 2) {
    for (const std::string& url : hit.partUrls) {
      if (stateOf(url)) urls.push_back(url);
    }
    return urls;
  }
  if (pullable(hit.url, hit.state)) urls.push_back(hit.url);
  return urls;
}

// the refusal names the host when the host is the reason
void startDownloads(const std::vector<std::string>& urls);
void downloadRow(const Hit& hit) {
  std::vector<std::string> urls = downloadUrlsFor(hit);
  if (urls.empty()) {
    std::string host = registrable(bareHost(hit.url));
    if (shareServesPage(host) && !resolvableHost(hit.url)) {
      setStatusText(0, host + " only hands the file to a browser click - open the row and use its button");
      return;
    }
  }
  startDownloads(urls);
}

void startDownloads(const std::vector<std::string>& urls) {
  if (urls.empty()) {
    setStatusText(0,
                  "nothing to pull on that row - it is a page, a shell, a captcha gate, or every "
                  "part is dead");
    return;
  }
  std::string folder = pickFolder(g.window);
  if (folder.empty()) {
    setStatusText(0, "cancelled - no folder picked, nothing downloaded");
    return;
  }
  if (GetFileAttributesA(folder.c_str()) == INVALID_FILE_ATTRIBUTES) {
    CreateDirectoryA(folder.c_str(), nullptr);
  }
  downloadsFolderRemember(folder);

  // hosts that hide the file behind one more step of their own are walked now,
  // on the click - the link they hand back is single use, so it must not be
  // spent during a hunt
  std::vector<std::string> queue;
  std::vector<std::string> names;
  std::vector<std::string> refused;
  bool anyResolvable = false;
  for (const std::string& url : urls) {
    if (!resolvableHost(url)) {
      queue.push_back(url);
      names.emplace_back();
      continue;
    }
    anyResolvable = true;
    DirectLink direct = resolveDirect(url);
    if (direct.ok) {
      queue.push_back(direct.url);
      names.push_back(direct.name);
    } else {
      refused.push_back(direct.note.empty() ? "no link" : direct.note);
    }
  }
  if (queue.empty()) {
    setStatusText(0, refused.empty() ? "the host gave no file link" : refused[0]);
    return;
  }

  std::vector<std::string> ids = downloadsAdd(queue, folder, names);
  downloadsStart(3);
  dl.allPaused = false;
  showDownloadsWindow();
  refreshDownloads();
  std::string note = "downloading " + std::to_string(ids.size()) + " file(s) to " + folder;
  if (anyResolvable) note += " - through the host's own download page";
  if (!refused.empty()) note += " - " + std::to_string(refused.size()) + " refused: " + refused[0];
  setStatusText(0, note);
}

void layoutDownloads() {
  if (!dl.window) return;
  RECT client{};
  GetClientRect(dl.window, &client);
  const int margin = 12;
  const int height = 28;
  const int gap = 8;
  const int headHeight = 22;

  int x = margin;
  int buttonWidth = 126;
  for (HWND button : {dl.pause, dl.cancel, dl.clear, dl.folder}) {
    MoveWindow(button, x, client.bottom - margin - height, buttonWidth, height, TRUE);
    x += buttonWidth + gap;
  }
  const int top = margin + headHeight + gap;
  const int bottom = client.bottom - margin - height - gap;
  MoveWindow(dl.head, margin, margin, client.right - margin * 2, headHeight, TRUE);
  MoveWindow(dl.list, margin, top, client.right - margin * 2, std::max(80, bottom - top), TRUE);
}

void insertDlColumn(int index) {
  LVCOLUMNW column{};
  column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
  column.fmt = LVCFMT_LEFT;
  if (index >= 2 && index <= 4) column.fmt = LVCFMT_RIGHT;
  column.cx = kDlColumnWidths[index];
  column.pszText = const_cast<LPWSTR>(kDlColumnHeaders[index]);
  column.iSubItem = index;
  SendMessageW(dl.list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&column));
}

void refreshDownloads() {
  if (!dl.window) return;
  dl.items = downloadsSnapshot();
  dl.ids.clear();

  int have = static_cast<int>(SendMessageW(dl.list, LVM_GETITEMCOUNT, 0, 0));
  while (have > static_cast<int>(dl.items.size())) {
    SendMessageW(dl.list, LVM_DELETEITEM, static_cast<WPARAM>(have - 1), 0);
    --have;
  }

  std::vector<std::vector<std::wstring>> cells;
  cells.reserve(dl.items.size());
  for (const DownloadItem& item : dl.items) {
    dl.ids.push_back(item.id);
    std::vector<std::wstring> row;
    row.reserve(kDlColumnCount);
    for (int column = 0; column < kDlColumnCount; ++column) row.push_back(widen(dlCell(item, column)));
    cells.push_back(std::move(row));
  }

  for (size_t index = 0; index < cells.size(); ++index) {
    if (static_cast<int>(index) >= have) {
      LVITEMW insert{};
      insert.mask = LVIF_TEXT | LVIF_PARAM;
      insert.iItem = static_cast<int>(index);
      insert.pszText = cells[index][0].data();
      insert.lParam = static_cast<LPARAM>(index);
      SendMessageW(dl.list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert));
      ++have;
    }
    for (int column = 0; column < kDlColumnCount; ++column) {
      LVITEMW sub{};
      sub.mask = LVIF_TEXT;
      sub.iItem = static_cast<int>(index);
      sub.iSubItem = column;
      sub.pszText = cells[index][static_cast<size_t>(column)].data();
      SendMessageW(dl.list, LVM_SETITEMTEXTW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&sub));
    }
  }

  long long done = 0;
  long long total = 0;
  size_t active = 0;
  size_t finished = 0;
  size_t failed = 0;
  for (const DownloadItem& item : dl.items) {
    done += item.doneBytes;
    total += item.totalBytes;
    if (item.state == "running" || item.state == "queued") ++active;
    if (item.state == "done") ++finished;
    if (item.state == "failed") ++failed;
  }
  std::string head = std::to_string(dl.items.size()) + " file(s)  -  ";
  head += std::to_string(active) + " active  -  ";
  head += formatBytes(done);
  if (total > 0) head += " of " + formatBytes(total);
  head += "  -  " + mbpsText(downloadsAggregateMBs()) + " MB/s";
  if (finished) head += "  -  " + std::to_string(finished) + " done";
  if (failed) head += "  -  " + std::to_string(failed) + " failed";
  head += "  -  saving to " + downloadsFolderDefault();
  setEditText(dl.head, head);

  setEditText(dl.pause, dl.allPaused ? "Resume" : "Pause");
  InvalidateRect(dl.list, nullptr, FALSE);
}

BOOL drawDownloadRow(HDC dc, int position, const RECT& bounds, UINT itemState) {
  const Palette& p = palette();
  if (position < 0 || static_cast<size_t>(position) >= dl.items.size()) return FALSE;
  const DownloadItem& item = dl.items[static_cast<size_t>(position)];
  bool selected = (itemState & LVIS_SELECTED) != 0;

  COLORREF face = selected ? p.selection
                           : (static_cast<size_t>(position) % 2 ? p.rowAlt : p.listBg);
  HBRUSH brush = CreateSolidBrush(face);
  FillRect(dc, &bounds, brush);
  DeleteObject(brush);

  RECT line = bounds;
  line.top = line.bottom - 1;
  HBRUSH hair = CreateSolidBrush(p.separator);
  FillRect(dc, &line, hair);
  DeleteObject(hair);

  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(dc, g.font);
  COLORREF ink = p.text;
  if (!selected) ink = dlColor(item);
  SetTextColor(dc, ink);

  int left = bounds.left;
  for (int column = 0; column < kDlColumnCount; ++column) {
    int width = static_cast<int>(SendMessageW(dl.list, LVM_GETCOLUMNWIDTH, static_cast<WPARAM>(column), 0));
    if (width <= 0) break;
    RECT cell{left, bounds.top, left + width, bounds.bottom};
    left += width;
    if (cell.left >= bounds.right) break;
    if (cell.right > bounds.right) cell.right = bounds.right;
    std::wstring text = widen(dlCell(item, column));
    if (text.empty()) continue;
    InflateRect(&cell, -5, 0);
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (column >= 2 && column <= 4) flags |= DT_RIGHT;
    DrawTextW(dc, text.c_str(), -1, &cell, flags);
  }
  if (itemState & LVIS_FOCUS) {
    RECT ring = bounds;
    InflateRect(&ring, -2, -2);
    drawFocusOutline(dc, ring, (itemState & LVIS_SELECTED) ? p.text : p.muted, 0);
  }
  if (oldFont) SelectObject(dc, oldFont);
  return TRUE;
}

void themeDownloads() {
  if (!dl.window) return;
  const Palette& p = palette();
  applyProcessTheme(g.darkMode);
  BOOL immersive = g.darkMode ? TRUE : FALSE;
  DwmSetWindowAttribute(dl.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));
  DWORD corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(dl.window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  SetWindowPos(dl.window, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

  const wchar_t* scheme = g.darkMode ? L"DarkMode_Explorer" : L"";
  for (HWND control : {dl.head, dl.pause, dl.cancel, dl.clear, dl.folder}) themeWindow(control, scheme);
  themeWindow(dl.list, g.darkMode ? L"DarkMode_ItemsView" : L"");
  paintListHeader(dl.list);
  installFlatFrame(dl.list);

  SendMessageW(dl.list, LVM_SETBKCOLOR, 0, static_cast<LPARAM>(p.listBg));
  SendMessageW(dl.list, LVM_SETTEXTCOLOR, 0, static_cast<LPARAM>(p.text));
  SendMessageW(dl.list, LVM_SETTEXTBKCOLOR, 0, static_cast<LPARAM>(p.listBg));

  // the same 1px spacer trick the main grid uses for taller rows
  SendMessageW(dl.list, LVM_SETIMAGELIST, LVSIL_SMALL, 0);
  HIMAGELIST spacer = ImageList_Create(1, 24, ILC_COLOR32, 1, 1);
  if (spacer) SendMessageW(dl.list, LVM_SETIMAGELIST, LVSIL_SMALL, reinterpret_cast<LPARAM>(spacer));

  InvalidateRect(dl.window, nullptr, TRUE);
  refreshDownloads();
}

void cancelSelected() {
  int row = -1;
  size_t cancelled = 0;
  while ((row = static_cast<int>(
              SendMessageW(dl.list, LVM_GETNEXTITEM, static_cast<WPARAM>(row), LVNI_SELECTED))) != -1) {
    if (row >= 0 && static_cast<size_t>(row) < dl.ids.size()) {
      downloadsCancel(dl.ids[static_cast<size_t>(row)]);
      ++cancelled;
    }
  }
  if (!cancelled) {
    for (const std::string& id : dl.ids) downloadsCancel(id);
    cancelled = dl.ids.size();
  }
  refreshDownloads();
  setEditText(dl.head, std::to_string(cancelled) + " transfer(s) cancelled - the partial files stay on disk and resume on the next pull");
}

LRESULT CALLBACK downloadsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
      dl.window = window;
      dl.head = createControl(window, "STATIC", "", SS_LEFTNOWORDWRAP, 0, IDC_DL_HEAD, instance);
      SendMessageW(dl.head, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold), TRUE);

      dl.list = CreateWindowExA(0, WC_LISTVIEWA, "",
                                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 10, 10,
                                window, reinterpret_cast<HMENU>(IDC_DL_LIST), instance, nullptr);
      SendMessageW(dl.list, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
      ListView_SetExtendedListViewStyle(dl.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                         LVS_EX_HEADERDRAGDROP);
      for (int index = 0; index < kDlColumnCount; ++index) insertDlColumn(index);

      dl.pause = createControl(window, "BUTTON", "Pause", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_DL_PAUSE,
                               instance);
      dl.cancel = createControl(window, "BUTTON", "Cancel", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_DL_CANCEL,
                                instance);
      dl.clear = createControl(window, "BUTTON", "Clear done", BS_OWNERDRAW | WS_TABSTOP, 0,
                               IDC_DL_CLEAR, instance);
      dl.folder = createControl(window, "BUTTON", "Open folder", BS_OWNERDRAW | WS_TABSTOP, 0,
                                IDC_DL_FOLDER, instance);

      layoutDownloads();
      themeDownloads();
      SetTimer(window, DOWNLOAD_TIMER, 500, nullptr);
      return 0;
    }

    case WM_SIZE:
      layoutDownloads();
      return 0;

    case WM_ERASEBKGND: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      RECT client{};
      GetClientRect(window, &client);
      FillRect(dc, &client, g.windowBrush ? g.windowBrush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
      return 1;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, palette().text);
      SetBkColor(dc, palette().window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_CTLCOLORBTN: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, palette().text);
      SetBkColor(dc, palette().window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
      if (item->CtlType == ODT_BUTTON) return drawButton(item);
      return FALSE;
    }

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = 720;
      info->ptMinTrackSize.y = 320;
      return 0;
    }

    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case IDC_DL_PAUSE:
          dl.allPaused = !dl.allPaused;
          if (dl.allPaused) downloadsPause();
          else downloadsResume();
          refreshDownloads();
          return 0;
        case IDC_DL_CANCEL:
          cancelSelected();
          return 0;
        case IDC_DL_CLEAR:
          downloadsRemoveFinished();
          refreshDownloads();
          return 0;
        case IDC_DL_FOLDER: {
          std::string folder = downloadsFolderDefault();
          ShellExecuteA(window, "open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        }
        default:
          return DefWindowProcA(window, message, wParam, lParam);
      }
    }

    case WM_NOTIFY: {
      LPNMHDR header = reinterpret_cast<LPNMHDR>(lParam);
      if (header->hwndFrom != dl.list) return 0;
      if (header->code == NM_CUSTOMDRAW) {
        LPNMLVCUSTOMDRAW draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
          if (drawDownloadRow(draw->nmcd.hdc, static_cast<int>(draw->nmcd.dwItemSpec), draw->nmcd.rc,
                              static_cast<UINT>(draw->nmcd.uItemState))) {
            return CDRF_SKIPDEFAULT;
          }
          return CDRF_DODEFAULT;
        }
        return CDRF_DODEFAULT;
      }
      if (header->code == NM_DBLCLK) {
        int row = static_cast<int>(SendMessageW(dl.list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1),
                                                LVNI_FOCUSED));
        if (row >= 0 && static_cast<size_t>(row) < dl.items.size() &&
            dl.items[static_cast<size_t>(row)].state == "done") {
          std::string path = dl.items[static_cast<size_t>(row)].path;
          ShellExecuteA(window, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
      }
      return 0;
    }

    case WM_TIMER:
      if (wParam == DOWNLOAD_TIMER) refreshDownloads();
      return 0;

    case WM_CLOSE:
      KillTimer(window, DOWNLOAD_TIMER);
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      KillTimer(window, DOWNLOAD_TIMER);
      dl.window = nullptr;
      dl.list = nullptr;
      dl.head = nullptr;
      dl.pause = nullptr;
      dl.cancel = nullptr;
      dl.clear = nullptr;
      dl.folder = nullptr;
      return 0;
  }

  return DefWindowProcA(window, message, wParam, lParam);
}

void showDownloadsWindow() {
  if (dl.window) {
    ShowWindow(dl.window, SW_SHOW);
    SetForegroundWindow(dl.window);
    return;
  }
  HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g.window, GWLP_HINSTANCE));
  HWND window = CreateWindowExA(0, "PKG-LinkScrapperDownloads", "PKG-LinkScrapper - downloads", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 980, 520, nullptr, nullptr, instance,
                                nullptr);
  if (!window) return;
  SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(g.window));
  HICON icon = reinterpret_cast<HICON>(GetClassLongPtrA(g.window, GCLP_HICON));
  if (icon) {
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
  }
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  refreshDownloads();
}

// ---- sources ---------------------------------------------------------------

constexpr int kSourceColumnCount = 8;
const wchar_t* kSourceHeaders[] = {L"Source", L"Type", L"State", L"Pages", L"Depth", L"Trust",
                                   L"Yield", L"Links it crawls"};
const int kSourceWidths[] = {136, 70, 76, 58, 58, 78, 62, 900};

std::vector<std::string> sourceLinks(const Source& source) {
  std::vector<std::string> out;
  for (const std::string& url : source.urls) out.push_back(url);
  for (const std::string& url : source.searchUrl) out.push_back(url);
  for (const std::string& url : source.pagedUrl) out.push_back(url);
  if (!source.itemUrl.empty()) out.push_back(source.itemUrl);
  for (const std::string& url : source.instances) out.push_back(url);
  for (const std::string& query : source.query) {
    out.push_back(source.engine.empty() ? query : source.engine + ": " + query);
  }
  return out;
}

std::string sourceCell(const Source& source, int column, size_t yield) {
  switch (column) {
    case 0:
      return source.name;
    case 1:
      return source.type;
    case 2:
      return source.enabled ? "live" : "parked";
    case 3:
      return std::to_string(source.pages);
    case 4:
      return std::to_string(source.depth);
    case 5:
      return source.trusted ? "trusted" : std::string();
    case 6:
      return source.enabled ? std::to_string(yield) : std::string("-");
    default: {
      std::vector<std::string> links = sourceLinks(source);
      std::string out;
      for (size_t i = 0; i < links.size(); ++i) out += (i ? "   " : "") + links[i];
      return out;
    }
  }
}

bool writeUserConfig(const std::string& text, std::string& error) {
  CreateDirectoryA(userConfigDirectory().c_str(), nullptr);
  std::ofstream stream(userConfigFile(), std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "cannot write " + userConfigFile();
    return false;
  }
  stream << text;
  if (!stream.good()) {
    error = "the disk refused " + userConfigFile();
    return false;
  }
  error.clear();
  return true;
}

// flips one source between live and parked and writes the whole list back out,
// so the change survives the program and is visible in the file
bool setSourceEnabled(const std::string& name, bool wantEnabled, std::string& error) {
  Config probe;
  if (!loadConfig(probe, exeDirectory(), "", error)) return false;

  std::vector<Source> live;
  std::vector<Source> off;
  bool found = false;
  for (Source source : probe.sources) {
    if (source.name == name) {
      source.enabled = wantEnabled;
      found = true;
    }
    (source.enabled ? live : off).push_back(source);
  }
  for (Source source : probe.parked) {
    if (source.name == name) {
      source.enabled = wantEnabled;
      found = true;
    }
    (source.enabled ? live : off).push_back(source);
  }
  if (!found) {
    error = "no source called " + name;
    return false;
  }

  probe.sources = live;
  probe.parked = off;
  probe.disabled.clear();
  if (!writeUserConfig(renderConfig(probe), error)) return false;
  error.clear();
  return true;
}

std::string allSourceLinks() {
  std::string out;
  for (const Source& source : sx.rows) {
    out += "# " + source.name + " (" + source.type + ", " + (source.enabled ? "live" : "parked") +
           ")\r\n";
    for (const std::string& link : sourceLinks(source)) out += link + "\r\n";
    out += "\r\n";
  }
  return out;
}

void refreshSources() {
  if (!sx.window) return;
  Config probe;
  std::string error;
  if (!loadConfig(probe, exeDirectory(), "", error)) {
    setEditText(sx.head, "config error: " + error);
    return;
  }
  sx.rows.clear();
  for (const Source& source : probe.sources) sx.rows.push_back(source);
  for (const Source& source : probe.parked) sx.rows.push_back(source);

  std::map<std::string, size_t> yields;
  for (const Hit& hit : g.hits) ++yields[hit.source];

  int have = static_cast<int>(SendMessageW(sx.list, LVM_GETITEMCOUNT, 0, 0));
  while (have > static_cast<int>(sx.rows.size())) {
    SendMessageW(sx.list, LVM_DELETEITEM, static_cast<WPARAM>(have - 1), 0);
    --have;
  }
  for (size_t index = 0; index < sx.rows.size(); ++index) {
    const Source& source = sx.rows[index];
    size_t yield = yields.count(source.name) ? yields[source.name] : 0;
    std::vector<std::wstring> cells;
    for (int column = 0; column < kSourceColumnCount; ++column) {
      cells.push_back(widen(sourceCell(source, column, yield)));
    }
    if (static_cast<int>(index) >= have) {
      LVITEMW insert{};
      insert.mask = LVIF_TEXT;
      insert.iItem = static_cast<int>(index);
      insert.pszText = cells[0].data();
      SendMessageW(sx.list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert));
      ++have;
    }
    for (int column = 0; column < kSourceColumnCount; ++column) {
      LVITEMW sub{};
      sub.mask = LVIF_TEXT;
      sub.iItem = static_cast<int>(index);
      sub.iSubItem = column;
      sub.pszText = cells[static_cast<size_t>(column)].data();
      SendMessageW(sx.list, LVM_SETITEMTEXTW, static_cast<WPARAM>(index),
                   reinterpret_cast<LPARAM>(&sub));
    }
  }

  size_t live = 0;
  size_t links = 0;
  for (const Source& source : sx.rows) {
    if (source.enabled) ++live;
    links += sourceLinks(source).size();
  }
  std::string head = std::to_string(sx.rows.size()) + " sources  -  " + std::to_string(live) +
                     " live  -  " + std::to_string(sx.rows.size() - live) + " parked  -  ";
  head += std::to_string(links) + " urls ready  -  ";
  head += std::to_string(g.hits.size()) + " rows from the last hunt";
  if (!sx.note.empty()) head += "  -  " + sx.note;
  setEditText(sx.head, head);
  InvalidateRect(sx.list, nullptr, FALSE);
}

BOOL drawSourceRow(HDC dc, int position, const RECT& bounds, UINT itemState) {
  const Palette& p = palette();
  if (position < 0 || static_cast<size_t>(position) >= sx.rows.size()) return FALSE;
  const Source& source = sx.rows[static_cast<size_t>(position)];
  bool selected = (itemState & LVIS_SELECTED) != 0;

  size_t yield = 0;
  for (const Hit& hit : g.hits) {
    if (hit.source == source.name) ++yield;
  }

  COLORREF face = selected ? p.selection
                           : (static_cast<size_t>(position) % 2 ? p.rowAlt : p.listBg);
  HBRUSH brush = CreateSolidBrush(face);
  FillRect(dc, &bounds, brush);
  DeleteObject(brush);

  RECT line = bounds;
  line.top = line.bottom - 1;
  HBRUSH hair = CreateSolidBrush(p.separator);
  FillRect(dc, &line, hair);
  DeleteObject(hair);

  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(dc, g.font);

  int left = bounds.left;
  for (int column = 0; column < kSourceColumnCount; ++column) {
    int width = static_cast<int>(SendMessageW(sx.list, LVM_GETCOLUMNWIDTH, static_cast<WPARAM>(column),
                                              0));
    if (width <= 0) break;
    RECT cell{left, bounds.top, left + width, bounds.bottom};
    left += width;
    if (cell.left >= bounds.right) break;
    if (cell.right > bounds.right) cell.right = bounds.right;

    std::wstring text = widen(sourceCell(source, column, yield));
    if (text.empty()) continue;

    COLORREF ink = p.text;
    if (!selected) {
      if (column == 0) ink = source.enabled ? p.alive : p.muted;
      else if (column == 2) ink = source.enabled ? p.alive : p.stub;
      else if (column == 5) ink = p.recommended;
    }
    SetTextColor(dc, ink);
    InflateRect(&cell, -5, 0);
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (column == 3 || column == 4 || column == 6) flags |= DT_RIGHT;
    DrawTextW(dc, text.c_str(), -1, &cell, flags);
  }
  if (itemState & LVIS_FOCUS) {
    RECT ring = bounds;
    InflateRect(&ring, -2, -2);
    drawFocusOutline(dc, ring, (itemState & LVIS_SELECTED) ? p.text : p.muted, 0);
  }
  if (oldFont) SelectObject(dc, oldFont);
  return TRUE;
}

int focusedSourceRow() {
  return static_cast<int>(SendMessageW(sx.list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_FOCUSED));
}

void layoutSources() {
  if (!sx.window) return;
  RECT client{};
  GetClientRect(sx.window, &client);
  const int margin = 12;
  const int height = 28;
  const int gap = 8;
  const int headHeight = 22;

  int x = margin;
  const int widths[] = {130, 132, 150, 120, 96, 132};
  HWND buttons[] = {sx.toggle, sx.copy, sx.write, sx.open, sx.reload, sx.reset};
  int bottom = client.bottom - margin - height;
  for (int i = 0; i < 6; ++i) {
    MoveWindow(buttons[i], x, bottom, widths[i], height, TRUE);
    x += widths[i] + gap;
  }
  const int top = margin + headHeight + gap;
  MoveWindow(sx.head, margin, margin, client.right - margin * 2, headHeight, TRUE);
  MoveWindow(sx.list, margin, top, client.right - margin * 2,
             std::max(80, bottom - gap - top), TRUE);
}

void insertSourceColumn(int index) {
  LVCOLUMNW column{};
  column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
  column.fmt = LVCFMT_LEFT;
  if (index == 3 || index == 4 || index == 6) column.fmt = LVCFMT_RIGHT;
  column.cx = kSourceWidths[index];
  column.pszText = const_cast<LPWSTR>(kSourceHeaders[index]);
  column.iSubItem = index;
  SendMessageW(sx.list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&column));
}

void themeSources() {
  if (!sx.window) return;
  const Palette& p = palette();
  BOOL immersive = g.darkMode ? TRUE : FALSE;
  DwmSetWindowAttribute(sx.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));
  DWORD corners = DWMWCP_ROUND;
  DwmSetWindowAttribute(sx.window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  SetWindowPos(sx.window, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

  const wchar_t* scheme = g.darkMode ? L"DarkMode_Explorer" : L"";
  for (HWND control : {sx.head, sx.toggle, sx.copy, sx.write, sx.open, sx.reload, sx.reset}) {
    themeWindow(control, scheme);
  }
  themeWindow(sx.list, g.darkMode ? L"DarkMode_ItemsView" : L"");
  paintListHeader(sx.list);
  installFlatFrame(sx.list);

  SendMessageW(sx.list, LVM_SETBKCOLOR, 0, static_cast<LPARAM>(p.listBg));
  SendMessageW(sx.list, LVM_SETTEXTCOLOR, 0, static_cast<LPARAM>(p.text));
  SendMessageW(sx.list, LVM_SETTEXTBKCOLOR, 0, static_cast<LPARAM>(p.listBg));
  SendMessageW(sx.list, LVM_SETIMAGELIST, LVSIL_SMALL, 0);
  HIMAGELIST spacer = ImageList_Create(1, 24, ILC_COLOR32, 1, 1);
  if (spacer) SendMessageW(sx.list, LVM_SETIMAGELIST, LVSIL_SMALL, reinterpret_cast<LPARAM>(spacer));

  InvalidateRect(sx.window, nullptr, TRUE);
  refreshSources();
}

LRESULT CALLBACK sourcesProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
      sx.window = window;
      sx.head = createControl(window, "STATIC", "", SS_LEFTNOWORDWRAP, 0, IDC_SX_HEAD, instance);
      SendMessageW(sx.head, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold), TRUE);

      sx.list = CreateWindowExA(0, WC_LISTVIEWA, "",
                                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 10, 10,
                                window, reinterpret_cast<HMENU>(IDC_SX_LIST), instance, nullptr);
      SendMessageW(sx.list, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
      ListView_SetExtendedListViewStyle(sx.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                         LVS_EX_HEADERDRAGDROP);
      for (int index = 0; index < kSourceColumnCount; ++index) insertSourceColumn(index);

      sx.toggle = createControl(window, "BUTTON", "Park / restore", BS_OWNERDRAW | WS_TABSTOP, 0,
                                IDC_SX_TOGGLE, instance);
      sx.copy = createControl(window, "BUTTON", "Copy all links", BS_OWNERDRAW | WS_TABSTOP, 0,
                              IDC_SX_COPY, instance);
      sx.write = createControl(window, "BUTTON", "Write config", BS_OWNERDRAW | WS_TABSTOP, 0,
                               IDC_SX_WRITE, instance);
      sx.open = createControl(window, "BUTTON", "Open folder", BS_OWNERDRAW | WS_TABSTOP, 0,
                              IDC_SX_OPEN, instance);
      sx.reload = createControl(window, "BUTTON", "Reload", BS_OWNERDRAW | WS_TABSTOP, 0,
                                IDC_SX_RELOAD, instance);
      sx.reset = createControl(window, "BUTTON", "Restore shipped", BS_OWNERDRAW | WS_TABSTOP, 0,
                               IDC_SX_RESET, instance);

      layoutSources();
      themeSources();
      return 0;
    }

    case WM_SIZE:
      layoutSources();
      return 0;

    case WM_ERASEBKGND: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      RECT client{};
      GetClientRect(window, &client);
      FillRect(dc, &client, g.windowBrush ? g.windowBrush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
      return 1;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, palette().text);
      SetBkColor(dc, palette().window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_CTLCOLORBTN: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, palette().text);
      SetBkColor(dc, palette().window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
      if (item->CtlType == ODT_BUTTON) return drawButton(item);
      return FALSE;
    }

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = 760;
      info->ptMinTrackSize.y = 340;
      return 0;
    }

    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case IDC_SX_TOGGLE: {
          int row = focusedSourceRow();
          if (row < 0 || static_cast<size_t>(row) >= sx.rows.size()) {
            setEditText(sx.head, "pick a source first - Park / restore switches it out of the hunt");
            break;
          }
          bool wantEnabled = !sx.rows[static_cast<size_t>(row)].enabled;
          std::string error;
          if (!setSourceEnabled(sx.rows[static_cast<size_t>(row)].name, wantEnabled, error)) {
            sx.note = "could not change that: " + error;
            setStatusText(0, sx.note);
          } else {
            sx.note = sx.rows[static_cast<size_t>(row)].name +
                      (wantEnabled ? " is live again" : " is parked - the hunt skips it");
            setStatusText(0, sx.note);
            loadGoodStore(g.good);
            applyTrustLists();
          }
          refreshSources();
          return 0;
        }
        case IDC_SX_COPY: {
          std::string text = allSourceLinks();
          if (text.empty()) {
            setEditText(sx.head, "nothing to copy");
            break;
          }
          copyToClipboard(text);
          setStatusText(0, "copied every source and url - " + std::to_string(sx.rows.size()) +
                               " sources");
          return 0;
        }
        case IDC_SX_WRITE: {
          Config probe;
          std::string error;
          if (!loadConfig(probe, exeDirectory(), "", error)) {
            sx.note = "config error: " + error;
            refreshSources();
            break;
          }
          std::string text = renderConfig(probe);
          if (!writeUserConfig(text, error)) {
            sx.note = error;
            setStatusText(0, error);
          } else {
            sx.note = "wrote " + std::to_string(probe.sources.size() + probe.parked.size()) +
                      " sources to " + userConfigFile();
            setStatusText(0, sx.note);
          }
          refreshSources();
          return 0;
        }
        case IDC_SX_OPEN: {
          ShellExecuteA(window, "open", userConfigDirectory().c_str(), nullptr, nullptr,
                        SW_SHOWNORMAL);
          return 0;
        }
        case IDC_SX_RELOAD: {
          loadGoodStore(g.good);
          applyTrustLists();
          rebuildRows();
          populateList();
          sx.note = "re-read sources, trusted lists and the proven-link store";
          setStatusText(0, sx.note);
          setStatusText(2, sourceSummary());
          refreshSources();
          return 0;
        }
        case IDC_SX_RESET: {
          int answer = MessageBoxW(window,
                                   L"Restore the shipped source list?\n\n"
                                   L"This rewrites sources.json with the built-in list: every source "
                                   L"is live again and any edits made in the file are replaced.",
                                   L"PKG-LinkScrapper", MB_YESNO | MB_ICONQUESTION);
          if (answer != IDYES) {
            sx.note = "nothing restored";
            refreshSources();
            break;
          }
          std::string error;
          if (!writeUserConfig(embeddedConfigText(), error)) {
            sx.note = error;
            setStatusText(0, error);
          } else {
            sx.note = "shipped list written back to " + userConfigFile();
            setStatusText(0, sx.note);
            loadGoodStore(g.good);
            applyTrustLists();
          }
          refreshSources();
          return 0;
        }
        default:
          return DefWindowProcA(window, message, wParam, lParam);
      }
      return 0;
    }

    case WM_NOTIFY: {
      LPNMHDR note = reinterpret_cast<LPNMHDR>(lParam);
      if (note->hwndFrom != sx.list) return 0;
      if (note->code == NM_CUSTOMDRAW) {
        LPNMLVCUSTOMDRAW draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
          if (drawSourceRow(draw->nmcd.hdc, static_cast<int>(draw->nmcd.dwItemSpec), draw->nmcd.rc,
                            static_cast<UINT>(draw->nmcd.uItemState))) {
            return CDRF_SKIPDEFAULT;
          }
          return CDRF_DODEFAULT;
        }
        return CDRF_DODEFAULT;
      }
      if (note->code == NM_DBLCLK) {
        int row = focusedSourceRow();
        if (row >= 0 && static_cast<size_t>(row) < sx.rows.size()) {
          std::vector<std::string> links = sourceLinks(sx.rows[static_cast<size_t>(row)]);
          if (!links.empty()) {
            ShellExecuteA(window, "open", links[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            setStatusText(0, "opened " + sx.rows[static_cast<size_t>(row)].name + "'s first link");
          }
        }
        return 0;
      }
      return 0;
    }

    case WM_CLOSE:
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      sx.window = nullptr;
      sx.list = nullptr;
      sx.head = nullptr;
      sx.toggle = nullptr;
      sx.copy = nullptr;
      sx.write = nullptr;
      sx.open = nullptr;
      sx.reload = nullptr;
      sx.reset = nullptr;
      return 0;
  }

  return DefWindowProcA(window, message, wParam, lParam);
}

void showSourcesWindow() {
  if (sx.window) {
    ShowWindow(sx.window, SW_SHOW);
    SetForegroundWindow(sx.window);
    refreshSources();
    return;
  }
  HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g.window, GWLP_HINSTANCE));
  HWND window = CreateWindowExA(0, "PKG-LinkScrapperSources", "PKG-LinkScrapper - sources", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1240, 560, nullptr, nullptr, instance,
                                nullptr);
  if (!window) return;
  SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(g.window));
  HICON icon = reinterpret_cast<HICON>(GetClassLongPtrA(g.window, GCLP_HICON));
  if (icon) {
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
  }
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
      g.window = window;
      g.font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
      g.bold = CreateFontA(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
      g.mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FIXED_PITCH | FF_DONTCARE, "Consolas");

      g.hint = createControl(window, "STATIC",
                             "Games - one per line, or separated by ; . Ctrl+Enter hunts. Download "
                             "pulls the selected link, or every part of a set.",
                             0, 0, 0, instance);
      SendMessageW(g.hint, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold), TRUE);
      g.games = createControl(window, "EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN |
                                                     ES_AUTOVSCROLL,
                               0, IDC_GAMES, instance);
      g.console = createControl(window, "COMBOBOX", "",
                                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_TABSTOP, 0, IDC_CONSOLE,
                                instance);
      fillCombo(g.console, {"Both", "PS4", "PS5"}, 0);
      g.limitLabel = createControl(window, "STATIC", "links/game", SS_RIGHT, 0, IDC_LIMIT_LABEL, instance);
      g.limit = createControl(window, "EDIT", "40", ES_NUMBER | WS_TABSTOP | ES_CENTER, 0,
                              IDC_LIMIT, instance);
      g.thresholdLabel = createControl(window, "STATIC", "match", SS_RIGHT, 0, IDC_THRESHOLD_LABEL,
                                        instance);
      g.threshold = createControl(window, "EDIT", "0.72", WS_TABSTOP | ES_CENTER, 0,
                                  IDC_THRESHOLD, instance);
      g.verify = createControl(window, "BUTTON", "verify links", BS_OWNERDRAW | WS_TABSTOP, 0,
                               IDC_VERIFY, instance);
      g.checkVerify = true;
      g.recommended = createControl(window, "BUTTON", "recommended only",
                                    BS_OWNERDRAW | WS_TABSTOP, 0, IDC_RECOMMENDED, instance);

      loadThemePreference();
      g.dark = createControl(window, "BUTTON", "dark", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_DARK,
                             instance);
      g.checkDark = g.darkMode;

      g.hunt = createControl(window, "BUTTON", "Hunt", BS_OWNERDRAW | WS_TABSTOP, 0, IDC_HUNT,
                             instance);
      g.stop = createControl(window, "BUTTON", "Stop", BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, 0,
                             IDC_STOP, instance);
      g.reprobe = createControl(window, "BUTTON", "Re-check",
                                BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, 0, IDC_REPROBE, instance);
      g.exportBtn = createControl(window, "BUTTON", "Export", BS_OWNERDRAW | WS_TABSTOP, 0,
                                  IDC_EXPORT, instance);
      g.reload = createControl(window, "BUTTON", "Sources", BS_OWNERDRAW | WS_TABSTOP, 0,
                               IDC_RELOAD, instance);
      g.download = createControl(window, "BUTTON", "Download",
                                 BS_OWNERDRAW | WS_TABSTOP | WS_DISABLED, 0, IDC_DOWNLOAD, instance);

      g.filter = createControl(window, "EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, 0,
                               IDC_FILTER, instance);

      g.list = CreateWindowExA(0, WC_LISTVIEWA, "",
                               WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                               0, 0, 10, 10, window, reinterpret_cast<HMENU>(IDC_LIST), instance,
                               nullptr);
      SendMessageW(g.list, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
      ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                    LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
      for (int index = 0; index < kColumnCount; ++index) insertColumn(index);

      g.details = createControl(window, "EDIT", "",
                                ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                0, IDC_DETAILS, instance);
      SendMessageW(g.details, WM_SETFONT, reinterpret_cast<WPARAM>(g.mono), TRUE);

      g.status = CreateWindowExA(0, STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(IDC_STATUS), instance, nullptr);
      SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

      g.gamesProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
          g.games, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gamesSubclass)));

      layoutControls();
      applyTheme();
      SetTimer(window, DETAILS_TIMER, 400, nullptr);

      SetFocus(g.games);
      applyDeadVerdicts();
      loadDeadStore(g.dead);
      setDeadStore(&g.dead);
      loadGoodStore(g.good);
      applyTrustLists();
      setStatusText(0, "ready");
      setStatusText(1, "0 links");
      setStatusText(2, sourceSummary());
      return 0;
    }

    case WM_SIZE:
      layoutControls();
      return 0;

    case WM_ERASEBKGND: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      RECT client{};
      GetClientRect(window, &client);
      FillRect(dc, &client, g.windowBrush ? g.windowBrush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
      return 1;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      const Palette& p = palette();
      SetTextColor(dc, p.text);
      SetBkColor(dc, p.window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
      if (item->CtlType == ODT_BUTTON) {
        if (isCommandButton(static_cast<int>(item->CtlID))) return drawButton(item);
        return drawCheck(item);
      }
      if (item->CtlType != ODT_COMBOBOX || item->itemID < 0) return FALSE;
      const Palette& p = palette();
      bool selected = (item->itemState & (ODS_SELECTED | ODS_COMBOBOXEDIT)) != 0;
      HBRUSH background = CreateSolidBrush(selected ? p.recommended : p.field);
      FillRect(item->hDC, &item->rcItem, background);
      DeleteObject(background);
      SetTextColor(item->hDC, p.text);
      SetBkMode(item->hDC, TRANSPARENT);
      static const wchar_t* kConsoleItems[] = {L"Both", L"PS4", L"PS5"};
      const wchar_t* label = kConsoleItems[std::min<size_t>(
          static_cast<size_t>(item->itemID), sizeof(kConsoleItems) / sizeof(kConsoleItems[0]) - 1)];
      RECT text = item->rcItem;
      InflateRect(&text, -6, -2);
      DrawTextW(item->hDC, label, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
      if (item->itemState & ODS_FOCUS) {
        RECT ring = item->rcItem;
        InflateRect(&ring, -2, -2);
        drawFocusOutline(item->hDC, ring, p.muted, 0);
      }
      return TRUE;
    }

    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      const Palette& p = palette();
      SetTextColor(dc, p.text);
      SetBkColor(dc, p.field);
      return reinterpret_cast<LRESULT>(g.fieldBrush);
    }

    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      const Palette& p = palette();
      SetTextColor(dc, p.text);
      SetBkColor(dc, p.field);
      return reinterpret_cast<LRESULT>(g.fieldBrush);
    }

    case WM_CTLCOLORBTN: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      const Palette& p = palette();
      SetTextColor(dc, p.text);
      SetBkColor(dc, p.window);
      return reinterpret_cast<LRESULT>(g.windowBrush);
    }

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = 1040;
      info->ptMinTrackSize.y = 560;
      return 0;
    }

    case WM_SETTEXT: {
      auto* text = reinterpret_cast<const char*>(lParam);
      if (text) setEditText(g.games, text);
      return 0;
    }

    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case IDC_HUNT:
          startHuntFromUi();
          return 0;
        case IDC_STOP:
          requestAbort();
          httpCancelAll();
          setStatusText(0, "stopping");
          return 0;
        case IDC_DARK: {
          toggleCheck(IDC_DARK);
          g.darkMode = isChecked(IDC_DARK);
          saveThemePreference();
          applyTheme();
          setStatusText(0, g.darkMode ? "dark mode" : "light mode");
          return 0;
        }
        case IDC_REPROBE:
          recheckFromUi();
          return 0;
        case IDC_DOWNLOAD: {
          const Hit* hit = focusedHit();
          if (!hit) {
            setStatusText(0, "pick a row first - Download pulls that link, or every part of that set");
            break;
          }
          downloadRow(*hit);
          return 0;
        }
        case IDC_EXPORT:
          exportResults();
          return 0;
        case IDC_RELOAD:
          showSourcesWindow();
          return 0;
        case IDC_VERIFY:
          toggleCheck(IDC_VERIFY);
          setStatusText(0, isChecked(IDC_VERIFY) ? "links will be probed"
                                                 : "probing off - rows stay unverified");
          return 0;
        case IDC_RECOMMENDED:
          toggleCheck(IDC_RECOMMENDED);
          rebuildRows();
          populateList();
          setStatusText(0, recommendedOnly() ? "recommended only" : "showing everything");
          return 0;
        case IDC_FILTER:
          if (HIWORD(wParam) == EN_CHANGE) {
            rebuildRows();
            populateList();
          }
          return 0;
        default:
          return DefWindowProcA(window, message, wParam, lParam);
      }
    }

    case WM_NOTIFY: {
      LPNMHDR header = reinterpret_cast<LPNMHDR>(lParam);
      if (header->hwndFrom == g.list) {
        if (header->code == NM_CUSTOMDRAW) {
          LPNMLVCUSTOMDRAW draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
          switch (draw->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
              return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
              RECT bounds = draw->nmcd.rc;
              if (drawRow(draw->nmcd.hdc, static_cast<int>(draw->nmcd.dwItemSpec), bounds,
                          static_cast<UINT>(draw->nmcd.uItemState))) {
                return CDRF_SKIPDEFAULT;
              }
              size_t position = static_cast<size_t>(draw->nmcd.dwItemSpec);
              if (position >= g.view.size()) return CDRF_DODEFAULT;
              const Hit& hit = g.view[position];
              draw->clrText = rowColor(hit);
              draw->clrTextBk = palette().listBg;
              return CDRF_DODEFAULT;
            }
            default:
              return CDRF_DODEFAULT;
          }
        }
        if (header->code == NM_CLICK) {
          POINT where;
          GetCursorPos(&where);
          ScreenToClient(g.list, &where);
          struct HitTest {
            POINT pt;
            UINT flags;
            int item;
            int subItem;
          } probe{{where.x, where.y}, 0, -1, -1};
          SendMessageW(g.list, LVM_SUBHITTEST, 0, reinterpret_cast<LPARAM>(&probe));
          if (probe.item >= 0 && probe.subItem == kPartsColumn &&
              static_cast<size_t>(probe.item) < g.view.size()) {
            toggleParts(probe.item);
          }
          return 0;
        }
        if (header->code == LVN_KEYDOWN) {
          LPNMLVKEYDOWN key = reinterpret_cast<LPNMLVKEYDOWN>(lParam);
          int focused = static_cast<int>(
              SendMessageW(g.list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_FOCUSED));
          if (key->wVKey == VK_RIGHT) setPartsOpen(focused, true);
          else if (key->wVKey == VK_LEFT) setPartsOpen(focused, false);
          return 0;
        }
        if (header->code == LVN_COLUMNCLICK) {
          LPNMLISTVIEW view = reinterpret_cast<LPNMLISTVIEW>(lParam);
          if (g.sortColumn == view->iSubItem) {
            g.sortAscending = !g.sortAscending;
          } else {
            g.sortColumn = view->iSubItem;
            g.sortAscending = true;
          }
          rebuildRows();
          populateList();
          return 0;
        }
        if (header->code == LVN_ITEMCHANGED) {
          LPNMLISTVIEW view = reinterpret_cast<LPNMLISTVIEW>(lParam);
          if ((view->uChanged & LVIF_STATE) && (view->uNewState & (LVIS_FOCUSED | LVIS_SELECTED))) {
            showDetails();
          }
          return 0;
        }
        if (header->code == NM_DBLCLK) {
          const Hit* hit = focusedHit();
          if (hit) ShellExecuteA(window, "open", hit->url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        }
        if (header->code == NM_RCLICK) {
          int focused = static_cast<int>(
              SendMessageW(g.list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_FOCUSED));
          const Hit* hit = focusedHit();
          if (hit) {
            bool grouped = !hit->isChild && hit->partCount >= 2;
            std::vector<std::string> grab = downloadUrlsFor(*hit);
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, 1, grouped ? "Open first part" : "Open link");
            if (grouped) {
              AppendMenuA(menu, MF_STRING, 5, "Open all parts");
              AppendMenuA(menu, MF_STRING, 6, "Copy all part URLs");
              bool open = g.expanded.count(hit->url) != 0;
              AppendMenuA(menu, MF_STRING, 8, open ? "Hide the part list" : "Show the part list");
            }
            if (!grab.empty()) {
              AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
              std::string label = grouped
                                      ? "Download all parts (" + std::to_string(grab.size()) + ")"
                                  : "Download this file";
              AppendMenuA(menu, MF_STRING, 9, label.c_str());
            }
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING, 2, "Copy URL");
            AppendMenuA(menu, MF_STRING, 3, "Copy row");
            AppendMenuA(menu, MF_STRING, 7, "Copy details");
            AppendMenuA(menu, MF_STRING, 11, "Downloads window");
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING, 4, grouped ? "Never show this set again" : "Never show this link again");
            const bool alreadyReported = deadReportAlreadySent(hit->url);
            std::string reportLabel = alreadyReported ? "Dead link already reported" : "Report dead link";
            AppendMenuA(menu, MF_STRING, 12, reportLabel.c_str());
            POINT point;
            GetCursorPos(&point);
            int choice = static_cast<int>(
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window, nullptr));
            DestroyMenu(menu);
            if (choice == 1) {
              ShellExecuteA(window, "open", hit->url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (choice == 5) {
              for (const std::string& url : hit->partUrls) {
                ShellExecuteA(window, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
              }
              setStatusText(0, "opening " + std::to_string(hit->partUrls.size()) + " parts");
            } else if (choice == 6) {
              std::string all;
              for (const std::string& url : hit->partUrls) all += url + "\r\n";
              copyToClipboard(all);
              setStatusText(0, "copied " + std::to_string(hit->partUrls.size()) + " part urls");
            } else if (choice == 8) {
              toggleParts(focused);
            } else if (choice == 9) {
              startDownloads(grab);
            } else if (choice == 11) {
              showDownloadsWindow();
            } else if (choice == 2) {
              copyToClipboard(hit->url);
              setStatusText(0, "copied url");
            } else if (choice == 3) {
              copyToClipboard(hit->game + "\t" + consoleLabel(hit->console) + "\t" + hit->state + "\t" +
                              hit->trust + "\t" + hit->size + "\t" + partsLabel(*hit) + "\t" +
                              hit->host + "\t" + hit->title + "\t" + hit->source + "\t" + hit->url);
              setStatusText(0, "copied row");
            } else if (choice == 7) {
              copyToClipboard(detailsText(*hit));
              setStatusText(0, "copied details");
            } else if (choice == 4) {
              rememberDead(*hit, true);
              rebuildRows();
              populateList();
              setStatusText(0, grouped ? "will not show this set again"
                                       : "will not show this link again");
              setStatusText(2, sourceSummary() + "  |  " + std::to_string(g.dead.size()) +
                                   " remembered dead  |  " + std::to_string(g.good.size()) +
                                   " remembered good");
            } else if (choice == 12) {
              DeadReport report;
              report.game = hit->game;
              report.url = hit->url;
              report.host = hit->host;
              report.source = hit->source;
              report.state = hit->state;
              report.size = hit->size;
              report.title = hit->title;
              // a link the reporter calls dead does not come back next hunt,
              // reported or not
              rememberDead(*hit, true);
              rebuildRows();
              populateList();
              if (alreadyReported) {
                setStatusText(0, "already reported - hidden from the list");
              } else {
                setStatusText(0, "reporting dead link #" + deadReportCode(hit->url) + " ...");
                std::thread([report] {
                  std::string error;
                  bool sent = sendDeadReport(report, error);
                  std::string text = sent ? "dead link reported and hidden - the owner was tagged"
                                          : "report failed: " + error + " - the link is hidden locally";
                  PostMessageW(g.window, WM_APP_REPORT_DONE, sent ? 1 : 0,
                               reinterpret_cast<LPARAM>(new std::string(text)));
                }).detach();
              }
            }
          }
          return 0;
        }
      }
      return 0;
    }

    case WM_TIMER:
      if (wParam == PAINT_TIMER && g.dirty) {
        g.dirty = false;
        rebuildRows();
        populateList();
      } else if (wParam == DETAILS_TIMER) {
        showDetails();
      }
      return 0;

    case WM_APP_HIT: {
      auto* incoming = reinterpret_cast<Hit*>(lParam);
      if (incoming) {
        auto found = g.indexByUrl.find(incoming->url);
        if (found == g.indexByUrl.end()) {
          if (g.hits.size() < g.liveCap) {
            g.indexByUrl[incoming->url] = g.hits.size();
            g.hits.push_back(*incoming);
          }
        } else {
          g.hits[found->second] = *incoming;
        }
        delete incoming;
        g.dirty = true;
      }
      return 0;
    }

    case WM_APP_PROGRESS: {
      auto* text = reinterpret_cast<std::string*>(lParam);
      if (text) {
        if (g.running) {
          g.statusText = *text;
          setStatusText(0, g.statusText);
        }
        delete text;
      }
      return 0;
    }

    case WM_APP_RESULTS: {
      auto* snapshot = reinterpret_cast<std::vector<Hit>*>(lParam);
      if (snapshot) {
        if (wParam == 1) {
          // a re-check pass: fold the probed rows back into what is on screen
          size_t updated = 0;
          for (const Hit& hit : *snapshot) {
            auto found = g.indexByUrl.find(hit.url);
            if (found == g.indexByUrl.end()) continue;
            g.hits[found->second] = hit;
            ++updated;
          }
          delete snapshot;
          setBusy(false);
          setStatusText(0, "re-checked " + std::to_string(updated) + " links");
          std::string storeError;
          rememberGood(g.hits, storeError);
          loadGoodStore(g.good);
          applyTrustLists();
          rebuildRows();
          populateList();
          return 0;
        }

        bool verified = true;
        for (const Hit& hit : *snapshot) {
          if (verified) rememberDead(hit);
        }
        g.hits = std::move(*snapshot);
        delete snapshot;
        g.indexByUrl.clear();
        for (size_t i = 0; i < g.hits.size(); ++i) g.indexByUrl[g.hits[i].url] = i;
        g.dirty = false;
        rebuildRows();
        populateList();
      }
      return 0;
    }

    case WM_APP_REPORT_DONE: {
      auto* text = reinterpret_cast<std::string*>(lParam);
      if (text) {
        setStatusText(0, *text);
        delete text;
      }
      return 0;
    }

    case WM_APP_DONE: {
      auto* snapshot = reinterpret_cast<std::vector<Hit>*>(lParam);
      KillTimer(window, PAINT_TIMER);
      // enable-state is recomputed from g.running inside populateList, so the
      // busy flag has to drop before the grid is rebuilt
      setBusy(false);
      if (snapshot) {
        for (const Hit& hit : *snapshot) rememberDead(hit);
        g.hits = std::move(*snapshot);
        delete snapshot;
        g.indexByUrl.clear();
        for (size_t i = 0; i < g.hits.size(); ++i) g.indexByUrl[g.hits[i].url] = i;
        g.dirty = false;
        rebuildRows();
        populateList();
      }
      {
        if (wParam == 1) setStatusText(0, "stopped");

        std::string storeError;
        if (rememberGood(g.hits, storeError)) {
          loadGoodStore(g.good);
          applyTrustLists();
        } else {
          setStatusText(0, "good list: " + storeError);
        }

        // nothing should leave a run still wearing unverified
        size_t pending = 0;
        for (const Hit& hit : g.hits) {
          if (hit.state == "unverified" || hit.state == "skipped") ++pending;
        }
        if (pending) {
          setStatusText(0, "re-checking " + std::to_string(pending) + " leftover links");
          recheckFromUi();
        }

        if (!g.hits.empty()) showHelpPopup();
        setStatusText(2, sourceSummary() + "  |  " + std::to_string(g.dead.size()) +
                             " remembered dead  |  " + std::to_string(g.good.size()) +
                             " remembered good");
      }
      return 0;
    }

    case WM_CLOSE:
      if (g.running) requestAbort();
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      requestAbort();
      downloadsAbort();
      if (dl.window) DestroyWindow(dl.window);
      if (sx.window) DestroyWindow(sx.window);
      setLogSink(nullptr);
      KillTimer(window, DETAILS_TIMER);
      if (g.font) DeleteObject(g.font);
      if (g.bold) DeleteObject(g.bold);
      if (g.mono) DeleteObject(g.mono);
      if (g.windowBrush) DeleteObject(g.windowBrush);
      if (g.fieldBrush) DeleteObject(g.fieldBrush);
      if (g.rowSpacer) ImageList_Destroy(g.rowSpacer);
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcA(window, message, wParam, lParam);
}

std::vector<std::string> commandTokens() {
  std::vector<std::string> out;
  int count = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!argv) return out;
  for (int index = 1; index < count; ++index) out.push_back(narrow(argv[index]));
  LocalFree(argv);
  return out;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
  INITCOMMONCONTROLSEX init{};
  init.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&init);
  SetProcessDPIAware();
  // the folder picker is a shell object, so this thread needs a COM apartment
  gComNote = hexResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

  WNDCLASSA wc{};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  loadThemePreference();
  applyProcessTheme(g.darkMode);
  wc.hbrBackground = CreateSolidBrush(palette().window);
  wc.lpszClassName = "PKG-LinkScrapperWindow";
  if (!RegisterClassA(&wc)) return 1;

  WNDCLASSA dwc{};
  dwc.lpfnWndProc = downloadsProc;
  dwc.hInstance = instance;
  dwc.hIcon = wc.hIcon;
  dwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  dwc.hbrBackground = CreateSolidBrush(palette().window);
  dwc.lpszClassName = "PKG-LinkScrapperDownloads";
  RegisterClassA(&dwc);

  WNDCLASSA swc{};
  swc.lpfnWndProc = sourcesProc;
  swc.hInstance = instance;
  swc.hIcon = wc.hIcon;
  swc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  swc.hbrBackground = CreateSolidBrush(palette().window);
  swc.lpszClassName = "PKG-LinkScrapperSources";
  RegisterClassA(&swc);

  HWND window = CreateWindowExA(0, "PKG-LinkScrapperWindow", "PKG-LinkScrapper - PS4/PS5 link hunter",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 780, nullptr,
                                nullptr, instance, nullptr);
  if (!window) return 1;

  setLogSink(progressSink);
  ShowWindow(window, show);
  UpdateWindow(window);

  std::vector<std::string> tokens = commandTokens();
  std::vector<std::string> games;
  std::string console;
  int limit = 0;
  for (size_t index = 0; index < tokens.size(); ++index) {
    const std::string& token = tokens[index];
    if (token == "--console" && index + 1 < tokens.size()) {
      console = toLowerAscii(tokens[++index]);
    } else if (token == "--limit" && index + 1 < tokens.size()) {
      limit = std::atoi(tokens[++index].c_str());
    } else if (token == "--threshold" && index + 1 < tokens.size()) {
      setEditText(g.threshold, tokens[++index]);
    } else if (token == "--no-verify") {
      setChecked(IDC_VERIFY, false);
    } else if (token == "--recommended" || token == "--trusted-only") {
      setChecked(IDC_RECOMMENDED, true);
    } else if (!token.empty() && token[0] != '-') {
      std::string name = token;
      while (!name.empty() && name.front() == '"') name.erase(name.begin());
      while (!name.empty() && name.back() == '"') name.pop_back();
      if (!name.empty()) games.push_back(name);
    }
  }

  // launchers differ on whether they quote arguments. if no token carries a
  // space then nothing was quoted, so the plain words are one title, not many
  bool anyQuoted = false;
  for (const std::string& game : games) {
    if (game.find(' ') != std::string::npos) anyQuoted = true;
  }
  if (!anyQuoted && games.size() > 1) {
    std::string joined;
    for (const std::string& game : games) joined += (joined.empty() ? "" : " ") + game;
    games = {joined};
  }

  if (!games.empty()) {
    std::string joined;
    for (const std::string& game : games) joined += (joined.empty() ? "" : "\r\n") + game;
    setEditText(g.games, joined);
    if (console == "ps4") SendMessageW(g.console, CB_SETCURSEL, 1, 0);
    else if (console == "ps5") SendMessageW(g.console, CB_SETCURSEL, 2, 0);
    if (limit > 0) setEditText(g.limit, std::to_string(limit));
    SendMessageW(g.hunt, BM_CLICK, 0, 0);
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    // tab and arrow keys go to whichever of the two windows has focus
    HWND target = GetActiveWindow();
    if (target != dl.window) target = window;
    if (!IsDialogMessageW(target, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  CoUninitialize();
  return static_cast<int>(message.wParam);
}
