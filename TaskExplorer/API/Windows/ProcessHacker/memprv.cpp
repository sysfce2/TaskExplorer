/*
 * System Informer -
 *   qt port of memory provider
 *
 * Copyright (C) 2010-2015 wj32
 * Copyright (C) 2017-2018 dmex
 * Copyright (C) 2019 David Xanatos
 *
 * This file is part of Task Explorer and contains System Informer code.
 *
 */

#include "stdafx.h"
#include "../ProcessHacker.h"
#include "../WinMemory.h"
#include "memprv.h"
#include "heapstruct.h"
#include "../../../MiscHelpers/Common/Settings.h"

#define MAX_HEAPS 1000
#define WS_REQUEST_COUNT (PAGE_SIZE / sizeof(MEMORY_WORKING_SET_EX_INFORMATION))

QMap<quint64, CMemoryPtr>::iterator PhLookupMemoryItemList(
    QMap<quint64, CMemoryPtr>& MemoryMap,
    _In_ PVOID Address
    )
{
    // Do an approximate search on the set to locate the memory item with the largest
    // base address that is still smaller than the given address.
	QMap<quint64, CMemoryPtr>::iterator I = MemoryMap.upperBound((quint64)Address);
    
    if (I != MemoryMap.end())
    {
		QSharedPointer<CWinMemory> memoryItem = I.value().staticCast<CWinMemory>();

        if ((ULONG_PTR)Address < (ULONG_PTR)memoryItem->m_BaseAddress + memoryItem->m_RegionSize)
            return I;
    }

    return MemoryMap.end();
}

QSharedPointer<CWinMemory> PhpSetMemoryRegionType(
    QMap<quint64, CMemoryPtr>& MemoryMap,
    _In_ PVOID Address,
    _In_ BOOLEAN GoToAllocationBase,
    _In_ int RegionType
    )
{
    QMap<quint64, CMemoryPtr>::iterator I = PhLookupMemoryItemList(MemoryMap, Address);

    if (I == MemoryMap.end())
        return NULL;

	QSharedPointer<CWinMemory> memoryItem = I.value().staticCast<CWinMemory>();

    if (GoToAllocationBase && memoryItem->m_AllocationBaseItem)
        memoryItem = memoryItem->m_AllocationBaseItem.staticCast<CWinMemory>();

    if (memoryItem->m_RegionType != UnknownRegion)
        return NULL;

    memoryItem->m_RegionType = RegionType;

    return memoryItem;
}

VOID PhpUpdateHeapRegions(
    QMap<quint64, CMemoryPtr>& MemoryMap,
    _In_ HANDLE ProcessId,
    _In_ HANDLE ProcessHandle
)
{
    NTSTATUS status;
    HANDLE processHandle = NULL;
    HANDLE clientProcessId = ProcessId;
    PROCESS_REFLECTION_INFORMATION reflectionInfo = { 0 };
    PRTL_DEBUG_INFORMATION debugBuffer = NULL;
    PPH_PROCESS_DEBUG_HEAP_INFORMATION heapDebugInfo = NULL;
    HANDLE powerRequestHandle = NULL;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_LIMITED_INFORMATION |
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_DUP_HANDLE,
        ProcessId
    );

    if (NT_SUCCESS(status))
    {
        if (WindowsVersion >= WINDOWS_10)
            PhCreateExecutionRequiredRequest(processHandle, &powerRequestHandle);

        if (theConf->GetBool("Options/EnableHeapReflection"))
        {
            status = PhCreateProcessReflection(
                &reflectionInfo,
                processHandle
            );

            if (NT_SUCCESS(status))
            {
                clientProcessId = reflectionInfo.ReflectionClientId.UniqueProcess;
            }
        }
    }

    for (ULONG i = 0x10000; ; i *= 2) // based on PhQueryProcessHeapInformation (ge0rdi)
    {
        if (!(debugBuffer = RtlCreateQueryDebugBuffer(i, FALSE)))
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        status = RtlQueryProcessDebugInformation(
            clientProcessId,
            RTL_QUERY_PROCESS_HEAP_SUMMARY | RTL_QUERY_PROCESS_HEAP_SEGMENTS | RTL_QUERY_PROCESS_NONINVASIVE,
            debugBuffer
        );

        if (!NT_SUCCESS(status))
        {
            RtlDestroyQueryDebugBuffer(debugBuffer);
            debugBuffer = NULL;
        }

        if (NT_SUCCESS(status) || status != STATUS_NO_MEMORY)
            break;

        if (2 * i <= i)
            break;
    }

    PhFreeProcessReflection(&reflectionInfo);

    if (processHandle)
        NtClose(processHandle);

    if (powerRequestHandle)
        PhDestroyExecutionRequiredRequest(powerRequestHandle);

    if (!NT_SUCCESS(status))
        return;

    if (!debugBuffer->Heaps)
    {
        RtlDestroyQueryDebugBuffer(debugBuffer);
        return;
    }

    if (WindowsVersion > WINDOWS_11)
    {
        heapDebugInfo = (PPH_PROCESS_DEBUG_HEAP_INFORMATION)PhAllocateZero(sizeof(PH_PROCESS_DEBUG_HEAP_INFORMATION) + ((PRTL_PROCESS_HEAPS_V2)debugBuffer->Heaps)->NumberOfHeaps * sizeof(PH_PROCESS_DEBUG_HEAP_ENTRY));
        heapDebugInfo->NumberOfHeaps = ((PRTL_PROCESS_HEAPS_V2)debugBuffer->Heaps)->NumberOfHeaps;
    }
    else
    {
        heapDebugInfo = (PPH_PROCESS_DEBUG_HEAP_INFORMATION)PhAllocateZero(sizeof(PH_PROCESS_DEBUG_HEAP_INFORMATION) + ((PRTL_PROCESS_HEAPS_V1)debugBuffer->Heaps)->NumberOfHeaps * sizeof(PH_PROCESS_DEBUG_HEAP_ENTRY));
        heapDebugInfo->NumberOfHeaps = ((PRTL_PROCESS_HEAPS_V1)debugBuffer->Heaps)->NumberOfHeaps;
    }

    for (ULONG i = 0; i < heapDebugInfo->NumberOfHeaps; i++)
    {
        RTL_HEAP_INFORMATION_V2 heapInfo = { 0 };
        QSharedPointer<CWinMemory> heapMemoryItem;

        if (WindowsVersion > WINDOWS_11)
        {
            heapInfo = ((PRTL_PROCESS_HEAPS_V2)debugBuffer->Heaps)->Heaps[i];
        }
        else
        {
            RTL_HEAP_INFORMATION_V1 heapInfoV1 = ((PRTL_PROCESS_HEAPS_V1)debugBuffer->Heaps)->Heaps[i];
            heapInfo.NumberOfEntries = heapInfoV1.NumberOfEntries;
            heapInfo.Entries = heapInfoV1.Entries;
            heapInfo.BytesCommitted = heapInfoV1.BytesCommitted;
            heapInfo.Flags = heapInfoV1.Flags;
            heapInfo.BaseAddress = heapInfoV1.BaseAddress;
        }

        if (!heapInfo.BaseAddress)
            continue;

        if (heapMemoryItem = PhpSetMemoryRegionType(MemoryMap, heapInfo.BaseAddress, TRUE, HeapRegion))
        {
            heapMemoryItem->u.Heap.Index = i;

            for (ULONG j = 0; j < heapInfo.NumberOfEntries; j++)
            {
                PRTL_HEAP_ENTRY heapEntry = &heapInfo.Entries[j];

                if (heapEntry->Flags & RTL_HEAP_SEGMENT)
                {
                    PVOID blockAddress = heapEntry->u.s2.FirstBlock;

                    QMap<quint64, CMemoryPtr>::iterator I = PhLookupMemoryItemList(MemoryMap, blockAddress);

                    if (I != MemoryMap.end() && ((CWinMemory*)I.value().data())->m_BaseAddress == (quint64)blockAddress && ((CWinMemory*)I.value().data())->m_RegionType == UnknownRegion)
                    {
                        ((CWinMemory*)I.value().data())->m_RegionType = HeapSegmentRegion;
                        ((CWinMemory*)I.value().data())->u_HeapSegment_HeapItem = heapMemoryItem;
                    }
                }
            }
        }
    }

    RtlDestroyQueryDebugBuffer(debugBuffer);
}

NTSTATUS PhpUpdateMemoryRegionTypes(
    QMap<quint64, CMemoryPtr>& MemoryMap,
	_In_ HANDLE ProcessId,
    _In_ HANDLE ProcessHandle
    )
{
    NTSTATUS status;
    PVOID processes;
    PSYSTEM_PROCESS_INFORMATION process;
    ULONG i;
#ifdef _WIN64
    BOOLEAN isWow64 = FALSE;
#endif
    QSharedPointer<CWinMemory> memoryItem;
    PLIST_ENTRY listEntry;

    if (!NT_SUCCESS(status = PhEnumProcessesEx(&processes, SystemExtendedProcessInformation)))
        return status;

    process = PhFindProcessInformation(processes, ProcessId);

    if (!process)
    {
        PhFree(processes);
        return STATUS_NOT_FOUND;
    }

    // USER_SHARED_DATA
    PhpSetMemoryRegionType(MemoryMap, USER_SHARED_DATA, TRUE, UserSharedDataRegion);

    // HYPERVISOR_SHARED_DATA
    if (WindowsVersion >= WINDOWS_10_RS4)
    {
        static PSYSTEM_HYPERVISOR_USER_SHARED_DATA hypervisorSharedUserData = NULL;
        static PH_INITONCE hypervisorSharedUserInitOnce = PH_INITONCE_INIT;

        if (PhBeginInitOnce(&hypervisorSharedUserInitOnce))
        {
            PhGetSystemHypervisorSharedPageInformation(&hypervisorSharedUserData);
            PhEndInitOnce(&hypervisorSharedUserInitOnce);
        }

        if (hypervisorSharedUserData)
        {
            PhpSetMemoryRegionType(MemoryMap, hypervisorSharedUserData, TRUE, HypervisorSharedDataRegion);
        }
    }

    // PEB, heap
    {
        ULONG numberOfHeaps;
        PVOID processPeb;
        PVOID processHeapsPtr;
        PVOID *processHeaps;
        PVOID apiSetMap;
        PVOID readOnlySharedMemory;
        PVOID codePageData;
        PVOID gdiSharedHandleTable;
        PVOID shimData;
        PVOID activationContextData;
        PVOID defaultActivationContextData;
        PVOID werRegistrationData;
        PVOID siloSharedData;
        PVOID telemetryCoverageData;
#ifdef _WIN64
        PVOID processPeb32;
        ULONG processHeapsPtr32;
        ULONG *processHeaps32;
        ULONG apiSetMap32;
        ULONG readOnlySharedMemory32;
        ULONG codePageData32;
        ULONG gdiSharedHandleTable32;
        ULONG shimData32;
        ULONG activationContextData32;
        ULONG defaultActivationContextData32;
        ULONG werRegistrationData32;
        ULONG siloSharedData32;
        ULONG telemetryCoverageData32;
#endif

        if (theConf->GetBool("Options/EnableHeapMemoryTagging", true))
        {
            PhpUpdateHeapRegions(MemoryMap, ProcessId, ProcessHandle);
        }

        if (NT_SUCCESS(PhGetProcessPeb(ProcessHandle, &processPeb)))
        {
            // HACK: Windows 10 RS2 and above 'added TEB/PEB sub-VAD segments' and we need to tag individual sections.
            PhpSetMemoryRegionType(MemoryMap, processPeb, WindowsVersion < WINDOWS_10_RS2 ? TRUE : FALSE, PebRegion);

            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, UFIELD_OFFSET(PEB, NumberOfHeaps)),
                &numberOfHeaps,
                sizeof(ULONG),
                NULL
            )) && numberOfHeaps > 0 && numberOfHeaps < MAX_HEAPS)
            {
                processHeaps = (PVOID *)PhAllocate(numberOfHeaps * sizeof(PVOID));

                if (
                    NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, PTR_ADD_OFFSET(processPeb, UFIELD_OFFSET(PEB, ProcessHeaps)), &processHeapsPtr, sizeof(PVOID), NULL)) &&
                    NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, processHeapsPtr, processHeaps, numberOfHeaps * sizeof(PVOID), NULL))
                    )
                {
                    for (i = 0; i < numberOfHeaps; i++)
                    {
                        if (memoryItem = PhpSetMemoryRegionType(MemoryMap, processHeaps[i], TRUE, HeapRegion))
                        {
                            PH_ANY_HEAP buffer;

                            memoryItem->u.Heap.Index = i;

                            // Try to read heap class from the header
                            if (NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, processHeaps[i], &buffer, sizeof(buffer), NULL)))
                            {
                                if (WindowsVersion >= WINDOWS_8)
                                {
                                    if (buffer.Heap.Signature == HEAP_SIGNATURE)
                                    {
                                        memoryItem->u.Heap.ClassValid = TRUE;
                                        memoryItem->u.Heap.Class = buffer.Heap.Flags & HEAP_CLASS_MASK;
                                    }
                                    else if (buffer.Heap32.Signature == HEAP_SIGNATURE)
                                    {
                                        memoryItem->u.Heap.ClassValid = TRUE;
                                        memoryItem->u.Heap.Class = buffer.Heap32.Flags & HEAP_CLASS_MASK;
                                    }
                                    else if (WindowsVersion >= WINDOWS_8_1)
                                    {
                                        // Windows 8.1 and above can use segment heaps
                                        if (buffer.SegmentHeap.Signature == SEGMENT_HEAP_SIGNATURE)
                                        {
                                            memoryItem->u.Heap.ClassValid = TRUE;
                                            memoryItem->u.Heap.Class = buffer.SegmentHeap.GlobalFlags & HEAP_CLASS_MASK;
                                        }
                                        else if (buffer.SegmentHeap32.Signature == SEGMENT_HEAP_SIGNATURE)
                                        {
                                            memoryItem->u.Heap.ClassValid = TRUE;
                                            memoryItem->u.Heap.Class = buffer.SegmentHeap32.GlobalFlags & HEAP_CLASS_MASK;
                                        }
                                    }
                                }
                                else
                                {
                                    if (buffer.HeapOld.Signature == HEAP_SIGNATURE)
                                    {
                                        memoryItem->u.Heap.ClassValid = TRUE;
                                        memoryItem->u.Heap.Class = buffer.HeapOld.Flags & HEAP_CLASS_MASK;
                                    }
                                    else if (buffer.HeapOld32.Signature == HEAP_SIGNATURE)
                                    {
                                        memoryItem->u.Heap.ClassValid = TRUE;
                                        memoryItem->u.Heap.Class = buffer.HeapOld32.Flags & HEAP_CLASS_MASK;
                                    }
                                }
                            }
                        }
                    }
                }

                PhFree(processHeaps);
            }

            // ApiSet schema map
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, ApiSetMap)),
                &apiSetMap,
                sizeof(PVOID),
                NULL
            )) && apiSetMap)
            {
                PhpSetMemoryRegionType(MemoryMap, apiSetMap, TRUE, ApiSetMapRegion);
            }

            // CSR shared memory
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, ReadOnlySharedMemoryBase)),
                &readOnlySharedMemory,
                sizeof(PVOID),
                NULL
            )) && readOnlySharedMemory)
            {
                PhpSetMemoryRegionType(MemoryMap, readOnlySharedMemory, TRUE, ReadOnlySharedMemoryRegion);
            }

            // CodePage data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, AnsiCodePageData)),
                &codePageData,
                sizeof(PVOID),
                NULL
            )) && codePageData)
            {
                PhpSetMemoryRegionType(MemoryMap, codePageData, TRUE, CodePageDataRegion);
            }

            // GDI shared handle table
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, GdiSharedHandleTable)),
                &gdiSharedHandleTable,
                sizeof(PVOID),
                NULL
            )) && gdiSharedHandleTable)
            {
                PhpSetMemoryRegionType(MemoryMap, gdiSharedHandleTable, TRUE, GdiSharedHandleTableRegion);
            }

            // Shim data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, pShimData)),
                &shimData,
                sizeof(PVOID),
                NULL
            )) && shimData)
            {
                PhpSetMemoryRegionType(MemoryMap, shimData, TRUE, ShimDataRegion);
            }

            // Process activation context data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, ActivationContextData)),
                &activationContextData,
                sizeof(PVOID),
                NULL
            )) && activationContextData)
            {
                if (memoryItem = PhpSetMemoryRegionType(MemoryMap, activationContextData, TRUE, ActivationContextDataRegion))
                {
                    memoryItem->u.ActivationContextData.Type = ProcessActivationContext;
                }
            }

            // System-default activation context data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, SystemDefaultActivationContextData)),
                &defaultActivationContextData,
                sizeof(PVOID),
                NULL
            )) && defaultActivationContextData)
            {
                if (memoryItem = PhpSetMemoryRegionType(MemoryMap, defaultActivationContextData, TRUE, ActivationContextDataRegion))
                {
                    memoryItem->u.ActivationContextData.Type = SystemActivationContext;
                }
            }

            // WER registration data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, WerRegistrationData)),
                &werRegistrationData,
                sizeof(PVOID),
                NULL
            )) && werRegistrationData)
            {
                PhpSetMemoryRegionType(MemoryMap, werRegistrationData, TRUE, WerRegistrationDataRegion);
            }

            // Silo shared data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, SharedData)),
                &siloSharedData,
                sizeof(PVOID),
                NULL
            )) && siloSharedData)
            {
                PhpSetMemoryRegionType(MemoryMap, siloSharedData, TRUE, SiloSharedDataRegion);
            }

            // Telemetry coverage map
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb, FIELD_OFFSET(PEB, TelemetryCoverageHeader)),
                &telemetryCoverageData,
                sizeof(PVOID),
                NULL
            )) && telemetryCoverageData)
            {
                PhpSetMemoryRegionType(MemoryMap, telemetryCoverageData, TRUE, TelemetryCoverageRegion);
            }
        }
#ifdef _WIN64

        if (NT_SUCCESS(PhGetProcessPeb32(ProcessHandle, &processPeb32)))
        {
            isWow64 = TRUE;
            PhpSetMemoryRegionType(MemoryMap, processPeb32, TRUE, Peb32Region);

            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, NumberOfHeaps)),
                &numberOfHeaps,
                sizeof(ULONG),
                NULL
            )) && numberOfHeaps < MAX_HEAPS)
            {
                processHeaps32 = (ULONG *)PhAllocate(numberOfHeaps * sizeof(ULONG));

                if (
                    NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, ProcessHeaps)), &processHeapsPtr32, sizeof(ULONG), NULL)) &&
                    NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, UlongToPtr(processHeapsPtr32), processHeaps32, numberOfHeaps * sizeof(ULONG), NULL))
                    )
                {
                    for (i = 0; i < numberOfHeaps; i++)
                    {
                        if (memoryItem = PhpSetMemoryRegionType(MemoryMap, UlongToPtr(processHeaps32[i]), TRUE, Heap32Region))
                            memoryItem->u.Heap.Index = i;
                    }
                }

                PhFree(processHeaps32);
            }

            // ApiSet schema map
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, ApiSetMap)),
                &apiSetMap32,
                sizeof(ULONG),
                NULL
            )) && apiSetMap32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(apiSetMap32), TRUE, ApiSetMapRegion);
            }

            // CSR shared memory
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, ReadOnlySharedMemoryBase)),
                &readOnlySharedMemory32,
                sizeof(ULONG),
                NULL
            )) && readOnlySharedMemory32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(readOnlySharedMemory32), TRUE, ReadOnlySharedMemoryRegion);
            }

            // CodePage data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, AnsiCodePageData)),
                &codePageData32,
                sizeof(ULONG),
                NULL
            )) && codePageData32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(codePageData32), TRUE, CodePageDataRegion);
            }

            // GDI shared handle table
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, GdiSharedHandleTable)),
                &gdiSharedHandleTable32,
                sizeof(ULONG),
                NULL
            )) && gdiSharedHandleTable32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(gdiSharedHandleTable32), TRUE, GdiSharedHandleTableRegion);
            }

            // Shim data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, pShimData)),
                &shimData32,
                sizeof(ULONG),
                NULL
            )) && shimData32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(shimData32), TRUE, ShimDataRegion);
            }

            // Process activation context data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, ActivationContextData)),
                &activationContextData32,
                sizeof(ULONG),
                NULL
            )) && activationContextData32)
            {
                if (memoryItem = PhpSetMemoryRegionType(MemoryMap, UlongToPtr(activationContextData32), TRUE, ActivationContextDataRegion))
                {
                    memoryItem->u.ActivationContextData.Type = ProcessActivationContext;
                }
            }

            // System-default activation context data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, SystemDefaultActivationContextData)),
                &defaultActivationContextData32,
                sizeof(ULONG),
                NULL
            )) && defaultActivationContextData32)
            {
                if (memoryItem = PhpSetMemoryRegionType(MemoryMap, UlongToPtr(defaultActivationContextData32), TRUE, ActivationContextDataRegion))
                {
                    memoryItem->u.ActivationContextData.Type = SystemActivationContext;
                }
            }

            // WER registration data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, WerRegistrationData)),
                &werRegistrationData32,
                sizeof(ULONG),
                NULL
            )) && werRegistrationData32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(werRegistrationData32), TRUE, WerRegistrationDataRegion);
            }

            // Silo shared data
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, SharedData)),
                &siloSharedData32,
                sizeof(ULONG),
                NULL
            )) && siloSharedData32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(siloSharedData32), TRUE, SiloSharedDataRegion);
            }

            // Telemetry coverage map
            if (NT_SUCCESS(NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(processPeb32, UFIELD_OFFSET(PEB32, TelemetryCoverageHeader)),
                &telemetryCoverageData32,
                sizeof(ULONG),
                NULL
            )) && telemetryCoverageData32)
            {
                PhpSetMemoryRegionType(MemoryMap, UlongToPtr(telemetryCoverageData32), TRUE, TelemetryCoverageRegion);
            }
        }
#endif
    }

    // TEB, stack
    for (i = 0; i < process->NumberOfThreads; i++)
    {
        PSYSTEM_EXTENDED_THREAD_INFORMATION thread = (PSYSTEM_EXTENDED_THREAD_INFORMATION)process->Threads + i;

        if (thread->TebBaseAddress)
        {
            NT_TIB ntTib;
            SIZE_T bytesRead;

            // HACK: Windows 10 RS2 and above 'added TEB/PEB sub-VAD segments' and we need to tag individual sections.
            if (memoryItem = PhpSetMemoryRegionType(MemoryMap, thread->TebBaseAddress, WindowsVersion < WINDOWS_10_RS2 ? TRUE : FALSE, TebRegion))
                memoryItem->u.Teb.ThreadId = thread->ThreadInfo.ClientId.UniqueThread;

            if (NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, thread->TebBaseAddress, &ntTib, sizeof(NT_TIB), &bytesRead)) &&
                bytesRead == sizeof(NT_TIB))
            {
                if ((ULONG_PTR)ntTib.StackLimit < (ULONG_PTR)ntTib.StackBase)
                {
                    if (memoryItem = PhpSetMemoryRegionType(MemoryMap, ntTib.StackLimit, TRUE, StackRegion))
                        memoryItem->u.Stack.ThreadId = thread->ThreadInfo.ClientId.UniqueThread;
                }
#ifdef _WIN64

                if (isWow64 && ntTib.ExceptionList)
                {
                    ULONG teb32 = PtrToUlong(ntTib.ExceptionList);
                    NT_TIB32 ntTib32;

                    // 64-bit and 32-bit TEBs usually share the same memory region, so don't do anything for the 32-bit
                    // TEB.

                    if (NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, UlongToPtr(teb32), &ntTib32, sizeof(NT_TIB32), &bytesRead)) &&
                        bytesRead == sizeof(NT_TIB32))
                    {
                        if (ntTib32.StackLimit < ntTib32.StackBase)
                        {
                            if (memoryItem = PhpSetMemoryRegionType(MemoryMap, UlongToPtr(ntTib32.StackLimit), TRUE, Stack32Region))
                                memoryItem->u.Stack.ThreadId = thread->ThreadInfo.ClientId.UniqueThread;
                        }
                    }
                }
#endif
            }
        }
    }

    // Mapped file, heap segment, unusable
    for(QMap<quint64, CMemoryPtr>::iterator I = MemoryMap.begin(); I != MemoryMap.end(); ++I)
    {
		memoryItem = I.value().staticCast<CWinMemory>();

        if (memoryItem->m_RegionType != UnknownRegion)
            continue;

        if (FlagOn(memoryItem->m_Type, MEM_MAPPED | MEM_IMAGE) && memoryItem->m_AllocationBaseItem == memoryItem)
        {
            MEMORY_IMAGE_INFORMATION imageInfo;
            PPH_STRING fileName;

            if (FlagOn(memoryItem->m_Type, MEM_IMAGE))
            {
                if (NT_SUCCESS(PhGetProcessMappedImageInformation(ProcessHandle, (PVOID)memoryItem->m_BaseAddress, &imageInfo)))
                {
                    memoryItem->u.MappedFile.SigningLevelValid = TRUE;
                    memoryItem->u.MappedFile.SigningLevel = (SE_SIGNING_LEVEL)imageInfo.ImageSigningLevel;
                }
            }

            if (NT_SUCCESS(PhGetProcessMappedFileName(ProcessHandle, (PVOID)memoryItem->m_BaseAddress, &fileName)))
            {
                PPH_STRING newFileName = PhResolveDevicePrefix(&fileName->sr);

                if (newFileName)
                    PhMoveReference((PVOID*)&fileName, newFileName);

                memoryItem->m_RegionType = MappedFileRegion;
                memoryItem->u_Custom_Text = CastPhString(fileName);
                continue;
            }
        }

        if (memoryItem->m_State & MEM_COMMIT)
        {
            UCHAR buffer[HEAP_SEGMENT_MAX_SIZE];

            if (NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, (PVOID)memoryItem->m_BaseAddress,
                buffer, sizeof(buffer), NULL)))
            {
                PVOID candidateHeap = NULL;
                ULONG candidateHeap32 = 0;
                QSharedPointer<CWinMemory> heapMemoryItem;
                PHEAP_SEGMENT heapSegment = (PHEAP_SEGMENT)buffer;
                PHEAP_SEGMENT32 heapSegment32 = (PHEAP_SEGMENT32)buffer;

                if (heapSegment->SegmentSignature == HEAP_SEGMENT_SIGNATURE)
                    candidateHeap = heapSegment->Heap;
                if (heapSegment32->SegmentSignature == HEAP_SEGMENT_SIGNATURE)
                    candidateHeap32 = heapSegment32->Heap;

                if (candidateHeap)
                {
                    QMap<quint64, CMemoryPtr>::iterator I = PhLookupMemoryItemList(MemoryMap, candidateHeap);

					if (I != MemoryMap.end())
					{
						heapMemoryItem = I.value().staticCast<CWinMemory>();
						if (heapMemoryItem->m_BaseAddress == (quint64)candidateHeap &&
							heapMemoryItem->m_RegionType == HeapRegion)
						{
							memoryItem->m_RegionType = HeapSegmentRegion;
							memoryItem->u_HeapSegment_HeapItem = heapMemoryItem;
							continue;
						}
					}
                }
                else if (candidateHeap32)
                {
                    QMap<quint64, CMemoryPtr>::iterator I = PhLookupMemoryItemList(MemoryMap, UlongToPtr(candidateHeap32));

					if (I != MemoryMap.end())
					{
						heapMemoryItem = I.value().staticCast<CWinMemory>();
						if (heapMemoryItem->m_BaseAddress == candidateHeap32 &&
							heapMemoryItem->m_RegionType == Heap32Region)
						{
							memoryItem->m_RegionType = HeapSegment32Region;
							memoryItem->u_HeapSegment_HeapItem = heapMemoryItem;
							continue;
						}
					}
                }
            }
        }

        if (FlagOn(memoryItem->m_Type, MEM_MAPPED) &&
            memoryItem->m_AllocationProtect == PAGE_READONLY &&
            memoryItem->m_AllocationBaseItem == memoryItem)
        {
            ACTIVATION_CONTEXT_DATA buffer;

            if (NT_SUCCESS(NtReadVirtualMemory(ProcessHandle, (PVOID)memoryItem->m_BaseAddress, &buffer, sizeof(buffer), NULL)))
            {
                if (buffer.Magic == ACTIVATION_CONTEXT_DATA_MAGIC)
                {
                    memoryItem->m_RegionType = ActivationContextDataRegion;
                    memoryItem->u.ActivationContextData.Type = CustomActivationContext;
                    continue;
                }
            }
        }
    }

#ifdef _WIN64

    PS_SYSTEM_DLL_INIT_BLOCK ldrInitBlock;
    QMap<quint64, CMemoryPtr>::iterator cfgBitmapMemoryItem;

    status = PhGetProcessSystemDllInitBlock(ProcessHandle, &ldrInitBlock);

    if (NT_SUCCESS(status))
    {
        PVOID cfgBitmapAddress = NULL;
        PVOID cfgBitmapWow64Address = NULL;

        if (RTL_CONTAINS_FIELD(&ldrInitBlock, ldrInitBlock.Size, Wow64CfgBitMap))
        {
            cfgBitmapAddress = (PVOID)ldrInitBlock.CfgBitMap;
            cfgBitmapWow64Address = (PVOID)ldrInitBlock.Wow64CfgBitMap;
        }

        if (cfgBitmapAddress)
		{
			cfgBitmapMemoryItem = PhLookupMemoryItemList(MemoryMap, cfgBitmapAddress);

			if(cfgBitmapMemoryItem != MemoryMap.end())
			{
				QMap<quint64, CMemoryPtr>::iterator memoryItem = cfgBitmapMemoryItem;

				while (memoryItem != MemoryMap.end() && memoryItem.value().staticCast<CWinMemory>()->m_AllocationBaseItem == cfgBitmapMemoryItem.value())
				{
					// lucasg: We could do a finer tagging since each MEM_COMMIT memory
					// std::map is the CFG bitmap of a loaded module. However that might be
					// brittle to changes made by Windows dev teams.
					memoryItem.value().staticCast<CWinMemory>()->m_RegionType = CfgBitmapRegion;

					memoryItem++;
				}
			}
        }

        // Note: Wow64 processes on 64bit also have CfgBitmap regions.
        if (isWow64 && cfgBitmapWow64Address)
        {
			cfgBitmapMemoryItem = PhLookupMemoryItemList(MemoryMap, cfgBitmapWow64Address);

			if(cfgBitmapMemoryItem != MemoryMap.end())
			{
				QMap<quint64, CMemoryPtr>::iterator memoryItem = cfgBitmapMemoryItem;

				while (memoryItem != MemoryMap.end() && memoryItem.value().staticCast<CWinMemory>()->m_AllocationBaseItem == cfgBitmapMemoryItem.value())
				{
					memoryItem.value().staticCast<CWinMemory>()->m_RegionType = CfgBitmap32Region;

					memoryItem++;
				}
			}
        }
    }
#endif

    PhFree(processes);

    return STATUS_SUCCESS;
}

NTSTATUS PhpUpdateMemoryWsCounters(
    QMap<quint64, CMemoryPtr>& MemoryMap,
    _In_ HANDLE ProcessHandle
    )
{
    PLIST_ENTRY listEntry;
    PMEMORY_WORKING_SET_EX_INFORMATION info;

    info = (PMEMORY_WORKING_SET_EX_INFORMATION)PhAllocatePage(WS_REQUEST_COUNT * sizeof(MEMORY_WORKING_SET_EX_INFORMATION), NULL);

    if (!info)
        return STATUS_NO_MEMORY;

	for(QMap<quint64, CMemoryPtr>::iterator I = MemoryMap.begin(); I != MemoryMap.end(); ++I)
    {
		QSharedPointer<CWinMemory> memoryItem = I.value().staticCast<CWinMemory>();
        ULONG_PTR virtualAddress;
        SIZE_T remainingPages;
        SIZE_T requestPages;
        SIZE_T i;

        if (!(memoryItem->m_State & MEM_COMMIT))
            continue;

        virtualAddress = (ULONG_PTR)memoryItem->m_BaseAddress;
        remainingPages = memoryItem->m_RegionSize / PAGE_SIZE;

        while (remainingPages != 0)
        {
            requestPages = std::min(remainingPages, (SIZE_T)WS_REQUEST_COUNT);

            for (i = 0; i < requestPages; i++)
            {
                info[i].VirtualAddress = (PVOID)virtualAddress;
                virtualAddress += PAGE_SIZE;
            }

            if (NT_SUCCESS(NtQueryVirtualMemory(
                ProcessHandle,
                NULL,
                MemoryWorkingSetExInformation,
                info,
                requestPages * sizeof(MEMORY_WORKING_SET_EX_INFORMATION),
                NULL
                )))
            {
                for (i = 0; i < requestPages; i++)
                {
                    PMEMORY_WORKING_SET_EX_BLOCK block = &info[i].VirtualAttributes;

                    if (block->Valid)
                    {
                        memoryItem->m_TotalWorkingSet += PAGE_SIZE;

                        if (block->ShareCount > 1)
                            memoryItem->m_SharedWorkingSet += PAGE_SIZE;
                        if (block->ShareCount == 0)
                            memoryItem->m_PrivateWorkingSet += PAGE_SIZE;
                        if (block->Shared)
                            memoryItem->m_ShareableWorkingSet += PAGE_SIZE;
                        if (block->Locked)
                            memoryItem->m_LockedWorkingSet += PAGE_SIZE;

                        if (memoryItem->m_Type & (MEM_MAPPED | MEM_IMAGE) && block->SharedOriginal)
                            memoryItem->m_SharedOriginalPages++;
                        if (block->Priority > memoryItem->m_Priority)
                            memoryItem->m_Priority = block->Priority;
                    }
                    else
                    {
                        if (memoryItem->m_Type & (MEM_MAPPED | MEM_IMAGE) && block->Invalid.SharedOriginal)
                            memoryItem->m_SharedOriginalPages++;

                        // VMMap does this, but is it correct? (dmex)
                        if (block->Invalid.Shared)
                            memoryItem->m_ShareableWorkingSet++;
                    }
                }
            }

            remainingPages -= requestPages;
        }
    }

    PhFreePage(info);

    return STATUS_SUCCESS;
}

/*NTSTATUS PhpUpdateMemoryWsCountersOld(
    QMap<quint64, CMemoryPtr>& MemoryMap,
    _In_ HANDLE ProcessHandle
    )
{
    NTSTATUS status;
    PMEMORY_WORKING_SET_INFORMATION info;
    QSharedPointer<CWinMemory> memoryItem = NULL;
    ULONG_PTR i;

    if (!NT_SUCCESS(status = PhGetProcessWorkingSetInformation(ProcessHandle, &info)))
        return status;

    for (i = 0; i < info->NumberOfEntries; i++)
    {
        PMEMORY_WORKING_SET_BLOCK block = &info->WorkingSetInfo[i];
        ULONG_PTR virtualAddress = block->VirtualPage * PAGE_SIZE;

        if (!memoryItem || virtualAddress < (ULONG_PTR)memoryItem->m_BaseAddress ||
            virtualAddress >= (ULONG_PTR)memoryItem->m_BaseAddress + memoryItem->m_RegionSize)
        {
            memoryItem = MemoryMap.value(virtualAddress);
        }

        if (memoryItem)
        {
            memoryItem->m_TotalWorkingSet += PAGE_SIZE;

            if (block->ShareCount > 1)
                memoryItem->m_SharedWorkingSet += PAGE_SIZE;
            if (block->ShareCount == 0)
                memoryItem->m_PrivateWorkingSet += PAGE_SIZE;
            if (block->Shared)
                memoryItem->m_ShareableWorkingSet += PAGE_SIZE;
        }
    }

    PhFree(info);

    return STATUS_SUCCESS;
}*/

NTSTATUS PhQueryMemoryItemList(
	_In_ HANDLE ProcessId,
	_In_ ULONG Flags,
	QMap<quint64, CMemoryPtr>& MemoryMap
)
{
    NTSTATUS status;
    HANDLE processHandle;
    ULONG_PTR allocationGranularity;
    PVOID baseAddress = (PVOID)0;
    MEMORY_BASIC_INFORMATION basicInfo;
    QSharedPointer<CWinMemory> allocationBaseItem;
    QSharedPointer<CWinMemory> previousMemoryItem;

    if (!NT_SUCCESS(status = PhOpenProcess(&processHandle, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, ProcessId)))
    {
        if (!NT_SUCCESS(status = PhOpenProcess(&processHandle, PROCESS_QUERY_INFORMATION, ProcessId)))
        {
			return status;
        }
    }

    allocationGranularity = PhSystemBasicInformation.AllocationGranularity;

	int count = 0;

    while (NT_SUCCESS(NtQueryVirtualMemory(
		processHandle, 
		baseAddress, 
		MemoryBasicInformation, 
		&basicInfo, 
		sizeof(MEMORY_BASIC_INFORMATION), 
		NULL
		)))
    {
	    QSharedPointer<CWinMemory> memoryItem;

        if (basicInfo.State & MEM_FREE)
        {
            if (Flags & PH_QUERY_MEMORY_IGNORE_FREE)
                goto ContinueLoop;

            basicInfo.AllocationBase = basicInfo.BaseAddress;
        }

		memoryItem = QSharedPointer<CWinMemory>(new CWinMemory());
		memoryItem->InitBasicInfo(&basicInfo, ProcessId);

        if (basicInfo.AllocationBase == basicInfo.BaseAddress)
            allocationBaseItem = memoryItem;
        if (allocationBaseItem && (quint64)basicInfo.AllocationBase == allocationBaseItem->m_BaseAddress)
            memoryItem->m_AllocationBaseItem = allocationBaseItem;

        //if (Flags & PH_QUERY_MEMORY_ZERO_PAD_ADDRESSES)
        //    PhPrintPointerPadZeros(memoryItem->BaseAddressString, memoryItem->BasicInfo.BaseAddress);
        //else
        //    PhPrintPointer(memoryItem->BaseAddressString, memoryItem->BasicInfo.BaseAddress);

        if (basicInfo.State & MEM_COMMIT)
        {
            memoryItem->m_CommittedSize = memoryItem->m_RegionSize;

            if (basicInfo.Type & MEM_PRIVATE)
                memoryItem->m_PrivateSize = memoryItem->m_RegionSize;
        }

        if (!FlagOn(basicInfo.State, MEM_FREE))
        {
            MEMORY_WORKING_SET_EX_INFORMATION pageInfo;

            //static_assert(HEAP_SEGMENT_MAX_SIZE < PAGE_SIZE, "Update query attributes for additional pages");

            // Query the attributes for the first page (dmex)

            memset(&pageInfo, 0, sizeof(MEMORY_WORKING_SET_EX_INFORMATION));
            pageInfo.VirtualAddress = baseAddress;

            if (NT_SUCCESS(NtQueryVirtualMemory(
                processHandle,
                NULL,
                MemoryWorkingSetExInformation,
                &pageInfo,
                sizeof(MEMORY_WORKING_SET_EX_INFORMATION),
                NULL
            )))
            {
                PMEMORY_WORKING_SET_EX_BLOCK block = &pageInfo.VirtualAttributes;

                memoryItem->m_Valid = !!block->Valid;
                memoryItem->m_Bad = !!block->Bad;
            }

            if (WindowsVersion > WINDOWS_8)
            {
                MEMORY_REGION_INFORMATION regionInfo;

                // Query the region (dmex)

                memset(&regionInfo, 0, sizeof(MEMORY_REGION_INFORMATION));

                if (NT_SUCCESS(NtQueryVirtualMemory(
                    processHandle,
                    baseAddress,
                    MemoryRegionInformationEx,
                    &regionInfo,
                    sizeof(MEMORY_REGION_INFORMATION),
                    NULL
                )))
                {
                    memoryItem->m_RegionTypeEx = regionInfo.RegionType;
                    //memoryItem->RegionSize = regionInfo.RegionSize;
                    //memoryItem->CommittedSize = regionInfo.CommitSize;
                }
            }
        }

		MemoryMap.insert(memoryItem->m_BaseAddress, memoryItem);

        if (basicInfo.State & MEM_FREE)
        {
            if ((ULONG_PTR)basicInfo.BaseAddress & (allocationGranularity - 1))
            {
                ULONG_PTR nextAllocationBase;
                ULONG_PTR potentialUnusableSize;

                // Split this free region into an unusable and a (possibly empty) usable region.

                nextAllocationBase = ALIGN_UP_BY(basicInfo.BaseAddress, allocationGranularity);
                potentialUnusableSize = nextAllocationBase - (ULONG_PTR)basicInfo.BaseAddress;

                memoryItem->m_RegionType = UnusableRegion;

                // VMMap does this, but is it correct?
                //if (previousMemoryItem && (previousMemoryItem->State & MEM_COMMIT))
                //    memoryItem->CommittedSize = min(potentialUnusableSize, basicInfo.RegionSize);

                if (nextAllocationBase < (ULONG_PTR)basicInfo.BaseAddress + basicInfo.RegionSize)
                {
                    QSharedPointer<CWinMemory> otherMemoryItem;

                    memoryItem->m_RegionSize = potentialUnusableSize;

                    otherMemoryItem = QSharedPointer<CWinMemory>(new CWinMemory());
					otherMemoryItem->InitBasicInfo(&basicInfo, ProcessId);

                    otherMemoryItem->m_BaseAddress = nextAllocationBase;
                    otherMemoryItem->m_AllocationBase = otherMemoryItem->m_BaseAddress;
                    otherMemoryItem->m_RegionSize = basicInfo.RegionSize - potentialUnusableSize;
                    otherMemoryItem->m_AllocationBaseItem = otherMemoryItem;

                    //if (Flags & PH_QUERY_MEMORY_ZERO_PAD_ADDRESSES)
                    //    PhPrintPointerPadZeros(otherMemoryItem->BaseAddressString, otherMemoryItem->BasicInfo.BaseAddress);
                    //else
                    //    PhPrintPointer(otherMemoryItem->BaseAddressString, otherMemoryItem->BasicInfo.BaseAddress);

                    MemoryMap.insert(otherMemoryItem->m_BaseAddress, otherMemoryItem);

                    previousMemoryItem = otherMemoryItem;
                    goto ContinueLoop;
                }
            }
        }

        previousMemoryItem = memoryItem;

ContinueLoop:
        baseAddress = PTR_ADD_OFFSET(baseAddress, basicInfo.RegionSize);
    }

    if (Flags & PH_QUERY_MEMORY_REGION_TYPE)
        PhpUpdateMemoryRegionTypes(MemoryMap, ProcessId, processHandle);

    if (Flags & PH_QUERY_MEMORY_WS_COUNTERS)
        PhpUpdateMemoryWsCounters(MemoryMap, processHandle);

    NtClose(processHandle);

	return STATUS_SUCCESS;
}