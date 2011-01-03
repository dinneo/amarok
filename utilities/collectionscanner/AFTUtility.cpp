/***************************************************************************
 *   Copyright (C) 2008-2009 Jeff Mitchell <mitchell@kde.org>              *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "AFTUtility.h"

//Taglib:
#include <apetag.h>
#include <flacfile.h>
#include <id3v1tag.h>
#include <id3v2tag.h>
#include <mpcfile.h>
#include <mpegfile.h>
#include <mp4item.h>
#include <mp4file.h>
#include <mp4tag.h>
#include <oggfile.h>
#include <oggflacfile.h>
#include <speexfile.h>
#include <textidentificationframe.h>
#include <tlist.h>
#include <uniquefileidentifierframe.h>
#include <vorbisfile.h>
#include <xiphcomment.h>

#include <QFile>
#include <QRegExp>
#include <QTime>

#define Qt4QStringToTString(s) TagLib::String(s.toUtf8().data(), TagLib::String::UTF8)

AFTUtility::AFTUtility()
{
    qsrand( QTime::currentTime().msec() );
}

const TagLib::ByteVector
generatedUniqueIdHelper( const TagLib::FileRef &fileref )
{
    if ( TagLib::MPEG::File *file = dynamic_cast<TagLib::MPEG::File *>( fileref.file() ) )
    {
        if( file->ID3v2Tag() )
            return file->ID3v2Tag()->render();
        else if( file->ID3v1Tag() )
            return file->ID3v1Tag()->render();
        else if( file->APETag() )
            return file->APETag()->render();
    }
    else if ( TagLib::Ogg::Vorbis::File *file = dynamic_cast<TagLib::Ogg::Vorbis::File *>( fileref.file() ) )
    {
        if( file->tag() )
            return file->tag()->render();
    }
    else if ( TagLib::Ogg::Speex::File *file = dynamic_cast<TagLib::Ogg::Speex::File *>( fileref.file() ) )
    {
        if( file->tag() )
            return file->tag()->render();
    }
    else if ( TagLib::Ogg::FLAC::File *file = dynamic_cast<TagLib::Ogg::FLAC::File *>( fileref.file() ) )
    {
        if( file->tag() )
            return file->tag()->render();
    }
    else if ( TagLib::FLAC::File *file = dynamic_cast<TagLib::FLAC::File *>( fileref.file() ) )
    {
        if( file->xiphComment() )
            return file->xiphComment()->render();
        else if( file->ID3v1Tag() )
            return file->ID3v1Tag()->render();
        else if( file->ID3v2Tag() )
            return file->ID3v2Tag()->render();
    }
    else if ( TagLib::MPC::File *file = dynamic_cast<TagLib::MPC::File *>( fileref.file() ) )
    {
        if( file->ID3v1Tag() )
            return file->ID3v1Tag()->render();
        else if( file->APETag() )
            return file->APETag()->render();
    }
    TagLib::ByteVector bv;
    return bv;
}

const QString
AFTUtility::readEmbeddedUniqueId( const TagLib::FileRef &fileref )
{
    int currentVersion = 1; //TODO: Make this more global?
    QString ourId = QString( "Amarok 2 AFTv" + QString::number( currentVersion )
                             + " - amarok.kde.org" );
    TagLib::String AFT_ID = Qt4QStringToTString( ourId.toUpper() );     //Used in Vorbis comments and APE tags.
    QString mbId = QString( "http://musicbrainz.org" );
    TagLib::String mbTID = TagLib::String( "MUSICBRAINZ_TRACKID" );     //Used in Vorbis comments and APE tags.
    QString storedMBId;
    QString mbDefaultUUID = QString( "[mb track uuid]" );
    QRegExp aftPattern( "[a-f0-9]+", Qt::CaseInsensitive );
    QRegExp mbIdPattern( "[-a-f0-9]+", Qt::CaseInsensitive );

    if( TagLib::MPEG::File *file = dynamic_cast<TagLib::MPEG::File *>( fileref.file() ) )
    {
        if( !file->ID3v2Tag( false ) )
            return QString();
        if( file->ID3v2Tag()->frameListMap()["UFID"].isEmpty() )
            return QString();
        TagLib::ID3v2::FrameList frameList = file->ID3v2Tag()->frameListMap()["UFID"];
        TagLib::ID3v2::FrameList::Iterator iter;
        for( iter = frameList.begin(); iter != frameList.end(); ++iter )
        {
            TagLib::ID3v2::UniqueFileIdentifierFrame* currFrame = dynamic_cast<TagLib::ID3v2::UniqueFileIdentifierFrame*>(*iter);
            if( currFrame )
            {
                QString owner = TStringToQString( currFrame->owner() );
                if( owner.compare( ourId, Qt::CaseInsensitive ) == 0 )
                {
                    QString identifier = TStringToQString( TagLib::String( currFrame->identifier() ) ).toLower();
                    if( aftPattern.exactMatch( identifier ) )
                        return identifier;
                    else
                        return QString();
                }
                else if( owner.compare( mbId, Qt::CaseInsensitive ) == 0 )
                    storedMBId = TStringToQString( TagLib::String( currFrame->identifier() ) ).toLower();
            }
        }
        if( !storedMBId.isEmpty() && ( storedMBId != mbDefaultUUID ) &&
            mbIdPattern.exactMatch( storedMBId ) )
            return QString( "mb-" ) + storedMBId;
    }
    else if( TagLib::MP4::File *file = dynamic_cast<TagLib::MP4::File *>( fileref.file() ) )
    {
        const TagLib::MP4::ItemListMap &itemsMap = file->tag()->itemListMap();
        for( TagLib::MP4::ItemListMap::ConstIterator it = itemsMap.begin(); it != itemsMap.end(); ++it )
        {
            const TagLib::String &key = it->first;
            if( key.find( Qt4QStringToTString( ourId ) ) != -1 )
            {
                QString identifier = TStringToQString( it->second.toStringList().toString() );
                if( !identifier.isEmpty() && aftPattern.exactMatch( identifier ) )
                    return identifier;
            }
            else if( key.find( "MusicBrainz Track Id" ) != -1 )
            {
                QString identifier = TStringToQString( it->second.toStringList().toString() );
                if( !identifier.isEmpty() && ( identifier != mbDefaultUUID )
                    && mbIdPattern.exactMatch( identifier ) )
                    return QString( "mb-" ) + identifier;
            }
        }
    }
    else if( TagLib::MPC::File *file = dynamic_cast<TagLib::MPC::File *>( fileref.file() ) )
    {
        if( file->APETag() )
        {
            const TagLib::APE::ItemListMap &itemsMap = file->APETag()->itemListMap();
            for( TagLib::APE::ItemListMap::ConstIterator it = itemsMap.begin(); it != itemsMap.end(); ++it )
            {
                if( it->first == AFT_ID )
                {
                    QString identifier = TStringToQString( it->second.toString() );
                    if( !identifier.isEmpty() && aftPattern.exactMatch( identifier ) )
                        return identifier;
                }
                else if( it->first == mbTID )
                {
                    QString identifier = TStringToQString( it->second.toString() );
                    if( !identifier.isEmpty() && ( identifier != mbDefaultUUID ) && mbIdPattern.exactMatch( identifier ) )
                        return QString( "mb-" ) + identifier;
                }
            }
        }
    }

    //from here below assumes a file with a XiphComment; put non-conforming formats up above...
    TagLib::Ogg::XiphComment *comment = 0;
    if( TagLib::FLAC::File *file = dynamic_cast<TagLib::FLAC::File *>( fileref.file() ) )
        comment = file->xiphComment( false );
    else if( TagLib::Ogg::File *file = dynamic_cast<TagLib::Ogg::File *>( fileref.file() ) )
    {
        if( dynamic_cast<TagLib::Ogg::FLAC::File*>(file) )
            comment = ( dynamic_cast<TagLib::Ogg::FLAC::File*>(file) )->tag();
        else if( dynamic_cast<TagLib::Ogg::Speex::File*>(file) )
            comment = ( dynamic_cast<TagLib::Ogg::Speex::File*>(file) )->tag();
        else if( dynamic_cast<TagLib::Ogg::Vorbis::File*>(file) )
            comment = ( dynamic_cast<TagLib::Ogg::Vorbis::File*>(file) )->tag();
    }

    if( !comment )
        return QString();

    if( comment->contains( AFT_ID ) )
    {
        QString identifier = TStringToQString( comment->fieldListMap()[ AFT_ID ].front()).toLower();
        if( aftPattern.exactMatch( identifier ) )
            return identifier;
        else
            return QString();
    }
    else if( comment->contains( mbTID ) )
    {
        QString identifier = TStringToQString( comment->fieldListMap()[ mbTID ].front() ).toLower();
        if( !identifier.isEmpty() && ( identifier != mbDefaultUUID ) &&
            mbIdPattern.exactMatch( identifier ) )
            return QString( "mb-" ) + identifier;
    }

    return QString();
}



const QString
AFTUtility::randomUniqueId( QCryptographicHash &md5 )
{
    //md5 has size of file already added for some little extra randomness for the hash
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    md5.addData( QString::number( qrand() ).toAscii() );
    return QString( md5.result().toHex() );
}

const QString
AFTUtility::readUniqueId( const QString &path, const TagLib::FileRef &fref )
{
    const TagLib::FileRef &fileref = fref.isNull() ? createFileRef( path ) : fref;

    QCryptographicHash md5( QCryptographicHash::Md5 );
    QFile qfile( path );
    QByteArray size;
    md5.addData( size.setNum( qfile.size() ) );

    if( fileref.isNull() )
        return randomUniqueId( md5 );

    const QString embeddedString = readEmbeddedUniqueId( fileref );
    if( !embeddedString.isEmpty() )
        return embeddedString;

    TagLib::ByteVector bv = generatedUniqueIdHelper( fileref );

    md5.addData( bv.data(), bv.size() );

    char databuf[16384];
    int readlen = 0;
    QString returnval;

    if( qfile.open( QIODevice::ReadOnly ) )
    {
        if( ( readlen = qfile.read( databuf, 16384 ) ) > 0 )
        {
            md5.addData( databuf, readlen );
            qfile.close();
            return QString( md5.result().toHex() );
        }
        else
        {
            qfile.close();
            return randomUniqueId( md5 );
        }
    }

    return randomUniqueId( md5 );
}

const TagLib::FileRef
AFTUtility::createFileRef( const QString &path )
{
#ifdef COMPLEX_TAGLIB_FILENAME
    const wchar_t * encodedName = reinterpret_cast<const wchar_t *>(path.utf16());
#else
    QByteArray fileName = QFile::encodeName( path );
    const char * encodedName = fileName.constData(); // valid as long as fileName exists
#endif
    return TagLib::FileRef( encodedName, true, TagLib::AudioProperties::Fast );
}