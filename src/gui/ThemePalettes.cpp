#include "ThemePalettes.h"

namespace ThemePalettes {

QPalette getSakuraPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(255, 240, 245)); // LavenderBlush
    palette.setColor(QPalette::WindowText, QColor(90, 40, 60));
    palette.setColor(QPalette::Base, QColor(255, 250, 250)); // Snow
    palette.setColor(QPalette::AlternateBase, QColor(255, 230, 240));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 240, 245));
    palette.setColor(QPalette::ToolTipText, QColor(90, 40, 60));
    palette.setColor(QPalette::Text, QColor(60, 20, 40));
    palette.setColor(QPalette::Button, QColor(255, 220, 230));
    palette.setColor(QPalette::ButtonText, QColor(60, 20, 40));
    palette.setColor(QPalette::BrightText, QColor(150, 0, 50));
    palette.setColor(QPalette::Link, QColor(255, 105, 180)); // HotPink
    palette.setColor(QPalette::Highlight, QColor(255, 160, 180)); // Pink
    palette.setColor(QPalette::HighlightedText, QColor(60, 20, 40));
    palette.setColor(QPalette::PlaceholderText, QColor(120, 80, 100));
    palette.setColor(QPalette::Accent, QColor(255, 192, 203)); // Soft Pink for Windows 11 sliders/buttons
    return palette;
}

QString getSakuraStyleSheet() {
    return "QToolBar::separator { background-color: rgba(255, 182, 193, 150); width: 1px; height: 1px; margin: 2px; }";
}

QPalette getAmoledPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0, 0, 0));
    palette.setColor(QPalette::WindowText, QColor(169, 183, 198)); // #a9b7c6
    palette.setColor(QPalette::Base, QColor(0, 0, 0));
    palette.setColor(QPalette::AlternateBase, QColor(20, 20, 20));
    palette.setColor(QPalette::ToolTipBase, QColor(0, 0, 0));
    palette.setColor(QPalette::ToolTipText, QColor(230, 126, 34)); // #e67e22
    palette.setColor(QPalette::Text, QColor(169, 183, 198));
    palette.setColor(QPalette::Button, QColor(15, 15, 15));
    palette.setColor(QPalette::ButtonText, QColor(169, 183, 198));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(230, 126, 34)); // #e67e22
    palette.setColor(QPalette::Highlight, QColor(230, 126, 34)); // #e67e22
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Accent, QColor(230, 126, 34)); 
    return palette;
}

QString getAmoledStyleSheet() {
    return "QToolBar::separator { background-color: rgba(230, 126, 34, 150); width: 1px; height: 1px; margin: 2px; }";
}

QPalette getMaterialDarkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 29, 35)); // #1e1d23
    palette.setColor(QPalette::WindowText, QColor(235, 241, 249)); // #a9b7c6
    palette.setColor(QPalette::Base, QColor(38, 46, 57));
    palette.setColor(QPalette::AlternateBase, QColor(46, 55, 67));
    palette.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
    palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    palette.setColor(QPalette::Text, QColor(235, 241, 249));
    palette.setColor(QPalette::Button, QColor(38, 46, 57));
    palette.setColor(QPalette::ButtonText, QColor(235, 241, 249));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(4, 185, 127)); // #04b97f
    palette.setColor(QPalette::Highlight, QColor(4, 185, 127));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Accent, QColor(4, 185, 127));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(124, 137, 154));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(124, 137, 154));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(124, 137, 154));
    return palette;
}

QString getMaterialDarkStyleSheet() {
    // Consistent Qt widget colors on Windows: a dark palette alone can result
    // in native light popup menus with nearly white (apparently disabled) text.
    // Keep the actual :disabled style distinct from normal enabled actions.
    return QStringLiteral(R"QSS(
        QMenuBar { background-color:#202630; color:#EDF2FA; border-bottom:1px solid #3B4654; }
        QMenuBar::item { background:transparent; color:#EDF2FA; padding:5px 10px; }
        QMenuBar::item:selected, QMenuBar::item:pressed {
            background-color:#39485A; color:#FFFFFF; border-radius:4px;
        }
        QMenu { background-color:#232B36; color:#E8EEF8; border:1px solid #536175;
                padding:5px; }
        QMenu::item { color:#E8EEF8; background:transparent; min-height:22px;
                      padding:4px 30px 4px 25px; margin:1px 3px; }
        QMenu::item:selected { color:#FFFFFF; background:#34659B; border-radius:4px; }
        QMenu::item:disabled { color:#7B8799; }
        QMenu::item:disabled:selected { color:#8995A7; background:#2D3745; }
        QMenu::separator { height:1px; background:#475568; margin:5px 9px; }
        QMenu::icon { padding-left:5px; }
        /* Match the message-box background and text contrast. */
        QMessageBox { background-color:#232B36; color:#EDF2FA; }
        QMessageBox QLabel { color:#EDF2FA; background-color:transparent; }
        QMessageBox QPushButton { color:#FFFFFF; }
        QToolBar { background-color:#242B35; color:#E8EEF8;
                   border:0; spacing:4px; }
        QToolBar::separator { background-color:#475567; width:1px;
                              margin:4px 5px; }
        QToolButton { color:#EDF2FA; }
        QToolButton:hover { background-color:#3E5066; }
        QToolButton:checked { color:#FFFFFF; background-color:#275D93; }
        QToolButton:disabled { color:#7F8A99; }
        QPushButton { color:#EDF2FA; background-color:#354353;
                      border:1px solid #65768A; border-radius:5px;
                      padding:4px 9px; min-height:22px; }
        QPushButton:hover { background-color:#46617C; border-color:#8BA6C5; }
        QPushButton:pressed { background-color:#285C8E; }
        QPushButton:disabled { color:#8290A2; background-color:#2B3440;
                               border-color:#465465; }
        QComboBox { color:#EDF2FA; background-color:#334151;
                    border:1px solid #60728A; border-radius:4px;
                    padding:4px 8px; }
        QComboBox:disabled { color:#8490A0; background-color:#29333F; }
        QToolTip { color:#F5F7FB; background-color:#303D4E;
                   border:1px solid #68798E; padding:5px; }
    )QSS");
}

QPalette getNordPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(46, 52, 64)); // nord0
    palette.setColor(QPalette::WindowText, QColor(216, 222, 233)); // nord4
    palette.setColor(QPalette::Base, QColor(59, 66, 82)); // nord1
    palette.setColor(QPalette::AlternateBase, QColor(67, 76, 94)); // nord2
    palette.setColor(QPalette::ToolTipBase, QColor(76, 86, 106)); // nord3
    palette.setColor(QPalette::ToolTipText, QColor(229, 233, 240)); // nord5
    palette.setColor(QPalette::Text, QColor(216, 222, 233)); // nord4
    palette.setColor(QPalette::Button, QColor(67, 76, 94)); // nord2
    palette.setColor(QPalette::ButtonText, QColor(216, 222, 233)); // nord4
    palette.setColor(QPalette::BrightText, QColor(191, 97, 106)); // nord11 (red)
    palette.setColor(QPalette::Link, QColor(136, 192, 208)); // nord8
    palette.setColor(QPalette::Highlight, QColor(129, 161, 193)); // nord9
    palette.setColor(QPalette::HighlightedText, QColor(46, 52, 64)); // nord0
    palette.setColor(QPalette::Accent, QColor(129, 161, 193));
    return palette;
}

QString getNordStyleSheet() {
    return "QToolBar::separator { background-color: rgba(76, 86, 106, 150); width: 1px; height: 1px; margin: 2px; }";
}

QPalette getAiryPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(250, 252, 255)); // Very light, slightly cool white
    palette.setColor(QPalette::WindowText, QColor(60, 70, 80)); // Soft dark blue-gray text
    palette.setColor(QPalette::Base, QColor(255, 255, 255)); // Pure white
    palette.setColor(QPalette::AlternateBase, QColor(245, 248, 252)); // Airy light blue-gray
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(60, 70, 80));
    palette.setColor(QPalette::Text, QColor(40, 50, 60));
    palette.setColor(QPalette::Button, QColor(240, 246, 252)); // Airy light blue button
    palette.setColor(QPalette::ButtonText, QColor(50, 60, 70));
    palette.setColor(QPalette::BrightText, QColor(255, 100, 100));
    palette.setColor(QPalette::Link, QColor(100, 160, 255));
    palette.setColor(QPalette::Highlight, QColor(210, 230, 255)); // Airy blue highlight
    palette.setColor(QPalette::HighlightedText, QColor(30, 40, 50));
    palette.setColor(QPalette::Accent, QColor(150, 200, 255));
    return palette;
}

QString getAiryStyleSheet() {
    return "QToolBar::separator { background-color: rgba(200, 210, 220, 150); width: 1px; height: 1px; margin: 2px; }";
}

} // namespace ThemePalettes
