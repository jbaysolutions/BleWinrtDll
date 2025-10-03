#pragma once

#include <cstdint>

typedef void(__cdecl* BleLogSinkFn)(const wchar_t* msg);

struct RadioInfo {
    wchar_t name[256];
    int32_t kind;   // Windows::Devices::Radios::RadioKind
    int32_t state;  // Windows::Devices::Radios::RadioState
    int32_t accessStatus; // Windows::Devices::Radios::RadioAccessStatus
};

struct DeviceUpdate {
    wchar_t id[100];
    bool isConnectable = false;
    bool isConnectableUpdated = false;
    wchar_t name[50];
    bool nameUpdated = false;
};

struct Service {
    wchar_t uuid[100];
};

struct Characteristic {
    wchar_t uuid[100];
    wchar_t userDescription[100];
};

struct BLEData {
    uint8_t buf[512];
    uint16_t size;
    wchar_t deviceId[256];
    wchar_t serviceUuid[256];
    wchar_t characteristicUuid[256];
};

struct ErrorMessage {
    wchar_t msg[1024];
};

enum class ScanStatus { PROCESSING, AVAILABLE, FINISHED };
