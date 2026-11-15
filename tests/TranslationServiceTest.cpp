#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "TestNetwork.h"
#include "TranslationService.h"
#include "LearningService.h"

// A keyless provider with a different protocol proves that neither credentials,
// JSON, POST, nor a context parameter are requirements of TranslationService.
class TestTranslator final : public showthis::TranslationProvider {
public:
    QString id() const override { return QStringLiteral("test"); }
    QString name() const override { return QStringLiteral("Test Translator"); }
    QVariantList configuration_fields() const override { return {}; }
    showthis::TranslationRequest make_request(const QString &word, const QString &context,
        const QString &source, const QString &target, const QVariantMap &) const override {
        QUrl url(QStringLiteral("https://translator.invalid/translate"));
        QUrlQuery query;
        query.addQueryItem("q", word);
        url.setQuery(query);
        QNetworkRequest request(url);
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
        QCOMPARE(service.provider_name(), QString("Off (manual translations)"));
        QCOMPARE(service.providers()[0].toMap().value("name").toString(), service.provider_name());
        service.translate("Haus", "Das Haus", "de", "ru");
        QCOMPARE(errors.size(), 1);
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!service.word_error().isEmpty());
        QVERIFY(service.context_error().isEmpty());
        service.translate_context("Das Haus", "de", "ru");
        QCOMPARE(errors.size(), 2);
        QVERIFY(!service.context_error().isEmpty());
        QVERIFY(network.requests.isEmpty());
        service.clear_word();
        QVERIFY(service.word_error().isEmpty());
        QVERIFY(!service.context_error().isEmpty());
        service.clear_context();
        QVERIFY(service.context_error().isEmpty());
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
        for (const auto *change : {"disable", "provider", "credentials", "plan", "forget"})
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
        QSignalSpy context_translated(&service, &showthis::TranslationService::context_translated);
        QSignalSpy session_translated(&service, &showthis::TranslationService::session_translated);
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        service.translate("Haus", "Das Haus", "de", "ru");
        auto pending_word = network.last_reply;
        service.translate_context("Das Haus", "de", "ru");
        QVERIFY(service.busy());
        auto pending = network.last_reply;
        service.translate_session("Independent passage", "de", "uk");
        auto pending_session = network.last_reply;
        QVERIFY(service.session_busy());
        if (change == "disable") service.set_provider_id("");
        else if (change == "provider") service.set_provider_id("libretranslate");
        else if (change == "credentials") service.set_option("api_key", "replacement");
        else if (change == "plan") service.set_option("plan", "pro");
        else service.forget_credentials();
        QVERIFY(!service.busy());
        QVERIFY(!service.session_busy());
        QVERIFY(service.session_text().isEmpty());
        QVERIFY(service.session_error().isEmpty());
        QVERIFY(!pending_session || pending_session->isFinished());
        QVERIFY(service.text().isEmpty());
        QVERIFY(!pending || pending->isFinished());
        QVERIFY(!pending_word || pending_word->isFinished());
        QVERIFY(service.context_text().isEmpty());
        QVERIFY(service.word_error().isEmpty());
        QVERIFY(service.context_error().isEmpty());
        if (pending_word) QMetaObject::invokeMethod(pending_word, "finished", Qt::DirectConnection);
        if (pending) pending->complete();
        if (pending_session) QMetaObject::invokeMethod(pending_session, "finished", Qt::DirectConnection);
        QCoreApplication::processEvents();
        QCOMPARE(translated.size(), 0);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(context_translated.size(), 0);
        QCOMPARE(session_translated.size(), 0);
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
        service->translate_context("Das Haus", "de", "ru");
        auto pending_context = network.last_reply;
        service->translate_session("Independent passage", "de", "uk");
        auto pending_session = network.last_reply;
        QVERIFY(service->session_busy());
        QVERIFY(service->busy());
        service.reset();
        QVERIFY(!pending || pending->isFinished());
        QVERIFY(!pending_context || pending_context->isFinished());
        QVERIFY(!pending_session || pending_session->isFinished());
        if (pending) QMetaObject::invokeMethod(pending, "finished", Qt::DirectConnection);
        if (pending_context) QMetaObject::invokeMethod(pending_context, "finished", Qt::DirectConnection);
        if (pending_session) QMetaObject::invokeMethod(pending_session, "finished", Qt::DirectConnection);
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
        QCOMPARE(service.word_error(), errors[0][0].toString());
        QVERIFY(service.context_error().isEmpty());
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
        QCOMPARE(service.word_error(), errors[0][0].toString());
        QVERIFY(service.context_error().isEmpty());
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
        QCOMPARE(service.providers().size(), 2); // Off plus the injected provider.
        service.set_provider_id("test");
        QVERIFY(service.configured());
        QVERIFY(!service.supports_context());
        QVERIFY(network.requests.isEmpty());
        service.translate("Haus", "private context", "de", "ru");
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.text(), QString("translated by another provider"));
        QCOMPARE(QUrlQuery(network.requests[0].url()).queryItemValue("q", QUrl::FullyDecoded), QString("Haus"));
        QCOMPARE(network.requests[0].rawHeader("X-Languages"), QByteArray("de:ru"));
        QVERIFY(network.requests[0].rawHeader("X-Context").isEmpty());
        QVERIFY(network.payloads[0].isEmpty());
    }

    void concurrent_channels_complete_out_of_order_data() {
        QTest::addColumn<bool>("context_first");
        QTest::newRow("context-first") << true;
        QTest::newRow("word-first") << false;
    }

    void concurrent_channels_complete_out_of_order() {
        QFETCH(bool, context_first);
        TestNetwork network;
        network.automatic = false;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy words(&service, &showthis::TranslationService::translated);
        QSignalSpy contexts(&service, &showthis::TranslationService::context_translated);
        network.response = R"({"translations":[{"text":"house"}]})";
        service.translate("Haus", "Das Haus ist gross.", "de", "en");
        auto word = network.last_reply;
        network.response = R"({"translations":[{"text":"The house is big."}]})";
        service.translate_context("Das Haus ist gross.", "de", "en");
        auto context = network.last_reply;
        QVERIFY(service.word_busy());
        QVERIFY(service.context_busy());
        QVERIFY(service.busy());
        const auto word_body = QJsonDocument::fromJson(network.payloads[0]).object();
        QCOMPARE(word_body.value("context").toString(), QString("Das Haus ist gross."));
        const auto context_body = QJsonDocument::fromJson(network.payloads[1]).object();
        QCOMPARE(context_body.value("text").toArray(), QJsonArray{"Das Haus ist gross."});
        QVERIFY(context_body.value("context").toString().isEmpty());
        (context_first ? context : word)->complete();
        QCOMPARE(service.word_busy(), context_first);
        QCOMPARE(service.context_busy(), !context_first);
        QVERIFY(service.busy());
        QCOMPARE(words.size(), context_first ? 0 : 1);
        QCOMPARE(contexts.size(), context_first ? 1 : 0);
        (context_first ? word : context)->complete();
        QVERIFY(!service.busy());
        QCOMPARE(words.size(), 1);
        QCOMPARE(contexts.size(), 1);
        QCOMPARE(service.text(), QString("house"));
        QCOMPARE(service.context_text(), QString("The house is big."));
        QVERIFY(service.word_error().isEmpty());
        QVERIFY(service.context_error().isEmpty());
    }

    void channel_cancel_and_replacement_are_isolated_data() {
        QTest::addColumn<bool>("context_channel");
        QTest::addColumn<bool>("replace");
        QTest::newRow("cancel-word") << false << false;
        QTest::newRow("cancel-context") << true << false;
        QTest::newRow("replace-word") << false << true;
        QTest::newRow("replace-context") << true << true;
    }

    void channel_cancel_and_replacement_are_isolated() {
        QFETCH(bool, context_channel);
        QFETCH(bool, replace);
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translations":[{"text":"original word"}]})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy words(&service, &showthis::TranslationService::translated);
        QSignalSpy contexts(&service, &showthis::TranslationService::context_translated);
        service.translate("Haus", "Das Haus", "de", "en");
        auto word = network.last_reply;
        network.response = R"({"translations":[{"text":"original context"}]})";
        service.translate_context("Das Haus", "de", "en");
        auto context = network.last_reply;
        auto cancelled = context_channel ? context : word;
        auto unaffected = context_channel ? word : context;
        if (replace) {
            network.response = R"({"translations":[{"text":"replacement"}]})";
            if (context_channel) service.translate_context("Neuer Kontext", "de", "en");
            else service.translate("Baum", "", "de", "en");
        } else {
            if (context_channel) service.clear_context();
            else service.clear_word();
        }
        QVERIFY(cancelled->isFinished());
        QVERIFY(!unaffected->isFinished());
        // Even an ill-behaved reply emitting finished again cannot publish stale data.
        QMetaObject::invokeMethod(cancelled, "finished", Qt::DirectConnection);
        QCOMPARE(words.size(), 0);
        QCOMPARE(contexts.size(), 0);
        unaffected->complete();
        QCOMPARE(context_channel ? service.text() : service.context_text(),
            context_channel ? QString("original word") : QString("original context"));
        if (replace) {
            QVERIFY(service.busy());
            network.last_reply->complete();
        }
        QVERIFY(!service.busy());
        QCOMPARE(context_channel ? service.context_text() : service.text(),
            replace ? QString("replacement") : QString{});
        QCOMPARE(words.size(), context_channel || replace ? 1 : 0);
        QCOMPARE(contexts.size(), !context_channel || replace ? 1 : 0);
    }

    void channel_errors_are_isolated_data() {
        QTest::addColumn<bool>("context_error");
        QTest::newRow("word-error") << false;
        QTest::newRow("context-error") << true;
    }

    void channel_errors_are_isolated() {
        QFETCH(bool, context_error);
        TestNetwork network;
        network.automatic = false;
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        QSignalSpy errors(&service, &showthis::TranslationService::error_occurred);
        network.status = context_error ? 200 : 403;
        network.response = R"({"translations":[{"text":"word result"}]})";
        service.translate("Haus", "", "de", "en");
        auto word = network.last_reply;
        network.status = context_error ? 403 : 200;
        network.response = R"({"translations":[{"text":"context result"}]})";
        service.translate_context("Das Haus", "de", "en");
        auto context = network.last_reply;
        (context_error ? context : word)->complete();
        QCOMPARE(errors.size(), 1);
        QVERIFY((context_error ? service.context_error() : service.word_error()).contains("API key"));
        QVERIFY((context_error ? service.word_error() : service.context_error()).isEmpty());
        (context_error ? word : context)->complete();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(context_error ? service.text() : service.context_text(),
            context_error ? QString("word result") : QString("context result"));
        // Retrying or clearing the successful channel does not hide the other error.
        if (context_error) service.clear_word();
        else service.clear_context();
        QVERIFY(!(context_error ? service.context_error() : service.word_error()).isEmpty());
        if (context_error) service.translate_context("Retry", "de", "en");
        else service.translate("Retry", "", "de", "en");
        QVERIFY(service.word_error().isEmpty());
        QVERIFY(service.context_error().isEmpty());
        service.clear();
    }

    void libretranslate_context_uses_primary_input() {
        TestNetwork network;
        network.response = R"({"translatedText":"The entire selected passage.\nA second sentence."})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("libretranslate");
        QVERIFY(!service.supports_context());
        const QString passage = "Die gesamte Auswahl.\nEin zweiter Satz.";
        QSignalSpy words(&service, &showthis::TranslationService::translated);
        QSignalSpy contexts(&service, &showthis::TranslationService::context_translated);
        service.translate_context(passage, "de", "en");
        QTRY_COMPARE(contexts.size(), 1);
        QCOMPARE(words.size(), 0);
        const auto payload = QJsonDocument::fromJson(network.payloads[0]).object();
        QCOMPARE(payload.value("q").toString(), passage);
        QCOMPARE(payload.value("source").toString(), QString("de"));
        QCOMPARE(payload.value("target").toString(), QString("en"));
        QVERIFY(!payload.contains("context"));
        QVERIFY(service.text().isEmpty());
        QCOMPARE(service.context_text(), QString("The entire selected passage.\nA second sentence."));
    }

    void session_survives_notebook_clear_and_draft_changes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translations":[{"text":"independent result"}]})";
        showthis::LearningService learning(nullptr, directory.filePath("notebook.json"), &network);
        auto *service = learning.translator();
        service->set_provider_id("deepl");
        service->set_option("api_key", "test-key");
        QSignalSpy sessions(service, &showthis::TranslationService::session_translated);
        service->translate("Haus", "Das Haus", "de", "en");
        auto word = network.last_reply;
        service->translate_context("Das Haus", "de", "en");
        auto context = network.last_reply;
        service->translate_session("Standalone text", "de", "uk");
        auto session = network.last_reply;
        QVERIFY(service->session_busy());
        QVERIFY(service->busy());
        learning.clear();
        QVERIFY(!service->busy());
        QVERIFY(word->isFinished());
        QVERIFY(context->isFinished());
        learning.prepare_draft({{"language", "fr"}, {"target_language", "en"}}, false);
        learning.set_source_language("it");
        learning.set_target_language("pl");
        QVERIFY(service->session_busy());
        QVERIFY(!session->isFinished());
        QCOMPARE(network.requests.size(), 3);
        session->complete();
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(service->session_text(), QString("independent result"));
        learning.clear();
        QCOMPARE(service->session_text(), QString("independent result"));
    }

    void session_cancel_and_replacement_are_isolated_data() {
        QTest::addColumn<bool>("replace");
        QTest::newRow("cancel") << false;
        QTest::newRow("replace") << true;
    }

    void session_cancel_and_replacement_are_isolated() {
        QFETCH(bool, replace);
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translations":[{"text":"notebook result"}]})";
        showthis::TranslationService service(nullptr, &network);
        service.set_provider_id("deepl");
        service.set_option("api_key", "test-key");
        service.translate("Haus", "Das Haus", "de", "en");
        auto word = network.last_reply;
        service.translate_context("Das Haus", "de", "en");
        auto context = network.last_reply;
        service.translate_session("Standalone", "de", "uk");
        auto cancelled = network.last_reply;
        QSignalSpy sessions(&service, &showthis::TranslationService::session_translated);
        if (replace) {
            network.response = R"({"translations":[{"text":"replacement"}]})";
            service.translate_session("Replacement", "fr", "pl");
        } else {
            service.clear_session();
        }
        QVERIFY(cancelled->isFinished());
        QVERIFY(!word->isFinished());
        QVERIFY(!context->isFinished());
        QMetaObject::invokeMethod(cancelled, "finished", Qt::DirectConnection);
        QCOMPARE(sessions.size(), 0);
        word->complete();
        context->complete();
        QVERIFY(!service.busy());
        QCOMPARE(service.text(), QString("notebook result"));
        QCOMPARE(service.context_text(), QString("notebook result"));
        QCOMPARE(service.session_busy(), replace);
        if (replace) network.last_reply->complete();
        QCOMPARE(sessions.size(), replace ? 1 : 0);
        QCOMPARE(service.session_text(), replace ? QString("replacement") : QString{});
        QVERIFY(service.session_error().isEmpty());
    }

    void session_errors_do_not_change_notebook_messages() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestNetwork network;
        network.automatic = false;
        network.status = 403;
        showthis::LearningService learning(nullptr, directory.filePath("notebook.json"), &network);
        auto *service = learning.translator();
        QSignalSpy errors(service, &showthis::TranslationService::error_occurred);
        service->translate_session("Standalone", "de", "uk");
        QVERIFY(!service->session_error().isEmpty());
        QCOMPARE(errors.size(), 0);
        service->set_provider_id("deepl");
        service->set_option("api_key", "test-key");
        const auto message = learning.message();
        service->translate_session("Standalone", "de", "uk");
        network.last_reply->complete();
        QVERIFY(service->session_error().contains("API key"));
        QVERIFY(service->session_text().isEmpty());
        QVERIFY(service->word_error().isEmpty());
        QVERIFY(service->context_error().isEmpty());
        QCOMPARE(learning.message(), message);
        QCOMPARE(errors.size(), 0);
        service->clear();
        QVERIFY(!service->session_error().isEmpty());
        service->clear_session();
        QVERIFY(service->session_error().isEmpty());
    }

    void session_uses_primary_text_data() {
        QTest::addColumn<QString>("provider");
        QTest::newRow("deepl") << QString("deepl");
        QTest::newRow("libretranslate") << QString("libretranslate");
        QTest::newRow("keyless-other-protocol") << QString("test");
    }

    void session_uses_primary_text() {
        QFETCH(QString, provider);
        TestNetwork network;
        network.response = provider == "deepl" ? QByteArray(R"({"translations":[{"text":"result"}]})")
            : provider == "libretranslate" ? QByteArray(R"({"translatedText":"result"})") : QByteArray("result");
        auto providers = showthis::default_translation_providers();
        providers.push_back(std::make_unique<TestTranslator>());
        showthis::TranslationService service(nullptr, &network, std::move(providers));
        service.set_provider_id(provider);
        if (provider == "deepl") service.set_option("api_key", "test-key");
        QSignalSpy sessions(&service, &showthis::TranslationService::session_translated);
        QSignalSpy words(&service, &showthis::TranslationService::translated);
        QSignalSpy contexts(&service, &showthis::TranslationService::context_translated);
        const QString passage = "Die gesamte Auswahl.\nEin zweiter Satz.";
        service.translate_session(passage, "de", "uk");
        QTRY_COMPARE(sessions.size(), 1);
        QVERIFY(!service.busy());
        QVERIFY(!service.session_busy());
        QCOMPARE(service.session_text(), QString("result"));
        QCOMPARE(words.size(), 0);
        QCOMPARE(contexts.size(), 0);
        QCOMPARE(network.requests.size(), 1);
        if (provider == "test") {
            QCOMPARE(QUrlQuery(network.requests[0].url()).queryItemValue("q", QUrl::FullyDecoded), passage);
            QVERIFY(network.requests[0].rawHeader("X-Context").isEmpty());
            QCOMPARE(network.requests[0].rawHeader("X-Languages"), QByteArray("de:uk"));
        } else {
            const auto payload = QJsonDocument::fromJson(network.payloads[0]).object();
            if (provider == "deepl") {
                QVERIFY(payload.value("context").toString().isEmpty());
                QCOMPARE(payload.value("text").toArray(), QJsonArray{passage});
                QCOMPARE(payload.value("source_lang").toString(), QString("DE"));
                QCOMPARE(payload.value("target_lang").toString(), QString("UK"));
            } else {
                QVERIFY(!payload.contains("context"));
                QCOMPARE(payload.value("q").toString(), passage);
                QCOMPARE(payload.value("source").toString(), QString("de"));
                QCOMPARE(payload.value("target").toString(), QString("uk"));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TranslationServiceTest)
#include "TranslationServiceTest.moc"
