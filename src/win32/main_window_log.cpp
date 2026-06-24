#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_log_view.h"
#include "win32/native_send_codec.h"
#include "win32/native_time_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"

#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

constexpr std::size_t kMaxLogEntryLimit = 200000;
constexpr std::size_t kLogTrimRebuildBatch = 256;
constexpr std::size_t kLogInsertBatchChars = 65536;
constexpr std::size_t kMaxRenderedLogLineChars = 4096;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

void NativeMainWindow::appendLog(const std::wstring& line) {
    appendLog(NativeLogKind::System, line);
}

void NativeMainWindow::appendLog(NativeLogKind kind, const std::wstring& line) {
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = nativeLocalClockText();
    entry.text = line;
    addLogEntry(std::move(entry));
}

void NativeMainWindow::appendPayloadLog(NativeLogKind kind, const std::vector<std::uint8_t>& payload) {
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = nativeLocalClockText();
    entry.payloadPrefix = std::wstring(nativeLogPayloadPrefix(kind));
    entry.payload = payload;
    entry.hasPayload = true;
    addLogEntry(std::move(entry));
}

std::wstring NativeMainWindow::formatPayloadForLog(const std::vector<std::uint8_t>& payload) const {
    const int mode = static_cast<int>(selectedComboData(logFormatCombo_, 0));
    switch (mode) {
    case 1:
        return nativeBytesToDecimal(payload);
    case 2:
        return nativeBytesToBinary(payload);
    case 3:
        return nativeDecodeBytesToText(payload, selectedLogCodePage());
    case 4:
        return nativeBytesToHex(payload) + L" | " + nativeDecodeBytesToText(payload, selectedLogCodePage());
    default:
        return nativeBytesToHex(payload);
    }
}

unsigned int NativeMainWindow::selectedLogCodePage() const {
    return static_cast<unsigned int>(selectedComboData(logEncodingCombo_, CP_UTF8));
}

void NativeMainWindow::clearLog() {
    KillTimer(window_, IDT_LOG_FLUSH);
    logFlushTimerActive_ = false;
    pendingLogLines_.clear();
    pendingLogChars_ = 0;
    logEntries_.clear();
    visibleLogChars_ = 0;
    visibleLogLineCount_ = 0;
    logTrimmedSinceRebuild_ = 0;
    SetWindowTextW(receiveLog_, L"");
    logFilterState_.clear();
    logScrollState_.clearContent();
    setStatus(tx(T::ClearLogStatus));
}

std::size_t NativeMainWindow::rebuildLogView() {
    if (receiveLog_ == nullptr) {
        return 0;
    }

    ++logRebuildPassCount_;
    KillTimer(window_, IDT_LOG_FLUSH);
    logFlushTimerActive_ = false;
    pendingLogLines_.clear();
    pendingLogChars_ = 0;
    logTrimmedSinceRebuild_ = 0;
    const int firstVisibleLine = nativeLogFirstVisibleLine(receiveLog_);
    std::deque<std::pair<NativeLogKind, std::wstring>> visibleLines;
    std::size_t visibleChars = 0;
    const std::wstring& loweredFilterText = logFilterState_.loweredFilterText();
    for (const NativeLogEntry& entry : logEntries_) {
        std::wstring rendered = renderLogEntry(entry);
        if (!containsLoweredNeedle(rendered, loweredFilterText)) {
            continue;
        }
        const std::size_t renderedSize = rendered.size();
        while (!visibleLines.empty() && visibleChars + renderedSize > logVisibleCharLimit_) {
            visibleChars -= visibleLines.front().second.size();
            visibleLines.erase(visibleLines.begin());
        }
        if (renderedSize <= logVisibleCharLimit_) {
            visibleChars += renderedSize;
            visibleLines.emplace_back(entry.kind, std::move(rendered));
        }
    }

    NativeLogRedrawGuard redraw(receiveLog_);
    SetWindowTextW(receiveLog_, L"");
    visibleLogChars_ = 0;
    for (const auto& line : visibleLines) {
        insertVisibleLogText(line.first, line.second);
    }
    logFilterState_.resetSearch();
    visibleLogLineCount_ = visibleLines.size();
    if (logScrollState_.autoFollow()) {
        nativeLogScrollToBottom(receiveLog_);
    } else {
        nativeLogRestoreFirstVisibleLine(receiveLog_, firstVisibleLine);
    }
    return visibleLogLineCount_;
}

void NativeMainWindow::queueVisibleLogEntry(const NativeLogEntry& entry) {
    queueVisibleLogText(entry.kind, renderLogEntry(entry));
}

void NativeMainWindow::queueVisibleLogText(NativeLogKind kind, std::wstring text) {
    pendingLogChars_ += text.size();
    pendingLogLines_.push_back(NativePendingLogLine{kind, std::move(text)});
    ++logQueuedLineCount_;
    scheduleLogFlush();
}

void NativeMainWindow::scheduleLogFlush() {
    if (window_ == nullptr || logFlushTimerActive_) {
        return;
    }
    logFlushTimerActive_ = SetTimer(window_, IDT_LOG_FLUSH, 40, nullptr) != 0;
}

void NativeMainWindow::flushPendingLogEntries() {
    if (receiveLog_ == nullptr) {
        return;
    }

    const bool needsTrimRebuild = logTrimmedSinceRebuild_ >= kLogTrimRebuildBatch;
    if (pendingLogLines_.empty() && !needsTrimRebuild) {
        return;
    }

    KillTimer(window_, IDT_LOG_FLUSH);
    logFlushTimerActive_ = false;
    ++logFlushPassCount_;

    if (pendingLogLines_.empty()) {
        rebuildLogView();
        return;
    }

    const NativeLogFollowDecision followDecision = logScrollState_.followDecision(nativeLogIsAtBottom(receiveLog_));
    const int firstVisibleLine = nativeLogFirstVisibleLine(receiveLog_);
    const NativeLogSelection selection = nativeLogSelection(receiveLog_);

    {
        NativeLogRedrawGuard redraw(receiveLog_);
        while (!pendingLogLines_.empty()) {
            const NativeLogKind batchKind = pendingLogLines_.front().kind;
            std::wstring batchText;
            std::size_t batchLineCount = 0;
            while (!pendingLogLines_.empty() && pendingLogLines_.front().kind == batchKind) {
                NativePendingLogLine& nextLine = pendingLogLines_.front();
                if (!batchText.empty() && batchText.size() + nextLine.text.size() > kLogInsertBatchChars) {
                    break;
                }
                batchText += nextLine.text;
                pendingLogChars_ -= std::min(pendingLogChars_, nextLine.text.size());
                pendingLogLines_.pop_front();
                ++batchLineCount;
            }
            insertVisibleLogText(batchKind, batchText);
            visibleLogLineCount_ += batchLineCount;
        }
    }

    if (visibleLogChars_ > logVisibleCharLimit_ || needsTrimRebuild) {
        rebuildLogView();
        return;
    }

    if (followDecision.shouldFollow) {
        nativeLogScrollToBottom(receiveLog_);
        return;
    }
    nativeLogRestoreFirstVisibleLine(receiveLog_, firstVisibleLine);
    nativeLogSetSelection(receiveLog_, selection.start, selection.end);
    if (followDecision.shouldShowHistoryNotice) {
        setStatus(tx(T::LogHistoryReadStatus));
    }
}

void NativeMainWindow::appendVisibleLogEntry(const NativeLogEntry& entry) {
    appendVisibleLogText(entry.kind, renderLogEntry(entry));
}

void NativeMainWindow::appendVisibleLogText(NativeLogKind kind, const std::wstring& text) {
    if (receiveLog_ == nullptr) {
        return;
    }
    const NativeLogFollowDecision followDecision = logScrollState_.followDecision(nativeLogIsAtBottom(receiveLog_));
    const int firstVisibleLine = nativeLogFirstVisibleLine(receiveLog_);
    const NativeLogSelection selection = nativeLogSelection(receiveLog_);
    insertVisibleLogText(kind, text);
    if (followDecision.shouldFollow) {
        nativeLogScrollToBottom(receiveLog_);
        return;
    }
    nativeLogRestoreFirstVisibleLine(receiveLog_, firstVisibleLine);
    nativeLogSetSelection(receiveLog_, selection.start, selection.end);
    if (followDecision.shouldShowHistoryNotice) {
        setStatus(tx(T::LogHistoryReadStatus));
    }
}

void NativeMainWindow::insertVisibleLogText(NativeLogKind kind, const std::wstring& text) {
    if (receiveLog_ == nullptr) {
        return;
    }
    nativeLogInsertText(receiveLog_, receiveLogUsesRichEdit_, logThemeIndex_, kind, text);
    visibleLogChars_ += text.size();
}

void NativeMainWindow::addLogEntry(NativeLogEntry entry) {
    entry.text = sanitizeLogText(entry.text);
    std::size_t trimmedCount = 0;
    logEntries_.push_back(std::move(entry));
    while (logEntries_.size() > logEntryLimit_) {
        logEntries_.pop_front();
        ++trimmedCount;
    }
    logTrimmedSinceRebuild_ += trimmedCount;

    if (logScrollState_.paused()) {
        const std::size_t hiddenLineCount = logScrollState_.noteHiddenLine();
        setStatus(uiString(T::ScrollPausedPrefix) + std::to_wstring(hiddenLineCount) + uiString(T::HiddenLinesSuffix));
        return;
    }

    const NativeLogEntry& latest = logEntries_.back();
    const std::wstring& loweredFilterText = logFilterState_.loweredFilterText();
    if (loweredFilterText.empty()) {
        queueVisibleLogEntry(latest);
        return;
    }

    std::wstring rendered = renderLogEntry(latest);
    if (containsLoweredNeedle(rendered, loweredFilterText)) {
        queueVisibleLogText(latest.kind, std::move(rendered));
    } else if (logTrimmedSinceRebuild_ >= kLogTrimRebuildBatch) {
        scheduleLogFlush();
    }
}

std::wstring NativeMainWindow::renderLogEntry(const NativeLogEntry& entry) const {
    std::wstring line;
    if (showLogTimestamps_) {
        line += L"[";
        line += entry.timestamp;
        line += L"] ";
    }
    if (entry.hasPayload) {
        line += entry.payloadPrefix;
        line += L" ";
        line += formatPayloadForLog(entry.payload);
    } else {
        line += entry.text;
    }
    line = clipRenderedLogLine(sanitizeLogText(line), kMaxRenderedLogLineChars, tx(T::LogEntryClippedSuffix));
    line += L"\r\n";
    return line;
}

std::wstring NativeMainWindow::visibleLogText() const {
    return controlText(receiveLog_);
}

void NativeMainWindow::updateLogFilter() {
    const NativeLogFilterUpdate filterUpdate = logFilterState_.setFilterText(controlText(logFilterEdit_));
    if (!filterUpdate.changed) {
        setStatus(uiString(T::LogFilterChangedPrefix) + std::to_wstring(visibleLogLineCount_) + uiString(T::ChinesePeriod));
        return;
    }
    const std::size_t visibleCount = rebuildLogView();
    setStatus(uiString(T::LogFilterChangedPrefix) + std::to_wstring(visibleCount) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::findNextLogMatch() {
    flushPendingLogEntries();
    const std::wstring needle = controlText(logSearchEdit_);
    if (needle.empty()) {
        setStatus(tx(T::LogFindEmptyStatus));
        return;
    }

    const std::wstring visibleText = visibleLogText();
    const NativeLogSearchResult search = logFilterState_.findNext(visibleText, needle);
    if (!search.found) {
        setStatus(uiString(T::LogFindNotFoundPrefix) + needle + uiString(T::ChinesePeriod));
        return;
    }

    nativeLogSetSelection(receiveLog_, search.position, search.position + search.length);
    nativeLogScrollCaret(receiveLog_);
    logScrollState_.markHistoryRead();
    setStatus(uiString(T::LogFindMatchedPrefix) + needle + uiString(T::ChinesePeriod));
}

void NativeMainWindow::toggleLogScrollPause() {
    if (!logScrollState_.paused()) {
        flushPendingLogEntries();
    }
    const bool paused = logScrollState_.togglePause();
    SetWindowTextW(pauseScrollButton_, paused ? tx(T::ResumeScrollButton) : tx(T::PauseScrollButton));
    if (!paused) {
        rebuildLogView();
        followLatestLogSilently();
    }
    setStatus(paused ? tx(T::PauseScrollStatus) : tx(T::ResumeScrollStatus));
}

void NativeMainWindow::followLatestLogSilently() {
    if (logScrollState_.paused()) {
        logScrollState_.resume();
        SetWindowTextW(pauseScrollButton_, tx(T::PauseScrollButton));
        rebuildLogView();
    }
    logScrollState_.followLatest();
    nativeLogScrollToBottom(receiveLog_);
}

void NativeMainWindow::followLatestLog() {
    followLatestLogSilently();
    setStatus(tx(T::FollowLatestLogStatus));
}

void NativeMainWindow::copyVisibleLogToClipboard() {
    flushPendingLogEntries();
    const std::wstring text = visibleLogText();
    if (!OpenClipboard(window_)) {
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    std::memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    CloseClipboard();
    setStatus(tx(T::LogCopiedStatus));
}

void NativeMainWindow::exportVisibleLog() {
    flushPendingLogEntries();
    const std::wstring text = visibleLogText();
    if (text.empty()) {
        setStatus(tx(T::LogExportEmptyStatus));
        return;
    }

    wchar_t fileName[MAX_PATH] = {};
    const std::wstring defaultName = uiString(T::LogExportDefaultPrefix)
        + utf8ToWide(nativeTimestampIdText())
        + L".txt";
    wcsncpy_s(fileName, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = tx(T::LogExportFilter);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrTitle = tx(T::LogExportDialogTitle);
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }

    const std::string utf8Text = wideToUtf8(text);
    std::ofstream output(std::filesystem::path(fileName), std::ios::binary | std::ios::trunc);
    if (!output) {
        setStatus(uiString(T::LogExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    output.write(utf8Text.data(), static_cast<std::streamsize>(utf8Text.size()));
    if (!output) {
        setStatus(uiString(T::LogExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    setStatus(uiString(T::LogExportOkPrefix) + std::wstring(fileName) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::applyLogTheme(int themeIndex) {
    logThemeIndex_ = std::clamp(themeIndex, 0, 2);
    if (menu_ != nullptr) {
        HMENU viewMenu = GetSubMenu(menu_, 4);
        CheckMenuRadioItem(
            viewMenu != nullptr ? viewMenu : menu_,
            IDM_VIEW_THEME_DEFAULT,
            IDM_VIEW_THEME_HIGH_CONTRAST,
            logThemeIndex_ == 0 ? IDM_VIEW_THEME_DEFAULT : (logThemeIndex_ == 1 ? IDM_VIEW_THEME_SOFT : IDM_VIEW_THEME_HIGH_CONTRAST),
            MF_BYCOMMAND);
    }

    nativeLogApplyTheme(receiveLog_, receiveLogUsesRichEdit_, logThemeIndex_);
}

void NativeMainWindow::applyLogCacheLimit(std::size_t visibleCharLimit) {
    logVisibleCharLimit_ = nativeNormalizeLogVisibleCharLimit(visibleCharLimit);
    logEntryLimit_ = std::clamp<std::size_t>(logVisibleCharLimit_ / 160, 1000, kMaxLogEntryLimit);
    if (receiveLog_ != nullptr) {
        nativeLogSetTextLimit(receiveLog_, logVisibleCharLimit_ + kMaxRenderedLogLineChars);
    }
}

void NativeMainWindow::updateLogTimestampMenu() {
    if (menu_ == nullptr) {
        return;
    }
    HMENU viewMenu = GetSubMenu(menu_, 4);
    CheckMenuItem(
        viewMenu != nullptr ? viewMenu : menu_,
        IDM_VIEW_SHOW_TIMESTAMPS,
        MF_BYCOMMAND | (showLogTimestamps_ ? MF_CHECKED : MF_UNCHECKED));
}

} // namespace svm::win32

#endif
