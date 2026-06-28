#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

QPixmap noteIconPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    const qreal scale = static_cast<qreal>(size) / 64.0;
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF pageRect(13.0 * scale, 6.0 * scale, 38.0 * scale, 52.0 * scale);
    const qreal foldSize = 11.0 * scale;

    QPainterPath pagePath;
    pagePath.moveTo(pageRect.left(), pageRect.top());
    pagePath.lineTo(pageRect.right() - foldSize, pageRect.top());
    pagePath.lineTo(pageRect.right(), pageRect.top() + foldSize);
    pagePath.lineTo(pageRect.right(), pageRect.bottom());
    pagePath.lineTo(pageRect.left(), pageRect.bottom());
    pagePath.closeSubpath();

    painter.setPen(QPen(QColor(33, 74, 105), 3.0 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 249, 222));
    painter.drawPath(pagePath);

    QPainterPath foldPath;
    foldPath.moveTo(pageRect.right() - foldSize, pageRect.top());
    foldPath.lineTo(pageRect.right() - foldSize, pageRect.top() + foldSize);
    foldPath.lineTo(pageRect.right(), pageRect.top() + foldSize);
    foldPath.closeSubpath();

    painter.setPen(QPen(QColor(33, 74, 105), 2.0 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(221, 236, 243));
    painter.drawPath(foldPath);

    painter.setPen(QPen(QColor(31, 132, 155), 3.0 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(22.0 * scale, 25.0 * scale), QPointF(42.0 * scale, 25.0 * scale));
    painter.drawLine(QPointF(22.0 * scale, 34.0 * scale), QPointF(42.0 * scale, 34.0 * scale));
    painter.drawLine(QPointF(22.0 * scale, 43.0 * scale), QPointF(35.0 * scale, 43.0 * scale));

    return pixmap;
}

QIcon noteAppIcon()
{
    QIcon icon;
    for (const int size : { 16, 24, 32, 48, 64, 128 }) {
        icon.addPixmap(noteIconPixmap(size));
    }
    return icon;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Zapiska"));
    QApplication::setApplicationName(QStringLiteral("Zapiska Marine Recorder"));
    const QIcon appIcon = noteAppIcon();
    QApplication::setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}
