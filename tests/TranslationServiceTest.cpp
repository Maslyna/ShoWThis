#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "TestNetwork.h"
#include "TranslationService.h"

// A keyless provider with a different protocol proves that neither credentials,
// JSON, POST, nor a context parameter are requirements of TranslationService.
class TestTranslator final : public showthis::TranslationProvider {
public:
    QString id() const override { return QStringLiteral("test"); }
    QString name() const override { return QStringLiteral("Test Translator"); }
    QVariantList configuration_fields() const override { return {}; }
    showthis::TranslationRequest make_request(const QString &word, const QString &context,
        const QString &source, const QString &target, const QVariantMap &) const override {
        QNetworkRequest request(QUrl(QStringLiteral("https://translator.invalid/translate")));
        request.setRawHeader("X-Word", word.toUtf8());
        request.setRawHeader("X-Context", context.toUtf8());
        request.setRawHeader("X-Languages", (source + ':' + target).toUtf8());
        return {request, "GET", {}};
    }
    showthis::TranslationResult parse_response(const QByteArray &body, int, QNetworkReply::NetworkError) const override {
        return {QString::fromUtf8(body), {}};
    }
};

class TranslationServiceTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;

private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings{}.clear(); }

    void disabled_and_incomplete_configuration() {
        TestNetwork network;
        showthis::TranslationService service(nullptr, &network);
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        QVERIFY(service.provider_id().isEmpty());
        QVERIFY(!service.configured());
        service.translate("Haus", "Das Haus", "de", "ru");
        QCOMPARE(errors.size(), 1);
        QVERIFY(network.requests.isEmpty());
        service.set_provider_id("deepl");
        QVERIFY(!service.configured());
        service.translate("Haus", "", "de", "ru");
        QVERIFY(network.requests.isEmpty());
        service.set_option("api_key", "   ");
        QVERIFY(!service.configured());
        service.set_option("api_key", "test-key");
        QVERIFY(service.configured());
        service.translate("  ", "", "de", "ru");
        QVERIFY(network.requests.isEmpty());
        service.set_provider_id("unknown");
        QCOMPARE(service.provider_id(), QString("deepl"));
        service.set_option("plan", "invalid");
        QCOMPARE(service.options().value("plan").toString(), QString("free"));
    }

    void configuration_does_not_send_requests_or_store_credentials() {
        TestNetwork network;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("plan", "pro");
        service.set_option("api_key", "private-test-key");
        QVERIFY(service.configured());
        QVERIFY(!service.options().contains("api_key"));
        QSettings settings;
        settings.sync();
        for (const auto &key : settings.allKeys())
            QVERIFY(!settings.value(key).toString().contains("private-test-key"));
        showthis::TranslationService reopened(nullptr, &network);
        QCOMPARE(reopened.provider_id(), QString("deepl"));
        QCOMPARE(reopened.options().value("plan").toString(), QString("pro"));
        QVERIFY(!reopened.configured());
        QVERIFY(network.requests.isEmpty());
        service.forget_credentials();
        QVERIFY(!service.configured());
    }

    void configuration_changes_cancel_pending_requests_data() {
        QTest::addColumn<QString>("change");
        for (const auto *change : {"disable", "credentials", "plan", "forget", "clear"})
            QTest::newRow(change) << QString::fromLatin1(change);
    }
    void configuration_changes_cancel_pending_requests() {
        QFETCH(QString, change);
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translations":[{"text":"stale"}]})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        service.translate("Haus", "Das Haus", "de", "ru");
        QVERIFY(service.busy());
        auto pending = network.last_reply;
        if (change == "disable") service.set_provider_id("");
        else if (change == "credentials") service.set_option("api_key", "replacement");
        else if (change == "plan") service.set_option("plan", "pro");
        else if (change == "forget") service.forget_credentials();
        else service.clear();
        QVERIFY(!service.busy());
        QVERIFY(service.text().isEmpty());
        QVERIFY(!pending || pending->isFinished());
        if (pending) pending->complete();
        QCoreApplication::processEvents();
        QCOMPARE(translated.size(), 0);
        QCOMPARE(errors.size(), 0);
    }

    void replacement_request_wins() {
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translations":[{"text":"new"}]})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        service.translate("first", "", "de", "ru");
        auto first = network.last_reply;
        service.translate("second", "", "de", "ru");
        QVERIFY(!first || first->isFinished());
        network.last_reply->complete();
        QCOMPARE(service.text(), QString("new"));
        QCOMPARE(translated.size(), 1);
    }

    void destruction_cancels_pending_requests() {
        TestNetwork network;
        network.automatic = false;
        auto service = std::make_unique<showthis::TranslationService>(nullptr, &network);
        service->set_provider_id("deepl");
        service->set_option("api_key", "test-key");
        service->translate("Haus", "", "de", "ru");
        auto pending = network.last_reply;
        QVERIFY(service->busy());
        service.reset();
        QVERIFY(!pending || pending->isFinished());
    }

    void provider_errors_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<QString>("message");
        QTest::newRow("key") << 403 << QByteArray{} << QString("API key");
        QTest::newRow("quota") << 456 << QByteArray{} << QString("quota");
        QTest::newRow("rate-limit") << 429 << QByteArray{} << QString("rate limit");
        QTest::newRow("languages") << 400 << QByteArray{} << QString("languages");
        QTest::newRow("unavailable") << 503 << QByteArray{} << QString("failed");
        QTest::newRow("invalid-json") << 200 << QByteArray("bad JSON") << QString("invalid response");
        QTest::newRow("invalid-schema") << 200 << QByteArray("{}") << QString("invalid response");
        QTest::newRow("empty") << 200 << QByteArray(R"({"translations":[]})") << QString("no translation");
    }
    void provider_errors() {
        QFETCH(int, status);
        QFETCH(QByteArray, response);
        QFETCH(QString, message);
        TestNetwork network;
        network.status = status;
        network.response = response;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        service.translate("Haus", "", "de", "ru");
        QTRY_VERIFY(!service.busy());
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0][0].toString().contains(message));
        QCOMPARE(translated.size(), 0);
        QVERIFY(service.text().isEmpty());
    }

    void libretranslate_request_data() {
        QTest::addColumn<QString>("base_url");
        QTest::addColumn<QString>("endpoint");
        QTest::addColumn<QString>("api_key");
        QTest::newRow("local-keyless") << QString("http://127.0.0.1:5000")
            << QString("http://127.0.0.1:5000/translate") << QString{};
        QTest::newRow("local-trailing-slash") << QString("http://localhost:5000/")
            << QString("http://localhost:5000/translate") << QString{};
        QTest::newRow("hosted-prefix-key") << QString("https://translator.invalid/libre")
            << QString("https://translator.invalid/libre/translate") << QString("session-secret");
        QTest::newRow("prefix-trailing-slash") << QString("https://translator.invalid/libre/")
            << QString("https://translator.invalid/libre/translate") << QString{};
    }

    void libretranslate_request() {
        QFETCH(QString, base_url);
        QFETCH(QString, endpoint);
        QFETCH(QString, api_key);
        TestNetwork network;
        network.response = R"({"translatedText":"будинок"})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        service.set_option("base_url", base_url);
        if (!api_key.isEmpty()) service.set_option("api_key", api_key);
        QVERIFY(service.configured());
        QVERIFY(!service.supports_context());
        QVERIFY(network.requests.isEmpty());
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        service.translate(" Haus ", "PRIVATE CONTEXT MUST NOT BE SENT", "de", "uk");
        QTRY_COMPARE(translated.size(), 1);
        QCOMPARE(service.text(), QString::fromUtf8("будинок"));
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests[0].url(), QUrl(endpoint));
        QCOMPARE(network.requests[0].header(QNetworkRequest::ContentTypeHeader).toString(), QString("application/json"));
        const auto body = QJsonDocument::fromJson(network.payloads[0]).object();
        QCOMPARE(body.value("q").toString(), QString("Haus"));
        QCOMPARE(body.value("source").toString(), QString("de"));
        QCOMPARE(body.value("target").toString(), QString("uk"));
        QCOMPARE(body.value("format").toString(), QString("text"));
        QCOMPARE(body.contains("api_key"), !api_key.isEmpty());
        if (!api_key.isEmpty()) QCOMPARE(body.value("api_key").toString(), api_key);
        QVERIFY(!body.contains("context"));
        QVERIFY(!network.payloads[0].contains("PRIVATE CONTEXT"));
    }

    void libretranslate_configuration_persists_without_credentials() {
        TestNetwork network;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "deepl-secret");
        service.set_provider_id("libretranslate");
        QVERIFY(service.configured()); // Local services need no API key.
        QCOMPARE(service.options().value("base_url").toString(), QString("http://127.0.0.1:5000"));
        service.set_option("base_url", "https://translator.invalid/prefix");
        service.set_option("api_key", "libre-secret");
        QVERIFY(!service.options().contains("api_key"));
        service.set_provider_id("deepl");
        QVERIFY(service.configured());
        service.set_provider_id("libretranslate");
        QCOMPARE(service.options().value("base_url").toString(), QString("https://translator.invalid/prefix"));
        service.forget_credentials();
        QVERIFY(service.configured()); // Forgetting an optional key does not disable local translation.
        service.set_option("api_key", "libre-secret");
        QSettings settings;
        settings.sync();
        for (const auto &key : settings.allKeys()) {
            QVERIFY(!settings.value(key).toString().contains("libre-secret"));
            QVERIFY(!settings.value(key).toString().contains("deepl-secret"));
        }
        showthis::TranslationService reopened(nullptr, &network);
        QCOMPARE(reopened.provider_id(), QString("libretranslate"));
        QVERIFY(reopened.configured());
        QCOMPARE(reopened.options().value("base_url").toString(), QString("https://translator.invalid/prefix"));
        network.response = R"({"translatedText":"house"})";
        reopened.translate("Haus", "private sentence", "de", "en");
        QTRY_VERIFY(!reopened.busy());
        QVERIFY(!QJsonDocument::fromJson(network.payloads[0]).object().contains("api_key"));
    }

    void libretranslate_invalid_server_url_data() {
        QTest::addColumn<QString>("url");
        for (const auto *url : {"", "localhost:5000", "/translate", "ftp://localhost:5000",
                 "http://name:password@localhost:5000", "http://localhost:5000?api_key=secret",
                 "http://localhost:5000/#fragment", "http://[::1"})
            QTest::newRow(*url ? url : "empty") << QString::fromLatin1(url);
    }

    void libretranslate_invalid_server_url() {
        QFETCH(QString, url);
        TestNetwork network;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        const auto previous = service.options();
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        service.set_option("base_url", url);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(service.options(), previous);
        QVERIFY(service.configured());
        QVERIFY(network.requests.isEmpty());
        QSettings settings;
        settings.setValue("translation/libretranslate/base_url", url);
        showthis::TranslationService invalid(nullptr, &network);
        QVERIFY(!invalid.configured());
        invalid.translate("Haus", "", "de", "en");
        QVERIFY(network.requests.isEmpty());
    }

    void libretranslate_endpoint_changes_cancel_pending_requests() {
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translatedText":"stale"})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        service.translate("Haus", "", "de", "en");
        auto pending = network.last_reply;
        QVERIFY(service.busy());
        service.set_option("base_url", "https://translator.invalid/other");
        QVERIFY(!service.busy());
        QVERIFY(!pending || pending->isFinished());
        if (pending) pending->complete();
        QCoreApplication::processEvents();
        QCOMPARE(translated.size(), 0);
        QVERIFY(service.text().isEmpty());
    }

    void libretranslate_errors_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("network_error");
        QTest::addColumn<QString>("message");
        const auto ok = int(QNetworkReply::NoError);
        QTest::newRow("language") << 400 << QByteArray(R"({"error":"de is not supported"})") << ok << QString("de is not supported");
        QTest::newRow("authentication") << 401 << QByteArray{} << ok << QString("API key");
        QTest::newRow("access") << 403 << QByteArray{} << ok << QString("API key");
        QTest::newRow("rate") << 429 << QByteArray{} << ok << QString("rate limit");
        QTest::newRow("server") << 500 << QByteArray(R"({"error":"Model is unavailable"})") << ok << QString("Model is unavailable");
        QTest::newRow("proxy") << 503 << QByteArray("<html>Unavailable</html>") << ok << QString("503");
        QTest::newRow("invalid-json") << 200 << QByteArray("not JSON") << ok << QString("invalid response");
        QTest::newRow("invalid-object") << 200 << QByteArray("[]") << ok << QString("invalid response");
        QTest::newRow("missing-text") << 200 << QByteArray("{}") << ok << QString("invalid response");
        QTest::newRow("array-text") << 200 << QByteArray(R"({"translatedText":["wrong shape"]})") << ok << QString("invalid response");
        QTest::newRow("empty-text") << 200 << QByteArray(R"({"translatedText":""})") << ok << QString("no translation");
        QTest::newRow("blank-text") << 200 << QByteArray(R"({"translatedText":" \n "})") << ok << QString("no translation");
        QTest::newRow("reported-error") << 200 << QByteArray(R"({"error":"Missing language model"})") << ok << QString("Missing language model");
        QTest::newRow("not-running") << 0 << QByteArray{} << int(QNetworkReply::ConnectionRefusedError) << QString("service is running");
        QTest::newRow("timeout") << 0 << QByteArray{} << int(QNetworkReply::TimeoutError) << QString("timed out");
    }

    void libretranslate_errors() {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(int, network_error);
        QFETCH(QString, message);
        TestNetwork network;
        network.responder = [&](const QNetworkRequest &) -> TestNetwork::Response {
            return {body, status, QNetworkReply::NetworkError(network_error)};
        };
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        service.translate("Haus", "", "de", "en");
        QTRY_VERIFY(!service.busy());
        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors[0][0].toString().contains(message), qPrintable(errors[0][0].toString()));
        QCOMPARE(translated.size(), 0);
        QVERIFY(service.text().isEmpty());
    }

    void libretranslate_real_http_and_redirect_privacy() {
        QTcpServer server;
        QTcpServer outside;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QVERIFY(outside.listen(QHostAddress::LocalHost));
        bool redirect = false;
        int outside_connections = 0;
        QList<QByteArray> requests;
        const QByteArray response = R"({"translatedText":"будинок"})";
        connect(&outside, &QTcpServer::newConnection, &outside, [&]() {
            ++outside_connections;
            auto *socket = outside.nextPendingConnection();
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(response.size())
                + "\r\nConnection: close\r\n\r\n" + response);
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes = QByteArray{}, handled = false]() mutable {
                if (handled) return;
                bytes += socket->readAll();
                const auto header_end = bytes.indexOf("\r\n\r\n");
                if (header_end < 0) return;
                int length = 0;
                for (const auto &line : bytes.first(header_end).split('\n')) {
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                }
                if (bytes.size() < header_end + 4 + length) return;
                handled = true;
                requests.append(bytes.first(header_end + 4 + length));
                if (redirect) {
                    socket->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:"
                        + QByteArray::number(outside.serverPort()) + "/translate\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
                }
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        service.set_option("base_url", QString("http://127.0.0.1:%1/services/libre/").arg(server.serverPort()));
        service.set_option("api_key", "transport-secret");
        QSignalSpy translated(&service, &showthis::TranslationService::translated);
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        service.translate("Haus", "PRIVATE CONTEXT", "de", "uk");
        QTRY_COMPARE(translated.size(), 1);
        QCOMPARE(service.text(), QString::fromUtf8("будинок"));
        QCOMPARE(requests.size(), 1);
        QVERIFY(requests[0].startsWith("POST /services/libre/translate HTTP/1.1\r\n"));
        const auto body = QJsonDocument::fromJson(requests[0].mid(requests[0].indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value("q").toString(), QString("Haus"));
        QCOMPARE(body.value("api_key").toString(), QString("transport-secret"));
        QVERIFY(!requests[0].contains("PRIVATE CONTEXT"));
        QCOMPARE(errors.size(), 0);
        redirect = true;
        service.translate("Haus", "PRIVATE CONTEXT", "de", "uk");
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!service.busy());
        QVERIFY(service.text().isEmpty());
        QCOMPARE(translated.size(), 1);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(outside_connections, 0); // Neither the word nor its key reaches another origin.
    }

    void alternate_keyless_provider() {
        TestNetwork network;
        network.response = "translated by another provider";
        showthis::TranslationProviders providers;
        providers.push_back(std::make_unique<TestTranslator>());
        showthis::TranslationService service(nullptr, &network, std::move(providers));
        QCOMPARE(service.providers().size(), 2); // None plus the injected provider.
        service.set_provider_id("test");
        QVERIFY(service.configured());
        QVERIFY(!service.supports_context());
        QVERIFY(network.requests.isEmpty());
        service.translate("Haus", "private context", "de", "ru");
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.text(), QString("translated by another provider"));
        QCOMPARE(network.requests[0].rawHeader("X-Word"), QByteArray("Haus"));
        QCOMPARE(network.requests[0].rawHeader("X-Languages"), QByteArray("de:ru"));
        QVERIFY(network.requests[0].rawHeader("X-Context").isEmpty());
        QVERIFY(network.payloads[0].isEmpty());
    }
};

QTEST_GUILESS_MAIN(TranslationServiceTest)
#include "TranslationServiceTest.moc"
