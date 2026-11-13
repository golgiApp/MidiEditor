#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void checkForUpdates();

signals:
    void updateAvailable(const QString &version, const QUrl &releaseUrl, const QString &releaseNotes);
    void noUpdateAvailable();
    void errorOccurred(QString error);

private:
    void onResult(QNetworkReply *reply);
    QNetworkAccessManager *manager;
};

#endif // UPDATECHECKER_H
