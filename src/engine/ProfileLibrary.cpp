#include "ProfileLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <lcms2.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icm.h>
#endif

namespace engine {

namespace {

QString colorSpaceName(cmsColorSpaceSignature s)
{
    switch (s) {
    case cmsSigCmykData: return QStringLiteral("CMYK");
    case cmsSigRgbData: return QStringLiteral("RGB");
    case cmsSigGrayData: return QStringLiteral("Gray");
    case cmsSigLabData: return QStringLiteral("Lab");
    default: return QStringLiteral("Other");
    }
}

QString deviceClassName(cmsProfileClassSignature s)
{
    switch (s) {
    case cmsSigInputClass: return QStringLiteral("Input");
    case cmsSigDisplayClass: return QStringLiteral("Display");
    case cmsSigOutputClass: return QStringLiteral("Output");
    case cmsSigLinkClass: return QStringLiteral("Device link");
    case cmsSigColorSpaceClass: return QStringLiteral("Colour space");
    case cmsSigAbstractClass: return QStringLiteral("Abstract");
    case cmsSigNamedColorClass: return QStringLiteral("Named colour");
    default: return QStringLiteral("Unknown");
    }
}

bool copyProfile(const QString &from, const QString &dir, QString *newPath, QString *error)
{
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("Could not create folder %1").arg(dir);
        return false;
    }
    const QString target = QDir(dir).filePath(QFileInfo(from).fileName());
    if (QFileInfo(target) == QFileInfo(from)) {
        if (newPath)
            *newPath = target;
        return true;
    }
    if (QFile::exists(target)) {
        if (ProfileLibrary::load(target) == ProfileLibrary::load(from)) {
            if (newPath)
                *newPath = target;
            return true;
        }
        if (error)
            *error = QStringLiteral("A different profile named %1 already exists in %2")
                         .arg(QFileInfo(from).fileName(), dir);
        return false;
    }
    if (!QFile::copy(from, target)) {
        if (error)
            *error = QStringLiteral("Could not copy the profile to %1").arg(dir);
        return false;
    }
    if (newPath)
        *newPath = target;
    return true;
}

} // namespace

ProfileLibrary::ProfileLibrary(const QString &appDir)
    : m_appDir(appDir.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/profiles")
                   : appDir)
{
}

QString ProfileLibrary::userSystemDir()
{
#if defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/ColorSync/Profiles");
#elif defined(Q_OS_WIN)
    return QString::fromLocal8Bit(qgetenv("SystemRoot")) + QStringLiteral("/System32/spool/drivers/color");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icc");
#endif
}

QStringList ProfileLibrary::systemDirs()
{
    QStringList dirs{ userSystemDir() };
#if defined(Q_OS_MACOS)
    dirs << QStringLiteral("/Library/ColorSync/Profiles")
         << QStringLiteral("/System/Library/ColorSync/Profiles");
#elif !defined(Q_OS_WIN)
    dirs << QStringLiteral("/usr/share/color/icc") << QStringLiteral("/usr/local/share/color/icc")
         << QDir::homePath() + QStringLiteral("/.color/icc");
#endif
    return dirs;
}

bool ProfileLibrary::inspect(const QString &path, ProfileInfo *info, QString *error)
{
    const QByteArray data = load(path);
    cmsHPROFILE p = data.isEmpty()
                        ? nullptr
                        : cmsOpenProfileFromMem(data.constData(), cmsUInt32Number(data.size()));
    if (!p) {
        if (error)
            *error = QStringLiteral("%1 is not a valid ICC profile.").arg(QFileInfo(path).fileName());
        return false;
    }
    if (info) {
        wchar_t text[256] = L"";
        cmsGetProfileInfo(p, cmsInfoDescription, "en", "US", text, 256);
        info->path = path;
        info->description = QString::fromWCharArray(text).trimmed();
        if (info->description.isEmpty())
            info->description = QFileInfo(path).completeBaseName();
        info->colorSpace = colorSpaceName(cmsGetColorSpace(p));
        info->deviceClass = deviceClassName(cmsGetDeviceClass(p));
    }
    cmsCloseProfile(p);
    return true;
}

QByteArray ProfileLibrary::load(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QList<ProfileInfo> ProfileLibrary::list() const
{
    QList<ProfileInfo> result;
    QSet<QString> seen;
    auto scan = [&](const QString &dir, bool app) {
        QDirIterator it(dir, { QStringLiteral("*.icc"), QStringLiteral("*.icm") }, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (seen.contains(canonical))
                continue;
            seen.insert(canonical);
            ProfileInfo info;
            if (!inspect(path, &info, nullptr))
                continue;
            info.inAppLibrary = app;
            result.append(info);
        }
    };
    scan(m_appDir, true);
    for (const QString &dir : systemDirs())
        scan(dir, false);
    return result;
}

QString ProfileLibrary::import(const QString &path, QString *error)
{
    if (!inspect(path, nullptr, error))
        return {};
    QString target;
    return copyProfile(path, m_appDir, &target, error) ? target : QString();
}

bool ProfileLibrary::remove(const QString &path, QString *error)
{
    const QString dir = QFileInfo(m_appDir).canonicalFilePath();
    if (dir.isEmpty() || QFileInfo(path).canonicalPath() != dir) {
        if (error)
            *error = QStringLiteral("Only profiles in the app library can be removed here.");
        return false;
    }
    if (!QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("Could not remove %1").arg(path);
        return false;
    }
    return true;
}

bool ProfileLibrary::installSystemWide(const QString &path, QString *error)
{
    if (!inspect(path, nullptr, error))
        return false;
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (!InstallColorProfileW(nullptr, native.c_str())) {
        if (error)
            *error = QStringLiteral("Windows refused to install the profile (error %1). "
                                    "Administrator rights may be required.")
                         .arg(GetLastError());
        return false;
    }
    return true;
#else
    return copyProfile(path, userSystemDir(), nullptr, error);
#endif
}

} // namespace engine
