// ICC profile management: the app's own profile folder plus the profile
// folders of the operating system.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace engine {

struct ProfileInfo {
    QString path;
    QString description;
    QString colorSpace;  // "CMYK", "RGB", "Gray", ...
    QString deviceClass; // "Output", "Display", ...
    bool inAppLibrary = false;

    bool isCmyk() const { return colorSpace == QLatin1String("CMYK"); }
    bool isRgb() const { return colorSpace == QLatin1String("RGB"); }
};

class ProfileLibrary {
public:
    // `appDir` overrides the app profile folder (used by tests).
    explicit ProfileLibrary(const QString &appDir = QString());

    QString appDir() const { return m_appDir; }
    static QStringList systemDirs();
    // Folder that a system-wide install for the current user copies into.
    static QString userSystemDir();

    // Profiles in the app library first, then system profiles.
    QList<ProfileInfo> list() const;

    // Path of the profile the operating system has assigned to the main
    // screen, or an empty string when none is assigned or the platform is
    // not supported (currently only Windows is).
    static QString systemMonitorProfile();

    static bool inspect(const QString &path, ProfileInfo *info, QString *error);
    static QByteArray load(const QString &path);

    // Copies a profile into the app library. Returns the new path, or an
    // empty string and sets `error`.
    QString import(const QString &path, QString *error);
    bool remove(const QString &path, QString *error);
    // Makes the profile available to other applications.
    static bool installSystemWide(const QString &path, QString *error);

private:
    QString m_appDir;
};

} // namespace engine
