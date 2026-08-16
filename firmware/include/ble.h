// ble.h — BLE service, characteristics, status/config notify.
// Protocol kept byte-identical to the old firmware (see plan) so tools/ble-tester stays
// a valid verification oracle against this port.
#pragma once

void bleInit();       // idempotent: safe to call at runtime to switch BLE back on
void bleShutdown();   // tears the stack down and frees its memory
void bleStatusNotify();
