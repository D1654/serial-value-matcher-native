#include "win32/native_log_model.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>

namespace {

using svm::win32::NativeLogFilterState;

void unchangedFilterDoesNotRequestRebuild() {
    NativeLogFilterState state;
    auto update = state.setFilterText(L"TX");
    assert(update.changed);
    assert(update.filterText == L"TX");

    update = state.setFilterText(L"TX");
    assert(!update.changed);
    assert(update.filterText == L"TX");
}

void changedFilterResetsSearchState() {
    NativeLogFilterState state;
    state.setFilterText(L"TX");
    const auto first = state.findNext(L"abc tx abc tx", L"tx");
    assert(first.found);
    assert(state.searchOffset() > 0);

    const auto update = state.setFilterText(L"RX");
    assert(update.changed);
    assert(state.filterText() == L"RX");
    assert(state.loweredFilterText() == L"rx");
    assert(state.searchText().empty());
    assert(state.searchOffset() == 0);
}

void findNextIsCaseInsensitiveAndWraps() {
    NativeLogFilterState state;
    const std::wstring text = L"[TX] One\r\n[rx] two\r\n[TX] three\r\n";

    auto match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position == 1);
    assert(match.length == 2);
    assert(!match.wrapped);

    match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position > 1);
    assert(!match.wrapped);

    match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position == 1);
    assert(match.wrapped);
}

void emptyNeedleClearsSearch() {
    NativeLogFilterState state;
    assert(state.findNext(L"abc", L"a").found);
    const auto empty = state.findNext(L"abc", L"");
    assert(!empty.found);
    assert(state.searchText().empty());
    assert(state.searchOffset() == 0);
}

void visibleLogTextEscapesControlCharacters() {
    std::wstring text;
    text.push_back(L'A');
    text.push_back(L'\r');
    text.push_back(L'\n');
    text.push_back(L'\t');
    text.push_back(static_cast<wchar_t>(0x01));
    text.push_back(L'B');

    assert(svm::win32::sanitizeLogText(text) == L"A\\r\\n\\t.B");
}

void containsNeedleIsCaseInsensitive() {
    assert(svm::win32::containsCaseInsensitive(L"[RX] Temperature=42", L"rx"));
    assert(svm::win32::containsCaseInsensitive(L"[TX] 温度=42", L"tx"));
    assert(!svm::win32::containsCaseInsensitive(L"[系统] connected", L"error"));
}

void longVisibleLogLinesAreClippedWithSuffix() {
    const std::wstring clipped = svm::win32::clipRenderedLogLine(L"ABCDEFGHIJ", 7, L"...");
    assert(clipped == L"ABCD...");

    const std::wstring unchanged = svm::win32::clipRenderedLogLine(L"ABC", 7, L"...");
    assert(unchanged == L"ABC");
    assert(svm::win32::clipRenderedLogLine(L"ABCDEFGHIJ", 2, L"...") == L"..");
    assert(svm::win32::clipRenderedLogLine(L"ABC", 0, L"...").empty());
}

void logEntriesAreBuiltByTheModel() {
    const auto text = svm::win32::nativeMakeTextLogEntry(
        svm::win32::NativeLogKind::Error,
        L"10:00",
        L"failed");
    assert(text.kind == svm::win32::NativeLogKind::Error);
    assert(text.timestamp == L"10:00");
    assert(text.text == L"failed");
    assert(!text.hasPayload);
    assert(!text.serialMetadata.has_value());

    const auto payload = svm::win32::nativeMakePayloadLogEntry(
        svm::win32::NativeLogKind::Tx,
        L"10:01",
        {0x41, 0x42});
    assert(payload.kind == svm::win32::NativeLogKind::Tx);
    assert(payload.timestamp == L"10:01");
    assert(payload.payloadPrefix == L"[TX]");
    assert(payload.payload.size() == 2);
    assert(payload.hasPayload);
    assert(!payload.serialMetadata.has_value());
}

void serialMetadataIsBoundedAndPayloadRemainsExplicit() {
    svm::transport::SerialOperationResult result{
        .operation = {
            .requestId = 42,
            .generation = 7,
            .kind = svm::transport::SerialOperationKind::Read,
        },
        .status = svm::transport::SerialOperationStatus::Failed,
        .deadlineStatus = svm::transport::SerialDeadlineStatus::Expired,
        .byteCount = 3,
        .endpoint = std::string(1024, 'P'),
        .error = {
            .category = svm::transport::SerialErrorCategory::IoFailure,
            .nativeCode = 0,
            .byteCount = 3,
            .commErrorMask = 0x0A,
            .inputQueueBytes = 12,
            .outputQueueBytes = 4,
        },
    };

    const auto metadataOnly = svm::win32::nativeMakeSerialTextLogEntry(
        svm::win32::NativeLogKind::Error,
        L"10:02",
        std::wstring(4096, L'E'),
        result);
    assert(!metadataOnly.hasPayload);
    assert(metadataOnly.payload.empty());
    assert(metadataOnly.text.size() == svm::win32::kNativeSerialLogTextMaxChars);
    assert(metadataOnly.serialMetadata.has_value());
    const auto& metadata = *metadataOnly.serialMetadata;
    assert(metadata.direction == svm::transport::SerialDataDirection::Receive);
    assert(metadata.operation == svm::transport::SerialOperationKind::Read);
    assert(metadata.status == svm::transport::SerialOperationStatus::Failed);
    assert(metadata.requestId == 42);
    assert(metadata.generation == 7);
    assert(metadata.byteCount == 3);
    assert(metadata.endpoint.size() == svm::win32::kNativeSerialLogEndpointMaxBytes);
    assert(metadata.nativeCode == 0);
    assert(metadata.commErrorMask == 0x0A);
    assert(metadata.inputQueueBytes == 12);
    assert(metadata.outputQueueBytes == 4);

    const auto payload = svm::win32::nativeMakeSerialPayloadLogEntry(
        svm::win32::NativeLogKind::Rx,
        L"10:03",
        {0x41, 0x42, 0x43},
        result);
    assert(payload.hasPayload);
    assert(payload.payload.size() == 3);
    assert(payload.serialMetadata.has_value());
}

} // namespace

int main() {
    unchangedFilterDoesNotRequestRebuild();
    changedFilterResetsSearchState();
    findNextIsCaseInsensitiveAndWraps();
    emptyNeedleClearsSearch();
    visibleLogTextEscapesControlCharacters();
    containsNeedleIsCaseInsensitive();
    longVisibleLogLinesAreClippedWithSuffix();
    logEntriesAreBuiltByTheModel();
    serialMetadataIsBoundedAndPayloadRemainsExplicit();

    std::cout << "native_log_filter_state_tests passed\n";
    return 0;
}
