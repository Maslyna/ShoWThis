#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <cstring>
#include <functional>

class TestReply : public QNetworkReply {
public:
    TestReply(const QNetworkRequest &request, const QByteArray &data, int status,
              bool automatic, QObject *parent, NetworkError network_error = NoError, qint64 declared_length = -1)
        : QNetworkReply(parent), m_data(data), m_status(status), m_network_error(network_error) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (declared_length >= 0)
            setHeader(QNetworkRequest::ContentLengthHeader, declared_length);
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
        if (m_network_error != NoError)
            setError(m_network_error, "test network failure");
        else if (m_status >= 400)
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
    NetworkError m_network_error;
    qint64 m_offset = 0;
};

class TestNetwork : public QNetworkAccessManager {
public:
    struct Response {
        QByteArray body;
        int status = 200;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        qint64 length = -1;
    };
    QByteArray response;
    int status = 200;
    bool automatic = true;
    QList<QNetworkRequest> requests;
    QList<QByteArray> payloads;
    QPointer<TestReply> last_reply;
    QList<QPointer<TestReply>> replies;
    std::function<Response(const QNetworkRequest &)> responder;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing_data) override {
        requests.append(request);
        payloads.append(outgoing_data ? outgoing_data->readAll() : QByteArray{});
        const auto result = responder ? responder(request) : Response{response, status};
        last_reply = new TestReply(request, result.body, result.status, automatic, this, result.error, result.length);
        replies.append(last_reply);
        return last_reply;
    }
};
