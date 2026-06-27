#include "SdrSource.h"

#include <QMetaType>

namespace marine {

SdrSource::SdrSource(QObject *parent)
    : QObject(parent)
{
}

SdrSource::~SdrSource() = default;

void registerSdrSourceMetaTypes()
{
    qRegisterMetaType<marine::SdrSourceState>("marine::SdrSourceState");
    qRegisterMetaType<marine::SdrDeviceInfo>("marine::SdrDeviceInfo");
    qRegisterMetaType<marine::SdrChannelConfig>("marine::SdrChannelConfig");
    qRegisterMetaType<marine::SdrSourceConfig>("marine::SdrSourceConfig");
    qRegisterMetaType<marine::SdrChannelStats>("marine::SdrChannelStats");
    qRegisterMetaType<marine::SdrStreamStats>("marine::SdrStreamStats");
    qRegisterMetaType<marine::IqBlock>("marine::IqBlock");
}

} // namespace marine
