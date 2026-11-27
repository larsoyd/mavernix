#include <QtTest>

#include "game-facts.hpp"

using namespace game_facts;

class GameFactsTest : public QObject
{
    Q_OBJECT

  private slots:
    void steam_id_of_app_id_data()
    {
        QTest::addColumn<QString>("appId");
        QTest::addColumn<QString>("id");
        QTest::newRow("proton game") << "steam_app_107100" << "107100";
        QTest::newRow("steam itself") << "steam" << "";
        QTest::newRow("no number") << "steam_app_" << "";
        QTest::newRow("not a number") << "steam_app_12x" << "";
        QTest::newRow("zero") << "steam_app_0" << "";
        QTest::newRow("only ends the same") << "notsteam_app_5" << "";
    }

    void steam_id_of_app_id()
    {
        QFETCH(QString, appId);
        QFETCH(QString, id);
        QCOMPARE(steamIdOfAppId(appId), id);
    }

    void game_of_environment_data()
    {
        QTest::addColumn<QByteArray>("environment");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("name");
        const auto text = [](const char* value, int size) { return QByteArray(value, size); };
        QTest::newRow("steam") << text("HOME=/h\0SteamAppId=221640\0SteamGameId=221640\0", 45) << "221640" << "";
        QTest::newRow("only game id") << text("SteamGameId=221640\0", 19) << "221640" << "";
        QTest::newRow("game id over a zero app id") << text("SteamAppId=0\0SteamGameId=7\0", 27) << "7" << "";
        QTest::newRow("steam itself") << text("SteamAppId=0\0", 13) << "" << "";
        QTest::newRow("not a number") << text("SteamAppId=abc\0", 15) << "" << "";
        QTest::newRow("other name") << text("NotSteamAppId=5\0", 16) << "" << "";
        QTest::newRow("lutris") << text("GAME_NAME=Skyrim: Special\0", 26) << "" << "Skyrim: Special";
        QTest::newRow("nothing") << text("HOME=/h\0", 8) << "" << "";
    }

    void game_of_environment()
    {
        QFETCH(QByteArray, environment);
        QFETCH(QString, id);
        QFETCH(QString, name);
        const ProcessGame game = gameOfEnvironment(environment);
        QCOMPARE(game.steamId, id);
        QCOMPARE(game.name, name);
    }

    void steam_libraries()
    {
        const QString file = "\"libraryfolders\"\n"
                             "{\n"
                             "\t\"0\"\n"
                             "\t{\n"
                             "\t\t\"path\"\t\t\"/home/you/.local/share/Steam\"\n"
                             "\t\t\"label\"\t\t\"\"\n"
                             "\t\t\"apps\"\n"
                             "\t\t{\n"
                             "\t\t\t\"228980\"\t\t\"353969662\"\n"
                             "\t\t}\n"
                             "\t}\n"
                             "\t\"1\"\n"
                             "\t{\n"
                             "\t\t\"path\"\t\t\"/mnt/games/My \\\"Steam\\\" Library\"\n"
                             "\t}\n"
                             "}";
        QCOMPARE(steamLibraries(file),
                 QStringList({"/home/you/.local/share/Steam", "/mnt/games/My \"Steam\" Library"}));
        QCOMPARE(steamLibraries(""), QStringList());
    }

    void manifest_name()
    {
        const QString file = "\"AppState\"\n{\n\t\"appid\"\t\t\"107100\"\n\t\"Universe\"\t\t\"1\"\n"
                             "\t\"name\"\t\t\"Bastion\"\n\t\"installdir\"\t\t\"Bastion\"\n}\n";
        QCOMPARE(manifestName(file), QString("Bastion"));
        QCOMPARE(manifestName("\"AppState\"\n{\n\t\"installdir\"\t\t\"x\"\n}\n"), QString());
        QCOMPARE(manifestName("\t\"username\"\t\t\"no\"\n"), QString());
    }

    void client_icon()
    {
        QCOMPARE(clientIconOf({"b71bcf668c1411086681df7ad5f23930f85eecfe", "library_600x900.jpg", "logo.png",
                               "d9dfcab8f300a9548439f70bef7b5f4e53d0fc28.jpg"}),
                 QString("d9dfcab8f300a9548439f70bef7b5f4e53d0fc28.jpg"));
        QCOMPARE(clientIconOf({"header.jpg", "logo.png"}), QString());
    }

    void lutris_icon()
    {
        QCOMPARE(lutrisIcon("The Elder Scrolls V: Skyrim Special Edition"),
                 QString("lutris_the-elder-scrolls-v-skyrim-special-edition"));
        QCOMPARE(lutrisIcon("Bastion"), QString("lutris_bastion"));
        QCOMPARE(lutrisIcon(""), QString());
    }

    void entry_ids_of_cgroup_data()
    {
        QTest::addColumn<QByteArray>("cgroup");
        QTest::addColumn<QStringList>("ids");
        const QByteArray user = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/";
        QTest::newRow("our service") << user + "app-app\\x2dmavernix\\x2dopen\\x5cx2dnectar.slice/"
                                                "app-mavernix-open\\x2dnectar\\x2dpikmin@3cd31d27.service\n"
                                     << QStringList{"mavernix-open-nectar-pikmin", "open-nectar-pikmin"};
        QTest::newRow("kio service") << user + "app-kitty@14289cb3.service\n" << QStringList{"kitty"};
        QTest::newRow("kio scope") << user + "app-org.kde.konsole-10169.scope\n" << QStringList{"org.kde.konsole"};
        QTest::newRow("unescaped scope") << user + "app-mullvad-vpn-1660.scope\n" << QStringList{"mullvad-vpn", "vpn"};
        QTest::newRow("service without random") << user + "app-foot.service\n" << QStringList{"foot"};
        QTest::newRow("group inside the unit") << user + "app-gimp@1.service/main\n" << QStringList{"gimp"};
        QTest::newRow("among v1 lines") << "1:net_cls:/\n" + user + "app-gimp@1.service\n" << QStringList{"gimp"};
        QTest::newRow("dbus service") << user + "../background.slice/plasma-dolphin.service\n" << QStringList{};
        QTest::newRow("only a slice") << user + "app-app\\x2dmavernix\\x2dsteam.slice\n" << QStringList{};
        QTest::newRow("v1 only") << QByteArray("1:name=systemd:/app-gimp@1.service\n") << QStringList{};
        QTest::newRow("nothing") << QByteArray() << QStringList{};
    }

    void entry_ids_of_cgroup()
    {
        QFETCH(QByteArray, cgroup);
        QFETCH(QStringList, ids);
        QCOMPARE(entryIdsOfCgroup(cgroup), ids);
    }

    void launched_shortcut_data()
    {
        QTest::addColumn<QByteArray>("environment");
        QTest::addColumn<QString>("path");
        const QByteArray own = QByteArray("HOME=/h\0GIO_LAUNCHED_DESKTOP_FILE=/h/Desktop/Notepad++.desktop\0", 63);
        QTest::newRow("its own launch") << own + QByteArray("GIO_LAUNCHED_DESKTOP_FILE_PID=4242\0", 35)
                                        << "/h/Desktop/Notepad++.desktop";
        QTest::newRow("pid before file") << QByteArray("GIO_LAUNCHED_DESKTOP_FILE_PID=4242\0", 35) + own.mid(8)
                                         << "/h/Desktop/Notepad++.desktop";
        QTest::newRow("a parent's launch") << own + QByteArray("GIO_LAUNCHED_DESKTOP_FILE_PID=424\0", 34) << "";
        QTest::newRow("no pid") << own << "";
        QTest::newRow("installed handler")
            << QByteArray("GIO_LAUNCHED_DESKTOP_FILE=/h/.local/share/applications/wine.desktop\0"
                          "GIO_LAUNCHED_DESKTOP_FILE_PID=4242\0", 103)
            << "";
        QTest::newRow("installed system wide")
            << QByteArray("GIO_LAUNCHED_DESKTOP_FILE=/usr/share/applications/wine.desktop\0"
                          "GIO_LAUNCHED_DESKTOP_FILE_PID=4242\0", 98)
            << "";
        QTest::newRow("folder named like one")
            << QByteArray("GIO_LAUNCHED_DESKTOP_FILE=/usr/share/applications-old/x.desktop\0"
                          "GIO_LAUNCHED_DESKTOP_FILE_PID=4242\0", 99)
            << "/usr/share/applications-old/x.desktop";
        QTest::newRow("nothing") << QByteArray("HOME=/h\0", 8) << "";
    }

    void launched_shortcut()
    {
        QFETCH(QByteArray, environment);
        QFETCH(QString, path);
        const QStringList installed{"/h/.local/share/applications", "/usr/share/applications"};
        QCOMPARE(launchedShortcut(environment, 4242, installed), path);
    }

    void shortcut_of()
    {
        const QString file = "[Desktop Entry]\n"
                             "Name=FL Studio 2026\n"
                             "Name[nb]=FL Studio norsk\n"
                             "Exec=env \"WINEPREFIX=/h/.wine\" /opt/wine/bin/wine "
                             "\"C:\\\\\\\\Program Files\\\\\\\\Image-Line\\\\\\\\FL64.exe\" %U\n"
                             "Icon=1010_FL64.0\n"
                             "StartupWMClass=fl64.exe\n"
                             "\n"
                             "[Desktop Action new]\n"
                             "Name=New Project\n"
                             "Exec=other.exe\n"
                             "Icon=document-new\n";
        const Shortcut found = shortcutOf("/h/Desktop/FL.desktop", file);
        QCOMPARE(found.path, QString("/h/Desktop/FL.desktop"));
        QCOMPARE(found.name, QString("FL Studio 2026"));
        QCOMPARE(found.icon, QString("1010_FL64.0"));
        QCOMPARE(found.windowClass, QString("fl64.exe"));
        QCOMPARE(found.programs, QStringList({"env", ".wine", "wine", "FL64.exe", "%U"}));
    }

    void opens_windows_programs_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<bool>("opener");
        QTest::newRow("wine loader") << "[Desktop Entry]\nName=Wine TKG Patched\nExec=wine-tkg %f\nMimeType="
                                        "application/x-ms-dos-executable;application/x-msi;application/x-msdownload;"
                                        "application/vnd.microsoft.portable-executable;\n" << true;
        QTest::newRow("only the new type") << "[Desktop Entry]\nMimeType=application/vnd.microsoft.portable-executable\n"
                                           << true;
        QTest::newRow("only the dos type") << "[Desktop Entry]\nMimeType=application/x-ms-dos-executable;\n" << true;
        QTest::newRow("only downloads") << "[Desktop Entry]\nMimeType=text/plain;application/x-msdownload;\n" << true;
        QTest::newRow("only installers") << "[Desktop Entry]\nMimeType=application/x-msi;\n" << true;
        QTest::newRow("windows app") << "[Desktop Entry]\nName=FL Studio 2026\nExec=env WINEPREFIX=/h/.wine wine "
                                        "\"C:\\\\\\\\FL64.exe\"\nStartupWMClass=fl64.exe\n" << false;
        QTest::newRow("text editor") << "[Desktop Entry]\nExec=kate %U\nMimeType=text/plain;text/x-c++src;\n" << false;
        QTest::newRow("type in an action") << "[Desktop Entry]\nExec=x\n[Desktop Action a]\n"
                                              "MimeType=application/x-ms-dos-executable;\n" << false;
    }

    void opens_windows_programs()
    {
        QFETCH(QString, file);
        QFETCH(bool, opener);
        QCOMPARE(opensWindowsPrograms(shortcutOf("x.desktop", file)), opener);
    }

    void shortcuts_for_data()
    {
        QTest::addColumn<QString>("appId");
        QTest::addColumn<QStringList>("paths");
        QTest::newRow("window class") << "notepad++.exe" << QStringList{"notepad"};
        QTest::newRow("class in other case") << "ModOrganizer.exe" << QStringList{"mo2"};
        QTest::newRow("exe opened by path") << "innyume_ver1.00.exe" << QStringList{"innyume"};
        QTest::newRow("two claim it") << "game.exe" << QStringList{"game", "game copy"};
        QTest::newRow("none") << "firefox" << QStringList{};
        QTest::newRow("only part of a name") << "pad++.exe" << QStringList{};
    }

    void shortcuts_for()
    {
        QFETCH(QString, appId);
        QFETCH(QStringList, paths);
        const QList<Shortcut> desktop{
            shortcutOf("notepad", "[Desktop Entry]\nExec=notepad++-open %f\nStartupWMClass=notepad++.exe\n"),
            shortcutOf("mo2", "[Desktop Entry]\nExec=mo2-fnv %u\nStartupWMClass=modorganizer.exe\n"),
            shortcutOf("innyume", "[Desktop Entry]\nExec=gio open \"/h/Games/Innyume/innyume_ver1.00.exe\"\n"),
            shortcutOf("game", "[Desktop Entry]\nExec=wine /h/a/Game.exe\n"),
            shortcutOf("game copy", "[Desktop Entry]\nExec=wine /h/b/game.exe\n"),
        };
        QStringList found;
        for (const Shortcut& shortcut : shortcutsFor(appId, desktop))
        {
            found << shortcut.path;
        }
        QCOMPARE(found, paths);
    }

    void program_of_maps_data()
    {
        QTest::addColumn<QString>("appId");
        QTest::addColumn<QString>("program");
        QTest::newRow("exe in lower case") << "setup-1.1.0.exe" << "/h/Games/Setup-1.1.0.exe";
        QTest::newRow("spaces in the path") << "microsoftedgeupdate.exe"
                                            << "/h/.wine/drive_c/Program Files (x86)/Edge/MicrosoftEdgeUpdate.exe";
        QTest::newRow("a dll of that name is no program") << "kernel32.exe" << "";
        QTest::newRow("not a windows program") << "firefox" << "";
        QTest::newRow("not mapped") << "other.exe" << "";
        QTest::newRow("no app id") << "" << "";
    }

    void program_of_maps()
    {
        QFETCH(QString, appId);
        QFETCH(QString, program);
        const QByteArray maps =
            "7f0000000000-7f0000001000 r--p 00000000 103:02 11 /opt/wine/lib/wine/x86_64-windows/kernel32.dll\n"
            "7f0000001000-7f0000002000 r--p 00000000 103:02 12 /usr/lib/firefox/firefox\n"
            "00400000-00401000 r--p 00000000 103:02 13 /h/Games/Setup-1.1.0.exe\n"
            "00401000-00402000 r-xp 00001000 103:02 13 /h/Games/Setup-1.1.0.exe\n"
            "00500000-00501000 r--p 00000000 103:02 14 "
            "/h/.wine/drive_c/Program Files (x86)/Edge/MicrosoftEdgeUpdate.exe\n"
            "7ffd00000000-7ffd00021000 rw-p 00000000 00:00 0 [stack]\n";
        QCOMPARE(programOfMaps(appId, maps), program);
    }
};

QTEST_APPLESS_MAIN(GameFactsTest)
#include "test-game-facts.moc"
