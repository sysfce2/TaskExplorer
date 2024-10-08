#pragma once

#include <sistatus.h>

#ifdef _KERNEL_MODE
#include <ntifs.h>
#include <ntintsafe.h>
#include <minwindef.h>
#include <ntstrsafe.h>
#include <fltKernel.h>
#else
#pragma warning(push)
#pragma warning(disable : 4201)
#pragma warning(disable : 4214)
#pragma warning(disable : 4324)
#pragma warning(disable : 4115)
#include <phnt_windows.h>
#include <phnt.h>
#pragma warning(pop)
#endif
