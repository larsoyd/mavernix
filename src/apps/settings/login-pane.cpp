#include "login-pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "choices.hpp"
#include "pane-parts.hpp"

static const QString greeter_table = QStringLiteral("greeter");
static const QString lock_table = QStringLiteral("lock");
static const QString preview_bin = QStringLiteral("/usr/lib/mavernix-greeter/preview-greeter");
static const QString admin_bin = QStringLiteral("/usr/bin/mavernix-greeter-admin");
static const QString greeter_bin = QStringLiteral("/usr/lib/mavernix-greeter/greeter-session");

LoginPane::LoginPane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };
    const auto greeter_val = [&choices](const char* key, const QVariant& fallback) {
        const QVariant v = choices.value(greeter_table, QLatin1String(key));
        return v.isValid() ? v : fallback;
    };

    const bool greeter_installed = QFileInfo::exists(greeter_bin);
    admin_available = QFileInfo::exists(admin_bin);

    auto* form = new PaneForm(this);

    // --- Status & Live Preview ------------------------------------------------
    sync_status = new QLabel(greeter_installed ? word("login_checking", "Checking login screen status…")
                                               : word("login_not_installed", "mavernix-greeter is not installed."));
    sync_status->setEnabled(greeter_installed);

    preview_button = new QPushButton(word("login_preview", "Preview…"));
    preview_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    preview_button->setEnabled(QFileInfo::exists(preview_bin));
    connect(preview_button, &QPushButton::clicked, this, &LoginPane::launchPreview);

    auto* header_row = new QWidget;
    auto* header_box = new QHBoxLayout(header_row);
    header_box->setContentsMargins(0, 0, 0, 0);
    header_box->setSpacing(10);
    header_box->addWidget(sync_status);
    header_box->addStretch();
    header_box->addWidget(preview_button);
    form->addRow(word("login_status", "Login screen:"), header_row);
    form->addDivider();

    // --- Appearance (User Choices -> inbox sync) ------------------------------
    bg_mode = new QComboBox;
    bg_mode->addItem(word("login_bg_desktop", "Same as Desktop"), QStringLiteral("desktop"));
    bg_mode->addItem(word("login_bg_blurred", "Blurred Desktop"), QStringLiteral("desktop_blurred"));
    bg_mode->addItem(word("login_bg_custom", "Custom Picture"), QStringLiteral("custom"));
    bg_mode->addItem(word("login_bg_gradient", "Classic Gradient"), QStringLiteral("gradient"));
    const QString current_mode = greeter_val("background_mode", QStringLiteral("desktop")).toString();
    bg_mode->setCurrentIndex(qMax(0, bg_mode->findData(current_mode)));
    bg_mode->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    form->addRow(word("login_background", "Background:"), bg_mode);

    custom_row = new QWidget;
    auto* custom_box = new QHBoxLayout(custom_row);
    custom_box->setContentsMargins(0, 0, 0, 0);
    custom_box->setSpacing(8);
    custom_label = new QLabel;
    custom_label->setEnabled(false);
    auto* choose_pic = new QPushButton(word("login_choose_picture", "Choose…"));
    choose_pic->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(choose_pic, &QPushButton::clicked, this, [this]() {
        const QString picked = QFileDialog::getOpenFileName(this,
            QStringLiteral("Choose Login Background"), QDir::homePath(),
            QStringLiteral("Pictures (*.png *.jpg *.jpeg *.webp)"));
        if (!picked.isEmpty())
        {
            this->choices.set(greeter_table, QStringLiteral("background_picture"), picked);
            updateCustomPictureLabel();
        }
    });
    custom_box->addWidget(choose_pic);
    custom_box->addWidget(custom_label);
    custom_box->addStretch();
    updateCustomPictureLabel();
    form->addRow(word("login_custom_picture", "Picture:"), custom_row);

    const int current_blur = greeter_val("background_blur", 28).toInt();
    blur_slider = stops_slider({8, 16, 24, 32, 44, 56}, current_blur,
        [&choices](double value) { choices.set(greeter_table, QStringLiteral("background_blur"), int(value)); });
    blur_row = with_end_words(blur_slider, word("login_blur_low", "Subtle"), word("login_blur_high", "Strong"));
    form->addRow(word("login_blur", "Blur strength:"), blur_row);

    const auto sync_bg_rows = [this, form]() {
        const QString mode = bg_mode->currentData().toString();
        form->setRowVisible(custom_row, mode == QLatin1String("custom"));
        form->setRowVisible(blur_row, mode == QLatin1String("desktop_blurred"));
    };
    sync_bg_rows();
    connect(bg_mode, &QComboBox::activated, this, [this, sync_bg_rows](int index) {
        this->choices.set(greeter_table, QStringLiteral("background_mode"), bg_mode->itemData(index));
        sync_bg_rows();
    });

    auto* follow_schedule = new QCheckBox(word("login_follow_schedule", "Follow day/night theme schedule"));
    follow_schedule->setChecked(greeter_val("follow_schedule", true).toBool());
    connect(follow_schedule, &QCheckBox::toggled, this,
        [&choices](bool on) { choices.set(greeter_table, QStringLiteral("follow_schedule"), on); });
    form->addCheck(follow_schedule);

    auto* show_clock = new QCheckBox(word("login_show_clock", "Show clock matching menu bar format"));
    show_clock->setChecked(greeter_val("show_clock", true).toBool());
    connect(show_clock, &QCheckBox::toggled, this, [&choices](bool on) {
        choices.set(greeter_table, QStringLiteral("show_clock"), on);
        choices.set(greeter_table, QStringLiteral("match_clock_format"), on);
    });
    form->addCheck(show_clock);

    auto* show_status = new QCheckBox(word("login_show_status", "Show keyboard layout and battery in top right"));
    show_status->setChecked(greeter_val("show_keyboard", true).toBool() || greeter_val("show_battery", true).toBool());
    connect(show_status, &QCheckBox::toggled, this, [&choices](bool on) {
        choices.set(greeter_table, QStringLiteral("show_keyboard"), on);
        choices.set(greeter_table, QStringLiteral("show_battery"), on);
    });
    form->addCheck(show_status);

    form->addDivider();

    // --- Transition, Lock Screen & Display Sleep ------------------------------
    auto* handoff = new QComboBox;
    handoff->addItem(word("login_handoff_choreographed", "Smooth & Animated"), QStringLiteral("choreographed"));
    handoff->addItem(word("login_handoff_simple", "Fade Only"), QStringLiteral("simple"));
    handoff->addItem(word("login_handoff_instant", "Instant"), QStringLiteral("instant"));
    const QString cur_handoff = greeter_val("handoff_style", QStringLiteral("choreographed")).toString();
    handoff->setCurrentIndex(qMax(0, handoff->findData(cur_handoff)));
    handoff->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(handoff, &QComboBox::activated, this, [&choices, handoff](int index) {
        choices.set(greeter_table, QStringLiteral("handoff_style"), handoff->itemData(index));
    });
    form->addRow(word("login_handoff", "Login transition:"), handoff);

    auto* lock_style = new QComboBox;
    lock_style->addItem(word("login_lock_greeter", "Floating (match login screen)"), QStringLiteral("greeter"));
    lock_style->addItem(word("login_lock_card", "Classic Card"), QStringLiteral("card"));
    const QVariant stored_lock = choices.value(lock_table, QStringLiteral("style"));
    const QString cur_lock = stored_lock.isValid() ? stored_lock.toString() : QStringLiteral("greeter");
    lock_style->setCurrentIndex(qMax(0, lock_style->findData(cur_lock)));
    lock_style->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(lock_style, &QComboBox::activated, this, [&choices, lock_style](int index) {
        choices.set(lock_table, QStringLiteral("style"), lock_style->itemData(index));
    });
    form->addRow(word("login_lock_style", "Lock screen style:"), lock_style);

    auto* dpms = new QComboBox;
    dpms->addItem(word("login_dpms_follow", "Follow Energy Saver"), -1);
    dpms->addItem(word("login_dpms_never", "Never"), 0);
    dpms->addItem(word("login_dpms_1m", "1 minute"), 60);
    dpms->addItem(word("login_dpms_5m", "5 minutes"), 300);
    dpms->addItem(word("login_dpms_10m", "10 minutes"), 600);
    dpms->addItem(word("login_dpms_15m", "15 minutes"), 900);
    dpms->addItem(word("login_dpms_30m", "30 minutes"), 1800);
    const QVariant dpms_sec = choices.value(greeter_table, QStringLiteral("screen_off_after_seconds"));
    const int cur_dpms = dpms_sec.isValid() ? dpms_sec.toInt() : -1;
    dpms->setCurrentIndex(qMax(0, dpms->findData(cur_dpms)));
    dpms->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(dpms, &QComboBox::activated, this, [&choices, dpms](int index) {
        const int secs = dpms->itemData(index).toInt();
        if (secs < 0)
        {
            choices.set(greeter_table, QStringLiteral("follow_idle_screen_off"), true);
            choices.remove({{greeter_table, QStringLiteral("screen_off_after_seconds")}});
        }
        else
        {
            choices.set(greeter_table, QStringLiteral("follow_idle_screen_off"), false);
            choices.set(greeter_table, QStringLiteral("screen_off_after_seconds"), secs);
        }
    });
    form->addRow(word("login_dpms", "Display sleep at login:"), dpms);

    // --- System-Wide Options (Polkit) -----------------------------------------
    if (admin_available)
    {
        form->addDivider();

        sync_enable_box = new QCheckBox(word("login_sync_enable", "Sync my look to the login screen"));
        form->addCheck(sync_enable_box);

        user_list_box = new QComboBox;
        user_list_box->addItem(word("login_users_list", "List of users"), true);
        user_list_box->addItem(word("login_users_name", "Name and password"), false);
        user_list_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        form->addRow(word("login_display_as", "Display login as:"), user_list_box);

        default_session_box = new QComboBox;
        default_session_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        form->addRow(word("login_default_session", "Default session:"), default_session_box);

        power_buttons_box = new QCheckBox(word("login_show_power", "Show Sleep, Restart and Shut Down buttons"));
        power_buttons_box->setChecked(true);
        form->addCheck(power_buttons_box);

        apply_system_button = new QPushButton(word("login_apply_system", "Apply System Options…"));
        apply_system_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(apply_system_button, &QPushButton::clicked, this, &LoginPane::applySystemOptions);

        reset_system_button = new QPushButton(word("login_reset_neutral", "Reset to Neutral…"));
        reset_system_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(reset_system_button, &QPushButton::clicked, this, &LoginPane::resetToNeutral);

        auto* admin_row = new QWidget;
        auto* admin_box = new QHBoxLayout(admin_row);
        admin_box->setContentsMargins(0, 0, 0, 0);
        admin_box->setSpacing(8);
        admin_box->addWidget(apply_system_button);
        admin_box->addWidget(reset_system_button);
        admin_box->addStretch();

        system_note = new QLabel(word("login_admin_note", "System options ask for your administrator password."));
        system_note->setFont(scaled_font(system_note->font(), small_words_scale));
        system_note->setEnabled(false);

        auto* admin_col = new QWidget;
        auto* admin_v = new QVBoxLayout(admin_col);
        admin_v->setContentsMargins(0, 0, 0, 0);
        admin_v->setSpacing(4);
        admin_v->addWidget(admin_row);
        admin_v->addWidget(system_note);
        form->addRow(QString(), admin_col);

        refreshSystemStatus();
    }
    else if (greeter_installed)
    {
        const QString user = qEnvironmentVariable("USER");
        const bool inbox_ok = !user.isEmpty() && QFileInfo(QStringLiteral("/var/lib/mavernix-greeter/inbox/") + user).isWritable();
        sync_status->setText(inbox_ok ? word("login_synced", "Look sync active")
                                      : word("login_not_synced", "Look sync not enabled"));
    }
}

void LoginPane::updateCustomPictureLabel()
{
    const QString path = choices.value(greeter_table, QStringLiteral("background_picture")).toString();
    if (path.isEmpty())
    {
        custom_label->setText(QStringLiteral("Using desktop picture"));
    }
    else
    {
        custom_label->setText(QFileInfo(path).fileName());
    }
}

void LoginPane::launchPreview()
{
    // export the freshest snapshot before launching the preview window
    if (!QStandardPaths::findExecutable(QStringLiteral("mavernix-reconfigure")).isEmpty())
    {
        QProcess::execute(QStringLiteral("mavernix-reconfigure"), {QStringLiteral("--greeter-look")});
    }
    QProcess::startDetached(preview_bin, {});
}

void LoginPane::refreshSystemStatus()
{
    if (!admin_available)
    {
        return;
    }
    auto* proc = new QProcess(this);
    const QString user = qEnvironmentVariable("USER");
    QStringList args = {QStringLiteral("status")};
    if (!user.isEmpty())
    {
        args << QStringLiteral("--user") << user;
    }
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, proc](int exitCode, QProcess::ExitStatus) {
        const QByteArray raw = proc->readAllStandardOutput();
        proc->deleteLater();
        if (exitCode != 0)
        {
            sync_status->setText(QStringLiteral("Could not read login screen status"));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        const QJsonObject cfg = obj.value(QStringLiteral("config")).toObject();
        const QJsonObject users = cfg.value(QStringLiteral("users")).toObject();
        const QJsonObject sess_cfg = cfg.value(QStringLiteral("sessions")).toObject();
        const QJsonObject power = cfg.value(QStringLiteral("power")).toObject();
        const QJsonObject installed = obj.value(QStringLiteral("installed_theme")).toObject();

        loaded_sync_enabled = obj.value(QStringLiteral("sync_enabled")).toBool();
        const QString theme_name = installed.value(QStringLiteral("name")).toString(QStringLiteral("Neutral"));
        sync_status->setText(loaded_sync_enabled ? QStringLiteral("Synced (%1)").arg(theme_name)
                                                 : QStringLiteral("Not synced (%1)").arg(theme_name));

        sync_enable_box->setChecked(loaded_sync_enabled);
        user_list_box->setCurrentIndex(qMax(0, user_list_box->findData(users.value(QStringLiteral("show_list")).toBool(true))));
        power_buttons_box->setChecked(power.value(QStringLiteral("show_buttons")).toBool(true));

        default_session_box->clear();
        const QString def_id = sess_cfg.value(QStringLiteral("default")).toString(QStringLiteral("mavernix"));
        for (const QJsonValue& val : obj.value(QStringLiteral("sessions")).toArray())
        {
            const QJsonObject s = val.toObject();
            default_session_box->addItem(s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("id")).toString());
        }
        if (default_session_box->count() == 0)
        {
            default_session_box->addItem(def_id, def_id);
        }
        default_session_box->setCurrentIndex(qMax(0, default_session_box->findData(def_id)));
    });
    proc->start(admin_bin, args);
}

void LoginPane::applySystemOptions()
{
    if (!admin_available)
    {
        return;
    }
    const QString user = qEnvironmentVariable("USER");
    QStringList set_args = {
        admin_bin, QStringLiteral("set"),
        QStringLiteral("--show-list"), user_list_box->currentData().toBool() ? QStringLiteral("true") : QStringLiteral("false"),
        QStringLiteral("--show-power"), power_buttons_box->isChecked() ? QStringLiteral("true") : QStringLiteral("false"),
    };
    const QString sess = default_session_box->currentData().toString();
    if (!sess.isEmpty())
    {
        set_args << QStringLiteral("--default-session") << sess;
    }

    const int rc = QProcess::execute(QStringLiteral("pkexec"), set_args);
    if (rc != 0)
    {
        if (rc != 126 && rc != 127)
        {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not update login screen configuration."));
        }
        refreshSystemStatus();
        return;
    }

    if (!user.isEmpty() && sync_enable_box->isChecked() != loaded_sync_enabled)
    {
        const QString action = sync_enable_box->isChecked() ? QStringLiteral("enable") : QStringLiteral("disable");
        const int sync_rc = QProcess::execute(QStringLiteral("pkexec"), {admin_bin, QStringLiteral("sync"), action, user});
        if (sync_rc == 0 && sync_enable_box->isChecked() &&
            !QStandardPaths::findExecutable(QStringLiteral("mavernix-reconfigure")).isEmpty())
        {
            QProcess::startDetached(QStringLiteral("mavernix-reconfigure"), {QStringLiteral("--greeter-look")});
        }
    }
    refreshSystemStatus();
}

void LoginPane::resetToNeutral()
{
    if (!admin_available)
    {
        return;
    }
    const int rc = QProcess::execute(QStringLiteral("pkexec"), {admin_bin, QStringLiteral("sync"), QStringLiteral("reset")});
    if (rc != 0 && rc != 126 && rc != 127)
    {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not reset login screen theme."));
    }
    refreshSystemStatus();
}
