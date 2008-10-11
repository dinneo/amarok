/***************************************************************************
 *   Copyright (C) 2005 by Max Howell <max.howell@methylblue.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.          *
 ***************************************************************************/

#include "overlayWidget.h"
#include "statusbar_ng/StatusBar.h"

#include <QPoint>
#include <QEvent>

#include "Debug.h"


namespace KDE {


OverlayWidget::OverlayWidget( QWidget *parent, QWidget *anchor, const char* name )
        : QFrame( parent->parentWidget() )
        , m_anchor( anchor )
        , m_parent( parent )
{
    parent->installEventFilter( this );
    setObjectName( name );

    hide();
}

void
OverlayWidget::reposition()
{
    adjustSize();

    // p is in the alignWidget's coordinates
    QPoint p;

   // p.setX( m_anchor->width() - width() );
    p.setX( m_anchor->x() );
    p.setY( m_anchor->y() - height() );

    debug() << "p before: " << p;

    p = m_anchor->mapToGlobal( p );

    debug() << "p after: " << p;

    move( p );
}


bool
OverlayWidget::event( QEvent *e )
{
    if ( e->type() == QEvent::ChildAdded )
        adjustSize();

    return QFrame::event( e );
}

}
