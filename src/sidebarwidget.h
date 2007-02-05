/*
  Copyright (c) 2006 Gábor Lehel <illissius@gmail.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*/

#ifndef AMAROK_SIDEBARWIDGET_H
#define AMAROK_SIDEBARWIDGET_H

#include <kvbox.h>

class SideBarWidget: public KVBox
{
    typedef KVBox super;
    Q_OBJECT
    public:
        SideBarWidget( QWidget *parent );
        virtual ~SideBarWidget();
        int addSideBar( const QIcon &icon, const QString &text );
        int count() const;
        QString text( int index ) const;
        QIcon icon( int index ) const;

    public slots:
        void open( int index );

    signals:
        void opened( int index );
        void closed();

    protected:
        virtual void wheelEvent( QWheelEvent *e );

    private slots:
        void slotClicked( bool checked );
        void slotSetVisible( bool visible );

    private:
        void updateShortcuts();
        class Private;
        Private* const d;
};

#endif
