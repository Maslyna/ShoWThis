#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
