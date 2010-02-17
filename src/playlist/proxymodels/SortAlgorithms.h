/****************************************************************************************
 * Copyright (c) 2009 Téo Mrnjavac <teo.mrnjavac@gmail.com>                             *
 * Copyright (c) 2010 Nanno Langstraat <langstr@gmail.com>                              *
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

#ifndef AMAROK_SORTALGORITHMS_H
#define AMAROK_SORTALGORITHMS_H

#include "ProxyBase.h"
#include "SortScheme.h"

namespace Playlist
{

/**
 * This struct defines a comparison functor that can be used by qSort(), qStableSort(), or
 * other sorting algorithms with a similar interface.
 * The comparison is operated on multiple levels of a Playlist::SortScheme.
 * @warning This functor is specific for this particular problem and wouldn't probably do
 * any good for sorting anything else than tracks.
 * @author Téo Mrnjavac <teo.mrnjavac@gmail.com>
 */
struct multilevelLessThan
{
    /**
     * Constructor.
     * @param sourceProxy a pointer to the underlying proxy instance.
     * @param scheme the sorting scheme that needs to be applied.
     */
    multilevelLessThan( QAbstractItemModel *sourceModel, const SortScheme &scheme )
        : m_sourceModel( sourceModel )
        , m_scheme( scheme )
        , m_randomSalt( qrand() )
    {}

    /**
     * Takes two row numbers from the proxy and compares the corresponding indexes based on
     * a number of chosen criteria (columns).
     * @param rowA the first row.
     * @param rowB the second row.
     * @return true if rowA is to be placed before rowB, false otherwise.
     */
    bool operator()( int rowA, int rowB );

    protected:
        /**
         * For random sort, we want to assign a random sequence number to each row in the
         * source model.
         *
         * On the other hand, the sequence number must stay constant for any given row.
         * The QSortFilterProxyModel sort code can ask us about the same row twice, and
         * we need to return consistent answers.
         *
         * It would be even nicer if the sequence number stayed constant for a given
         * *item* instead of *row*, but that is not truly necessary and costs performance.
         *
         * The sequence numbers don't need to be contiguous.
         * The sequence numbers don't need to be unique; a few collisions are no problem.
         *
         * @return a sequence number that is random, but constant for 'sourceRow'.
         */
        long constantRandomSeqnumForRow( int sourceRow );

    private:
        QAbstractItemModel *m_sourceModel;     //! The underlying model which holds the rows that need to be sorted.
        SortScheme m_scheme;           //! The current sorting scheme.
        long m_randomSalt;    //! Change the random row order from run to run.
};

}   //namespace Playlist

#endif  //AMAROK_SORTALGORITHMS_H
