#include "ShellIconProvider.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPainter>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

namespace {
QImage fallbackIcon(const QString &path, QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / 32.0, size.height() / 32.0);
    painter.setPen(Qt::NoPen);
    if (QFileInfo(path).isDir()) {
        painter.setBrush(QColor(216, 158, 49));
        painter.drawRoundedRect(QRectF(3, 7, 13, 9), 2, 2);
        painter.setBrush(QColor(242, 191, 74));
        painter.drawRoundedRect(QRectF(3, 11, 26, 16), 2, 2);
    } else {
        painter.setBrush(QColor(224, 231, 239));
        painter.drawRoundedRect(QRectF(7, 3, 19, 26), 2, 2);
        painter.setPen(QPen(QColor(129, 147, 168), 1.6));
        for (int y : {13, 18, 23})
            painter.drawLine(QPointF(11, y), QPointF(22, y));
    }
    return image;
}

#ifdef Q_OS_WIN
QImage shellThumbnail(const QString &nativePath, QSize size)
{
    IShellItemImageFactory *factory = nullptr;
    const HRESULT created = SHCreateItemFromParsingName(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()), nullptr, IID_IShellItemImageFactory,
        reinterpret_cast<void **>(&factory));
    if (FAILED(created))
        return {};
    HBITMAP bitmap = nullptr;
    const HRESULT extracted = factory->GetImage(SIZE{size.width(), size.height()},
                                                SIIGBF_THUMBNAILONLY, &bitmap);
    factory->Release();
    QImage thumbnail;
    if (SUCCEEDED(extracted) && bitmap) {
        BITMAP metadata{};
        if (GetObjectW(bitmap, sizeof(metadata), &metadata)
            && metadata.bmWidth > 0 && metadata.bmWidth <= 256
            && metadata.bmHeight > 0 && metadata.bmHeight <= 256)
            thumbnail = QImage::fromHBITMAP(bitmap);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (thumbnail.isNull())
        return {};
    thumbnail = thumbnail.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(QPoint((size.width() - thumbnail.width()) / 2,
                             (size.height() - thumbnail.height()) / 2), thumbnail);
    return image;
}

QImage drawIcon(HICON icon, QSize size, QRgb background)
{
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size.width();
    bitmapInfo.bmiHeader.biHeight = -size.height();
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    const HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return {};
    const HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        return {};
    }
    const HGDIOBJ previous = SelectObject(dc, bitmap);
    auto *data = static_cast<QRgb *>(pixels);
    std::fill_n(data, size.width() * size.height(), background);
    const bool drawn = DrawIconEx(dc, 0, 0, icon, size.width(), size.height(), 0, nullptr, DI_NORMAL);
    GdiFlush();
    QImage image;
    if (drawn)
        image = QImage(static_cast<const uchar *>(pixels), size.width(), size.height(),
                       size.width() * 4, QImage::Format_RGB32).copy();
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return image;
}

QImage shellIcon(const QString &path, QSize size)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SHFILEINFOW info{};
    const QString nativePath = QDir::toNativeSeparators(path);
    if (size.width() >= 48 && size.height() >= 48) {
        QImage thumbnail = shellThumbnail(nativePath, size);
        if (!thumbnail.isNull()) {
            if (SUCCEEDED(initialized))
                CoUninitialize();
            return thumbnail;
        }
    }
    const auto result = SHGetFileInfoW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), 0,
                                      &info, sizeof(info), SHGFI_ICON
                                          | (size.width() <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON));
    QImage image;
    if (result && info.hIcon) {
        const QImage black = drawIcon(info.hIcon, size, qRgb(0, 0, 0));
        const QImage white = drawIcon(info.hIcon, size, qRgb(255, 255, 255));
        if (!black.isNull() && !white.isNull()) {
            image = QImage(size, QImage::Format_ARGB32_Premultiplied);
            // Recover transparency for both old mask-based icons and modern alpha icons.
            for (int y = 0; y < size.height(); ++y) {
                const auto *dark = reinterpret_cast<const QRgb *>(black.constScanLine(y));
                const auto *light = reinterpret_cast<const QRgb *>(white.constScanLine(y));
                auto *output = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < size.width(); ++x) {
                    const int delta = std::max({qRed(light[x]) - qRed(dark[x]),
                                                qGreen(light[x]) - qGreen(dark[x]),
                                                qBlue(light[x]) - qBlue(dark[x])});
                    const int alpha = std::clamp(255 - delta, 0, 255);
                    output[x] = qRgba(std::min(qRed(dark[x]), alpha),
                                      std::min(qGreen(dark[x]), alpha),
                                      std::min(qBlue(dark[x]), alpha), alpha);
                }
            }
        }
        DestroyIcon(info.hIcon);
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return image;
}
#endif
} // namespace

ShellIconProvider::ShellIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading),
      m_cache(8192)
{
}

QImage ShellIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int width = std::clamp(requestedSize.width() > 0 ? requestedSize.width() : 32, 8, 256);
    const int height = std::clamp(requestedSize.height() > 0 ? requestedSize.height() : width, 8, 256);
    const QSize dimensions(width, height);
    if (size)
        *size = dimensions;
    QString path = QUrl::fromPercentEncoding(id.toUtf8());
    if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null))
        path.clear();
    const QString key = path + QLatin1Char('|') + QString::number(width) + QLatin1Char('x') + QString::number(height);
    {
        QMutexLocker lock(&m_mutex);
        if (const QImage *cached = m_cache.object(key))
            return *cached;
    }
    QImage image;
#ifdef Q_OS_WIN
    if (!path.isEmpty())
        image = shellIcon(path, dimensions);
#endif
    if (image.isNull())
        image = fallbackIcon(path, dimensions);
    {
        QMutexLocker lock(&m_mutex);
        m_cache.insert(key, new QImage(image), int(std::max<qsizetype>(1, image.sizeInBytes() / 1024)));
    }
    return image;
}
