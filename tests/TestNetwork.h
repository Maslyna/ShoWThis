#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <cstring>

class TestReply : public QNetworkReply {
public:
    TestReply(const QNetworkRequest &request, const QByteArray &data, int status,
              bool automatic, QObject *parent)
        : QNetworkReply(parent), m_data(data), m_status(status) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        if (automatic)
            QTimer::singleShot(0, this, [this]() { complete(); });
    }

    void abort() override {
        if (isFinished())
            return;
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }

    void complete() {
        if (isFinished())
            return;
        if (m_status >= 400)
            setError(QNetworkReply::ContentAccessDenied, "test HTTP failure");
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    qint64 bytesAvailable() const override {
        return m_data.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *destination, qint64 maximum) override {
        const auto length = qMin(maximum, qint64(m_data.size() - m_offset));
        if (length <= 0)
            return -1;
        std::memcpy(destination, m_data.constData() + m_offset, size_t(length));
        m_offset += length;
        return length;
    }

private:
    QByteArray m_data;
    int m_status;
    qint64 m_offset = 0;
};

class TestNetwork : public QNetworkAccessManager {
public:
    QByteArray response;
    int status = 200;
    bool automatic = true;
    QList<QNetworkRequest> requests;
    QList<QByteArray> payloads;
    QPointer<TestReply> last_reply;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing_data) override {
        requests.append(request);
        payloads.append(outgoing_data ? outgoing_data->readAll() : QByteArray{});
        last_reply = new TestReply(request, response, status, automatic, this);
        return last_reply;
    }
};
