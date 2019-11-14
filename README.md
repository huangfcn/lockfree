1, lock free memory management routines based on fixed size memory blocks

   fixed size memory blocks routines (startup, cleanup, alloc, free)       
      create a free list with fixed size memory block                      
      allocation of a memory block becomes getting a block from free list  
      free       of a memory block becomes putting a block into free list  
                                                                           
   general memory malloc/free through fixed size memory blocks             
      maintain fixed size memory blocks with different size                
      allocation becomes getting a block from corresponding free list      
      free       becomes putting a block into corresponding free list      
                                                                           
         1 bytes -   240   bytes, maintained in blocks aligned to  16 bytes
       241 bytes -  3,840  bytes, maintained in blocks aligned to 256 bytes
     3,841 bytes -  61,440 bytes, maintained in blocks aligned to  4k bytes
    61,441 bytes - 524,288 bytes, maintained in blocks aligned to 64k bytes
       otherwise                , call system memory management calls

2, lock free stack & lock free queue (MSQueue)
3, single producer / single consumer ring buffer queue
4, multiple producers / multiple consumers ring buffer queue  