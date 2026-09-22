#ifndef ASCREENCAPTUREBACKEND_H
#define ASCREENCAPTUREBACKEND_H

#include <QObject>

class AScreenCaptureBackend : public QObject
{
    Q_OBJECT

public:
    explicit AScreenCaptureBackend(QObject *parent = nullptr);
    ~AScreenCaptureBackend() override = default;

    virtual void capture() = 0;

signals:
    void captured(const QImage &image);
    void capture_failed(const QString &reason);
};

#endif // ASCREENCAPTUREBACKEND_H
