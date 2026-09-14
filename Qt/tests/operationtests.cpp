#include "TaskManager.h"
#include "FileOperationWorker.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <QtTest>
#include <miniz.h>
#include <cstring>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

class OperationTests final : public QObject {
    Q_OBJECT
private:
    static QString fixtureTemplate() {
        return QDir::current().absoluteFilePath(QStringLiteral("operationtests-XXXXXX"));
    }
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(contents), contents.size());
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static QVariant role(TaskManager &manager, const QByteArray &name, int row = 0) {
        const auto roles = manager.model()->roleNames();
        return manager.model()->data(manager.model()->index(row, 0), roles.key(name));
    }
    static QByteArray fileIdentity(const QString &path) {
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(path);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return {};
        BY_HANDLE_FILE_INFORMATION information{};
        const bool success = GetFileInformationByHandle(handle, &information);
        CloseHandle(handle);
        if (!success)
            return {};
        return QByteArray::number(information.dwVolumeSerialNumber) + ":"
            + QByteArray::number(information.nFileIndexHigh) + ":"
            + QByteArray::number(information.nFileIndexLow);
#else
        struct stat information{};
        if (::stat(QFile::encodeName(path).constData(), &information))
            return {};
        return QByteArray::number(quint64(information.st_dev)) + ":"
            + QByteArray::number(quint64(information.st_ino));
#endif
    }
    static void setModified(const QString &path, const QDateTime &modified) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(path).lastModified(), modified);
    }
    static void zip(const QString &path, const QList<QPair<QByteArray, QByteArray>> &entries,
                    quint32 attributes = 0) {
        mz_zip_archive archive{};
        QVERIFY(mz_zip_writer_init_heap(&archive, 0, 0));
        for (const auto &entry : entries) {
            QByteArray name = entry.first;
            if (name.startsWith('/'))
                name[0] = '_';
            if (!mz_zip_writer_add_mem(&archive, name.constData(), entry.second.constData(),
                                      static_cast<size_t>(entry.second.size()), MZ_DEFAULT_COMPRESSION)) {
                mz_zip_writer_end(&archive);
                QFAIL("Cannot add ZIP fixture entry");
            }
        }
        void *memory = nullptr;
        size_t length = 0;
        const bool finalized = mz_zip_writer_finalize_heap_archive(&archive, &memory, &length);
        mz_zip_writer_end(&archive);
        QVERIFY(finalized);
        QByteArray bytes(static_cast<const char *>(memory), static_cast<qsizetype>(length));
        mz_free(memory);
        for (const auto &entry : entries) {
            if (entry.first.startsWith('/')) {
                QByteArray safeName = entry.first;
                safeName[0] = '_';
                bytes.replace(safeName, entry.first);
            }
        }
        if (attributes) {
            const auto central = bytes.indexOf(QByteArray("PK\x01\x02", 4));
            QVERIFY(central >= 0);
            qToLittleEndian(attributes, bytes.data() + central + 38);
        }
        write(path, bytes);
    }
    static bool linkDirectory(const QString &link, const QString &target) {
#ifdef Q_OS_WIN
        const auto nativeLink = QDir::toNativeSeparators(link);
        const auto nativeTarget = QDir::toNativeSeparators(target);
        return CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
            reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
            SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
#else
        return ::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()) == 0;
#endif
    }

private slots:
    void copyDirectoryAndModelThread() {
        QTemporaryDir fixture(fixtureTemplate());
        QVERIFY(fixture.isValid());
        QDir root(fixture.path());
        QVERIFY(root.mkpath("source/nested"));
        QVERIFY(root.mkdir("destination"));
        write(root.filePath("source/nested/file.txt"), "content");
        TaskManager manager;
        bool wrongThread = false;
        connect(manager.model(), &QAbstractItemModel::dataChanged, &manager,
            [&] { wrongThread |= QThread::currentThread() != manager.thread(); }, Qt::DirectConnection);
        QSignalSpy finished(&manager, &TaskManager::taskFinished);
        QVERIFY(!manager.submit("copy", {QUrl::fromLocalFile(root.filePath("source"))},
                                QUrl::fromLocalFile(root.filePath("destination"))).isEmpty());
        QVERIFY(manager.busy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!manager.busy());
        QCOMPARE(role(manager, "status").toString(), "completed");
        QCOMPARE(role(manager, "progress").toDouble(), 1.0);
        QCOMPARE(role(manager, "processedBytes").toULongLong(), quint64(7));
        QCOMPARE(role(manager, "totalBytes").toULongLong(), quint64(7));
        QCOMPARE(role(manager, "bytesPerSecond").toDouble(), 0.0);
        QVERIFY(!role(manager, "cancellable").toBool());
        QCOMPARE(read(root.filePath("destination/source/nested/file.txt")), "content");
        QCOMPARE(QFileInfo(root.filePath("destination/source")).lastModified(),
                 QFileInfo(root.filePath("source")).lastModified());
        QVERIFY(!wrongThread);
        manager.clearFinished();
        QCOMPARE(manager.model()->rowCount(), 0);
    }
    void nativeMovePreservesIdentityAndTime_data() {
        QTest::addColumn<bool>("directory");
        QTest::newRow("file") << false;
        QTest::newRow("whole-directory") << true;
    }
    void nativeMovePreservesIdentityAndTime() {
        QFETCH(bool, directory);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        if (directory)
            QVERIFY(root.mkdir("source"));
        const QString source = root.filePath("source");
        const QString sourceFile = directory ? root.filePath("source/payload") : source;
        const QByteArray payload("native move");
        write(sourceFile, payload);
        const QDateTime modified = QDateTime::fromString("2001-02-03T04:05:06Z", Qt::ISODate);
        setModified(sourceFile, modified);
        const auto sourceTime = QFileInfo(source).lastModified();
        const auto originalId = fileIdentity(source);
        const auto originalFileId = fileIdentity(sourceFile);
        QVERIFY(!originalId.isEmpty());
        QVERIFY(!originalFileId.isEmpty());
        TaskManager manager;
        manager.submit("move", {source}, QUrl::fromLocalFile(root.filePath("destination")));
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "completed");
        const QString target = root.filePath("destination/source");
        const QString targetFile = directory ? root.filePath("destination/source/payload") : target;
        QVERIFY(!QFileInfo::exists(source));
        QCOMPARE(fileIdentity(target), originalId);
        QCOMPARE(fileIdentity(targetFile), originalFileId);
        QCOMPARE(QFileInfo(target).lastModified(), sourceTime);
        QCOMPARE(QFileInfo(targetFile).lastModified(), modified);
        QCOMPARE(read(targetFile), payload);
        QCOMPARE(role(manager, "processedBytes").toULongLong(), quint64(payload.size()));
        QCOMPARE(role(manager, "totalBytes").toULongLong(), quint64(payload.size()));
        QCOMPARE(role(manager, "bytesPerSecond").toDouble(), 0.0);
    }
    void copyFallbackPreservesTime_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("copy") << QString("copy");
        QTest::newRow("move-overwrite") << QString("move");
    }
    void copyFallbackPreservesTime() {
        QFETCH(QString, operation);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        const QString source = root.filePath("source/a");
        const QString target = root.filePath("destination/a");
        write(source, "new content");
        write(target, "old content");
        const QDateTime modified = QDateTime::fromString("2001-02-03T04:05:06Z", Qt::ISODate);
        setModified(source, modified);
        TaskManager manager;
        manager.submit(operation, {source}, QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "completed");
        QCOMPARE(QFileInfo(target).lastModified(), modified);
        QCOMPARE(read(target), "new content");
        QCOMPARE(QFileInfo::exists(source), operation == "copy");
    }
    void collisionPolicies_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("policy");
        for (const auto &operation : {QString("copy"), QString("move")})
            for (const auto &policy : {QString("overwrite"), QString("skip"), QString("rename")})
                QTest::newRow(qPrintable(operation + "-" + policy)) << operation << policy;
    }
    void collisionPolicies() {
        QFETCH(QString, operation);
        QFETCH(QString, policy);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        const QString source = root.filePath("source/a.txt");
        write(source, "new");
        write(root.filePath("destination/a.txt"), "old");
        TaskManager manager;
        QSignalSpy successfulMoves(&manager, &TaskManager::successfulMove);
        const auto id = manager.submit(operation, {source}, QUrl::fromLocalFile(root.filePath("destination")), policy);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), policy == "skip" ? "partial" : "completed");
        QCOMPARE(read(root.filePath("destination/a.txt")), policy == "overwrite" ? "new" : "old");
        if (policy == "rename")
            QCOMPARE(read(root.filePath("destination/a (1).txt")), "new");
        QCOMPARE(QFileInfo::exists(source), operation == "copy" || policy == "skip");
        const bool fullyMoved = operation == "move" && policy != "skip";
        QCOMPARE(successfulMoves.size(), fullyMoved ? 1 : 0);
        if (fullyMoved) {
            QCOMPARE(successfulMoves.first().at(0).toString(), id);
            QCOMPARE(successfulMoves.first().at(1).toList(), QVariantList{QUrl::fromLocalFile(source)});
        }
    }
    void askApplyToAll() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        QVariantList sources;
        for (const auto &name : {QString("a"), QString("b")}) {
            write(root.filePath("source/" + name), "new");
            write(root.filePath("destination/" + name), "old");
            sources << root.filePath("source/" + name);
        }
        TaskManager manager;
        QSignalSpy conflicts(&manager, &TaskManager::conflict);
        connect(&manager, &TaskManager::conflict, &manager,
            [&](const QString &id, const QString &, const QString &) {
                manager.resolveConflict(id, "overwrite", true);
            });
        manager.submit("copy", sources, QUrl::fromLocalFile(root.filePath("destination")));
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(read(root.filePath("destination/a")), "new");
        QCOMPARE(read(root.filePath("destination/b")), "new");
    }
    void rejectSelfAndDescendant_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<bool>("descendant");
        QTest::newRow("copy-self") << QString("copy") << false;
        QTest::newRow("move-self") << QString("move") << false;
        QTest::newRow("copy-descendant") << QString("copy") << true;
        QTest::newRow("move-descendant") << QString("move") << true;
    }
    void rejectSelfAndDescendant() {
        QFETCH(QString, operation);
        QFETCH(bool, descendant);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkpath("source/child"));
        write(root.filePath("source/a"), "preserved");
        TaskManager manager;
        manager.submit(operation, {root.filePath("source")},
            QUrl::fromLocalFile(descendant ? root.filePath("source/child") : root.path()), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QCOMPARE(read(root.filePath("source/a")), "preserved");
        QVERIFY(!QFileInfo::exists(root.filePath("source/child/source")));
    }
    void rejectHardlinkAlias() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        const QString source = root.filePath("source/a");
        const QString destination = root.filePath("destination/a");
        write(source, "preserved");
#ifdef Q_OS_WIN
        const auto nativeSource = QDir::toNativeSeparators(source);
        const auto nativeDestination = QDir::toNativeSeparators(destination);
        if (!CreateHardLinkW(reinterpret_cast<LPCWSTR>(nativeDestination.utf16()),
                             reinterpret_cast<LPCWSTR>(nativeSource.utf16()), nullptr))
            QSKIP("Hard links are not available on this filesystem");
#else
        if (::link(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()))
            QSKIP("Hard links are not available on this filesystem");
#endif
        TaskManager manager;
        manager.submit("move", {source}, QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QCOMPARE(read(source), "preserved");
        QCOMPARE(read(destination), "preserved");
    }
    void cancelWaitingAndQueued() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        write(root.filePath("source/a"), "new");
        write(root.filePath("destination/a"), "old");
        TaskManager manager;
        QSignalSpy successfulMoves(&manager, &TaskManager::successfulMove);
        QSignalSpy conflicts(&manager, &TaskManager::conflict);
        const QString first = manager.submit("move", {root.filePath("source/a")},
            QUrl::fromLocalFile(root.filePath("destination")));
        QTRY_COMPARE_WITH_TIMEOUT(conflicts.size(), 1, 10000);
        QCOMPARE(role(manager, "status").toString(), "waiting");
        QCOMPARE(role(manager, "bytesPerSecond").toDouble(), 0.0);
        QCOMPARE(role(manager, "processedBytes").toULongLong(), quint64(0));
        QCOMPARE(role(manager, "totalBytes").toULongLong(), quint64(3));
        const QString second = manager.submit("delete", {root.filePath("source/a")});
        manager.cancel(second);
        manager.cancel(first);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status", 0).toString(), "cancelled");
        QCOMPARE(role(manager, "status", 1).toString(), "cancelled");
        QCOMPARE(successfulMoves.size(), 0);
        QCOMPARE(read(root.filePath("source/a")), "new");
        QCOMPARE(read(root.filePath("destination/a")), "old");
        QCOMPARE(QDir(root.filePath("destination")).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
    void destructorUnblocksConflict() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        write(root.filePath("source/a"), "new");
        write(root.filePath("destination/a"), "old");
        auto manager = std::make_unique<TaskManager>();
        QSignalSpy conflicts(manager.get(), &TaskManager::conflict);
        manager->submit("copy", {root.filePath("source/a")}, QUrl::fromLocalFile(root.filePath("destination")));
        QTRY_COMPARE_WITH_TIMEOUT(conflicts.size(), 1, 10000);
        QElapsedTimer timer;
        timer.start();
        manager.reset();
        QVERIFY(timer.elapsed() < 5000);
        QCOMPARE(read(root.filePath("destination/a")), "old");
    }
    void cancelDuringChunk_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("copy") << QString("copy");
        QTest::newRow("move") << QString("move");
        QTest::newRow("extract") << QString("extract");
    }
    void cancelDuringChunk() {
        QFETCH(QString, operation);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkdir("destination"));
        const QByteArray payload(4 * 1024 * 1024, 'a');
        const QString source = root.filePath("source/a");
        write(source, payload);
        write(root.filePath("destination/a"), "preserved");
        if (operation == "extract")
            zip(root.filePath("input.zip"), {{"a", payload}});
        FileOperations::Job job;
        job.operation = operation;
        job.policy = "overwrite";
        job.sources = {operation == "extract" ? root.filePath("input.zip") : source};
        job.destination = root.filePath("destination");
        bool receivedBytes = false;
        bool validByteProgress = true;
        const auto result = FileOperations::run(job, [&](const FileOperations::Update &update) {
            if (update.progress > 0) {
                receivedBytes = true;
                validByteProgress &= update.processedBytes > 0
                    && update.processedBytes <= update.totalBytes
                    && update.totalBytes == quint64(payload.size()) && update.bytesPerSecond > 0;
                job.cancelled = true;
            }
        }, [](const QString &, const QString &) {});
        QVERIFY(receivedBytes);
        QVERIFY(validByteProgress);
        QCOMPARE(result.status, "cancelled");
        QCOMPARE(result.bytesPerSecond, 0.0);
        QVERIFY(result.processedBytes > 0);
        QCOMPARE(result.totalBytes, quint64(payload.size()));
        QCOMPARE(read(root.filePath("destination/a")), "preserved");
        QCOMPARE(read(source), payload);
        QCOMPARE(QDir(root.filePath("destination")).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
    void failedMovePreservesSource() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("source"));
        QVERIFY(root.mkpath("destination/a"));
        write(root.filePath("source/a"), "source");
        write(root.filePath("destination/a/keep"), "preserved");
        TaskManager manager;
        manager.submit("move", {root.filePath("source/a")},
            QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QCOMPARE(read(root.filePath("source/a")), "source");
        QCOMPARE(read(root.filePath("destination/a/keep")), "preserved");
    }
    void extractAndRenameCollision() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        zip(root.filePath("input.zip"), {{"Folder/a.txt", "from ZIP"}, {"empty.txt", ""}});
        TaskManager manager;
        const QUrl destination = QUrl::fromLocalFile(root.filePath("destination"));
        manager.submit("extract", {root.filePath("input.zip")}, destination);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "completed");
        QCOMPARE(read(root.filePath("destination/Folder/a.txt")), "from ZIP");
        QVERIFY(QFileInfo::exists(root.filePath("destination/empty.txt")));
        manager.submit("extract", {root.filePath("input.zip")}, destination, "rename");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status", 1).toString(), "completed");
        QCOMPARE(read(root.filePath("destination/Folder/a (1).txt")), "from ZIP");
    }
    void unsafeArchive_data() {
        QTest::addColumn<QByteArray>("name");
        QTest::addColumn<quint32>("attributes");
        QTest::newRow("parent") << QByteArray("../escape") << quint32(0);
        QTest::newRow("nested-parent") << QByteArray("a/../../escape") << quint32(0);
        QTest::newRow("backslash-parent") << QByteArray("a\\..\\..\\escape") << quint32(0);
        QTest::newRow("absolute") << QByteArray("/escape") << quint32(0);
        QTest::newRow("drive") << QByteArray("C:/escape") << quint32(0);
        QTest::newRow("unc") << QByteArray("\\\\server\\share\\escape") << quint32(0);
        QTest::newRow("ads") << QByteArray("file:stream") << quint32(0);
        QTest::newRow("reserved") << QByteArray("folder/NUL.txt") << quint32(0);
        QTest::newRow("reserved-port") << QByteArray("COM1") << quint32(0);
        QTest::newRow("trailing-dot") << QByteArray("file.") << quint32(0);
        QTest::newRow("trailing-space") << QByteArray("file ") << quint32(0);
        QTest::newRow("symlink") << QByteArray("link") << quint32(0120777U << 16);
    }
    void unsafeArchive() {
        QFETCH(QByteArray, name);
        QFETCH(quint32, attributes);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        zip(root.filePath("input.zip"), {{name, "outside"}, {"safe.txt", "safe"}}, attributes);
        TaskManager manager;
        manager.submit("extract", {root.filePath("input.zip")},
                       QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QVERIFY(!role(manager, "error").toString().isEmpty());
        QCOMPARE(QDir(root.filePath("destination")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
        QVERIFY(!QFileInfo::exists(root.filePath("escape")));
    }
    void rejectArchiveDestinationLink() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        QVERIFY(root.mkdir("outside"));
        if (!linkDirectory(root.filePath("destination/link"), root.filePath("outside")))
            QSKIP("Directory symlink creation requires developer mode or elevated privileges");
        const auto cleanup = qScopeGuard([&] {
#ifdef Q_OS_WIN
            const auto native = QDir::toNativeSeparators(root.filePath("destination/link"));
            RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
#else
            QFile::remove(root.filePath("destination/link"));
#endif
        });
        zip(root.filePath("input.zip"), {{"link/escape", "bad"}});
        TaskManager manager;
        manager.submit("extract", {root.filePath("input.zip")},
                       QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QVERIFY(!QFileInfo::exists(root.filePath("outside/escape")));
    }
    void rejectDuplicateArchivePaths() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        zip(root.filePath("input.zip"), {{"a", "first"}, {"a", "second"}});
        TaskManager manager;
        manager.submit("extract", {root.filePath("input.zip")},
                       QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QVERIFY(!QFileInfo::exists(root.filePath("destination/a")));
    }
    void archiveLimits_data() {
        QTest::addColumn<bool>("countLimit");
        QTest::newRow("entry-count") << true;
        QTest::newRow("uncompressed-size") << false;
    }
    void archiveLimits() {
        QFETCH(bool, countLimit);
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        QList<QPair<QByteArray, QByteArray>> entries;
        const int count = countLimit ? 100001 : 6;
        entries.reserve(count);
        for (int i = 0; i < count; ++i)
            entries.append({QByteArray::number(i), countLimit ? QByteArray{} : QByteArray(1024, 'a')});
        const QString input = root.filePath("input.zip");
        zip(input, entries);
        if (!countLimit) {
            QByteArray bytes = read(input);
            qsizetype central = 0;
            for (int i = 0; i < count; ++i) {
                central = bytes.indexOf(QByteArray("PK\x01\x02", 4), central);
                QVERIFY(central >= 0);
                qToLittleEndian<quint32>(0x80000000, bytes.data() + central + 24);
                central += 46;
            }
            write(input, bytes);
        }
        TaskManager manager;
        manager.submit("extract", {input}, QUrl::fromLocalFile(root.filePath("destination")));
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QVERIFY2(role(manager, "error").toString().contains(countLimit ? "100,000" : "10 GiB"),
                 qPrintable(role(manager, "error").toString()));
        QCOMPARE(QDir(root.filePath("destination")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
    }
    void corruptArchivePreservesExistingTarget() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("destination"));
        const QString input = root.filePath("input.zip");
        zip(input, {{"a", "archive content"}});
        QByteArray bytes = read(input);
        const auto central = bytes.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central >= 0);
        bytes[central + 16] ^= 1;
        write(input, bytes);
        write(root.filePath("destination/a"), "preserved");
        TaskManager manager;
        manager.submit("extract", {input}, QUrl::fromLocalFile(root.filePath("destination")), "overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "failed");
        QCOMPARE(read(root.filePath("destination/a")), "preserved");
        QCOMPARE(QDir(root.filePath("destination")).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
    void deleteDirectory() {
        QTemporaryDir fixture(fixtureTemplate());
        QDir root(fixture.path());
        QVERIFY(root.mkpath("source/nested"));
        write(root.filePath("source/nested/a"), "data");
        TaskManager manager;
        manager.submit("delete", {root.filePath("source")});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10000);
        QCOMPARE(role(manager, "status").toString(), "completed");
        QVERIFY(!QFileInfo::exists(root.filePath("source")));
    }
    void rejectRemoteAndInvalidInput() {
        TaskManager manager;
        QSignalSpy errors(&manager, &TaskManager::errorOccurred);
        QVERIFY(manager.submit("unknown", {"a"}).isEmpty());
        QVERIFY(manager.submit("copy", {QUrl("https://example.invalid/file")}).isEmpty());
        QVERIFY(manager.submit("delete", {}).isEmpty());
        QCOMPARE(errors.size(), 3);
        QVERIFY(!manager.busy());
        QCOMPARE(manager.model()->rowCount(), 0);
    }
};

QTEST_GUILESS_MAIN(OperationTests)
#include "operationtests.moc"
