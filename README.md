# memefs / WinFsp-MemFs-Extended
*MemExtendedFs (memefs)* is a *fork* of [WinFsp's memfs](https://github.com/winfsp/winfsp/tree/master/tst/memfs) with some improvements that make it suitable for a constantly running **RAM disk** that only allocates the **memory actually needed** to store the files.

## Improvements
- **Much better performance when writing unpreallocated files** because of vectors of sectors instead of using a heap for every file
- Better storage limit indication
- *Total memory* limit instead of *file node \* individual size* limit
- Ability to set the volume label via the CLI (-l)
- More efficient memory management by resetting the heap when all files are fully deleted

### Benchmarks
![Unpreallocated File Write Times](benchmarks/unprealloctimes.avif) \
![File I/O Speeds](benchmarks/filespeeds.avif) \
As you can see, the unpreallocated file write times make the original memfs unusable, especially for web downloads. But if you need maximum sequential speed and are able to preallocate the file with NtCreateFile and its AllocationSize, then you should use the original memfs.
![Fsbench](benchmarks/fsbench.avif)

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
    -M MaxDelay         [maximum slow IO delay in millis]
    -P PercentDelay     [percent of slow IO to make pending]
    -R RarefyDelay      [adjust the rarity of pending slow IO]
    -F FileSystemName
    -S RootSddl         [file rights: FA, etc; NO generic rights: GA, etc.]
    -u \Server\Share    [UNC prefix (single backslash)]
    -m MountPoint       [X:|* (required if no UNC prefix)]
    -l VolumeLabel      [optional volume label name]
```

## Recommended Installation
1. Install [WinFsp](https://winfsp.dev/rel/)
1. Download a [memefs release](https://github.com/Ceiridge/WinFsp-MemFs-Extended/releases)
1. Start a test memefs instance to see if it works (see example command above)
1. Stop the test instance
1. Copy `memefs-x64.exe` (and `memefs-x86.exe`) to your WinFsp installation directory (`C:\Program Files (x86)\WinFsp\bin\`)
1. Create a WinFsp service by downloading and executing the [InstallMemefsService.reg](InstallMemefsService.reg) file (64-bit only supported. For the 32-bit version, it has to be manually installed) \
	If you have installed WinFsp somewhere else, make sure to edit the executable path.
1. Test the service by running this command in your WinFsp installation dir.: `launchctl-x64.exe start memefs64 test "" X:` (admin rights may be required)
1. Verify that memefs is running with the drive letter X
1. Stop the service by running the following: `launchctl-x64.exe stop memefs64 test`
1. Create a scheduled task to automatically launch memefs when logging in
	1. Open the Task Scheduler
	1. Create a new scheduled task
	1. Execute with highest privileges, but with your user account
	1. Add a trigger that triggers on logon (with your user account)
	1. Add a new action with the following settings:
	```
	Program/Script: "C:\Program Files (x86)\WinFsp\bin\launchctl-x64.exe"
	Arguments: start memefs64 ramdisk "" R:
	```
	1. Optionally disable *Only run if the computer is connected to a power supply* in the task settings
	1. Save, run and test
