Click开发笔记

* kill router
    * 对于处于调度队列中的Task，中断所有线程，遍历调度队列查找是否有指定Router的Task将其删除，额外遍历需要代价，另一方面并不是所有的调度队列中都有router的task，所以中断这些线程是没有必要的
    对要被kill的Task进行标记，当Task下次被调度时将其从队列中移除，这个过程同样要遍历队列，但是这是调度机制的一个环节，无论我们删与不删都会发生，我们只是利用了这个过程没有进行额外的遍历，并且对于没有相应Task的只需添加一个标志位判断而已，是非常小的代价
    * 如何处理没有在调度队列中的Task，不能直接删除，因为可能某个线程正在将其
    * Task是否会被调度与网络流量有关，可能没有需要Task处理的数据包或者需要较长的时间才会出现相应的数据包，导致有些Task永远不会被调度
    * Task正在运行