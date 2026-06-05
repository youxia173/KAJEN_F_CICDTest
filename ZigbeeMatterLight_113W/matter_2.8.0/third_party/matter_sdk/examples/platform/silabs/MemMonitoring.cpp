/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "MemMonitoring.h"

#include "AppConfig.h"
#include <cmsis_os2.h>
#include <platform/CHIPDeviceLayer.h>
#include <sl_cmsis_os2_common.h>
#include <sl_memory_manager.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

static osThreadId_t sMonitorThreadHandle;
constexpr uint32_t kMonitorTaskSize = 1024;
constexpr uint32_t kMonitorIntervalSec = 30;
constexpr uint32_t kCriticalStackSpaceBytes = 128;
constexpr uint32_t kMaxTrackedThreads = 24;
static uint8_t monitorStack[kMonitorTaskSize];
static osThread_t sMonitorTaskControlBlock;
constexpr osThreadAttr_t kMonitorTaskAttr = { .name       = "MemMonitor",
                                              .attr_bits  = osThreadDetached,
                                              .cb_mem     = &sMonitorTaskControlBlock,
                                              .cb_size    = osThreadCbSize,
                                              .stack_mem  = monitorStack,
                                              .stack_size = kMonitorTaskSize,
                                              .priority   = osPriorityLow };

size_t nbAllocSuccess        = 0;
size_t nbFreeSuccess         = 0;
size_t largestBlockAllocated = 0;

namespace {

const char * SafeThreadName(osThreadId_t threadId)
{
    const char * name = osThreadGetName(threadId);
    return (name != nullptr && name[0] != '\0') ? name : "<noname>";
}

} // namespace

void MemMonitoring::StartMonitor()
{
    sMonitorThreadHandle = osThreadNew(MonitorTask, nullptr, &kMonitorTaskAttr);
}

void MemMonitoring::MonitorTask(void * pvParameter)
{
    (void) pvParameter;

    while (true)
    {
        osThreadId_t threadIdTable[kMaxTrackedThreads] = { 0 };
        uint32_t threadCount                           = osThreadGetCount();
        if (threadCount > kMaxTrackedThreads)
        {
            threadCount = kMaxTrackedThreads;
        }
        threadCount = osThreadEnumerate(threadIdTable, threadCount);

        bool hasZeroStackThread                 = false;
        bool hasLowStackThread                  = false;
        uint32_t lowestStackSpace               = UINT32_MAX;
        const char * lowestStackThreadName      = "<none>";
        const char * zeroStackThreadName        = "<none>";
        uint8_t trackedThreadCountWithoutMonitor = 0;

        for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
        {
            osThreadId_t tId = threadIdTable[tIdIndex];
            if (tId == nullptr || tId == sMonitorThreadHandle)
            {
                continue;
            }

            trackedThreadCountWithoutMonitor++;
            uint32_t stackSpace = osThreadGetStackSpace(tId);
            const char * taskName = SafeThreadName(tId);

            if (stackSpace < lowestStackSpace)
            {
                lowestStackSpace      = stackSpace;
                lowestStackThreadName = taskName;
            }

            if (stackSpace == 0)
            {
                hasZeroStackThread = true;
                zeroStackThreadName = taskName;
            }
            else if (stackSpace <= kCriticalStackSpaceBytes)
            {
                hasLowStackThread = true;
            }
        }

        SILABS_LOG("=============================");
        SILABS_LOG(" ");
        SILABS_LOG("Largest Block allocated     %lu B", largestBlockAllocated);
        SILABS_LOG("Number Of Successful Alloc  %lu", nbAllocSuccess);
        SILABS_LOG("Number Of Successful Frees  %lu", nbFreeSuccess);
        SILABS_LOG("Tracked threads              %u", trackedThreadCountWithoutMonitor);
        SILABS_LOG("Lowest stack highwatermark   %lu B (%s)",
                   (lowestStackSpace == UINT32_MAX) ? 0UL : static_cast<unsigned long>(lowestStackSpace), lowestStackThreadName);

        const long long totalBytes = static_cast<long long>(sl_memory_get_total_heap_size());
        const long long usedBytes  = static_cast<long long>(sl_memory_get_used_heap_size());
        const long long freeBytes  = totalBytes - usedBytes;
        const long long highWaterMarkHeadroom =
            totalBytes - static_cast<long long>(sl_memory_get_heap_high_watermark());

        SILABS_LOG("HEAP memory: used %lldB free %lldB total %lldB", usedBytes, freeBytes, totalBytes);
        SILABS_LOG("High water mark: %lldB", highWaterMarkHeadroom);

        if (hasZeroStackThread)
        {
            SILABS_LOG("[MEM][CRIT] stack highwatermark reached 0 B on task: %s", zeroStackThreadName);
        }
        else if (hasLowStackThread)
        {
            SILABS_LOG("[MEM][WARN] task stack highwatermark is below %lu B", static_cast<unsigned long>(kCriticalStackSpaceBytes));
        }

        SILABS_LOG(" ");

        SILABS_LOG("Thread stack highwatermark ");
        for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
        {
            osThreadId_t tId = threadIdTable[tIdIndex];
            if (tId != nullptr && tId != sMonitorThreadHandle) // don't print stats for this current debug thread.
            {
                // The smallest amount of free stack space there has been since the thread creation
                SILABS_LOG("\t%-10s : %6lu B", SafeThreadName(tId), osThreadGetStackSpace(tId));
            }
        }

        SILABS_LOG(" ");
        SILABS_LOG("=============================");
        // Reduced log cadence to lower noise while still preserving observability.
        osDelay(osKernelGetTickFreq() * kMonitorIntervalSec);
    }
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip

extern "C" void memMonitoringTrackAlloc(void * ptr, size_t size)
{
    if (ptr != NULL)
    {
        chip::DeviceLayer::Silabs::nbAllocSuccess++;
        if (chip::DeviceLayer::Silabs::largestBlockAllocated < size)
        {
            chip::DeviceLayer::Silabs::largestBlockAllocated = size;
        }
    }
}

extern "C" void memMonitoringTrackFree(void * ptr, size_t size)
{
    chip::DeviceLayer::Silabs::nbFreeSuccess++;
}
