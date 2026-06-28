#include "SdrSource.h"

#include <QMetaType>

namespace zapiska {

SdrSource::SdrSource(QObject *parent)
    : QObject(parent)
{
}

SdrSource::~SdrSource() = default;

void registerSdrSourceMetaTypes()
{
    qRegisterMetaType<zapiska::SdrSourceState>("zapiska::SdrSourceState");
    qRegisterMetaType<zapiska::SdrSquelchMode>("zapiska::SdrSquelchMode");
    qRegisterMetaType<zapiska::SdrDeviceInfo>("zapiska::SdrDeviceInfo");
    qRegisterMetaType<zapiska::SdrChannelConfig>("zapiska::SdrChannelConfig");
    qRegisterMetaType<zapiska::SdrSourceConfig>("zapiska::SdrSourceConfig");
    qRegisterMetaType<zapiska::SdrChannelStats>("zapiska::SdrChannelStats");
    qRegisterMetaType<zapiska::SdrStreamStats>("zapiska::SdrStreamStats");
    qRegisterMetaType<zapiska::IqBlock>("zapiska::IqBlock");
    qRegisterMetaType<zapiska::SdrSpectrumFrame>("zapiska::SdrSpectrumFrame");
}

} // namespace zapiska
