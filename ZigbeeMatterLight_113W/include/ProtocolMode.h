/*
 * App extensions on top of SDK SL_MATTER_ZIGBEE_SEQUENTIAL:
 *  - 15 min dual commissioning (Matter window + Zigbee permit-join)
 *  - Zigbee device join first -> blink, close Matter, persist Zigbee-only
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ActiveProtocolMode : uint8_t
{
    kProtocolModeNone   = 0,
    kProtocolModeMatter = 1,
    kProtocolModeZigbee = 2,
};

void ProtocolModeInitFromStorage(void);
bool ProtocolModeIsZigbeeExclusive(void);
bool ProtocolModeIsDualCommissioningActive(void);
bool ProtocolModeAllowZigbeePermitJoin(void);
void ProtocolModeNotifyDualCommissioningActive(bool active);
void ProtocolModeReopenJoinIfAllowed(void);
void ProtocolModeOnAppInitComplete(void);
void ProtocolModeOnZigbeeDeviceJoined(uint16_t nodeId);

#ifdef __cplusplus
}
#endif
