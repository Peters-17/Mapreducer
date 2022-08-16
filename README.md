### Project Introduction
In 2004, engineers at Google introduced a new paradigm for large-scale parallel data processing known as MapReduce (see the original paper here (Links to an external site.), and make sure to look in the citations at the end). One key aspect of MapReduce is that it makes programming such tasks on large-scale clusters easy for developers; instead of worrying about how to manage parallelism, handle machine crashes, and many other complexities common within clusters of machines, the developer can instead just focus on writing little bits of code (described below) and the infrastructure handles the rest.

In this project, you'll be building a simplified version of MapReduce for just a single machine. While somewhat easier to build MapReduce for a single machine, there are still numerous challenges, mostly in building the correct concurrency support. Thus, you'll have to think a bit about how to build the MapReduce implementation, and then build it to work efficiently and correctly. For some additional practice with concurrency, you will modify an existing HashMap implementation to be multi-threaded by adding reader-writer locks.

There are three specific objectives to this assignment:

1.To learn about the general nature of the MapReduce paradigm.

2.To implement a correct and efficient MapReduce framework using threads and related functions.

3.To gain more experience writing concurrent code.

### Here are a few things to consider in your implementation:

Thread Management. This part is fairly straightforward. You should create num_mappers mapping threads, and assign a file to each Map() invocation in some manner you think is best (e.g., Round Robin, Shortest-File-First, etc.). Which way might lead to best performance? You should also create num_reducers reducer threads at some point, to work on the map'd output.

Partitioning and Sorting. Your central data structure should be concurrent, allowing mappers to each put values into different partitions correctly and efficiently. Once the mappers have completed, a sorting phase should order the key/value-lists. Then, finally, each reducer thread should start calling the user-defined Reduce() function on the keys in sorted order per partition. You should think about what type of locking is needed throughout this process for correctness. The number of partitions should correspond to the number of reducers.

Memory Management. One last concern is memory management. The MR_Emit() function is passed a key/value pair; it is the responsibility of the MR library to make copies of each of these. Then, when the entire mapping and reduction is complete, it is the responsibility of the MR library to free everything.
