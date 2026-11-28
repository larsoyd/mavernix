// what marks a window as a game, by its app id and what started it

#include <QtTest>

#include <string>

#include "game-marks.hpp"

using namespace game_window;

namespace
{
std::string environment(std::initializer_list<const char*> entries)
{
    std::string made;
    for (const char* entry : entries)
    {
        made += entry;
        made += '\0';
    }
    return made;
}
} // namespace

class TestGameWindow : public QObject
{
    Q_OBJECT

  private slots:
    void steam_marks_a_game_by_its_number()
    {
        QVERIFY(game_environment(environment({"HOME=/home/a", "SteamAppId=620"})));
        QVERIFY(game_environment(environment({"SteamGameId=489830"})));
        QVERIFY(game_environment(environment({"STEAM_COMPAT_APP_ID=1091500"})));
        QVERIFY(!game_environment(environment({"SteamAppId=0"})));
        QVERIFY(!game_environment(environment({"SteamGameId=abc"})));
        QVERIFY(!game_environment(environment({"SteamAppId="})));
    }

    void launchers_mark_a_game_by_any_value()
    {
        QVERIFY(game_environment(environment({"GAME_NAME=Celeste"})));
        QVERIFY(game_environment(environment({"LUTRIS_GAME_UUID=5f0c7c3e"})));
        QVERIFY(game_environment(environment({"UMU_ID=umu-default"})));
        QVERIFY(!game_environment(environment({"GAME_NAME="})));
        QVERIFY(!game_environment(environment({"LUTRIS_GAME_UUID="})));
    }

    void other_environments_are_no_game()
    {
        QVERIFY(!game_environment(environment({"PATH=/usr/bin", "STEAM_RUNTIME=1", "GAMENAME=x"})));
        QVERIFY(!game_environment(std::string()));
        // an entry without a value or an equals sign is skipped, not misread
        QVERIFY(!game_environment(environment({"SteamAppId", "=620"})));
    }

    void patterns_match_as_file_names_do()
    {
        QVERIFY(matches_any("steam_app_* Minecraft*", "steam_app_620"));
        QVERIFY(matches_any("steam_app_* Minecraft*", "minecraft 26.3"));
        QVERIFY(matches_any("  gamescope  ", "gamescope"));
        QVERIFY(!matches_any("steam_app_*", "steam"));
        QVERIFY(!matches_any("", "steam_app_620"));
        QVERIFY(!matches_any("steam_app_*", ""));
    }

    void the_marks_of_a_missing_process_are_no_game()
    {
        QVERIFY(!started_as_game(0));
        QVERIFY(!started_as_game(-1));
    }

    void a_running_process_is_read_from_its_environ()
    {
        check_started_by({QStringLiteral("SteamAppId=620")}, true);
        check_started_by({QStringLiteral("SteamAppId=0")}, false);
    }

  private:
    // a sleeping child with the given environment, read once it runs sleep itself
    void check_started_by(const QStringList& marks, bool game)
    {
        QProcess child;
        QProcessEnvironment env;
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        for (const QString& mark : marks)
        {
            env.insert(mark.section(QLatin1Char('='), 0, 0), mark.section(QLatin1Char('='), 1));
        }
        child.setProcessEnvironment(env);
        child.start(QStringLiteral("sleep"), {QStringLiteral("10")});
        QVERIFY2(child.waitForStarted(5000), "sleep did not start");
        const pid_t pid = pid_t(child.processId());
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        bool running = false;
        for (int tries = 0; tries < 200 && !running; tries++)
        {
            QTest::qWait(10);
            if (comm.open(QIODevice::ReadOnly))
            {
                running = comm.readAll().trimmed() == "sleep";
                comm.close();
            }
        }
        const bool found = started_as_game(pid);
        child.kill();
        child.waitForFinished(5000);
        QVERIFY2(running, "the child never ran sleep");
        QCOMPARE(found, game);
    }
};

QTEST_GUILESS_MAIN(TestGameWindow)
#include "test-game-window.moc"
