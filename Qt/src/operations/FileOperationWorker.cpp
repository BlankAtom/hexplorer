#include "FileOperationWorker.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <miniz.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <sys/syscall.h>
#elif defined(Q_OS_DARWIN)
#include <stdio.h>
#endif
#endif

namespace FileOperations {
namespace {

constexpr quint64 ArchiveSizeLimit = 10ULL * 1024 * 1024 * 1024;
constexpr mz_uint ArchiveEntryLimit = 100000;
constexpr int DepthLimit = 256;
constexpr qint64 ChunkSize = 1024 * 1024;

struct Failure { QString message; };
struct Cancelled {};

[[noreturn]] void fail(const QString &message) { throw Failure{message}; }

QString pathKey(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

bool exists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool isLink(const QString &path)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return QFileInfo(path).isSymLink();
}

struct Identity {
    quint64 device = 0;
    quint64 file = 0;
    bool valid = false;
    bool operator==(const Identity &other) const {
        return valid && other.valid && device == other.device && file == other.file;
    }
};

#ifdef Q_OS_WIN
Identity handleIdentity(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information))
        return {};
    return {information.dwVolumeSerialNumber,
            (quint64(information.nFileIndexHigh) << 32) | information.nFileIndexLow, true};
}
#endif

Identity identity(const QString &path)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    const auto result = handleIdentity(handle);
    CloseHandle(handle);
    return result;
#else
    struct stat information{};
    if (::stat(QFile::encodeName(path).constData(), &information))
        return {};
    return {quint64(information.st_dev), quint64(information.st_ino), true};
#endif
}

Identity identity(QFile &file)
{
#ifdef Q_OS_WIN
    return handleIdentity(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
    struct stat information{};
    if (::fstat(file.handle(), &information))
        return {};
    return {quint64(information.st_dev), quint64(information.st_ino), true};
#endif
}

bool sameFile(const QString &left, const QString &right)
{
    if (pathKey(left) == pathKey(right))
        return true;
    return identity(left) == identity(right);
}

bool atOrInside(const QString &path, const QString &directory)
{
    QString current = QDir::cleanPath(path);
    for (;;) {
        if (sameFile(current, directory))
            return true;
        const QString parent = QFileInfo(current).absolutePath();
        if (pathKey(parent) == pathKey(current))
            return false;
        current = parent;
    }
}

bool nativeRenameNoReplace(const QString &source, const QString &destination)
{
#ifdef Q_OS_WIN
    const auto from = QDir::toNativeSeparators(source);
    const auto to = QDir::toNativeSeparators(destination);
    // Deliberately omit MOVEFILE_COPY_ALLOWED and MOVEFILE_REPLACE_EXISTING.
    if (MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()),
                    reinterpret_cast<LPCWSTR>(to.utf16()), 0))
        return true;
    const auto error = GetLastError();
    if (error == ERROR_NOT_SAME_DEVICE)
        return false;
    fail(QStringLiteral("Native move failed (Windows error %1): %2").arg(error).arg(source));
#elif defined(Q_OS_LINUX) && defined(SYS_renameat2)
    constexpr unsigned RenameNoReplace = 1;
    if (::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(source).constData(),
                  AT_FDCWD, QFile::encodeName(destination).constData(), RenameNoReplace) == 0)
        return true;
    const int error = errno;
    if (error == EXDEV || error == ENOSYS || error == EINVAL || error == EOPNOTSUPP)
        return false;
    fail(QStringLiteral("Native move failed (%1): %2").arg(error).arg(source));
#elif defined(Q_OS_DARWIN)
    if (::renamex_np(QFile::encodeName(source).constData(),
                     QFile::encodeName(destination).constData(), RENAME_EXCL) == 0)
        return true;
    const int error = errno;
    if (error == EXDEV || error == ENOTSUP)
        return false;
    fail(QStringLiteral("Native move failed (%1): %2").arg(error).arg(source));
#else
    Q_UNUSED(source);
    Q_UNUSED(destination);
    return false;
#endif
}

void preserveDirectoryTime(const QString &path, const QDateTime &modified)
{
    const qint64 milliseconds = modified.toMSecsSinceEpoch();
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        fail(QStringLiteral("Cannot open directory to preserve modification time: %1").arg(path));
    constexpr qint64 EpochOffset = 11644473600000;
    const quint64 ticks = static_cast<quint64>(milliseconds + EpochOffset) * 10000;
    FILETIME time{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    const bool success = SetFileTime(handle, nullptr, nullptr, &time);
    CloseHandle(handle);
#else
    timespec times[2]{};
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = milliseconds / 1000;
    times[1].tv_nsec = (milliseconds % 1000) * 1000000;
    if (times[1].tv_nsec < 0) {
        --times[1].tv_sec;
        times[1].tv_nsec += 1000000000;
    }
    const bool success = ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(),
                                    times, AT_SYMLINK_NOFOLLOW) == 0;
#endif
    if (!success || QFileInfo(path).lastModified() != modified)
        fail(QStringLiteral("Could not preserve directory modification time: %1").arg(path));
}

bool reservedName(const QString &component)
{
    const QString base = component.section(u'.', 0, 0).toUpper();
    static const QSet<QString> names{QStringLiteral("CON"), QStringLiteral("PRN"),
        QStringLiteral("AUX"), QStringLiteral("NUL"), QStringLiteral("CONIN$"),
        QStringLiteral("CONOUT$"), QStringLiteral("CLOCK$")};
    if (names.contains(base))
        return true;
    if (base.size() == 4 && (base.startsWith("COM") || base.startsWith("LPT"))) {
        const QChar digit = base[3];
        return (digit >= u'0' && digit <= u'9') || digit == QChar(0x00b9)
            || digit == QChar(0x00b2) || digit == QChar(0x00b3);
    }
    return false;
}

void validateComponent(const QString &component)
{
    if (component.isEmpty() || component == "." || component == ".."
        || component.endsWith(u'.') || component.endsWith(u' ') || reservedName(component))
        fail(QStringLiteral("Unsafe path component: %1").arg(component));
    for (const QChar ch : component) {
        if (ch.unicode() < 32 || ch.unicode() == 127
            || QStringLiteral("<>:\"|?*").contains(ch))
            fail(QStringLiteral("Unsafe path component: %1").arg(component));
    }
}

void safePath(const QString &path)
{
    QString current = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    const QString normalized = QDir::fromNativeSeparators(current);
    if (normalized.startsWith("//?/") || normalized.startsWith("//./"))
        fail(QStringLiteral("Device namespace paths are not supported: %1").arg(path));
    const auto parts = normalized.split(u'/', Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (i == 0 && parts[i].size() == 2 && parts[i][1] == u':')
            continue;
        validateComponent(parts[i]);
    }
#endif
    for (;;) {
        if (isLink(current))
            fail(QStringLiteral("Links and reparse points are not followed: %1").arg(current));
        const QString parent = QFileInfo(current).absolutePath();
        if (pathKey(parent) == pathKey(current))
            break;
        current = parent;
    }
}

void requirePlainDataStreams(const QString &path)
{
#ifdef Q_OS_WIN
    WIN32_FIND_STREAM_DATA stream{};
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = FindFirstStreamW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                    FindStreamInfoStandard, &stream, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_HANDLE_EOF || error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED)
            return;
        fail(QStringLiteral("Cannot inspect file streams: %1").arg(path));
    }
    struct Search { HANDLE handle; ~Search() { FindClose(handle); } } search{handle};
    do {
        if (QString::fromWCharArray(stream.cStreamName).compare("::$DATA", Qt::CaseInsensitive))
            fail(QStringLiteral("Copy/move of files with alternate data streams is not supported: %1").arg(path));
    } while (FindNextStreamW(handle, &stream));
    if (GetLastError() != ERROR_HANDLE_EOF)
        fail(QStringLiteral("Cannot enumerate file streams: %1").arg(path));
#else
    Q_UNUSED(path);
#endif
}

struct Node {
    QString name;
    QString source;
    bool directory = false;
    quint64 bytes = 0;
    quint64 files = 0;
    int archiveIndex = -1;
    Identity sourceIdentity;
    QDateTime modified;
    std::map<QString, Node> children;
};

class Archive {
public:
    explicit Archive(const QString &path) : file(path) {
        if (!file.open(QIODevice::ReadOnly))
            fail(QStringLiteral("Cannot open archive %1: %2").arg(path, file.errorString()));
        zip.m_pIO_opaque = &file;
        zip.m_pRead = [](void *opaque, mz_uint64 offset, void *buffer, size_t length) -> size_t {
            auto &input = *static_cast<QFile *>(opaque);
            if (offset > mz_uint64(std::numeric_limits<qint64>::max())
                || length > size_t(std::numeric_limits<qint64>::max())
                || !input.seek(static_cast<qint64>(offset)))
                return 0;
            const auto read = input.read(static_cast<char *>(buffer), static_cast<qint64>(length));
            return read < 0 ? 0 : static_cast<size_t>(read);
        };
        if (!mz_zip_reader_init(&zip, static_cast<mz_uint64>(file.size()), 0))
            fail(QStringLiteral("Invalid or unsupported ZIP archive: %1").arg(path));
        initialized = true;
    }
    ~Archive() { if (initialized) mz_zip_reader_end(&zip); }
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;
    QFile file;
    mz_zip_archive zip{};
private:
    bool initialized = false;
};

class Worker {
public:
    Worker(Job &job, const Progress &progress, const Conflict &conflict)
        : job(job), progress(progress), conflict(conflict) {
        timer.start();
        rateTimer.start();
    }

    Update execute() {
        try {
            check();
            publish(QStringLiteral("running"), true);
            preflight();
            scanned = true;
            publish(QStringLiteral("running"), true);
            for (auto &root : roots) {
                check();
                if (job.operation == "extract") {
                    for (auto &[name, child] : root.tree.children)
                        transfer(child, QDir(job.destination).filePath(child.name), root.archive.get());
                } else if (job.operation == "copy" || job.operation == "move") {
                    transfer(root.tree, QDir(job.destination).filePath(root.tree.name), nullptr);
                } else if (job.operation == "delete") {
                    remove(root.tree);
                } else {
                    recycle(root.tree);
                }
            }
            check();
            return snapshot(skipped ? QStringLiteral("partial") : QStringLiteral("completed"),
                    skipped ? fraction() : 1.0,
                    skipped ? QStringLiteral("Finished with skipped items. %1").arg(summary()) : QString{});
        } catch (const Cancelled &) {
            return snapshot(QStringLiteral("cancelled"), fraction(),
                    QStringLiteral("Cancelled. %1").arg(summary()));
        } catch (const Failure &error) {
            return snapshot(mutations ? QStringLiteral("partial") : QStringLiteral("failed"), fraction(),
                    error.message + QStringLiteral(" ") + summary());
        } catch (const std::exception &error) {
            return snapshot(mutations ? QStringLiteral("partial") : QStringLiteral("failed"), fraction(),
                    QString::fromUtf8(error.what()) + QStringLiteral(" ") + summary());
        } catch (...) {
            return snapshot(mutations ? QStringLiteral("partial") : QStringLiteral("failed"), fraction(),
                    QStringLiteral("Unexpected file operation failure. ") + summary());
        }
    }

private:
    struct Root { Node tree; std::unique_ptr<Archive> archive; };
    Job &job;
    const Progress &progress;
    const Conflict &conflict;
    std::vector<Root> roots;
    quint64 total = 0;
    quint64 done = 0;
    quint64 completed = 0;
    quint64 skipped = 0;
    quint64 mutations = 0;
    bool scanned = false;
    bool reportedBytes = false;
    QString current;
    QElapsedTimer timer;
    QElapsedTimer rateTimer;
    quint64 rateBytes = 0;

    void check() const { if (job.cancelled.load()) throw Cancelled{}; }
    Update snapshot(const QString &status, double fraction, const QString &error = {}) const {
        return {status, fraction, current, error, done, total, 0};
    }
    double fraction() const {
        if (!scanned)
            return -1;
        return total ? std::min(1.0, double(done) / double(total)) : 0;
    }
    QString summary() const {
        return QStringLiteral("%1 file(s) committed/removed; %2 item(s) skipped. "
                              "Completed changes are retained.")
            .arg(completed).arg(skipped);
    }
    void publish(const QString &status = QStringLiteral("running"), bool force = false) {
        if (done && !reportedBytes) {
            force = true;
            reportedBytes = true;
        }
        if (!force && timer.elapsed() < 80)
            return;
        timer.restart();
        const double seconds = double(std::max<qint64>(1, rateTimer.nsecsElapsed())) / 1000000000.0;
        const double speed = status == "running" ? double(done - rateBytes) / seconds : 0;
        rateBytes = done;
        rateTimer.restart();
        progress({status, fraction(), current, {}, done, total, speed});
    }
    void addTotal(quint64 bytes) {
        if (bytes > std::numeric_limits<quint64>::max() - total)
            fail(QStringLiteral("File operation size overflow."));
        total += bytes;
    }
    Node scan(const QString &path, int depth = 0) {
        check();
        if (depth > DepthLimit)
            fail(QStringLiteral("Directory nesting exceeds the safety limit."));
        safePath(path);
        QFileInfo info(path);
        current = path;
        publish();
        if (!info.exists())
            fail(QStringLiteral("Source does not exist: %1").arg(path));
        Node node;
        node.name = info.fileName();
        node.source = path;
        node.directory = info.isDir();
        node.sourceIdentity = identity(path);
        node.modified = info.lastModified();
        if (!node.sourceIdentity.valid)
            fail(QStringLiteral("Cannot identify source: %1").arg(path));
        if (node.directory) {
            if (!info.isReadable())
                fail(QStringLiteral("Cannot read directory: %1").arg(path));
            std::error_code error;
#ifdef Q_OS_WIN
            const std::filesystem::path nativePath(path.toStdWString());
#else
            const std::filesystem::path nativePath(QFile::encodeName(path).constData());
#endif
            std::filesystem::directory_iterator iterator(nativePath, error);
            if (error)
                fail(QStringLiteral("Cannot enumerate directory: %1 (%2)")
                    .arg(path, QString::fromStdString(error.message())));
            for (; iterator != std::filesystem::directory_iterator(); iterator.increment(error)) {
                if (error)
                    break;
#ifdef Q_OS_WIN
                const QString child = QString::fromStdWString(iterator->path().native());
#else
                const QString child = QFile::decodeName(iterator->path().native().c_str());
#endif
                auto subtree = scan(QFileInfo(child).absoluteFilePath(), depth + 1);
                if (subtree.bytes > std::numeric_limits<quint64>::max() - node.bytes)
                    fail(QStringLiteral("Directory size overflow."));
                node.bytes += subtree.bytes;
                node.files += subtree.files;
                node.children.emplace(subtree.name, std::move(subtree));
            }
            if (error)
                fail(QStringLiteral("Directory enumeration failed: %1 (%2)")
                    .arg(path, QString::fromStdString(error.message())));
        } else {
            if (!info.isFile())
                fail(QStringLiteral("Only regular files and directories are supported: %1").arg(path));
            if (job.operation == "copy")
                requirePlainDataStreams(path);
            node.bytes = static_cast<quint64>(info.size());
            node.files = 1;
            addTotal(node.bytes);
        }
        return node;
    }
    Node scanArchive(Archive &archive, const QString &source) {
        Node root;
        root.directory = true;
        root.source = source;
        const mz_uint count = mz_zip_reader_get_num_files(&archive.zip);
        if (count > ArchiveEntryLimit)
            fail(QStringLiteral("ZIP has too many entries (limit 100,000)."));
        QSet<QString> entries;
        for (mz_uint index = 0; index < count; ++index) {
            check();
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&archive.zip, index, &stat))
                fail(QStringLiteral("Cannot read ZIP entry metadata."));
            const quint32 mode = (stat.m_external_attr >> 16) & 0170000;
            if (mode && mode != 0100000 && mode != 0040000)
                fail(QStringLiteral("ZIP links and special files are not supported."));
            if (stat.m_external_attr & 0x400)
                fail(QStringLiteral("ZIP reparse points are not supported."));
            if (stat.m_is_encrypted || !mz_zip_reader_is_file_supported(&archive.zip, index))
                fail(QStringLiteral("Encrypted or unsupported ZIP entry."));
            const mz_uint length = mz_zip_reader_get_filename(&archive.zip, index, nullptr, 0);
            if (length <= 1 || length > 4097)
                fail(QStringLiteral("ZIP entry has an empty or excessively long name."));
            QByteArray raw(static_cast<qsizetype>(length), '\0');
            if (mz_zip_reader_get_filename(&archive.zip, index, raw.data(), length) != length
                || std::strlen(raw.constData()) != length - 1)
                fail(QStringLiteral("ZIP entry contains an invalid name."));
            raw.chop(1);
            QStringDecoder decoder(QStringDecoder::Utf8);
            QString path = decoder(raw);
            if (decoder.hasError())
                fail(QStringLiteral("ZIP filenames must be valid UTF-8."));
            path.replace(u'\\', u'/');
            const bool directory = stat.m_is_directory || mode == 0040000 || path.endsWith(u'/');
            if (path.startsWith(u'/'))
                fail(QStringLiteral("Absolute ZIP paths are not allowed."));
            if (path.endsWith(u'/'))
                path.chop(1);
            const auto parts = path.split(u'/', Qt::KeepEmptyParts);
            if (parts.size() > DepthLimit)
                fail(QStringLiteral("ZIP directory nesting exceeds the safety limit."));
            for (const auto &part : parts)
                validateComponent(part);
            const QString key = pathKey(path);
            if (entries.contains(key))
                fail(QStringLiteral("Duplicate ZIP destination: %1").arg(path));
            entries.insert(key);
            if (directory && stat.m_uncomp_size != 0)
                fail(QStringLiteral("ZIP directory entry contains file data."));
            if (stat.m_uncomp_size > ArchiveSizeLimit - total)
                fail(QStringLiteral("ZIP exceeds the 10 GiB uncompressed size limit."));
            addTotal(stat.m_uncomp_size);
            Node *node = &root;
            for (qsizetype part = 0; part < parts.size(); ++part) {
                const QString childKey = pathKey(parts[part]);
                auto [it, inserted] = node->children.try_emplace(childKey);
                Node &child = it->second;
                if (inserted) {
                    child.name = parts[part];
                    child.directory = true;
                    child.source = source + QStringLiteral(":") + parts.mid(0, part + 1).join(u'/');
                }
                if (!child.directory)
                    fail(QStringLiteral("ZIP file/directory path conflict: %1").arg(path));
                node = &child;
                if (part == parts.size() - 1) {
                    if (!directory && !node->children.empty())
                        fail(QStringLiteral("ZIP file/directory path conflict: %1").arg(path));
                    node->directory = directory;
                    node->bytes = stat.m_uncomp_size;
                    node->archiveIndex = static_cast<int>(index);
                }
            }
            current = path;
            publish();
        }
        return root;
    }
    void preflight() {
        if (!job.destination.isEmpty()) {
            safePath(job.destination);
            if (!QFileInfo(job.destination).isDir())
                fail(QStringLiteral("Destination is not an existing directory: %1").arg(job.destination));
        }
        for (qsizetype i = 0; i < job.sources.size(); ++i) {
            check();
            const QString &source = job.sources[i];
            safePath(source);
            QFileInfo info(source);
            if (!info.exists())
                fail(QStringLiteral("Source does not exist: %1").arg(source));
            if (info.fileName().isEmpty() || QDir(source).isRoot())
                fail(QStringLiteral("Filesystem roots cannot be operated on."));
            for (qsizetype j = 0; j < i; ++j) {
                if (sameFile(source, job.sources[j])
                    || (info.isDir() && atOrInside(job.sources[j], source))
                    || (QFileInfo(job.sources[j]).isDir() && atOrInside(source, job.sources[j])))
                    fail(QStringLiteral("Overlapping or aliased source selections are not supported."));
            }
            Root root;
            if (job.operation == "extract") {
                if (!info.isFile())
                    fail(QStringLiteral("Extraction requires ZIP files."));
                root.archive = std::make_unique<Archive>(source);
                root.tree = scanArchive(*root.archive, source);
            } else {
                if (!job.destination.isEmpty()) {
                    const auto target = QDir(job.destination).filePath(info.fileName());
                    if (sameFile(source, target) || (info.isDir() && atOrInside(target, source)))
                        fail(QStringLiteral("Cannot copy/move an item onto itself or into its descendant: %1")
                            .arg(source));
                }
                root.tree = scan(source);
            }
            roots.push_back(std::move(root));
        }
    }
    QString ask(const QString &source, const QString &target) {
        QMutexLocker locker(&job.mutex);
        check();
        if (job.policy != "ask")
            return job.policy;
        job.waiting = true;
        job.decisionReady = false;
        publish(QStringLiteral("waiting"), true);
        conflict(source, target);
        while (!job.decisionReady && !job.cancelled)
            job.condition.wait(&job.mutex);
        job.waiting = false;
        check();
        const auto decision = job.decision;
        locker.unlock();
        publish(QStringLiteral("running"), true);
        return decision;
    }
    struct Target { QString path; bool overwrite = false; };
    Target choose(const Node &node, const QString &target, bool archive) {
        check();
        safePath(target);
        if (!archive && sameFile(node.source, target))
            fail(QStringLiteral("Source and destination are the same file: %1").arg(target));
        if (archive) {
            for (const auto &source : job.sources)
                if (sameFile(source, target))
                    fail(QStringLiteral("Extraction would overwrite an input archive: %1").arg(target));
        }
        if (!exists(target))
            return {target, false};
        if (node.directory && QFileInfo(target).isDir())
            return {target, true};
        const QString policy = ask(node.source, target);
        check();
        if (policy == "skip") {
            ++skipped;
            return {};
        }
        if (policy == "rename") {
            const QFileInfo info(target);
            QString stem = info.fileName();
            QString extension;
            if (!node.directory) {
                const auto dot = stem.lastIndexOf(u'.');
                if (dot > 0) {
                    extension = stem.mid(dot);
                    stem.truncate(dot);
                }
            }
            for (quint64 suffix = 1; suffix < 1000000; ++suffix) {
                check();
                const auto renamed = info.dir().filePath(
                    QStringLiteral("%1 (%2)%3").arg(stem).arg(suffix).arg(extension));
                if (!exists(renamed)) {
                    safePath(renamed);
                    return {renamed, false};
                }
            }
            fail(QStringLiteral("Cannot find an unused destination name."));
        }
        if (policy != "overwrite")
            fail(QStringLiteral("Invalid conflict resolution."));
        if (node.directory || QFileInfo(target).isDir())
            fail(QStringLiteral("Overwrite cannot replace a file with a directory or a directory with a file: %1")
                .arg(target));
        return {target, true};
    }
    void validateTarget(const Target &target) {
        check();
        safePath(target.path);
        if (exists(target.path) && (!target.overwrite || !QFileInfo(target.path).isFile()))
            fail(QStringLiteral("Destination changed during the operation: %1").arg(target.path));
    }
    void validateSource(const Node &node) {
        safePath(node.source);
        if (!(node.sourceIdentity == identity(node.source)))
            fail(QStringLiteral("Source was replaced during the operation: %1").arg(node.source));
    }
    void validateRenameTree(const Node &node) {
        check();
        validateSource(node);
        const QFileInfo now(node.source);
        if (now.lastModified() != node.modified || now.isDir() != node.directory
            || (!node.directory && now.size() != static_cast<qint64>(node.bytes)))
            fail(QStringLiteral("Source changed before native move: %1").arg(node.source));
        for (const auto &[name, child] : node.children)
            validateRenameTree(child);
    }
    bool tryNativeMove(const Node &node, const Target &target) {
        if (job.operation != "move" || target.overwrite)
            return false;
        const Identity parent = identity(QFileInfo(target.path).absolutePath());
        if (!parent.valid || parent.device != node.sourceIdentity.device)
            return false;
        validateRenameTree(node);
        validateTarget(target);
        if (sameFile(node.source, target.path)
            || (node.directory && atOrInside(target.path, node.source)))
            fail(QStringLiteral("Native move destination aliases the source or its descendant."));
        check();
        if (!nativeRenameNoReplace(node.source, target.path))
            return false;
        ++mutations;
        completed += node.files;
        done += node.bytes;
        if (!(node.sourceIdentity == identity(target.path)))
            fail(QStringLiteral("Destination changed immediately after native move: %1").arg(target.path));
        publish();
        return true;
    }
    bool transfer(Node &node, const QString &requested, Archive *archive) {
        check();
        current = node.source;
        publish();
        const Target target = choose(node, requested, archive != nullptr);
        if (target.path.isEmpty())
            return false;
        if (!archive && tryNativeMove(node, target))
            return true;
        if (node.directory) {
            if (!archive)
                validateSource(node);
            const bool created = !exists(target.path);
            if (created) {
                safePath(target.path);
                if (!QDir().mkdir(target.path))
                    fail(QStringLiteral("Cannot create directory: %1").arg(target.path));
                ++mutations;
            }
            if (!QFileInfo(target.path).isDir())
                fail(QStringLiteral("Destination is not a directory: %1").arg(target.path));
            bool all = true;
            for (auto &[key, child] : node.children)
                all = transfer(child, QDir(target.path).filePath(child.name), archive) && all;
            if (created && all && !archive) {
                check();
                safePath(target.path);
                preserveDirectoryTime(target.path, node.modified);
            }
            if (job.operation == "move" && all) {
                check();
                validateSource(node);
                if (!QDir().rmdir(node.source))
                    fail(QStringLiteral("Copied directory but could not remove source (it may have changed): %1")
                        .arg(node.source));
                ++mutations;
            }
            return all;
        }
        if (archive)
            extractFile(node, target, *archive);
        else
            copyFile(node, target);
        return true;
    }
    void copyFile(const Node &node, const Target &target) {
        validateSource(node);
        requirePlainDataStreams(node.source);
        validateTarget(target);
        QFile input(node.source);
        if (!input.open(QIODevice::ReadOnly))
            fail(QStringLiteral("Cannot read %1: %2").arg(node.source, input.errorString()));
        const Identity original = identity(input);
        const QDateTime modified = QFileInfo(node.source).lastModified();
        if (!(original == node.sourceIdentity) || input.size() != static_cast<qint64>(node.bytes)
            || modified != node.modified)
            fail(QStringLiteral("Source changed or its identity cannot be checked: %1").arg(node.source));
        if (original == identity(target.path))
            fail(QStringLiteral("Source and destination are hard links to the same file."));
        QSaveFile output(target.path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly))
            fail(QStringLiteral("Cannot write %1: %2").arg(target.path, output.errorString()));
        QByteArray buffer(ChunkSize, Qt::Uninitialized);
        quint64 written = 0;
        while (true) {
            check();
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count < 0)
                fail(QStringLiteral("Read failed for %1: %2").arg(node.source, input.errorString()));
            if (!count)
                break;
            if (quint64(count) > node.bytes - written)
                fail(QStringLiteral("Source grew during copy: %1").arg(node.source));
            if (output.write(buffer.constData(), count) != count)
                fail(QStringLiteral("Write failed for %1: %2").arg(target.path, output.errorString()));
            written += static_cast<quint64>(count);
            done += static_cast<quint64>(count);
            publish();
        }
        if (written != node.bytes || input.size() != static_cast<qint64>(node.bytes)
            || QFileInfo(node.source).lastModified() != modified)
            fail(QStringLiteral("Source changed during copy: %1").arg(node.source));
        if (!output.setPermissions(input.permissions()))
            fail(QStringLiteral("Cannot preserve file permissions: %1").arg(target.path));
        if (!output.flush() || !output.setFileTime(modified, QFileDevice::FileModificationTime)
            || output.fileTime(QFileDevice::FileModificationTime) != modified)
            fail(QStringLiteral("Cannot preserve file modification time: %1").arg(target.path));
        validateTarget(target);
        if (original == identity(target.path))
            fail(QStringLiteral("Destination became an alias of the source."));
        if (!output.commit())
            fail(QStringLiteral("Cannot commit %1: %2").arg(target.path, output.errorString()));
        ++mutations;
        ++completed;
        input.close();
        if (job.operation == "move") {
            check();
            validateSource(node);
            requirePlainDataStreams(node.source);
            const QFileInfo now(node.source);
            if (!(original == identity(node.source)) || now.size() != static_cast<qint64>(node.bytes)
                || now.lastModified() != modified)
                fail(QStringLiteral("Target committed, but changed source was not removed: %1").arg(node.source));
            if (!QFile::remove(node.source))
                fail(QStringLiteral("Target committed, but source could not be removed: %1").arg(node.source));
        }
    }
    void extractFile(const Node &node, const Target &target, Archive &archive) {
        validateTarget(target);
        QSaveFile output(target.path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly))
            fail(QStringLiteral("Cannot extract to %1: %2").arg(target.path, output.errorString()));
        struct Context {
            Worker *worker;
            QSaveFile *output;
            quint64 expected;
            quint64 written = 0;
            bool failed = false;
        } context{this, &output, node.bytes};
        const auto callback = [](void *opaque, mz_uint64 offset, const void *data, size_t length) -> size_t {
            auto &ctx = *static_cast<Context *>(opaque);
            if (ctx.worker->job.cancelled || offset != ctx.written
                || ctx.written > ctx.expected || length > ctx.expected - ctx.written) {
                ctx.failed = true;
                return 0;
            }
            size_t processed = 0;
            while (processed < length) {
                if (ctx.worker->job.cancelled) {
                    ctx.failed = true;
                    return 0;
                }
                const auto chunk = static_cast<qint64>(std::min<size_t>(ChunkSize, length - processed));
                if (ctx.output->write(static_cast<const char *>(data) + processed, chunk) != chunk) {
                    ctx.failed = true;
                    return 0;
                }
                processed += static_cast<size_t>(chunk);
                ctx.written += static_cast<quint64>(chunk);
                ctx.worker->done += static_cast<quint64>(chunk);
                // Never unwind a C callback, including on allocation failure.
                try { ctx.worker->publish(); } catch (...) { ctx.failed = true; return 0; }
            }
            return length;
        };
        const bool extracted = mz_zip_reader_extract_to_callback(&archive.zip,
            static_cast<mz_uint>(node.archiveIndex), callback, &context, 0);
        check();
        if (!extracted || context.failed || context.written != node.bytes)
            fail(QStringLiteral("ZIP extraction/CRC or write failure: %1").arg(node.source));
        validateTarget(target);
        for (const auto &source : job.sources)
            if (sameFile(source, target.path))
                fail(QStringLiteral("Extraction would overwrite an input archive."));
        if (!output.commit())
            fail(QStringLiteral("Cannot commit extracted file %1: %2").arg(target.path, output.errorString()));
        ++mutations;
        ++completed;
    }
    void remove(Node &node) {
        check();
        current = node.source;
        validateSource(node);
        if (node.directory) {
            for (auto &[key, child] : node.children)
                remove(child);
            check();
            validateSource(node);
            if (!QDir().rmdir(node.source))
                fail(QStringLiteral("Cannot remove directory (it may have changed): %1").arg(node.source));
        } else {
            if (!QFile::remove(node.source))
                fail(QStringLiteral("Cannot delete file: %1").arg(node.source));
            done += node.bytes;
            ++completed;
        }
        ++mutations;
        publish();
    }
    void recycle(Node &node) {
        check();
        current = node.source;
        validateSource(node);
#ifdef Q_OS_WIN
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized))
            fail(QStringLiteral("Cannot initialize recycle worker COM apartment."));
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        IFileOperation *rawOperation = nullptr;
        HRESULT result = CoCreateInstance(__uuidof(FileOperation), nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&rawOperation));
        if (FAILED(result))
            fail(QStringLiteral("Cannot create Windows recycle operation."));
        const auto releaseOperation = [](IFileOperation *value) { value->Release(); };
        std::unique_ptr<IFileOperation, decltype(releaseOperation)> operation(rawOperation, releaseOperation);
        result = operation->SetOperationFlags(FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT
            | FOF_NO_CONNECTED_ELEMENTS | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE | FOFX_NOCOPYHOOKS);
        if (FAILED(result))
            fail(QStringLiteral("Cannot configure a noninteractive recycle operation."));
        IShellItem *rawItem = nullptr;
        const QString path = QDir::toNativeSeparators(node.source);
        result = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                             IID_PPV_ARGS(&rawItem));
        if (FAILED(result))
            fail(QStringLiteral("Cannot access recycle source: %1").arg(node.source));
        const auto releaseItem = [](IShellItem *value) { value->Release(); };
        std::unique_ptr<IShellItem, decltype(releaseItem)> item(rawItem, releaseItem);
        if (FAILED(operation->DeleteItem(item.get(), nullptr)))
            fail(QStringLiteral("Cannot queue recycle source: %1").arg(node.source));
        check();
        result = operation->PerformOperations();
        BOOL aborted = FALSE;
        const HRESULT queryResult = operation->GetAnyOperationsAborted(&aborted);
        if (FAILED(result) || FAILED(queryResult) || aborted) {
            ++mutations;
            fail(QStringLiteral("Recycle failed or was aborted (Windows error %1): %2. "
                                "The shell may have recycled part of this item.")
                .arg(quint32(result), 8, 16, QLatin1Char('0')).arg(node.source));
        }
#else
        QString trashedPath;
        if (!QFile::moveToTrash(node.source, &trashedPath))
            fail(QStringLiteral("Cannot move to trash: %1").arg(node.source));
#endif
        if (exists(node.source))
            fail(QStringLiteral("Recycle did not remove the source: %1").arg(node.source));
        done += node.bytes;
        completed += node.files;
        ++mutations;
        publish();
    }
};

}

bool validPolicy(const QString &policy)
{
    return policy == "ask" || policy == "overwrite" || policy == "skip" || policy == "rename";
}

Update run(Job &job, const Progress &progress, const Conflict &conflict)
{
    return Worker(job, progress, conflict).execute();
}

}
