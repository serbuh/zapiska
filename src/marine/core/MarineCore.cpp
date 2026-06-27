#include "MarineCore.h"

#include <QLocale>

namespace marine {

QVector<ChannelConfig> defaultChannels()
{
    return {
        {
            QStringLiteral("16"),
            QStringLiteral("Marine Channel 16"),
            156800000,
            QStringLiteral("nfm"),
            10000,
            true,
            true,
        },
    };
}

QString formatFrequencyMHz(qint64 frequencyHz)
{
    const double mhz = static_cast<double>(frequencyHz) / 1000000.0;
    return QLocale::c().toString(mhz, 'f', 3) + QStringLiteral(" MHz");
}

} // namespace marine

