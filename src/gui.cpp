#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "pak.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

constexpr std::uint32_t page_size = 512;
enum Id { open_base = 100, open_compare, clear_compare, search_id, changed_id,
          list_id, prev_id, next_id };
enum class Change { base, same, added, removed, changed };

struct Row {
    std::string name;
    std::optional<pak::Entry> left, right;
    Change change = Change::base;
};

std::wstring wide(std::string_view value) {
    return {value.begin(), value.end()}; // PAK entry names are ASCII.
}

const wchar_t* label(Change change) {
    switch (change) {
    case Change::same: return L"Igual";
    case Change::added: return L"Adicionado";
    case Change::removed: return L"Removido";
    case Change::changed: return L"Alterado";
    default: return L"—";
    }
}

struct Document {
    std::filesystem::path path;
    pak::Archive archive;
    explicit Document(const std::filesystem::path& file) : path(file), archive(file) {}
};

std::vector<Row> index(Document& base, Document* compare) {
    std::vector<Row> result;
    const auto& left = base.archive.entries();
    result.reserve(left.size() + (compare ? compare->archive.entries().size() : 0));
    for (const auto& [name, entry] : left) {
        Row row{name, entry, {}, Change::base};
        if (compare) {
            const auto right = compare->archive.entries().find(name);
            if (right == compare->archive.entries().end()) {
                row.change = Change::removed;
            } else {
                row.right = right->second;
                row.change = pak::same_content(base.archive, entry, compare->archive, right->second)
                                 ? Change::same : Change::changed;
            }
        }
        result.push_back(std::move(row));
    }
    if (compare) {
        for (const auto& [name, entry] : compare->archive.entries()) {
            if (!left.contains(name)) result.push_back({name, {}, entry, Change::added});
        }
        std::ranges::sort(result, {}, &Row::name);
    }
    return result;
}

struct Page {
    std::array<char, page_size> bytes{};
    std::uint32_t size = 0;
};

Page read_page(Document* doc, const std::optional<pak::Entry>& entry, std::uint32_t start) {
    Page page;
    if (doc && entry && start < entry->size) {
        page.size = std::min(page_size, entry->size - start);
        doc->archive.read(*entry, start, std::span{page.bytes}.first(page.size));
    }
    return page;
}

std::wstring hex_page(const std::optional<pak::Entry>& entry, std::uint32_t start,
                      const Page& page, const Page* other) {
    if (!entry) return L"Arquivo ausente neste pacote.";
    if (entry->size == 0) return L"Arquivo vazio.";
    if (page.size == 0) return L"Fim do arquivo.";

    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (std::uint32_t row = 0; row < page.size; row += 16) {
        const auto count = std::min<std::uint32_t>(16, page.size - row);
        out << std::setw(8) << static_cast<std::uint64_t>(entry->offset) + start + row << L"  ";
        bool changed = false;
        for (std::uint32_t i = 0; i < 16; ++i) {
            if (i >= count) {
                out << L"   ";
                continue;
            }
            const auto at = row + i;
            const auto c = static_cast<unsigned char>(page.bytes[at]);
            const bool differs = other && (at >= other->size || page.bytes[at] != other->bytes[at]);
            changed |= differs;
            out << std::setw(2) << static_cast<unsigned>(c) << (differs ? L'*' : L' ');
        }
        out << L" |";
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto c = static_cast<unsigned char>(page.bytes[row + i]);
            out << (c >= 32 && c <= 126 ? static_cast<wchar_t>(c) : L'.');
        }
        out << L"|" << (changed ? L" *" : L"") << L"\r\n";
    }
    return out.str();
}

class Window {
public:
    HWND hwnd = nullptr;
    void create_controls();
    void layout(int width, int height);
    void open(std::filesystem::path path, bool comparison);
    void choose(bool comparison);
    void command(int id, int notification);
    void selected();

private:
    HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int id = 0,
                 DWORD ex_style = 0);
    void refresh_list();
    void show_hex();
    void update_titles();

    HWND open_a{}, open_b{}, clear{}, title{}, search_label{}, search{}, changed{}, list{},
         left_title{}, right_title{}, left_hex{}, right_hex{}, previous{}, next{}, status{};
    std::unique_ptr<Document> base_, compare_;
    std::vector<Row> rows_;
    std::vector<std::size_t> visible_;
    std::optional<std::size_t> selection_;
    std::uint32_t start_ = 0;
    bool refreshing_ = false;
};

HWND Window::control(const wchar_t* type, const wchar_t* text, DWORD style, int id, DWORD ex_style) {
    auto handle = CreateWindowExW(ex_style, type, text, WS_CHILD | WS_VISIBLE | style,
                                  0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return handle;
}

void Window::create_controls() {
    open_a = control(WC_BUTTONW, L"Abrir PAK…", WS_TABSTOP, open_base);
    open_b = control(WC_BUTTONW, L"Comparar com…", WS_TABSTOP, open_compare);
    clear = control(WC_BUTTONW, L"Limpar comparação", WS_TABSTOP, clear_compare);
    title = control(WC_STATICW, L"", SS_NOPREFIX | SS_PATHELLIPSIS);
    search_label = control(WC_STATICW, L"Filtrar caminho", 0);
    search = control(WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, search_id, WS_EX_CLIENTEDGE);
    changed = control(WC_BUTTONW, L"Só diferenças", BS_AUTOCHECKBOX | WS_TABSTOP, changed_id);
    list = control(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                   list_id, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const std::array headers{L"Caminho no pacote", L"Estado", L"Bytes (base)", L"Bytes (comparação)"};
    const std::array widths{350, 115, 125, 145};
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(headers[i]);
        column.cx = widths[i];
        ListView_InsertColumn(list, i, &column);
    }
    left_title = control(WC_STATICW, L"", SS_NOPREFIX | SS_PATHELLIPSIS);
    right_title = control(WC_STATICW, L"", SS_NOPREFIX | SS_PATHELLIPSIS);
    const DWORD hex_style = WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                            ES_READONLY | ES_AUTOHSCROLL;
    left_hex = control(WC_EDITW, L"", hex_style, 0, WS_EX_CLIENTEDGE);
    right_hex = control(WC_EDITW, L"", hex_style, 0, WS_EX_CLIENTEDGE);
    for (auto edit : {left_hex, right_hex})
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
    previous = control(WC_BUTTONW, L"Página anterior", WS_TABSTOP, prev_id);
    next = control(WC_BUTTONW, L"Próxima página", WS_TABSTOP, next_id);
    status = control(WC_STATICW, L"", SS_NOPREFIX);
    update_titles();
    show_hex();
}

void Window::layout(int width, int height) {
    const int inner = width - 32;
    const int list_height = std::max(130, (height - 150) * 2 / 5);
    const int hex_label_y = 117 + list_height;
    const int hex_height = std::max(80, height - hex_label_y - 74);
    const int left_width = compare_ ? (inner - 12) / 2 : inner;
    auto place = [](HWND target, int x, int y, int w, int h) {
        MoveWindow(target, x, y, std::max(w, 0), h, TRUE);
    };
    place(open_a, 16, 14, 118, 30);
    place(open_b, 142, 14, 130, 30);
    place(clear, 280, 14, 144, 30);
    place(title, 16, 54, inner, 18);
    place(search_label, 16, 82, 89, 18);
    place(search, 108, 77, std::min(350, inner - 250), 24);
    place(changed, std::min(475, width - 150), 77, 130, 24);
    place(list, 16, 106, inner, list_height);
    ListView_SetColumnWidth(list, 0, std::max(250, inner - 410));
    place(left_title, 16, hex_label_y, left_width, 18);
    place(right_title, 28 + left_width, hex_label_y, inner - left_width - 12, 18);
    place(left_hex, 16, hex_label_y + 21, left_width, hex_height);
    place(right_hex, 28 + left_width, hex_label_y + 21, inner - left_width - 12, hex_height);
    place(previous, 16, height - 41, 110, 27);
    place(next, 134, height - 41, 110, 27);
    place(status, 257, height - 36, inner - 240, 22);
}

void Window::update_titles() {
    EnableWindow(open_b, base_ != nullptr);
    EnableWindow(clear, compare_ != nullptr);
    EnableWindow(changed, compare_ != nullptr);
    ShowWindow(right_title, compare_ ? SW_SHOW : SW_HIDE);
    ShowWindow(right_hex, compare_ ? SW_SHOW : SW_HIDE);
    auto summary = base_ ? L"Base: " + base_->path.wstring() : L"Abra um PAK do Quake clássico para começar.";
    if (compare_) summary += L"    |    Comparação: " + compare_->path.wstring();
    SetWindowTextW(title, summary.c_str());
    RECT rect{};
    GetClientRect(hwnd, &rect);
    layout(rect.right, rect.bottom);
}

void Window::open(std::filesystem::path path, bool comparison) {
    auto doc = std::make_unique<Document>(path);
    // Keep the current view if reading either archive fails.
    auto updated = comparison ? index(*base_, doc.get()) : index(*doc, compare_.get());
    if (comparison) compare_ = std::move(doc);
    else base_ = std::move(doc);
    rows_ = std::move(updated);
    update_titles();
    refresh_list();
}

void Window::choose(bool comparison) {
    std::array<wchar_t, 32768> file{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Pacotes Quake (*.pak)\0*.pak\0Todos os arquivos\0*.*\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = comparison ? L"Escolher PAK para comparação" : L"Abrir PAK base";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) open(file.data(), comparison);
}

void Window::refresh_list() {
    const int length = GetWindowTextLengthW(search);
    std::wstring query(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(search, query.data(), length + 1);
    query.resize(length);
    const bool only_changes = compare_ && SendMessageW(changed, BM_GETCHECK, 0, 0) == BST_CHECKED;
    refreshing_ = true;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    visible_.clear();
    selection_.reset();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (only_changes && row.change == Change::same) continue;
        auto name = wide(row.name);
        if (!query.empty() && FindStringOrdinal(FIND_FROMSTART, name.c_str(), -1,
                                                query.c_str(), -1, TRUE) < 0) continue;
        const auto position = static_cast<int>(visible_.size());
        visible_.push_back(i);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = position;
        item.pszText = name.data();
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, position, 1, const_cast<wchar_t*>(label(row.change)));
        auto left = row.left ? std::to_wstring(row.left->size) : L"—";
        auto right = row.right ? std::to_wstring(row.right->size) : L"—";
        ListView_SetItemText(list, position, 2, left.data());
        ListView_SetItemText(list, position, 3, right.data());
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    refreshing_ = false;
    if (!visible_.empty()) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    selected();
}

void Window::selected() {
    if (refreshing_) return;
    const auto item = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto new_selection = item < 0 ? std::nullopt : std::optional{visible_.at(item)};
    if (selection_ != new_selection) start_ = 0;
    selection_ = new_selection;
    show_hex();
}

void Window::show_hex() {
    std::wstring summary = std::to_wstring(visible_.size()) + L" de " +
                           std::to_wstring(rows_.size()) + L" entradas";
    std::uint32_t size = 0;
    if (selection_) {
        const auto& row = rows_[*selection_];
        const auto a = read_page(base_.get(), row.left, start_);
        const auto b = read_page(compare_.get(), row.right, start_);
        const auto left = hex_page(row.left, start_, a, compare_ ? &b : nullptr);
        SetWindowTextW(left_hex, left.c_str());
        if (compare_) SetWindowTextW(right_hex, hex_page(row.right, start_, b, &a).c_str());
        size = std::max(row.left ? row.left->size : 0, row.right ? row.right->size : 0);
        const auto name = wide(row.name);
        SetWindowTextW(left_title, (L"Base  /  " + name).c_str());
        SetWindowTextW(right_title, (L"Comparação  /  " + name).c_str());
        if (size != 0) summary += L"  |  Página " + std::to_wstring(start_ / page_size + 1) +
                                 L" de " + std::to_wstring((size - 1) / page_size + 1);
        if (compare_) summary += L"  |  * byte diferente";
    } else {
        SetWindowTextW(left_title, L"Base");
        SetWindowTextW(right_title, L"Comparação");
        SetWindowTextW(left_hex, base_ ? L"Nenhuma entrada selecionada." : L"Selecione um PAK para ver os bytes.");
        SetWindowTextW(right_hex, L"Nenhuma entrada selecionada.");
    }
    SetWindowTextW(status, summary.c_str());
    EnableWindow(previous, start_ > 0);
    EnableWindow(next, size > start_ && size - start_ > page_size);
}

void Window::command(int id, int notification) {
    if (id == search_id) {
        if (notification == EN_CHANGE) refresh_list();
        return;
    }
    if (notification != BN_CLICKED) return;
    switch (id) {
    case open_base: choose(false); break;
    case open_compare: if (base_) choose(true); break;
    case clear_compare:
        if (!base_) break;
        rows_ = index(*base_, nullptr);
        compare_.reset();
        SendMessageW(changed, BM_SETCHECK, BST_UNCHECKED, 0);
        update_titles();
        refresh_list();
        break;
    case changed_id: refresh_list(); break;
    case prev_id: if (start_ >= page_size) { start_ -= page_size; show_hex(); } break;
    case next_id: if (IsWindowEnabled(next)) { start_ += page_size; show_hex(); } break;
    }
}

LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* view = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        view = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        view->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    try {
        switch (message) {
        case WM_CREATE: view->create_controls(); return 0;
        case WM_SIZE: view->layout(LOWORD(lparam), HIWORD(lparam)); return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {800, 565};
            return 0;
        case WM_COMMAND: view->command(LOWORD(wparam), HIWORD(wparam)); return 0;
        case WM_NOTIFY: {
            const auto& notification = *reinterpret_cast<NMHDR*>(lparam);
            if (notification.idFrom == list_id && notification.code == LVN_ITEMCHANGED) view->selected();
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) {
        MessageBoxW(hwnd, wide(e.what()).c_str(), L"Quake PAK Inspector", MB_OK | MB_ICONERROR);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpszClassName = L"QuakePakInspector";
    cls.lpfnWndProc = dispatch;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    if (!RegisterClassW(&cls)) return 1;
    Window view;
    const auto hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"Quake PAK Inspector",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                      1230, 760, nullptr, nullptr, instance, &view);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    int count = 0;
    const auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv) {
        try {
            if (count >= 2) view.open(argv[1], false);
            if (count >= 3) view.open(argv[2], true);
        } catch (const std::exception& e) {
            MessageBoxW(hwnd, wide(e.what()).c_str(), L"Quake PAK Inspector", MB_OK | MB_ICONERROR);
        }
        LocalFree(argv);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
