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

#ifndef __TBB_parallel_for_H
#define __TBB_parallel_for_H

#include "task.h"
#include "partitioner.h"
#include "blocked_range.h"
#include <new>
#include <iostream>
#include "tbb_exception.h"

//#include "scheduler.h"

namespace tbb {

//! @cond INTERNAL
namespace internal {

    //! Task type used in parallel_for
    /** @ingroup algorithms */
    template<typename Range, typename Body, typename Partitioner>
    class start_for: public task {
        Range my_range;
        const Body my_body;
        typename Partitioner::partition_type my_partition;
#if defined TBB_NARY_SYNC || defined __TBB_LAZY_BF_EXECUTION
        task *my_continuation;
#endif
        /*override*/ task* execute();
        task* eager_execute();
        task* lazy_execute_df();
        task* lazy_execute_bf();


        //! Constructor for root task.
        start_for( const Range& range, const Body& body, Partitioner& partitioner ) :
            my_range(range),    
            my_body(body),
            my_partition(partitioner)
#if defined TBB_NARY_SYNC || defined __TBB_LAZY_BF_EXECUTION
            ,my_continuation(NULL)
#endif
        {
#ifdef __TBB_LAZY_BF_EXECUTION
        	// FIXME This is now initialized by the default constructor
        	//bflws_pushed = false;
#endif
        }

        //! Copy Constructor (for root task).
        start_for( start_for& parent_) :
            my_range(parent_.my_range.get_rest()),
            my_body(parent_.my_body),
            my_partition(parent_.my_partition)
#if defined TBB_NARY_SYNC || defined __TBB_LAZY_BF_EXECUTION
			//, my_continuation(parent_.my_continuation)
			, my_continuation(NULL)
#endif
        {
            my_partition.set_affinity(*this);
        }
        //! Splitting constructor used to generate children.
        /** this becomes left child.  Newly constructed object is right child. */
        start_for( start_for& parent_, split ) :
            my_range(parent_.my_range,split()),    
            my_body(parent_.my_body),
            my_partition(parent_.my_partition,split())
#if defined TBB_NARY_SYNC || defined __TBB_LAZY_BF_EXECUTION
			, my_continuation(NULL)
#endif
        {
            my_partition.set_affinity(*this);
        }
        //! Update affinity info, if any.
        /*override*/ void note_affinity( affinity_id id ) {
            my_partition.note_affinity( id );
        }

        //! Does this task type implement a split constructor?
        /*override*/ bool is_splittable () { return true; }

        //! Does this task still contain work?
        /*override*/ bool is_empty_task() {
        	/*bool result = my_range.empty();
        	if (result) printf ("Lala = %d\n", result);
        	return result;*/
        	return my_range.empty();
        }

        //! Is this task object splittable
        inline bool am_i_splittable() {
        	 return my_range.is_divisible() && !my_partition.should_execute_range(*this);
        }

        //!
        inline void split_postponed(bool delay) {
#if defined TBB_NARY_SYNC || defined __TBB_LAZY_BF_EXECUTION
    		if (my_continuation == NULL) {
    			my_continuation = new( allocate_continuation() ) empty_task;
    			recycle_as_child_of(*my_continuation);
    			my_continuation->set_ref_count(2);
    			start_for& b = *new( my_continuation->allocate_child() ) start_for(*this,split());
    			my_partition.spawn_or_delay(delay,b);
    		} else {
    			start_for& b = *new( allocate_additional_child_of(*my_continuation) ) start_for(*this,split());
    			my_partition.spawn_or_delay(delay,b);
    		}
#else
           	empty_task& c = *new( this->allocate_continuation() ) empty_task;
           	recycle_as_child_of(c);
           	c.set_ref_count(2);
           	start_for& b = *new( c.allocate_child() ) start_for(*this,split());
           	my_partition.spawn_or_delay(delay,b);
#endif
        }

#ifdef __TBB_LAZY_BF_EXECUTION
        //!spawn or split postponed task. Return true when pushed and false when split
        /*override*/bool spawn_postponed () {
    		bool delay = my_partition.decide_whether_to_delay();
        	if (am_i_splittable() == true) {
        		// split and spawn half
        		split_postponed(delay);
        		return false;
        	} else { // i_am_not_splittable
        		bflws_pushed = true;
        		if (my_continuation == NULL) {
        			/*my_continuation = new( allocate_continuation() ) empty_task;
        			recycle_as_child_of(*my_continuation);
        			my_continuation->set_ref_count(2);
        			start_for& b = *new( my_continuation->allocate_child() ) start_for(*this);//*/

        			// FIXME shouldn't we split the exiting task into two or is it enough
        			// that we explicitly spawn the "continuation"
        			start_for& b = *new( allocate_continuation() )  start_for(*this);
        			// FIXME the next line is probably not needed now that the constructor
        			// initialized bflws_pushed to false
        			my_partition.spawn_or_delay(delay,b);
        		} else {
        			start_for& b = *new( allocate_additional_child_of(*my_continuation) ) start_for(*this);
        			// FIXME the next line is probably not needed now that the constructor
        			// initialized bflws_pushed to false
        			my_partition.spawn_or_delay(delay,b);
        		}//*/
        		return true;
        	}
        }
#endif

    public:
        static void run(  const Range& range, const Body& body, const Partitioner& partitioner ) {
            if( !range.empty() ) {
#if !__TBB_TASK_GROUP_CONTEXT || TBB_JOIN_OUTER_TASK_GROUP
                start_for& a = *new(task::allocate_root()) start_for(range,body,const_cast<Partitioner&>(partitioner));
#else
                // Bound context prevents exceptions from body to affect nesting or sibling algorithms,
                // and allows users to handle exceptions safely by wrapping parallel_for in the try-block.
                task_group_context context;
                start_for& a = *new(task::allocate_root(context)) start_for(range,body,const_cast<Partitioner&>(partitioner));
#endif /* __TBB_TASK_GROUP_CONTEXT && !TBB_JOIN_OUTER_TASK_GROUP */
                task::spawn_root_and_wait(a);
            }
        }
#if __TBB_TASK_GROUP_CONTEXT
        static void run(  const Range& range, const Body& body, const Partitioner& partitioner, task_group_context& context ) {
            if( !range.empty() ) {
                start_for& a = *new(task::allocate_root(context)) start_for(range,body,const_cast<Partitioner&>(partitioner));
                task::spawn_root_and_wait(a);
            }
        }
#endif /* __TBB_TASK_GROUP_CONTEXT */
    };

    /** Execute alias */
    template<typename Range, typename Body, typename Partitioner>
    task* start_for<Range,Body,Partitioner>::execute() {
#ifdef __TBB_LAZY_DF_EXECUTION
    	return start_for<Range,Body,Partitioner>::lazy_execute_df();
#else
#ifdef __TBB_LAZY_BF_EXECUTION
    	return start_for<Range,Body,Partitioner>::lazy_execute_bf();
#else /* __TBB_EAGER_EXECUTION */
    	return start_for<Range,Body,Partitioner>::eager_execute();
#endif
#endif
    }

    /** Eager Execute */
    template<typename Range, typename Body, typename Partitioner>
    task* start_for<Range,Body,Partitioner>::eager_execute() {
#ifdef TBB_DEBUG
//    	printf("Running EAGER execute\n");
#endif
        if( am_i_splittable()==false ) {
        	// Execute iterations (if range not divisible or should be
        	//                     executed based on partitioner strategy)
            my_body( my_range );
            // Then continue: we may have a delay-list that we need to spawn
            return my_partition.continue_after_execute_range();
        } else {
#ifdef TBB_DEBUG
        	std::cout << "Eager Splittin' " << my_range.to_string() << "\n";
#endif
        	/* OLD CODE */
            empty_task& c = *new( this->allocate_continuation() ) empty_task;
            recycle_as_child_of(c);
            c.set_ref_count(2);
            bool delay = my_partition.decide_whether_to_delay();
            start_for& b = *new( c.allocate_child() ) start_for(*this,split());
            my_partition.spawn_or_delay(delay,b);
            return this;//*/
        	/* NEW CODE -- replace recursion by while loop - */
            // :: After some experimentation (addmitedly not much), the new
            // :: code doesn't seem to be beneficial for performance, so I am
            // :: commenting it out, but keeping it for reference.
        	// While using a task-list to spawn the logN tasks would improve performance,
            // we still want to spawn some (perhaps just one?) of the work before having
            // to create all logN tasks.
            /*empty_task& c = *new( this->allocate_continuation() ) empty_task;
            recycle_as_child_of(c);
            c.set_ref_count(2);
            bool delay = my_partition.decide_whether_to_delay();
            start_for& b = *new( c.allocate_child() ) start_for(*this,split());
            my_partition.spawn_or_delay(delay,b);
            while( my_range.is_divisible() && !my_partition.should_execute_range(*this) ) {
            	start_for& b = *new( allocate_additional_child_of(c) ) start_for(*this,split());
                my_partition.spawn_or_delay(delay,b);
            }
            return this; // <- this is now the last piece of the task and it will be executed right away.*/
        }
    }

    /** Lazy execute - Depth First*/
    template<typename Range, typename Body, typename Partitioner>
    task* start_for<Range,Body,Partitioner>::lazy_execute_df() {
    	while ( am_i_splittable() == true ) {
#ifdef DEQUE_THRESH
    		if ( task_pool_size() < DEQUE_THRESH) {
#else
    		if ( is_task_pool_empty() ) {
#endif
    			// Split Range, push half and return other half task (to be executed)
#ifdef TBB_DEBUG
            	std::cout << "Lazy Splittin' " << my_range.to_string() << "\n";
#endif
            	bool delay = my_partition.decide_whether_to_delay();
            	split_postponed(delay);
#ifdef LBS_SPLIT_DELAY
            	usleep(LBS_SPLIT_DELAY);
#endif
#ifdef TBB_DEBUG
            	std::cout << "Lazy Splittin' Range after splitting :" << my_range.to_string() << "\n";
#endif
            	// Returning the split task causes it to execute immediately.
            	// The 'NEW CODE' (which is commented out because it does not work)
            	// attempts to split the task further without returning
#ifndef TBB_NARY_SYNC
            	return this;
#endif
    		} else {
        		// Execute some iterations
        		Range subrange = my_range.get_some();
        		my_body( subrange );

    		}
    	} // end while
#ifdef TBB_NARY_SYNC
   		/* NEW CODE v2.0 -- N-ary synchronization */
        if (my_continuation != NULL) {
        	my_continuation = NULL;
        	return this;
        } else {
        	// Execute remaining iterations (if range not divisible or should be
        	//                     executed based on partitioner strategy)
        	my_body( my_range );
			// Then continue: we may have a delay-list that we need to spawn
			// continue_after_execute_range always returns NULL, except in the
			// case of affinity partitioners, where we may have delayed some
			// spawns, in which case it returns one (the first one?) in the
			// delay list.
			return my_partition.continue_after_execute_range();
        }
#else // !TBB_NARY_SYNC, i.e., BINARY_SYNC
    	my_body( my_range );
		// Then continue: we may have a delay-list that we need to spawn
		// continue_after_execute_range always returns NULL, except in the
		// case of affinity partitioners, where we may have delayed some
		// spawns, in which case it returns one (the first one?) in the
		// delay list.
		return my_partition.continue_after_execute_range();
#endif
    }

#ifdef __TBB_LAZY_BF_EXECUTION
    /** Lazy execute - Breadth First*/
    template<typename Range, typename Body, typename Partitioner>
    task* start_for<Range,Body,Partitioner>::lazy_execute_bf() {
   	    if (bflws_pushed == true) {
   	    	//std::cout << "Ha! this code IS executed!\n";
   	    	return my_partition.continue_after_execute_range();
   	    }
   	    /* Add task to postponed_task_list */
   	    // FIXME this can probably be moved to within the first 'split' part
   	    // Save tail and enqueue current task to postponed_task_list
    	this->next_postponed = NULL;
    	// enqueue postponed task
    	task* saved_tail = this->push_postponed_task();

    	while ( am_i_splittable() == true ) {
    #ifdef DEQUE_THRESH
    		if ( task_pool_size() < DEQUE_THRESH) {
    #else
    		if ( is_task_pool_empty() ) {
    #endif
    			// Split Range, push half and return other half task (to be executed)
    			//generic_scheduler* s = (generic_scheduler*)(prefix().owner);
    			task* t2s = get_oldest_postponed_task();
    			bool was_pushed = t2s->spawn_postponed();
    			if (was_pushed) pushed_oldest_postponed_task();
    			//*/

                /*
    			bool success = this->spawn_or_split_oldest_postponed_task();
				if (success == false) {
					std::cerr << "ERROR was not able to spawn or split oldest postponed task (including this one)\n";
					break;
				}//*/
    		} else {
        		// Execute some iterations
#ifdef _TBB_MELD
        		my_body( my_range, this );
#else
        		Range subrange = my_range.get_some();
        		my_body( subrange );
#endif
    		}
    	} // end while
    	// Deque this from postponed_task_list
    	bool consumed = this->pop_postponed_task(saved_tail);
    	////////////////BEGIN DEBUG//////////////////////
    	/*if (bflws_pushed == true) {
    		std::cout << "Consumed = " << consumed << ", bflws_pushed = " << bflws_pushed
    				<< ", stolen:" << this->is_stolen_task() << "\n";
    	}
    	if (consumed != bflws_pushed) {
    		std::cout << "ASSERTION Failed: Consumed = " << consumed << ", bflws_pushed = "
    				<< bflws_pushed << ", stolen:" << this->is_stolen_task() << "\n";
    	}//*/
    	////////////////END DEBUG//////////////////////
    	//if (bflws_pushed == true)
    	//	return my_partition.continue_after_execute_range();
		if (my_continuation!=NULL) {
			my_continuation = NULL;
    		return this;
   		} else {
   			// 'this' was not split => execute the remainder
   			if (bflws_pushed == false)
#ifdef _TBB_MELD
   				my_body( my_range, this );
#else
				my_body( my_range );
#endif
    	}
        return my_partition.continue_after_execute_range();
    }
#endif
} // namespace internal
//! @endcond


// Requirements on Range concept are documented in blocked_range.h

/** \page parallel_for_body_req Requirements on parallel_for body
    Class \c Body implementing the concept of parallel_for body must define:
    - \code Body::Body( const Body& ); \endcode                 Copy constructor
    - \code Body::~Body(); \endcode                             Destructor
    - \code void Body::operator()( Range& r ) const; \endcode   Function call operator applying the body to range \c r.
**/

/** \name parallel_for
    See also requirements on \ref range_req "Range" and \ref parallel_for_body_req "parallel_for Body". **/
//@{

//! Parallel iteration over range with default partitioner. 
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body ) {
    internal::start_for<Range,Body,__TBB_DEFAULT_PARTITIONER>::run(range,body,__TBB_DEFAULT_PARTITIONER());
}

//! Parallel iteration over range with simple partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const simple_partitioner& partitioner ) {
    internal::start_for<Range,Body,simple_partitioner>::run(range,body,partitioner);
}

//! Parallel iteration over range with auto_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const auto_partitioner& partitioner ) {
    internal::start_for<Range,Body,auto_partitioner>::run(range,body,partitioner);
}

//! Parallel iteration over range with affinity_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, affinity_partitioner& partitioner ) {
    internal::start_for<Range,Body,affinity_partitioner>::run(range,body,partitioner);
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration over range with simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const simple_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,simple_partitioner>::run(range, body, partitioner, context);
}

//! Parallel iteration over range with auto_partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const auto_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,auto_partitioner>::run(range, body, partitioner, context);
}

//! Parallel iteration over range with affinity_partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, affinity_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,affinity_partitioner>::run(range,body,partitioner, context);
}
#endif /* __TBB_TASK_GROUP_CONTEXT */
//@}

//! @cond INTERNAL
namespace internal {
    //! Calls the function with values from range [begin, end) with a step provided
template<typename Function, typename Index>
class parallel_for_body : internal::no_assign {
    const Function &my_func;
    const Index my_begin;
    const Index my_step; 
public:
    parallel_for_body( const Function& _func, Index& _begin, Index& _step) 
        : my_func(_func), my_begin(_begin), my_step(_step) {}
    
    void operator()( tbb::blocked_range<Index>& r ) const {
        for( Index i = r.begin(),  k = my_begin + i * my_step; i < r.end(); i++, k = k + my_step)
            my_func( k );
    }
}; // class parallel_for_body
} // namespace internal
//! @endcond

namespace strict_ppl {

//@{
//! Parallel iteration over a range of integers with a step provided
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f) {
    tbb::task_group_context context;
    parallel_for(first, last, step, f, context);
}
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, tbb::task_group_context &context) {
    if (step <= 0 )
        internal::throw_exception(internal::eid_nonpositive_step); // throws std::invalid_argument
    else if (last > first) {
        // Above "else" is necessary to prevent "potential divide by zero" warning
        Index end = (last - first) / step;
        if (first + end * step < last) end++;
        tbb::blocked_range<Index> range(static_cast<Index>(0), end);
        internal::parallel_for_body<Function, Index> body(f, first, step);
        tbb::parallel_for(range, body, tbb::auto_partitioner(), context);
    }
}
//! Parallel iteration over a range of integers with a default step value
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f) {
    tbb::task_group_context context;
    parallel_for(first, last, static_cast<Index>(1), f, context);
}
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f, tbb::task_group_context &context) {
    parallel_for(first, last, static_cast<Index>(1), f, context);
}

//@}

} // namespace strict_ppl

using strict_ppl::parallel_for;

} // namespace tbb

#endif /* __TBB_parallel_for_H */

