/*
 * SubWindow.cpp - Implementation of QMdiSubWindow that correctly tracks
 *   the geometry that windows should be restored to.
 *   Workaround for https://bugreports.qt.io/browse/QTBUG-256
 *   This implementation adds a custom themed title bar to
 *   the subwindow.
 *
 * Copyright (c) 2015 Colin Wallace <wallace.colin.a@gmail.com>
 * Copyright (c) 2016 Steffen Baranowsky <baramgb@freenet.de>
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

#include "SubWindow.h"

#include "lmmsconfig.h"

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#include <algorithm>

#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QMdiArea>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QSizeGrip>
#include <QStyleOption>
#include <QStyleOptionTitleBar>
#include <QWindow>

#include "embed.h"

namespace lmms::gui
{


SubWindow::SubWindow(QWidget* parent, Qt::WindowFlags windowFlags)
	: QMdiSubWindow{parent, windowFlags}
	, m_buttonSize{17, 17}
	, m_titleBarHeight{titleBarHeight()}
	, m_hasFocus{false}
	, m_isDetachable{true}
{
	// initialize the tracked geometry to whatever Qt thinks the normal geometry currently is.
	// this should always work, since QMdiSubWindows will not start as maximized
	m_trackedNormalGeom = normalGeometry();

	// inits the colors
	m_activeColor = Qt::SolidPattern;
	m_textShadowColor = Qt::black;
	m_borderColor = Qt::black;

	// close, maximize, restore, and detach buttons
	auto createButton = [this](std::string_view iconName, const QString& tooltip) -> QPushButton* {
		auto button = new QPushButton{embed::getIconPixmap(iconName), QString{}, this};
		button->resize(m_buttonSize);
		button->setFocusPolicy(Qt::NoFocus);
		button->setCursor(Qt::ArrowCursor);
		button->setAttribute(Qt::WA_NoMousePropagation);
		button->setToolTip(tooltip);
		return button;
	};
	m_closeBtn = createButton("close", tr("Close"));
	connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);

	m_maximizeBtn = createButton("maximize", tr("Maximize"));
	connect(m_maximizeBtn, &QPushButton::clicked, this, &QWidget::showMaximized);

	m_restoreBtn = createButton("restore", tr("Restore"));
	connect(m_restoreBtn, &QPushButton::clicked, this, &QWidget::showNormal);

	m_detachBtn = createButton("detach", tr("Detach"));
	connect(m_detachBtn, &QPushButton::clicked, this, &SubWindow::detach);

	m_pinBtn = createButton("pin", tr("Always on top"));
	m_pinBtn->setCheckable(true);
	connect(m_pinBtn, &QPushButton::toggled, this, &SubWindow::setPinned);
	// show the un-pinned state from the start (dimmed pin)
	updatePinIcon(false);

	// QLabel for the window title and the shadow effect
	m_shadow = new QGraphicsDropShadowEffect();
	m_shadow->setColor( m_textShadowColor );
	m_shadow->setXOffset( 1 );
	m_shadow->setYOffset( 1 );

	m_windowTitle = new QLabel( this );
	m_windowTitle->setFocusPolicy( Qt::NoFocus );
	m_windowTitle->setAttribute( Qt::WA_TransparentForMouseEvents, true );
	m_windowTitle->setGraphicsEffect( m_shadow );

	layout()->setSizeConstraint(QLayout::SetMinAndMaxSize);

	// Disable the minimize button and make sure that the custom window hint is set
	setWindowFlags((this->windowFlags() & ~Qt::WindowMinimizeButtonHint) | Qt::CustomizeWindowHint);

	connect(mdiArea(), &QMdiArea::subWindowActivated, this, &SubWindow::focusChanged);
}




/**
 * @brief SubWindow::paintEvent
 * 
 *  This draws our new title bar with custom colors
 *  and draws a window icon on the left upper corner.
 */
void SubWindow::paintEvent( QPaintEvent * )
{
	// Don't paint any of the other stuff if the sub window is maximized
	// so that only its child content is painted.
	if (isMaximized()) { return; }

	QPainter p( this );
	QRect rect( 0, 0, width(), m_titleBarHeight );

	const bool isActive = windowState() & Qt::WindowActive;

	p.fillRect( rect, isActive ? activeColor() : p.pen().brush() );

	// window border
	p.setPen( borderColor() );

	// bottom, left, and right lines
	p.drawLine( 0, height() - 1, width(), height() - 1 );
	p.drawLine( 0, m_titleBarHeight, 0, height() - 1 );
	p.drawLine( width() - 1, m_titleBarHeight, width() - 1, height() - 1 );

	// window icon
	if( widget() )
	{
		QPixmap winicon( widget()->windowIcon().pixmap( m_buttonSize ) );
		p.drawPixmap( 3, 3, m_buttonSize.width(), m_buttonSize.height(), winicon );
	}
}




/**
 * @brief SubWindow::changeEvent
 * 
 * Triggers if the window title changes and calls adjustTitleBar().
 * @param event
 */
void SubWindow::changeEvent( QEvent *event )
{
	QMdiSubWindow::changeEvent( event );

	if( event->type() == QEvent::WindowTitleChange )
	{
		adjustTitleBar();
	}
}




void SubWindow::setVisible(bool visible)
{
	// the SubWindow itself is the window in both the attached and the
	// detached state, so visibility is always controlled here.
	// The child widget is kept visible: a hidden child does not get included
	// into the layout and the maximum size collapses to 0x0 (bug #8292).
	widget()->show();
	QMdiSubWindow::setVisible(visible);
}




void SubWindow::showEvent(QShowEvent* e)
{
	// everything except the Song Editor floats from its first show on
	if (m_alwaysDetached && !isDetached() && isDetachable())
	{
		detach();
		return;
	}

	if (isDetached())
	{
		setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	}
}




bool SubWindow::isDetachable() const
{
	return m_isDetachable;
}




void SubWindow::setDetachable(bool on)
{
	m_isDetachable = on;
}




bool SubWindow::isDetached() const
{
	return windowFlags().testFlag(Qt::Window);
}




void SubWindow::setDetached(bool on)
{
	if (on) { detach(); }
	else { attach(); }
}




/**
 * @brief SubWindow::elideText
 * 
 *  Stores the given text into the given label.
 *  Shorts the text if it's too big for the labels width
 *  and adds three dots (...)
 * 
 * @param label - holds a pointer to the QLabel
 * @param text  - the text which will be stored (and if needed broken down) into the QLabel.
 */
void SubWindow::elideText( QLabel *label, QString text )
{
	QFontMetrics metrix( label->font() );
	int width = label->width() - 2;
	QString clippedText = metrix.elidedText( text, Qt::ElideRight, width );
	label->setText( clippedText );
}




/**
 * @brief SubWindow::getTrueNormalGeometry
 * 
 *  same as QWidet::normalGeometry, but works properly under X11
 *  see https://bugreports.qt.io/browse/QTBUG-256
 */
QRect SubWindow::getTrueNormalGeometry() const
{
	return m_trackedNormalGeom;
}




QBrush SubWindow::activeColor() const
{
	return m_activeColor;
}




QColor SubWindow::textShadowColor() const
{
	return m_textShadowColor;
}




QColor SubWindow::borderColor() const
{
	return m_borderColor;
}




void SubWindow::setActiveColor( const QBrush & b )
{
	m_activeColor = b;
}




void SubWindow::setTextShadowColor( const QColor & c )
{
	m_textShadowColor = c;
}




void SubWindow::setBorderColor( const QColor &c )
{
	m_borderColor = c;
}




void SubWindow::detach()
{
	if (!isDetachable() || isDetached()) { return; }

	auto area = mdiArea();
	if (area == nullptr) { return; }
	m_mdiArea = area;

	const auto pos = mapToGlobal(QPoint(0, 0));
	const auto oldSize = size();
	const bool shown = isVisible();

	m_attachedFlags = windowFlags();
	m_attachedMargins = contentsMargins();

	// float the whole subwindow - custom Qt frame included - so a detached
	// window looks exactly like an attached one. QMdiSubWindow rewrites any
	// flags passed to setWindowFlags() (it forces Qt::SubWindow back on), so
	// the only reliable way out of the workspace is setParent() with
	// explicit flags. A frameless tool window keeps the LMMS-drawn frame and
	// adds no extra taskbar entry.
	setParent(nullptr, Qt::Tool | Qt::FramelessWindowHint);

	// reparenting resets the contents margins - restore the attached ones so
	// the layout stays pixel-identical (event() keeps re-asserting this)
	setContentsMargins(m_attachedMargins);

	// group with the main window for stacking and minimizing
	auto mainWin = m_mdiArea->window();
	if (mainWin != nullptr)
	{
		winId();	// make sure our QWindow exists
		if (mainWin->windowHandle() != nullptr) { windowHandle()->setTransientParent(mainWin->windowHandle()); }
	}

	// frameless windows have no native resize edges - offer a size grip
	if (m_sizeGrip == nullptr)
	{
		m_sizeGrip = new QSizeGrip(this);
		m_sizeGrip->resize(m_sizeGrip->sizeHint());
	}
	m_sizeGrip->show();

	// reparenting must not change the window size
	resize(oldSize);

	if (shown) { show(); }
	move(pos);
	m_sizeGrip->move(width() - m_sizeGrip->width() - frameWidth(),
			height() - m_sizeGrip->height() - frameWidth());
	m_sizeGrip->raise();
	adjustTitleBar();
}

void SubWindow::updatePinIcon(bool on)
{
	// recolour the pin so its on/off state is obvious: a bright amber pin
	// when pinned (always-on-top), a dim grey pin when not
	QPixmap icon = embed::getIconPixmap("pin");
	QPainter p(&icon);
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(icon.rect(), on ? QColor(255, 176, 32) : QColor(130, 130, 130));
	p.end();
	m_pinBtn->setIcon(icon);
}

void SubWindow::setPinned(bool on)
{
	// reflect the state on the button regardless of whether the change to the
	// actual window flag below applies
	updatePinIcon(on);

	if (!isDetached()) { return; }

#ifdef LMMS_BUILD_WIN32
	// use the Win32 API directly - toggling Qt window flags would recreate
	// the native window (fatal for embedded plug-in views)
	SetWindowPos(reinterpret_cast<HWND>(winId()),
			on ? HWND_TOPMOST : HWND_NOTOPMOST,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
	setWindowFlag(Qt::WindowStaysOnTopHint, on);
	show();
#endif
}

void SubWindow::attach()
{
	if (!isDetached() || m_mdiArea == nullptr) { return; }

	// release the pin while the window is still a top-level one
	if (m_pinBtn->isChecked()) { m_pinBtn->setChecked(false); }

	const bool shown = isVisible();

	auto frame = geometry();
	frame.moveTo(m_mdiArea->mapFromGlobal(frame.topLeft()));

	// Make sure the window fully fits into the workspace
	frame.setSize({
		std::min(frame.width(), m_mdiArea->width()),
		std::min(frame.height(), m_mdiArea->height())
	});

	frame.moveTo(
		std::clamp(frame.left(), 0, m_mdiArea->rect().width() - frame.width()),
		std::clamp(frame.top(), 0, m_mdiArea->rect().height() - frame.height())
	);

	m_dragging = false;
	if (m_sizeGrip != nullptr) { m_sizeGrip->hide(); }

	// re-enter the workspace; QMdiSubWindow's ParentChange handling restores
	// the proper subwindow flags and margins itself
	setParent(m_mdiArea->viewport(), m_attachedFlags);

	if (shown) { show(); }
	setGeometry(frame);
	adjustTitleBar();
}



int SubWindow::titleBarHeight() const
{
	QStyleOptionTitleBar so;
	so.titleBarState = Qt::WindowActive; // kThemeStateActiv
	so.titleBarFlags = Qt::Window;
	return style()->pixelMetric(QStyle::PM_TitleBarHeight, &so, this);
}




int SubWindow::frameWidth() const
{
	QStyleOptionFrame so;
	return style()->pixelMetric(QStyle::PM_MdiSubWindowFrameWidth, &so, this);
}




QMargins SubWindow::decorationMargins() const
{
	return {
		frameWidth(),     // left
		titleBarHeight(), // top
		frameWidth(),     // right
		frameWidth()      // bottom
	};
}




void SubWindow::updateTitleBar()
{
	adjustTitleBar();
}


/**
 * @brief SubWindow::moveEvent
 * 
 *  overrides the QMdiSubWindow::moveEvent() for saving the position
 *  of the subwindow into m_trackedNormalGeom. This position
 *  will be saved with the project because of an Qt bug which doesn't
 *  save the right position. look at: https://bugreports.qt.io/browse/QTBUG-256
 * @param event
 */
void SubWindow::moveEvent( QMoveEvent * event )
{
	QMdiSubWindow::moveEvent( event );
	// if the window was moved and ISN'T minimized/maximized/fullscreen,
	// then save the current position
	if( !isMaximized() && !isMinimized() && !isFullScreen() )
	{
		m_trackedNormalGeom.moveTopLeft( event->pos() );
	}
}




/**
 * @brief SubWindow::adjustTitleBar
 * 
 *  Our title bar needs buttons for maximize/restore and close in the right upper corner.
 *  We check if the subwindow is maximizable and put the buttons on the right positions.
 *  At next we calculate the width of the title label and call elideText() for adding
 *  the window title to m_windowTitle (which is a QLabel)
 */
void SubWindow::adjustTitleBar()
{
	// Don't show the title or any button if the sub window is maximized. Otherwise they
	// might show up behind the actual maximized content of the child widget.
	if (isMaximized())
	{
		m_closeBtn->hide();
		m_maximizeBtn->hide();
		m_restoreBtn->hide();
		m_pinBtn->hide();
		m_windowTitle->hide();

		return;
	}

	// The sub window is not maximized, i.e. the title must be shown
	// as well as some buttons.

	// Title adjustments
	m_windowTitle->show();

	// button adjustments
	m_maximizeBtn->hide();
	m_restoreBtn->hide();
	m_detachBtn->hide();

	const int rightSpace = 3;
	const int buttonGap = 1;
	const int menuButtonSpace = 24;

	auto buttonPos = QPoint{width() - rightSpace - m_buttonSize.width(), 3};
	const auto buttonStep = QPoint{m_buttonSize.width() + buttonGap, 0};

	// the buttonBarWidth depends on the number of buttons.
	// we need it to calculate the width of window title label
	int buttonBarWidth = rightSpace;

	// set the buttons on their positions, starting from the right
	if (m_closable)
	{
		m_closeBtn->move(buttonPos);
		m_closeBtn->show();
		buttonPos -= buttonStep;
		buttonBarWidth += m_buttonSize.width();
	}
	else
	{
		m_closeBtn->hide();
	}

	// here we ask: is the Subwindow maximizable and
	// then we set the buttons and show them if needed
	if( windowFlags() & Qt::WindowMaximizeButtonHint )
	{
		buttonBarWidth = buttonBarWidth + m_buttonSize.width() + buttonGap;
		m_maximizeBtn->move(buttonPos);
		m_restoreBtn->move(buttonPos);
		if (!isMaximized())
		{
			m_maximizeBtn->show();
			buttonPos -= buttonStep;
		}
	}

	// we're keeping the restore button around if we open projects
	// from older versions that have saved minimized windows
	if (isMinimized())
	{
		m_restoreBtn->show();
		buttonPos -= buttonStep;
	}

	if (isDetachable() && !isDetached())
	{
		m_detachBtn->move(buttonPos);
		m_detachBtn->show();
		buttonPos -= buttonStep;
		buttonBarWidth = buttonBarWidth + m_buttonSize.width() + buttonGap;
	}

	// floating windows can be pinned on top of everything
	if (isDetached())
	{
		m_pinBtn->move(buttonPos);
		m_pinBtn->show();
		buttonPos -= buttonStep;
		buttonBarWidth = buttonBarWidth + m_buttonSize.width() + buttonGap;
	}
	else
	{
		m_pinBtn->hide();
	}

	if( widget() )
	{
		// title QLabel adjustments
		m_windowTitle->setAlignment( Qt::AlignHCenter );
		m_windowTitle->setFixedWidth( widget()->width() - ( menuButtonSpace + buttonBarWidth ) );
		m_windowTitle->move( menuButtonSpace,
			( m_titleBarHeight / 2 ) - ( m_windowTitle->sizeHint().height() / 2 ) - 1 );

		// if minimized we can't use widget()->width(). We have to hard code the width,
		// as the width of all minimized windows is the same.
		if( isMinimized() )
		{
			m_windowTitle->setFixedWidth( 120 );
		}

		// truncate the label string if the window is to small. Adds "..."
		elideText( m_windowTitle, widget()->windowTitle() );
		m_windowTitle->setTextInteractionFlags( Qt::NoTextInteraction );
		m_windowTitle->adjustSize();
	}
}



void SubWindow::focusChanged( QMdiSubWindow *subWindow )
{
	if( m_hasFocus && subWindow != this )
	{
		m_hasFocus = false;
		emit focusLost();
	}
	else if( subWindow == this )
	{
		m_hasFocus = true;
	}
}




/**
 * @brief SubWindow::resizeEvent
 * 
 *  At first we give the event to QMdiSubWindow::resizeEvent() which handles
 *  the event on its behavior.
 *
 *  On every resize event we have to adjust our title label.
 * 
 *  At last we store the current size into m_trackedNormalGeom. This size
 *  will be saved with the project because of an Qt bug which doesn't
 *  save the right size. look at: https://bugreports.qt.io/browse/QTBUG-256
 * 
 * @param event
 */
void SubWindow::resizeEvent( QResizeEvent * event )
{
	// When the parent QMdiArea gets resized, maximized subwindows also gets resized, if any.
	// In that case, we should call QMdiSubWindow::resizeEvent first
	// to ensure we get the correct window state.
	QMdiSubWindow::resizeEvent( event );
	adjustTitleBar();

	// keep the size grip of a floating window in its bottom-right corner
	if (m_sizeGrip != nullptr && isDetached())
	{
		m_sizeGrip->move(width() - m_sizeGrip->width() - frameWidth(),
				height() - m_sizeGrip->height() - frameWidth());
		m_sizeGrip->raise();
	}

	// if the window was resized and ISN'T minimized/maximized/fullscreen,
	// then save the current size
	if( !isMaximized() && !isMinimized() && !isFullScreen() )
	{
		m_trackedNormalGeom.setSize( event->size() );
	}
}




/**
 * @brief SubWindow::eventFilter
 *
 * Override of QMdiSubWindow's event filter.
 * This is not how regular eventFilters work, it is never installed explicitly.
 * Instead, it is installed by Qt and conveniently installs itself
 * onto the child widget. Despite relying on internal implementation details,
 * as of writing this it seems to be the best way to do so as soon as the widget is set.
 */
bool SubWindow::eventFilter(QObject* obj, QEvent* event)
{
	if (obj != static_cast<QObject*>(widget()))
	{
		return QMdiSubWindow::eventFilter(obj, event);
	}

	switch (event->type())
	{
		case QEvent::WindowStateChange:
			event->accept();
			return true;

		case QEvent::Close:
			if (!m_closable)
			{
				event->ignore();
				return true;
			}
			if (isDetached() && !m_alwaysDetached)
			{
				// closing a manually detached window re-attaches it
				attach();
				event->ignore();
				return true;
			}
			hide();
			return QMdiSubWindow::eventFilter(obj, event);

		default:
			return QMdiSubWindow::eventFilter(obj, event);
	}
}




void SubWindow::closeEvent(QCloseEvent* event)
{
	if (!m_closable)
	{
		event->ignore();
		return;
	}
	if (isDetached())
	{
		// always-detached windows just hide; a manually detached window
		// returns to the workspace
		if (m_alwaysDetached) { hide(); }
		else { attach(); }
		event->ignore();
		return;
	}
	QMdiSubWindow::closeEvent(event);
}




bool SubWindow::event(QEvent* event)
{
	const bool result = QMdiSubWindow::event(event);

	// QMdiSubWindow zeroes the contents margins on various events (parent or
	// style changes ...), which would let the child overlap the painted
	// title bar - keep the margins captured while attached, so the layout
	// stays pixel-identical to the attached state
	if (isDetached() && contentsMargins() != m_attachedMargins)
	{
		setContentsMargins(m_attachedMargins);
		adjustTitleBar();
	}

	return result;
}




void SubWindow::mousePressEvent(QMouseEvent* event)
{
	if (isDetached())
	{
		if (event->button() == Qt::LeftButton
				&& event->position().toPoint().y() <= titleBarHeight() + frameWidth())
		{
			m_dragging = true;
			m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
		}
		event->accept();
		return;
	}
	QMdiSubWindow::mousePressEvent(event);
}




void SubWindow::mouseMoveEvent(QMouseEvent* event)
{
	if (isDetached())
	{
		if (m_dragging)
		{
			move(event->globalPosition().toPoint() - m_dragOffset);
		}
		event->accept();
		return;
	}
	QMdiSubWindow::mouseMoveEvent(event);
}




void SubWindow::mouseReleaseEvent(QMouseEvent* event)
{
	if (isDetached())
	{
		m_dragging = false;
		event->accept();
		return;
	}
	QMdiSubWindow::mouseReleaseEvent(event);
}




void SubWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (isDetached())
	{
		// double-clicking the title bar of a manually detached window
		// re-attaches it
		if (!m_alwaysDetached && event->position().toPoint().y() <= m_titleBarHeight + frameWidth())
		{
			attach();
		}
		event->accept();
		return;
	}
	QMdiSubWindow::mouseDoubleClickEvent(event);
}


} // namespace lmms::gui
