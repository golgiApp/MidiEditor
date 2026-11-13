#ifndef UPDATEAVAILABLEDIALOG_H
#define UPDATEAVAILABLEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class UpdateAvailableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UpdateAvailableDialog(const QString &latestVersion, const QString &currentVersion, const QString &changelogBody, QWidget *parent = nullptr);
};

#endif // UPDATEAVAILABLEDIALOG_H
