/*
 * LmmsStyle.cpp - the graphical style used by LMMS to create a consistent
 *				  interface
 *
 * Copyright (c) 2007-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <array>

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>  // IWYU pragma: keep
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleOption>

#include "ConfigManager.h"
#include "embed.h"
#include "LmmsPalette.h"
#include "LmmsStyle.h"
#include "TextFloat.h"

namespace lmms::gui
{
namespace
{

//! The default accent colour (KitsuneTone light blue / 水色).
QColor defaultAccentColor() { return QColor(0x5e, 0xcf, 0xe8); }

//! The configured accent colour, or the default light blue.
QColor themeAccentColor()
{
	const QString a = ConfigManager::inst()->value("theme", "accent", QString());
	const QColor c(a);
	return (a.isEmpty() || !c.isValid()) ? defaultAccentColor() : c;
}

//! The green accent family used throughout the default theme. Deliberately
//! broad: it also catches greenish-dark/near-black tints (low saturation and/or
//! low brightness) so nothing keeps a green cast. The blue-grey UI backgrounds
//! sit at hue ~200-230 and are left untouched.
bool isAccentGreen(int h, int s, int v)
{
	return h >= 85 && h <= 175 && s > 10 && v > 5;
}

//! A blue-tinted neutral (the dark UI backgrounds sit at hue ~180-270 with a
//! modest blue cast); these are flattened to neutral grey.
bool isBlueNeutral(int h, int s, int v)
{
	return h >= 180 && h <= 270 && s >= 8 && s <= 90 && v > 0;
}

//! Map one colour through the theme: rotate green accents to \a accentHue (when
//! set), and strip the blue cast from neutral backgrounds to a plain grey.
QColor mapThemeColor(const QColor& c, int accentHue)
{
	int h, s, v, a;
	c.getHsv(&h, &s, &v, &a);
	if (h >= 0 && isAccentGreen(h, s, v))
	{
		return accentHue >= 0 ? QColor::fromHsv(accentHue, s, v) : c;
	}

	// Neutral / near-neutral colours (backgrounds, panels, borders): strip any
	// blue cast and pull the dark ones much closer to black, for a deep dark UI.
	// Bright neutrals (text, highlights) are left alone.
	const bool blueNeutral = (h >= 0 && isBlueNeutral(h, s, v));
	if (blueNeutral || s < 40)
	{
		const int newS = blueNeutral ? 0 : s;
		int newV = v;
		if (v < 130)
		{
			newV = static_cast<int>(v * 0.4);  // darken backgrounds/panels
		}
		if (newS != s || newV != v)
		{
			return QColor::fromHsv(h < 0 ? 0 : h, newS, newV);
		}
	}
	return c;
}

//! Re-hue an image's green accent pixels to \a targetHue in place; returns true
//! if any pixel was changed.
bool tintImageGreens(QImage& img, int targetHue)
{
	img = img.convertToFormat(QImage::Format_ARGB32);
	bool changed = false;
	for (int y = 0; y < img.height(); ++y)
	{
		auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < img.width(); ++x)
		{
			const QColor c = QColor::fromRgba(line[x]);
			int h, s, v, a;
			c.getHsv(&h, &s, &v, &a);
			if (c.alpha() > 0 && h >= 0 && isAccentGreen(h, s, v))
			{
				QColor nc = QColor::fromHsv(targetHue, s, v);
				nc.setAlpha(c.alpha());
				line[x] = nc.rgba();
				changed = true;
			}
		}
	}
	return changed;
}

//! Tint the green pixmaps referenced by the stylesheet's url("resources:*.png")
//! rules (slider handles etc.) to the accent, rewriting the urls to cached,
//! re-hued copies. Non-green images are left pointing at the original.
QString tintUrlImages(const QString& css, int targetHue)
{
	static const QRegularExpression urlRe(
			QStringLiteral("url\\(\\s*\"resources:([^\"]+\\.png)\"\\s*\\)"));
	const QString cacheDir = QDir::tempPath()
			+ QStringLiteral("/kitsunetone_theme/h") + QString::number(targetHue);
	QDir().mkpath(cacheDir);

	QHash<QString, QString> rewritten;  // original name -> replacement url or ""
	QString out;
	out.reserve(css.size());
	qsizetype last = 0;
	auto it = urlRe.globalMatch(css);
	while (it.hasNext())
	{
		const auto match = it.next();
		const QString name = match.captured(1);
		out += css.mid(last, match.capturedStart() - last);
		last = match.capturedEnd();

		if (!rewritten.contains(name))
		{
			QString replacement;
			const QString outPath = cacheDir + QLatin1Char('/') + name;
			if (QFile::exists(outPath))
			{
				replacement = QStringLiteral("url(\"%1\")").arg(outPath);
			}
			else
			{
				QImage img(QStringLiteral("resources:") + name);
				if (!img.isNull() && tintImageGreens(img, targetHue) && img.save(outPath, "PNG"))
				{
					replacement = QStringLiteral("url(\"%1\")").arg(outPath);
				}
			}
			rewritten.insert(name, replacement);
		}

		const QString& replacement = rewritten[name];
		out += replacement.isEmpty() ? match.captured(0) : replacement;
	}
	out += css.mid(last);
	return out;
}

//! Apply the accent colour to a stylesheet by rewriting its colour literals.
//! #rrggbb, rgb(...) and rgba(...) are all handled.
QString recolorAccentCss(const QString& css)
{
	const QString accentStr = ConfigManager::inst()->value("theme", "accent", QString());
	// empty = the default light-blue accent
	const QColor accent = accentStr.isEmpty() ? defaultAccentColor() : QColor(accentStr);
	const bool hasAccent = accent.isValid() && accent.hue() >= 0;
	const int accentHue = hasAccent ? accent.hue() : -1;

	static const QRegularExpression colorRe(QStringLiteral(
			"#[0-9a-fA-F]{6}\\b|rgba?\\(\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*\\d+\\s*(?:,\\s*\\d+\\s*)?\\)"));

	QString out;
	out.reserve(css.size());
	qsizetype last = 0;
	auto it = colorRe.globalMatch(css);
	while (it.hasNext())
	{
		const auto match = it.next();
		const QString token = match.captured(0);
		out += css.mid(last, match.capturedStart() - last);
		last = match.capturedEnd();

		QColor c;
		bool isRgbFunc = false;
		int alpha255 = 255;
		if (token.startsWith(QLatin1Char('#'))) { c = QColor(token); }
		else
		{
			isRgbFunc = true;
			static const QRegularExpression num(QStringLiteral("\\d+"));
			auto ni = num.globalMatch(token);
			QList<int> vals;
			while (ni.hasNext()) { vals << ni.next().captured(0).toInt(); }
			if (vals.size() >= 3)
			{
				c = QColor(vals[0], vals[1], vals[2]);
				if (vals.size() >= 4) { alpha255 = vals[3]; }
			}
		}

		if (!c.isValid()) { out += token; continue; }
		const QColor nc = mapThemeColor(c, accentHue);
		if (nc.rgb() == c.rgb()) { out += token; continue; }
		if (isRgbFunc)
		{
			out += token.startsWith(QLatin1String("rgba"))
					? QStringLiteral("rgba(%1,%2,%3,%4)").arg(nc.red()).arg(nc.green()).arg(nc.blue()).arg(alpha255)
					: QStringLiteral("rgb(%1,%2,%3)").arg(nc.red()).arg(nc.green()).arg(nc.blue());
		}
		else { out += nc.name(); }
	}
	out += css.mid(last);
	// also re-hue the green url("resources:*.png") images (slider handles, ...)
	return accentHue >= 0 ? tintUrlImages(out, accentHue) : out;
}

} // namespace
} // namespace lmms::gui


namespace lmms::gui
{


QPalette * LmmsStyle::s_palette = nullptr;

QLinearGradient getGradient( const QColor & _col, const QRectF & _rect )
{
	QLinearGradient g( _rect.topLeft(), _rect.bottomLeft() );

	qreal hue = _col.hueF();
	qreal value = _col.valueF();
	qreal saturation = _col.saturationF();

	QColor c = _col;
	c.setHsvF( hue, 0.42 * saturation, 0.98 * value ); // TODO: MIDI clip: 1.08
	g.setColorAt( 0, c );
	c.setHsvF( hue, 0.58 * saturation, 0.95 * value ); // TODO: MIDI clip: 1.05
	g.setColorAt( 0.25, c );
	c.setHsvF( hue, 0.70 * saturation, 0.93 * value ); // TODO: MIDI clip: 1.03
	g.setColorAt( 0.5, c );

	c.setHsvF( hue, 0.95 * saturation, 0.9 * value );
	g.setColorAt( 0.501, c );
	c.setHsvF( hue * 0.95, 0.95 * saturation, 0.95 * value );
	g.setColorAt( 0.75, c );
	c.setHsvF( hue * 0.90, 0.95 * saturation, 1 * value );
	g.setColorAt( 1.0, c );

	return g;
}



QLinearGradient darken( const QLinearGradient & _gradient )
{
	QGradientStops stops = _gradient.stops();
	for (auto& stop : stops)
	{
		QColor color = stop.second;
		stop.second = color.lighter(133);
	}

	QLinearGradient g = _gradient;
	g.setStops(stops);
	return g;
}



void drawPath( QPainter *p, const QPainterPath &path,
			   const QColor &col, const QColor &borderCol,
			   bool dark = false )
{
	const QRectF pathRect = path.boundingRect();

	const QLinearGradient baseGradient = getGradient(col, pathRect);
	const QLinearGradient darkGradient = darken(baseGradient);

	p->setOpacity(0.25);

	// glow
	if (dark)
		p->strokePath(path, QPen(darkGradient, 4));
	else
		p->strokePath(path, QPen(baseGradient, 4));

	p->setOpacity(1.0);

	// fill
	if (dark)
		p->fillPath(path, darkGradient);
	else
		p->fillPath(path, baseGradient);

	// TODO: Remove??
	/*
	QLinearGradient g(pathRect.topLeft(), pathRect.topRight());
	g.setCoordinateMode(QGradient::ObjectBoundingMode);

	p->setOpacity(0.2);
	p->fillPath(path, g);*/
	// END: Remove??

	p->setOpacity(0.5);

	// highlight (pattern)
	if (dark)
		p->strokePath(path, QPen(borderCol.lighter(133), 2));
	else
		p->strokePath(path, QPen(borderCol, 2));
}



LmmsStyle::LmmsStyle() :
	QProxyStyle()
{
	QFile file( "resources:style.css" );
	file.open( QIODevice::ReadOnly );
	qApp->setStyleSheet( recolorAccentCss( QString::fromUtf8( file.readAll() ) ) );

	m_styleReloader.addPath(QFileInfo{file}.absoluteFilePath());
	connect(&m_styleReloader, &QFileSystemWatcher::fileChanged, this,
		[this](const QString& path)
		{
			if (auto file = QFile{path}; file.exists())
			{
				file.open(QIODevice::ReadOnly);
				qApp->setStyleSheet(recolorAccentCss(QString::fromUtf8(file.readAll())));
				TextFloat::displayMessage(
					tr("Theme updated"),
					tr("LMMS theme file %1 has been reloaded.").arg(file.fileName()),
					embed::getIconPixmap("colorize"),
					3000
				);
				// Handle delete + overwrite events
				if (!m_styleReloader.files().contains(path))
				{
					m_styleReloader.addPath(path);
				}
			}
		}
	);

	if( s_palette != nullptr ) { qApp->setPalette( *s_palette ); }

	setBaseStyle( QStyleFactory::create( "Fusion" ) );
}




void LmmsStyle::applyTheme()
{
	// re-read the theme, apply the chosen accent, and rebuild the palette so a
	// theme change from the settings takes effect without a restart
	QFile file( "resources:style.css" );
	if( !file.open( QIODevice::ReadOnly ) ) { return; }
	qApp->setStyleSheet( recolorAccentCss( QString::fromUtf8( file.readAll() ) ) );

	auto* lmmsPalette = new LmmsPalette( nullptr, QApplication::style() );
	const QPalette palette = lmmsPalette->palette();
	delete lmmsPalette;
	qApp->setPalette( palette );
	if( s_palette != nullptr ) { *s_palette = palette; }
}




QColor LmmsStyle::accentColor()
{
	return themeAccentColor();
}




QPixmap LmmsStyle::tintAccentPixmap(const QPixmap& pixmap)
{
	// re-hue the green accent pixels of an artwork pixmap (LCD digits, fader
	// knob, send indicator, ...) to the theme accent, so pixmap-based greens
	// follow the chosen theme. No-op when no accent is configured.
	const QString accent = ConfigManager::inst()->value("theme", "accent", QString());
	const QColor target = accent.isEmpty() ? defaultAccentColor() : QColor(accent);
	const int targetHue = target.hue();
	if (!target.isValid() || targetHue < 0) { return pixmap; }

	QImage img = pixmap.toImage();
	return tintImageGreens(img, targetHue) ? QPixmap::fromImage(img) : pixmap;
}




QPalette LmmsStyle::standardPalette() const
{
	if( s_palette != nullptr) { return * s_palette; }

	QPalette pal = QProxyStyle::standardPalette();

	return( pal );
}


void LmmsStyle::drawComplexControl( ComplexControl control,
					const QStyleOptionComplex * option,
					QPainter *painter,
						const QWidget *widget ) const
{
	// fix broken titlebar styling on win32
	if( control == CC_TitleBar )
	{
		const auto titleBar = qstyleoption_cast<const QStyleOptionTitleBar*>(option);
		if( titleBar )
		{
			QStyleOptionTitleBar so( *titleBar );
			so.palette = standardPalette();
			so.palette.setColor( QPalette::HighlightedText,
				( titleBar->titleBarState & State_Active ) ?
					QColor( 255, 255, 255 ) :
						QColor( 192, 192, 192 ) );
			so.palette.setColor( QPalette::Text,
							QColor( 64, 64, 64 ) );
			QProxyStyle::drawComplexControl( control, &so,
							painter, widget );
			return;
		}
	}
	else if (control == CC_MdiControls)
	{
		QStyleOptionComplex so(*option);
		so.palette.setColor(QPalette::Button, QColor(223, 228, 236));
		QProxyStyle::drawComplexControl(control, &so, painter, widget);
		return;
	}
/*	else if( control == CC_ScrollBar )
	{
		painter->fillRect( option->rect, QApplication::palette().color( QPalette::Active,
							QPalette::Window ) );

	}*/
	QProxyStyle::drawComplexControl( control, option, painter, widget );
}




void LmmsStyle::drawPrimitive( PrimitiveElement element,
		const QStyleOption *option, QPainter *painter,
		const QWidget *widget) const
{
	if( element == QStyle::PE_Frame ||
			element == QStyle::PE_FrameLineEdit ||
			element == QStyle::PE_PanelLineEdit )
	{
		const QRect rect = option->rect;

		QColor black = QColor( 0, 0, 0 );
		QColor shadow = option->palette.shadow().color();
		QColor highlight = option->palette.highlight().color();

		int a100 = 165;
		int a75 = static_cast<int>( a100 * .75 );
		int a50 = static_cast<int>( a100 * .6 );
		int a25 = static_cast<int>( a100 * .33 );

		auto lines = std::array<QLine, 4>{};
		auto points = std::array<QPoint, 4>{};

		// black inside lines
		// 50%
		black.setAlpha(a100);
		painter->setPen(QPen(black, 0));
		lines[0] = QLine(rect.left() + 2, rect.top() + 1,
					rect.right() - 2, rect.top() + 1);
		lines[1] = QLine(rect.left() + 2, rect.bottom() - 1,
					rect.right() - 2, rect.bottom() - 1);
		lines[2] = QLine(rect.left() + 1, rect.top() + 2,
					rect.left() + 1, rect.bottom() - 2);
		lines[3] = QLine(rect.right() - 1, rect.top() + 2,
					rect.right() - 1, rect.bottom() - 2);
		painter->drawLines(lines.data(), 4);

		// black inside dots
		black.setAlpha(a50);
		painter->setPen(QPen(black, 0));
		points[0] = QPoint(rect.left() + 2, rect.top() + 2);
		points[1] = QPoint(rect.left() + 2, rect.bottom() - 2);
		points[2] = QPoint(rect.right() - 2, rect.top() + 2);
		points[3] = QPoint(rect.right() - 2, rect.bottom() - 2);
		painter->drawPoints(points.data(), 4);


		// outside lines - shadow
		// 100%
		shadow.setAlpha(a75);
		painter->setPen(QPen(shadow, 0));
		lines[0] = QLine(rect.left() + 2, rect.top(),
						rect.right() - 2, rect.top());
		lines[1] = QLine(rect.left(), rect.top() + 2,
						rect.left(), rect.bottom() - 2);
		painter->drawLines(lines.data(), 2);

		// outside corner dots - shadow
		// 75%
		shadow.setAlpha(a50);
		painter->setPen(QPen(shadow, 0));
		points[0] = QPoint(rect.left() + 1, rect.top() + 1);
		points[1] = QPoint(rect.right() - 1, rect.top() + 1);
		painter->drawPoints(points.data(), 2);

		// outside end dots - shadow
		// 50%
		shadow.setAlpha(a25);
		painter->setPen(QPen(shadow, 0));
		points[0] = QPoint(rect.left() + 1, rect.top());
		points[1] = QPoint(rect.left(), rect.top() + 1);
		points[2] = QPoint(rect.right() - 1, rect.top());
		points[3] = QPoint(rect.left(), rect.bottom() - 1);
		painter->drawPoints(points.data(), 4);


		// outside lines - highlight
		// 100%
		highlight.setAlpha(a75);
		painter->setPen(QPen(highlight, 0));
		lines[0] = QLine(rect.left() + 2, rect.bottom(),
					rect.right() - 2, rect.bottom());
		lines[1] = QLine(rect.right(), rect.top() + 2,
					rect.right(), rect.bottom() - 2);
		painter->drawLines(lines.data(), 2);

		// outside corner dots - highlight
		// 75%
		highlight.setAlpha(a50);
		painter->setPen(QPen(highlight, 0));
		points[0] = QPoint(rect.left() + 1, rect.bottom() - 1);
		points[1] = QPoint(rect.right() - 1, rect.bottom() - 1);
		painter->drawPoints(points.data(), 2);

		// outside end dots - highlight
		// 50%
		highlight.setAlpha(a25);
		painter->setPen(QPen(highlight, 0));
		points[0] = QPoint(rect.right() - 1, rect.bottom());
		points[1] = QPoint(rect.right(), rect.bottom() - 1);
		points[2] = QPoint(rect.left() + 1, rect.bottom());
		points[3] = QPoint(rect.right(), rect.top() + 1);
		painter->drawPoints(points.data(), 4);
	}
	else
	{
		QProxyStyle::drawPrimitive( element, option, painter, widget );
	}

}


int LmmsStyle::pixelMetric( PixelMetric _metric, const QStyleOption * _option,
						const QWidget * _widget ) const
{
	switch( _metric )
	{
		case QStyle::PM_ButtonMargin:
			return 3;

		case QStyle::PM_ButtonIconSize:
			return 20;

		case QStyle::PM_ToolBarItemMargin:
			return 1;

		case QStyle::PM_ToolBarItemSpacing:
			return 2;

		case QStyle::PM_TitleBarHeight:
			return 24;

		default:
			return QProxyStyle::pixelMetric( _metric, _option, _widget );
	}
}


QImage LmmsStyle::colorizeXpm( const char * const * xpm, const QBrush& fill ) const
{
	QImage arrowXpm( xpm );
	QImage arrow( arrowXpm.size(), QImage::Format_ARGB32 );
	QPainter arrowPainter( &arrow );
	arrowPainter.fillRect( arrow.rect(), fill );
	arrowPainter.end();
	arrow.setAlphaChannel( arrowXpm );

	return arrow;
}


void LmmsStyle::hoverColors( bool sunken, bool hover, bool active, QColor& color, QColor& blend ) const
{
	if( active )
	{
		if( sunken )
		{
			color = QColor( 75, 75, 75 );
			blend = QColor( 65, 65, 65 );
		}
		else if( hover )
		{
			color = QColor( 100, 100, 100 );
			blend = QColor( 75, 75, 75 );
		}
		else
		{
			color = QColor( 21, 21, 21 );
			blend = QColor( 33, 33, 33 );
		}
	}
	else
	{
		color = QColor( 21, 21, 21 );
		blend = QColor( 33, 33, 33 );
	}
}


} // namespace lmms::gui
