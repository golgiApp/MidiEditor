#include "UpdateAvailableDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QApplication>
#include <QRegularExpression>
#include <QStyle>

UpdateAvailableDialog::UpdateAvailableDialog(const QString &latestVersion, const QString &currentVersion, const QString &changelogBody, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Update Available"));
    setMinimumWidth(500);
    setMinimumHeight(550);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(30, 30, 30, 30);

    // Header section
    QLabel *titleLabel = new QLabel(tr("A New Version is Available!"), this);
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    // Version info
    QString versionHtml = QString("<div style='font-size: 14px; line-height: 1.6;'>"
                                  "<b>Version %1</b> is now available.<br/>"
                                  "You are currently using version %2."
                                  "</div>").arg(latestVersion, currentVersion);
    QLabel *versionLabel = new QLabel(versionHtml, this);
    versionLabel->setTextFormat(Qt::RichText);
    mainLayout->addWidget(versionLabel);

    // What's New section
    QLabel *whatsNewLabel = new QLabel(tr("<b>What's New:</b>"), this);
    whatsNewLabel->setStyleSheet("font-size: 14px; margin-top: 5px;");
    mainLayout->addWidget(whatsNewLabel);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setBackgroundRole(QPalette::Base);
    scrollArea->setFrameShape(QFrame::StyledPanel);
    
    QWidget *scrollContent = new QWidget();
    scrollContent->setBackgroundRole(QPalette::Base);
    scrollContent->setAutoFillBackground(true);
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(15, 15, 15, 15);
    scrollLayout->setSpacing(10);

    // Clean up unsupported Markdown tags and fix italics
    QString cleanBody = changelogBody;
    cleanBody.remove(QRegularExpression("<details>\\s*"));
    cleanBody.remove(QRegularExpression("</details>\\s*"));
    cleanBody.remove(QRegularExpression("<summary>.*?</summary>\\s*"));
    cleanBody.replace(QRegularExpression("(?<!\\\\)_(.+?)(?<!\\\\)_"), "*\\1*");
    
    if (cleanBody.trimmed().isEmpty()) {
        cleanBody = tr("Release notes are available on GitHub.");
    }

    // Parse into collapsible sections
    QStringList lines = cleanBody.split('\n');
    QString currentHeader;
    QString currentBody;
    
    struct VersionEntry {
        QString header;
        QString body;
    };
    QList<VersionEntry> entries;
    
    QRegularExpression headerRegex("^#+\\s*(.*)");

    for (const QString& line : lines) {
        QRegularExpressionMatch match = headerRegex.match(line);
        if (match.hasMatch()) {
            if (!currentHeader.isEmpty() || !currentBody.trimmed().isEmpty()) {
                entries.append({currentHeader, currentBody.trimmed()});
                currentBody.clear();
            }
            currentHeader = match.captured(1);
        } else {
            currentBody += line + "\n";
        }
    }
    if (!currentHeader.isEmpty() || !currentBody.trimmed().isEmpty()) {
        entries.append({currentHeader, currentBody.trimmed()});
    }
    
    if (entries.isEmpty()) {
        entries.append({"Release Notes", cleanBody});
    }

    // Create accordion UI
    for (int i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        
        QPushButton *btn = new QPushButton();
        btn->setCheckable(true);
        bool isFirst = (i == 0);
        btn->setChecked(isFirst);
        btn->setText(" " + entry.header);
        btn->setIcon(qApp->style()->standardIcon(isFirst ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
        btn->setStyleSheet("text-align: left; font-weight: bold; padding: 5px;");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QLabel *lbl = new QLabel();
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        lbl->setTextFormat(Qt::MarkdownText);
        lbl->setWordWrap(true);
        lbl->setText(entry.body);
        lbl->setVisible(isFirst);
        lbl->setContentsMargins(20, 5, 0, 15);
        lbl->setForegroundRole(QPalette::Text);

        // Connect toggle
        connect(btn, &QPushButton::toggled, lbl, [btn, lbl](bool checked) {
            lbl->setVisible(checked);
            btn->setIcon(qApp->style()->standardIcon(checked ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
        });

        scrollLayout->addWidget(btn);
        scrollLayout->addWidget(lbl);
    }
    scrollLayout->addStretch();
    
    scrollArea->setWidget(scrollContent);
    scrollArea->setMinimumHeight(200);
    mainLayout->addWidget(scrollArea, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);

    QPushButton *releaseButton = new QPushButton(tr("Open Release on GitHub"), this);
    QPushButton *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);

    buttonLayout->addWidget(releaseButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(releaseButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
}
