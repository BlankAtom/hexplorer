#include "NativeServices.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QWindow>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

namespace {
const QString dropEffectMime = QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
const QString cutMime = QStringLiteral("application/x-kde-cutselection");

QMimeData *fileMimeData(const QList<QUrl> &urls, bool cut)
{
    auto *mime = new QMimeData;
    // Qt's Windows MIME converter exposes these URLs as CF_HDROP to Explorer.
    mime->setUrls(urls);
    QByteArray effect(4, '\0');
    qToLittleEndian<quint32>(cut ? 2u : 1u, effect.data());
    mime->setData(dropEffectMime, effect);
    mime->setData(cutMime, cut ? QByteArray("1") : QByteArray("0"));
    return mime;
}

bool isCut(const QMimeData *mime)
{
    if (mime->hasFormat(dropEffectMime)) {
        const QByteArray effect = mime->data(dropEffectMime);
        return effect.size() >= 4 && qFromLittleEndian<quint32>(effect.constData()) == 2;
    }
    return mime->data(cutMime) == QByteArray("1");
}

QString pathKey(const QUrl &url)
{
    QString path = QDir::cleanPath(url.toLocalFile());
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

#ifdef Q_OS_WIN
template<typename T> struct ComRelease {
    void operator()(T *value) const { if (value) value->Release(); }
};
template<typename T> using ComPtr = std::unique_ptr<T, ComRelease<T>>;
struct PidlRelease {
    void operator()(ITEMIDLIST __unaligned *value) const
    {
        CoTaskMemFree(const_cast<ITEMIDLIST *>(value));
    }
};
using PidlPtr = std::unique_ptr<ITEMIDLIST __unaligned, PidlRelease>;

class OleScope {
public:
    OleScope() : result(OleInitialize(nullptr)) {}
    ~OleScope() { if (SUCCEEDED(result)) OleUninitialize(); }
    HRESULT result;
};

HWND ownerWindow()
{
    QWindow *window = QGuiApplication::focusWindow();
    if (!window) {
        for (QWindow *candidate : QGuiApplication::topLevelWindows()) {
            if (candidate->isVisible()) {
                window = candidate;
                break;
            }
        }
    }
    return window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
}

QString shellError(const QString &action, HRESULT error)
{
    return QStringLiteral("%1 failed (Windows error 0x%2).")
        .arg(action, QString::number(quint32(error), 16).rightJustified(8, QLatin1Char('0')));
}

struct MenuMessages {
    IContextMenu2 *menu2 = nullptr;
    IContextMenu3 *menu3 = nullptr;
};

LRESULT CALLBACK menuWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *menus = reinterpret_cast<MenuMessages *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (menus && (message == WM_INITMENUPOPUP || message == WM_DRAWITEM
                  || message == WM_MEASUREITEM || message == WM_MENUCHAR)) {
        if ((message == WM_DRAWITEM || message == WM_MEASUREITEM) && wParam != 0)
            return DefWindowProcW(window, message, wParam, lParam);
        if (menus->menu3) {
            LRESULT result = 0;
            if (SUCCEEDED(menus->menu3->HandleMenuMsg2(message, wParam, lParam, &result)))
                return result;
        } else if (menus->menu2 && message != WM_MENUCHAR) {
            if (SUCCEEDED(menus->menu2->HandleMenuMsg(message, wParam, lParam)))
                return message == WM_INITMENUPOPUP ? 0 : TRUE;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createMenuWindow(HWND owner, MenuMessages *messages)
{
    static const ATOM atom = [] {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = menuWindowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"HExplorerShellMenuOwner";
        return RegisterClassW(&windowClass);
    }();
    if (!atom)
        return nullptr;
    return CreateWindowExW(WS_EX_TOOLWINDOW, MAKEINTATOM(atom), L"", WS_POPUP,
                           0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), messages);
}

// QML supplies logical global coordinates; native popup APIs expect physical pixels.
POINT nativePoint(double x, double y, HWND owner)
{
    POINT result{LONG(std::clamp(x, double(std::numeric_limits<LONG>::min()),
                                 double(std::numeric_limits<LONG>::max()))),
                 LONG(std::clamp(y, double(std::numeric_limits<LONG>::min()),
                                 double(std::numeric_limits<LONG>::max())))};
    QWindow *window = QGuiApplication::focusWindow();
    if (window && reinterpret_cast<HWND>(window->winId()) == owner) {
        POINT origin{0, 0};
        if (ClientToScreen(owner, &origin)) {
            const QPoint logicalOrigin = window->mapToGlobal(QPoint(0, 0));
            const qreal scale = window->devicePixelRatio();
            result.x = origin.x + LONG(std::clamp((x - logicalOrigin.x()) * scale,
                                                double(std::numeric_limits<LONG>::min() / 2),
                                                double(std::numeric_limits<LONG>::max() / 2)));
            result.y = origin.y + LONG(std::clamp((y - logicalOrigin.y()) * scale,
                                                double(std::numeric_limits<LONG>::min() / 2),
                                                double(std::numeric_limits<LONG>::max() / 2)));
        }
    }
    return result;
}
#endif
} // namespace

NativeServices::NativeServices(QObject *parent) : QObject(parent) {}

QList<QUrl> NativeServices::localUrls(const QVariantList &values)
{
    QList<QUrl> urls;
    for (const QVariant &value : values) {
        const QUrl url = value.toUrl();
        if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()
            || url.toLocalFile().contains(QChar::Null)) {
            emit errorOccurred(QStringLiteral("This action requires local file URLs."));
            return {};
        }
        urls.append(url);
    }
    return urls;
}

void NativeServices::open(QUrl url)
{
    if (localUrls({url}).isEmpty())
        return;
    if (!QDesktopServices::openUrl(url))
        emit errorOccurred(QStringLiteral("Windows or the desktop could not open this item."));
}

void NativeServices::reveal(QUrl url)
{
    if (localUrls({url}).isEmpty())
        return;
#ifdef Q_OS_WIN
    OleScope ole;
    if (FAILED(ole.result)) {
        emit errorOccurred(shellError(QStringLiteral("Initialize shell"), ole.result));
        return;
    }
    const QString path = QDir::toNativeSeparators(url.toLocalFile());
    PIDLIST_ABSOLUTE raw = nullptr;
    HRESULT result = SHParseDisplayName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &raw, 0, nullptr);
    PidlPtr item(raw);
    if (SUCCEEDED(result))
        result = SHOpenFolderAndSelectItems(item.get(), 0, nullptr, 0);
    if (FAILED(result))
        emit errorOccurred(shellError(QStringLiteral("Reveal item"), result));
#else
    const QUrl parent = QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath());
    if (!QDesktopServices::openUrl(parent))
        emit errorOccurred(QStringLiteral("The desktop could not open the containing folder."));
#endif
}

void NativeServices::properties(QUrl url)
{
    if (localUrls({url}).isEmpty())
        return;
#ifdef Q_OS_WIN
    OleScope ole;
    if (FAILED(ole.result)) {
        emit errorOccurred(shellError(QStringLiteral("Initialize shell"), ole.result));
        return;
    }
    const QString path = QDir::toNativeSeparators(url.toLocalFile());
    SHELLEXECUTEINFOW execution{};
    execution.cbSize = sizeof(execution);
    execution.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    execution.hwnd = ownerWindow();
    execution.lpVerb = L"properties";
    execution.lpFile = reinterpret_cast<LPCWSTR>(path.utf16());
    execution.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execution))
        emit errorOccurred(shellError(QStringLiteral("Show properties"), HRESULT_FROM_WIN32(GetLastError())));
#else
    emit errorOccurred(QStringLiteral("Native properties are only available on Windows. Opening the containing folder."));
    reveal(url);
#endif
}

void NativeServices::contextMenu(QVariantList values, double x, double y)
{
    const QList<QUrl> urls = localUrls(values);
    if (urls.isEmpty())
        return;
    if (!std::isfinite(x) || !std::isfinite(y)) {
        emit errorOccurred(QStringLiteral("Invalid context menu position."));
        return;
    }
    const QString parent = pathKey(QUrl::fromLocalFile(QFileInfo(urls.first().toLocalFile()).absolutePath()));
    for (const QUrl &url : urls) {
        if (pathKey(QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath())) != parent) {
            emit errorOccurred(QStringLiteral("Shell context menus require all selected items to be in the same folder."));
            return;
        }
    }
#ifdef Q_OS_WIN
    OleScope ole;
    if (FAILED(ole.result)) {
        emit errorOccurred(shellError(QStringLiteral("Initialize shell"), ole.result));
        return;
    }
    std::vector<PidlPtr> items;
    std::vector<PCUITEMID_CHILD> children;
    ComPtr<IShellFolder> folder;
    PidlPtr parentItem;
    for (const QUrl &url : urls) {
        const QString path = QDir::toNativeSeparators(url.toLocalFile());
        PIDLIST_ABSOLUTE raw = nullptr;
        HRESULT result = SHParseDisplayName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &raw, 0, nullptr);
        PidlPtr item(raw);
        if (FAILED(result)) {
            emit errorOccurred(shellError(QStringLiteral("Find shell item"), result));
            return;
        }
        PidlPtr currentParent(ILCloneFull(item.get()));
        if (!currentParent || !ILRemoveLastID(currentParent.get())) {
            emit errorOccurred(QStringLiteral("The shell cannot show a file menu for this root item."));
            return;
        }
        if (parentItem && !ILIsEqual(parentItem.get(), currentParent.get())) {
            emit errorOccurred(QStringLiteral("Selected shell items have different parents."));
            return;
        }
        if (!folder) {
            IShellFolder *rawFolder = nullptr;
            PCUITEMID_CHILD child = nullptr;
            result = SHBindToParent(item.get(), IID_IShellFolder, reinterpret_cast<void **>(&rawFolder), &child);
            folder.reset(rawFolder);
            if (FAILED(result)) {
                emit errorOccurred(shellError(QStringLiteral("Open shell folder"), result));
                return;
            }
            parentItem = std::move(currentParent);
        }
        children.push_back(ILFindLastID(item.get()));
        items.push_back(std::move(item));
    }
    const HWND owner = ownerWindow();
    IContextMenu *rawMenu = nullptr;
    HRESULT result = folder->GetUIObjectOf(owner, UINT(children.size()), children.data(),
                                          IID_IContextMenu, nullptr, reinterpret_cast<void **>(&rawMenu));
    ComPtr<IContextMenu> menu(rawMenu);
    if (FAILED(result)) {
        emit errorOccurred(shellError(QStringLiteral("Create shell context menu"), result));
        return;
    }
    IContextMenu3 *rawMenu3 = nullptr;
    IContextMenu2 *rawMenu2 = nullptr;
    menu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void **>(&rawMenu3));
    ComPtr<IContextMenu3> menu3(rawMenu3);
    if (!menu3)
        menu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void **>(&rawMenu2));
    ComPtr<IContextMenu2> menu2(rawMenu2);
    MenuMessages messages{menu2.get(), menu3.get()};
    const HWND menuOwner = createMenuWindow(owner, &messages);
    if (!menuOwner) {
        emit errorOccurred(QStringLiteral("Could not create the native menu owner window."));
        return;
    }
    const HMENU popup = CreatePopupMenu();
    if (!popup) {
        DestroyWindow(menuOwner);
        emit errorOccurred(QStringLiteral("Could not allocate a native context menu."));
        return;
    }
    const bool extended = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    result = menu->QueryContextMenu(popup, 0, 1, 0x7fff, CMF_NORMAL | (extended ? CMF_EXTENDEDVERBS : 0));
    const POINT point = nativePoint(x, y, owner);
    UINT selected = 0;
    if (SUCCEEDED(result)) {
        SetForegroundWindow(owner ? owner : menuOwner);
        selected = UINT(TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, menuOwner, nullptr));
        PostMessageW(menuOwner, WM_NULL, 0, 0);
    }
    if (selected) {
        CMINVOKECOMMANDINFOEX command{};
        command.cbSize = sizeof(command);
        command.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        if (GetKeyState(VK_CONTROL) & 0x8000)
            command.fMask |= CMIC_MASK_CONTROL_DOWN;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            command.fMask |= CMIC_MASK_SHIFT_DOWN;
        command.hwnd = owner ? owner : menuOwner;
        command.lpVerb = MAKEINTRESOURCEA(selected - 1);
        command.lpVerbW = MAKEINTRESOURCEW(selected - 1);
        command.nShow = SW_SHOWNORMAL;
        command.ptInvoke = point;
        result = menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO *>(&command));
    }
    DestroyMenu(popup);
    DestroyWindow(menuOwner);
    if (FAILED(result))
        emit errorOccurred(shellError(QStringLiteral("Shell context menu"), result));
#else
    emit errorOccurred(QStringLiteral("Native shell context menus are only available on Windows."));
#endif
}

void NativeServices::setClipboard(QVariantList values, bool cut)
{
    const QList<QUrl> urls = localUrls(values);
    if (urls.isEmpty())
        return;
    QGuiApplication::clipboard()->setMimeData(fileMimeData(urls, cut));
}

QVariantMap NativeServices::clipboardFiles()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVariantList urls;
    if (mime) {
        for (const QUrl &url : mime->urls()) {
            if (url.isValid() && url.isLocalFile() && !url.toLocalFile().isEmpty()
                && !url.toLocalFile().contains(QChar::Null))
                urls.append(url);
        }
    }
    return {{QStringLiteral("urls"), urls},
            {QStringLiteral("cut"), mime && !urls.isEmpty() && isCut(mime)}};
}

void NativeServices::clearCutClipboard(QVariantList completedUrls)
{
    const QList<QUrl> completed = localUrls(completedUrls);
    if (completed.isEmpty())
        return;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !isCut(mime))
        return;
    const QList<QUrl> current = mime->urls();
    if (current.size() != completed.size())
        return;
    QStringList expected;
    QStringList actual;
    for (const QUrl &url : completed)
        expected.append(pathKey(url));
    for (const QUrl &url : current) {
        if (!url.isLocalFile())
            return;
        actual.append(pathKey(url));
    }
    expected.sort();
    actual.sort();
    if (expected == actual)
        QGuiApplication::clipboard()->clear();
}

int NativeServices::startDrag(QVariantList values)
{
    if (m_dragActive)
        return int(Qt::IgnoreAction);
    const QList<QUrl> urls = localUrls(values);
    if (urls.isEmpty())
        return int(Qt::IgnoreAction);
    QWindow *window = QGuiApplication::focusWindow();
    if (!window) {
        emit errorOccurred(QStringLiteral("A focused window is required to start dragging files."));
        return int(Qt::IgnoreAction);
    }
    QPointer<QDrag> drag = new QDrag(window);
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    drag->setMimeData(mime);
    // Unlike clipboard transfers, a drag must not advertise Preferred DropEffect:
    // Explorer uses its volume policy and modifiers when that hint is absent.
    // Copy is only Qt's fallback proposal; native receivers negotiate all actions.
    QPointer<NativeServices> self(this);
    m_dragActive = true;
    emit dragActiveChanged();
    if (!self || !drag) {
        if (drag)
            drag->deleteLater();
        if (self) {
            self->m_dragActive = false;
            emit self->dragActiveChanged();
        }
        return int(Qt::IgnoreAction);
    }
    const Qt::DropAction result = drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction);
    if (drag)
        drag->deleteLater();
    if (self) {
        self->m_dragActive = false;
        emit self->dragActiveChanged();
    }
    // A native file receiver performs the move itself; never delete source files here.
    return int(result);
}
