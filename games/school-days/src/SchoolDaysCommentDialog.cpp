#include <Kotonoha/routing/SchoolDaysCommentDialog.hpp>

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <vector>
#endif

namespace Kotonoha {

#ifdef _WIN32
namespace {

constexpr int kEditId = 0x40c;
constexpr int kInstructionId = 0x410;

struct DialogState {
    std::u16string initial;
    std::u16string result;
    bool useEnglish = false;
};

void AlignDword(std::vector<uint16_t>* words) {
    if ((words->size() & 1u) != 0u) words->push_back(0u);
}

void PushDword(std::vector<uint16_t>* words, uint32_t value) {
    words->push_back(static_cast<uint16_t>(value));
    words->push_back(static_cast<uint16_t>(value >> 16u));
}

void PushString(std::vector<uint16_t>* words, const wchar_t* value) {
    if (value != nullptr) {
        while (*value != L'\0') words->push_back(static_cast<uint16_t>(*value++));
    }
    words->push_back(0u);
}

void PushOrdinal(std::vector<uint16_t>* words, uint16_t ordinal) {
    words->push_back(0xffffu);
    words->push_back(ordinal);
}

void AddExItem(std::vector<uint16_t>* words, DWORD style, DWORD exStyle,
               short x, short y, short cx, short cy, uint32_t id,
               uint16_t classOrdinal, const wchar_t* title) {
    AlignDword(words);
    PushDword(words, 0u); // helpID
    PushDword(words, exStyle);
    PushDword(words, style);
    words->push_back(static_cast<uint16_t>(x));
    words->push_back(static_cast<uint16_t>(y));
    words->push_back(static_cast<uint16_t>(cx));
    words->push_back(static_cast<uint16_t>(cy));
    PushDword(words, id);
    PushOrdinal(words, classOrdinal);
    PushString(words, title);
    words->push_back(0u); // no creation data
}

std::vector<uint16_t> BuildTemplate(bool useEnglish) {
    std::vector<uint16_t> w;
    w.reserve(192u);

    // DIALOGEX geometry/styles match shipped resources 0x73 (JP) and 0x77
    // (EN).  Reconstruct the declarative fields rather than embedding/copying
    // the original resource blob.
    w.push_back(1u);      // dlgVer
    w.push_back(0xffffu); // DIALOGEX signature
    PushDword(&w, 0u);    // helpID
    PushDword(&w, 0u);
    PushDword(&w, 0x90c002cau);
    w.push_back(4u); // cdit
    w.push_back(0u); w.push_back(0u); w.push_back(280u); w.push_back(62u);
    w.push_back(0u); // no menu
    w.push_back(0u); // default dialog class
    PushString(&w, useEnglish ? L"Enter comment" : L"\x30b3\x30e1\x30f3\x30c8\x5165\x529b");
    w.push_back(8u);
    w.push_back(400u);
    w.push_back(0x0100u); // italic=0, charset=DEFAULT_CHARSET(1)
    PushString(&w, L"MS Shell Dlg");

    AddExItem(&w, 0x50010001u, 0u, 223, 41, 50, 14, IDCANCEL, 0x80u,
              useEnglish ? L"Cancel" : L"\x30ad\x30e3\x30f3\x30bb\x30eb");
    AddExItem(&w, 0x50810080u, 0u, 7, 21, 266, 14, kEditId, 0x81u, L"");
    AddExItem(&w, 0x50020000u, 0u, 7, 8, 266, 8, kInstructionId, 0x82u,
              useEnglish ? L"Insert comments (120 characters or less)"
                         : L"\x30b3\x30e1\x30f3\x30c8\x5165\x529b\xff08\xff16\xff10\x6587\x5b57\x4ee5\x5185\xff09");
    AddExItem(&w, 0x50010001u, 0u, 168, 41, 50, 14, IDOK, 0x80u, L"OK");
    return w;
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        if (state != nullptr) {
            SetDlgItemTextW(dialog, kEditId,
                            reinterpret_cast<const wchar_t*>(state->initial.c_str()));
            // The shipped dialog describes 60 JP characters / 120 EN chars.
            SendDlgItemMessageW(dialog, kEditId, EM_SETLIMITTEXT,
                                state->useEnglish ? 120u : 60u, 0);
        }
        SetFocus(GetDlgItem(dialog, kEditId));
        SendDlgItemMessageW(dialog, kEditId, EM_SETSEL, 0u, -1);
        return FALSE;
    }
    if (message == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            if (state == nullptr) return TRUE;
            const int length = GetWindowTextLengthW(GetDlgItem(dialog, kEditId));
            std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1u, L'\0');
            GetDlgItemTextW(dialog, kEditId, buffer.data(),
                            static_cast<int>(buffer.size()));
            state->result.assign(
                reinterpret_cast<const char16_t*>(buffer.data()),
                reinterpret_cast<const char16_t*>(buffer.data() + length));
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

} // namespace
#endif

SchoolDaysCommentDialog::Result SchoolDaysCommentDialog::Run(
    SDL_Window* owner, bool useEnglish, const std::u16string& initial,
    std::u16string* output, std::string* error) {
    if (error != nullptr) error->clear();
    if (output == nullptr) {
        if (error != nullptr) *error = "School Days comment-dialog output is null";
        return Result::Failed;
    }
    output->clear();
#ifndef _WIN32
    (void)owner; (void)useEnglish; (void)initial;
    if (error != nullptr) *error = "School Days native comment dialog is Windows-only";
    return Result::Unsupported;
#else
    HWND hwnd = nullptr;
    if (owner != nullptr) {
        const SDL_PropertiesID props = SDL_GetWindowProperties(owner);
        hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }
    DialogState state;
    state.initial = initial;
    state.useEnglish = useEnglish;
    auto templ = BuildTemplate(useEnglish);
    const INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        reinterpret_cast<const DLGTEMPLATE*>(templ.data()), hwnd, DialogProc,
        reinterpret_cast<LPARAM>(&state));
    if (result == IDOK) {
        *output = std::move(state.result);
        return Result::Confirmed;
    }
    if (result == IDCANCEL) return Result::Cancelled;
    if (error != nullptr) *error = "School Days native comment dialog failed";
    return Result::Failed;
#endif
}

} // namespace Kotonoha
