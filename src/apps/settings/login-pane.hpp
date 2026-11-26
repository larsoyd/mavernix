// login screen pane: appearance, transition, idle display sleep and polkit options

#pragma once

#include <QWidget>

class Choices;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class LoginPane : public QWidget
{
    Q_OBJECT

  public:
    explicit LoginPane(Choices& choices, QWidget* parent = nullptr);

  private:
    void refreshSystemStatus();
    void applySystemOptions();
    void resetToNeutral();
    void launchPreview();
    void updateCustomPictureLabel();

    Choices& choices;

    QLabel* sync_status = nullptr;
    QPushButton* preview_button = nullptr;
    QComboBox* bg_mode = nullptr;
    QWidget* custom_row = nullptr;
    QLabel* custom_label = nullptr;
    QWidget* blur_row = nullptr;
    QSlider* blur_slider = nullptr;

    // system-wide polkit controls when mavernix-greeter-admin is installed
    QCheckBox* sync_enable_box = nullptr;
    QComboBox* user_list_box = nullptr;
    QComboBox* default_session_box = nullptr;
    QCheckBox* power_buttons_box = nullptr;
    QPushButton* apply_system_button = nullptr;
    QPushButton* reset_system_button = nullptr;
    QLabel* system_note = nullptr;
    bool loaded_sync_enabled = false;
    bool admin_available = false;
};
