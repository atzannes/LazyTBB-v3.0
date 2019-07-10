Implementation Strategy
-----------------------
I modified parallel_for.h, parallel_reduce.h, and task.h/task.cpp. A compile time flag is used to compile the requested version of the scheduler:
(1) Breadth-First Lazy: Lazy with private deque to split or push oldest postponed task
(2) Depth-First Lazy: Lazy without private deque. Splits or pushes current (innermost) task when deque is below threshold
(3) Eager(default): No flag needed in this case

TBB FLAGS
---------
DEQUE_THRESH
* If it is defined it should be the number of elements in a deque below which more tasks are pushed onto the deque.
* If it is not defined it defaults to 1, meaning that a push onto a deque occurs only when the deque is empty.

__TBB_LAZY_BF_EXECUTION
* Executes the lazy scheduler and splits or pushes the oldest postponed task (keeps track of them using private deque).

__TBB_LAZY_DF_EXECUTION
* Executes the lazy scheduler and splits or pushes the current (most deeply nested) task.

TBB_NARY_SYNC
I was experimenting with an alternative synchronization approach for parallel loops and reducers. I didn't find any advantage or disadvantage using it. So I abandoned it. Kept just for reference.

NOTES:
Lazy Scheduling is intended to be used with Simple Partitioner, as AutoPartitioner would check the deque too infrequently.
