// which new windows are hidden at login, and when the wait for them ends

#include <QtTest>

#include "expectations.hpp"

using namespace hide_at_login;
using namespace std::chrono_literals;

namespace
{
const clock::time_point start = clock::time_point{} + 1h;

std::optional<std::string> none() { return std::nullopt; }
} // namespace

class TestHideAtLogin : public QObject
{
    Q_OBJECT

  private slots:
    void a_window_of_an_expected_program_is_hidden_once()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris", "lutris"}, start + 60s);
        QCOMPARE(expected.take("net.lutris.Lutris", start + 1s), std::optional<std::string>("net.lutris.Lutris"));
        QCOMPARE(expected.take("net.lutris.Lutris", start + 2s), none());
        QVERIFY(expected.empty());
    }

    void any_of_its_ids_finds_it_without_case()
    {
        expectations_t expected;
        expected.expect({"org.mozilla.Thunderbird", "thunderbird"}, start + 60s);
        QCOMPARE(expected.take("THUNDERBIRD", start), std::optional<std::string>("org.mozilla.Thunderbird"));

        expected.expect({"org.mozilla.Thunderbird", "thunderbird"}, start + 60s);
        QCOMPARE(expected.take("org.mozilla.thunderbird", start), std::optional<std::string>("org.mozilla.Thunderbird"));
    }

    void other_programs_are_left_alone()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris", "lutris"}, start + 60s);
        QCOMPARE(expected.take("org.kde.konsole", start), none());
        QCOMPARE(expected.take("lutri", start), none());
        QCOMPARE(expected.take("", start), none());
        QVERIFY(!expected.empty());
    }

    void each_program_is_taken_on_its_own()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris"}, start + 60s);
        expected.expect({"org.mozilla.Thunderbird"}, start + 60s);
        QCOMPARE(expected.take("org.mozilla.Thunderbird", start), std::optional<std::string>("org.mozilla.Thunderbird"));
        QVERIFY(!expected.empty());
        QCOMPARE(expected.take("net.lutris.Lutris", start), std::optional<std::string>("net.lutris.Lutris"));
        QVERIFY(expected.empty());
    }

    void a_late_window_is_not_hidden()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris"}, start + 60s);
        QCOMPARE(expected.take("net.lutris.Lutris", start + 60s), none());
        QCOMPARE(expected.take("net.lutris.Lutris", start + 61s), none());
    }

    void a_program_whose_time_ran_out_is_gone()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris"}, start + 10s);
        QCOMPARE(expected.take_expired(start + 10s).size(), size_t(1));
        QVERIFY(expected.empty());
    }

    void time_runs_out_for_each_program_by_its_own_deadline()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris"}, start + 10s);
        expected.expect({"org.mozilla.Thunderbird"}, start + 60s);
        QVERIFY(expected.take_expired(start + 9s).empty());
        QCOMPARE(expected.take_expired(start + 10s), std::vector<std::string>{"net.lutris.Lutris"});
        QVERIFY(expected.take_expired(start + 11s).empty());
        QVERIFY(!expected.empty());
        QCOMPARE(expected.take_expired(start + 60s), std::vector<std::string>{"org.mozilla.Thunderbird"});
        QVERIFY(expected.empty());
    }

    void expecting_a_program_again_moves_its_deadline()
    {
        expectations_t expected;
        expected.expect({"net.lutris.Lutris"}, start + 10s);
        expected.expect({"net.lutris.Lutris", "lutris"}, start + 30s);
        QVERIFY(expected.take_expired(start + 20s).empty());
        QCOMPARE(expected.take("lutris", start + 20s), std::optional<std::string>("net.lutris.Lutris"));
        QVERIFY(expected.empty());
    }

    void a_program_with_no_ids_is_not_kept()
    {
        expectations_t expected;
        expected.expect({}, start + 60s);
        expected.expect({""}, start + 60s);
        QVERIFY(expected.empty());
        QCOMPARE(expected.take("", start), none());
    }
};

QTEST_APPLESS_MAIN(TestHideAtLogin)
#include "test-hide-at-login.moc"
