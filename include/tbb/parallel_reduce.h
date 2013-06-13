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

#ifndef __TBB_parallel_reduce_H
#define __TBB_parallel_reduce_H

#include "task.h"
#include "aligned_space.h"
#include "partitioner.h"
#include <new>
#include <iostream>

namespace tbb {

//! @cond INTERNAL
namespace internal {

    //! ITT instrumented routine that stores src into location pointed to by dst.
    void __TBB_EXPORTED_FUNC itt_store_pointer_with_release_v3( void* dst, void* src );

    //! ITT instrumented routine that loads pointer from location pointed to by src.
    void* __TBB_EXPORTED_FUNC itt_load_pointer_with_acquire_v3( const void* src );

    template<typename T> inline void parallel_reduce_store_body( T*& dst, T* src ) {
#if TBB_USE_THREADING_TOOLS
        itt_store_pointer_with_release_v3(&dst,src);
#else
        __TBB_store_with_release(dst,src);
#endif /* TBB_USE_THREADING_TOOLS */
    }

    template<typename T> inline T* parallel_reduce_load_body( T*& src ) {
#if TBB_USE_THREADING_TOOLS
        return static_cast<T*>(itt_load_pointer_with_acquire_v3(&src));
#else
        return __TBB_load_with_acquire(src);
#endif /* TBB_USE_THREADING_TOOLS */
    }

    //! 0 if root, 1 if a left child, 2 if a right child.
    /** Represented as a char, not enum, for compactness. */
    typedef char reduction_context;

    //! Task type use to combine the partial results of parallel_reduce.
    /** @ingroup algorithms */
    template<typename Body>
    class finish_reduce: public task {
        //! Pointer to body, or NULL if the left child has not yet finished. 
        Body* my_body;
        bool has_right_zombie;
        const reduction_context my_context;
        aligned_space<Body,1> zombie_space;
        finish_reduce( char context_ ) : 
            my_body(NULL),
            has_right_zombie(false),
            my_context(context_)
        {
        }
        task* execute() {
            if( has_right_zombie ) {
                // Right child was stolen.
                Body* s = zombie_space.begin();
                //std::cout << "Joining...!\n";
                my_body->join( *s );
                s->~Body();
            }
            if( my_context==1 ) 
                parallel_reduce_store_body( static_cast<finish_reduce*>(parent())->my_body, my_body );
            return NULL;
        }       
        template<typename Range,typename Body_, typename Partitioner>
        friend class start_reduce;
    };

    //! Task type used to split the work of parallel_reduce.
    /** @ingroup algorithms */
    template<typename Range, typename Body, typename Partitioner>
    class start_reduce: public task {
        typedef finish_reduce<Body> finish_type;
        Body* my_body;
        Range my_range;
        typename Partitioner::partition_type my_partition;
        reduction_context my_context;
#if defined __TBB_LAZY_BF_EXECUTION || defined TBB_NARY_SYNC
        task* my_continuation;
#endif
        /*override*/ task* execute();
        task* eager_execute();
        task* lazy_execute_df();
        task* lazy_execute_bf();
        task* old_lazy_execute();

        template<typename Body_>
        friend class finish_reduce;
    
        //! Constructor used for root task
        start_reduce( const Range& range, Body* body, Partitioner& partitioner ) :
            my_body(body),
            my_range(range),
            my_partition(partitioner),
            my_context(0)
#if defined __TBB_LAZY_BF_EXECUTION || defined TBB_NARY_SYNC
			, my_continuation(NULL)
#endif
        {
        }
        //! Copy Constructor
        start_reduce(start_reduce& parent_) :
            my_body(parent_.my_body),
            my_range(parent_.my_range),
            my_partition(parent_.my_partition),
            my_context(2)
			//my_context(parent_.my_context)
#if defined __TBB_LAZY_BF_EXECUTION || defined TBB_NARY_SYNC
			, my_continuation(NULL)
#endif
        {
            my_partition.set_affinity(*this);
            parent_.my_context = 1;
            //std::cout << "Reducer copy constructor called!!\n";
        }
        //! Splitting constructor used to generate children.
        /** this becomes left child.  Newly constructed object is right child. */
        start_reduce( start_reduce& parent_, split ) :
            my_body(parent_.my_body),
            my_range(parent_.my_range,split()),
            my_partition(parent_.my_partition,split()),
            my_context(2)
#if defined __TBB_LAZY_BF_EXECUTION || defined TBB_NARY_SYNC
			, my_continuation(NULL)
#endif
        {
            my_partition.set_affinity(*this);
            parent_.my_context = 1;
            //std::cout << "Reducer split constructor called\n";
        }
        //! Update affinity info, if any
        /*override*/ void note_affinity( affinity_id id ) {
            my_partition.note_affinity( id );
        }
        //! Is this task object splittable
        inline bool am_i_splittable() {
        	 return my_range.is_divisible() && !my_partition.should_execute_range(*this);
        }

#ifdef __TBB_LAZY_BF_EXECUTION
        //!spawn or split postponed task. Return true when pushed and false when split
        /*override*/bool spawn_postponed () {
    		bool delay = my_partition.decide_whether_to_delay();
        	if (am_i_splittable() == true) {
        		// split and spawn half
       			my_continuation = new( allocate_continuation() ) finish_type(my_context);
       			recycle_as_child_of(*my_continuation);
       			my_continuation->set_ref_count(2);
       			start_reduce& b = *new( my_continuation->allocate_child() ) start_reduce(*this,split());
       			my_partition.spawn_or_delay(delay,b);
        		return false;
        	} else { // i_am_not_splittable
        		bflws_pushed = true;
                //std::cout << "Pushing postponed reducer task\n";
        		//NOTE: the following code can only be reached if the reducer has
        		// Inner parallelism defined
       			my_continuation = new( allocate_continuation() ) finish_type(my_context);
       			recycle_as_child_of(*my_continuation);
       			my_continuation->set_ref_count(2);
       			start_reduce& b = *new( my_continuation->allocate_child() ) start_reduce(*this);
                //my_context = 1;
                //b.my_context = 2;
       			my_partition.spawn_or_delay(delay,b);
                //*/
                /*
        		if (my_continuation == NULL) {
        			// FIXME shouldn't we split the exiting task into two or is it enough
        			// that we explicitly spawn the "continuation"
        			start_reduce& b = *new( allocate_continuation() )  start_reduce(*this);
        			//std::cout << "Big trouble in little china!!\n\n";
        			// FIXME the next line is probably not needed now that the constructor
        			// initialized bflws_pushed to false
        			//b.bflws_pushed = false;
        			my_partition.spawn_or_delay(delay,b);
        		} else {
        			std::cout << "Big trouble in little china!!\n\n";
        			start_reduce& b = *new( allocate_additional_child_of(*my_continuation) ) start_reduce(*this);
        			// FIXME the next line is probably not needed now that the constructor
        			// initialized bflws_pushed to false
        			//b.bflws_pushed = false;
        			my_partition.spawn_or_delay(delay,b);
        		}//*/
        		return true;
        	}
        }
#endif

public:
        static void run( const Range& range, Body& body, Partitioner& partitioner ) {
            if( !range.empty() ) {
#if !__TBB_TASK_GROUP_CONTEXT || TBB_JOIN_OUTER_TASK_GROUP
                task::spawn_root_and_wait( *new(task::allocate_root()) start_reduce(range,&body,partitioner) );
#else
                // Bound context prevents exceptions from body to affect nesting or sibling algorithms,
                // and allows users to handle exceptions safely by wrapping parallel_for in the try-block.
                task_group_context context;
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_reduce(range,&body,partitioner) );
#endif /* __TBB_TASK_GROUP_CONTEXT && !TBB_JOIN_OUTER_TASK_GROUP */
            }
        }
#if __TBB_TASK_GROUP_CONTEXT
        static void run( const Range& range, Body& body, Partitioner& partitioner, task_group_context& context ) {
            if( !range.empty() ) 
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_reduce(range,&body,partitioner) );
        }
#endif /* __TBB_TASK_GROUP_CONTEXT */
    };

    /** Execute alias */
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::execute() {
#ifdef __TBB_LAZY_DF_EXECUTION
    	return start_reduce<Range,Body,Partitioner>::lazy_execute_df();
#else
#ifdef __TBB_LAZY_BF_EXECUTION
    	return start_reduce<Range,Body,Partitioner>::lazy_execute_bf();
#else  /* __TBB_EAGER_EXECUTION */
    	return start_reduce<Range,Body,Partitioner>::eager_execute();
#endif
#endif
    }

    /* Eager Execute */
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::eager_execute() {
        if( my_context==2 ) {
            finish_type* p = static_cast<finish_type*>(parent() );
            if( !parallel_reduce_load_body(p->my_body) ) {
                my_body = new( p->zombie_space.begin() ) Body(*my_body,split());
                p->has_right_zombie = true;
            }
        }
        if( !am_i_splittable() ) {
            (*my_body)( my_range );
            if( my_context==1 )
                parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
            return my_partition.continue_after_execute_range();
        } else {
            finish_type& c = *new( allocate_continuation()) finish_type(my_context);
            recycle_as_child_of(c);
            c.set_ref_count(2);
            bool delay = my_partition.decide_whether_to_delay();
            start_reduce& b = *new( c.allocate_child() ) start_reduce(*this,split());
            my_partition.spawn_or_delay(delay,b);
            return this;
        }
    }

    /* Lazy Execute - Depth First */
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::lazy_execute_df() {
        if( my_context==2 ) { // if right child
            finish_type* p = static_cast<finish_type*>(parent() );
            if( !parallel_reduce_load_body(p->my_body) ) {
                my_body = new( p->zombie_space.begin() ) Body(*my_body,split());
                p->has_right_zombie = true;
                //std::cout << "Has right zombie!\n";
            } 
        }
        while ( am_i_splittable() ) {
        	// 1. check deque
#ifdef DEQUE_THRESH
    		if ( task_pool_size() < DEQUE_THRESH) {
#else
    		if ( is_task_pool_empty() ) {
#endif
            	// Split Range, push half and return other half task (to be executed)
#ifdef TBB_DEBUG
            	std::cout << "Lazy Splittin' " << my_range.to_string() << "\n";
#endif
            	// split
#ifdef TBB_NARY_SYNC
            	bool delay = my_partition.decide_whether_to_delay();
       			my_continuation = new( allocate_continuation() ) finish_type(my_context);
       			recycle_as_child_of(*my_continuation);
       			my_continuation->set_ref_count(2);
       			start_reduce& b = *new( my_continuation->allocate_child() ) start_reduce(*this,split());
       			my_partition.spawn_or_delay(delay,b);
#else
            	finish_type& c = *new( allocate_continuation()) finish_type(my_context);
                recycle_as_child_of(c);
                c.set_ref_count(2);
                bool delay = my_partition.decide_whether_to_delay();
                start_reduce& b = *new( c.allocate_child() ) start_reduce(*this,split());
                my_partition.spawn_or_delay(delay,b);

            	// returning here because we "recycled as child" this task
                return this;
#endif
    		} else {
        		// a. Execute some iterations
#ifdef __TBB_AUTO_PPT
        		if (my_range.grainsize()==1) {
        			/* This is too slow, trying something else
        			tbb::tick_count::interval_t i = tbb::tick_count::interval_t(1e-2); // 10 ms
        			tbb::tick_count t0, t1;
        			t1 = tbb::tick_count::now();
					do {
						t0=t1;
						Range subrange = my_range.get_some();
						(*my_body)( subrange );
						t1 = tbb::tick_count::now();
						i -= (t1-t0);
						//printf("interval left = %f seconds\n", i.seconds());
						if (i.seconds() > 0) {
							// increment grainsize
							my_range.double_grainsize();
						} else {
							break;
						}
					} while (my_range.is_divisible() );*/
        			tbb::tick_count::interval_t i = tbb::tick_count::interval_t(1e-4); // 10 ms
        			tbb::tick_count t0, t1;
        			t0 = tbb::tick_count::now();
       				Range subrange = my_range.get_some();
       				(*my_body)( subrange );
       				t1 = tbb::tick_count::now();
       				int newppt = ceil ( i.seconds() /(t1-t0).seconds() );
       				newppt = (newppt>0) ? newppt : 1;
       				//printf("DEBUG:: setting grainsize to %d\n", newppt);
       				my_range.set_grainsize(newppt);
        		} else { // grainsize > 0 : just execute some
        			Range subrange = my_range.get_some();
        			(*my_body)( subrange );
        		}
#else // ! _TBB_AUTO_PPT

    			Range subrange = my_range.get_some();
    			(*my_body)( subrange );
#endif

        	} // end execute some iterations
    	} // end while
#ifdef TBB_NARY_SYNC
        if (my_continuation != NULL) {
        	my_continuation = NULL;
        	return this;
        } else {
        	// Execute remaining iterations (if range not divisible or should be
        	//                     executed based on partitioner strategy)
            (*my_body)( my_range );
            if( my_context==1 ) // if left child
                parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
            return my_partition.continue_after_execute_range();
        }
#else
    	// Execute remaining iterations (if range not divisible or should be
    	//                     executed based on partitioner strategy)
        (*my_body)( my_range );
        if( my_context==1 ) // if left child
            parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
        return my_partition.continue_after_execute_range();
#endif
    }

#ifdef __TBB_LAZY_BF_EXECUTION
    /* Lazy Execute - Breadth First */
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::lazy_execute_bf() {
   	    if (bflws_pushed == true) {
   	    	//std::cout << "Ha! this code IS executed!\n";
            //std::cout << "my_context = " << my_context << "\n";
            /*
            if (my_context == 0) 
                std::cout << "my_context = 0\n";
            else if (my_context == 1)
                std::cout << "my_context = 1\n";
            else if (my_context == 2)
                std::cout << "my_context = 2\n";
            else 
                std::cout << "my_context =/= [0,1,2]!!\n";
            //*/
       	    /*if( my_context==1 ) { // if left child  
                finish_type* p = static_cast<finish_type*>(parent() );
                if( parallel_reduce_load_body(p->my_body) == NULL ) 
           	        parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
            }//*/
   	    	return my_partition.continue_after_execute_range();
   	    }
        if( my_context==2 ) { // if right child
            /*
            //std::cout << "It's the right child...\n";
            finish_type* p = static_cast<finish_type*>(parent() );
            if( !parallel_reduce_load_body(p->my_body) ) {
                // This is the right child but the left child has not finished yet..
                // -> allocate space for body.
                my_body = new( p->zombie_space.begin() ) Body(*my_body,split());
                p->has_right_zombie = true;
                //std::cout << "Has right zombie!\n";
            }
            //std::cout << "DONE!!!\n";
            //*/
            
            finish_type* p = static_cast<finish_type*>(parent() );
            my_body = new( p->zombie_space.begin() ) Body(*my_body,split());
            p->has_right_zombie = true;
            //*/
        }
    	this->next_postponed = NULL;
    	// enqueue postponed task
    	task* saved_tail = this->push_postponed_task();

        while ( am_i_splittable() ) {
            // 1. check deque
    #ifdef DEQUE_THRESH
        	if ( task_pool_size() < DEQUE_THRESH) {
    #else
        	if ( is_task_pool_empty() ) {
    #endif
    			// Split Range, push half and return other half task (to be executed)
    			bool success = this->spawn_or_split_oldest_postponed_task();
    			//std::cout << "Lazy-BF Split Reducer\n";
				if (success == false) {
					std::cerr << "ERROR was not able to spawn or split oldest postponed task (including this one)\n";
					break;
				}
        	} else {
           		// Execute some iterations
        		Range subrange = my_range.get_some();
        		(*my_body)( subrange );
            } // end execute some iterations
        } // end while
        // Deque this from postponed_task_list
        bool consumed = this->pop_postponed_task(saved_tail);

    	if (my_continuation!=NULL) {
    		my_continuation = NULL;
       	    if( my_context==1 ) // if left child
                //if( parallel_reduce_load_body(static_cast<finish_type*>(parent())->my_body) == NULL ) 
       	            parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
        	return this;
       	} else {
       		// 'this' was not split => execute the remainder
       		if (bflws_pushed == false) {
       			// Execute remaining iterations (if range not divisible or should be
		       	//                     executed based on partitioner strategy)
       			//std::cout << "executing final iterations\n";
       			(*my_body)( my_range );
       		}
       	    if( my_context==1 ) // if left child
                //if( parallel_reduce_load_body(static_cast<finish_type*>(parent())->my_body) == NULL ) 
       	            parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
            return my_partition.continue_after_execute_range();
        }
    }
#endif

    /* Old Lazy Execute */
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::old_lazy_execute() {
        if( my_context==2 ) { // if right child
        	finish_type* p = static_cast<finish_type*>(parent() );
            if( !parallel_reduce_load_body(p->my_body) ) {
                my_body = new( p->zombie_space.begin() ) Body(*my_body,split());
                p->has_right_zombie = true;
            }
        }
        if( !my_range.is_divisible() || my_partition.should_execute_range(*this) ) {
            (*my_body)( my_range );
            if( my_context==1 ) // if left child
                parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
            return my_partition.continue_after_execute_range();
        } else {
        	// 1. check deque
        	bool isEmpty = is_task_pool_empty();
        	//fprintf(stderr, "isEmpty=%d\n", isEmpty);

        	while ( !isEmpty && my_range.is_divisible() ) {

        		// a. Execute some iterations
#ifdef __TBB_AUTO_PPT
        		if (my_range.grainsize()==1) {
        			/* This is too slow, trying something else
        			tbb::tick_count::interval_t i = tbb::tick_count::interval_t(1e-2); // 10 ms
        			tbb::tick_count t0, t1;
        			t1 = tbb::tick_count::now();
					do {
						t0=t1;
						Range subrange = my_range.get_some();
						(*my_body)( subrange );
						t1 = tbb::tick_count::now();
						i -= (t1-t0);
						//printf("interval left = %f seconds\n", i.seconds());
						if (i.seconds() > 0) {
							// increment grainsize
							my_range.double_grainsize();
						} else {
							break;
						}
					} while (my_range.is_divisible() );*/
        			tbb::tick_count::interval_t i = tbb::tick_count::interval_t(1e-4); // 10 ms
        			tbb::tick_count t0, t1;
        			t0 = tbb::tick_count::now();
       				Range subrange = my_range.get_some();
       				(*my_body)( subrange );
       				t1 = tbb::tick_count::now();
       				int newppt = ceil ( i.seconds() /(t1-t0).seconds() );
       				newppt = (newppt>0) ? newppt : 1;
       				//printf("DEBUG:: setting grainsize to %d\n", newppt);
       				my_range.set_grainsize(newppt);
        		} else { // grainsize > 0 : just execute some
        			Range subrange = my_range.get_some();
        			(*my_body)( subrange );
        		}
#else // ! _TBB_AUTO_PPT

    			Range subrange = my_range.get_some();
    			(*my_body)( subrange );
#endif
        		//delete subrange;

        		/*// b. Execute half
        		Range half(my_range,split());
        		my_body( half );*/

        		// Check Deque Again
        		isEmpty = is_task_pool_empty();

        	} // end while ( !isEmpty )
            if( !my_range.is_divisible() || my_partition.should_execute_range(*this) ) {
            	// Execute iterations (if range not divisible or should be
            	//                     executed based on partitioner strategy)
                (*my_body)( my_range );
                if( my_context==1 ) // if left child
                    parallel_reduce_store_body(static_cast<finish_type*>(parent())->my_body, my_body );
                return my_partition.continue_after_execute_range();
            } else {
            	// Split Range, push half and return other half task (to be executed)
#ifdef TBB_DEBUG
            	std::cout << "Lazy Splittin' " << my_range.to_string() << "\n";
#endif
            	// split
                finish_type& c = *new( allocate_continuation()) finish_type(my_context);
                recycle_as_child_of(c);
                c.set_ref_count(2);
                bool delay = my_partition.decide_whether_to_delay();
                start_reduce& b = *new( c.allocate_child() ) start_reduce(*this,split());
                my_partition.spawn_or_delay(delay,b);
                return this;
            }

        }
    }

    //! Auxiliary class for parallel_reduce; for internal use only.
    /** The adaptor class that implements \ref parallel_reduce_body_req "parallel_reduce Body"
        using given \ref parallel_reduce_lambda_req "anonymous function objects".
     **/
    /** @ingroup algorithms */
    template<typename Range, typename Value, typename RealBody, typename Reduction>
    class lambda_reduce_body {

//FIXME: decide if my_real_body, my_reduction, and identity_element should be copied or referenced
//       (might require some performance measurements)

        const Value&     identity_element;
        const RealBody&  my_real_body;
        const Reduction& my_reduction;
        Value            my_value;
        lambda_reduce_body& operator= ( const lambda_reduce_body& other );
    public:
        lambda_reduce_body( const Value& identity, const RealBody& body, const Reduction& reduction )
            : identity_element(identity)
            , my_real_body(body)
            , my_reduction(reduction)
            , my_value(identity)
        { }
        lambda_reduce_body( const lambda_reduce_body& other )
            : identity_element(other.identity_element)
            , my_real_body(other.my_real_body)
            , my_reduction(other.my_reduction)
            , my_value(other.my_value)
        { }
        lambda_reduce_body( lambda_reduce_body& other, tbb::split )
            : identity_element(other.identity_element)
            , my_real_body(other.my_real_body)
            , my_reduction(other.my_reduction)
            , my_value(other.identity_element)
        { }
        void operator()(Range& range) {
            my_value = my_real_body(range, const_cast<const Value&>(my_value));
        }
        void join( lambda_reduce_body& rhs ) {
            my_value = my_reduction(const_cast<const Value&>(my_value), const_cast<const Value&>(rhs.my_value));
        }
        Value result() const {
            return my_value;
        }
    };

} // namespace internal
//! @endcond

// Requirements on Range concept are documented in blocked_range.h

/** \page parallel_reduce_body_req Requirements on parallel_reduce body
    Class \c Body implementing the concept of parallel_reduce body must define:
    - \code Body::Body( Body&, split ); \endcode        Splitting constructor.
                                                        Must be able to run concurrently with operator() and method \c join
    - \code Body::~Body(); \endcode                     Destructor
    - \code void Body::operator()( Range& r ); \endcode Function call operator applying body to range \c r
                                                        and accumulating the result
    - \code void Body::join( Body& b ); \endcode        Join results. 
                                                        The result in \c b should be merged into the result of \c this
**/

/** \page parallel_reduce_lambda_req Requirements on parallel_reduce anonymous function objects (lambda functions)
    TO BE DOCUMENTED
**/

/** \name parallel_reduce
    See also requirements on \ref range_req "Range" and \ref parallel_reduce_body_req "parallel_reduce Body". **/
//@{

//! Parallel iteration with reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body ) {
    internal::start_reduce<Range,Body, const __TBB_DEFAULT_PARTITIONER>::run( range, body, __TBB_DEFAULT_PARTITIONER() );
}

//! Parallel iteration with reduction and simple_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const simple_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,const simple_partitioner>::run( range, body, partitioner );
}

//! Parallel iteration with reduction and auto_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const auto_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,const auto_partitioner>::run( range, body, partitioner );
}

//! Parallel iteration with reduction and affinity_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, affinity_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,affinity_partitioner>::run( range, body, partitioner );
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const simple_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,const simple_partitioner>::run( range, body, partitioner, context );
}

//! Parallel iteration with reduction, auto_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const auto_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,const auto_partitioner>::run( range, body, partitioner, context );
}

//! Parallel iteration with reduction, affinity_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, affinity_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,affinity_partitioner>::run( range, body, partitioner, context );
}
#endif /* __TBB_TASK_GROUP_CONTEXT */

/** parallel_reduce overloads that work with anonymous function objects
    (see also \ref parallel_reduce_lambda_req "requirements on parallel_reduce anonymous function objects"). **/

//! Parallel iteration with reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const __TBB_DEFAULT_PARTITIONER>
                          ::run(range, body, __TBB_DEFAULT_PARTITIONER() );
    return body.result();
}

//! Parallel iteration with reduction and simple_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const simple_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const simple_partitioner>
                          ::run(range, body, partitioner );
    return body.result();
}

//! Parallel iteration with reduction and auto_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const auto_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const auto_partitioner>
                          ::run( range, body, partitioner );
    return body.result();
}

//! Parallel iteration with reduction and affinity_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       affinity_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,affinity_partitioner>
                                        ::run( range, body, partitioner );
    return body.result();
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const simple_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const simple_partitioner>
                          ::run( range, body, partitioner, context );
    return body.result();
}

//! Parallel iteration with reduction, auto_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const auto_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const auto_partitioner>
                          ::run( range, body, partitioner, context );
    return body.result();
}

//! Parallel iteration with reduction, affinity_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       affinity_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,affinity_partitioner>
                                        ::run( range, body, partitioner, context );
    return body.result();
}
#endif /* __TBB_TASK_GROUP_CONTEXT */
//@}

} // namespace tbb

#endif /* __TBB_parallel_reduce_H */

