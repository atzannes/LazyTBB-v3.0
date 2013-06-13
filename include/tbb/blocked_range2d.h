/*
    Copyright 2005-2010 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#ifndef __TBB_blocked_range2d_H
#define __TBB_blocked_range2d_H

#include "tbb_stddef.h"
#include "blocked_range.h"
#include <string>
#include <math.h>

namespace tbb {

//! A 2-dimensional range that models the Range concept.
/** @ingroup algorithms */
template<typename RowValue, typename ColValue=RowValue>
class blocked_range2d {
public:
    //! Type for size of an iteration range
    typedef blocked_range<RowValue> row_range_type;
    typedef blocked_range<ColValue> col_range_type;
 
private:
    row_range_type my_rows;
    col_range_type my_cols;

public:
    std::string to_string() {
       std::stringstream out;
       out << my_rows.to_string() << my_cols.to_string();
       return out.str();
    }

    blocked_range2d () : my_rows(), my_cols() {}

    blocked_range2d( RowValue row_begin, RowValue row_end, typename row_range_type::size_type row_grainsize,
                     ColValue col_begin, ColValue col_end, typename col_range_type::size_type col_grainsize ) : 
        my_rows(row_begin,row_end,row_grainsize),
        my_cols(col_begin,col_end,col_grainsize)
    {
    }

    blocked_range2d( RowValue row_begin, RowValue row_end,
                     ColValue col_begin, ColValue col_end ) : 
        my_rows(row_begin,row_end),
        my_cols(col_begin,col_end)
    {
    }

    //! True if range is empty
    bool empty() const {
        // Yes, it is a logical OR here, not AND.
        return my_rows.empty() || my_cols.empty();
    }

    //! True if range is divisible into two pieces.
    bool is_divisible() const {
        return my_rows.is_divisible() || my_cols.is_divisible();
    }

    //! Splits range unevenly
    /** */
    blocked_range2d get_some() {
        blocked_range2d some;
        if( my_rows.size()*double(my_cols.grainsize()) < my_cols.size()*double(my_rows.grainsize()) ) {
            some = blocked_range2d ( my_rows.my_begin,
            		                     my_rows.my_end,
            		                     my_rows.my_grainsize,
                                         my_cols.my_begin,
                                         my_cols.my_begin+my_cols.my_grainsize,
                                         my_cols.my_grainsize);
            my_cols.my_begin += my_cols.my_grainsize;
        } else {
            some = blocked_range2d ( my_rows.my_begin,
            		                     my_rows.my_begin+my_rows.my_grainsize,
            		                     my_rows.my_grainsize,
                                         my_cols.my_begin,
                                         my_cols.my_end,
                                         my_cols.my_grainsize);
            my_rows.my_begin += my_rows.my_grainsize;
        }
        return some;
    }

    //! Creates a copy of this and empties this range by reducing the end to the begining.
    // It is important not to increase the begining but to reduce the end for correctness
    // of Melded code. This operation can be seen as a split where the middle of the split
    // is equal to the begining of the original range.
    inline blocked_range2d get_rest() {
        //blocked_range rest(this);
        blocked_range2d rest(my_rows.my_begin, my_rows.my_end, my_rows.my_grainsize, 
                             my_cols.my_begin, my_cols.my_end, my_rows.my_grainsize);
        my_rows.my_end = my_rows.my_begin;
        my_cols.my_end = my_cols.my_begin;
        return rest;
    }

    //! Double the Grainsize value
    void double_grainsize() {
    	if (my_rows.my_grainsize < my_cols.my_grainsize) {
    		my_rows.my_grainsize *= 2;
    	} else {
    		my_cols.my_grainsize *= 2;
    	}
    }

    //! Set the Grainsize value: the trick is that col_grainsize*row_grainsize
    //  should be ~grainsize
    void set_grainsize(int grainsize) {
    	double root = ceil(sqrt(grainsize));
    	my_rows.my_grainsize = root;
    	my_cols.my_grainsize = root;
    }

    blocked_range2d( blocked_range2d& r, split ) : 
        my_rows(r.my_rows),
        my_cols(r.my_cols)
    {
    	// if (row.size/row.grain < col.size/col.grain) then split cols else split rows
        if( my_rows.size()*double(my_cols.grainsize()) < my_cols.size()*double(my_rows.grainsize()) ) {
            my_cols.my_begin = col_range_type::do_split(r.my_cols);
        } else {
            my_rows.my_begin = row_range_type::do_split(r.my_rows);
        }
    }

    //! The rows of the iteration space 
    const row_range_type& rows() const {return my_rows;}

    //! The columns of the iteration space 
    const col_range_type& cols() const {return my_cols;}
};

} // namespace tbb 

#endif /* __TBB_blocked_range2d_H */
