/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QByteArray>
#include <QDebug>
#include <QProcess>
#include <QSettings>
#include <QThread>
#include <QSysInfo>
#include <boost/version.hpp>
#include <libtorrent/version.hpp>

#ifdef DISABLE_GUI
#include <QCoreApplication>
#else
#include <QApplication>
#include <QDesktopWidget>
#include <QStyle>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <powrprof.h>
const int UNLEN = 256;
#else
#include <unistd.h>
#include <sys/types.h>
#endif

#ifdef Q_OS_MAC
#include <CoreServices/CoreServices.h>
#include <Carbon/Carbon.h>
#endif

#ifndef DISABLE_GUI
#if (defined(Q_OS_UNIX) && !defined(Q_OS_MAC)) && defined(QT_DBUS_LIB)
#include <QDBusInterface>
#include <QDBusMessage>
#endif
#endif // DISABLE_GUI

#ifndef DISABLE_GUI
#include <QDesktopServices>
#include <QProcess>
#endif

#include "base/utils/string.h"
#include "base/unicodestrings.h"
#include "base/logger.h"
#include "misc.h"
#include "fs.h"

static struct { const char *source; const char *comment; } units[] = {
    QT_TRANSLATE_NOOP3("misc", "B", "bytes"),
    QT_TRANSLATE_NOOP3("misc", "KiB", "kibibytes (1024 bytes)"),
    QT_TRANSLATE_NOOP3("misc", "MiB", "mebibytes (1024 kibibytes)"),
    QT_TRANSLATE_NOOP3("misc", "GiB", "gibibytes (1024 mibibytes)"),
    QT_TRANSLATE_NOOP3("misc", "TiB", "tebibytes (1024 gibibytes)")
};

#ifndef DISABLE_GUI
void Utils::Misc::shutdownComputer(ShutdownAction action)
{
#if (defined(Q_OS_UNIX) && !defined(Q_OS_MAC)) && defined(QT_DBUS_LIB)
    // Use dbus to power off / suspend the system
    if (action != ShutdownAction::Shutdown) {
        // Some recent systems use systemd's logind
        QDBusInterface login1Iface("org.freedesktop.login1", "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
        if (login1Iface.isValid()) {
            if (action == ShutdownAction::Suspend)
                login1Iface.call("Suspend", false);
            else
                login1Iface.call("Hibernate", false);
            return;
        }
        // Else, other recent systems use UPower
        QDBusInterface upowerIface("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                   "org.freedesktop.UPower", QDBusConnection::systemBus());
        if (upowerIface.isValid()) {
            if (action == ShutdownAction::Suspend)
                upowerIface.call("Suspend");
            else
                upowerIface.call("Hibernate");
            return;
        }
        // HAL (older systems)
        QDBusInterface halIface("org.freedesktop.Hal", "/org/freedesktop/Hal/devices/computer",
                                "org.freedesktop.Hal.Device.SystemPowerManagement",
                                QDBusConnection::systemBus());
        if (action == ShutdownAction::Suspend)
            halIface.call("Suspend", 5);
        else
            halIface.call("Hibernate");
    }
    else {
        // Some recent systems use systemd's logind
        QDBusInterface login1Iface("org.freedesktop.login1", "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
        if (login1Iface.isValid()) {
            login1Iface.call("PowerOff", false);
            return;
        }
        // Else, other recent systems use ConsoleKit
        QDBusInterface consolekitIface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager",
                                       "org.freedesktop.ConsoleKit.Manager", QDBusConnection::systemBus());
        if (consolekitIface.isValid()) {
            consolekitIface.call("Stop");
            return;
        }
        // HAL (older systems)
        QDBusInterface halIface("org.freedesktop.Hal", "/org/freedesktop/Hal/devices/computer",
                                "org.freedesktop.Hal.Device.SystemPowerManagement",
                                QDBusConnection::systemBus());
        halIface.call("Shutdown");
    }
#endif
#ifdef Q_OS_MAC
    AEEventID EventToSend;
    if (action != ShutdownAction::Shutdown)
        EventToSend = kAESleep;
    else
        EventToSend = kAEShutDown;
    AEAddressDesc targetDesc;
    static const ProcessSerialNumber kPSNOfSystemProcess = { 0, kSystemProcess };
    AppleEvent eventReply = {typeNull, NULL};
    AppleEvent appleEventToSend = {typeNull, NULL};

    OSStatus error = AECreateDesc(typeProcessSerialNumber, &kPSNOfSystemProcess,
                         sizeof(kPSNOfSystemProcess), &targetDesc);

    if (error != noErr)
        return;

    error = AECreateAppleEvent(kCoreEventClass, EventToSend, &targetDesc,
                               kAutoGenerateReturnID, kAnyTransactionID, &appleEventToSend);

    AEDisposeDesc(&targetDesc);
    if (error != noErr)
        return;

    error = AESend(&appleEventToSend, &eventReply, kAENoReply,
                   kAENormalPriority, kAEDefaultTimeout, NULL, NULL);

    AEDisposeDesc(&appleEventToSend);
    if (error != noErr)
        return;

    AEDisposeDesc(&eventReply);
#endif
#ifdef Q_OS_WIN
    HANDLE hToken;            // handle to process token
    TOKEN_PRIVILEGES tkp;     // pointer to token structure
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return;
    // Get the LUID for shutdown privilege.
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME,
                         &tkp.Privileges[0].Luid);

    tkp.PrivilegeCount = 1; // one privilege to set
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get shutdown privilege for this process.

    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0,
                          (PTOKEN_PRIVILEGES) NULL, 0);

    // Cannot test the return value of AdjustTokenPrivileges.

    if (GetLastError() != ERROR_SUCCESS)
        return;

    if (action == ShutdownAction::Suspend)
        SetSuspendState(false, false, false);
    else if (action == ShutdownAction::Hibernate)
        SetSuspendState(true, false, false);
    else
        InitiateSystemShutdownA(0, QCoreApplication::translate("misc", "qBittorrent will shutdown the computer now because all downloads are complete.").toLocal8Bit().data(), 10, true, false);

    // Disable shutdown privilege.
    tkp.Privileges[0].Attributes = 0;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0,
                          (PTOKEN_PRIVILEGES) NULL, 0);
#endif
}
#endif // DISABLE_GUI

#ifndef DISABLE_GUI
// Get screen center
QPoint Utils::Misc::screenCenter(QWidget *win)
{
    int scrn = 0;
    const QWidget *w = win->window();

    if (w)
        scrn = QApplication::desktop()->screenNumber(w);
    else if (QApplication::desktop()->isVirtualDesktop())
        scrn = QApplication::desktop()->screenNumber(QCursor::pos());
    else
        scrn = QApplication::desktop()->screenNumber(win);

    QRect desk(QApplication::desktop()->availableGeometry(scrn));
    return QPoint((desk.width() - win->frameGeometry().width()) / 2, (desk.height() - win->frameGeometry().height()) / 2);
}
#endif

/**
 * Detects the python version.
 */
int Utils::Misc::pythonVersion()
{
    static int version = -1;
    if (version < 0) {
        QString versionComplete = pythonVersionComplete().trimmed();
        QStringList splitted = versionComplete.split('.');
        if (splitted.size() > 1) {
            int highVer = splitted.at(0).toInt();
            if ((highVer == 2) || (highVer == 3))
                version = highVer;
        }
    }
    return version;
}

/**
 * Detects the python executable by calling "python --version".
 */
QString Utils::Misc::pythonExecutable()
{
    static QString executable;
    if (executable.isEmpty()) {
        QProcess pythonProc;
#if defined(Q_OS_UNIX)
        /*
         * On Unix-Like Systems python2 and python3 should always exist
         * http://legacy.python.org/dev/peps/pep-0394/
         */
        pythonProc.start("python3", QStringList() << "--version", QIODevice::ReadOnly);
        if (pythonProc.waitForFinished() && pythonProc.exitCode() == 0) {
            executable = "python3";
            return executable;
        }
        pythonProc.start("python2", QStringList() << "--version", QIODevice::ReadOnly);
        if (pythonProc.waitForFinished() && pythonProc.exitCode() == 0) {
            executable = "python2";
            return executable;
        }
#endif
        // Look for "python" in Windows and in UNIX if "python2" and "python3" are
        // not detected.
        pythonProc.start("python", QStringList() << "--version", QIODevice::ReadOnly);
        if (pythonProc.waitForFinished() && pythonProc.exitCode() == 0)
            executable = "python";
        else
            Logger::instance()->addMessage(QCoreApplication::translate("misc", "Python not detected"), Log::INFO);
    }
    return executable;
}

/**
 * Returns the complete python version
 * eg 2.7.9
 * Make sure to have setup python first
 */
QString Utils::Misc::pythonVersionComplete() {
    static QString version;
    if (version.isEmpty()) {
        if (pythonExecutable().isEmpty())
            return version;
        QProcess pythonProc;
        pythonProc.start(pythonExecutable(), QStringList() << "--version", QIODevice::ReadOnly);
        if (pythonProc.waitForFinished() && pythonProc.exitCode() == 0) {
            QByteArray output = pythonProc.readAllStandardOutput();
            if (output.isEmpty())
                output = pythonProc.readAllStandardError();

            // Software 'Anaconda' installs its own python interpreter
            // and `python --version` returns a string like this:
            // `Python 3.4.3 :: Anaconda 2.3.0 (64-bit)`
            const QList<QByteArray> verSplit = output.split(' ');
            if (verSplit.size() > 1) {
                version = verSplit.at(1).trimmed();
                Logger::instance()->addMessage(QCoreApplication::translate("misc", "Python version: %1").arg(version), Log::INFO);
            }
        }
    }
    return version;
}

// return best userfriendly storage unit (B, KiB, MiB, GiB, TiB)
// use Binary prefix standards from IEC 60027-2
// see http://en.wikipedia.org/wiki/Kilobyte
// value must be given in bytes
// to send numbers instead of strings with suffixes
QString Utils::Misc::friendlyUnit(qreal val, bool is_speed)
{
    if (val < 0)
        return QCoreApplication::translate("misc", "Unknown", "Unknown (size)");
    int i = 0;
    while(val >= 1024. && i < 4) {
        val /= 1024.;
        ++i;
    }
    QString ret;
    if (i == 0)
        ret = QString::number((long)val) + " " + QCoreApplication::translate("misc", units[0].source, units[0].comment);
    else
        ret = Utils::String::fromDouble(val, 1) + " " + QCoreApplication::translate("misc", units[i].source, units[i].comment);
    if (is_speed)
        ret += QCoreApplication::translate("misc", "/s", "per second");
    return ret;
}

bool Utils::Misc::isPreviewable(const QString& extension)
{
    static QSet<QString> multimedia_extensions;
    if (multimedia_extensions.empty()) {
        multimedia_extensions.insert("3GP");
        multimedia_extensions.insert("AAC");
        multimedia_extensions.insert("AC3");
        multimedia_extensions.insert("AIF");
        multimedia_extensions.insert("AIFC");
        multimedia_extensions.insert("AIFF");
        multimedia_extensions.insert("ASF");
        multimedia_extensions.insert("AU");
        multimedia_extensions.insert("AVI");
        multimedia_extensions.insert("FLAC");
        multimedia_extensions.insert("FLV");
        multimedia_extensions.insert("M3U");
        multimedia_extensions.insert("M4A");
        multimedia_extensions.insert("M4P");
        multimedia_extensions.insert("M4V");
        multimedia_extensions.insert("MID");
        multimedia_extensions.insert("MKV");
        multimedia_extensions.insert("MOV");
        multimedia_extensions.insert("MP2");
        multimedia_extensions.insert("MP3");
        multimedia_extensions.insert("MP4");
        multimedia_extensions.insert("MPC");
        multimedia_extensions.insert("MPE");
        multimedia_extensions.insert("MPEG");
        multimedia_extensions.insert("MPG");
        multimedia_extensions.insert("MPP");
        multimedia_extensions.insert("OGG");
        multimedia_extensions.insert("OGM");
        multimedia_extensions.insert("OGV");
        multimedia_extensions.insert("QT");
        multimedia_extensions.insert("RA");
        multimedia_extensions.insert("RAM");
        multimedia_extensions.insert("RM");
        multimedia_extensions.insert("RMV");
        multimedia_extensions.insert("RMVB");
        multimedia_extensions.insert("SWA");
        multimedia_extensions.insert("SWF");
        multimedia_extensions.insert("VOB");
        multimedia_extensions.insert("WAV");
        multimedia_extensions.insert("WMA");
        multimedia_extensions.insert("WMV");
    }

    if (extension.isEmpty())
        return false;

    return multimedia_extensions.contains(extension.toUpper());
}

// Take a number of seconds and return an user-friendly
// time duration like "1d 2h 10m".
QString Utils::Misc::userFriendlyDuration(qlonglong seconds)
{
    if (seconds < 0 || seconds >= MAX_ETA)
        return QString::fromUtf8(C_INFINITY);
    if (seconds == 0)
        return "0";
    if (seconds < 60)
        return QCoreApplication::translate("misc", "< 1m", "< 1 minute");
    int minutes = seconds / 60;
    if (minutes < 60)
        return QCoreApplication::translate("misc", "%1m","e.g: 10minutes").arg(QString::number(minutes));
    int hours = minutes / 60;
    minutes = minutes - hours * 60;
    if (hours < 24)
        return QCoreApplication::translate("misc", "%1h %2m", "e.g: 3hours 5minutes").arg(QString::number(hours)).arg(QString::number(minutes));
    int days = hours / 24;
    hours = hours - days * 24;
    if (days < 100)
        return QCoreApplication::translate("misc", "%1d %2h", "e.g: 2days 10hours").arg(QString::number(days)).arg(QString::number(hours));
    return QString::fromUtf8(C_INFINITY);
}

QString Utils::Misc::getUserIDString()
{
    QString uid = "0";
#ifdef Q_OS_WIN
    WCHAR buffer[UNLEN + 1] = {0};
    DWORD buffer_len = sizeof(buffer)/sizeof(*buffer);
    if (GetUserNameW(buffer, &buffer_len))
        uid = QString::fromWCharArray(buffer);
#else
    uid = QString::number(getuid());
#endif
    return uid;
}

QStringList Utils::Misc::toStringList(const QList<bool> &l)
{
    QStringList ret;
    foreach (const bool &b, l)
        ret << (b ? "1" : "0");
    return ret;
}

QList<int> Utils::Misc::intListfromStringList(const QStringList &l)
{
    QList<int> ret;
    foreach (const QString &s, l)
        ret << s.toInt();
    return ret;
}

QList<bool> Utils::Misc::boolListfromStringList(const QStringList &l)
{
    QList<bool> ret;
    foreach (const QString &s, l)
        ret << (s == "1");
    return ret;
}

bool Utils::Misc::isUrl(const QString &s)
{
    const QString scheme = QUrl(s).scheme();
    QRegExp is_url("http[s]?|ftp", Qt::CaseInsensitive);
    return is_url.exactMatch(scheme);
}

QString Utils::Misc::parseHtmlLinks(const QString &raw_text)
{
    QString result = raw_text;
    static QRegExp reURL(
        "(\\s|^)"                                             //start with whitespace or beginning of line
        "("
        "("                                              //case 1 -- URL with scheme
        "(http(s?))\\://"                            //start with scheme
        "([a-zA-Z0-9_-]+\\.)+"                       //  domainpart.  at least one of these must exist
        "([a-zA-Z0-9\\?%=&/_\\.:#;-]+)"              //  everything to 1st non-URI char, must be at least one char after the previous dot (cannot use ".*" because it can be too greedy)
        ")"
        "|"
        "("                                             //case 2a -- no scheme, contains common TLD  example.com
        "([a-zA-Z0-9_-]+\\.)+"                      //  domainpart.  at least one of these must exist
        "(?="                                       //  must be followed by TLD
        "AERO|aero|"                          //N.B. assertions are non-capturing
        "ARPA|arpa|"
        "ASIA|asia|"
        "BIZ|biz|"
        "CAT|cat|"
        "COM|com|"
        "COOP|coop|"
        "EDU|edu|"
        "GOV|gov|"
        "INFO|info|"
        "INT|int|"
        "JOBS|jobs|"
        "MIL|mil|"
        "MOBI|mobi|"
        "MUSEUM|museum|"
        "NAME|name|"
        "NET|net|"
        "ORG|org|"
        "PRO|pro|"
        "RO|ro|"
        "RU|ru|"
        "TEL|tel|"
        "TRAVEL|travel"
        ")"
        "([a-zA-Z0-9\\?%=&/_\\.:#;-]+)"             //  everything to 1st non-URI char, must be at least one char after the previous dot (cannot use ".*" because it can be too greedy)
        ")"
        "|"
        "("                                             // case 2b no scheme, no TLD, must have at least 2 alphanum strings plus uncommon TLD string  --> del.icio.us
        "([a-zA-Z0-9_-]+\\.) {2,}"                   //2 or more domainpart.   --> del.icio.
        "[a-zA-Z]{2,}"                              //one ab  (2 char or longer) --> us
        "([a-zA-Z0-9\\?%=&/_\\.:#;-]*)"             // everything to 1st non-URI char, maybe nothing  in case of del.icio.us/path
        ")"
        ")"
        );


    // Capture links
    result.replace(reURL, "\\1<a href=\"\\2\">\\2</a>");

    // Capture links without scheme
    static QRegExp reNoScheme("<a\\s+href=\"(?!http(s?))([a-zA-Z0-9\\?%=&/_\\.-:#]+)\\s*\">");
    result.replace(reNoScheme, "<a href=\"http://\\1\">");

    // to preserve plain text formatting
    result = "<p style=\"white-space: pre-wrap;\">" + result + "</p>";
    return result;
}

#ifndef DISABLE_GUI
// Open the given path with an appropriate application
void Utils::Misc::openPath(const QString& absolutePath)
{
    const QString path = Utils::Fs::fromNativePath(absolutePath);
    // Hack to access samba shares with QDesktopServices::openUrl
    if (path.startsWith("//"))
        QDesktopServices::openUrl(Utils::Fs::toNativePath("file:" + path));
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// Open the parent directory of the given path with a file manager and select
// (if possible) the item at the given path
void Utils::Misc::openFolderSelect(const QString& absolutePath)
{
    const QString path = Utils::Fs::fromNativePath(absolutePath);
#ifdef Q_OS_WIN
    if (QFileInfo(path).exists()) {
        // Syntax is: explorer /select, "C:\Folder1\Folder2\file_to_select"
        // Dir separators MUST be win-style slashes

        // QProcess::startDetached() has an obscure bug. If the path has
        // no spaces and a comma(and maybe other special characters) it doesn't
        // get wrapped in quotes. So explorer.exe can't find the correct path and
        // displays the default one. If we wrap the path in quotes and pass it to
        // QProcess::startDetached() explorer.exe still shows the default path. In
        // this case QProcess::startDetached() probably puts its own quotes around ours.

        STARTUPINFO startupInfo;
        ::ZeroMemory(&startupInfo, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);

        PROCESS_INFORMATION processInfo;
        ::ZeroMemory(&processInfo, sizeof(processInfo));

        QString cmd = QString("explorer.exe /select,\"%1\"").arg(Utils::Fs::toNativePath(absolutePath));
        LPWSTR lpCmd = new WCHAR[cmd.size() + 1];
        cmd.toWCharArray(lpCmd);
        lpCmd[cmd.size()] = 0;

        bool ret = ::CreateProcessW(NULL, lpCmd, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);
        delete [] lpCmd;

        if (ret) {
            ::CloseHandle(processInfo.hProcess);
            ::CloseHandle(processInfo.hThread);
        }
    }
    else {
        // If the item to select doesn't exist, try to open its parent
        openPath(path.left(path.lastIndexOf("/")));
    }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (QFileInfo(path).exists()) {
        QProcess proc;
        QString output;
        proc.start("xdg-mime", QStringList() << "query" << "default" << "inode/directory");
        proc.waitForFinished();
        output = proc.readLine().simplified();
        if (output == "dolphin.desktop" || output == "org.kde.dolphin.desktop")
            proc.startDetached("dolphin", QStringList() << "--select" << Utils::Fs::toNativePath(path));
        else if (output == "nautilus.desktop" || output == "org.gnome.Nautilus.desktop"
                 || output == "nautilus-folder-handler.desktop")
            proc.startDetached("nautilus", QStringList() << "--no-desktop" << Utils::Fs::toNativePath(path));
        else if (output == "caja-folder-handler.desktop")
            proc.startDetached("caja", QStringList() << "--no-desktop" << Utils::Fs::toNativePath(path));
        else if (output == "nemo.desktop")
            proc.startDetached("nemo", QStringList() << "--no-desktop" << Utils::Fs::toNativePath(path));
        else if (output == "konqueror.desktop" || output == "kfmclient_dir.desktop")
            proc.startDetached("konqueror", QStringList() << "--select" << Utils::Fs::toNativePath(path));
        else
            openPath(path.left(path.lastIndexOf("/")));
    }
    else {
        // If the item to select doesn't exist, try to open its parent
        openPath(path.left(path.lastIndexOf("/")));
    }
#else
    openPath(path.left(path.lastIndexOf("/")));
#endif
}
#endif // DISABLE_GUI

namespace
{
    //  Trick to get a portable sleep() function
    class SleeperThread : public QThread
    {
    public:
        static void msleep(unsigned long msecs)
        {
            QThread::msleep(msecs);
        }
    };
}

void Utils::Misc::msleep(unsigned long msecs)
{
    SleeperThread::msleep(msecs);
}

#ifndef DISABLE_GUI
QSize Utils::Misc::smallIconSize()
{
    // Get DPI scaled icon size (device-dependent), see QT source
    int s = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    return QSize(s, s);
}
#endif

QString Utils::Misc::osName()
{
    // static initialization for usage in signal handler
    static const QString name =
#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
    QString("%1 %2 %3")
    .arg(QSysInfo::prettyProductName())
    .arg(QSysInfo::kernelVersion())
    .arg(QSysInfo::currentCpuArchitecture());
#else
    "<Input OS name here>";
#endif
    return name;
}

QString Utils::Misc::boostVersionString()
{
    // static initialization for usage in signal handler
    static const QString ver = QString("%1.%2.%3")
                 .arg(BOOST_VERSION / 100000)
                 .arg((BOOST_VERSION / 100) % 1000)
                 .arg(BOOST_VERSION % 100);
    return ver;
}

QString Utils::Misc::libtorrentVersionString()
{
    // static initialization for usage in signal handler
    static const QString ver = LIBTORRENT_VERSION;
    return ver;
}
