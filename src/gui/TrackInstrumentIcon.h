#ifndef XIV_TRACK_INSTRUMENT_ICON_H
#define XIV_TRACK_INSTRUMENT_ICON_H

#include <QPainter>
#include <QPainterPath>

namespace xiv {

// Draw the instrument at the origin in a 36-by-36 logical-pixel box.
// Program numbers are zero-based and follow the XIV SoundFont assignments.
inline void drawTrackInstrumentIcon(QPainter &p, int program) {
    const QColor ink = p.pen().color();
    const QPen outline(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(outline);
    p.setBrush(Qt::NoBrush);
    QPainterPath shape;

    switch (program) {
    case 0: case 1: case 2: case 3:
        shape.moveTo(-16, 4);
        shape.lineTo(7, -13);
        shape.quadTo(13, -15, 11, -8);
        shape.quadTo(10, -5, 15, -3);
        shape.quadTo(17, -2, 17, 3);
        shape.lineTo(-16, 4);
        p.fillPath(shape, ink);
        p.drawRoundedRect(QRectF(-16, 5, 33, 4), 1, 1);
        p.drawLine(QPointF(-12, 9), QPointF(-12, 14));
        p.drawLine(QPointF(13, 9), QPointF(13, 14));
        break;
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
    case 40: case 41: case 42: case 43: case 45: case 110: {
        const bool bowed = program >= 40;
        p.rotate(38);
        shape.moveTo(-2, -4);
        shape.cubicTo(-9, -7, -10, 0, -7, 3);
        shape.cubicTo(-6, 5, -12, 6, -10, 12);
        shape.cubicTo(-8, 19, 8, 19, 10, 12);
        shape.cubicTo(12, 6, 6, 5, 7, 3);
        shape.cubicTo(10, 0, 9, -7, 2, -4);
        shape.closeSubpath();
        p.drawPath(shape);
        p.drawLine(QPointF(0, 3), QPointF(0, -16));
        p.drawRoundedRect(QRectF(-2.5, -18, 5, 5), 1, 1);
        if (bowed) {
            p.drawLine(QPointF(-4, 7), QPointF(4, 7));
            p.drawLine(QPointF(12, -15), QPointF(12, 11));
        } else {
            p.drawEllipse(QPointF(0, 7), 2.5, 2.5);
        }
        break;
    }
    case 46:
        shape.moveTo(-13, 13);
        shape.lineTo(-11, -14);
        shape.quadTo(-4, -17, 4, -9);
        shape.quadTo(10, -5, 15, -10);
        shape.lineTo(-10, 15);
        shape.closeSubpath();
        p.drawPath(shape);
        for (int i = 0; i < 4; ++i)
            p.drawLine(QPointF(-7 + 4 * i, -13 + 2 * i), QPointF(-8 + 4 * i, 11 - 4 * i));
        break;
    case 56: case 57: {
        p.rotate(-15);
        p.drawLine(QPointF(-17, 0), QPointF(8, 0));
        p.drawLine(QPointF(-17, -2), QPointF(-17, 2));
        shape.moveTo(-12, 0);
        shape.cubicTo(-15, 6, -11, 8, -7, 8);
        shape.lineTo(2, 8);
        shape.quadTo(9, 8, 7, 1);
        p.drawPath(shape);
        if (program == 56) {
            for (int x : {-7, -3, 1}) {
                p.drawLine(QPointF(x, -5), QPointF(x, 5));
                p.drawLine(QPointF(x - 1, -5), QPointF(x + 1, -5));
            }
        } else {
            p.drawLine(QPointF(-9, 4), QPointF(5, 4));
        }
        QPainterPath bell;
        bell.moveTo(4, -1);
        bell.quadTo(11, -2, 13, -8);
        bell.lineTo(13, 8);
        bell.quadTo(11, 2, 4, 1);
        p.fillPath(bell, ink);
        p.drawEllipse(QRectF(12, -8, 5, 16));
        break;
    }
    case 58:
        p.drawEllipse(QRectF(-3, -16, 18, 5));
        shape.moveTo(-2, -12);
        shape.quadTo(4, -7, 4, 4);
        shape.quadTo(4, 13, -3, 13);
        shape.quadTo(-10, 13, -10, 6);
        shape.lineTo(-10, -4);
        shape.quadTo(-10, -7, -6, -7);
        p.drawPath(shape);
        shape = QPainterPath();
        shape.moveTo(14, -12);
        shape.quadTo(8, -6, 8, 5);
        shape.cubicTo(8, 20, -15, 20, -15, 7);
        shape.lineTo(-15, -3);
        shape.quadTo(-15, -10, -7, -10);
        p.drawPath(shape);
        p.drawLine(QPointF(-6, -6), QPointF(-6, 5));
        p.drawLine(QPointF(-2, -5), QPointF(-2, 5));
        break;
    case 60:
        p.drawEllipse(QRectF(-13, -7, 21, 21));
        p.drawEllipse(QRectF(-9, -3, 13, 13));
        shape.moveTo(-10, -4);
        shape.quadTo(-11, -13, -4, -13);
        shape.lineTo(1, -13);
        shape.moveTo(5, -3);
        shape.quadTo(9, -3, 16, -9);
        shape.lineTo(16, 7);
        shape.quadTo(11, 3, 8, 3);
        p.drawPath(shape);
        break;
    case 64: case 65: case 66: case 67: {
        p.rotate(-18);
        // S-shaped neck, long keyed body and a J-shaped bow.
        shape.moveTo(-15, -13);
        shape.cubicTo(-12, -19, -5, -18, -5, -14);
        shape.quadTo(-5, -12, -7, -10);
        shape.lineTo(-7, 7);
        shape.cubicTo(-7, 18, 8, 18, 8, 7);
        shape.lineTo(8, 2);
        p.setPen(QPen(ink, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(shape);
        p.drawLine(QPointF(-15, -13), QPointF(-17, -12));
        QPainterPath bell;
        bell.moveTo(6, 5);
        bell.cubicTo(6, 0, 4, -3, 3, -5);
        bell.lineTo(15, -3);
        bell.quadTo(11, -1, 10, 6);
        bell.closeSubpath();
        p.fillPath(bell, ink);
        p.setPen(outline);
        p.setBrush(p.background().color());
        p.drawEllipse(QRectF(2, -7, 14, 4.5));
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        for (int y : {-7, -2, 3, 8}) p.drawEllipse(QPointF(-3.5, y), 1.15, 1.15);
        break;
    }
    case 68: case 69: case 70: case 71: case 72: case 73: case 74:
        p.rotate(-38);
        p.drawRoundedRect(QRectF(-17, -2.2, 34, 4.4), 1, 1);
        if (program >= 72) {
            for (int x : {-10, -5, 0, 5, 10}) p.drawEllipse(QPointF(x, -4), 1, 1);
        } else {
            p.drawLine(QPointF(11, -2), QPointF(17, -5));
            p.drawLine(QPointF(11, 2), QPointF(17, 5));
        }
        break;
    case 75:
        for (int i = 0; i < 6; ++i)
            p.drawRoundedRect(QRectF(-15 + 5 * i, -11, 4, 25 - 3 * i), 1, 1);
        p.drawLine(QPointF(-16, -4), QPointF(15, -4));
        break;
    case 47: case 98:
        p.drawEllipse(QRectF(-14, -9, 28, 9));
        shape.moveTo(-14, -4);
        if (program == 47) {
            shape.quadTo(-14, 12, 0, 12);
            shape.quadTo(14, 12, 14, -4);
        } else {
            shape.lineTo(-14, 7);
            shape.quadTo(0, 16, 14, 7);
            shape.lineTo(14, -4);
            p.drawLine(QPointF(-9, -13), QPointF(5, -4));
            p.drawLine(QPointF(8, -13), QPointF(-3, -4));
        }
        p.drawPath(shape);
        p.drawLine(QPointF(-8, 0), QPointF(-6, 9));
        p.drawLine(QPointF(8, 0), QPointF(6, 9));
        break;
    case 96:
        p.drawEllipse(QRectF(-17, -8, 14, 6));
        p.drawEllipse(QRectF(0, -10, 17, 7));
        p.drawPolyline(QPolygonF{QPointF(-17, -5), QPointF(-14, 9), QPointF(-6, 9), QPointF(-3, -5)});
        p.drawPolyline(QPolygonF{QPointF(0, -6), QPointF(3, 12), QPointF(14, 12), QPointF(17, -6)});
        break;
    case 97:
        p.drawEllipse(QRectF(-13, -14, 26, 26));
        p.drawEllipse(QRectF(-9, -10, 18, 18));
        p.drawLine(QPointF(-9, 9), QPointF(-14, 15));
        p.drawLine(QPointF(9, 9), QPointF(14, 15));
        break;
    case 99:
        p.drawEllipse(QRectF(-16, -6, 32, 12));
        p.drawLine(QPointF(0, -10), QPointF(0, 14));
        p.drawLine(QPointF(-7, 14), QPointF(7, 14));
        break;
    default:
        p.setBrush(ink);
        p.drawEllipse(QRectF(-10, 3, 10, 7));
        p.drawLine(QPointF(0, 6), QPointF(0, -13));
        p.drawLine(QPointF(0, -13), QPointF(9, -8));
        break;
    }
}

} // namespace xiv

#endif
