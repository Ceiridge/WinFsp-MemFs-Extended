# memefs / WinFsp-MemFs-Extended
*MemExtendedFs (memefs)* is a *fork* of [WinFsp's memfs](https://github.com/winfsp/winfsp/tree/master/tst/memfs) with some improvements that make it suitable for a constantly running **RAM disk** that only allocates the **memory actually needed** to store the files.

## Improvements
- Much better performance when writing unpreallocated files because of vectors of sectors instead of using a heap for every file
- Better storage limit indication
- *Total memory* limit instead of *file node \* individual size* limit
- Ability to set the volume label via the CLI (-l)

## CLI
```
usage: memefs OPTIONS

options:
    -d DebugFlags       [-1: enable all debug logs]
    -D DebugLogFile     [file path; use - for stderr]
    -i                  [case insensitive file system]
    -f                  [flush and purge cache on cleanup]
    -t FileInfoTimeout  [millis]
    -s MaxFsSize        [bytes of maximum total memory size]
    -M MaxDelay         [maximum slow IO delay in millis]
    -P PercentDelay     [percent of slow IO to make pending]
    -R RarefyDelay      [adjust the rarity of pending slow IO]
    -F FileSystemName
    -S RootSddl         [file rights: FA, etc; NO generic rights: GA, etc.]
    -u \Server\Share    [UNC prefix (single backslash)]
    -m MountPoint       [X:|* (required if no UNC prefix)]
    -l VolumeLabel      [optional volume label name]
```
