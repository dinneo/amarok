// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// See COPYING file for licensing information

#include "config.h"        //for AMAZON_SUPPORT

#include "amarokconfig.h"
#include "clicklineedit.h"
#include "colorgenerator.h"
#include "k3bexporter.h"
#include "metabundle.h"
#include "playlist.h"      //appendMedia()
#include "statusbar.h"
#include "mediabrowser.h"

#include <qdatetime.h>
#include <qimage.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qregexp.h>
#include <qtooltip.h>       //QToolTip::add()

#include <kapplication.h> //kapp
#include <kdebug.h>
#include <kdirlister.h>
#include <kfiledialog.h>
#include <kglobal.h>
#include <kiconloader.h>
#include <kio/netaccess.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kpopupmenu.h>
#include <kprogress.h>
#include <krun.h>
#include <kstandarddirs.h> //locate file
#include <ktabbar.h>
#include <ktempfile.h>
#include <ktoolbar.h>
#include <ktoolbarbutton.h> //ctor
#include <kurl.h>
#include <kurldrag.h>       //dragObject()

#define escapeIPod(s)   QString(s).replace( "/", "%252f" )


MediaDevice *MediaDevice::s_instance = 0;

bool MediaBrowser::isAvailable() //static
{
    return !KStandardDirs::findExe( "gnupod_addsong.pl" ).isNull()
           || !KStandardDirs::findExe( "gnupod_addsong" ).isNull();
}


MediaBrowser::MediaBrowser( const char *name )
   : QVBox( 0, name )
{
    setSpacing( 4 );
    setMargin( 5 );

    KToolBar* toolbar = new KToolBar( this );
    toolbar->setMovingEnabled(false);
    toolbar->setFlat(true);
    toolbar->setIconSize( 16 );
    toolbar->setEnableContextMenu( false );

    { //<Search LineEdit>
        KToolBarButton *button;
        KToolBar* searchToolBar = new KToolBar( this );
        searchToolBar->setMovingEnabled(false);
        searchToolBar->setFlat(true);
        searchToolBar->setIconSize( 16 );
        searchToolBar->setEnableContextMenu( false );

        button       = new KToolBarButton( "locationbar_erase", 0, searchToolBar );
        m_searchEdit = new ClickLineEdit( searchToolBar, i18n( "Filter here..." ), "filter_edit" );
        searchToolBar->setStretchableWidget( m_searchEdit );

        m_searchEdit->setFrame( QFrame::Sunken );
        connect( button, SIGNAL( clicked() ), m_searchEdit, SLOT( clear() ) );

        QToolTip::add( button, i18n( "Clear filter" ) );
        QToolTip::add( m_searchEdit, i18n( "Enter space-separated terms to filter collection" ) );
    } //</Search LineEdit>

    m_view = new MediaDeviceView( this );

    setFocusProxy( m_view ); //default object to get focus
    setMinimumWidth( toolbar->sizeHint().width() + 2 ); //set a reasonable minWidth
}


MediaBrowser::~MediaBrowser()
{
    delete m_view;
}


MediaDeviceList::MediaDeviceList( MediaDeviceView* parent )
    : KListView( parent )
    , m_parent( parent )
{
    setSelectionMode( QListView::Extended );
    setItemsMovable( false );
    setShowSortIndicator( true );
    setFullWidth( true );
    setRootIsDecorated( true );
    setDropVisualizer( true );    //the visualizer (a line marker) is drawn when dragging over tracks
    setDropHighlighter( true );    //and the highligther (a focus rect) is drawn when dragging over playlists
    setDropVisualizerWidth( 3 );
    setAcceptDrops( true );

    addColumn( i18n( "Artist" ) );
    renderView( 0 );
    
    connect( this, SIGNAL( expanded( QListViewItem* ) ),
             this,   SLOT( renderView( QListViewItem* ) ) );

    connect( this, SIGNAL( collapsed( QListViewItem* ) ),
             this,   SLOT( slotCollapse( QListViewItem* ) ) );

    connect( this, SIGNAL( rightButtonPressed( QListViewItem*, const QPoint&, int ) ),
             this,   SLOT( rmbPressed( QListViewItem*, const QPoint&, int ) ) );
}


MediaDeviceList::~MediaDeviceList()
{}


void
MediaDeviceList::renderView( QListViewItem* parent )  //SLOT
{
    if ( parent == 0 )
        clear();

    KIconLoader iconLoader;
    QPixmap pixmap = iconLoader.loadIcon( "usbpendrive_unmount", KIcon::Toolbar, KIcon::SizeSmall );

    QStringList items;
    items = m_parent->m_device->items( parent );

    bool track = ( parent && parent->parent() );
    for ( uint i = 0; i < items.count(); track ? i+=2 : ++i )
    {
        MediaItem* item;
        if ( !parent )
            item = new MediaItem( this );
        else
            item = new MediaItem( parent );

        item->setExpandable( !track );
        item->setDragEnabled( true );
        item->setDropEnabled( true );
        item->setText( 0, items[ i ] );
        item->setPixmap( 0, pixmap );
        
        if ( track )
            item->setUrl( items[i+1] );
    }
}


void
MediaDeviceList::renderNode( QListViewItem* parent, const KURL& url )  //SLOT
{
//    renderNode( 0, KURL( "ipod:/Artists/" ) );
    KDirLister dl;
    dl.setAutoErrorHandlingEnabled( false, 0 );
    dl.openURL( url, false, true );

    while ( !dl.isFinished() )
        kapp->processEvents( 100 );

    KFileItem* fi;
    KFileItemList fil = dl.items();

    KIconLoader iconLoader;
    QPixmap pixmap = iconLoader.loadIcon( "usbpendrive_unmount", KIcon::Toolbar, KIcon::SizeSmall );
    for ( fi = fil.first(); fi; fi = fil.next() )
    {
        MediaItem* item;
        if ( !parent )
            item = new MediaItem( this );
        else
            item = new MediaItem( parent );
    
        item->setExpandable( fi->isDir() );
        item->setDragEnabled( true );
        item->setDropEnabled( true );
        item->setUrl( "ipod:" + fi->url().path() );
        item->setText( 0, fi->text() );
        item->setPixmap( 0, pixmap );
    }
}


void
MediaDeviceList::slotCollapse( QListViewItem* item )  //SLOT
{
    kdDebug() << k_funcinfo << endl;

    QListViewItem* child = item->firstChild();
    QListViewItem* childTmp;

    //delete all children
    while ( child )
    {
        childTmp = child;
        child = child->nextSibling();
        delete childTmp;
    }
}


void
MediaDeviceList::startDrag()
{
    nodeBuildDragList( 0 );
    KURLDrag* d = new KURLDrag( m_dragList, this );
    d->dragCopy();
}


void
MediaDeviceList::nodeBuildDragList( MediaItem* item )
{
    MediaItem* fi;

    m_dragList.clear();

    if ( !item )
        fi = (MediaItem*)firstChild();
    else
        fi = item;

    while ( fi )
    {
        if ( fi->isSelected() )
        {
            kdDebug() << fi->url().path() << endl;
            m_dragList << fi->url().path();
        } else
        {
            if ( fi->childCount() )
                nodeBuildDragList( (MediaItem*)fi->firstChild() );
        }

        fi = (MediaItem*)fi->nextSibling();
    }
}


void
MediaDeviceList::contentsDragEnterEvent( QDragEnterEvent *e )
{
    kdDebug() << "[MediaBrowser] Items dropping?" << endl;
    e->accept( e->source() != viewport() && KURLDrag::canDecode( e ) );
}


void
MediaDeviceList::contentsDropEvent( QDropEvent *e )
{
    KURL::List list;
    if ( KURLDrag::decode( e, list ) )
    {
        KURL::List::Iterator it = list.begin();
        for ( ; it != list.end(); ++it )
        {
            MediaDevice::instance()->addURL( *it );
        }
    }
}


void
MediaDeviceList::contentsDragMoveEvent( QDragMoveEvent* e )
{
    const QPoint p = contentsToViewport( e->pos() );
    QListViewItem *item = itemAt( p );
}   


void
MediaDeviceList::rmbPressed( QListViewItem* item, const QPoint& point, int ) //SLOT
{
    if ( item ) {
        KPopupMenu menu( this );

        enum Actions { APPEND, MAKE, QUEUE, BURN_ARTIST, BURN_ALBUM,
                       BURN_DATACD, BURN_AUDIOCD, DELETE };

        menu.insertItem( SmallIconSet( "1downarrow" ), i18n( "&Append to Playlist" ), APPEND );
        menu.insertItem( SmallIconSet( "player_playlist_2" ), i18n( "&Make Playlist" ), MAKE );
        menu.insertItem( SmallIconSet( "2rightarrow" ), i18n( "&Queue After Current Track" ), QUEUE );

        menu.insertSeparator();

        switch ( item->depth() )
        {
            case 0: 
                menu.insertItem( SmallIconSet( "cdrom_unmount" ), i18n("Burn All Tracks by This Artist"), BURN_ARTIST );
                menu.setItemEnabled( BURN_ARTIST, K3bExporter::isAvailable() );
                break;

            case 1:
                menu.insertItem( SmallIconSet( "cdrom_unmount" ), i18n("Burn This Album"), BURN_ALBUM );
                menu.setItemEnabled( BURN_ALBUM, K3bExporter::isAvailable() );
                break;

            case 2:
                menu.insertItem( SmallIconSet( "cdrom_unmount" ), i18n("Burn to CD as Data"), BURN_DATACD );
                menu.setItemEnabled( BURN_DATACD, K3bExporter::isAvailable() );
                menu.insertItem( SmallIconSet( "cdaudio_unmount" ), i18n("Burn to CD as Audio"), BURN_AUDIOCD );
                menu.setItemEnabled( BURN_AUDIOCD, K3bExporter::isAvailable() );
                break;
        }

        menu.insertSeparator();
        menu.insertItem( SmallIconSet( "editdelete" ), i18n("Delete File"), DELETE );

        switch( menu.exec( point ) )
        {
            case APPEND:
                Playlist::instance()->insertMedia( m_dragList, Playlist::Append );
                break;
            case MAKE:
                Playlist::instance()->insertMedia( m_dragList, Playlist::Replace );
                break;
            case QUEUE:
                Playlist::instance()->insertMedia( m_dragList, Playlist::Queue );
                break;
            case BURN_ARTIST:
                K3bExporter::instance()->exportArtist( item->text(0) );
                break;
            case BURN_ALBUM:
                K3bExporter::instance()->exportAlbum( item->text(0) );
                break;
            case BURN_DATACD:
                K3bExporter::instance()->exportTracks( m_dragList, K3bExporter::DataCD );
                break;
            case BURN_AUDIOCD:
                K3bExporter::instance()->exportTracks( m_dragList, K3bExporter::AudioCD );
                break;
            case DELETE:
                m_parent->m_device->deleteFiles( m_dragList );
                break;
        }
    }
}


MediaDeviceView::MediaDeviceView( MediaBrowser* parent )
    : QVBox( parent )
    , m_device ( new MediaDevice( this ) )
    , m_deviceList( new MediaDeviceList( this ) )
    , m_transferList( new KListView( this ) )
    , m_parent( parent )
{
    m_transferList->setFixedHeight( 200 );
    m_transferList->setSelectionMode( QListView::Extended );
    m_transferList->setItemsMovable( false );
    m_transferList->setShowSortIndicator( true );
    m_transferList->setFullWidth( true );
    m_transferList->setRootIsDecorated( false );
    m_transferList->setDropVisualizer( true );    //the visualizer (a line marker) is drawn when dragging over tracks
    m_transferList->setDropHighlighter( true );    //and the highligther (a focus rect) is drawn when dragging over playlists
    m_transferList->setDropVisualizerWidth( 3 );
    m_transferList->setAcceptDrops( true );
    m_transferList->addColumn( i18n( "URL" ) );
    
    QHBox* hb( this );
    hb->setSpacing( 4 );
    m_stats = new QLabel( i18n( "1 track in queue", "%n tracks in queue", m_transferList->childCount() ), hb );
    m_progress = new KProgress( hb );
    QPushButton* transferButton = new QPushButton( SmallIconSet( "rebuild" ), i18n( "Transfer" ), hb );

    m_progress->setFixedHeight( transferButton->sizeHint().height() );
    m_progress->hide();

    connect( transferButton, SIGNAL( clicked() ), MediaDevice::instance(), SLOT( transferFiles() ) );
}


MediaDeviceView::~MediaDeviceView()
{
    delete m_transferList;
    delete m_deviceList;
    delete m_device;
}


MediaDevice::MediaDevice( MediaDeviceView* parent )
    : m_parent( parent )
{
    s_instance = this;

    m_ipod = new IPod::IPod();
    m_ipod->open( "/mnt/ipod/" );
}


MediaDevice::~MediaDevice()
{
    delete m_ipod;
}


void
MediaDevice::addURL( const KURL& url )
{
    MetaBundle mb( url, false );
    if ( !fileExists( mb ) && ( m_transferURLs.findIndex( url ) == -1 ) )
    {
        MediaItem* item = new MediaItem( m_parent->m_transferList );
        item->setExpandable( false );
        item->setDropEnabled( true );
        item->setUrl( url.path() );
        item->setText( 0, url.path() );
    
        m_transferURLs << url;
        m_parent->m_stats->setText( i18n( "1 track in queue", "%n tracks in queue", m_parent->m_transferList->childCount() ) );
    } else
        amaroK::StatusBar::instance()->message( i18n( "Track already exists on iPod: " + url.path().local8Bit() ) );
}


QStringList
MediaDevice::items( QListViewItem* item )
{
    QStringList items;
    
    if ( !item )
        m_ipod->getArtists( items );
    else
    {
        if ( !item->parent() )  // second level
        {
            Artist* artist;
            artist = m_ipod->getArtistByName( item->text( 0 ) );
            if ( artist )
                for ( ArtistIterator it( *artist ); it.current(); ++it )
                    items << it.currentKey();
        }
        else
        {
            TrackList* album;
            album = m_ipod->getAlbum( item->parent()->text( 0 ), item->text( 0 ) );
    
            if ( album )
            {
                TrackList::Iterator it = album->getTrackIDs();
                while ( it.hasNext() )
                {
                    TrackMetadata* track = m_ipod->getTrackByID( it.next() );
                    items << track->getTitle();
                    items << m_ipod->getRealPath( track->getPath() );
                }
            }
        }
    }

    return items;
}


void
MediaDevice::transferFiles()  //SLOT
{
    m_parent->m_progress->setTotalSteps( m_parent->m_transferList->childCount() );
    m_parent->m_progress->show();

    KIO::CopyJob *job = KIO::copy( m_transferURLs, KURL( "ipod:/Artists/" ), true );
    connect( job, SIGNAL( copyingDone( KIO::Job *, const KURL &, const KURL &, bool, bool ) ),
             this,  SLOT( fileTransferred( KIO::Job *, const KURL &, const KURL &, bool, bool ) ) );

    connect( job, SIGNAL( result( KIO::Job * ) ),
             this,  SLOT( fileTransferFinished( KIO::Job * ) ) );
}


void
MediaDevice::deleteFiles( const KURL::List& files )
{
}


bool
MediaDevice::fileExists( const MetaBundle& bundle )  
{
    TrackList* album;

    kdDebug() << "lala: " << bundle.artist() << " - " << bundle.album() << endl;
    album = m_ipod->getAlbum( bundle.artist(), bundle.album() );
    if ( album )
    {
        TrackList::Iterator it = album->getTrackIDs();
        while ( it.hasNext() )
        {
            TrackMetadata* track = m_ipod->getTrackByID( it.next() );
            kdDebug() << "lala: " << track->getArtist() << " - " << track->getTitle() << endl;
            if ( track->getTitle() == bundle.title() )
                return true;
        }
    }
    
    return false;
}


void
MediaDevice::fileTransferred( KIO::Job *job, const KURL &from, const KURL &to, bool dir, bool renamed )  //SLOT
{
    m_parent->m_progress->setProgress( m_parent->m_progress->value() + 1 );
    m_parent->m_deviceList->renderView( 0 );
}


void
MediaDevice::fileTransferFinished( KIO::Job *job )  //SLOT
{
    m_parent->m_stats->setText( i18n( "1 track in queue", "%n tracks in queue", m_parent->m_transferList->childCount() ) );
    m_parent->m_transferList->clear();
    m_transferURLs.clear();
    
    // sync ipod, now
    KIO::get( "ipod:/Utilities/Synchronize?really=OK", false, false );
    m_parent->m_progress->hide();
}


#include "mediabrowser.moc"
