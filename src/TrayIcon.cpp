/****************************************************************************************
 * Copyright (c) 2003 Stanislav Karchebny <berkus@users.sf.net>                         *
 * Copyright (c) 2003 Max Howell <max.howell@methylblue.com>                            *
 * Copyright (c) 2004 Enrico Ros <eros.kde@email.it>                                    *
 * Copyright (c) 2006 Ian Monroe <ian@monroe.nu>                                        *
 * Copyright (c) 2009,2010 Kevin Funk <krf@electrostorm.net>                            *
 * Copyright (c) 2009 Mark Kretschmann <kretschmann@kde.org>                            *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "TrayIcon.h"

#include "core/support/Amarok.h"
#include "EngineController.h"
#include "amarokconfig.h"
#include "GlobalCurrentTrackActions.h"
#include "core/capabilities/ActionsCapability.h"
#include "core/capabilities/BookmarkThisCapability.h"
#include "playlist/PlaylistActions.h"
#include "SvgHandler.h"
#include <KAboutData>
#include <KAction>
#include <KCmdLineArgs>
#include <KLocale>
#include <KMenu>
#include <KStandardDirs>

#include <QAction>
#include <QFontMetrics>
#include <QPixmap>
#include <QToolTip>

Amarok::TrayIcon::TrayIcon( QObject *parent )
        : KStatusNotifierItem( parent )
        , m_track( The::engineController()->currentTrack() )
{
    PERF_LOG( "Beginning TrayIcon Constructor" );
    KActionCollection* const ac = Amarok::actionCollection();

    setStatus( KStatusNotifierItem::Active );

    PERF_LOG( "Before adding actions" );

#ifdef Q_WS_MAC
    // Add these functions to the dock icon menu in OS X
    extern void qt_mac_set_dock_menu(QMenu *);
    qt_mac_set_dock_menu( contextMenu() );
    contextMenu()->addAction( ac->action( "playlist_playmedia" ) );
    contextMenu()->addSeparator();
#endif

    contextMenu()->addAction( ac->action( "prev"       ) );
    contextMenu()->addAction( ac->action( "play_pause" ) );
    contextMenu()->addAction( ac->action( "stop"       ) );
    contextMenu()->addAction( ac->action( "next"       ) );
    contextMenu()->setObjectName( "TrayIconContextMenu" );

    PERF_LOG( "Initializing system tray icon" );

    EngineController* const engine = The::engineController();

    setIconByName( "amarok" );
    updateOverlayIcon();
    updateToolTipIcon();
    updateMenu();

    m_track = engine->currentTrack();

    connect( engine, SIGNAL( trackPlaying( Meta::TrackPtr ) ),
             this, SLOT( trackPlaying( Meta::TrackPtr ) ) );
    connect( engine, SIGNAL( stopped( qint64, qint64 ) ),
             this, SLOT( stopped() ) );
    connect( engine, SIGNAL( paused() ),
             this, SLOT( paused() ) );

    connect( engine, SIGNAL( trackMetadataChanged( Meta::TrackPtr ) ),
             this, SLOT( metadataChanged( Meta::TrackPtr ) ) );

    connect( engine, SIGNAL( albumMetadataChanged( Meta::AlbumPtr ) ),
             this, SLOT( metadataChanged( Meta::AlbumPtr ) ) );

    connect( engine, SIGNAL( volumeChanged( int ) ),
             this, SLOT( updateToolTip() ) );

    connect( engine, SIGNAL( muteStateChanged( bool ) ),
             this, SLOT( updateToolTip() ) );


    connect( this, SIGNAL( scrollRequested( int, Qt::Orientation ) ), SLOT( slotScrollRequested(int, Qt::Orientation) ) );
    connect( this, SIGNAL( secondaryActivateRequested( const QPoint & ) ),
             The::engineController(), SLOT( playPause() ) );
}

void
Amarok::TrayIcon::updateToolTipIcon()
{
    updateToolTip(); // the normal update

    if( m_track )
    {
        if( m_track->album() && m_track->album()->hasImage() )
        {
            QPixmap image = The::svgHandler()->imageWithBorder( m_track->album(), KIconLoader::SizeLarge, 5 );
            setToolTipIconByPixmap( image );
        }
        else
        {
            setToolTipIconByName( "amarok" );
        }
    }
    else
    {
        setToolTipIconByName( "amarok" );
    }
}


void
Amarok::TrayIcon::updateToolTip()
{
    if( m_track )
    {
        setToolTipTitle( The::engineController()->prettyNowPlaying() );

        QStringList tooltip;

        QString volume;
        if ( The::engineController()->isMuted() )
        {
            volume = i18n( "Muted" );
        }
        else
        {
            volume = QString( "%1%" ).arg( The::engineController()->volume() );
        }
        tooltip << QString("<i>%1: %2</i>").arg( i18n( "Volume" ) ).arg( volume );

        const float score = m_track->score();
        if( score > 0.f )
        {
            tooltip << QString( "%1: %2" ).arg( i18n( "Score" ) ).arg( QString::number( score, 'f', 2 ) );
        }

        const int rating = m_track->rating();
        if( rating > 0 )
        {
            QString stars;
            for( int i = 0; i < rating / 2; ++i )
                stars += QString( "<img src=\"%1\" height=\"%2\" width=\"%3\">" )
                        .arg( KStandardDirs::locate( "data", "amarok/images/star.png" ) )
                        .arg( QFontMetrics( QToolTip::font() ).height() )
                        .arg( QFontMetrics( QToolTip::font() ).height() );
            if( rating % 2 )
                stars += QString( "<img src=\"%1\" height=\"%2\" width=\"%3\">" )
                        .arg( KStandardDirs::locate( "data", "amarok/images/smallstar.png" ) )
                        .arg( QFontMetrics( QToolTip::font() ).height() )
                        .arg( QFontMetrics( QToolTip::font() ).height() );

            tooltip << QString( "%1: %2" ).arg( i18n( "Rating" ) ).arg( stars );
        }

        const int count = m_track->playCount();
        if( count > 0 )
        {
            tooltip << QString( "%1: %2" ).arg( i18n( "Play Count" ) ).arg( QString::number( count ) );
        }

        const QDateTime lastPlayed = m_track->lastPlayed();
        tooltip << QString( "%1: %2" ).arg( i18n( "Last played" ) ).arg( Amarok::verboseTimeSince( lastPlayed ) );

        setToolTipSubTitle( tooltip.join("<br>") );
    }
    else
    {
        setToolTipTitle( KCmdLineArgs::aboutData()->programName() );
        setToolTipSubTitle( The::engineController()->prettyNowPlaying() );
    }
}

void
Amarok::TrayIcon::trackPlaying( Meta::TrackPtr track )
{
    m_track = track;

    updateOverlayIcon();
    updateMenu();
    updateToolTipIcon();
}

void
Amarok::TrayIcon::paused()
{
    updateOverlayIcon();
    updateToolTipIcon();

}

void
Amarok::TrayIcon::stopped()
{
    m_track = 0;
    updateOverlayIcon();
    updateMenu(); // remove custom track actions on stop
    updateToolTipIcon();
}

void
Amarok::TrayIcon::metadataChanged( Meta::TrackPtr track )
{
    Q_UNUSED( track )

    updateToolTip();
    updateMenu();
}

void
Amarok::TrayIcon::metadataChanged( Meta::AlbumPtr album )
{
    Q_UNUSED( album )

    updateToolTipIcon();
    updateMenu();
}

void
Amarok::TrayIcon::slotScrollRequested( int delta, Qt::Orientation orientation )
{
    Q_UNUSED( orientation )

    The::engineController()->increaseVolume( delta / Amarok::VOLUME_SENSITIVITY );
}



void
Amarok::TrayIcon::updateMenu()
{
    foreach( QAction* action, m_extraActions )
    {
        contextMenu()->removeAction( action );
        // -- delete actions without parent (e.g. the ones from the capabilities)
        if( action && !action->parent() )
        {
            delete action;
        }
    }
    m_extraActions.clear();

    contextMenu()->removeAction( m_separator.data() );

    delete m_separator.data();

    if( !m_track )
        return;

    foreach( QAction *action, The::globalCurrentTrackActions()->actions() )
        m_extraActions.append( action );

    QScopedPointer<Capabilities::ActionsCapability> ac( m_track->create<Capabilities::ActionsCapability>() );
    if( ac )
    {
        QList<QAction*> actions = ac->actions();
        foreach( QAction *action, actions )
            m_extraActions.append( action );
    }

    QScopedPointer<Capabilities::BookmarkThisCapability> btc( m_track->create<Capabilities::BookmarkThisCapability>() );
    if( btc )
    {
        m_extraActions.append( btc->bookmarkAction() );
    }

    if ( m_extraActions.count() > 0 )
    {
        KActionCollection const *ac = Amarok::actionCollection();
        QAction *preferenceAction = ac->action( KStandardAction::name( KStandardAction::Preferences ) );

        // remove the 3 bottom items, so we can push them to the bottom again
        contextMenu()->removeAction( actionCollection()->action( "file_quit" ) );
        contextMenu()->removeAction( actionCollection()->action( "minimizeRestore" ) );
        contextMenu()->removeAction( preferenceAction );

        foreach( QAction* action, m_extraActions )
            contextMenu()->addAction( action );

        m_separator = contextMenu()->addSeparator();
        // readd
        contextMenu()->addAction( actionCollection()->action( "minimizeRestore" ) );
        contextMenu()->addAction( actionCollection()->action( "file_quit" ) );
        contextMenu()->addAction( preferenceAction );
    }
}

void
Amarok::TrayIcon::updateOverlayIcon()
{
    if( The::engineController()->isPlaying() )
        setOverlayIconByName( "media-playback-start" );
    else if( The::engineController()->isPaused() )
        setOverlayIconByName( "media-playback-pause" );
    else
        setOverlayIconByName( QString() );
}

#include "TrayIcon.moc"
