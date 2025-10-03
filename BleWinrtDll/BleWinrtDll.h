#pragma once

#include "BleTypes.h"

extern "C" {

	__declspec(dllexport) bool IsBluetoothAvailable();

	__declspec(dllexport) void StartDeviceScan();

	__declspec(dllexport) ScanStatus PollDevice(DeviceUpdate* device, bool block);

	__declspec(dllexport) void StopDeviceScan();

	__declspec(dllexport) void ScanServices(wchar_t* deviceId);

	__declspec(dllexport) ScanStatus PollService(Service* service, bool block);

	__declspec(dllexport) void ScanCharacteristics(wchar_t* deviceId, wchar_t* serviceId);

	__declspec(dllexport) ScanStatus PollCharacteristic(Characteristic* characteristic, bool block);

	__declspec(dllexport) bool SubscribeCharacteristic(wchar_t* deviceId, wchar_t* serviceId, wchar_t* characteristicId, bool block);

	__declspec(dllexport) bool PollData(BLEData* data, bool block);

	__declspec(dllexport) bool SendData(BLEData* data, bool block);

	__declspec(dllexport) void Quit();

	__declspec(dllexport) void GetError(ErrorMessage* buf);

	__declspec(dllexport) void SetLogSink(BleLogSinkFn sink);
}
