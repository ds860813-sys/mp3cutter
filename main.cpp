#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "bass.h"
#include "bassmix.h"
#include "basswasapi.h"
#include "resource.h"

#pragma comment(lib, "bass.lib")
#pragma comment(lib, "bassmix.lib")
#pragma comment(lib, "basswasapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

template <typename Left, typename Right>
auto MinValue(Left left, Right right) {
    using Common = std::common_type_t<Left, Right>;
    const Common a = static_cast<Common>(left);
    const Common b = static_cast<Common>(right);
    return a < b ? a : b;
}

template <typename Left, typename Right>
auto MaxValue(Left left, Right right) {
    using Common = std::common_type_t<Left, Right>;
    const Common a = static_cast<Common>(left);
    const Common b = static_cast<Common>(right);
    return a > b ? a : b;
}

template <typename Value, typename Low, typename High>
auto ClampValue(Value value, Low low, High high) {
    return MinValue(MaxValue(value, low), high);
}

constexpr int kBaseWidth = 1200;
constexpr int kBaseHeight = 460;
constexpr int kCaptionHeight = 54;
constexpr int kMargin = 24;
constexpr int kButtonHeight = 42;
constexpr int kButtonWidth = 150;
constexpr int kButtonGap = 8;
constexpr int kScrollHeight = 16;
constexpr int kSectionGap = 12;
constexpr int kInfoGap = 24;
constexpr int kInfoMinWidth = 250;
constexpr int kVolumeControlWidth = 168;
constexpr int kMenuWidth = 438;
constexpr int kMenuHeight = 400;
constexpr int kMenuButtonWidth = 88;
constexpr int kCloseButtonWidth = 52;
constexpr int kGripSize = 16;
constexpr int kMaxWaveColumns = 65536;
constexpr double kMaxZoom = 100.0;
constexpr int kPositionTimerMs = 50;
// Wave dragging does not need animation-level frame rates. Limiting it keeps
// the UI responsive on low-power machines without changing scroll behavior.
constexpr int kFrameTimerMs = 40;
constexpr int kTailGuardMs = 100;
constexpr int kStopFadeMs = 5;
constexpr int kMenuDeviceRows = 4;

constexpr int kIdOpen = 1001;
constexpr int kIdPlay = 1002;
constexpr int kIdPlayRange = 1003;
constexpr int kIdExport = 1004;
constexpr int kIdSetMarkerA = 1005;
constexpr int kIdSetMarkerB = 1006;

constexpr UINT_PTR kTimerPosition = 1;
constexpr UINT_PTR kTimerFrame = 2;
constexpr UINT kMessageLoadDone = WM_APP + 1;
constexpr UINT kMessageExportDone = WM_APP + 2;
constexpr UINT kMessageCloseMenu = WM_APP + 3;

const COLORREF kColorBack = RGB(18, 19, 22);
const COLORREF kColorPanel = RGB(25, 27, 31);
const COLORREF kColorPanelRaised = RGB(34, 37, 42);
const COLORREF kColorLine = RGB(53, 57, 64);
const COLORREF kColorText = RGB(229, 232, 237);
const COLORREF kColorDim = RGB(145, 151, 161);
const COLORREF kColorWaveArea = RGB(11, 13, 16);
const COLORREF kColorWave = RGB(72, 194, 120);
const COLORREF kColorWaveZero = RGB(49, 53, 59);
const COLORREF kColorMarkerA = RGB(85, 151, 235);
const COLORREF kColorMarkerB = RGB(222, 92, 84);
const COLORREF kColorCursor = RGB(244, 245, 248);
const COLORREF kColorButton = RGB(37, 40, 46);
const COLORREF kColorButtonActive = RGB(50, 82, 68);
const COLORREF kColorSlider = RGB(74, 80, 90);
const COLORREF kColorSliderFill = RGB(80, 180, 123);

const float kUiPresets[] = {0.70f, 0.85f, 1.00f};
const wchar_t* const kUiPresetNames[] = {
    L"Compact (70%)", L"Standard (85%)", L"Large (100%)"};
int g_uiPreset = 2;
float g_uiScale = 1.0f;

int S(int value) {
    return static_cast<int>(value * g_uiScale + (value >= 0 ? 0.5f : -0.5f));
}

enum class AudioOutputMode {
    Auto = 0,
    Shared = 1,
    Exclusive = 2
};

enum class AuditionKind {
    None,
    Cursor,
    MarkerRange
};

struct WaveView {
    std::vector<float> lo;
    std::vector<float> hi;
    float peak = 0.0f;
    uint64_t totalFrames = 0;
    int sampleRate = 0;
    int bitrateKbps = 0;
    double duration = 0.0;
};

struct LoadResult {
    uint64_t generation = 0;
    bool ok = false;
    std::wstring path;
    std::wstring error;
    WaveView wave;
};

struct WasapiDevice {
    int index = -1;
    std::wstring id;
    std::wstring name;
};

struct UiBrushes {
    HBRUSH back = nullptr;
    HBRUSH panel = nullptr;
    HBRUSH panelRaised = nullptr;
    HBRUSH line = nullptr;
    HBRUSH waveArea = nullptr;
    HBRUSH wave = nullptr;
    HBRUSH waveZero = nullptr;
    HBRUSH markerA = nullptr;
    HBRUSH markerB = nullptr;
    HBRUSH cursor = nullptr;
    HBRUSH button = nullptr;
    HBRUSH buttonActive = nullptr;
    HBRUSH slider = nullptr;
    HBRUSH sliderFill = nullptr;
};

UiBrushes g_brushes;
HWND g_hwnd = nullptr;
HWND g_btnOpen = nullptr;
HWND g_btnPlay = nullptr;
HWND g_btnRange = nullptr;
HWND g_btnExport = nullptr;
HFONT g_font = nullptr;
HFONT g_smallFont = nullptr;
HFONT g_titleFont = nullptr;

HBITMAP g_backBitmap = nullptr;
int g_backWidth = 0;
int g_backHeight = 0;

bool g_sourceReady = false;
bool g_loadingWaveform = false;
bool g_exportBusy = false;
std::wstring g_sourcePath;
std::wstring g_pendingPath;
WaveView g_wave;

double g_markerA = 0.0;
double g_markerB = 1.0;
double g_cursor = 0.0;
double g_zoom = 1.0;
double g_viewStart = 0.0;

int g_dragMarker = 0;
bool g_scrollDrag = false;
double g_scrollGrab = 0.0;
bool g_panPending = false;
bool g_panDrag = false;
POINT g_panStart = {};
double g_panGrab = 0.0;
bool g_frameTimerActive = false;
ULONGLONG g_lastFrameTick = 0;
RECT g_pendingDirty = {};
bool g_pendingDirtyValid = false;

bool g_menuOpen = false;
bool g_volumeDrag = false;
int g_menuDeviceScroll = 0;
std::vector<WasapiDevice> g_outputDevices;

bool g_audioInitialized = false;
bool g_playbackAvailable = false;
bool g_wasapiMode = false;
bool g_wasapiExclusive = false;
std::atomic<HSTREAM> g_wasapiMixer{0};
std::wstring g_outputDeviceId;
std::wstring g_outputDeviceName;
std::wstring g_outputLabel;
AudioOutputMode g_outputMode = AudioOutputMode::Auto;
int g_volume = 70;

HSTREAM g_playChannel = 0;
AuditionKind g_playKind = AuditionKind::None;
double g_playReturnPoint = 0.0;
double g_playRangeStop = 1.0;
uint64_t g_playGuardBytes = 0;
bool g_awakeHeld = false;

std::wstring g_lastOpenDirectory;
std::wstring g_lastExportDirectory;

std::thread g_waveThread;
std::atomic_bool g_waveCancel{false};
uint64_t g_loadGeneration = 0;
std::shared_ptr<LoadResult> g_loadResult;
std::mutex g_loadResultMutex;

struct ExportResult {
    bool ok = false;
    bool canceled = false;
    std::wstring error;
    std::wstring outputPath;
    double startFraction = 0.0;
    double stopFraction = 1.0;
    double duration = 0.0;
};

std::thread g_exportThread;
std::atomic_bool g_exportCancel{false};
std::shared_ptr<ExportResult> g_exportResult;
std::mutex g_exportResultMutex;

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

std::wstring FileStemOf(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

bool HasExtension(const std::wstring& path, const wchar_t* extension) {
    const size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, extension) == 0;
}

bool IsExistingDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          text.data(), static_cast<int>(text.size()),
                                          nullptr, 0);
    if (count <= 0) return std::wstring();
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string();
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count,
                        nullptr, nullptr);
    return result;
}

std::wstring Trim(std::wstring text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return std::wstring();
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring FullPathOf(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed) return path;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
    if (!GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                          buffer.data(), nullptr)) {
        return path;
    }
    return buffer.data();
}

std::wstring WindowsErrorText(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = length && text ? std::wstring(text, length) : std::wstring();
    if (text) LocalFree(text);
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

std::wstring FormatSeconds(double seconds) {
    if (!(seconds > 0.0)) seconds = 0.0;
    const int minutes = static_cast<int>(seconds) / 60;
    const double rest = seconds - static_cast<double>(minutes * 60);
    wchar_t text[64] = {};
    swprintf_s(text, L"%d:%05.2f", minutes, rest);
    return text;
}

HBRUSH MakeBrush(COLORREF color) {
    return CreateSolidBrush(color);
}

void EnsureBrushes() {
    if (g_brushes.back) return;
    g_brushes.back = MakeBrush(kColorBack);
    g_brushes.panel = MakeBrush(kColorPanel);
    g_brushes.panelRaised = MakeBrush(kColorPanelRaised);
    g_brushes.line = MakeBrush(kColorLine);
    g_brushes.waveArea = MakeBrush(kColorWaveArea);
    g_brushes.wave = MakeBrush(kColorWave);
    g_brushes.waveZero = MakeBrush(kColorWaveZero);
    g_brushes.markerA = MakeBrush(kColorMarkerA);
    g_brushes.markerB = MakeBrush(kColorMarkerB);
    g_brushes.cursor = MakeBrush(kColorCursor);
    g_brushes.button = MakeBrush(kColorButton);
    g_brushes.buttonActive = MakeBrush(kColorButtonActive);
    g_brushes.slider = MakeBrush(kColorSlider);
    g_brushes.sliderFill = MakeBrush(kColorSliderFill);
}

void DeleteBrushes() {
    HBRUSH* brushes[] = {
        &g_brushes.back, &g_brushes.panel, &g_brushes.panelRaised,
        &g_brushes.line, &g_brushes.waveArea, &g_brushes.wave,
        &g_brushes.waveZero, &g_brushes.markerA, &g_brushes.markerB,
        &g_brushes.cursor, &g_brushes.button, &g_brushes.buttonActive,
        &g_brushes.slider, &g_brushes.sliderFill};
    for (HBRUSH* brush : brushes) {
        if (*brush) DeleteObject(*brush);
        *brush = nullptr;
    }
}

void RebuildFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_titleFont) DeleteObject(g_titleFont);
    g_font = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_smallFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-S(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void FillRectColor(HDC dc, const RECT& rect, HBRUSH brush) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    FillRect(dc, &rect, brush);
}

void DrawTextInRect(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                    COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font ? font : g_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
              format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

RECT ButtonRect(int index) {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    int x = S(kMargin);
    for (int i = 0; i < index; ++i) x += S(kButtonWidth) + S(kButtonGap);
    const int bottom = client.bottom - S(kMargin);
    return {x, bottom - S(kButtonHeight), x + S(kButtonWidth), bottom};
}

int BottomButtonsWidth() {
    return S(kButtonWidth) * 4 + S(kButtonGap) * 3;
}

int MinimumWindowWidth() {
    return S(kMargin) * 2 + BottomButtonsWidth() + S(kInfoGap) * 2 +
           S(kVolumeControlWidth) + S(kInfoMinWidth);
}

RECT WaveAreaRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int bottomBarTop = client.bottom - S(kMargin) - S(kButtonHeight);
    return {
        S(kMargin),
        S(kCaptionHeight) + S(kMargin),
        MaxValue(S(kMargin) + 2, client.right - S(kMargin)),
        MaxValue(S(kCaptionHeight) + S(kMargin) + 2,
                   bottomBarTop - S(kSectionGap) - S(kScrollHeight))};
}

RECT WaveScrollRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int bottomBarTop = client.bottom - S(kMargin) - S(kButtonHeight);
    return {
        S(kMargin),
        bottomBarTop - S(kSectionGap) - S(kScrollHeight),
        MaxValue(S(kMargin) + 2, client.right - S(kMargin)),
        bottomBarTop - S(kSectionGap)};
}

RECT VolumeControlRect() {
    const RECT lastButton = ButtonRect(3);
    const int left = lastButton.right + S(kInfoGap);
    return {left, lastButton.top, left + S(kVolumeControlWidth),
            lastButton.bottom};
}

RECT VolumeSliderRect() {
    const RECT control = VolumeControlRect();
    return {control.left, control.bottom - S(12),
            control.right, control.bottom - S(7)};
}

RECT WaveInfoRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int left = VolumeControlRect().right + S(kInfoGap);
    return {
        left,
        client.bottom - S(kMargin) - S(kButtonHeight),
        MaxValue(left + 2, client.right - S(kMargin)),
        client.bottom - S(kMargin)};
}

RECT MenuButtonRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int right = client.right - S(kMargin) - S(kCloseButtonWidth) - S(8);
    return {right - S(kMenuButtonWidth), S(10),
            right, S(kCaptionHeight) - S(10)};
}

RECT CloseButtonRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    return {client.right - S(kMargin) - S(kCloseButtonWidth), S(10),
            client.right - S(kMargin), S(kCaptionHeight) - S(10)};
}

RECT SettingsMenuRect() {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int width = S(kMenuWidth);
    const int height =
        MinValue(S(kMenuHeight),
                   MaxValue(S(300), client.bottom - S(kCaptionHeight) - S(16)));
    return {
        MaxValue(S(kMargin), client.right - S(kMargin) - width),
        S(kCaptionHeight) + S(8),
        MaxValue(S(kMargin) + S(220), client.right - S(kMargin)),
        S(kCaptionHeight) + S(8) + height};
}

void SaveSettings();
void LayoutControls();
void RefreshButtonStates();
void ApplyVolume();
void CloseMenu();
void ApplyUiPreset(int preset);
void StartWaveLoad(const std::wstring& path);
void OpenMp3Dialog();
void ToggleAudition(AuditionKind kind);
void UpdatePosition();
void DrawMain(HDC dc);

std::wstring SettingsPath() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return L"settings.txt";
    std::wstring exe(path.data(), length);
    return DirectoryOf(exe) + L"\\settings.txt";
}

bool ReadAllBytes(const std::wstring& path, std::vector<unsigned char>* data) {
    if (!data) return false;
    data->clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    data->resize(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < data->size()) {
        DWORD got = 0;
        const DWORD request = static_cast<DWORD>(
            MinValue(data->size() - total, static_cast<size_t>(64 * 1024)));
        if (!ReadFile(file, data->data() + total, request, &got, nullptr) || got == 0) {
            CloseHandle(file);
            data->clear();
            return false;
        }
        total += got;
    }
    CloseHandle(file);
    return true;
}

bool WriteTextAtomic(const std::wstring& path, const std::string& content) {
    wchar_t suffix[96] = {};
    swprintf_s(suffix, L".tmp.%lu.%llu", GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring temporary = path + suffix;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t writtenTotal = 0;
    while (writtenTotal < content.size()) {
        DWORD written = 0;
        const DWORD request = static_cast<DWORD>(
            MinValue(content.size() - writtenTotal, static_cast<size_t>(64 * 1024)));
        if (!WriteFile(file, content.data() + writtenTotal, request, &written, nullptr) ||
            written == 0) {
            ok = false;
            break;
        }
        writtenTotal += written;
    }
    if (ok && !FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (ok) {
        ok = MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

const wchar_t* OutputModeName(AudioOutputMode mode) {
    switch (mode) {
    case AudioOutputMode::Shared: return L"shared";
    case AudioOutputMode::Exclusive: return L"exclusive";
    default: return L"auto";
    }
}

bool ParseOutputMode(const std::wstring& text, AudioOutputMode* mode) {
    if (!mode) return false;
    if (_wcsicmp(text.c_str(), L"auto") == 0) {
        *mode = AudioOutputMode::Auto;
    } else if (_wcsicmp(text.c_str(), L"shared") == 0) {
        *mode = AudioOutputMode::Shared;
    } else if (_wcsicmp(text.c_str(), L"exclusive") == 0) {
        *mode = AudioOutputMode::Exclusive;
    } else {
        return false;
    }
    return true;
}

int NearestUiPreset(float value) {
    int nearest = 2;
    float distance = (std::numeric_limits<float>::max)();
    for (int i = 0; i < 3; ++i) {
        const float current = std::fabs(value - kUiPresets[i]);
        if (current < distance) {
            distance = current;
            nearest = i;
        }
    }
    return nearest;
}

void LoadSettings() {
    g_uiPreset = 2;
    g_uiScale = 1.0f;
    g_volume = 70;
    g_outputMode = AudioOutputMode::Auto;
    g_outputDeviceId.clear();
    g_lastOpenDirectory.clear();
    g_lastExportDirectory.clear();

    std::vector<unsigned char> bytes;
    if (!ReadAllBytes(SettingsPath(), &bytes) || bytes.empty()) return;
    size_t offset = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
                            bytes[2] == 0xBF
                        ? 3
                        : 0;
    std::string content(reinterpret_cast<const char*>(bytes.data() + offset),
                        bytes.size() - offset);
    const std::wstring text = Utf8ToWide(content);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const size_t tab = line.find(L'\t');
        if (tab != std::wstring::npos) {
            const std::wstring key = Trim(line.substr(0, tab));
            const std::wstring value = Trim(line.substr(tab + 1));
            if (key == L"ui_scale") {
                wchar_t* tail = nullptr;
                const float scale = std::wcstof(value.c_str(), &tail);
                if (tail && tail != value.c_str() && std::isfinite(scale) &&
                    scale >= 0.5f && scale <= 1.5f) {
                    g_uiPreset = NearestUiPreset(scale);
                    g_uiScale = kUiPresets[g_uiPreset];
                }
            } else if (key == L"output_mode") {
                AudioOutputMode mode = AudioOutputMode::Auto;
                if (ParseOutputMode(value, &mode)) g_outputMode = mode;
            } else if (key == L"output_device") {
                if (_wcsicmp(value.c_str(), L"default") != 0) {
                    g_outputDeviceId = value;
                }
            } else if (key == L"last_open_directory") {
                if (IsExistingDirectory(value)) g_lastOpenDirectory = value;
            } else if (key == L"last_export_directory") {
                if (IsExistingDirectory(value)) g_lastExportDirectory = value;
            }
        }
        if (end >= text.size()) break;
        pos = end + 1;
    }
}

void SaveSettings() {
    const int preset =
        g_uiPreset >= 0 && g_uiPreset < 3 ? g_uiPreset : 2;
    std::wstring text;
    wchar_t line[128] = {};
    swprintf_s(line, L"ui_scale\t%.2f\r\n", static_cast<double>(kUiPresets[preset]));
    text += line;
    swprintf_s(line, L"output_mode\t%s\r\n", OutputModeName(g_outputMode));
    text += line;
    text += L"output_device\t";
    text += g_outputDeviceId.empty() ? L"default" : g_outputDeviceId;
    text += L"\r\n";
    text += L"last_open_directory\t";
    text += g_lastOpenDirectory;
    text += L"\r\n";
    text += L"last_export_directory\t";
    text += g_lastExportDirectory;
    text += L"\r\n";

    std::string content("\xEF\xBB\xBF", 3);
    content += WideToUtf8(text);
    WriteTextAtomic(SettingsPath(), content);
}

bool WriteAll(HANDLE file, const void* data, DWORD bytes) {
    const unsigned char* cursor = static_cast<const unsigned char*>(data);
    while (bytes) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, bytes, &written, nullptr) || written == 0) {
            return false;
        }
        cursor += written;
        bytes -= written;
    }
    return true;
}

struct Mp3FrameHeader {
    uint32_t sampleRate = 0;
    uint32_t frameBytes = 0;
    uint32_t samples = 0;
    uint16_t bitrateKbps = 0;
    uint16_t sideOffset = 0;
    uint16_t sideBytes = 0;
    int channels = 0;
};

struct Mp3Frame {
    uint64_t offset = 0;
    uint64_t sampleStart = 0;
    uint32_t bytes = 0;
    uint32_t samples = 0;
    uint32_t mainDataBytes = 0;
    uint16_t mainDataBegin = 0;
    bool metadataFrame = false;
};

struct Mp3Index {
    std::vector<Mp3Frame> frames;
    uint32_t sampleRate = 0;
    int channels = 0;
    uint64_t audioStart = 0;
    uint64_t audioEnd = 0;
    uint64_t totalSamples = 0;
};

class ReadOnlyFileMapping {
public:
    ReadOnlyFileMapping() = default;
    ReadOnlyFileMapping(const ReadOnlyFileMapping&) = delete;
    ReadOnlyFileMapping& operator=(const ReadOnlyFileMapping&) = delete;

    ~ReadOnlyFileMapping() {
        Reset();
    }

    bool Open(const std::wstring& path) {
        Reset();
        file_ = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(file_, &size) || size.QuadPart <= 0) {
            Reset();
            return false;
        }
        size_ = static_cast<uint64_t>(size.QuadPart);
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY,
                                      static_cast<DWORD>(size_ >> 32),
                                      static_cast<DWORD>(size_), nullptr);
        if (!mapping_) {
            Reset();
            return false;
        }
        data_ = static_cast<const unsigned char*>(
            MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) {
            Reset();
            return false;
        }
        return true;
    }

    const unsigned char* Data() const { return data_; }
    uint64_t Size() const { return size_; }

private:
    void Reset() {
        if (data_) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
        size_ = 0;
    }

    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    const unsigned char* data_ = nullptr;
    uint64_t size_ = 0;
};

bool ParseMp3FrameHeader(const unsigned char* bytes, uint64_t available,
                         Mp3FrameHeader* header) {
    if (!bytes || !header || available < 4) return false;
    if (bytes[0] != 0xFF || (bytes[1] & 0xE0) != 0xE0) return false;

    const int versionBits = (bytes[1] >> 3) & 0x03;
    const int layerBits = (bytes[1] >> 1) & 0x03;
    const int bitrateIndex = (bytes[2] >> 4) & 0x0F;
    const int sampleRateIndex = (bytes[2] >> 2) & 0x03;
    const int padding = (bytes[2] >> 1) & 0x01;
    const int emphasis = bytes[3] & 0x03;
    if (versionBits == 1 || layerBits != 1 || bitrateIndex == 0 ||
        bitrateIndex == 15 || sampleRateIndex == 3 || emphasis == 2) {
        return false;
    }

    static const uint16_t mpeg1Bitrates[] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t mpeg2Bitrates[] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t mpeg1Rates[] = {44100, 48000, 32000};
    static const uint32_t mpeg2Rates[] = {22050, 24000, 16000};
    static const uint32_t mpeg25Rates[] = {11025, 12000, 8000};

    const bool mpeg1 = versionBits == 3;
    const uint16_t bitrateKbps =
        mpeg1 ? mpeg1Bitrates[bitrateIndex] : mpeg2Bitrates[bitrateIndex];
    const uint32_t sampleRate =
        versionBits == 3 ? mpeg1Rates[sampleRateIndex]
                         : (versionBits == 2 ? mpeg2Rates[sampleRateIndex]
                                             : mpeg25Rates[sampleRateIndex]);
    if (!bitrateKbps || !sampleRate) return false;

    const uint32_t samples = mpeg1 ? 1152 : 576;
    const uint32_t frameBytes = static_cast<uint32_t>(
        (static_cast<uint64_t>(mpeg1 ? 144u : 72u) * bitrateKbps * 1000u) /
            sampleRate +
        static_cast<uint32_t>(padding));
    const bool hasCrc = (bytes[1] & 0x01) == 0;
    const int channels = ((bytes[3] >> 6) == 3) ? 1 : 2;
    const uint16_t sideOffset = static_cast<uint16_t>(4 + (hasCrc ? 2 : 0));
    const uint16_t sideBytes = static_cast<uint16_t>(
        mpeg1 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17));
    if (frameBytes <= static_cast<uint32_t>(sideOffset + sideBytes) ||
        available < frameBytes) {
        return false;
    }

    header->sampleRate = sampleRate;
    header->frameBytes = frameBytes;
    header->samples = samples;
    header->bitrateKbps = bitrateKbps;
    header->sideOffset = sideOffset;
    header->sideBytes = sideBytes;
    header->channels = channels;
    return true;
}

bool ParseMp3Frame(const unsigned char* data, uint64_t audioEnd, uint64_t offset,
                   Mp3Frame* frame, Mp3FrameHeader* header) {
    if (!data || !frame || offset >= audioEnd) return false;
    Mp3FrameHeader parsed = {};
    if (!ParseMp3FrameHeader(data + offset, audioEnd - offset, &parsed)) return false;
    const unsigned char* side = data + offset + parsed.sideOffset;
    frame->offset = offset;
    frame->bytes = parsed.frameBytes;
    frame->samples = parsed.samples;
    frame->mainDataBytes = parsed.frameBytes - parsed.sideOffset - parsed.sideBytes;
    frame->mainDataBegin =
        static_cast<uint16_t>((static_cast<uint16_t>(side[0]) << 1) | (side[1] >> 7));
    const uint64_t sideEnd = offset + parsed.sideOffset + parsed.sideBytes;
    const auto hasAscii = [data, audioEnd](uint64_t at, const char* text) {
        if (at + 4 > audioEnd) return false;
        for (int i = 0; i < 4; ++i) {
            if (data[at + static_cast<uint64_t>(i)] !=
                static_cast<unsigned char>(text[i])) {
                return false;
            }
        }
        return true;
    };
    frame->metadataFrame =
        hasAscii(sideEnd, "Xing") || hasAscii(sideEnd, "Info") ||
        hasAscii(offset + 36, "VBRI");
    if (header) *header = parsed;
    return true;
}

bool IndexMp3File(const std::wstring& path, Mp3Index* index,
                  const std::atomic_bool& cancel, std::wstring* error) {
    if (!index) return false;
    *index = Mp3Index();
    ReadOnlyFileMapping file;
    if (!file.Open(path)) {
        if (error) *error = L"Could not open the MP3 file.";
        return false;
    }

    HSTREAM probe = BASS_StreamCreateFile(
        FALSE, path.c_str(), 0, 0,
        BASS_UNICODE | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    if (!probe) {
        if (error) {
            wchar_t text[160] = {};
            swprintf_s(text, L"BASS did not recognize the MP3 (error code %lu).",
                       BASS_ErrorGetCode());
            *error = text;
        }
        return false;
    }
    const QWORD start = BASS_StreamGetFilePosition(probe, BASS_FILEPOS_START);
    const QWORD end = BASS_StreamGetFilePosition(probe, BASS_FILEPOS_END);
    BASS_StreamFree(probe);
    if (start == static_cast<QWORD>(-1) || end == static_cast<QWORD>(-1) ||
        start >= file.Size() || end == 0) {
        if (error) *error = L"Could not determine the MP3 audio data boundaries.";
        return false;
    }

    const uint64_t audioStart = static_cast<uint64_t>(start);
    const uint64_t audioEnd =
        MinValue(file.Size(), audioStart + static_cast<uint64_t>(end));
    uint64_t firstFrameOffset = 0;
    Mp3FrameHeader firstHeader = {};
    bool foundFirst = false;
    for (uint64_t at = audioStart; at + 4 <= audioEnd; ++at) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        Mp3FrameHeader header = {};
        Mp3Frame frame = {};
        if (!ParseMp3Frame(file.Data(), audioEnd, at, &frame, &header)) continue;
        if (at + frame.bytes < audioEnd) {
            Mp3Frame next = {};
            Mp3FrameHeader nextHeader = {};
            if (!ParseMp3Frame(file.Data(), audioEnd, at + frame.bytes,
                               &next, &nextHeader) ||
                nextHeader.sampleRate != header.sampleRate ||
                nextHeader.channels != header.channels) {
                continue;
            }
        }
        firstFrameOffset = at;
        firstHeader = header;
        foundFirst = true;
        break;
    }
    if (!foundFirst) {
        if (error) *error = L"No valid MP3 Layer III frames were found in the file.";
        return false;
    }

    index->sampleRate = firstHeader.sampleRate;
    index->channels = firstHeader.channels;
    index->audioStart = firstFrameOffset;
    index->audioEnd = audioEnd;
    uint64_t at = firstFrameOffset;
    while (at + 4 <= audioEnd) {
        if ((index->frames.size() & 0x3FFu) == 0 &&
            cancel.load(std::memory_order_relaxed)) {
            return false;
        }
        Mp3Frame frame = {};
        Mp3FrameHeader header = {};
        if (!ParseMp3Frame(file.Data(), audioEnd, at, &frame, &header) ||
            at + frame.bytes > audioEnd) {
            if (error) *error = L"The MP3 contains a damaged or incomplete frame.";
            return false;
        }
        if (header.sampleRate != index->sampleRate ||
            header.channels != index->channels) {
            if (error) *error = L"Changing format within an MP3 is not supported.";
            return false;
        }
        frame.sampleStart = index->totalSamples;
        index->totalSamples += frame.samples;
        index->frames.push_back(frame);
        at += frame.bytes;
    }
    if (index->frames.empty() || index->totalSamples == 0) {
        if (error) *error = L"No audio frames were found in the MP3.";
        return false;
    }
    return true;
}

float EvaluateMp3Boundary(HSTREAM decoder, int channels, double seconds, bool atEnd) {
    if (!decoder || channels <= 0) return 1.0f;
    BASS_CHANNELINFO info = {};
    if (!BASS_ChannelGetInfo(decoder, &info) || !info.freq) return 1.0f;
    const int frameCount = 64;
    double readAt = seconds;
    if (atEnd) {
        readAt = MaxValue(0.0, seconds -
                                     static_cast<double>(frameCount) /
                                         static_cast<double>(info.freq));
    }
    const QWORD bytePos = BASS_ChannelSeconds2Bytes(decoder, readAt);
    if (bytePos == static_cast<QWORD>(-1) ||
        !BASS_ChannelSetPosition(decoder, bytePos, BASS_POS_BYTE)) {
        return 1.0f;
    }

    std::vector<float> samples(
        static_cast<size_t>(frameCount) * static_cast<size_t>(channels));
    const DWORD got = BASS_ChannelGetData(
        decoder, samples.data(),
        static_cast<DWORD>(samples.size() * sizeof(float)));
    if (got == 0 || got == static_cast<DWORD>(-1)) return 1.0f;
    const size_t gotFrames =
        got / (sizeof(float) * static_cast<size_t>(channels));
    if (!gotFrames) return 1.0f;

    const size_t edgeFrame = atEnd ? gotFrames - 1 : 0;
    const size_t first = atEnd ? (gotFrames > 8 ? gotFrames - 8 : 0) : 0;
    const size_t last =
        atEnd ? gotFrames : MinValue(gotFrames, static_cast<size_t>(8));
    float edge = 0.0f;
    float average = 0.0f;
    for (size_t frame = first; frame < last; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const float value = std::fabs(
                samples[frame * static_cast<size_t>(channels) +
                        static_cast<size_t>(channel)]);
            average += value;
            if (frame == edgeFrame && value > edge) edge = value;
        }
    }
    average /= static_cast<float>(
        (last - first) * static_cast<size_t>(channels));
    return edge + average * 0.25f;
}

size_t Mp3ReservoirStart(const Mp3Index& index, size_t frame) {
    uint32_t needed = index.frames[frame].mainDataBegin;
    size_t start = frame;
    while (needed > 0 && start > 0) {
        --start;
        const uint32_t available = index.frames[start].mainDataBytes;
        needed = needed > available ? needed - available : 0;
    }
    return start;
}

bool ChooseMp3Range(const Mp3Index& index, double startFraction,
                    double stopFraction, HSTREAM decoder, size_t* startFrame,
                    size_t* endFrame) {
    if (!startFrame || !endFrame || index.frames.empty() ||
        !(startFraction >= 0.0 && stopFraction <= 1.0 &&
          startFraction < stopFraction)) {
        return false;
    }
    const uint64_t startTarget = static_cast<uint64_t>(
        startFraction * static_cast<double>(index.totalSamples));
    uint64_t stopTarget = static_cast<uint64_t>(
        std::ceil(stopFraction * static_cast<double>(index.totalSamples)));
    if (stopTarget <= startTarget) stopTarget = startTarget + 1;

    size_t targetStart = 0;
    while (targetStart + 1 < index.frames.size() &&
           index.frames[targetStart + 1].sampleStart <= startTarget) {
        ++targetStart;
    }
    size_t targetEnd = targetStart;
    while (targetEnd + 1 < index.frames.size() &&
           index.frames[targetEnd + 1].sampleStart < stopTarget) {
        ++targetEnd;
    }

    const size_t searchLimit = MinValue(targetEnd, targetStart + 8);
    float bestScore = (std::numeric_limits<float>::max)();
    size_t bestStart = targetEnd;
    bool foundStart = false;
    for (size_t candidate = targetStart; candidate <= searchLimit; ++candidate) {
        if (index.frames[candidate].metadataFrame) continue;
        const size_t candidateStart = Mp3ReservoirStart(index, candidate);
        if (candidateStart > targetEnd) continue;
        const uint64_t startSample = index.frames[candidateStart].sampleStart;
        const uint64_t distanceSamples =
            startSample >= startTarget ? startSample - startTarget
                                       : startTarget - startSample;
        const double distanceFrames =
            static_cast<double>(distanceSamples) /
            static_cast<double>(index.frames[candidateStart].samples);
        const double seconds =
            static_cast<double>(startSample) / index.sampleRate;
        const float score =
            EvaluateMp3Boundary(decoder, index.channels, seconds, false) +
            static_cast<float>(distanceFrames) * 0.005f;
        if (score < bestScore) {
            bestScore = score;
            bestStart = candidateStart;
            foundStart = true;
        }
    }
    if (!foundStart) return false;

    size_t bestEnd = targetEnd;
    bestScore = (std::numeric_limits<float>::max)();
    const size_t endBegin = targetEnd > bestStart ? targetEnd - 1 : bestStart;
    const size_t endLimit =
        MinValue(index.frames.size() - 1, targetEnd + 1);
    for (size_t i = endBegin; i <= endLimit; ++i) {
        if (i < bestStart) continue;
        const Mp3Frame& frame = index.frames[i];
        const double seconds =
            static_cast<double>(frame.sampleStart + frame.samples) /
            index.sampleRate;
        const int64_t distance =
            static_cast<int64_t>(i) - static_cast<int64_t>(targetEnd);
        const float score =
            EvaluateMp3Boundary(decoder, index.channels, seconds, true) +
            std::fabs(static_cast<float>(distance)) * 0.005f;
        if (score < bestScore) {
            bestScore = score;
            bestEnd = i;
        }
    }
    if (bestEnd < bestStart) return false;
    *startFrame = bestStart;
    *endFrame = bestEnd;
    return true;
}

ExportResult ExportMp3Range(const std::wstring& source, const std::wstring& target,
                            double startFraction, double stopFraction,
                            std::atomic_bool& cancel) {
    ExportResult result;
    result.outputPath = target;
    result.startFraction = startFraction;
    result.stopFraction = stopFraction;
    if (!HasExtension(source, L".mp3")) {
        result.error = L"Lossless export supports MP3 files only.";
        return result;
    }
    if (_wcsicmp(FullPathOf(source).c_str(), FullPathOf(target).c_str()) == 0) {
        result.error = L"Cannot overwrite the source MP3.";
        return result;
    }

    Mp3Index index;
    std::wstring indexError;
    if (!IndexMp3File(source, &index, cancel, &indexError)) {
        result.canceled = cancel.load(std::memory_order_relaxed);
        result.error = result.canceled ? L"" : indexError;
        return result;
    }

    HSTREAM decoder = BASS_StreamCreateFile(
        FALSE, source.c_str(), 0, 0,
        BASS_UNICODE | BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    size_t startFrame = 0;
    size_t endFrame = 0;
    const bool rangeOk =
        decoder && ChooseMp3Range(index, startFraction, stopFraction, decoder,
                                  &startFrame, &endFrame);
    if (decoder) BASS_StreamFree(decoder);
    if (!rangeOk) {
        result.error = L"Could not select safe MP3 frame boundaries.";
        return result;
    }

    const Mp3Frame& first = index.frames[startFrame];
    const Mp3Frame& last = index.frames[endFrame];
    const uint64_t copyStart = first.offset;
    const uint64_t copyStop = last.offset + last.bytes;
    if (copyStop <= copyStart || copyStop > index.audioEnd) {
        result.error = L"Invalid MP3 frame range.";
        return result;
    }
    result.startFraction =
        static_cast<double>(first.sampleStart) /
        static_cast<double>(index.totalSamples);
    result.stopFraction =
        static_cast<double>(last.sampleStart + last.samples) /
        static_cast<double>(index.totalSamples);
    result.duration =
        static_cast<double>(last.sampleStart + last.samples - first.sampleStart) /
        static_cast<double>(index.sampleRate);

    wchar_t suffix[96] = {};
    swprintf_s(suffix, L".tmp.%lu.%llu", GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    std::wstring temporary = target + suffix;
    HANDLE output = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && output == INVALID_HANDLE_VALUE; ++attempt) {
        if (attempt) {
            swprintf_s(suffix, L".tmp.%lu.%llu.%d", GetCurrentProcessId(),
                       static_cast<unsigned long long>(GetTickCount64()), attempt);
            temporary = target + suffix;
        }
        output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             nullptr);
    }
    if (output == INVALID_HANDLE_VALUE) {
        result.error = L"Could not create a temporary file next to the save location.";
        return result;
    }

    ReadOnlyFileMapping sourceFile;
    if (!sourceFile.Open(source)) {
        CloseHandle(output);
        DeleteFileW(temporary.c_str());
        result.error = L"Could not reopen the source MP3.";
        return result;
    }

    bool writeOk = true;
    uint64_t at = copyStart;
    while (at < copyStop) {
        if (cancel.load(std::memory_order_relaxed)) {
            result.canceled = true;
            writeOk = false;
            break;
        }
        const DWORD chunk = static_cast<DWORD>(
            MinValue(copyStop - at, static_cast<uint64_t>(1024 * 1024)));
        if (!WriteAll(output, sourceFile.Data() + at, chunk)) {
            writeOk = false;
            result.error = L"Failed to write MP3: " +
                           WindowsErrorText(GetLastError());
            break;
        }
        at += chunk;
    }
    if (writeOk && !FlushFileBuffers(output)) {
        writeOk = false;
        result.error = L"Could not flush MP3 data to disk: " +
                       WindowsErrorText(GetLastError());
    }
    CloseHandle(output);
    if (!writeOk) {
        DeleteFileW(temporary.c_str());
        if (result.canceled) result.error.clear();
        return result;
    }

    HSTREAM verify = BASS_StreamCreateFile(
        FALSE, temporary.c_str(), 0, 0,
        BASS_UNICODE | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    if (!verify) {
        DeleteFileW(temporary.c_str());
        wchar_t text[180] = {};
        swprintf_s(text, L"The exported MP3 cannot be opened by BASS (error code %lu).",
                   BASS_ErrorGetCode());
        result.error = text;
        return result;
    }
    BASS_StreamFree(verify);
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD errorCode = GetLastError();
        DeleteFileW(temporary.c_str());
        result.error = L"Could not replace the destination MP3: " +
                       WindowsErrorText(errorCode);
        return result;
    }
    result.ok = true;
    return result;
}

DWORD CALLBACK WasapiProc(void* buffer, DWORD length, void* user) {
    (void)user;
    const HSTREAM mixer =
        g_wasapiMixer.load(std::memory_order_relaxed);
    if (!mixer) return 0;
    const DWORD data = BASS_ChannelGetData(mixer, buffer, length);
    return data == static_cast<DWORD>(-1) ? 0 : data;
}

DWORD CALLBACK WasapiLatencyProc(HSTREAM handle, void* user) {
    (void)handle;
    (void)user;
    const DWORD buffered = BASS_WASAPI_GetData(nullptr, BASS_DATA_AVAILABLE);
    return buffered == static_cast<DWORD>(-1) ? 0 : buffered;
}

void RefreshOutputDevices() {
    g_outputDevices.clear();
    BASS_WASAPI_DEVICEINFO info = {};
    for (int index = 0; BASS_WASAPI_GetDeviceInfo(
                            static_cast<DWORD>(index), &info);
         ++index) {
        if (!(info.flags & BASS_DEVICE_ENABLED) ||
            (info.flags & (BASS_DEVICE_INPUT | BASS_DEVICE_LOOPBACK))) {
            continue;
        }
        WasapiDevice device;
        device.index = index;
        device.id = info.id ? Utf8ToWide(info.id) : std::wstring();
        device.name = info.name ? Utf8ToWide(info.name) : std::wstring();
        if (device.name.empty()) device.name = L"Audio device";
        g_outputDevices.push_back(device);
    }
    const int total = static_cast<int>(g_outputDevices.size());
    g_menuDeviceScroll =
        MaxValue(0, MinValue(g_menuDeviceScroll, MaxValue(0, total - kMenuDeviceRows)));
}

bool FindWasapiOutputDevice(int* device, DWORD* mixfreq, std::wstring* name,
                            std::wstring* stableId) {
    BASS_WASAPI_DEVICEINFO info = {};
    int fallbackDevice = -1;
    int firstDevice = -1;
    DWORD fallbackFreq = 48000;
    DWORD firstFreq = 48000;
    std::wstring fallbackName;
    std::wstring fallbackId;
    std::wstring firstName;
    std::wstring firstId;
    for (int i = 0; BASS_WASAPI_GetDeviceInfo(static_cast<DWORD>(i), &info); ++i) {
        if (!(info.flags & BASS_DEVICE_ENABLED) ||
            (info.flags & (BASS_DEVICE_INPUT | BASS_DEVICE_LOOPBACK))) {
            continue;
        }
        const std::wstring deviceName =
            info.name ? Utf8ToWide(info.name) : std::wstring();
        const std::wstring deviceId =
            info.id ? Utf8ToWide(info.id) : std::wstring();
        if (!g_outputDeviceId.empty() &&
            _wcsicmp(deviceId.c_str(), g_outputDeviceId.c_str()) == 0) {
            if (device) *device = i;
            if (mixfreq) *mixfreq = info.mixfreq ? info.mixfreq : 48000;
            if (name) *name = deviceName;
            if (stableId) *stableId = deviceId;
            return true;
        }
        if (firstDevice < 0) {
            firstDevice = i;
            firstFreq = info.mixfreq ? info.mixfreq : 48000;
            firstName = deviceName;
            firstId = deviceId;
        }
        if ((info.flags & BASS_DEVICE_DEFAULT) && fallbackDevice < 0) {
            fallbackDevice = i;
            fallbackFreq = info.mixfreq ? info.mixfreq : 48000;
            fallbackName = deviceName;
            fallbackId = deviceId;
        }
    }
    if (fallbackDevice < 0) {
        fallbackDevice = firstDevice;
        fallbackFreq = firstFreq;
        fallbackName = firstName;
        fallbackId = firstId;
    }
    if (fallbackDevice < 0) return false;
    if (device) *device = fallbackDevice;
    if (mixfreq) *mixfreq = fallbackFreq;
    if (name) *name = fallbackName;
    if (stableId) *stableId = fallbackId;
    return true;
}

bool TryInitWasapiOutput(int device, bool exclusive) {
    const DWORD baseFlags = BASS_WASAPI_AUTOFORMAT | BASS_WASAPI_BUFFER;
    const float buffers[2] = {0.2f, 0.3f};
    const float periods[2] = {0.03f, 0.05f};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD flags = baseFlags |
                            (exclusive ? BASS_WASAPI_EXCLUSIVE : 0);
        if (!BASS_WASAPI_Init(device, 0, 0, flags, buffers[attempt],
                              periods[attempt], WasapiProc, nullptr)) {
            continue;
        }
        BASS_WASAPI_INFO info = {};
        if (!BASS_WASAPI_GetInfo(&info) || !info.freq || !info.chans) {
            BASS_WASAPI_Free();
            continue;
        }
        HSTREAM mixer = BASS_Mixer_StreamCreate(
            info.freq, info.chans,
            BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_MIXER_POSEX |
                BASS_MIXER_NONSTOP);
        if (mixer && !BASS_Mixer_StreamSetLatency(
                         mixer, WasapiLatencyProc, nullptr)) {
            BASS_WASAPI_Free();
            BASS_StreamFree(mixer);
            continue;
        }
        g_wasapiMixer.store(mixer, std::memory_order_relaxed);
        if (mixer && BASS_WASAPI_Start()) {
            g_wasapiMode = true;
            g_wasapiExclusive = exclusive;
            g_outputLabel = exclusive ? L"WASAPI, exclusive mode"
                                      : L"WASAPI, shared mode";
            return true;
        }
        BASS_WASAPI_Free();
        mixer = g_wasapiMixer.exchange(0, std::memory_order_relaxed);
        if (mixer) BASS_StreamFree(mixer);
    }
    return false;
}

void FreeAudioOutput();

bool InitAudioOutput() {
    FreeAudioOutput();
    g_playbackAvailable = false;
    const bool systemDefaultRequested = g_outputDeviceId.empty();
    int device = -1;
    DWORD mixfreq = 48000;
    std::wstring name;
    std::wstring id;
    FindWasapiOutputDevice(&device, &mixfreq, &name, &id);
    g_outputDeviceName = name.empty() ? L"Default device" : name;

    if (device >= 0 && BASS_Init(0, mixfreq, 0, nullptr, nullptr)) {
        bool wasapiOk = false;
        if (g_outputMode != AudioOutputMode::Shared) {
            wasapiOk = TryInitWasapiOutput(device, true);
        }
        if (!wasapiOk && g_outputMode != AudioOutputMode::Exclusive) {
            wasapiOk = TryInitWasapiOutput(device, false);
        }
        if (!wasapiOk && g_outputMode == AudioOutputMode::Exclusive) {
            wasapiOk = TryInitWasapiOutput(device, false);
        }
        if (wasapiOk) {
            if (!systemDefaultRequested) g_outputDeviceId = id;
            g_audioInitialized = true;
            g_playbackAvailable = true;
            ApplyVolume();
            return true;
        }
        BASS_Free();
    }

    g_wasapiMode = false;
    g_wasapiExclusive = false;
    g_wasapiMixer.store(0, std::memory_order_relaxed);
    if (BASS_Init(-1, 44100, 0, nullptr, nullptr)) {
        g_audioInitialized = true;
        g_playbackAvailable = true;
        g_outputLabel = L"BASS system output";
        g_outputDeviceName = L"BASS system output";
        g_outputDeviceId.clear();
        ApplyVolume();
        return true;
    }

    // Decoding, waveform generation, and export remain usable without an
    // output device. Playback controls stay disabled in this mode.
    if (BASS_Init(0, 44100, 0, nullptr, nullptr)) {
        g_audioInitialized = true;
        g_playbackAvailable = false;
        g_outputLabel = L"playback unavailable";
        g_outputDeviceName = L"No output device available";
        g_outputDeviceId.clear();
        return true;
    }

    g_outputLabel = L"output unavailable";
    g_outputDeviceName = L"No output device available";
    return false;
}

void FreeAudioOutput() {
    if (g_playChannel) {
        if (g_wasapiMode) BASS_Mixer_ChannelRemove(g_playChannel);
        BASS_StreamFree(g_playChannel);
        g_playChannel = 0;
    }
    if (g_audioInitialized) {
        BASS_WASAPI_Free();
        HSTREAM mixer = g_wasapiMixer.exchange(0, std::memory_order_relaxed);
        if (mixer) BASS_StreamFree(mixer);
        BASS_Free();
    }
    g_audioInitialized = false;
    g_playbackAvailable = false;
    g_wasapiMode = false;
    g_wasapiExclusive = false;
}

bool AnySoundPlaying() {
    if (!g_playChannel) return false;
    const DWORD state = g_wasapiMode ? BASS_Mixer_ChannelIsActive(g_playChannel)
                                     : BASS_ChannelIsActive(g_playChannel);
    return state == BASS_ACTIVE_PLAYING;
}

void ApplyKeepAwake() {
    const bool want = AnySoundPlaying();
    if (want == g_awakeHeld) return;
    g_awakeHeld = want;
    SetThreadExecutionState(
        want ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
             : ES_CONTINUOUS);
}

void ApplyVolume() {
    if (!g_playChannel) return;
    BASS_ChannelSetAttribute(g_playChannel, BASS_ATTRIB_VOL,
                             static_cast<float>(g_volume) / 100.0f);
}

double ViewSpan() {
    return 1.0 / g_zoom;
}

void ClampView() {
    g_zoom = MaxValue(1.0, MinValue(g_zoom, kMaxZoom));
    const double span = ViewSpan();
    if (span >= 1.0) {
        g_viewStart = 0.0;
        return;
    }
    g_viewStart = MaxValue(0.0, MinValue(g_viewStart, 1.0 - span));
}

double FracAt(const RECT& area, int x) {
    const double width = area.right > area.left ? area.right - area.left : 1.0;
    double fraction =
        g_viewStart + (static_cast<double>(x - area.left) / width) * ViewSpan();
    return MaxValue(0.0, MinValue(fraction, 1.0));
}

int XOfFrac(const RECT& area, double fraction) {
    const double width = area.right > area.left ? area.right - area.left : 1.0;
    return area.left + static_cast<int>(
                           (fraction - g_viewStart) / ViewSpan() * width + 0.5);
}

bool FracVisible(double fraction) {
    return fraction >= g_viewStart &&
           fraction <= g_viewStart + ViewSpan();
}

void InvalidateStrip(double fraction) {
    if (!g_hwnd || fraction < 0.0 || !FracVisible(fraction)) return;
    const RECT area = WaveAreaRect();
    const int x = XOfFrac(area, fraction);
    RECT strip = {x - S(4), area.top, x + S(5), area.bottom};
    if (strip.left < area.left) strip.left = area.left;
    if (strip.right > area.right) strip.right = area.right;
    InvalidateRect(g_hwnd, &strip, FALSE);
}

void SetCursorFraction(double fraction, bool force) {
    fraction = MaxValue(0.0, MinValue(fraction, 1.0));
    if (!force && std::fabs(fraction - g_cursor) < 0.0000001) return;
    const double old = g_cursor;
    g_cursor = fraction;
    InvalidateStrip(old);
    InvalidateStrip(fraction);
}

bool WaveWindowAvailable() {
    return g_sourceReady && !g_loadingWaveform && !g_exportBusy;
}

bool CreateDecodeWaveform(const std::wstring& path,
                          const std::atomic_bool& cancel, WaveView* output,
                          std::wstring* error) {
    if (!output) return false;
    *output = WaveView();
    HSTREAM decoder = BASS_StreamCreateFile(
        FALSE, path.c_str(), 0, 0,
        BASS_UNICODE | BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE |
            BASS_STREAM_PRESCAN);
    if (!decoder) {
        if (error) {
            wchar_t text[180] = {};
            swprintf_s(text, L"BASS could not open the MP3 (error code %lu).",
                       BASS_ErrorGetCode());
            *error = text;
        }
        return false;
    }
    BASS_CHANNELINFO info = {};
    if (!BASS_ChannelGetInfo(decoder, &info) || info.chans <= 0) {
        BASS_StreamFree(decoder);
        if (error) *error = L"Could not read the MP3 parameters.";
        return false;
    }
    output->sampleRate = static_cast<int>(info.freq);
    float bitrate = 0.0f;
    if (BASS_ChannelGetAttribute(decoder, BASS_ATTRIB_BITRATE, &bitrate) &&
        bitrate > 0.0f) {
        output->bitrateKbps = static_cast<int>(bitrate + 0.5f);
    }

    const size_t frameBytes =
        sizeof(float) * static_cast<size_t>(info.chans);
    const QWORD lengthBytes = BASS_ChannelGetLength(decoder, BASS_POS_BYTE);
    const bool lengthKnown =
        lengthBytes && lengthBytes != static_cast<QWORD>(-1);
    bool collectWaveform = lengthKnown;
    if (lengthKnown) {
        output->totalFrames = static_cast<uint64_t>(lengthBytes / frameBytes);
        const size_t columns = static_cast<size_t>(
            MaxValue(static_cast<uint64_t>(2),
                       MinValue(output->totalFrames,
                                  static_cast<uint64_t>(kMaxWaveColumns))));
        output->lo.assign(columns, 0.0f);
        output->hi.assign(columns, 0.0f);
    }

    std::vector<float> raw(32 * 1024);
    std::vector<float> mono;
    uint64_t decoded = 0;
    auto addChunk = [&](size_t frames) {
        mono.resize(frames);
        for (size_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (DWORD channel = 0; channel < info.chans; ++channel) {
                sum += raw[frame * static_cast<size_t>(info.chans) +
                           static_cast<size_t>(channel)];
            }
            mono[frame] = sum / static_cast<float>(info.chans);
        }
        if (!collectWaveform || !output->totalFrames || output->lo.empty()) return;
        const size_t columns = output->lo.size();
        for (float value : mono) {
            size_t column = static_cast<size_t>(
                (decoded * static_cast<uint64_t>(columns)) /
                output->totalFrames);
            if (column >= columns) column = columns - 1;
            if (value < output->lo[column]) output->lo[column] = value;
            if (value > output->hi[column]) output->hi[column] = value;
            const float magnitude = std::fabs(value);
            if (magnitude > output->peak) output->peak = magnitude;
            ++decoded;
        }
    };

    auto decode = [&]() -> bool {
        for (;;) {
            if (cancel.load(std::memory_order_relaxed)) return false;
            const DWORD got = BASS_ChannelGetData(
                decoder, raw.data(),
                static_cast<DWORD>(raw.size() * sizeof(float)));
            if (got == static_cast<DWORD>(-1)) {
                if (BASS_ErrorGetCode() == BASS_ERROR_ENDED) break;
                if (error) {
                    wchar_t text[180] = {};
                    swprintf_s(text, L"MP3 decoding failed (error code %lu).",
                               BASS_ErrorGetCode());
                    *error = text;
                }
                return false;
            }
            if (got == 0) break;
            const size_t frames = got / frameBytes;
            if (!frames) break;
            if (collectWaveform) {
                addChunk(frames);
            } else {
                decoded += frames;
            }
        }
        return true;
    };

    bool ok = decode();
    if (ok && !lengthKnown) {
        const uint64_t totalFrames = decoded;
        if (!totalFrames || !BASS_ChannelSetPosition(decoder, 0, BASS_POS_BYTE)) {
            ok = false;
            if (error) *error = L"Could not read the MP3 again.";
        } else {
            output->totalFrames = totalFrames;
            const size_t columns = static_cast<size_t>(
                MaxValue(static_cast<uint64_t>(2),
                           MinValue(totalFrames,
                                      static_cast<uint64_t>(kMaxWaveColumns))));
            output->lo.assign(columns, 0.0f);
            output->hi.assign(columns, 0.0f);
            decoded = 0;
            collectWaveform = true;
            ok = decode();
        }
    }
    BASS_StreamFree(decoder);
    if (!ok) {
        if (cancel.load(std::memory_order_relaxed) && error) error->clear();
        return false;
    }
    if (!output->totalFrames || output->sampleRate <= 0) {
        if (error) *error = L"The MP3 contains no decodable audio.";
        return false;
    }
    output->duration = static_cast<double>(output->totalFrames) /
                       static_cast<double>(output->sampleRate);
    return true;
}

void DestroyPlayChannel() {
    if (!g_playChannel) return;
    if (g_wasapiMode && BASS_Mixer_ChannelGetMixer(g_playChannel)) {
        BASS_Mixer_ChannelRemove(g_playChannel);
    }
    BASS_StreamFree(g_playChannel);
    g_playChannel = 0;
}

void StopPlayback(bool restoreCursor) {
    const double returnPoint = g_playReturnPoint;
    const bool hadPlayback = g_playKind != AuditionKind::None;
    DestroyPlayChannel();
    g_playKind = AuditionKind::None;
    g_playRangeStop = 1.0;
    g_playGuardBytes = 0;
    if (restoreCursor && hadPlayback) {
        SetCursorFraction(returnPoint, true);
    }
    ApplyKeepAwake();
    RefreshButtonStates();
}

void FinishPlaybackNaturally() {
    const AuditionKind kind = g_playKind;
    const double rangeStop = g_playRangeStop;
    DestroyPlayChannel();
    g_playKind = AuditionKind::None;
    g_playRangeStop = 1.0;
    g_playGuardBytes = 0;
    SetCursorFraction(kind == AuditionKind::MarkerRange ? rangeStop : 1.0, true);
    ApplyKeepAwake();
    RefreshButtonStates();
}

bool StartPlaybackAt(AuditionKind kind, double start, double returnPoint) {
    if (!WaveWindowAvailable() || !g_playbackAvailable ||
        kind == AuditionKind::None) {
        return false;
    }
    DestroyPlayChannel();
    g_playKind = AuditionKind::None;

    const DWORD streamFlags = BASS_UNICODE |
                              (g_wasapiMode
                                   ? (BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE)
                                   : 0);
    HSTREAM channel = BASS_StreamCreateFile(
        FALSE, g_sourcePath.c_str(), 0, 0, streamFlags);
    if (!channel) {
        wchar_t text[260] = {};
        swprintf_s(text, L"Could not open the MP3 for playback.\n\n"
                         L"BASS error code: %lu",
                   BASS_ErrorGetCode());
        MessageBoxW(g_hwnd, text, L"Playback", MB_OK | MB_ICONERROR);
        return false;
    }

    const QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    if (!length || length == static_cast<QWORD>(-1)) {
        BASS_StreamFree(channel);
        MessageBoxW(g_hwnd, L"Could not determine the MP3 duration.",
                    L"Playback", MB_OK | MB_ICONERROR);
        return false;
    }

    start = MaxValue(0.0, MinValue(start, 1.0));
    if (kind == AuditionKind::Cursor && start >= 1.0) start = 0.0;
    if (kind == AuditionKind::MarkerRange) {
        start = MaxValue(g_markerA, MinValue(start, g_markerB));
        if (start >= g_markerB) start = g_markerA;
    }

    const QWORD startBytes =
        static_cast<QWORD>(start * static_cast<double>(length));
    const QWORD stopBytes =
        kind == AuditionKind::MarkerRange
            ? static_cast<QWORD>(g_markerB * static_cast<double>(length))
            : 0;
    const QWORD audibleBytes =
        stopBytes > startBytes ? stopBytes - startBytes : 0;
    QWORD guardBytes = 0;
    QWORD mixerStopBytes = 0;
    QWORD mixerPlayBytes = 0;
    const HSTREAM mixer =
        g_wasapiMixer.load(std::memory_order_relaxed);

    if (kind == AuditionKind::MarkerRange) {
        guardBytes =
            BASS_ChannelSeconds2Bytes(channel,
                                      static_cast<double>(kTailGuardMs) / 1000.0);
        if (guardBytes == static_cast<QWORD>(-1)) guardBytes = 0;

        // Mixer lengths and envelope positions are expressed in the mixer's
        // sample format, not in the source MP3 channel's byte units.
        if (g_wasapiMode && mixer) {
            const double audibleSeconds =
                BASS_ChannelBytes2Seconds(channel, audibleBytes);
            if (!(audibleSeconds > 0.0)) {
                BASS_StreamFree(channel);
                MessageBoxW(g_hwnd, L"Could not calculate the A-B duration.",
                            L"Playback", MB_OK | MB_ICONERROR);
                return false;
            }
            mixerStopBytes =
                BASS_ChannelSeconds2Bytes(mixer, audibleSeconds);
            mixerPlayBytes = BASS_ChannelSeconds2Bytes(
                mixer, audibleSeconds +
                           static_cast<double>(kTailGuardMs) / 1000.0);
            if (mixerStopBytes == static_cast<QWORD>(-1) ||
                mixerPlayBytes == static_cast<QWORD>(-1)) {
                BASS_StreamFree(channel);
                MessageBoxW(g_hwnd,
                            L"Could not convert the A-B range for playback.",
                            L"Playback", MB_OK | MB_ICONERROR);
                return false;
            }
        }
        if (!BASS_ChannelSetPosition(channel, startBytes, BASS_POS_BYTE)) {
            BASS_StreamFree(channel);
            return false;
        }
    } else if (!BASS_ChannelSetPosition(channel, startBytes, BASS_POS_BYTE)) {
        BASS_StreamFree(channel);
        return false;
    }

    BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL,
                             static_cast<float>(g_volume) / 100.0f);
    bool started = false;
    if (g_wasapiMode && mixer) {
        DWORD mixerFlags = BASS_MIXER_CHAN_DOWNMIX | BASS_MIXER_CHAN_BUFFER;
        if (kind == AuditionKind::MarkerRange) {
            mixerFlags |= BASS_MIXER_CHAN_PAUSE;
            started = BASS_Mixer_StreamAddChannelEx(
                          mixer, channel, mixerFlags, 0,
                          mixerPlayBytes) != FALSE;
            if (started) {
                BASS_MIXER_NODE nodes[3] = {};
                DWORD count = 0;
                const QWORD fadeBytes = BASS_ChannelSeconds2Bytes(
                    mixer, static_cast<double>(kStopFadeMs) / 1000.0);
                const QWORD fadeStart =
                    fadeBytes != static_cast<QWORD>(-1) &&
                            mixerStopBytes > fadeBytes
                        ? mixerStopBytes - fadeBytes
                        : 0;
                nodes[count++] = {0, 1.0f};
                if (fadeStart > 0) nodes[count++] = {fadeStart, 1.0f};
                nodes[count++] = {mixerStopBytes, 0.0f};
                const bool envelopeSet =
                    BASS_Mixer_ChannelSetEnvelope(
                        channel, BASS_MIXER_ENV_VOL, nodes, count) != FALSE;
                const bool resumed =
                    envelopeSet &&
                    BASS_Mixer_ChannelFlags(
                        channel, 0, BASS_MIXER_CHAN_PAUSE) !=
                        static_cast<DWORD>(-1);
                if (!envelopeSet || !resumed) {
                    const DWORD errorCode = BASS_ErrorGetCode();
                    BASS_Mixer_ChannelRemove(channel);
                    BASS_StreamFree(channel);
                    wchar_t text[300] = {};
                    swprintf_s(text,
                               L"Could not prepare the end of the range.\n\n"
                               L"BASS error code: %lu",
                               errorCode);
                    MessageBoxW(g_hwnd, text, L"Playback",
                                MB_OK | MB_ICONERROR);
                    return false;
                }
            }
        } else {
            started =
                BASS_Mixer_StreamAddChannel(mixer, channel, mixerFlags) !=
                FALSE;
        }
    } else {
        started = BASS_ChannelPlay(channel, FALSE) != FALSE;
    }
    if (!started) {
        const DWORD errorCode = BASS_ErrorGetCode();
        BASS_StreamFree(channel);
        wchar_t text[260] = {};
        swprintf_s(text, L"Could not start playback.\n\n"
                         L"BASS error code: %lu",
                   errorCode);
        MessageBoxW(g_hwnd, text, L"Playback", MB_OK | MB_ICONERROR);
        RefreshButtonStates();
        return false;
    }

    g_playChannel = channel;
    g_playKind = kind;
    g_playReturnPoint =
        MaxValue(0.0, MinValue(returnPoint, 1.0));
    g_playRangeStop = kind == AuditionKind::MarkerRange ? g_markerB : 1.0;
    g_playGuardBytes = kind == AuditionKind::MarkerRange
                           ? MinValue(guardBytes, length - startBytes)
                           : 0;
    SetCursorFraction(start, true);
    ApplyKeepAwake();
    RefreshButtonStates();
    return true;
}

void ToggleAudition(AuditionKind kind) {
    if (!WaveWindowAvailable()) return;
    if (g_playKind == kind && AnySoundPlaying()) {
        StopPlayback(true);
        return;
    }
    if (kind == AuditionKind::MarkerRange) {
        if (g_markerA < 0.0 || g_markerB <= g_markerA) return;
        StartPlaybackAt(kind, g_markerA, g_markerA);
        return;
    }
    double start = g_cursor;
    if (start < 0.0 || start >= 1.0) start = 0.0;
    StartPlaybackAt(kind, start, start);
}

double ChannelFraction(HSTREAM channel) {
    if (!channel) return -1.0;
    const QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    if (!length || length == static_cast<QWORD>(-1)) return -1.0;
    const QWORD position =
        g_wasapiMode ? BASS_Mixer_ChannelGetPosition(channel, BASS_POS_BYTE)
                     : BASS_ChannelGetPosition(channel, BASS_POS_BYTE);
    if (position == static_cast<QWORD>(-1)) return -1.0;
    return MaxValue(0.0, MinValue(
                              static_cast<double>(position) /
                                  static_cast<double>(length),
                              1.0));
}

void UpdatePosition() {
    if (!g_playChannel || g_playKind == AuditionKind::None) return;
    const DWORD state =
        g_wasapiMode ? BASS_Mixer_ChannelIsActive(g_playChannel)
                     : BASS_ChannelIsActive(g_playChannel);
    const bool active = state == BASS_ACTIVE_PLAYING ||
                        state == BASS_ACTIVE_WAITING ||
                        state == BASS_ACTIVE_QUEUED;
    if (!active) {
        FinishPlaybackNaturally();
        return;
    }

    const double position = ChannelFraction(g_playChannel);
    if (position < 0.0) return;
    if (g_playKind == AuditionKind::MarkerRange) {
        if (position + 0.000001 >= g_markerB) {
            SetCursorFraction(g_markerB, false);
        } else {
            SetCursorFraction(position, false);
        }
        const QWORD length =
            BASS_ChannelGetLength(g_playChannel, BASS_POS_BYTE);
        if (length && length != static_cast<QWORD>(-1)) {
            const QWORD stopBytes = static_cast<QWORD>(
                g_playRangeStop * static_cast<double>(length));
            const QWORD timerStop =
                MinValue(length, stopBytes + g_playGuardBytes);
            const QWORD current =
                g_wasapiMode
                    ? BASS_Mixer_ChannelGetPosition(g_playChannel,
                                                    BASS_POS_BYTE)
                    : BASS_ChannelGetPosition(g_playChannel, BASS_POS_BYTE);
            if (current != static_cast<QWORD>(-1) && current >= timerStop) {
                FinishPlaybackNaturally();
            }
        }
    } else {
        SetCursorFraction(position, false);
    }
}

bool ApplyAudioSelection(AudioOutputMode mode, const std::wstring& deviceId) {
    if (g_loadingWaveform || g_exportBusy) return false;
    const AudioOutputMode previousMode = g_outputMode;
    const std::wstring previousId = g_outputDeviceId;
    const bool wasPlaying = g_playKind != AuditionKind::None &&
                            AnySoundPlaying();
    const AuditionKind savedKind = g_playKind;
    const double savedCursor = g_cursor;
    const double savedReturn = g_playReturnPoint;

    if (wasPlaying) {
        DestroyPlayChannel();
        g_playKind = AuditionKind::None;
        ApplyKeepAwake();
    }
    g_outputMode = mode;
    g_outputDeviceId = deviceId;
    const bool initialized = InitAudioOutput();
    if (!initialized) {
        FreeAudioOutput();
        g_outputMode = previousMode;
        g_outputDeviceId = previousId;
        const bool restored = InitAudioOutput();
        if (restored && wasPlaying) {
            SetCursorFraction(savedCursor, true);
            StartPlaybackAt(savedKind, savedCursor, savedReturn);
        }
        RefreshOutputDevices();
        RefreshButtonStates();
        MessageBoxW(g_hwnd,
                    L"Could not switch the audio output. The previous working output was restored.",
                    L"Sound", MB_OK | MB_ICONWARNING);
        return false;
    }

    g_volume = MaxValue(0, MinValue(g_volume, 100));
    if (wasPlaying) {
        SetCursorFraction(savedCursor, true);
        StartPlaybackAt(savedKind, savedCursor, savedReturn);
    }
    RefreshOutputDevices();
    SaveSettings();
    RefreshButtonStates();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    return true;
}

void StartWaveLoad(const std::wstring& path) {
    if (g_exportBusy || path.empty()) return;
    StopPlayback(false);
    g_waveCancel.store(true, std::memory_order_relaxed);
    if (g_waveThread.joinable()) g_waveThread.join();

    const uint64_t generation = ++g_loadGeneration;
    g_waveCancel.store(false, std::memory_order_relaxed);
    g_loadingWaveform = true;
    g_pendingPath = path;
    {
        std::lock_guard<std::mutex> lock(g_loadResultMutex);
        g_loadResult.reset();
    }
    wchar_t title[1024] = {};
    swprintf_s(title, L"Loading — %s", FileNameOf(path).c_str());
    SetWindowTextW(g_hwnd, title);
    RefreshButtonStates();
    InvalidateRect(g_hwnd, nullptr, FALSE);

    try {
        auto result = std::make_shared<LoadResult>();
        result->generation = generation;
        result->path = path;
        result->error = L"Waveform generation failed unexpectedly.";
        result->error.reserve(128);
        g_waveThread = std::thread([generation, path, result]() {
            try {
                result->ok = CreateDecodeWaveform(
                    path, g_waveCancel, &result->wave, &result->error);
                if (result->ok) result->error.clear();
            } catch (...) {
                // Worker exceptions must become a UI result; otherwise the
                // process terminates and the loading state never completes.
                result->ok = false;
            }
            {
                std::lock_guard<std::mutex> lock(g_loadResultMutex);
                g_loadResult = result;
            }
            if (!PostMessageW(g_hwnd, kMessageLoadDone,
                              static_cast<WPARAM>(generation), 0)) {
                std::lock_guard<std::mutex> lock(g_loadResultMutex);
                if (g_loadResult == result) g_loadResult.reset();
            }
        });
    } catch (const std::exception&) {
        g_loadingWaveform = false;
        g_pendingPath.clear();
        MessageBoxW(g_hwnd, L"Could not start background waveform generation.",
                    L"Error", MB_OK | MB_ICONERROR);
        RefreshButtonStates();
    }
}

void FinishWaveLoad(uint64_t generation) {
    // A stale completion can arrive after a newer load starts. Check the
    // generation before joining so an old message cannot block the new worker.
    if (generation != g_loadGeneration) return;
    if (g_waveThread.joinable()) g_waveThread.join();
    std::shared_ptr<LoadResult> result;
    {
        std::lock_guard<std::mutex> lock(g_loadResultMutex);
        result.swap(g_loadResult);
    }
    if (!result || result->generation != generation) return;

    g_loadingWaveform = false;
    g_pendingPath.clear();
    if (!result->ok) {
        if (!result->error.empty()) {
            MessageBoxW(g_hwnd, result->error.c_str(),
                        L"Could not open MP3", MB_OK | MB_ICONERROR);
        }
        if (g_sourceReady) {
            SetWindowTextW(g_hwnd,
                           (FileNameOf(g_sourcePath) +
                            L" — Mp3cutter").c_str());
        } else {
            SetWindowTextW(g_hwnd, L"Mp3cutter");
        }
        RefreshButtonStates();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    g_sourcePath = result->path;
    g_sourceReady = true;
    g_wave = std::move(result->wave);
    g_markerA = 0.0;
    g_markerB = 1.0;
    g_cursor = 0.0;
    g_zoom = 1.0;
    g_viewStart = 0.0;
    g_lastOpenDirectory = DirectoryOf(g_sourcePath);
    SaveSettings();
    SetWindowTextW(g_hwnd,
                   (FileNameOf(g_sourcePath) +
                    L" — Mp3cutter").c_str());
    RefreshButtonStates();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void OpenMp3Dialog() {
    if (g_exportBusy) {
        MessageBoxW(g_hwnd, L"Wait for the current MP3 export to finish.",
                    L"Export", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"MP3 files (*.mp3)\0*.mp3\0";
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrInitialDir =
        IsExistingDirectory(g_lastOpenDirectory)
            ? g_lastOpenDirectory.c_str()
            : nullptr;
    ofn.lpstrDefExt = L"mp3";
    ofn.lpstrTitle = L"Open MP3 — Mp3cutter";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::wstring selected = path.data();
    if (!HasExtension(selected, L".mp3")) {
        MessageBoxW(g_hwnd, L"This application supports MP3 files only.",
                    L"Open MP3", MB_OK | MB_ICONWARNING);
        return;
    }
    StartWaveLoad(selected);
}

void StartExport() {
    if (!WaveWindowAvailable() || g_exportBusy || g_markerB <= g_markerA) return;
    std::wstring exportDirectory = g_lastExportDirectory;
    if (!IsExistingDirectory(exportDirectory)) {
        exportDirectory = DirectoryOf(g_sourcePath);
    }
    const std::wstring initialName =
        FileStemOf(g_sourcePath) + L" - excerpt.mp3";
    std::vector<wchar_t> path(32768, L'\0');
    wcsncpy_s(path.data(), path.size(), initialName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"MP3 files (*.mp3)\0*.mp3\0";
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrInitialDir =
        IsExistingDirectory(exportDirectory) ? exportDirectory.c_str() : nullptr;
    ofn.lpstrDefExt = L"mp3";
    ofn.lpstrTitle = L"Save MP3 as — Mp3cutter";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
                OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring target = path.data();
    if (!HasExtension(target, L".mp3")) target += L".mp3";
    if (_wcsicmp(FullPathOf(g_sourcePath).c_str(),
                 FullPathOf(target).c_str()) == 0) {
        MessageBoxW(g_hwnd, L"Cannot overwrite the source MP3.",
                    L"Export", MB_OK | MB_ICONERROR);
        return;
    }

    StopPlayback(false);
    g_lastExportDirectory = DirectoryOf(target);
    SaveSettings();
    if (g_exportThread.joinable()) g_exportThread.join();
    {
        std::lock_guard<std::mutex> lock(g_exportResultMutex);
        g_exportResult.reset();
    }
    g_exportCancel.store(false, std::memory_order_relaxed);
    g_exportBusy = true;
    CloseMenu();
    RefreshButtonStates();
    InvalidateRect(g_hwnd, nullptr, FALSE);

    const std::wstring source = g_sourcePath;
    const double start = g_markerA;
    const double stop = g_markerB;
    try {
        auto result = std::make_shared<ExportResult>();
        result->outputPath = target;
        result->startFraction = start;
        result->stopFraction = stop;
        result->error = L"Background export failed unexpectedly.";
        result->error.reserve(128);
        g_exportThread = std::thread(
            [source, target, start, stop, result]() {
            try {
                *result = ExportMp3Range(
                    source, target, start, stop, g_exportCancel);
            } catch (...) {
                // Keep an allocation or unexpected conversion failure from
                // terminating the process inside the worker.
                result->ok = false;
                result->canceled =
                    g_exportCancel.load(std::memory_order_relaxed);
            }
            {
                std::lock_guard<std::mutex> lock(g_exportResultMutex);
                g_exportResult = result;
            }
            if (!PostMessageW(g_hwnd, kMessageExportDone, 0, 0)) {
                std::lock_guard<std::mutex> lock(g_exportResultMutex);
                if (g_exportResult == result) g_exportResult.reset();
            }
            });
    } catch (const std::exception&) {
        g_exportBusy = false;
        RefreshButtonStates();
        MessageBoxW(g_hwnd, L"Could not start background export.",
                    L"Export error", MB_OK | MB_ICONERROR);
    }
}

void FinishExport() {
    if (g_exportThread.joinable()) g_exportThread.join();
    std::shared_ptr<ExportResult> result;
    {
        std::lock_guard<std::mutex> lock(g_exportResultMutex);
        result.swap(g_exportResult);
    }
    g_exportBusy = false;
    RefreshButtonStates();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    if (!result) return;
    if (result->ok) {
        g_markerA = result->startFraction;
        g_markerB = result->stopFraction;
        ClampView();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        wchar_t text[1200] = {};
        swprintf_s(text,
                   L"MP3 saved without re-encoding.\n\n%s\n\n"
                   L"Exact duration: %.3f s",
                   result->outputPath.c_str(), result->duration);
        MessageBoxW(g_hwnd, text, L"Export complete",
                    MB_OK | MB_ICONINFORMATION);
    } else if (!result->canceled && !result->error.empty()) {
        MessageBoxW(g_hwnd, result->error.c_str(), L"Export error",
                    MB_OK | MB_ICONERROR);
    }
}

RECT MenuScaleRect(int index) {
    const RECT menu = SettingsMenuRect();
    const int left = menu.left + S(16);
    const int right = menu.right - S(16);
    const int top = menu.top + S(64) + index * S(27);
    return {left, top, right, top + S(24)};
}

RECT MenuModeRect(int index) {
    const RECT menu = SettingsMenuRect();
    const int left = menu.left + S(16);
    const int right = menu.right - S(16);
    const int gap = S(5);
    const int width = (right - left - gap * 2) / 3;
    return {left + index * (width + gap), menu.top + S(192),
            left + index * (width + gap) + width, menu.top + S(216)};
}

RECT MenuDeviceListRect() {
    const RECT menu = SettingsMenuRect();
    return {menu.left + S(16), menu.top + S(242),
            menu.right - S(16), menu.bottom - S(10)};
}

RECT MenuSystemDeviceRect() {
    const RECT list = MenuDeviceListRect();
    return {list.left + 1, list.top + 2, list.right - 1, list.top + S(22)};
}

RECT MenuDeviceRowRect(int visibleIndex) {
    const RECT list = MenuDeviceListRect();
    const int top = list.top + S(26) + visibleIndex * S(25);
    return {list.left + 1, top, list.right - S(7), top + S(22)};
}

void DrawSettingsMenu(HDC dc) {
    if (!g_menuOpen) return;
    const RECT menu = SettingsMenuRect();
    FillRectColor(dc, {menu.left + S(4), menu.top + S(5),
                       menu.right + S(4), menu.bottom + S(5)},
                  g_brushes.waveArea);
    FillRectColor(dc, menu, g_brushes.panel);
    FillRectColor(dc, {menu.left, menu.bottom - 1, menu.right, menu.bottom},
                  g_brushes.line);
    FillRectColor(dc, {menu.right - 1, menu.top, menu.right, menu.bottom},
                  g_brushes.line);

    RECT header = {menu.left + S(16), menu.top + S(10),
                   menu.right - S(16), menu.top + S(36)};
    DrawTextInRect(dc, L"Settings", header, g_titleFont, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    RECT scaleLabel = {menu.left + S(16), menu.top + S(42),
                       menu.right - S(16), menu.top + S(62)};
    DrawTextInRect(dc, L"Scale", scaleLabel, g_smallFont, kColorDim,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    for (int i = 0; i < 3; ++i) {
        const RECT row = MenuScaleRect(i);
        FillRectColor(dc, row, i == g_uiPreset ? g_brushes.buttonActive
                                               : g_brushes.panelRaised);
        RECT text = row;
        text.left += S(10);
        DrawTextInRect(dc, kUiPresetNames[i], text, g_font, kColorText,
                       DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    RECT soundLabel = {menu.left + S(16), menu.top + S(148),
                       menu.right - S(16), menu.top + S(170)};
    DrawTextInRect(dc, L"Sound", soundLabel, g_titleFont, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    RECT modeLabel = {menu.left + S(16), menu.top + S(170),
                      menu.right - S(16), menu.top + S(188)};
    DrawTextInRect(dc, L"Output mode", modeLabel, g_smallFont, kColorDim,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    const wchar_t* modeNames[] = {L"Auto", L"Shared", L"Exclusive"};
    for (int i = 0; i < 3; ++i) {
        const RECT modeRect = MenuModeRect(i);
        FillRectColor(dc, modeRect,
                      static_cast<int>(g_outputMode) == i
                          ? g_brushes.buttonActive
                          : g_brushes.panelRaised);
        DrawTextInRect(dc, modeNames[i], modeRect, g_smallFont, kColorText,
                       DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    RECT outputLabel = {menu.left + S(16), menu.top + S(220),
                        menu.right - S(16), menu.top + S(238)};
    DrawTextInRect(dc, L"Output device", outputLabel, g_smallFont,
                   kColorDim, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    const RECT list = MenuDeviceListRect();
    FillRectColor(dc, list, g_brushes.waveArea);
    const RECT system = MenuSystemDeviceRect();
    const bool systemSelected = g_outputDeviceId.empty();
    if (systemSelected) {
        FillRectColor(dc, system, g_brushes.buttonActive);
    }
    RECT systemText = system;
    systemText.left += S(8);
    systemText.right -= S(8);
    DrawTextInRect(dc, L"System device", systemText, g_smallFont,
                   kColorText, DT_SINGLELINE | DT_VCENTER | DT_LEFT |
                                    DT_END_ELLIPSIS);

    const int count = static_cast<int>(g_outputDevices.size());
    const int first = MinValue(g_menuDeviceScroll, count);
    for (int visible = 0; visible < kMenuDeviceRows; ++visible) {
        const int index = first + visible;
        if (index >= count) break;
        const RECT row = MenuDeviceRowRect(visible);
        if (row.bottom > list.bottom - S(2)) break;
        const WasapiDevice& device = g_outputDevices[static_cast<size_t>(index)];
        const bool selected =
            _wcsicmp(g_outputDeviceId.c_str(), device.id.c_str()) == 0;
        if (selected) FillRectColor(dc, row, g_brushes.buttonActive);
        RECT text = row;
        text.left += S(8);
        text.right -= S(8);
        DrawTextInRect(dc, device.name, text, g_smallFont, kColorText,
                       DT_SINGLELINE | DT_VCENTER | DT_LEFT |
                           DT_END_ELLIPSIS);
    }

    if (count > kMenuDeviceRows) {
        const int trackTop = list.top + S(26);
        const int trackBottom = list.bottom - S(3);
        FillRectColor(dc, {list.right - S(5), trackTop, list.right - S(2),
                           trackBottom},
                      g_brushes.slider);
        const int maxScroll = count - kMenuDeviceRows;
        const int available = trackBottom - trackTop;
        const int thumbHeight =
            MaxValue(S(12), available * kMenuDeviceRows /
                                      (count + 1));
        const int thumbTop =
            trackTop + (available - thumbHeight) * g_menuDeviceScroll /
                           MaxValue(1, maxScroll);
        FillRectColor(dc, {list.right - S(5), thumbTop, list.right - S(2),
                           thumbTop + thumbHeight},
                      g_brushes.cursor);
    }
    FillRectColor(dc, {menu.left, menu.top, menu.right, menu.top + 1},
                  g_brushes.line);
    FillRectColor(dc, {menu.left, menu.top, menu.left + 1, menu.bottom},
                  g_brushes.line);
}

void DrawWave(HDC dc) {
    const RECT area = WaveAreaRect();
    if (area.right <= area.left || area.bottom <= area.top) return;
    FillRectColor(dc, area, g_brushes.waveArea);
    FillRectColor(dc, {area.left, area.top, area.right, area.top + 1},
                  g_brushes.line);
    FillRectColor(dc, {area.left, area.bottom - 1, area.right, area.bottom},
                  g_brushes.line);

    const int middle = (area.top + area.bottom) / 2;
    FillRectColor(dc, {area.left, middle, area.right, middle + 1},
                  g_brushes.waveZero);
    if (!g_sourceReady || g_wave.lo.empty()) return;

    const int width = area.right - area.left;
    const int half = (area.bottom - area.top) / 2 - S(3);
    const int columns = static_cast<int>(g_wave.lo.size());
    const float scale = g_wave.peak > 1.0f ? 1.0f / g_wave.peak : 1.0f;
    int firstPx = 0;
    int lastPx = width;
    RECT clip = {};
    if (GetClipBox(dc, &clip) != ERROR) {
        firstPx = MaxValue(0, MinValue(clip.left - area.left, width));
        lastPx = MaxValue(firstPx, MinValue(clip.right - area.left, width));
    }
    int runLeft = area.left;
    int runTop = 0;
    int runBottom = 0;
    bool runOpen = false;
    int px = firstPx;
    for (; px < lastPx && columns > 0; ++px) {
        const double left =
            g_viewStart + static_cast<double>(px) / width * ViewSpan();
        const double right =
            g_viewStart + static_cast<double>(px + 1) / width * ViewSpan();
        int from = static_cast<int>(left * columns);
        int to = static_cast<int>(right * columns);
        if (to <= from) to = from + 1;
        from = MaxValue(0, MinValue(from, columns - 1));
        to = MaxValue(from + 1, MinValue(to, columns));
        float lo = 0.0f;
        float hi = 0.0f;
        for (int column = from; column < to; ++column) {
            lo = MinValue(lo, g_wave.lo[static_cast<size_t>(column)]);
            hi = MaxValue(hi, g_wave.hi[static_cast<size_t>(column)]);
        }
        int top = middle - static_cast<int>(
                                hi * static_cast<float>(half) * scale + 0.5f);
        int bottom = middle - static_cast<int>(
                                  lo * static_cast<float>(half) * scale - 0.5f);
        top = MaxValue(area.top + 1, MinValue(top, area.bottom - 1));
        bottom =
            MaxValue(top + 1, MinValue(bottom, area.bottom));
        if (runOpen && top == runTop && bottom == runBottom) continue;
        if (runOpen) {
            FillRectColor(dc, {runLeft, runTop, area.left + px, runBottom},
                          g_brushes.wave);
        }
        runLeft = area.left + px;
        runTop = top;
        runBottom = bottom;
        runOpen = true;
    }
    if (runOpen) {
        FillRectColor(dc, {runLeft, runTop, area.left + px, runBottom},
                      g_brushes.wave);
    }

    const auto drawMarker = [&](double fraction, HBRUSH brush,
                                const wchar_t* letter) {
        if (!FracVisible(fraction)) return;
        int x = XOfFrac(area, fraction);
        x = MaxValue(area.left, MinValue(x, area.right - S(2)));
        FillRectColor(dc, {x, area.top, x + MaxValue(1, S(2)), area.bottom},
                      brush);
        RECT flag = {x - S(8), area.top + S(2), x + S(10), area.top + S(20)};
        if (flag.left < area.left) {
            const int shift = area.left - flag.left;
            flag.left += shift;
            flag.right += shift;
        }
        if (flag.right > area.right) {
            const int shift = flag.right - area.right;
            flag.left -= shift;
            flag.right -= shift;
        }
        FillRectColor(dc, flag, brush);
        DrawTextInRect(dc, letter, flag, g_smallFont, kColorWaveArea,
                       DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    };
    drawMarker(g_markerA, g_brushes.markerA, L"A");
    drawMarker(g_markerB, g_brushes.markerB, L"B");
    if (FracVisible(g_cursor)) {
        int x = XOfFrac(area, g_cursor);
        x = MaxValue(area.left, MinValue(x, area.right - S(2)));
        FillRectColor(dc, {x, area.top + S(20), x + MaxValue(1, S(2)),
                           area.bottom},
                      g_brushes.cursor);
    }
}

void DrawScrollBar(HDC dc) {
    const RECT track = WaveScrollRect();
    if (track.right <= track.left || track.bottom <= track.top) return;
    FillRectColor(dc, track, g_brushes.panel);
    FillRectColor(dc, {track.left, track.top, track.right, track.top + 1},
                  g_brushes.line);
    RECT thumb = track;
    const int width = track.right - track.left;
    thumb.left =
        track.left + static_cast<int>(g_viewStart * static_cast<double>(width));
    thumb.right = track.left + static_cast<int>(
                                   (g_viewStart + ViewSpan()) *
                                   static_cast<double>(width));
    const int minimum = S(20);
    if (thumb.right - thumb.left < minimum) {
        thumb.right = thumb.left + minimum;
    }
    if (thumb.right > track.right) {
        thumb.right = track.right;
        thumb.left = track.right - minimum;
    }
    FillRectColor(dc, thumb, g_brushes.slider);
}

void DrawVolumeControl(HDC dc) {
    const RECT control = VolumeControlRect();
    if (control.right <= control.left || control.bottom <= control.top) return;

    RECT label = {control.left, control.top, control.right,
                  control.top + S(19)};
    DrawTextInRect(dc, L"Volume", label, g_smallFont, kColorDim,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    wchar_t value[16] = {};
    swprintf_s(value, L"%d%%", g_volume);
    DrawTextInRect(dc, value, label, g_smallFont, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

    const RECT slider = VolumeSliderRect();
    FillRectColor(dc, slider, g_brushes.slider);
    const int width = MaxValue(1, slider.right - slider.left);
    const int fillWidth = static_cast<int>(
        width * static_cast<double>(g_volume) / 100.0 + 0.5);
    if (fillWidth > 0) {
        FillRectColor(dc, {slider.left, slider.top,
                           slider.left + MinValue(fillWidth, width),
                           slider.bottom},
                      g_brushes.sliderFill);
    }
    const int knobX = slider.left + fillWidth;
    FillRectColor(dc, {knobX - S(2), slider.top - S(5),
                       knobX + S(3), slider.bottom + S(5)},
                  g_brushes.cursor);
}

void DrawInfo(HDC dc) {
    const RECT info = WaveInfoRect();
    FillRectColor(dc, {info.left - S(1), info.top, info.left, info.bottom},
                  g_brushes.line);
    if (!g_sourceReady) return;
    const double rangeDuration =
        g_wave.duration * MaxValue(0.0, g_markerB - g_markerA);
    wchar_t bitrate[64] = {};
    wchar_t frequency[64] = {};
    if (g_wave.bitrateKbps > 0) {
        swprintf_s(bitrate, L"%d kbps", g_wave.bitrateKbps);
    } else {
        wcscpy_s(bitrate, L"MP3");
    }
    swprintf_s(frequency, L"%d Hz", g_wave.sampleRate);
    const std::wstring duration = FormatSeconds(rangeDuration);
    const int gap = S(18);
    const int usable = info.right - info.left - S(18);
    const int cell = usable / 3;
    const wchar_t* values[] = {bitrate, frequency, duration.c_str()};
    const wchar_t* labels[] = {L"average bitrate", L"frequency", L"A-B"};
    for (int i = 0; i < 3; ++i) {
        RECT label = {info.left + S(9) + i * cell, info.top + S(2),
                      info.left + S(9) + (i + 1) * cell - gap, info.top + S(18)};
        RECT value = {label.left, info.top + S(18), label.right,
                      info.top + S(38)};
        DrawTextInRect(dc, labels[i], label, g_smallFont, kColorDim,
                       DT_SINGLELINE | DT_VCENTER | DT_LEFT |
                           DT_END_ELLIPSIS);
        DrawTextInRect(dc, values[i], value, g_font, kColorText,
                       DT_SINGLELINE | DT_VCENTER | DT_LEFT |
                           DT_END_ELLIPSIS);
    }
}

void DrawCaption(HDC dc) {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    FillRectColor(dc, {0, 0, client.right, S(kCaptionHeight)}, g_brushes.back);
    FillRectColor(dc, {0, S(kCaptionHeight) - 1, client.right,
                       S(kCaptionHeight)},
                  g_brushes.line);

    const RECT menuButton = MenuButtonRect();
    const RECT closeButton = CloseButtonRect();
    wchar_t title[1024] = {};
    GetWindowTextW(g_hwnd, title, 1024);
    RECT titleRect = {S(kMargin), 0, menuButton.left - S(16),
                      S(kCaptionHeight)};
    DrawTextInRect(dc, title, titleRect, g_titleFont, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    FillRectColor(
        dc, menuButton,
        g_menuOpen ? g_brushes.buttonActive : g_brushes.panelRaised);
    DrawTextInRect(dc, L"Menu", menuButton, g_font, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    FillRectColor(dc, closeButton, g_brushes.panelRaised);
    DrawTextInRect(dc, L"×", closeButton, g_titleFont, kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

void DrawGrip(HDC dc) {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    for (int i = 1; i <= 3; ++i) {
        const int offset = S(i * 5);
        for (int j = 0; j < S(1) + 1; ++j) {
            FillRectColor(dc,
                          {client.right - S(5) - offset,
                           client.bottom - S(4) - offset + j,
                           client.right - S(2) - offset + j,
                           client.bottom - S(3) - offset + j},
                          g_brushes.line);
        }
    }
}

void DrawMain(HDC dc) {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    FillRectColor(dc, client, g_brushes.back);
    DrawWave(dc);
    DrawScrollBar(dc);
    DrawVolumeControl(dc);
    DrawInfo(dc);
    DrawCaption(dc);
    DrawGrip(dc);
    DrawSettingsMenu(dc);
}

void DrawOwnedButton(const DRAWITEMSTRUCT* item) {
    if (!item) return;
    HDC dc = item->hDC;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const int id = static_cast<int>(item->CtlID);
    bool active = false;
    std::wstring label;
    if (id == kIdOpen) {
        label = g_loadingWaveform ? L"Open MP3…" : L"Open MP3";
    } else if (id == kIdPlay) {
        active = g_playKind == AuditionKind::Cursor;
        label = active ? L"Stop" : L"Play";
    } else if (id == kIdPlayRange) {
        active = g_playKind == AuditionKind::MarkerRange;
        label = active ? L"Stop" : L"Play A-B";
    } else if (id == kIdExport) {
        label = g_exportBusy ? L"Export…" : L"Export A-B";
    }
    RECT rect = item->rcItem;
    FillRectColor(dc, rect,
                  active ? g_brushes.buttonActive
                         : (pressed ? g_brushes.panelRaised
                                    : g_brushes.button));
    FillRectColor(dc, {rect.left, rect.bottom - 1, rect.right, rect.bottom},
                  g_brushes.line);
    DrawTextInRect(dc, label, rect, g_font,
                   disabled ? kColorDim : kColorText,
                   DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

void RefreshButtonStates() {
    if (!g_hwnd) return;
    const bool usable = WaveWindowAvailable();
    const bool playbackUsable = usable && g_playbackAvailable;
    EnableWindow(g_btnOpen, g_exportBusy ? FALSE : TRUE);
    EnableWindow(g_btnPlay, playbackUsable ? TRUE : FALSE);
    EnableWindow(g_btnRange, playbackUsable ? TRUE : FALSE);
    EnableWindow(g_btnExport, usable ? TRUE : FALSE);
    InvalidateRect(g_btnOpen, nullptr, FALSE);
    InvalidateRect(g_btnPlay, nullptr, FALSE);
    InvalidateRect(g_btnRange, nullptr, FALSE);
    InvalidateRect(g_btnExport, nullptr, FALSE);
}

void LayoutControls() {
    if (!g_hwnd) return;
    const HWND buttons[] = {g_btnOpen, g_btnPlay, g_btnRange, g_btnExport};
    for (int i = 0; i < 4; ++i) {
        const RECT rect = ButtonRect(i);
        MoveWindow(buttons[i], rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, TRUE);
    }
}

HWND CreateOwnedButton(HWND parent, HINSTANCE instance, int id) {
    return CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}

void CloseMenu() {
    if (!g_menuOpen) return;
    g_menuOpen = false;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void OpenMenu() {
    RefreshOutputDevices();
    g_menuOpen = true;
    g_menuDeviceScroll = 0;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void SetVolumeFromSlider(int x) {
    const RECT slider = VolumeSliderRect();
    const int width = MaxValue(1, slider.right - slider.left);
    g_volume = MaxValue(0, MinValue(100, (x - slider.left) * 100 / width));
    ApplyVolume();
    RECT control = VolumeControlRect();
    InvalidateRect(g_hwnd, &control, FALSE);
}

bool StartVolumeDrag(POINT point) {
    RECT hit = VolumeControlRect();
    InflateRect(&hit, S(3), S(3));
    if (!PtInRect(&hit, point)) return false;
    g_volumeDrag = true;
    SetCapture(g_hwnd);
    SetVolumeFromSlider(point.x);
    return true;
}

bool HandleMenuClick(POINT point) {
    if (!g_menuOpen) return false;
    const RECT menu = SettingsMenuRect();
    if (!PtInRect(&menu, point)) {
        CloseMenu();
        return true;
    }
    for (int i = 0; i < 3; ++i) {
        const RECT scale = MenuScaleRect(i);
        if (PtInRect(&scale, point)) {
            ApplyUiPreset(i);
            CloseMenu();
            return true;
        }
    }
    for (int i = 0; i < 3; ++i) {
        const RECT modeRect = MenuModeRect(i);
        if (PtInRect(&modeRect, point)) {
            if (ApplyAudioSelection(
                    static_cast<AudioOutputMode>(i), g_outputDeviceId)) {
                CloseMenu();
            }
            return true;
        }
    }
    const RECT systemDevice = MenuSystemDeviceRect();
    if (PtInRect(&systemDevice, point)) {
        if (ApplyAudioSelection(g_outputMode, std::wstring())) {
            CloseMenu();
        }
        return true;
    }
    for (int visible = 0; visible < kMenuDeviceRows; ++visible) {
        const int index = g_menuDeviceScroll + visible;
        if (index >= static_cast<int>(g_outputDevices.size())) break;
        const RECT row = MenuDeviceRowRect(visible);
        if (PtInRect(&row, point)) {
            const WasapiDevice& device =
                g_outputDevices[static_cast<size_t>(index)];
            if (ApplyAudioSelection(g_outputMode, device.id)) {
                CloseMenu();
            }
            return true;
        }
    }
    return true;
}

void ApplyUiPreset(int preset) {
    if (preset < 0 || preset >= 3) return;
    if (preset == g_uiPreset) return;
    const float oldScale = g_uiScale;
    g_uiPreset = preset;
    g_uiScale = kUiPresets[preset];
    const float factor = g_uiScale / oldScale;

    RECT windowRect = {};
    GetWindowRect(g_hwnd, &windowRect);
    const int newWidth = MaxValue(
        MinimumWindowWidth(),
        static_cast<int>((windowRect.right - windowRect.left) * factor + 0.5f));
    const int newHeight =
        MaxValue(S(kBaseHeight), static_cast<int>(
                              (windowRect.bottom - windowRect.top) * factor +
                              0.5f));
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR currentMonitor =
        MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(currentMonitor, &monitor)) {
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        const int width = MinValue(newWidth, workWidth * 9 / 10);
        const int height = MinValue(newHeight, workHeight * 9 / 10);
        int x = windowRect.left;
        int y = windowRect.top;
        if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
        if (y + height > monitor.rcWork.bottom) y = monitor.rcWork.bottom - height;
        if (x < monitor.rcWork.left) x = monitor.rcWork.left;
        if (y < monitor.rcWork.top) y = monitor.rcWork.top;
        SetWindowPos(g_hwnd, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RebuildFonts();
    if (g_backBitmap) {
        DeleteObject(g_backBitmap);
        g_backBitmap = nullptr;
        g_backWidth = 0;
        g_backHeight = 0;
    }
    LayoutControls();
    SaveSettings();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

bool MarkerAt(POINT point) {
    const RECT area = WaveAreaRect();
    if (!g_sourceReady || point.y < area.top + S(1) ||
        point.y > area.top + S(21)) {
        return false;
    }
    const auto hit = [&](double fraction) {
        if (!FracVisible(fraction)) return false;
        int x = XOfFrac(area, fraction);
        x = MaxValue(area.left, MinValue(x, area.right - 2));
        const RECT flag = {x - S(8), area.top + S(2), x + S(10),
                           area.top + S(20)};
        return PtInRect(&flag, point) != FALSE;
    };
    if (hit(g_markerA)) return true;
    if (hit(g_markerB)) return true;
    return false;
}

int MarkerIndexAt(POINT point) {
    const RECT area = WaveAreaRect();
    if (!g_sourceReady || point.y < area.top + S(1) ||
        point.y > area.top + S(21)) {
        return 0;
    }
    int best = 0;
    int bestDistance = (std::numeric_limits<int>::max)();
    const auto consider = [&](double fraction, int index) {
        if (!FracVisible(fraction)) return;
        int x = XOfFrac(area, fraction);
        x = MaxValue(area.left, MinValue(x, area.right - 2));
        const RECT flag = {x - S(8), area.top + S(2), x + S(10),
                           area.top + S(20)};
        if (PtInRect(&flag, point)) {
            const int distance = std::abs(point.x - x);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
    };
    consider(g_markerA, 1);
    consider(g_markerB, 2);
    return best;
}

void SetMarkerAtX(int marker, int x) {
    const RECT area = WaveAreaRect();
    const double width =
        area.right > area.left ? area.right - area.left : 1.0;
    const double gap = 4.0 * ViewSpan() / width;
    const double fraction = FracAt(area, x);
    if (marker == 1) {
        if (fraction >= g_markerB) {
            // Crossing B makes A active and preserves a valid range by moving
            // B to the end of the track.
            g_markerA = MinValue(fraction, 1.0 - gap);
            g_markerB = 1.0;
        } else {
            g_markerA =
                MaxValue(0.0, MinValue(fraction, g_markerB - gap));
        }
    } else {
        if (fraction <= g_markerA) {
            // Crossing A makes B active and moves A to the start.
            g_markerB = MaxValue(fraction, gap);
            g_markerA = 0.0;
        } else {
            g_markerB =
                MinValue(1.0, MaxValue(fraction, g_markerA + gap));
        }
    }
    InvalidateRect(g_hwnd, &area, FALSE);
    RECT info = WaveInfoRect();
    InvalidateRect(g_hwnd, &info, FALSE);
}

void DragMarker(int x) {
    if (!g_dragMarker) return;
    SetMarkerAtX(g_dragMarker, x);
}

void ShowWaveContextMenu(POINT point) {
    const RECT area = WaveAreaRect();
    if (!WaveWindowAvailable() || !PtInRect(&area, point)) return;
    CloseMenu();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kIdSetMarkerA, L"Set A here");
    AppendMenuW(menu, MF_STRING, kIdSetMarkerB, L"Set B here");

    POINT screen = point;
    ClientToScreen(g_hwnd, &screen);
    const UINT flags =
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY;
    const int command =
        TrackPopupMenu(menu, flags, screen.x, screen.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);

    if (command == kIdSetMarkerA) {
        SetMarkerAtX(1, point.x);
    } else if (command == kIdSetMarkerB) {
        SetMarkerAtX(2, point.x);
    }
}

RECT ScrollThumbRect() {
    const RECT track = WaveScrollRect();
    const int width = track.right - track.left;
    RECT thumb = track;
    thumb.left =
        track.left + static_cast<int>(g_viewStart * static_cast<double>(width));
    thumb.right = track.left + static_cast<int>(
                                   (g_viewStart + ViewSpan()) *
                                   static_cast<double>(width));
    const int minimum = S(20);
    if (thumb.right - thumb.left < minimum) {
        thumb.right = thumb.left + minimum;
    }
    if (thumb.right > track.right) {
        thumb.right = track.right;
        thumb.left = track.right - minimum;
    }
    return thumb;
}

void ScrollViewTo(int x) {
    if (ViewSpan() >= 1.0) return;
    const RECT track = WaveScrollRect();
    const int width = MaxValue(1, track.right - track.left);
    g_viewStart =
        static_cast<double>(x - track.left) / width - g_scrollGrab;
    ClampView();
}

void RequestFrame(const RECT* dirty) {
    if (dirty) {
        if (!g_pendingDirtyValid) {
            g_pendingDirty = *dirty;
            g_pendingDirtyValid = true;
        } else {
            UnionRect(&g_pendingDirty, &g_pendingDirty, dirty);
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (!g_frameTimerActive && now - g_lastFrameTick >= kFrameTimerMs) {
        g_lastFrameTick = now;
        if (g_pendingDirtyValid) {
            InvalidateRect(g_hwnd, &g_pendingDirty, FALSE);
            g_pendingDirtyValid = false;
        }
        return;
    }
    if (!g_frameTimerActive) {
        g_frameTimerActive = true;
        SetTimer(g_hwnd, kTimerFrame, kFrameTimerMs, nullptr);
    }
}

bool SeekActivePlayback(double fraction) {
    if (!g_playChannel || g_playKind == AuditionKind::None) return false;
    fraction = MaxValue(0.0, MinValue(fraction, 1.0));
    if (g_playKind == AuditionKind::MarkerRange) {
        if (fraction <= g_markerA) fraction = g_markerA;
        if (fraction >= g_markerB) {
            FinishPlaybackNaturally();
            return true;
        }
        // An A-B audition runs as a mixer source that carries a volume
        // envelope and a length limit expressed in mixer bytes relative to
        // the moment it was added. Seeking such a source in place leaves the
        // envelope out of sync with the new position, so its fade-out no
        // longer lands on B and the silent kTailGuardMs tail becomes
        // audible. Restarting the audition at the new position rebuilds the
        // envelope and the length limit from the A-B markers, which keeps
        // the ending identical to a freshly started A-B playback.
        return StartPlaybackAt(AuditionKind::MarkerRange, fraction,
                               g_playReturnPoint);
    }
    if (fraction >= 1.0) {
        FinishPlaybackNaturally();
        return true;
    }
    const QWORD length = BASS_ChannelGetLength(g_playChannel, BASS_POS_BYTE);
    if (!length || length == static_cast<QWORD>(-1)) return false;
    QWORD position =
        static_cast<QWORD>(fraction * static_cast<double>(length));
    if (position >= length) position = length - 1;
    const bool moved =
        g_wasapiMode
            ? BASS_Mixer_ChannelSetPosition(g_playChannel, position,
                                             BASS_POS_BYTE) != FALSE
            : BASS_ChannelSetPosition(g_playChannel, position,
                                      BASS_POS_BYTE) != FALSE;
    if (moved) SetCursorFraction(fraction, true);
    return moved;
}

void HandleWaveMouseDown(POINT point) {
    const RECT scroll = WaveScrollRect();
    if (PtInRect(&scroll, point)) {
        const RECT thumb = ScrollThumbRect();
        const int width = MaxValue(1, scroll.right - scroll.left);
        if (PtInRect(&thumb, point)) {
            g_scrollGrab =
                static_cast<double>(point.x - thumb.left) / width;
        } else {
            g_scrollGrab =
                static_cast<double>(thumb.right - thumb.left) / width / 2.0;
        }
        g_scrollDrag = true;
        SetCapture(g_hwnd);
        ScrollViewTo(point.x);
        RECT dirty = WaveAreaRect();
        UnionRect(&dirty, &dirty, &scroll);
        RequestFrame(&dirty);
        return;
    }

    const RECT area = WaveAreaRect();
    if (!PtInRect(&area, point) || !g_sourceReady) return;
    const int marker = MarkerIndexAt(point);
    if (marker) {
        g_dragMarker = marker;
        SetCapture(g_hwnd);
        InvalidateRect(g_hwnd, &area, FALSE);
        return;
    }
    g_panPending = true;
    g_panStart = point;
    const double width =
        area.right > area.left ? area.right - area.left : 1.0;
    g_panGrab =
        g_viewStart + static_cast<double>(point.x - area.left) / width * ViewSpan();
    SetCapture(g_hwnd);
}

void HandleWaveMouseMove(POINT point) {
    if (g_panPending) {
        if (std::abs(point.x - g_panStart.x) >= GetSystemMetrics(SM_CXDRAG) ||
            std::abs(point.y - g_panStart.y) >= GetSystemMetrics(SM_CYDRAG)) {
            if (ViewSpan() < 1.0) {
                g_panPending = false;
                g_panDrag = true;
            } else {
                // The whole track is visible, so the gesture cannot pan. Keep
                // the pending seek instead of dropping it, and let it follow
                // the pointer until the button is released.
                g_panStart = point;
            }
        }
    }
    if (g_scrollDrag) {
        const double before = g_viewStart;
        ScrollViewTo(point.x);
        if (before != g_viewStart) {
            RECT dirty = WaveAreaRect();
            RECT scroll = WaveScrollRect();
            UnionRect(&dirty, &dirty, &scroll);
            RequestFrame(&dirty);
        }
    } else if (g_dragMarker) {
        DragMarker(point.x);
    } else if (g_panDrag) {
        RECT dirty = WaveAreaRect();
        const RECT area = dirty;
        const double width =
            area.right > area.left ? area.right - area.left : 1.0;
        const double before = g_viewStart;
        g_viewStart =
            g_panGrab -
            static_cast<double>(point.x - area.left) / width * ViewSpan();
        ClampView();
        if (before != g_viewStart) {
            RECT scroll = WaveScrollRect();
            UnionRect(&dirty, &dirty, &scroll);
            RequestFrame(&dirty);
        }
    }
}

void HandleWaveMouseUp(POINT point) {
    (void)point;
    // A button release always ends the gesture and releases the capture.
    // The flags are copied first because ReleaseCapture sends
    // WM_CAPTURECHANGED synchronously and that handler clears them. Keeping
    // the capture would make the whole window, including the owned buttons,
    // ignore mouse clicks, which looks like a frozen UI.
    const bool volumeDrag = g_volumeDrag;
    const bool scrollDrag = g_scrollDrag;
    const bool panDrag = g_panDrag;
    const bool panPending = g_panPending;
    const int dragMarker = g_dragMarker;
    const POINT panStart = g_panStart;
    g_volumeDrag = false;
    g_scrollDrag = false;
    g_panDrag = false;
    g_panPending = false;
    g_dragMarker = 0;
    if (GetCapture() == g_hwnd) ReleaseCapture();

    if (volumeDrag) {
        RECT control = VolumeControlRect();
        InvalidateRect(g_hwnd, &control, FALSE);
        return;
    }
    if (scrollDrag || panDrag) {
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }
    if (panPending) {
        const RECT area = WaveAreaRect();
        const double fraction = FracAt(area, panStart.x);
        if (!SeekActivePlayback(fraction)) {
            SetCursorFraction(fraction, true);
        }
        InvalidateRect(g_hwnd, &area, FALSE);
        return;
    }
    if (dragMarker) {
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

void HandleWheel(POINT point, int delta, bool shift) {
    if (!delta) return;
    const RECT deviceList = MenuDeviceListRect();
    if (g_menuOpen && PtInRect(&deviceList, point)) {
        const int maxScroll =
            MaxValue(0, static_cast<int>(g_outputDevices.size()) -
                              kMenuDeviceRows);
        g_menuDeviceScroll += delta > 0 ? -1 : 1;
        g_menuDeviceScroll =
            MaxValue(0, MinValue(g_menuDeviceScroll, maxScroll));
        RECT list = MenuDeviceListRect();
        InvalidateRect(g_hwnd, &list, FALSE);
        return;
    }
    if (g_menuOpen) return;
    if (!g_sourceReady) return;
    RECT area = WaveAreaRect();
    if (!PtInRect(&area, point) && !shift) return;
    if (shift) {
        const double step = ViewSpan() * 0.15 * (delta > 0 ? -1.0 : 1.0);
        g_viewStart += step;
        ClampView();
    } else {
        const double focus = FracAt(area, point.x);
        double ratio = area.right > area.left
                           ? static_cast<double>(point.x - area.left) /
                                 static_cast<double>(area.right - area.left)
                           : 0.5;
        ratio = MaxValue(0.0, MinValue(ratio, 1.0));
        g_zoom *= delta > 0 ? 1.3 : 1.0 / 1.3;
        ClampView();
        g_viewStart = focus - ratio * ViewSpan();
        ClampView();
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

LRESULT HitTestWindow(LPARAM lParam) {
    POINT screen = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    POINT point = screen;
    ScreenToClient(g_hwnd, &point);
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int border = S(6);
    const bool left = point.x < border;
    const bool right = point.x >= client.right - border;
    const bool top = point.y < border;
    const bool bottom = point.y >= client.bottom - border;
    if (left && top) return HTTOPLEFT;
    if (right && top) return HTTOPRIGHT;
    if (left && bottom) return HTBOTTOMLEFT;
    if (right && bottom) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    if (point.x >= client.right - S(kGripSize) &&
        point.y >= client.bottom - S(kGripSize)) {
        return HTBOTTOMRIGHT;
    }
    const RECT menuButton = MenuButtonRect();
    const RECT closeButton = CloseButtonRect();
    if (PtInRect(&menuButton, point) || PtInRect(&closeButton, point)) {
        return HTCLIENT;
    }
    if (point.y < S(kCaptionHeight)) return HTCAPTION;
    return HTCLIENT;
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCPAINT:
        return 0;
    case WM_ERASEBKGND:
        return 1;

    case WM_CREATE: {
        g_hwnd = hwnd;
        EnsureBrushes();
        HINSTANCE instance =
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        g_btnOpen =
            CreateOwnedButton(hwnd, instance, kIdOpen);
        g_btnPlay =
            CreateOwnedButton(hwnd, instance, kIdPlay);
        g_btnRange =
            CreateOwnedButton(hwnd, instance, kIdPlayRange);
        g_btnExport =
            CreateOwnedButton(hwnd, instance, kIdExport);
        LayoutControls();
        RefreshButtonStates();
        SetTimer(hwnd, kTimerPosition, kPositionTimerMs, nullptr);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = MinimumWindowWidth();
        limits->ptMinTrackSize.y = S(kBaseHeight);
        return 0;
    }

    case WM_NCHITTEST:
        return HitTestWindow(lParam);

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LayoutControls();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            CloseMenu();
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client = {};
        GetClientRect(hwnd, &client);
        if (client.right <= 0 || client.bottom <= 0 ||
            paint.rcPaint.right <= paint.rcPaint.left ||
            paint.rcPaint.bottom <= paint.rcPaint.top) {
            EndPaint(hwnd, &paint);
            return 0;
        }
        if (!g_backBitmap || g_backWidth != client.right ||
            g_backHeight != client.bottom) {
            if (g_backBitmap) DeleteObject(g_backBitmap);
            g_backBitmap =
                CreateCompatibleBitmap(dc, client.right, client.bottom);
            g_backWidth = g_backBitmap ? client.right : 0;
            g_backHeight = g_backBitmap ? client.bottom : 0;
            if (g_backBitmap) paint.rcPaint = client;
        }
        if (!g_backBitmap) {
            DrawMain(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ oldBitmap =
            memory ? SelectObject(memory, g_backBitmap) : nullptr;
        if (!memory || !oldBitmap || oldBitmap == HGDI_ERROR) {
            if (memory) DeleteDC(memory);
            DrawMain(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        IntersectClipRect(memory, paint.rcPaint.left, paint.rcPaint.top,
                          paint.rcPaint.right, paint.rcPaint.bottom);
        DrawMain(memory);
        BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
               paint.rcPaint.right - paint.rcPaint.left,
               paint.rcPaint.bottom - paint.rcPaint.top, memory,
               paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteDC(memory);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* item =
            reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON) {
            DrawOwnedButton(item);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kIdOpen: OpenMp3Dialog(); return 0;
            case kIdPlay: ToggleAudition(AuditionKind::Cursor); return 0;
            case kIdPlayRange:
                ToggleAudition(AuditionKind::MarkerRange);
                return 0;
            case kIdExport: StartExport(); return 0;
            default: break;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_menuOpen && HandleMenuClick(point)) return 0;
        RECT menuButton = MenuButtonRect();
        if (PtInRect(&menuButton, point)) {
            if (g_menuOpen) CloseMenu();
            else OpenMenu();
            return 0;
        }
        RECT closeButton = CloseButtonRect();
        if (PtInRect(&closeButton, point)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (StartVolumeDrag(point)) return 0;
        if (point.y >= S(kCaptionHeight)) HandleWaveMouseDown(point);
        return 0;
    }

    case WM_MOUSEMOVE: {
        const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_volumeDrag) {
            SetVolumeFromSlider(point.x);
            return 0;
        }
        HandleWaveMouseMove(point);
        return 0;
    }

    case WM_LBUTTONUP: {
        const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        HandleWaveMouseUp(point);
        return 0;
    }

    case WM_RBUTTONUP: {
        SetFocus(hwnd);
        const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ShowWaveContextMenu(point);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        HandleWheel(point, GET_WHEEL_DELTA_WPARAM(wParam),
                    (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0);
        return 0;
    }

    case WM_CAPTURECHANGED: {
        // Only repaint when a gesture is actually cancelled. A plain click
        // must not force a full window repaint.
        const bool wasDragging = g_dragMarker || g_scrollDrag || g_panDrag ||
                                 g_panPending || g_volumeDrag;
        g_dragMarker = 0;
        g_scrollDrag = false;
        g_panDrag = false;
        g_panPending = false;
        g_volumeDrag = false;
        if (wasDragging) {
            g_pendingDirtyValid = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_CANCELMODE:
        // A modal loop takes the mouse away from the window: drop the gesture
        // so that no stale capture is left behind.
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;

    case WM_KILLFOCUS:
        if (GetCapture() == hwnd) ReleaseCapture();
        CloseMenu();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point = {};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            LPCWSTR cursor = IDC_ARROW;
            RECT volume = VolumeControlRect();
            if (PtInRect(&volume, point)) {
                cursor = IDC_HAND;
            } else if (g_sourceReady) {
                const RECT area = WaveAreaRect();
                const RECT scroll = WaveScrollRect();
                if (PtInRect(&scroll, point)) {
                    cursor = IDC_HAND;
                } else if (PtInRect(&area, point)) {
                    if (g_dragMarker || MarkerAt(point)) cursor = IDC_SIZEWE;
                    else if (g_panDrag || ViewSpan() < 1.0) cursor = IDC_HAND;
                }
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wParam == kTimerPosition) {
            UpdatePosition();
            return 0;
        }
        if (wParam == kTimerFrame) {
            KillTimer(hwnd, kTimerFrame);
            g_frameTimerActive = false;
            g_lastFrameTick = GetTickCount64();
            if (g_pendingDirtyValid) {
                InvalidateRect(hwnd, &g_pendingDirty, FALSE);
                g_pendingDirtyValid = false;
            }
            return 0;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (g_menuOpen) {
                CloseMenu();
            } else if (g_playKind != AuditionKind::None) {
                StopPlayback(true);
            }
            return 0;
        }
        break;

    case kMessageCloseMenu:
        CloseMenu();
        return 0;
    case kMessageLoadDone:
        FinishWaveLoad(static_cast<uint64_t>(wParam));
        return 0;
    case kMessageExportDone:
        FinishExport();
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (GetCapture() == hwnd) ReleaseCapture();
        g_waveCancel.store(true, std::memory_order_relaxed);
        g_exportCancel.store(true, std::memory_order_relaxed);
        StopPlayback(false);
        if (g_waveThread.joinable()) g_waveThread.join();
        if (g_exportThread.joinable()) g_exportThread.join();
        SaveSettings();
        FreeAudioOutput();
        if (g_awakeHeld) {
            SetThreadExecutionState(ES_CONTINUOUS);
            g_awakeHeld = false;
        }
        if (g_backBitmap) {
            DeleteObject(g_backBitmap);
            g_backBitmap = nullptr;
        }
        if (g_font) DeleteObject(g_font);
        if (g_smallFont) DeleteObject(g_smallFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        g_font = nullptr;
        g_smallFont = nullptr;
        g_titleFont = nullptr;
        DeleteBrushes();
        g_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void CenterWindow(HWND hwnd) {
    RECT rect = {};
    GetWindowRect(hwnd, &rect);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR current = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(current, &monitor)) return;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x =
        monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
    const int y =
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

int RunApplication(HINSTANCE instance, PWSTR commandLine, int showCommand) {
    SetProcessDPIAware();
    LoadSettings();
    RebuildFonts();
    EnsureBrushes();

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    if (!InitAudioOutput()) {
        wchar_t text[320] = {};
        swprintf_s(text,
                   L"Could not initialize audio output.\n"
                   L"BASS error code: %lu\n\n"
                   L"Make sure bass.dll, bassmix.dll, and basswasapi.dll "
                   L"(x64) are next to the application.",
                   BASS_ErrorGetCode());
        MessageBoxW(nullptr, text, L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    RefreshOutputDevices();

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon =
        LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = L"Mp3cutterMainWindow";
    if (!RegisterClassExW(&windowClass)) {
        FreeAudioOutput();
        return 1;
    }

    const int width = MaxValue(MinimumWindowWidth(), S(kBaseWidth));
    const int height = S(kBaseHeight);
    HWND hwnd = CreateWindowExW(
        0, windowClass.lpszClassName, L"Mp3cutter",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance,
        nullptr);
    if (!hwnd) {
        FreeAudioOutput();
        return 1;
    }
    g_hwnd = hwnd;
    CenterWindow(hwnd);
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    std::wstring startupPath;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(commandLine, &argumentCount);
    if (arguments) {
        for (int i = 0; i < argumentCount; ++i) {
            if (HasExtension(arguments[i], L".mp3")) {
                startupPath = arguments[i];
                break;
            }
        }
        LocalFree(arguments);
    }
    if (!startupPath.empty()) StartWaveLoad(startupPath);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            g_menuOpen) {
            PostMessageW(g_hwnd, kMessageCloseMenu, 0, 0);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

int ReportFatalError() {
    if (g_waveThread.joinable()) {
        g_waveCancel.store(true, std::memory_order_relaxed);
        g_waveThread.join();
    }
    if (g_exportThread.joinable()) {
        g_exportCancel.store(true, std::memory_order_relaxed);
        g_exportThread.join();
    }
    FreeAudioOutput();
    MessageBoxW(nullptr,
                L"Fatal application error.\nThe application will close.",
                L"Error", MB_OK | MB_ICONERROR);
    return 1;
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE previousInstance,
                    _In_ PWSTR commandLine, _In_ int showCommand) {
    (void)previousInstance;
    (void)commandLine;
    try {
        return RunApplication(instance, commandLine, showCommand);
    } catch (const std::exception&) {
        return ReportFatalError();
    } catch (...) {
        return ReportFatalError();
    }
}
