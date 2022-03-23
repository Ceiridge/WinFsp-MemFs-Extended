# memefs / WinFsp-MemFs-Extended
*MemExtendedFs (memefs)* is a *fork* of [WinFsp's memfs](https://github.com/winfsp/winfsp/tree/master/tst/memfs) with some improvements that make it suitable for a constantly running **RAM disk** that only allocates the **memory actually needed** to store the files.

## Improvements
- Much better performance when writing unpreallocated files because of vectors of sectors instead of using a heap for every file
- Better storage limit indication
- *Total memory* limit instead of *file node \* individual size* limit
