#pragma once

#include "SdrSource.h"

#include <memory>

namespace marine {

class GrOsmoSdrSource : public SdrSource
{
    Q_OBJECT

public:
    explicit GrOsmoSdrSource(QObject *parent = nullptr);
    ~GrOsmoSdrSource() override;

    QString backendName() const override;
    QVector<SdrDeviceInfo> discoverDevices(QString *errorMessage = nullptr) override;
    bool open(const SdrSourceConfig &config, QString *errorMessage = nullptr) override;
    void close() override;
    bool start(QString *errorMessage = nullptr) override;
    void stop() override;
    bool setLiveAudioEnabled(bool enabled, QString *errorMessage = nullptr) override;
    bool liveAudioEnabled() const override;
    bool startRecording(const QString &channelId,
        const QString &filePath,
        QString *errorMessage = nullptr) override;
    void stopRecording() override;
    bool recording() const override;
    bool setChannelSquelch(const QString &channelId,
        SdrSquelchMode mode,
        double thresholdDbfs,
        QString *errorMessage = nullptr) override;
    SdrSourceState state() const override;
    SdrSourceConfig config() const override;
    SdrStreamStats stats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace marine
