/*
 * App extensions on SDK SL_MATTER_ZIGBEE_SEQUENTIAL.
 * Matter-side leave/stay logic uses FabricTable in BaseApplication.
 */

#include "ProtocolMode.h"

#include "AppTask.h"
#include "BaseApplication.h"
#include "ZigbeeCallbacks.h"

#include "app/framework/include/af.h"

#include <app/server/Server.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>

namespace {

constexpr char kProtoModeKey[] = "ProtoMode";

uint8_t sActiveMode           = kProtocolModeNone;
bool sDualCommissioningActive = false;
bool sExclusiveLocked         = false;
bool sPermitJoinClosed        = false;

void PersistMode(uint8_t mode)
{
    (void) chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kProtoModeKey, &mode, sizeof(mode));
}

} // namespace

void ProtocolModeInitFromStorage(void)
{
    uint8_t stored = kProtocolModeNone;
    const CHIP_ERROR err =
        chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kProtoModeKey, &stored, sizeof(stored));
    if (err == CHIP_NO_ERROR && stored <= kProtocolModeZigbee)
    {
        sActiveMode = stored;
    }

    if (BaseApplication::sIsProvisioned)
    {
        sActiveMode = kProtocolModeMatter;
    }

    sExclusiveLocked  = (sActiveMode != kProtocolModeNone);
    sPermitJoinClosed = sExclusiveLocked || BaseApplication::sIsProvisioned;
}

bool ProtocolModeIsZigbeeExclusive(void)
{
    return sActiveMode == kProtocolModeZigbee;
}

bool ProtocolModeIsDualCommissioningActive(void)
{
    return sDualCommissioningActive;
}

bool ProtocolModeAllowZigbeePermitJoin(void)
{
    if (sExclusiveLocked || BaseApplication::sIsProvisioned || sPermitJoinClosed)
    {
        return false;
    }
    return true;
}

void ProtocolModeNotifyDualCommissioningActive(bool active)
{
    sDualCommissioningActive = active;
    if (active)
    {
        sPermitJoinClosed = false;
        if (sl_zigbee_af_network_state() == SL_ZIGBEE_JOINED_NETWORK)
        {
            Zigbee::RefreshPermitJoin("dual-active");
        }
        else
        {
            Zigbee::RequestStart();
        }
    }
    else
    {
        sPermitJoinClosed = true;
        Zigbee::StopJoinWindow();
    }
}

void ProtocolModeReopenJoinIfAllowed(void)
{
    if (!ProtocolModeAllowZigbeePermitJoin())
    {
        return;
    }

    Zigbee::RefreshPermitJoin("reopen");
}

void ProtocolModeOnAppInitComplete(void)
{
    if (!ProtocolModeAllowZigbeePermitJoin())
    {
        return;
    }

    Zigbee::RefreshPermitJoin("app-init-done");
}

static void EnterZigbeeExclusive(const char * reason)
{
    if (sExclusiveLocked)
    {
        return;
    }

    sExclusiveLocked = true;
    sActiveMode      = kProtocolModeZigbee;
    PersistMode(sActiveMode);
    ProtocolModeNotifyDualCommissioningActive(false);

    AppTask::CloseDualCommissioning();

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    (void) chip::DeviceLayer::ConnectivityMgr().SetBLEAdvertisingEnabled(false);
    chip::Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    Zigbee::StopJoinWindow();
    AppTask::NotifyPairingSuccess(reason);
    ChipLogError(Zcl, "[PROTO] Zigbee exclusive (%s)", (reason != nullptr) ? reason : "?");
}

void ProtocolModeOnZigbeeDeviceJoined(uint16_t nodeId)
{
    if (!ProtocolModeAllowZigbeePermitJoin())
    {
        return;
    }

    if (nodeId == 0 || nodeId == 0xFFFE)
    {
        return;
    }

    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { EnterZigbeeExclusive("zigbee-join"); }, 0);
}
