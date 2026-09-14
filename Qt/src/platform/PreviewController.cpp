#include "PreviewController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QSet>
#include <QStringDecoder>
#include <QThreadPool>
#include <QtConcurrentRun>

namespace {
constexpr qint64 textLimit = 1024 * 1024;
constexpr qint64 imageByteLimit = 64 * 1024 * 1024;
constexpr qint64 imagePixelLimit = 16 * 1024 * 1024;
constexpr int imageDimensionLimit = 8192;

struct PreviewResult {
    QString kind = QStringLiteral("info");
    QString text;
    QString title;
    QString details;
    QString error;
};

PreviewResult failed(PreviewResult result, const QString &message)
{
    result.kind = QStringLiteral("error");
    result.error = message;
    return result;
}

QThreadPool &previewPool()
{
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return pool;
}

PreviewResult loadPreview(const QUrl &url, const std::shared_ptr<std::atomic_bool> &cancelled)
{
    PreviewResult result;
    if (cancelled->load())
        return result;
    const QFileInfo info(url.toLocalFile());
    result.title = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
    if (!info.exists())
        return failed(result, QStringLiteral("The item no longer exists or cannot be accessed."));

    result.details = QStringLiteral("%1\n%2\nModified: %3")
                         .arg(QDir::toNativeSeparators(info.absoluteFilePath()),
                              info.isDir() ? QStringLiteral("Folder")
                                           : QStringLiteral("%1 bytes").arg(info.size()),
                              info.lastModified().toString(Qt::ISODate));
    // Devices, sockets and directories must never enter the file-reading path.
    if (!info.isFile() || cancelled->load())
        return result;

    static const QSet<QString> images{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("ico")
    };
    const QString extension = info.suffix().toLower();
    if (images.contains(extension)) {
        if (info.size() > imageByteLimit)
            return failed(result, QStringLiteral("Image is too large to preview (64 MiB limit)."));
        QImageReader reader(info.absoluteFilePath());
        const QByteArray format = reader.format().toLower();
        static const QSet<QByteArray> formats{"png", "jpeg", "jpg", "bmp", "gif", "webp", "ico"};
        if (!formats.contains(format) || !reader.canRead())
            return failed(result, QStringLiteral("Unsupported or damaged image."));
        const QSize dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.isEmpty()
            || dimensions.width() > imageDimensionLimit || dimensions.height() > imageDimensionLimit
            || qint64(dimensions.width()) * dimensions.height() > imagePixelLimit)
            return failed(result, QStringLiteral("Image dimensions exceed the safe preview limit."));
        reader.setAutoTransform(true);
        if (cancelled->load())
            return result;
        if (reader.read().isNull())
            return failed(result, QStringLiteral("Image could not be decoded safely."));
        result.kind = QStringLiteral("image");
        result.details += QStringLiteral("\n%1 × %2 pixels").arg(dimensions.width()).arg(dimensions.height());
        return result;
    }

    static const QSet<QString> textExtensions{
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("log"),
        QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("json"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("ini"), QStringLiteral("conf"), QStringLiteral("cfg"),
        QStringLiteral("toml"), QStringLiteral("cpp"), QStringLiteral("c"),
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("cs"),
        QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"),
        QStringLiteral("tsx"), QStringLiteral("jsx"), QStringLiteral("css"),
        QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("svg"),
        QStringLiteral("qml"), QStringLiteral("cmake"), QStringLiteral("rs"),
        QStringLiteral("go"), QStringLiteral("java"), QStringLiteral("sh"),
        QStringLiteral("bat"), QStringLiteral("ps1"), QStringLiteral("sql"),
        QStringLiteral("gitignore"), QStringLiteral("editorconfig")
    };
    if (!textExtensions.contains(extension) && !info.suffix().isEmpty())
        return result;

    QFile file(info.absoluteFilePath());
    if (cancelled->load())
        return result;
    if (!file.open(QIODevice::ReadOnly))
        return failed(result, file.errorString());
    QByteArray bytes = file.read(textLimit + 1);
    if (file.error() != QFileDevice::NoError)
        return failed(result, file.errorString());
    const bool truncated = bytes.size() > textLimit;
    bytes.truncate(textLimit);
    QStringDecoder::Encoding encoding = QStringDecoder::Utf8;
    if (bytes.startsWith("\xFF\xFE"))
        encoding = QStringDecoder::Utf16LE;
    else if (bytes.startsWith("\xFE\xFF"))
        encoding = QStringDecoder::Utf16BE;
    else if (bytes.contains('\0'))
        return result;

    QStringDecoder decoder(encoding);
    QString text = decoder(bytes);
    // A truncated final code point is held by the streaming decoder, not replaced.
    if (decoder.hasError())
        return result;
    for (const QChar character : text) {
        if (character.unicode() < 32 && character != QLatin1Char('\t')
            && character != QLatin1Char('\r') && character != QLatin1Char('\n'))
            return result;
    }
    result.kind = QStringLiteral("text");
    result.text = std::move(text);
    if (truncated)
        result.details += QStringLiteral("\nPreview limited to the first 1 MiB.");
    return result;
}
} // namespace

PreviewController::PreviewController(QObject *parent) : QObject(parent) {}

PreviewController::~PreviewController()
{
    if (m_cancelled)
        m_cancelled->store(true);
}

void PreviewController::setPinned(bool pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    emit pinnedChanged();
}

void PreviewController::preview(QUrl url)
{
    if (m_pinned)
        return;
    if (m_cancelled)
        m_cancelled->store(true);
    const quint64 generation = ++m_generation;
    m_url = url;
    m_kind = QStringLiteral("none");
    m_text.clear();
    m_title.clear();
    m_details.clear();
    m_error.clear();
    if (url.isEmpty()) {
        emit previewChanged();
        return;
    }
    if (!url.isLocalFile()) {
        m_kind = QStringLiteral("error");
        m_error = QStringLiteral("Only local files can be previewed.");
        emit previewChanged();
        return;
    }
    emit previewChanged();
    auto *watcher = new QFutureWatcher<PreviewResult>(this);
    connect(watcher, &QFutureWatcher<PreviewResult>::finished, this, [this, watcher, generation] {
        const PreviewResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_kind = result.kind;
        m_text = result.text;
        m_title = result.title;
        m_details = result.details;
        m_error = result.error;
        emit previewChanged();
    });
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    watcher->setFuture(QtConcurrent::run(&previewPool(), [url, cancelled = m_cancelled] {
        return loadPreview(url, cancelled);
    }));
}
