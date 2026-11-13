#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
    manager = new QNetworkAccessManager(this);
}

void UpdateChecker::checkForUpdates()
{
    QNetworkRequest request(QUrl(QStringLiteral(
        "https://api.github.com/repos/golgiApp/XIVMIDIStudio/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XIV MIDI Studio"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onResult(reply);
    });
}

void UpdateChecker::onResult(QNetworkReply *reply)
{
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404) {
        emit noUpdateAvailable();
        return;
    }
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        return;
    }

    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value("tag_name").toString();
    static const QRegularExpression stableTag(QStringLiteral("\\Av?([0-9]+(?:\\.[0-9]+){2,3})\\z"));
    const auto match = stableTag.match(tag);
    const QString version = match.captured(1);
    const QUrl releaseUrl(QStringLiteral("https://github.com/golgiApp/XIVMIDIStudio/releases/tag/") + tag);
    if (!match.hasMatch() || QUrl(release.value("html_url").toString()) != releaseUrl ||
        release.value("draft").toBool() || release.value("prerelease").toBool()) {
        emit errorOccurred(tr("GitHub did not return an official XIV MIDI Studio release."));
        return;
    }

    const auto latest = QVersionNumber::fromString(version);
    const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
    if (latest.normalized() <= current.normalized()) {
        emit noUpdateAvailable();
        return;
    }

    emit updateAvailable(version, releaseUrl, release.value("body").toString());
}
