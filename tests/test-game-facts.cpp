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
};

QTEST_APPLESS_MAIN(GameFactsTest)
#include "test-game-facts.moc"
