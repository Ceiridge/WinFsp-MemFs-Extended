# tmpfs for Windows / memefs
*WinFsp-MemFs-Extended/MemExtendedFs (memefs)* is a *fork* of [WinFsp's memfs](https://github.com/winfsp/winfsp/tree/master/tst/memfs) with some improvements that make it suitable for a constantly running **RAM disk** that only allocates the **memory actually needed** to store the files.

## Improvements
- **Completely rewritten in modern C++**, guaranteeing memory and thread-safety and easy maintainability
- **Much better performance when writing unpreallocated files** because of vectors of sectors instead of using a heap for every file
- Better storage limit indication
- *Total memory* limit instead of *file node \* individual size* limit
- Ability to set the volume label via the CLI (-l)

### Benchmarks
![Unpreallocated File Write Times](benchmarks/unprealloctimes.avif) \
![File I/O Speeds](benchmarks/filespeeds.avif) \
As you can see, the unpreallocated file write times make the original memfs unusable, especially for web downloads. But if you need maximum sequential speed and are able to preallocate the file with NtCreateFile and its AllocationSize, then you should use the original memfs.
![Fsbench](benchmarks/fsbench.avif)
**The fsbench results above are outdated** and memefs (this repository) is faster in most cases, sometimes significantly.

## CLI
```
usage: memefs OPTIONS
simple example: memefs -i -F NTFS -u "" -m R:

options:
    -d DebugFlags       [-1: enable all debug logs]
    -D DebugLogFile     [file path; use - for stderr]
    -i                  [case insensitive file system]
    -f                  [flush and purge cache on cleanup]
    -s MaxFsSize        [bytes of maximum total memory size]
    -F FileSystemName
    -S RootSddl         [file rights: FA, etc; NO generic rights: GA, etc.]
    -u \Server\Share  [UNC prefix (single backslash)]
    -m MountPoint       [X:|* (required if no UNC prefix)]
    -l VolumeLabel      [optional volume label name]
```
