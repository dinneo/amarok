/****************************************************************************************
 * Copyright (c) 2007 Maximilian Kossick <maximilian.kossick@googlemail.com>            *
 * Copyright (c) 2008 Daniel Winter <dw@danielwinter.de>                                *
 * Copyright (c) 2010 Ralf Engels <ralf-engels@gmx.de>                                  *
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

#define DEBUG_PREFIX "SqlMeta"

#include "SqlMeta.h"

#include "amarokconfig.h"
#include "core/support/Amarok.h"
#include "CapabilityDelegate.h"
#include <core/support/Debug.h>
#include <core/meta/support/MetaUtility.h>
#include <shared/MetaTagLib.h> // for getting an embedded cover
#include <core-impl/collections/support/ArtistHelper.h>
#include "SqlCollection.h"
#include "SqlQueryMaker.h"
#include "SqlRegistry.h"
#include "covermanager/CoverFetcher.h"
#include "core/collections/support/SqlStorage.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMultiHash>
#include <QReadWriteLock>
#include <QReadLocker>
#include <QWriteLocker>
#include <QMutexLocker>
#include <QPixmapCache>
#include <QCryptographicHash>

#include <KCodecs>
#include <KLocale>
#include <KSharedPtr>

// additional constants
namespace Meta
{
    static const qint64 valAlbumId  = valCustom + 1;
}

using namespace Meta;

QString
SqlTrack::getTrackReturnValues()
{
    //do not use any weird column names that contains commas: this will break getTrackReturnValuesCount()
    return "urls.id, urls.deviceid, urls.rpath, urls.directory, urls.uniqueid, "
           "tracks.id, tracks.title, tracks.comment, "
           "tracks.tracknumber, tracks.discnumber, "
           "statistics.score, statistics.rating, "
           "tracks.bitrate, tracks.length, "
           "tracks.filesize, tracks.samplerate, "
           "statistics.id, "
           "statistics.createdate, statistics.accessdate, "
           "statistics.playcount, tracks.filetype, tracks.bpm, "
           "tracks.createdate, tracks.albumgain, tracks.albumpeakgain, "
           "tracks.trackgain, tracks.trackpeakgain, "
           "artists.name, artists.id, " // TODO: just reading the id should be sufficient
           "albums.name, albums.id, albums.artist, " // TODO: again here
           "genres.name, genres.id, " // TODO: again here
           "composers.name, composers.id, " // TODO: again here
           "years.name, years.id"; // TODO: again here
}

QString
SqlTrack::getTrackJoinConditions()
{
    return "LEFT JOIN tracks ON urls.id = tracks.url "
           "LEFT JOIN statistics ON urls.id = statistics.url "
           "LEFT JOIN artists ON tracks.artist = artists.id "
           "LEFT JOIN albums ON tracks.album = albums.id "
           "LEFT JOIN genres ON tracks.genre = genres.id "
           "LEFT JOIN composers ON tracks.composer = composers.id "
           "LEFT JOIN years ON tracks.year = years.id";
}

int
SqlTrack::getTrackReturnValueCount()
{
    static int count = getTrackReturnValues().split( ',' ).count();
    return count;
}

SqlTrack::SqlTrack( Collections::SqlCollection* collection, int deviceId,
                    const QString &rpath, int directoryId, const QString uidUrl )
    : Track()
    , m_collection( QPointer<Collections::SqlCollection>( collection ) )
    , m_writeFile( true )
    , m_labelsInCache( false )
{
    m_batchUpdate = true; // I don't want commits yet

    m_urlId = -1; // this will be set with the first database write
    m_trackId = -1; // this will be set with the first database write
    m_statisticsId = -1;

    setUrl( deviceId, rpath, directoryId );
    m_url = m_cache.value( Meta::valUrl ).toString(); // SqlRegistry already has this url
    setUidUrl( uidUrl );
    m_uid = m_cache.value( Meta::valUniqueId ).toString(); // SqlRegistry already has this uid


    // ensure that these values get a correct database id
    m_cache.insert( Meta::valAlbum, QVariant() );
    m_cache.insert( Meta::valArtist, QVariant() );
    m_cache.insert( Meta::valComposer, QVariant() );
    m_cache.insert( Meta::valYear, QVariant() );
    m_cache.insert( Meta::valGenre, QVariant() );

    m_trackNumber = 0;
    m_discNumber = 0;
    m_score = 0;
    m_rating = 0;
    m_bitrate = 0;
    m_length = 0;
    m_filesize = 0;
    m_sampleRate = 0;
    m_playCount = 0;
    m_bpm = 0.0;
    m_createDate = QDateTime::currentDateTime();
    m_cache.insert( Meta::valCreateDate, m_createDate ); // ensure that the created date is written the next time

    m_trackGain = 0.0;
    m_trackPeakGain = 0.0;
    m_albumGain = 0.0;
    m_albumPeakGain = 0.0;

    m_batchUpdate = false;

    m_filetype = Amarok::Unknown;
}

SqlTrack::SqlTrack( Collections::SqlCollection* collection, const QStringList &result )
    : Track()
    , m_collection( QPointer<Collections::SqlCollection>( collection ) )
    , m_batchUpdate( false )
    , m_writeFile( true )
    , m_labelsInCache( false )
{
    QStringList::ConstIterator iter = result.constBegin();
    m_urlId = (*(iter++)).toInt();
    m_deviceId = (*(iter++)).toInt();
    m_rpath = *(iter++);
    m_directoryId = (*(iter++)).toInt();
    m_url = KUrl( m_collection->mountPointManager()->getAbsolutePath( m_deviceId, m_rpath ) );
    m_uid = *(iter++);
    m_trackId = (*(iter++)).toInt();
    m_title = *(iter++);
    m_comment = *(iter++);
    m_trackNumber = (*(iter++)).toInt();
    m_discNumber = (*(iter++)).toInt();
    m_score = (*(iter++)).toDouble();
    m_rating = (*(iter++)).toInt();
    m_bitrate = (*(iter++)).toInt();
    m_length = (*(iter++)).toInt();
    m_filesize = (*(iter++)).toInt();
    m_sampleRate = (*(iter++)).toInt();
    m_statisticsId = (*(iter++)).toInt();
    uint time = (*(iter++)).toUInt();
    if( time > 0 )
        m_firstPlayed = QDateTime::fromTime_t(time);
    time = (*(iter++)).toUInt();
    if( time > 0 )
        m_lastPlayed = QDateTime::fromTime_t(time);
    m_playCount = (*(iter++)).toInt();
    m_filetype = Amarok::FileType( (*(iter++)).toInt() );
    m_bpm = (*(iter++)).toFloat();
    m_createDate = QDateTime::fromTime_t((*(iter++)).toUInt());

    // if there is no track gain, we assume a gain of 0
    // if there is no album gain, we use the track gain
    QString albumGain = *(iter++);
    QString albumPeakGain = *(iter++);
    m_trackGain = (*(iter++)).toDouble();
    m_trackPeakGain = (*(iter++)).toDouble();
    if ( albumGain.isEmpty() )
    {
        m_albumGain = m_trackGain;
        m_albumPeakGain = m_trackPeakGain;
    }
    else
    {
        m_albumGain = albumGain.toDouble();
        m_albumPeakGain = albumPeakGain.toDouble();
    }
    SqlRegistry* registry = m_collection->registry();

    QString artist = *(iter++);
    int artistId = (*(iter++)).toInt();
    if( artistId > 0 )
        m_artist = registry->getArtist( artistId, artist );

    QString album = *(iter++);
    int albumId =(*(iter++)).toInt();
    int albumArtistId = (*(iter++)).toInt();
    if( albumId > 0 ) // sanity check
        m_album = registry->getAlbum( albumId, album, albumArtistId );

    QString genre = *(iter++);
    int genreId = (*(iter++)).toInt();
    if( genreId > 0 ) // sanity check
        m_genre = registry->getGenre( genreId, genre );

    QString composer = *(iter++);
    int composerId = (*(iter++)).toInt();
    if( composerId > 0 ) // sanity check
        m_composer = registry->getComposer( composerId, composer );

    QString year = *(iter++);
    int yearId = (*(iter++)).toInt();
    if( yearId > 0 ) // sanity check
    m_year = registry->getYear( year.toInt(), yearId );
    //Q_ASSERT_X( iter == result.constEnd(), "SqlTrack( Collections::SqlCollection*, QStringList )", "number of expected fields did not match number of actual fields: expected " + result.size() );
}

SqlTrack::~SqlTrack()
{
    QWriteLocker locker( &m_lock );

    if( !m_cache.isEmpty() )
        warning() << "Destroying track with unwritten meta information." << m_title << "cache:" << m_cache;
    if( m_batchUpdate )
        warning() << "Destroying track with unclosed batch update." << m_title;
}

QString
SqlTrack::name() const
{
    QReadLocker locker( &m_lock );
    return m_title;
}

QString
SqlTrack::prettyName() const
{
    if ( !name().isEmpty() )
        return name();
    return  prettyTitle( m_url.fileName() );
}


QString
SqlTrack::fullPrettyName() const
{
    QString artistName;
    Meta::ArtistPtr artist = this->artist();
    if( artist )
        artistName = artist->name();

    //FIXME doesn't work for resume playback

    QString s;
    if( !artistName.isEmpty() )
        s = i18n("%1 - %2", artist->name(), name() );

    if( s.isEmpty() )
        s = name();

    if( s.isEmpty() )
        s = prettyTitle( playableUrl().fileName() );

    return s;
}

void
SqlTrack::setTitle( const QString &newTitle )
{
    QWriteLocker locker( &m_lock );

    if ( m_title != newTitle )
        commitMetaDataChanges( Meta::valTitle, newTitle );
}


KUrl
SqlTrack::playableUrl() const
{
    QReadLocker locker( &m_lock );
    return m_url;
}

QString
SqlTrack::prettyUrl() const
{
    QReadLocker locker( &m_lock );
    return m_url.path();
}

void
SqlTrack::setUrl( int deviceId, const QString &rpath, int directoryId )
{
    QWriteLocker locker( &m_lock );

    if( m_deviceId == deviceId &&
        m_rpath == rpath &&
        m_directoryId == directoryId )
        return;

    m_deviceId = deviceId;
    m_rpath = rpath;
    m_directoryId = directoryId;

    commitMetaDataChanges( Meta::valUrl,
                           m_collection->mountPointManager()->getAbsolutePath( m_deviceId, m_rpath ) );
}

QString
SqlTrack::uidUrl() const
{
    QReadLocker locker( &m_lock );
    return m_uid;
}

void
SqlTrack::setUidUrl( const QString &uid )
{
    QWriteLocker locker( &m_lock );

    // -- ensure that the uid starts with the collections protocol (amarok-sqltrackuid)
    QString newid = uid;
    QString protocol;
    if( m_collection )
        protocol = m_collection->uidUrlProtocol()+"://";
    if( !newid.startsWith( protocol ) )
        newid.prepend( protocol );

    m_cache.insert( Meta::valUniqueId, newid );

    if( !m_batchUpdate )
    {
        debug() << "setting uidUrl manually...did you really mean to do this?";
        commitMetaDataChanges();
    }
}

bool
SqlTrack::isPlayable() const
{
    QReadLocker locker( &m_lock );

    //a song is not playable anymore if the collection was removed
    return m_collection && QFile::exists( m_url.path() );
}

bool
SqlTrack::isEditable() const
{
    QReadLocker locker( &m_lock );

    QFile::Permissions p = QFile::permissions( m_url.path() );
    const bool editable = ( p & QFile::WriteUser ) || ( p & QFile::WriteGroup ) || ( p & QFile::WriteOther );
    return m_collection && QFile::exists( m_url.path() ) && editable;
}

Meta::AlbumPtr
SqlTrack::album() const
{
    QReadLocker locker( &m_lock );
    return m_album;
}

void
SqlTrack::setAlbum( const QString &newAlbum )
{
    QWriteLocker locker( &m_lock );

    if( !m_album || m_album->name() != newAlbum )
        commitMetaDataChanges( Meta::valAlbum, newAlbum );
}

void
SqlTrack::setAlbum( int albumId )
{
    QWriteLocker locker( &m_lock );

    commitMetaDataChanges( Meta::valAlbumId, albumId );
}

Meta::ArtistPtr
SqlTrack::artist() const
{
    QReadLocker locker( &m_lock );
    return m_artist;
}

void
SqlTrack::setArtist( const QString &newArtist )
{
    QWriteLocker locker( &m_lock );

    if( !m_artist || m_artist->name() != newArtist )
        commitMetaDataChanges( Meta::valArtist, newArtist );
}

void
SqlTrack::setAlbumArtist( const QString &newAlbumArtist )
{
    if( m_album.isNull() )
        return;

    if( !newAlbumArtist.compare( "Various Artists", Qt::CaseInsensitive ) ||
        !newAlbumArtist.compare( i18n( "Various Artists" ), Qt::CaseInsensitive ) )
    {
        commitMetaDataChanges( Meta::valCompilation, true );
    }
    else
    {
        m_cache.insert( Meta::valAlbumArtist, ArtistHelper::realTrackArtist( newAlbumArtist ) );
        m_cache.insert( Meta::valCompilation, false );
        commitMetaDataChanges();
    }
}

Meta::ComposerPtr
SqlTrack::composer() const
{
    QReadLocker locker( &m_lock );
    return m_composer;
}

void
SqlTrack::setComposer( const QString &newComposer )
{
    QWriteLocker locker( &m_lock );

    if( !m_composer || m_composer->name() != newComposer )
        commitMetaDataChanges( Meta::valComposer, newComposer );
}

Meta::YearPtr
SqlTrack::year() const
{
    QReadLocker locker( &m_lock );
    return m_year;
}

void
SqlTrack::setYear( int newYear )
{
    QWriteLocker locker( &m_lock );

    if( !m_year || m_year->year() != newYear )
        commitMetaDataChanges( Meta::valYear, newYear );
}

Meta::GenrePtr
SqlTrack::genre() const
{
    QReadLocker locker( &m_lock );
    return m_genre;
}

void
SqlTrack::setGenre( const QString &newGenre )
{
    QWriteLocker locker( &m_lock );

    if( !m_genre || m_genre->name() != newGenre )
        commitMetaDataChanges( Meta::valGenre, newGenre );
}

QString
SqlTrack::type() const
{
    QReadLocker locker( &m_lock );

    return m_url.isLocalFile()
           ? Amarok::FileTypeSupport::toString( m_filetype )
           : "stream"; // don't localize. This is used in different files to identify streams.
}

void
SqlTrack::setType( Amarok::FileType newType )
{
    QWriteLocker locker( &m_lock );

    if ( m_filetype != newType )
        commitMetaDataChanges( Meta::valFiletype, int(newType) );
}

qreal
SqlTrack::bpm() const
{
    QReadLocker locker( &m_lock );
    return m_bpm;
}

void
SqlTrack::setBpm( const qreal newBpm )
{
    QWriteLocker locker( &m_lock );

    if ( m_bpm != newBpm )
        commitMetaDataChanges( Meta::valBpm, newBpm );
}

QString
SqlTrack::comment() const
{
    QReadLocker locker( &m_lock );
    return m_comment;
}

void
SqlTrack::setComment( const QString &newComment )
{
    QWriteLocker locker( &m_lock );

    if( newComment != m_comment )
        commitMetaDataChanges( Meta::valComment, newComment );
}

double
SqlTrack::score() const
{
    QReadLocker locker( &m_lock );
    return m_score;
}

void
SqlTrack::setScore( double newScore )
{
    QWriteLocker locker( &m_lock );

    if( qAbs( newScore - m_score ) > 0.001 ) // we don't commit for minimal changes
        commitMetaDataChanges( Meta::valScore, newScore );
}

int
SqlTrack::rating() const
{
    QReadLocker locker( &m_lock );
    return m_rating;
}

void
SqlTrack::setRating( int newRating )
{
    QWriteLocker locker( &m_lock );

    if( newRating != m_rating )
        commitMetaDataChanges( Meta::valRating, newRating );
}

qint64
SqlTrack::length() const
{
    QReadLocker locker( &m_lock );
    return m_length;
}

void
SqlTrack::setLength( qint64 newLength )
{
    QWriteLocker locker( &m_lock );

    if( newLength != m_length )
        commitMetaDataChanges( Meta::valLength, newLength );
}

int
SqlTrack::filesize() const
{
    QReadLocker locker( &m_lock );
    return m_filesize;
}

int
SqlTrack::sampleRate() const
{
    QReadLocker locker( &m_lock );
    return m_sampleRate;
}

void
SqlTrack::setSampleRate( int newSampleRate )
{
    QWriteLocker locker( &m_lock );

    if( newSampleRate != m_sampleRate )
        commitMetaDataChanges( Meta::valSamplerate, newSampleRate );
}

int
SqlTrack::bitrate() const
{
    QReadLocker locker( &m_lock );
    return m_bitrate;
}

void
SqlTrack::setBitrate( int newBitrate )
{
    QWriteLocker locker( &m_lock );

    if( newBitrate != m_bitrate )
        commitMetaDataChanges( Meta::valBitrate, newBitrate );
}

QDateTime
SqlTrack::createDate() const
{
    QReadLocker locker( &m_lock );
    return m_createDate;
}

int
SqlTrack::trackNumber() const
{
    QReadLocker locker( &m_lock );
    return m_trackNumber;
}

void
SqlTrack::setTrackNumber( int newTrackNumber )
{
    QWriteLocker locker( &m_lock );

    if( newTrackNumber != m_trackNumber )
        commitMetaDataChanges( Meta::valTrackNr, newTrackNumber );
}

int
SqlTrack::discNumber() const
{
    QReadLocker locker( &m_lock );
    return m_discNumber;
}

void
SqlTrack::setDiscNumber( int newDiscNumber )
{
    QWriteLocker locker( &m_lock );

    if( newDiscNumber != m_discNumber )
        commitMetaDataChanges( Meta::valDiscNr, newDiscNumber );
}

QDateTime
SqlTrack::lastPlayed() const
{
    QReadLocker locker( &m_lock );
    return m_lastPlayed;
}

void
SqlTrack::setLastPlayed( const QDateTime &newTime )
{
    QWriteLocker locker( &m_lock );

    if( newTime != m_lastPlayed )
        commitMetaDataChanges( Meta::valLastPlayed, newTime );
}

QDateTime
SqlTrack::firstPlayed() const
{
    QReadLocker locker( &m_lock );
    return m_firstPlayed;
}

void
SqlTrack::setFirstPlayed( const QDateTime &newTime )
{
    QWriteLocker locker( &m_lock );

    if( newTime != m_firstPlayed )
        commitMetaDataChanges( Meta::valFirstPlayed, newTime );
}

int
SqlTrack::playCount() const
{
    QReadLocker locker( &m_lock );
    return m_playCount;
}

void
SqlTrack::setPlayCount( const int newCount )
{
    QWriteLocker locker( &m_lock );

    if( newCount != m_playCount )
        commitMetaDataChanges( Meta::valPlaycount, newCount );
}

qreal
SqlTrack::replayGain( ReplayGainTag mode ) const
{
    QReadLocker locker(&(const_cast<SqlTrack*>(this)->m_lock));

    switch( mode )
    {
    case Meta::ReplayGain_Track_Gain:
        return m_trackGain;
    case Meta::ReplayGain_Track_Peak:
        return m_trackPeakGain;
    case Meta::ReplayGain_Album_Gain:
        return m_albumGain;
    case Meta::ReplayGain_Album_Peak:
        return m_albumPeakGain;
    }
    return 0.0;
}

void
SqlTrack::setReplayGain( Meta::ReplayGainTag mode, qreal value )
{
    if( qAbs( value - replayGain( mode ) ) < 0.01 )
        return;

    {
        QWriteLocker locker( &m_lock );

        switch( mode )
        {
        case Meta::ReplayGain_Track_Gain:
            m_cache.insert( Meta::valTrackGain, value );
            break;
        case Meta::ReplayGain_Track_Peak:
            m_cache.insert( Meta::valTrackGainPeak, value );
            break;
        case Meta::ReplayGain_Album_Gain:
            m_cache.insert( Meta::valAlbumGain, value );
            break;
        case Meta::ReplayGain_Album_Peak:
            m_cache.insert( Meta::valAlbumGainPeak, value );
            break;
        }

        commitMetaDataChanges();
    }
}


void
SqlTrack::beginMetaDataUpdate()
{
    QWriteLocker locker( &m_lock );

    m_batchUpdate = true;
}

void
SqlTrack::endMetaDataUpdate()
{
    QWriteLocker locker( &m_lock );

    m_batchUpdate = false;
    commitMetaDataChanges();
}

void
SqlTrack::commitMetaDataChanges()
{
    if( m_batchUpdate )
        return;
    if( m_cache.isEmpty() )
        return; // nothing to do

    // debug() << "SqlTrack::commitMetaDataChanges " << m_cache;

    QString oldUid = m_uid;
    bool collectionChanged = false;

    // for all the following objects we need to invalidate the cache and
    // notify the observers after the update
    KSharedPtr<SqlArtist>   oldArtist;
    KSharedPtr<SqlArtist>   newArtist;
    KSharedPtr<SqlAlbum>    oldAlbum;
    KSharedPtr<SqlAlbum>    newAlbum;
    KSharedPtr<SqlComposer> oldComposer;
    KSharedPtr<SqlComposer> newComposer;
    KSharedPtr<SqlGenre>    oldGenre;
    KSharedPtr<SqlGenre>    newGenre;
    KSharedPtr<SqlYear>     oldYear;
    KSharedPtr<SqlYear>     newYear;

    if( m_cache.contains( Meta::valFiletype ) )
        m_filetype = Amarok::FileType(m_cache.value( Meta::valFiletype ).toInt());
    if( m_cache.contains( Meta::valTitle ) )
        m_title = m_cache.value( Meta::valTitle ).toString();
    if( m_cache.contains( Meta::valComment ) )
        m_comment = m_cache.value( Meta::valComment ).toString();
    if( m_cache.contains( Meta::valScore ) )
        m_score = m_cache.value( Meta::valScore ).toDouble();
    if( m_cache.contains( Meta::valRating ) )
        m_rating = m_cache.value( Meta::valRating ).toInt();
    if( m_cache.contains( Meta::valLength ) )
        m_length = m_cache.value( Meta::valLength ).toLongLong();
    if( m_cache.contains( Meta::valSamplerate ) )
        m_sampleRate = m_cache.value( Meta::valSamplerate ).toInt();
    if( m_cache.contains( Meta::valBitrate ) )
        m_bitrate = m_cache.value( Meta::valBitrate ).toInt();
    if( m_cache.contains( Meta::valFirstPlayed ) )
        m_firstPlayed = m_cache.value( Meta::valFirstPlayed ).toDateTime();
    if( m_cache.contains( Meta::valLastPlayed ) )
        m_lastPlayed = m_cache.value( Meta::valLastPlayed ).toDateTime();
    if( m_cache.contains( Meta::valTrackNr ) )
        m_trackNumber = m_cache.value( Meta::valTrackNr ).toInt();
    if( m_cache.contains( Meta::valDiscNr ) )
        m_discNumber = m_cache.value( Meta::valDiscNr ).toInt();
    if( m_cache.contains( Meta::valPlaycount ) )
        m_playCount = m_cache.value( Meta::valPlaycount ).toInt();
    if( m_cache.contains( Meta::valCreateDate ) )
        m_createDate = m_cache.value( Meta::valCreateDate ).toDateTime();
    if( m_cache.contains( Meta::valTrackGain ) )
        m_trackGain = m_cache.value( Meta::valTrackGain ).toDouble();
    if( m_cache.contains( Meta::valTrackGainPeak ) )
        m_trackPeakGain = m_cache.value( Meta::valTrackGainPeak ).toDouble();
    if( m_cache.contains( Meta::valAlbumGain ) )
        m_albumGain = m_cache.value( Meta::valAlbumGain ).toDouble();
    if( m_cache.contains( Meta::valAlbumGainPeak ) )
        m_albumPeakGain = m_cache.value( Meta::valAlbumGainPeak ).toDouble();

    if( m_cache.contains( Meta::valUrl ) )
    {
        // slight problem here: it is possible to set the url to the one of an already
        // existing track, which is forbidden by the database
        // At least the ScanResultProcessor handles this problem

        KUrl oldUrl = m_url;
        KUrl newUrl = m_cache.value( Meta::valUrl ).toString();
        if( oldUrl != newUrl )
            m_collection->registry()->updateCachedUrl( oldUrl.path(), newUrl.path() );
        m_url = newUrl;
        // debug() << "m_cache contains a new URL, setting m_url to " << m_url << " from " << oldUrl;
    }

    // Use the latest uid here
    if( m_cache.contains( Meta::valUniqueId ) )
    {
        QString newUid = m_cache.value( Meta::valUniqueId ).toString();
        if( oldUid != newUid &&
            m_collection->registry()->updateCachedUid( oldUid, newUid ) )
            m_uid = newUid;
    }

    if( m_cache.contains( Meta::valArtist ) )
    {
        //invalidate cache of the old artist...
        oldArtist = static_cast<SqlArtist*>(m_artist.data());
        m_artist = m_collection->registry()->getArtist( m_cache.value( Meta::valArtist ).toString() );
        //and the new one
        newArtist = static_cast<SqlArtist*>(m_artist.data());

        // if the current album is no compilation and we aren't changing
        // the album anyway, then we need to create a new album with the
        // new artist.
        if( m_album &&
            m_album->hasAlbumArtist() &&
            m_album->albumArtist() == oldArtist &&
            !m_cache.contains( Meta::valAlbum ) &&
            !m_cache.contains( Meta::valAlbumId ) )
            m_cache.insert( Meta::valAlbum, m_album->name() );

        collectionChanged = true;
    }

    if( m_cache.contains( Meta::valAlbum ) ||
        m_cache.contains( Meta::valAlbumId ) ||
        m_cache.contains( Meta::valAlbumArtist ) )
    {
        oldAlbum = static_cast<SqlAlbum*>(m_album.data());

        if( m_cache.contains( Meta::valAlbumId ) )
            m_album = m_collection->registry()->getAlbum( m_cache.value( Meta::valAlbumId ).toInt() );
        else
        {
            // the album should remain a compilation after renaming it
            // TODO: we would need to use the artist helper
            QString newArtistName;
            if( m_cache.contains( Meta::valAlbumArtist ) )
                newArtistName = m_cache.value( Meta::valAlbumArtist ).toString();
            else if( oldAlbum && oldAlbum->isCompilation() && !oldAlbum->name().isEmpty() )
                newArtistName = QString();
            else if( oldAlbum && oldAlbum->hasAlbumArtist() )
                newArtistName = oldAlbum->albumArtist()->name();

            m_album = m_collection->registry()->getAlbum( m_cache.contains( Meta::valAlbum)
                                                          ? m_cache.value( Meta::valAlbum ).toString()
                                                          : oldAlbum->name(),
                                                          newArtistName );
        }

        newAlbum = static_cast<SqlAlbum*>(m_album.data());

        // due to the complex logic with artist and albumId it can happen that
        // in the end we have the same album as before.
        if( newAlbum == oldAlbum )
        {
            m_cache.remove( Meta::valAlbum );
            m_cache.remove( Meta::valAlbumId );
            m_cache.remove( Meta::valAlbumArtist );
            oldAlbum.clear();
            newAlbum.clear();
        }
        else
        {
            // copy the image BUG: 203211
            if( oldAlbum && newAlbum &&
                oldAlbum->hasImage() && !newAlbum->hasImage() )
                newAlbum->setImage( oldAlbum->imageLocation().path() );

            collectionChanged = true;
        }
    }

    if( m_cache.contains( Meta::valComposer ) )
    {
        oldComposer = static_cast<SqlComposer*>(m_composer.data());
        m_composer = m_collection->registry()->getComposer( m_cache.value( Meta::valComposer ).toString() );
        newComposer = static_cast<SqlComposer*>(m_composer.data());
        collectionChanged = true;
    }

    if( m_cache.contains( Meta::valGenre ) )
    {
        oldGenre = static_cast<SqlGenre*>(m_genre.data());
        m_genre = m_collection->registry()->getGenre( m_cache.value( Meta::valGenre ).toString() );
        newGenre = static_cast<SqlGenre*>(m_genre.data());
        collectionChanged = true;
    }

    if( m_cache.contains( Meta::valYear ) )
    {
        oldYear = static_cast<SqlYear*>(m_year.data());
        m_year = m_collection->registry()->getYear( m_cache.value( Meta::valYear ).toInt() );
        newYear = static_cast<SqlYear*>(m_year.data());
        collectionChanged = true;
    }

    if( m_cache.contains( Meta::valBpm ) )
        m_bpm = m_cache.value( Meta::valBpm ).toDouble();

    // --- write the file
    if( m_writeFile )
        writeMetaDataToFile();

    //updating the fields might have changed the filesize
    //read the current filesize so that we can update the db
    QFile file( m_url.path() );
    if( file.exists() )
    {
        if( m_filesize != file.size() )
        {
            m_cache.insert( Meta::valFilesize, file.size() );
            m_filesize = file.size();
        }
    }

    // --- add to the registry dirty list
    SqlRegistry *registry = m_collection->registry();
    {
        QMutexLocker locker2( &registry->m_blockMutex );
        registry->m_dirtyTracks.insert( Meta::SqlTrackPtr( this ) );
        if( oldArtist )
            registry->m_dirtyArtists.insert( oldArtist );
        if( newArtist )
            registry->m_dirtyArtists.insert( newArtist );
        if( oldAlbum )
            registry->m_dirtyAlbums.insert( oldAlbum );
        if( newAlbum )
            registry->m_dirtyAlbums.insert( newAlbum );
        if( oldComposer )
            registry->m_dirtyComposers.insert( oldComposer );
        if( newComposer )
            registry->m_dirtyComposers.insert( newComposer );
        if( oldGenre )
            registry->m_dirtyGenres.insert( oldGenre );
        if( newGenre )
            registry->m_dirtyGenres.insert( newGenre );
        if( oldYear )
            registry->m_dirtyYears.insert( oldYear );
        if( newYear )
            registry->m_dirtyYears.insert( newYear );
    }

    m_lock.unlock(); // or else we provoke a deadlock
    registry->commitDirtyTracks();
    m_lock.lockForWrite();

    if( m_uid != oldUid )
    {
        updatePlaylistsToDb( m_cache, oldUid );
        updateEmbeddedCoversToDb( m_cache, oldUid );
    }

    // --- clean up
    m_cache.clear();
}

void
SqlTrack::writeMetaDataToFile()
{
    Meta::Tag::writeTags( m_url.path(), m_cache );
}

void
SqlTrack::updatePlaylistsToDb( const FieldHash &fields, const QString &oldUid )
{
    if( fields.isEmpty() )
        return; // nothing to do

    SqlStorage *storage = m_collection->sqlStorage();
    QString tags;

    if( fields.contains( Meta::valUrl ) )
        tags += QString( ",url='%1'" ).arg( storage->escape( m_url.path() ) );
    if( fields.contains( Meta::valTitle ) )
        tags += QString( ",title='%1'" ).arg( storage->escape( m_title ) );
   if( fields.contains( Meta::valAlbum ) )
        tags += QString( ",album='%1'" ).arg( m_album ? storage->escape( m_album->prettyName() ) : "" );
   if( fields.contains( Meta::valArtist ) )
        tags += QString( ",artist='%1'" ).arg( m_artist ? storage->escape( m_artist->prettyName() ) : "" );
    if( fields.contains( Meta::valLength ) )
        tags += QString(",length=%1").arg( QString::number( m_length ) );
    if( fields.contains( Meta::valUniqueId ) )
        tags += QString( ",uniqueid='%1'" ).arg( storage->escape( m_uid ) );

    if( !tags.isEmpty() )
    {
        tags = tags.remove(0, 1); // the first character is always a ','
        QString update = "UPDATE playlist_tracks SET %1 WHERE uniqueid = '%2';";
        update = update.arg( tags, storage->escape( oldUid ) );
        storage->query( update );
    }
}

void
SqlTrack::updateEmbeddedCoversToDb( const FieldHash &fields, const QString &oldUid )
{
    if( fields.isEmpty() )
        return; // nothing to do

    SqlStorage *storage = m_collection->sqlStorage();
    QString tags;

    if( fields.contains( Meta::valUniqueId ) )
        tags += QString( ",path='%1'" ).arg( storage->escape( m_uid ) );

    if( !tags.isEmpty() )
    {
        tags = tags.remove(0, 1); // the first character is always a ','
        QString update = "UPDATE images SET %1 WHERE path = '%2';";
        update = update.arg( tags, storage->escape( oldUid ) );
        storage->query( update );
    }
}

QString
SqlTrack::prettyTitle( const QString &filename ) //static
{
    QString s = filename; //just so the code is more readable

    //remove .part extension if it exists
    if (s.endsWith( ".part" ))
        s = s.left( s.length() - 5 );

    //remove file extension, s/_/ /g and decode %2f-like sequences
    s = s.left( s.lastIndexOf( '.' ) ).replace( '_', ' ' );
    s = KUrl::fromPercentEncoding( s.toAscii() );

    return s;
}

void
SqlTrack::finishedPlaying( double playedFraction )
{
    beginMetaDataUpdate(); // Batch updates, so we only bother our observers once.

    bool doUpdate = false;

    if( m_length < 60000 ) // less than 1min
        doUpdate = playedFraction >= 0.9;
    else
        doUpdate = playedFraction >= 0.7;

    if( doUpdate )
    {
        setPlayCount( playCount() + 1 );
        if( !firstPlayed().isValid() )
            setFirstPlayed( QDateTime::currentDateTime() );
        setLastPlayed( QDateTime::currentDateTime() );
    }

    setScore( Amarok::computeScore( score(), playCount(), playedFraction ) );

    endMetaDataUpdate();
}

bool
SqlTrack::inCollection() const
{
    QReadLocker locker( &m_lock );
    return m_trackId > 0;
}

Collections::Collection*
SqlTrack::collection() const
{
    return m_collection;
}

QString
SqlTrack::cachedLyrics() const
{
    QString query = QString( "SELECT lyrics FROM lyrics WHERE url = '%1'" )
                        .arg( m_collection->sqlStorage()->escape( m_rpath ) );
    QStringList result = m_collection->sqlStorage()->query( query );
    if( result.isEmpty() )
        return QString();
    return result[0];
}

void
SqlTrack::setCachedLyrics( const QString &lyrics )
{
    QString query = QString( "SELECT count(*) FROM lyrics WHERE url = '%1'")
                        .arg( m_collection->sqlStorage()->escape(m_rpath) );

    const QStringList queryResult = m_collection->sqlStorage()->query( query );

    if( queryResult.isEmpty() )
        return;

    if( queryResult[0].toInt() == 0 )
    {
        QString insert = QString( "INSERT INTO lyrics( url, lyrics ) VALUES ( '%1', '%2' );" )
                            .arg( m_collection->sqlStorage()->escape( m_rpath ),
                                  m_collection->sqlStorage()->escape( lyrics ) );
        m_collection->sqlStorage()->insert( insert, "lyrics" );
    }
    else
    {
        QString update = QString( "UPDATE lyrics SET lyrics = '%1' WHERE url = '%2';" )
                            .arg( m_collection->sqlStorage()->escape( lyrics ),
                                  m_collection->sqlStorage()->escape( m_rpath ) );
        m_collection->sqlStorage()->query( update );
    }

    notifyObservers();
}

bool
SqlTrack::hasCapabilityInterface( Capabilities::Capability::Type type ) const
{
    return m_collection->trackCapabilityDelegate() &&
        m_collection->trackCapabilityDelegate()->hasCapabilityInterface( type, this );
}

Capabilities::Capability*
SqlTrack::createCapabilityInterface( Capabilities::Capability::Type type )
{
    if( m_collection->trackCapabilityDelegate() )
        return m_collection->trackCapabilityDelegate()->createCapabilityInterface( type, this );
    return 0;
}

void
SqlTrack::addLabel( const QString &label )
{
    Meta::LabelPtr realLabel = m_collection->registry()->getLabel( label );
    addLabel( realLabel );
}

void
SqlTrack::addLabel( const Meta::LabelPtr &label )
{
    KSharedPtr<SqlLabel> sqlLabel = KSharedPtr<SqlLabel>::dynamicCast( label );
    if( !sqlLabel )
    {
        Meta::LabelPtr tmp = m_collection->registry()->getLabel( label->name() );
        sqlLabel = KSharedPtr<SqlLabel>::dynamicCast( tmp );
    }
    if( sqlLabel )
    {
        QWriteLocker locker( &m_lock );
        commitMetaDataChanges(); // we need to have a up-to-date m_urlId
        if( m_urlId <= 0 )
        {
            warning() << "Track does not have an urlId.";
            return;
        }

        QString countQuery = "SELECT COUNT(*) FROM urls_labels WHERE url = %1 AND label = %2;";
        QStringList countRs = m_collection->sqlStorage()->query( countQuery.arg( QString::number( m_urlId ), QString::number( sqlLabel->id() ) ) );
        if( countRs.first().toInt() == 0 )
        {
            QString insert = "INSERT INTO urls_labels(url,label) VALUES (%1,%2);";
            m_collection->sqlStorage()->insert( insert.arg( QString::number( m_urlId ), QString::number( sqlLabel->id() ) ), "urls_labels" );

            if( m_labelsInCache )
            {
                m_labelsCache.append( Meta::LabelPtr::staticCast( sqlLabel ) );
            }
            locker.unlock();
            notifyObservers();
            sqlLabel->invalidateCache();
            sqlLabel->notifyObservers();
        }
    }
}

int
SqlTrack::deviceId() const
{
    QReadLocker locker( &m_lock );
    return m_deviceId;
}

QString
SqlTrack::rpath() const
{
    QReadLocker locker( &m_lock );
    return m_rpath;
}

int
SqlTrack::id() const
{
    QReadLocker locker( &m_lock );
    return m_trackId;
}

int
SqlTrack::urlId() const
{
    QReadLocker locker( &m_lock );
    return m_trackId;
}

void
SqlTrack::removeLabel( const Meta::LabelPtr &label )
{
    KSharedPtr<SqlLabel> sqlLabel = KSharedPtr<SqlLabel>::dynamicCast( label );
    if( !sqlLabel )
    {
        Meta::LabelPtr tmp = m_collection->registry()->getLabel( label->name() );
        sqlLabel = KSharedPtr<SqlLabel>::dynamicCast( tmp );
    }
    if( sqlLabel )
    {
        QString query = "DELETE FROM urls_labels WHERE label = %2 and url = (SELECT url FROM tracks WHERE id = %1);";
        m_collection->sqlStorage()->query( query.arg( QString::number( m_trackId ), QString::number( sqlLabel->id() ) ) );
        if( m_labelsInCache )
        {
            m_labelsCache.removeAll( Meta::LabelPtr::staticCast( sqlLabel ) );
        }
        notifyObservers();
        sqlLabel->invalidateCache();
        sqlLabel->notifyObservers();
    }
}

Meta::LabelList
SqlTrack::labels() const
{
    if( m_labelsInCache )
    {
        return m_labelsCache;
    }
    else if( m_collection )
    {
        Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
        qm->setQueryType( Collections::QueryMaker::Label );
        const_cast<SqlTrack*>( this )->addMatchTo( qm );
        qm->setBlocking( true );
        qm->run();

        m_labelsInCache = true;
        m_labelsCache = qm->labels();

        delete qm;
        return m_labelsCache;
    }
    else
    {
        return Meta::LabelList();
    }
}

void
SqlTrack::remove()
{
    QWriteLocker locker( &m_lock );
    m_cache.clear();
    locker.unlock();
    m_collection->registry()->removeTrack( m_urlId, m_uid );

    // -- inform all albums, artist, years
#undef foreachInvalidateCache
#define INVALIDATE_AND_UPDATE(X) if( X ) \
    { \
        X->invalidateCache(); \
        X->notifyObservers(); \
    }
    INVALIDATE_AND_UPDATE(static_cast<Meta::SqlArtist*>(m_artist.data()));
    INVALIDATE_AND_UPDATE(static_cast<Meta::SqlAlbum*>(m_album.data()));
    INVALIDATE_AND_UPDATE(static_cast<Meta::SqlComposer*>(m_composer.data()));
    INVALIDATE_AND_UPDATE(static_cast<Meta::SqlGenre*>(m_genre.data()));
    INVALIDATE_AND_UPDATE(static_cast<Meta::SqlYear*>(m_year.data()));
#undef INVALIDATE_AND_UPDATE
    m_artist = 0;
    m_album = 0;
    m_composer = 0;
    m_genre = 0;
    m_year = 0;

    m_urlId = 0;
    m_trackId = 0;
    m_statisticsId = 0;

    m_collection->collectionUpdated();
}

//---------------------- class Artist --------------------------

SqlArtist::SqlArtist( Collections::SqlCollection* collection, int id, const QString &name ) : Artist()
    ,m_collection( QPointer<Collections::SqlCollection>( collection ) )
    ,m_id( id )
    ,m_name( name )
    ,m_tracksLoaded( false )
    ,m_albumsLoaded( false )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

Meta::SqlArtist::~SqlArtist()
{
}

void
SqlArtist::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_tracks.clear();
}

TrackList
SqlArtist::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
        return m_tracks;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Track );
    addMatchTo( qm );
    qm->setBlocking( true );
    qm->run();
    m_tracks = qm->tracks( m_collection->collectionId() );
    delete qm;
    m_tracksLoaded = true;
    return m_tracks;
}

AlbumList
SqlArtist::albums()
{
    QMutexLocker locker( &m_mutex );
    if( m_albumsLoaded )
        return m_albums;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Album );
    addMatchTo( qm );
    qm->setBlocking( true );
    qm->run();
    m_albums = qm->albums( m_collection->collectionId() );
    delete qm;
    m_albumsLoaded = true;
    return m_albums;
}

bool
SqlArtist::hasCapabilityInterface( Capabilities::Capability::Type type ) const
{
    return m_collection->artistCapabilityDelegate() &&
        m_collection->artistCapabilityDelegate()->hasCapabilityInterface( type, this );
}

Capabilities::Capability*
SqlArtist::createCapabilityInterface( Capabilities::Capability::Type type )
{
    if( m_collection->artistCapabilityDelegate() )
        return m_collection->artistCapabilityDelegate()->createCapabilityInterface( type, this );
    return 0;
}


//--------------- class Album ---------------------------------
const QString SqlAlbum::AMAROK_UNSET_MAGIC = QString( "AMAROK_UNSET_MAGIC" );

SqlAlbum::SqlAlbum( Collections::SqlCollection* collection, int id, const QString &name, int artist ) : Album()
    , m_collection( QPointer<Collections::SqlCollection>( collection ) )
    , m_name( name )
    , m_id( id )
    , m_artistId( artist )
    , m_imageId( -1 )
    , m_hasImage( false )
    , m_hasImageChecked( false )
    , m_pixmapCacheDirty( false )
    , m_unsetImageId( -1 )
    , m_tracksLoaded( false )
    , m_suppressAutoFetch( false )
    , m_mutex( QMutex::Recursive )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

Meta::SqlAlbum::~SqlAlbum()
{
    // now it would be nice to clean up the unused pixmaps from the cache.
    // however the destructor can be called from another thread and in such
    // a case handling pixmaps is very dangerous.
    // clearPixmapCache();
}

void
SqlAlbum::clearPixmapCache()
{
    // debug() << "Cleaning pixmap cache for"<<m_name;
    foreach( const QString &id, m_pixmapCacheIds )
    {
        QPixmapCache::remove( id );
        // debug() << "SqlAlbum::image removing for"<<m_name << id;
    }
    m_pixmapCacheIds.clear();
    m_pixmapCacheDirty = false;
}

void
SqlAlbum::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_hasImage = false;
    m_hasImageChecked = false;
    clearPixmapCache();
    m_tracks.clear();
}

TrackList
SqlAlbum::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
        return m_tracks;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Track );
    addMatchTo( qm );
    qm->orderBy( Meta::valDiscNr );
    qm->orderBy( Meta::valTrackNr );
    qm->orderBy( Meta::valTitle );
    qm->setBlocking( true );
    qm->run();
    m_tracks = qm->tracks( m_collection->collectionId() );
    delete qm;
    m_tracksLoaded = true;
    return m_tracks;
}

// note for internal implementation:
// if hasImage returns true then m_imagePath is set
bool
SqlAlbum::hasImage( int size ) const
{
    Q_UNUSED(size); // we have every size if we have an image at all
    QMutexLocker locker( &m_mutex );

    if( m_name.isEmpty() )
        return false;

    if( !m_hasImageChecked )
    {
        m_hasImageChecked = true;

        const_cast<SqlAlbum*>( this )->largeImagePath();

        // The user has explicitly set no cover
        if( m_imagePath == AMAROK_UNSET_MAGIC )
            m_hasImage = false;

        // if we don't have an image but it was not explicitely blocked
        else if( m_imagePath.isEmpty() )
        {
            // Cover fetching runs in another thread. If there is a retrieved cover
            // then updateImage() gets called which updates the cache and alerts the
            // subscribers. We use queueAlbum() because this runs the fetch as a
            // background job and doesn't give an intruding popup asking for confirmation
            if( !m_suppressAutoFetch && !m_name.isEmpty() && AmarokConfig::autoGetCoverArt() )
                CoverFetcher::instance()->queueAlbum( AlbumPtr(const_cast<SqlAlbum *>(this)) );

            m_hasImage = false;
        }
        else
            m_hasImage = true;
    }

    return m_hasImage;
}

QPixmap
SqlAlbum::image( int size )
{
    QMutexLocker locker( &m_mutex );

    if( !hasImage() )
        return Meta::Album::image( size );

    // clean the pixmap cache here in the hope that ::image is only called from the UI thread
    if( m_pixmapCacheDirty )
        clearPixmapCache();

    QPixmap pixmap;
    // look in the memory pixmap cache
    // large scale images are not stored in memory
    QString cachedPixmapKey = QString::number(size) + "@cover:" + m_imagePath;

    if( size > 1 && QPixmapCache::find( cachedPixmapKey, &pixmap ) )
        return pixmap;

    // findCachedImage looks for a scaled version of the fullsize image
    // which may have been saved on a previous lookup
    QString cachedImagePath;
    if( size <= 1 )
        cachedImagePath = m_imagePath;
    else
        cachedImagePath = scaledDiskCachePath( size );

    //FIXME this cache doesn't differentiate between shadowed/unshadowed
    // a image exists. just load it.
    if( !cachedImagePath.isEmpty() && QFile( cachedImagePath ).exists() )
    {
        pixmap = QPixmap( cachedImagePath );
        if( pixmap.isNull() )
            return Meta::Album::image( size );
        if( size > 1)
        {
            if( QPixmapCache::insert( cachedPixmapKey, pixmap ) )
                m_pixmapCacheIds.insert( cachedPixmapKey );
        // debug() << "SqlAlbum::image adding1 for"<<m_name << cachedPixmapKey<<"from"<<cachedImagePath;
        }
        return pixmap;
    }

    // no cached scaled image exists. Have to create it
    QImage image;

    // --- embedded cover
    if( m_collection && m_imagePath.startsWith( m_collection->uidUrlProtocol() ) )
    {
        // -- check if we have a track with the given path as uid
        Meta::TrackPtr track = m_collection->getTrackFromUid( m_imagePath );
        if( track )
            image = Meta::Tag::getEmbeddedCover( track->playableUrl().path() );
    }

    // --- a normal path
    if( image.isNull() )
        image = QImage( m_imagePath );

    if( image.isNull() )
        return Meta::Album::image( size );

    if( size > 1 && size < 1000 )
    {
        image = image.scaled( size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation );
        image.save( cachedImagePath, "PNG" );
    }

    pixmap = QPixmap::fromImage( image );
    if( size > 1)
    {
        if( QPixmapCache::insert( cachedPixmapKey, pixmap ) )
            m_pixmapCacheIds.insert( cachedPixmapKey );
        // debug() << "SqlAlbum::image adding2 for"<<m_name << cachedPixmapKey<<"from"<<cachedImagePath;
    }
    return pixmap;
}

KUrl
SqlAlbum::imageLocation( int size )
{
    if( !hasImage() )
        return KUrl();

    // findCachedImage looks for a scaled version of the fullsize image
    // which may have been saved on a previous lookup
    QString cachedImagePath;
    if( size <= 1 )
        cachedImagePath = m_imagePath;
    else
        cachedImagePath = scaledDiskCachePath( size );

    if( cachedImagePath.isEmpty() )
        return KUrl();

    if( !QFile( cachedImagePath ).exists() )
    {
        // If we don't have the location, it's possible that we haven't tried to find the image yet
        // So, let's look for it and just ignore the result
        QPixmap i = image( size );
        Q_UNUSED( i )
    }

    if( !QFile( cachedImagePath ).exists() )
        return KUrl();

    return cachedImagePath;
}

void
SqlAlbum::setImage( const QImage &image )
{
    if( m_name.isEmpty() )
        return;

    QMutexLocker locker( &m_mutex );
    if( image.isNull() )
        return;

    // removeImage() will destroy all scaled cached versions of the artwork
    // and remove references from the database if required.
    removeImage();

    QString path = largeDiskCachePath();
    // make sure not to overwrite existing images
    while( QFile(path).exists() )
        path += "_"; // not that nice but it shouldn't happen that often.

    image.save( path, "JPG" );
    setImage( path );

    locker.unlock();
    notifyObservers();
}

void
SqlAlbum::removeImage()
{
    QMutexLocker locker( &m_mutex );
    if( !hasImage() )
        return;

    // Update the database image path
    // Set the album image to a magic value which will tell Amarok not to fetch it automatically
    const int unsetId = unsetImageId();
    QString query = "UPDATE albums SET image = %1 WHERE id = %2";
    m_collection->sqlStorage()->query( query.arg( QString::number( unsetId ), QString::number( m_id ) ) );

    // From here on we check if there are any remaining references to that particular image in the database
    // If there aren't, then we should remove the image path from the database ( and possibly delete the file? )
    // If there are, we need to leave it since other albums will reference this particular image path.
    //
    query = "SELECT count( albums.id ) FROM albums "
                    "WHERE albums.image = %1";
    QStringList res = m_collection->sqlStorage()->query( query.arg( QString::number( m_imageId ) ) );

    if( !res.isEmpty() )
    {
        int references = res[0].toInt();

        // If there are no more references to this particular image, then we should clean up
        if( references <= 0 )
        {
            query = "DELETE FROM images WHERE id = %1";
            m_collection->sqlStorage()->query( query.arg( QString::number( m_imageId ) ) );

            // remove the large cover only if it was cached.
            QDir largeCoverDir( Amarok::saveLocation( "albumcovers/large/" ) );
            if( QFileInfo(m_imagePath).absoluteDir() == largeCoverDir )
                QFile::remove( m_imagePath );

            // remove all cache images
            QString key = md5sum( QString(), QString(), m_imagePath );
            QDir        cacheDir( Amarok::saveLocation( "albumcovers/cache/" ) );
            QStringList cacheFilter;
            cacheFilter << QString( "*@" ) + key;
            QStringList cachedImages = cacheDir.entryList( cacheFilter );

            foreach( const QString &image, cachedImages )
            {
                bool r = QFile::remove( cacheDir.filePath( image ) );
                debug() << "deleting cached image: " << image << " : " + ( r ? QString("ok") : QString("fail") );
            }

            m_pixmapCacheDirty = true;
        }
    }

    m_imageId = -1;
    m_imagePath = QString();
    m_hasImage = false;
    m_hasImageChecked = true;

    locker.unlock();
    notifyObservers();
}

int
SqlAlbum::unsetImageId() const
{
    // Return the cached value if we have already done the lookup before
    if( m_unsetImageId >= 0 )
        return m_unsetImageId;

    QString query = "SELECT id FROM images WHERE path = '%1'";
    QStringList res = m_collection->sqlStorage()->query( query.arg( AMAROK_UNSET_MAGIC ) );

    // We already have the AMAROK_UNSET_MAGIC variable in the database
    if( !res.isEmpty() )
    {
        m_unsetImageId = res[0].toInt();
    }
    else
    {
        // We need to create this value
        query = QString( "INSERT INTO images( path ) VALUES ( '%1' )" )
                         .arg( m_collection->sqlStorage()->escape( AMAROK_UNSET_MAGIC ) );
        m_unsetImageId = m_collection->sqlStorage()->insert( query, "images" );
    }
    return m_unsetImageId;
}

bool
SqlAlbum::isCompilation() const
{
    return !hasAlbumArtist();
}

bool
SqlAlbum::hasAlbumArtist() const
{
    return !albumArtist().isNull();
}

Meta::ArtistPtr
SqlAlbum::albumArtist() const
{
    if( m_artistId > 0 && !m_artist )
    {
        const_cast<SqlAlbum*>( this )->m_artist =
            m_collection->registry()->getArtist( m_artistId );
    }
    return m_artist;
}

QByteArray
SqlAlbum::md5sum( const QString& artist, const QString& album, const QString& file ) const
{
    // FIXME: names with unicode characters are not supported.
    // FIXME: "The Beatles"."Collection" and "The"."Beatles Collection" will produce the same hash.
    // FIXME: Correcting this now would invalidate all existing image stores.
    KMD5 context( artist.toLower().toLocal8Bit() + album.toLower().toLocal8Bit() + file.toLocal8Bit() );
    return context.hexDigest();
}

QString
SqlAlbum::largeDiskCachePath() const
{
    // IMPROVEMENT: the large disk cache path could be human readable
    const QString artist = hasAlbumArtist() ? albumArtist()->name() : QString();
    if( artist.isEmpty() && m_name.isEmpty() )
        return QString();

    QDir largeCoverDir( Amarok::saveLocation( "albumcovers/large/" ) );
    const QString key = md5sum( artist, m_name, QString() );
        return largeCoverDir.filePath( key );
}

QString
SqlAlbum::scaledDiskCachePath( int size ) const
{
    const QByteArray widthKey = QByteArray::number( size ) + '@';
    QDir cacheCoverDir( Amarok::saveLocation( "albumcovers/cache/" ) );
    QString key = md5sum( QString(), QString(), m_imagePath );

    if( !cacheCoverDir.exists( widthKey + key ) )
    {
        // the correct location is empty
        // check deprecated locations for the image cache and delete them
        // (deleting the scaled image cache is fine)

        const QString artist = hasAlbumArtist() ? albumArtist()->name() : QString();
        if( artist.isEmpty() && m_name.isEmpty() )
            ; // do nothing special
        else
        {
            QString oldKey;
            oldKey = md5sum( artist, m_name, m_imagePath );
            if( cacheCoverDir.exists( widthKey + oldKey ) )
                cacheCoverDir.remove( widthKey + oldKey );

            oldKey = md5sum( artist, m_name, QString() );
            if( cacheCoverDir.exists( widthKey + oldKey ) )
                cacheCoverDir.remove( widthKey + oldKey );
        }
    }

    return cacheCoverDir.filePath( widthKey + key );
}

QString
SqlAlbum::largeImagePath()
{
    // Look up in the database
    QString query = "SELECT images.id, images.path FROM images, albums WHERE albums.image = images.id AND albums.id = %1;"; // TODO: shouldn't we do a JOIN here?
    QStringList res = m_collection->sqlStorage()->query( query.arg( m_id ) );
    if( !res.isEmpty() )
    {
        m_imageId = res.at(0).toInt();
        m_imagePath = res.at(1);

        // explicitely deleted image
        if( m_imagePath == AMAROK_UNSET_MAGIC )
            return AMAROK_UNSET_MAGIC;

        // embedded image (e.g. id3v2 APIC
        if( m_imagePath.startsWith( "amarok-sqltrackuid://" ) )
        {
            // Amarok 2.3.2 did store embedded images as unique ids in the database
            setImage( m_imagePath );
            return m_imagePath;
        }

        // normal file
        if( !m_imagePath.isEmpty() && QFile::exists( m_imagePath ) )
            return m_imagePath;
    }

    // After a rescan we currently lose all image information, so we need
    // to check that we haven't already downloaded this image before.
    m_imagePath = largeDiskCachePath();
    if( !m_imagePath.isEmpty() && QFile::exists( m_imagePath ) ) {
        setImage(m_imagePath);
        return m_imagePath;
    }

    m_imageId = -1;
    m_imagePath = QString();

    return QString();
}

// note: we won't notify the observers. we are a private function. the caller must do that.
void
SqlAlbum::setImage( const QString &path )
{
    if( m_imagePath == path )
        return;
    if( m_name.isEmpty() ) // the empty album never has an image
        return;

    QMutexLocker locker( &m_mutex );

    QString imagePath = path;

    QString query = "SELECT id FROM images WHERE path = '%1'";
    query = query.arg( m_collection->sqlStorage()->escape( imagePath ) );
    QStringList res = m_collection->sqlStorage()->query( query );

    if( res.isEmpty() )
    {
        QString insert = QString( "INSERT INTO images( path ) VALUES ( '%1' )" )
                            .arg( m_collection->sqlStorage()->escape( imagePath ) );
        m_imageId = m_collection->sqlStorage()->insert( insert, "images" );
    }
    else
        m_imageId = res[0].toInt();

    if( m_imageId >= 0 )
    {
        query = QString("UPDATE albums SET image = %1 WHERE albums.id = %2" )
                    .arg( QString::number( m_imageId ), QString::number( m_id ) );
        m_collection->sqlStorage()->query( query );

        m_imagePath = imagePath;
        m_hasImage = true;
        m_hasImageChecked = true;
        m_pixmapCacheDirty = true;
    }
}

void
SqlAlbum::setCompilation( bool compilation )
{
    if( m_name.isEmpty() )
        return;

    if( isCompilation() == compilation )
    {
        return;
    }
    else
    {
        m_collection->blockUpdatedSignal();

        if( compilation )
        {
            // get the new compilation album
            Meta::AlbumPtr metaAlbum = m_collection->registry()->getAlbum( name(), QString() );
            KSharedPtr<SqlAlbum> sqlAlbum = KSharedPtr<SqlAlbum>::dynamicCast( metaAlbum );

            Meta::FieldHash changes;
            changes.insert( Meta::valCompilation, 1);

            Meta::TrackList myTracks = tracks();
            foreach( Meta::TrackPtr metaTrack, myTracks )
            {
                SqlTrack* sqlTrack = static_cast<SqlTrack*>(metaTrack.data());

                // copy over the cover image
                if( sqlTrack->album()->hasImage() && !sqlAlbum->hasImage() )
                    sqlAlbum->setImage( sqlTrack->album()->imageLocation().path() );

                // move the track
                sqlTrack->setAlbum( sqlAlbum->id() );
                Meta::Tag::writeTags( sqlTrack->playableUrl().path(), changes );
            }
            /* TODO: delete all old tracks albums */
        }
        else
        {
            Meta::FieldHash changes;
            changes.insert( Meta::valCompilation, 0);

            Meta::TrackList myTracks = tracks();
            foreach( Meta::TrackPtr metaTrack, myTracks )
            {
                SqlTrack* sqlTrack = static_cast<SqlTrack*>(metaTrack.data());
                Meta::ArtistPtr trackArtist = sqlTrack->artist();

                // get the new album
                Meta::AlbumPtr metaAlbum = m_collection->registry()->getAlbum(
                    sqlTrack->album()->name(),
                    trackArtist ? ArtistHelper::realTrackArtist( trackArtist->name() ) : QString() );
                KSharedPtr<SqlAlbum> sqlAlbum = KSharedPtr<SqlAlbum>::dynamicCast( metaAlbum );

                // copy over the cover image
                if( sqlTrack->album()->hasImage() && !sqlAlbum->hasImage() )
                    sqlAlbum->setImage( sqlTrack->album()->imageLocation().path() );

                // move the track
                sqlTrack->setAlbum( sqlAlbum->id() );
                Meta::Tag::writeTags( sqlTrack->playableUrl().path(), changes );
            }
            /* TODO //step 5: delete the original album, if necessary */
        }

        m_collection->unblockUpdatedSignal();
    }
}

bool
SqlAlbum::hasCapabilityInterface( Capabilities::Capability::Type type ) const
{
    if( m_name.isEmpty() )
        return false;

    return m_collection->albumCapabilityDelegate() &&
        m_collection->albumCapabilityDelegate()->hasCapabilityInterface( type, this );
}

Capabilities::Capability*
SqlAlbum::createCapabilityInterface( Capabilities::Capability::Type type )
{
    if( m_name.isEmpty() )
        return 0;

    if( m_collection->albumCapabilityDelegate() )
        return m_collection->albumCapabilityDelegate()->createCapabilityInterface( type, this );
    return 0;
}

//---------------SqlComposer---------------------------------

SqlComposer::SqlComposer( Collections::SqlCollection* collection, int id, const QString &name ) : Composer()
    ,m_collection( QPointer<Collections::SqlCollection>( collection ) )
    ,m_id( id )
    ,m_name( name )
    ,m_tracksLoaded( false )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

void
SqlComposer::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_tracks.clear();
}

TrackList
SqlComposer::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
    {
        return m_tracks;
    }
    else if( m_collection )
    {
        Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
        qm->setQueryType( Collections::QueryMaker::Track );
        addMatchTo( qm );
        qm->setBlocking( true );
        qm->run();
        m_tracks = qm->tracks( m_collection->collectionId() );
        delete qm;
        m_tracksLoaded = true;
        return m_tracks;
    }
    else
        return TrackList();
}

//---------------SqlGenre---------------------------------

SqlGenre::SqlGenre( Collections::SqlCollection* collection, int id, const QString &name ) : Genre()
    ,m_collection( QPointer<Collections::SqlCollection>( collection ) )
    ,m_id( id )
    ,m_name( name )
    ,m_tracksLoaded( false )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

void
SqlGenre::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_tracks.clear();
}

TrackList
SqlGenre::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
        return m_tracks;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Track );
    addMatchTo( qm );
    qm->setBlocking( true );
    qm->run();
    m_tracks = qm->tracks( m_collection->collectionId() );
    delete qm;
    m_tracksLoaded = true;
    return m_tracks;
}

//---------------SqlYear---------------------------------

SqlYear::SqlYear( Collections::SqlCollection* collection, int id, int year) : Year()
    ,m_collection( QPointer<Collections::SqlCollection>( collection ) )
    ,m_id( id )
    ,m_year( year )
    ,m_tracksLoaded( false )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

void
SqlYear::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_tracks.clear();
}

TrackList
SqlYear::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
        return m_tracks;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Track );
    addMatchTo( qm );
    qm->setBlocking( true );
    qm->run();
    m_tracks = qm->tracks( m_collection->collectionId() );
    delete qm;
    m_tracksLoaded = true;
    return m_tracks;
}

//---------------SqlLabel---------------------------------

SqlLabel::SqlLabel( Collections::SqlCollection *collection, int id, const QString &name ) : Meta::Label()
    ,m_collection( QPointer<Collections::SqlCollection>( collection ) )
    ,m_id( id )
    ,m_name( name )
    ,m_tracksLoaded( false )
{
    Q_ASSERT( m_collection );
    Q_ASSERT( m_id > 0 );
}

void
SqlLabel::invalidateCache()
{
    QMutexLocker locker( &m_mutex );
    m_tracksLoaded = false;
    m_tracks.clear();
}

TrackList
SqlLabel::tracks()
{
    QMutexLocker locker( &m_mutex );
    if( m_tracksLoaded )
        return m_tracks;

    Collections::SqlQueryMaker *qm = static_cast< Collections::SqlQueryMaker* >( m_collection->queryMaker() );
    qm->setQueryType( Collections::QueryMaker::Track );
    addMatchTo( qm );
    qm->setBlocking( true );
    qm->run();
    m_tracks = qm->tracks();
    delete qm;
    m_tracksLoaded = true;
    return m_tracks;
}

