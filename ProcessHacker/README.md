<p align="center">
    <a href="https://systeminformer.com">
        <img src="https://github.com/winsiderss/systeminformer/raw/master/SystemInformer/resources/systeminformer-128x128.png"/>
    </a>
    <h1 align="center">System Informer</h1>
    <h5 align="center">A free, powerful, multi-purpose tool that helps you monitor system resources, debug software and detect malware.</h5>
    <h6 align="center">Brought to you by Winsider Seminars & Solutions, Inc.</h6>
</p>
<p align="center">
    <a href="https://github.com/winsiderss/systeminformer/actions/workflows/msbuild.yml">
        <img src="https://img.shields.io/github/actions/workflow/status/winsiderss/systeminformer/msbuild.yml?branch=master&style=for-the-badge"/>
    </a>
    <a href="https://github.com/winsiderss/systeminformer/graphs/contributors">
        <img src="https://img.shields.io/github/contributors/winsiderss/systeminformer.svg?style=for-the-badge&color=blue"/>
    </a>
    <a href="https://opensource.org/licenses/MIT">
        <img src="https://img.shields.io/badge/license-MIT-blue.svg?style=for-the-badge&color=blue"/>
    </a>
</p>
<p align="center">
    <a href="https://systeminformer.com/downloads">
        <img src="https://img.shields.io/github/downloads/winsiderss/si-builds/total.svg?style=for-the-badge&color=blue"/>
    </a>
    <a href="https://somsubhra.github.io/github-release-stats/?username=winsiderss&repository=systeminformer">
        <img src="https://img.shields.io/github/downloads/winsiderss/systeminformer/total.svg?style=for-the-badge&color=blue&label="/>
    </a>
    <a href="https://sourceforge.net/projects/processhacker/files/stats/timeline?period=monthly">
        <img src="https://img.shields.io/sourceforge/dt/processhacker.svg?style=for-the-badge&color=blue&label="/>
    </a>
</p>
<p align="center">
    <a href="https://discord.com/invite/k2MQd2DzC2">
        <img src="https://img.shields.io/badge/Discord-grey?style=for-the-badge&logoColor=white&logo=discord"/>
    </a>
    <a href="https://x.com/systeminformer">
        <img src="https://img.shields.io/badge/Twitter-grey?style=for-the-badge&logoColor=white&logo=x"/>
    </a>
    <a href="https://systeminformer.com">
        <img src="https://img.shields.io/badge/Website-grey?style=for-the-badge&logo=data:image/svg%2bxml;base64,PHN2ZyB2aWV3Qm94PSIwIDAgMTIgMTIiIGZpbGw9Im5vbmUiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PGNpcmNsZSBjeD0iNiIgY3k9IjYiIHI9IjUuNSIgc3Ryb2tlPSJ3aGl0ZSIvPjxlbGxpcHNlIGN4PSI2IiBjeT0iNiIgcng9IjUuNSIgcnk9IjIiIHRyYW5zZm9ybT0icm90YXRlKDkwIDYgNikiIHN0cm9rZT0id2hpdGUiLz48cGF0aCBkPSJNMSA2SDExIiBzdHJva2U9IndoaXRlIiBzdHJva2UtbGluZWNhcD0icm91bmQiLz48L3N2Zz4="/>
    </a>
</p>

## System requirements

Windows 10 or higher, 32-bit or 64-bit.

## Features

* A detailed overview of system activity with highlighting.
* Graphs and statistics allow you quickly to track down resource hogs and
  runaway processes.
* Can't edit or delete a file? Discover which processes are using that file.
* See what programs have active network connections, and close them if
  necessary.
* Get real-time information on disk access.
* View detailed stack traces with kernel-mode, WOW64 and .NET support.
* Go beyond services.msc: create, edit and control services.
* Small, portable and no installation required.
* 100% [Free Software](https://www.gnu.org/philosophy/free-sw.en.html)
  ([MIT](https://opensource.org/licenses/MIT))

## Building the project

Requires Visual Studio (2022 or later).

After cloning the repo run `build_init.cmd` located in the `build` directory,
this doesn't not run again unless there are updates to the tools or third party
libraries.

Execute `build_release.cmd` located in the `build` directory to compile the
project or load the `SystemInformer.sln` and `Plugins.sln` solutions if you
prefer building the project using Visual Studio.

You can download the free
[Visual Studio Community Edition](https://www.visualstudio.com/vs/community/)
to build the System Informer source code.

See the [build readme](./build/README.md) for more information or if you're
having trouble building.

## Enhancements/Bugs

Please use the
[GitHub issue tracker](https://github.com/winsiderss/systeminformer/issues) for
reporting problems or suggesting new features.

## Settings

If you are running System Informer from a USB drive, you may want to
save System Informer's settings there as well. To do this, create a
blank file named "SystemInformer.exe.settings.xml" in the same
directory as SystemInformer.exe. You can do this using Windows Explorer:

1. Make sure "Hide extensions for known file types" is unticked in
   Tools > Folder options > View.
2. Right-click in the folder and choose New > Text Document.
3. Rename the file to SystemInformer.exe.settings.xml (delete the ".txt"
   extension).
