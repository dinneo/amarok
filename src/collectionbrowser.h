// (c) 2004 Mark Kretschmann <markey@web.de>
// (c) 2004 Christian Muehlhaeuser <chris@chris.de>
// (c) 2005 Gábor Lehel <illissius@gmail.com>
// (c) 2005 Christan Baumgart <christianbaumgart@web.de>
// See COPYING file for licensing information.

#ifndef AMAROK_COLLECTIONBROWSER_H
#define AMAROK_COLLECTIONBROWSER_H

#include "qlabel.h"
#include "qvbox.h"           //baseclass

#include <klistview.h>       //baseclass
#include <qstringlist.h>     //stack allocated
#include <kurl.h>            //stack allocated
#include <kdialogbase.h>     //baseclass

#include "collectiondb.h"

class ClickLineEdit;
class CollectionDB;

class QCString;
class QDragObject;
class QPixmap;
class QPoint;
class QStringList;

class KAction;
class KComboBox;
class KRadioAction;
class KPopupMenu;
class KProgress;

class CollectionView;
class CollectionItem;
class OrganizeCollectionDialog;

class CollectionBrowser: public QVBox
{
    Q_OBJECT
    friend class CollectionView;

    public:
        CollectionBrowser( const char* name );
        virtual bool eventFilter( QObject*, QEvent* );

    public slots:
        void setupDirs();

    private slots:
        void slotSetFilterTimeout();
        void slotSetFilter();

    private:
        void layoutToolbar();

        //attributes:
        enum CatMenuId { IdAlbum = 1, IdArtist = 2, IdGenre = 4, IdYear = 8 , IdScan = 16, IdNone = 32,
                    IdArtistAlbum = 64, IdGenreArtist = 128, IdGenreArtistAlbum = 256, IdVisYearAlbum = 512, IdArtistVisYearAlbum = 1024 };

        class KToolBar* m_toolbar;
        KAction* m_configureAction;
        KAction* m_scanAction;
        KRadioAction* m_treeViewAction;
        KRadioAction* m_flatViewAction;
        class KActionMenu* m_tagfilterMenuButton;

        KPopupMenu* m_categoryMenu;
        KPopupMenu* m_cat1Menu;
        KPopupMenu* m_cat2Menu;
        KPopupMenu* m_cat3Menu;
        KLineEdit*  m_searchEdit;
        KComboBox* m_timeFilter;
        CollectionView* m_view;
        QTimer*     m_timer;

    friend class CollectionItem; // for CatMenuId
};


class CollectionItem : public KListViewItem {
    public:
        CollectionItem( QListView* parent, int cat )
            : KListViewItem( parent )
            , m_cat( cat ) {};
        CollectionItem( QListView* parent )
            : KListViewItem( parent ) {};
        CollectionItem( QListViewItem* parent, int cat )
            : KListViewItem( parent )
            , m_cat( cat ) {};
        CollectionItem( QListViewItem* parent )
            : KListViewItem( parent ) {};
        void setUrl( const QString& url ) { m_url.setPath( url ); }
        const KURL& url() const { return m_url; }

        virtual void sortChildItems ( int column, bool ascending ); //reimplemented

    private:
        //for sorting
        virtual int compare( QListViewItem*, int, bool ) const; //reimplemented

    //attributes:
        KURL m_url;
        int m_cat;
};


class CollectionView : public KListView
{
    Q_OBJECT
    friend class CollectionBrowser;

    public:
        enum ViewMode  { modeTreeView, modeFlatView };

        friend class CollectionItem; // for access to m_cat2
        friend class ContextBrowser; // for setupDirs()

        CollectionView( CollectionBrowser* parent );
        ~CollectionView();

        static CollectionView* instance() { return m_instance; }

        void setFilter( const QString &filter ) { m_filter = filter; }
        void setTimeFilter( const uint timeFilter ) { m_timeFilter = timeFilter; }
        QString filter() { return m_filter; }
        uint timeFilter() { return m_timeFilter; }
        CollectionItem* currentItem() { return static_cast<CollectionItem*>( KListView::currentItem() ); }

        int trackDepth() { return m_trackDepth; }

        // avoid duplicated code
        static void manipulateThe( QString &original, bool reverse = false );

    public slots:
        /** Rebuilds and displays the treeview by querying the database. */
        void renderView();

        void setTreeMode() { setViewMode( modeTreeView ); };
        void setFlatMode() { setViewMode( modeFlatView ); };

        void presetMenu( int id );
        void cat1Menu( int id, bool rerender = true );
        void cat2Menu( int id, bool rerender = true );
        void cat3Menu( int id, bool rerender = true );
        void organizeFiles( const KURL::List &list, const QString &caption, bool addToCollection=false );

    private slots:
        void setupDirs();

        void scanStarted();
        void scanDone( bool changed = true );

        void slotExpand( QListViewItem* );
        void slotCollapse( QListViewItem* );
        void enableCat3Menu( bool );
        void invokeItem( QListViewItem* );
        void rmbPressed( QListViewItem*, const QPoint&, int );
        void selectAll() {QListView::selectAll(true); }
        /** Tries to download the cover image from Amazon.com */
        void fetchCover();
        /** Shows dialog with information on selected track */
        void showTrackInfo();
        void deleteSelectedFiles();

    private:
        enum Tag { Title, Artist, Album, Genre, Length, Track, Year, Comment, Playcount, Score, Rating, Filename, Firstplay, Lastplay, Modified, Bitrate };

        void setViewMode( int mode, bool rerender = true );
        void startDrag();
        KURL::List listSelected();

        void playlistFromURLs( const KURL::List &urls );
        QPixmap iconForCategory( const int cat ) const;
        QString captionForCategory( const int cat ) const;
        inline QString captionForTag( const Tag ) const;

        /** Rebuild selections, viewport and expanded items after reloads */
        void cacheView();
        void restoreView();

        // avoid duplicated code
        static inline bool endsInThe( const QString & text );
        static inline void yearAlbumCalc( QString &year, QString &text );
        inline void updateTrackDepth();

        uint translateTimeFilter( uint filterMode );

        /**Call when a category has changed **/
        void updateColumnHeader();
        // Reimplemented from KListView
        void viewportPaintEvent( QPaintEvent* );
        void viewportResizeEvent( QResizeEvent* );
        bool eventFilter( QObject*, QEvent* );
        void contentsDragEnterEvent( QDragEnterEvent* );
        void contentsDragMoveEvent( QDragMoveEvent* );
        void contentsDropEvent( QDropEvent *e );

    //attributes:
        static CollectionView* m_instance;

        CollectionBrowser* m_parent;
        QString m_filter;
        uint m_timeFilter;
        int m_cat1;
        int m_cat2;
        int m_cat3;
        int m_trackDepth;
        int m_viewMode;

        QValueList<QStringList> m_cacheOpenItemPaths;
        QString                 m_cacheViewportTopItem;
        QString                 m_cacheCurrentItem;
};

// why is signal detailsClicked() missing from KDialogBase?
class OrganizeCollectionDialogBase : public KDialogBase
{
    Q_OBJECT
    public:
    OrganizeCollectionDialogBase( QWidget *parent=0, const char *name=0, bool modal=true,
            const QString &caption=QString::null,
            int buttonMask=Ok|Apply|Cancel )
        : KDialogBase( parent, name, modal, caption, buttonMask )
    {
    }

    signals:
        void detailsClicked();
    public slots:
        void slotDetails() { KDialogBase::slotDetails(); emit detailsClicked(); adjustSize(); }
};




#endif /* AMAROK_COLLECTIONBROWSER_H */
