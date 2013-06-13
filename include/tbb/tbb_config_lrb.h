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

#ifndef __TBB_tbb_config_lrb_H
#define __TBB_tbb_config_lrb_H

#if !__LRB__
    #error tbb_config_lrb.h should be included only when building for LRB platform
#endif

#if __TBB_LRB_NATIVE
/** Alpha LRB compilers have problems with floating point code generation. Thus
    the corresponding pieces of TBB unit tests are disabled. **/
#define __TBB_FLOATING_POINT_BROKEN 1

/** Early LRB hardware does not support mfence and pause instructions **/
#define __TBB_rel_acq_fence __TBB_release_consistency_helper
#define __TBB_Pause(x) _mm_delay_32(x)

#if !__FreeBSD__
    #error LRB compiler does not define __FreeBSD__ anymore. Check for the __TBB_XXX_BROKEN defined under __FreeBSD__
#endif /* !__FreeBSD__ */
#endif /*__TBB_LRB_NATIVE*/

#endif /* __TBB_tbb_config_lrb_H */
