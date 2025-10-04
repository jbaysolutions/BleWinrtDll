#pragma once

#include "BleTypes.h"

extern "C" {
	// Enumerate available radios. Pass nullptr/0 to query required count; returns total Bluetooth radio count.
	__declspec(dllexport) uint32_t GetRadios(RadioInfo* radios, uint32_t capacity);

	// Return true if at least one Bluetooth radio is present and currently on. Logs diagnostics either way.
	__declspec(dllexport) bool IsBluetoothAvailable();

	// Begin device discovery. seconds == 0 keeps scanning until StopDeviceScan or Quit is called.
	__declspec(dllexport) void StartDeviceScan(uint32_t seconds);

	// Stop an active device scan and release watcher resources.
	__declspec(dllexport) void StopDeviceScan();

	__declspec(dllexport) ScanStatus PollDevice(DeviceUpdate* device, bool block);

    // Connect/disconnect against a WinRT device ID.
    __declspec(dllexport) bool ConnectDevice(wchar_t* deviceId, bool block);
	
    __declspec(dllexport) bool DisconnectDevice(wchar_t* deviceId);


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
