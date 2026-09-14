#include "NativeServices.h"
#include "PreviewController.h"
#include "ShellIconProvider.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtEndian>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

class PlatformTests final : public QObject
{
    Q_OBJECT

private:
    QString m_directory;

    QUrl writeFile(const QString &name, const QByteArray &bytes)
    {
        const QString path = m_directory + QDir::separator() + name;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return {};
        return QUrl::fromLocalFile(path);
    }

private slots:
    void initTestCase()
    {
        m_directory = QDir::current().absoluteFilePath(
            QStringLiteral("platform-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(m_directory));
    }

    void cleanupTestCase()
    {
        QVERIFY(QDir(m_directory).removeRecursively());
    }

    void textPreview()
    {
        const QUrl url = writeFile(QStringLiteral("unicode.txt"), QString::fromUtf8("Hello 世界\n").toUtf8());
        QVERIFY(!url.isEmpty());
        PreviewController preview;
        preview.preview(url);
        QTRY_COMPARE(preview.kind(), QStringLiteral("text"));
        QCOMPARE(preview.text(), QString::fromUtf8("Hello 世界\n"));
        QCOMPARE(preview.title(), QStringLiteral("unicode.txt"));
        QVERIFY(preview.details().contains(QStringLiteral("bytes")));
    }

    void boundedTextAndBinary()
    {
        PreviewController preview;
        const QUrl large = writeFile(QStringLiteral("large.txt"), QByteArray(1024 * 1024 + 50, 'a'));
        QVERIFY(!large.isEmpty());
        preview.preview(large);
        QTRY_COMPARE(preview.kind(), QStringLiteral("text"));
        QCOMPARE(preview.text().size(), 1024 * 1024);
        QVERIFY(preview.details().contains(QStringLiteral("limited")));
        const QUrl binary = writeFile(QStringLiteral("binary.txt"), QByteArray("abc\0def", 7));
        preview.preview(binary);
        QTRY_COMPARE(preview.kind(), QStringLiteral("info"));
        QVERIFY(preview.text().isEmpty());
    }

    void activeContentIsPlainText()
    {
        PreviewController preview;
        const QByteArray html("<html><script>alert('not run')</script></html>");
        preview.preview(writeFile(QStringLiteral("active.html"), html));
        QTRY_COMPARE(preview.kind(), QStringLiteral("text"));
        QCOMPARE(preview.text(), QString::fromUtf8(html));
        preview.preview(writeFile(QStringLiteral("vector.svg"), QByteArray("<svg xmlns='http://www.w3.org/2000/svg'/>")));
        QTRY_COMPARE(preview.kind(), QStringLiteral("text"));
    }

    void utf16Preview()
    {
        PreviewController preview;
        QByteArray bytes = QByteArray::fromHex("fffe");
        const QString original = QString::fromUtf8("Hello 世界\n");
        for (const QChar character : original) {
            char encoded[2];
            qToLittleEndian<quint16>(character.unicode(), encoded);
            bytes.append(encoded, 2);
        }
        preview.preview(writeFile(QStringLiteral("utf16.txt"), bytes));
        QTRY_COMPARE(preview.kind(), QStringLiteral("text"));
        QCOMPARE(preview.text(), original);
    }

    void rejectsObsoleteResultsAndPins()
    {
        PreviewController preview;
        const QUrl first = writeFile(QStringLiteral("first.txt"), QByteArray(1024 * 1024, 'a'));
        const QUrl second = writeFile(QStringLiteral("second.txt"), QByteArray("latest"));
        preview.preview(first);
        preview.preview(second);
        QTRY_COMPARE(preview.text(), QStringLiteral("latest"));
        QTest::qWait(100);
        QCOMPARE(preview.url(), second);
        QCOMPARE(preview.text(), QStringLiteral("latest"));
        preview.setPinned(true);
        preview.preview(first);
        QCOMPARE(preview.url(), second);
        preview.setPinned(false);
        preview.preview(first);
        preview.preview({});
        QTest::qWait(100);
        QCOMPARE(preview.kind(), QStringLiteral("none"));
        QVERIFY(preview.text().isEmpty());
    }

    void destroyingPendingPreviewIsSafe()
    {
        auto *preview = new PreviewController;
        preview->preview(writeFile(QStringLiteral("pending.txt"), QByteArray(1024 * 1024, 'x')));
        delete preview;
        QTest::qWait(50);
    }

    void imageAndErrors()
    {
        PreviewController preview;
        const QString path = m_directory + QDir::separator() + QStringLiteral("small.png");
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        preview.preview(QUrl::fromLocalFile(path));
        QTRY_COMPARE(preview.kind(), QStringLiteral("image"));
        const QString widePath = m_directory + QDir::separator() + QStringLiteral("wide.png");
        QImage wide(8193, 1, QImage::Format_RGB32);
        wide.fill(Qt::blue);
        QVERIFY(wide.save(widePath));
        preview.preview(QUrl::fromLocalFile(widePath));
        QTRY_COMPARE(preview.kind(), QStringLiteral("error"));
        QVERIFY(preview.error().contains(QStringLiteral("dimensions")));
        preview.preview(writeFile(QStringLiteral("fake.png"), QByteArray("<svg/>")));
        QTRY_COMPARE(preview.kind(), QStringLiteral("error"));
        preview.preview(QUrl(QStringLiteral("https://example.invalid/file.txt")));
        QCOMPARE(preview.kind(), QStringLiteral("error"));
        QVERIFY(!preview.error().isEmpty());
        preview.preview(QUrl::fromLocalFile(m_directory + QDir::separator() + QStringLiteral("missing.txt")));
        QTRY_COMPARE(preview.kind(), QStringLiteral("error"));
        preview.preview(QUrl::fromLocalFile(m_directory));
        QTRY_COMPARE(preview.kind(), QStringLiteral("info"));
        QVERIFY(preview.details().contains(QStringLiteral("Folder")));
    }

    void clipboardRoundTrip()
    {
        auto *saved = new QMimeData;
        if (const QMimeData *previous = QGuiApplication::clipboard()->mimeData()) {
            for (const QString &format : previous->formats())
                saved->setData(format, previous->data(format));
        }
        const auto restoreClipboard = qScopeGuard([saved] {
            QGuiApplication::clipboard()->setMimeData(saved);
        });
        NativeServices services;
        const QUrl first = writeFile(QStringLiteral("clipboard-a.txt"), QByteArray("a"));
        const QUrl second = writeFile(QStringLiteral("clipboard-b.txt"), QByteArray("b"));
        const QVariantList urls{first, second};
        services.setClipboard(urls, true);
        QCOMPARE(services.clipboardFiles().value(QStringLiteral("urls")).toList(), urls);
        QVERIFY(services.clipboardFiles().value(QStringLiteral("cut")).toBool());
        const QByteArray effect = QGuiApplication::clipboard()->mimeData()->data(
            QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\""));
        QCOMPARE(effect.size(), 4);
        QCOMPARE(qFromLittleEndian<quint32>(effect.constData()), 2u);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            QVERIFY(IsClipboardFormatAvailable(CF_HDROP));
            QVERIFY(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"Preferred DropEffect")));
            QVERIFY(OpenClipboard(nullptr));
            const auto closeClipboard = qScopeGuard([] { CloseClipboard(); });
            const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            QVERIFY(drop != nullptr);
            QCOMPARE(DragQueryFileW(drop, 0xffffffff, nullptr, 0), 2u);
        }
#endif
        services.clearCutClipboard({first});
        QCOMPARE(services.clipboardFiles().value(QStringLiteral("urls")).toList(), urls);
        services.clearCutClipboard({second, first});
        QVERIFY(services.clipboardFiles().value(QStringLiteral("urls")).toList().isEmpty());
        services.setClipboard(urls, false);
        services.clearCutClipboard(urls);
        QCOMPARE(services.clipboardFiles().value(QStringLiteral("urls")).toList(), urls);
        QVERIFY(!services.clipboardFiles().value(QStringLiteral("cut")).toBool());
        services.setClipboard({first}, true);
        services.clearCutClipboard(urls);
        QCOMPARE(services.clipboardFiles().value(QStringLiteral("urls")).toList(), QVariantList{first});
    }

    void invalidNativeInputs()
    {
        NativeServices services;
        QSignalSpy errors(&services, &NativeServices::errorOccurred);
        QSignalSpy dragChanges(&services, &NativeServices::dragActiveChanged);
        QVERIFY(!services.ownsDrag());
        QCOMPARE(services.property("dragActive").toBool(), false);
        services.open(QUrl(QStringLiteral("https://example.invalid")));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(services.startDrag({}), int(Qt::IgnoreAction));
        QCOMPARE(services.startDrag({QUrl(QStringLiteral("https://example.invalid"))}), int(Qt::IgnoreAction));
        QCOMPARE(errors.size(), 2);
        services.contextMenu({QUrl::fromLocalFile(m_directory + QStringLiteral("\\a.txt")),
                              QUrl::fromLocalFile(m_directory + QStringLiteral("\\other\\b.txt"))}, 0, 0);
        QCOMPARE(errors.size(), 3);
        QVERIFY(!services.ownsDrag());
        QCOMPARE(dragChanges.size(), 0);
    }

    void iconsAreBoundedAndCached()
    {
        ShellIconProvider provider;
        const QUrl url = writeFile(QStringLiteral("icon # 你好.txt"), QByteArray("icon"));
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(url.toLocalFile()));
        QSize actual;
        const QImage first = provider.requestImage(id, &actual, QSize(32, 32));
        QVERIFY(!first.isNull());
        QCOMPARE(actual, QSize(32, 32));
        QCOMPARE(provider.requestImage(id, nullptr, QSize(32, 32)), first);
        const QImage gridFallback = provider.requestImage(id, &actual, QSize(48, 48));
        QVERIFY(!gridFallback.isNull());
        QCOMPARE(gridFallback.size(), QSize(48, 48));
        const QImage large = provider.requestImage(id, &actual, QSize(10000, 10000));
        QCOMPARE(large.size(), QSize(256, 256));
    }

    void gridThumbnail()
    {
        const QString path = m_directory + QDir::separator() + QStringLiteral("thumbnail.png");
        QImage source(80, 40, QImage::Format_RGB32);
        source.fill(Qt::green);
        QVERIFY(source.save(path));
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(path));
        ShellIconProvider provider;
        QSize actual;
        const QImage image = provider.requestImage(id, &actual, QSize(48, 48));
        QVERIFY(!image.isNull());
        QCOMPARE(actual, QSize(48, 48));
        QCOMPARE(image.size(), actual);
        QCOMPARE(provider.requestImage(id, nullptr, QSize(48, 48)), image);
    }
};

QTEST_MAIN(PlatformTests)
#include "platformtests.moc"
