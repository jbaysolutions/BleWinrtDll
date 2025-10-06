// BleWinrtDll.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"

#include "BleWinrtDll.h"

#pragma comment(lib, "windowsapp")

// macro for file, see also https://stackoverflow.com/a/14421702
#define __WFILE__ L"BleWinrtDll.cpp"

using namespace std;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

using namespace Windows::Storage::Streams;

using namespace Windows::Devices::Radios;

union to_guid
{
	uint8_t buf[16];
	guid guid;
};

const uint8_t BYTE_ORDER[] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };

guid make_guid(const wchar_t* value)
{
	to_guid to_guid;
	memset(&to_guid, 0, sizeof(to_guid));
	int offset = 0;
	for (int i = 0; i < wcslen(value); i++) {
		if (value[i] >= '0' && value[i] <= '9')
		{
			uint8_t digit = value[i] - '0';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else if (value[i] >= 'A' && value[i] <= 'F')
		{
			uint8_t digit = 10 + value[i] - 'A';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else if (value[i] >= 'a' && value[i] <= 'f')
		{
			uint8_t digit = 10 + value[i] - 'a';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else
		{
			// skip char
		}
	}

	return to_guid.guid;
}

// implement own caching instead of using the system-provicded cache as there is an AccessDenied error when trying to
// call GetCharacteristicsAsync on a service for which a reference is hold in global scope
// cf. https://stackoverflow.com/a/36106137

mutex errorLock;
wchar_t last_error[2048];
struct CharacteristicCacheEntry {
	GattCharacteristic characteristic = nullptr;
};
struct ServiceCacheEntry {
	GattDeviceService service = nullptr;
	map<long, CharacteristicCacheEntry> characteristics = { };
};
struct DeviceCacheEntry {
    BluetoothLEDevice device = nullptr;
    map<long, ServiceCacheEntry> services = { };
    BluetoothLEDevice::ConnectionStatusChanged_revoker statusChangedRevoker{};
    bool statusSubscribed = false;
};
map<long, DeviceCacheEntry> cache;
void EnsureStatusSubscription(long key, BluetoothLEDevice const& device);


// using hashes of uuids to omit storing the c-strings in reliable storage
long hsh(wchar_t* wstr)
{
	long hash = 5381;
	int c;
	while (c = *wstr++)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

void clearError() {
	lock_guard error_lock(errorLock);
	wcscpy_s(last_error, L"Ok");
}

void saveError(const wchar_t* message, ...) {
	lock_guard error_lock(errorLock);
	va_list args;
	va_start(args, message);
	vswprintf_s(last_error, message, args);
	va_end(args);
	wcout << last_error << endl;
}

IAsyncOperation<BluetoothLEDevice> retrieveDevice(wchar_t* deviceId)
{
    auto key = hsh(deviceId);

    if (auto it = cache.find(key); it != cache.end())
    {
        EnsureStatusSubscription(key, it->second.device);
        co_return it->second.device;
    }

    BluetoothLEDevice result = co_await BluetoothLEDevice::FromIdAsync(deviceId);
    if (!result)
    {
        saveError(L"%s:%d Failed to connect to device.", __WFILE__, __LINE__);
        co_return nullptr;
    }

    clearError();
    auto& entry = cache[key];
    entry.device = result;
    EnsureStatusSubscription(key, entry.device);
    co_return entry.device;
}
IAsyncOperation<GattDeviceService> retrieveService(wchar_t* deviceId, wchar_t* serviceId) {
	auto device = co_await retrieveDevice(deviceId);
	if (device == nullptr)
		co_return nullptr;
	if (cache[hsh(deviceId)].services.count(hsh(serviceId)))
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].service;
	GattDeviceServicesResult result = co_await device.GetGattServicesForUuidAsync(make_guid(serviceId), BluetoothCacheMode::Cached);
	if (result.Status() != GattCommunicationStatus::Success) {
		saveError(L"%s:%d Failed retrieving services.", __WFILE__, __LINE__);
		co_return nullptr;
	}
	else if (result.Services().Size() == 0) {
		saveError(L"%s:%d No service found with uuid ", __WFILE__, __LINE__);
		co_return nullptr;
	}
	else {
		clearError();
		cache[hsh(deviceId)].services[hsh(serviceId)] = { result.Services().GetAt(0) };
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].service;
	}
}
IAsyncOperation<GattCharacteristic> retrieveCharacteristic(wchar_t* deviceId, wchar_t* serviceId, wchar_t* characteristicId) {
	auto service = co_await retrieveService(deviceId, serviceId);
	if (service == nullptr)
		co_return nullptr;
	if (cache[hsh(deviceId)].services[hsh(serviceId)].characteristics.count(hsh(characteristicId)))
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)].characteristic;
	GattCharacteristicsResult result = co_await service.GetCharacteristicsForUuidAsync(make_guid(characteristicId), BluetoothCacheMode::Cached);
	if (result.Status() != GattCommunicationStatus::Success) {
		saveError(L"%s:%d Error scanning characteristics from service %s with status %d", __WFILE__, __LINE__, serviceId, result.Status());
		co_return nullptr;
	}
	else if (result.Characteristics().Size() == 0) {
		saveError(L"%s:%d No characteristic found with uuid %s", __WFILE__, __LINE__, characteristicId);
		co_return nullptr;
	}
	else {
		clearError();
		cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)] = { result.Characteristics().GetAt(0) };
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)].characteristic;
	}
}



DeviceWatcher deviceWatcher{ nullptr };
DeviceWatcher::Added_revoker deviceWatcherAddedRevoker;
DeviceWatcher::Updated_revoker deviceWatcherUpdatedRevoker;
DeviceWatcher::EnumerationCompleted_revoker deviceWatcherCompletedRevoker;

queue<DeviceUpdate> deviceQueue{};
mutex deviceQueueLock;
condition_variable deviceQueueSignal;
bool deviceScanFinished;

queue<Service> serviceQueue{};
mutex serviceQueueLock;
condition_variable serviceQueueSignal;
bool serviceScanFinished;

queue<Characteristic> characteristicQueue{};
mutex characteristicQueueLock;
condition_variable characteristicQueueSignal;
bool characteristicScanFinished;

// global flag to release calling thread
mutex quitLock;
bool quitFlag = false;

struct Subscription {
	GattCharacteristic characteristic = nullptr;
	GattCharacteristic::ValueChanged_revoker revoker;
};
list<Subscription*> subscriptions;
mutex subscribeQueueLock;
condition_variable subscribeQueueSignal;

queue<BLEData> dataQueue{};
mutex dataQueueLock;
condition_variable dataQueueSignal;

namespace
{
    DeviceUpdate MakeDeviceUpdate(DeviceInformation const& info)
    {
        DeviceUpdate update{};
        wcscpy_s(update.id, _countof(update.id), info.Id().c_str());

        if (info.Name().size())
        {
            wcscpy_s(update.name, _countof(update.name), info.Name().c_str());
            update.nameUpdated = true;
        }

        if (info.Properties().HasKey(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"))
        {
            update.isConnectable =
                unbox_value<bool>(info.Properties().Lookup(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"));
            update.isConnectableUpdated = true;
        }
        return update;
    }

    DeviceUpdate MakeDeviceUpdate(DeviceInformationUpdate const& info)
    {
        DeviceUpdate update{};
        wcscpy_s(update.id, _countof(update.id), info.Id().c_str());

        if (info.Properties().HasKey(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"))
        {
            update.isConnectable =
                unbox_value<bool>(info.Properties().Lookup(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"));
            update.isConnectableUpdated = true;
        }
        return update;
    }

	inline bool ShouldQuit()
    {
        std::lock_guard guard(quitLock);
        return quitFlag;
    }

    inline void Enqueue(DeviceUpdate const& update)
    {
        std::lock_guard guard(deviceQueueLock);
        deviceQueue.push(update);
        deviceQueueSignal.notify_one();
    }

	Service MakeService(GattDeviceService const& svc)
    {
        Service s{};
        wcscpy_s(s.uuid, _countof(s.uuid), to_hstring(svc.Uuid()).c_str());
        return s;
    }

    Characteristic MakeCharacteristic(GattCharacteristic const& c,
                                      wchar_t const* userDescription)
    {
        Characteristic ch{};
        wcscpy_s(ch.uuid, _countof(ch.uuid), to_hstring(c.Uuid()).c_str());
        wcscpy_s(ch.userDescription, _countof(ch.userDescription), userDescription);
        return ch;
    }

	void EnqueueService(Service const& svc)
    {
        std::lock_guard guard(serviceQueueLock);
        serviceQueue.push(svc);
        serviceQueueSignal.notify_one();
    }

    void EnqueueCharacteristic(Characteristic const& ch)
    {
        std::lock_guard guard(characteristicQueueLock);
        characteristicQueue.push(ch);
        characteristicQueueSignal.notify_one();
    }

    std::wstring ReadCharacteristicDescription(GattCharacteristic const& ch)
    {
        constexpr auto userDescUuid = L"00002901-0000-1000-8000-00805F9B34FB";
        auto descScan = ch.GetDescriptorsForUuidAsync(make_guid(userDescUuid), BluetoothCacheMode::Uncached).get();
        if (descScan.Descriptors().Size() == 0)
            return L"no description available";

        auto descriptor = descScan.Descriptors().GetAt(0);
        auto value = descriptor.ReadValueAsync().get();
        if (value.Status() != GattCommunicationStatus::Success)
            throw hresult_error(E_FAIL, L"ReadValueAsync failed");

        auto reader = DataReader::FromBuffer(value.Value());
        return std::wstring(reader.ReadString(reader.UnconsumedBufferLength()));
    }
}

bool QuittableWait(condition_variable& signal, unique_lock<mutex>& waitLock) {
	{
		lock_guard quit_lock(quitLock);
		if (quitFlag)
			return true;
	}
	signal.wait(waitLock);
	lock_guard quit_lock(quitLock);
	return quitFlag;
}

void DeviceWatcher_Added(DeviceWatcher, DeviceInformation info)
{
    if (ShouldQuit()) return;
    Enqueue(MakeDeviceUpdate(info));
}

void DeviceWatcher_Updated(DeviceWatcher, DeviceInformationUpdate info)
{
    if (ShouldQuit()) return;
    Enqueue(MakeDeviceUpdate(info));
}

void DeviceWatcher_EnumerationCompleted(DeviceWatcher sender, IInspectable const&) {
	// StopDeviceScan();
}

// ---- logging sink (no windows.h) ----
static BleLogSinkFn g_logSink = nullptr;

void SetLogSink(BleLogSinkFn sink)
{
    g_logSink = sink;
}

static inline void LogLine(const std::wstring& s)
{
    if (g_logSink) g_logSink(s.c_str());
    else           std::wcerr << s << L'\n'; // simple fallback
}

static inline void ensure_apartment()
{
    try
    {
        // OK on threads without a COM apartment; throws if already STA
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (winrt::hresult_error const& e)
    {
        // Ignore "Cannot change thread mode after it is set." (RPC_E_CHANGED_MODE)
        if (e.code() != winrt::hresult{ static_cast<int32_t>(0x80010106) })
        {
            throw; // anything else is a real error
        }
        // Already initialized (likely STA) — safe to proceed.
    }
}

uint32_t GetRadios(RadioInfo* radios, uint32_t capacity)
{
    const uint32_t bufferCapacity = (radios != nullptr) ? capacity : 0;
    uint32_t bluetoothCount = 0;

    try
    {
        ensure_apartment();

        auto accessStatus = Radio::RequestAccessAsync().get();
        if (accessStatus != RadioAccessStatus::Allowed)
        {
            saveError(L"%s:%d Bluetooth radio access denied (status %d).",
                      __WFILE__, __LINE__, static_cast<int32_t>(accessStatus));
            return 0;
        }

        auto radiosVector = Radio::GetRadiosAsync().get();
        LogLine(L"[BleWinrtDll] ---- Bluetooth radio enumeration ----");

        for (auto const& radio : radiosVector)
        {
            if (radio.Kind() != RadioKind::Bluetooth)
            {
                continue;
            }

            if (bufferCapacity && bluetoothCount < bufferCapacity)
            {
                auto& out = radios[bluetoothCount];
                wcsncpy_s(out.name,
                          _countof(out.name),
                          radio.Name().c_str(),
                          _TRUNCATE);
                out.kind = static_cast<int32_t>(radio.Kind());
                out.state = static_cast<int32_t>(radio.State());
                out.accessStatus = static_cast<int32_t>(accessStatus);
            }

            ++bluetoothCount;
        }

        if (bufferCapacity && bluetoothCount > bufferCapacity)
        {
            LogLine(L"[BleWinrtDll] Warning: radio buffer truncated.");
        }

        clearError();
        return bluetoothCount;
    }
    catch (hresult_error const& e)
    {
        saveError(L"%s:%d Bluetooth radio enumeration failed: %s",
                  __WFILE__, __LINE__, e.message().c_str());
    }
    catch (...)
    {
        saveError(L"%s:%d Bluetooth radio enumeration failed: unknown exception.",
                  __WFILE__, __LINE__);
    }

    return 0;
}

// ---- availability with full radio dump (no windows.h) ----
bool IsBluetoothAvailable()
{
    try
    {
        ensure_apartment();

        auto radios = Radio::GetRadiosAsync().get();
        LogLine(L"[BleWinrtDll] ---- Radio enumeration ----");
        LogLine(L"[BleWinrtDll] Count = " + std::to_wstring(radios.Size()));

        bool anyBt = false, btOn = false;

        for (auto const& r : radios)
        {
            std::wstring name = r.Name().c_str();
            std::wstring line = L"[BleWinrtDll]  - Name=\"" + name +
                                L"\", Kind=" + std::to_wstring(static_cast<int32_t>(r.Kind())) +
                                L", State=" + std::to_wstring(static_cast<int32_t>(r.State()));
            LogLine(line);

            if (r.Kind() == RadioKind::Bluetooth) {
                anyBt = true;
                if (r.State() == RadioState::On) btOn = true;
            }
        }

        if (!anyBt) LogLine(L"[BleWinrtDll] No Bluetooth radio found.");
        LogLine(L"[BleWinrtDll] ---- End enumeration ----");

        return btOn;
    }
    catch (hresult_error const& e)
    {
        LogLine(std::wstring(L"[BleWinrtDll] Radio enumeration failed: ") + e.message().c_str());
        return false;
    }
    catch (...)
    {
        LogLine(L"[BleWinrtDll] Radio enumeration failed: unknown exception.");
        return false;
    }
}

void StartDeviceScan(uint32_t seconds) {
	// as this is the first function that must be called, if Quit() was called before, assume here that the client wants to restart
	{
		lock_guard lock(quitLock);
		quitFlag = false;
		clearError();
	}

	IVector<hstring> requestedProperties = single_threaded_vector<hstring>({ L"System.Devices.Aep.DeviceAddress", L"System.Devices.Aep.IsConnected", L"System.Devices.Aep.Bluetooth.Le.IsConnectable" });
	hstring aqsAllBluetoothLEDevices = L"(System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\")"; // list Bluetooth LE devices
	deviceWatcher = DeviceInformation::CreateWatcher(
		aqsAllBluetoothLEDevices,
		requestedProperties,
		DeviceInformationKind::AssociationEndpoint);

	// see https://docs.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/handle-events#revoke-a-registered-delegate
	deviceWatcherAddedRevoker = deviceWatcher.Added(auto_revoke, &DeviceWatcher_Added);
	deviceWatcherUpdatedRevoker = deviceWatcher.Updated(auto_revoke, &DeviceWatcher_Updated);
	deviceWatcherCompletedRevoker = deviceWatcher.EnumerationCompleted(auto_revoke, &DeviceWatcher_EnumerationCompleted);
	// ~30 seconds scan ; for permanent scanning use BluetoothLEAdvertisementWatcher, see the BluetoothAdvertisement.zip sample
	deviceScanFinished = false;
	deviceWatcher.Start();

	if (seconds > 0) {
        std::thread([seconds] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            StopDeviceScan();
        }).detach();
    }
}

void StopDeviceScan() {
	lock_guard lock(deviceQueueLock);
	if (deviceWatcher != nullptr) {
		deviceWatcherAddedRevoker.revoke();
		deviceWatcherUpdatedRevoker.revoke();
		deviceWatcherCompletedRevoker.revoke();
		deviceWatcher.Stop();
		deviceWatcher = nullptr;
	}
	deviceScanFinished = true;
	deviceQueueSignal.notify_one();
}

ScanStatus PollDevice(DeviceUpdate* device, bool block)
{
    std::unique_lock<std::mutex> lock(deviceQueueLock);

    while (deviceQueue.empty())
    {
        if (deviceScanFinished)
            return ScanStatus::FINISHED;

        if (!block)
            return ScanStatus::PROCESSING;

        if (QuittableWait(deviceQueueSignal, lock))
            return ScanStatus::FINISHED;
    }

    *device = deviceQueue.front();
    deviceQueue.pop();
    return ScanStatus::AVAILABLE;
}

// Connect
queue<ConnectionUpdate> connectionQueue{};
mutex connectionQueueLock;
condition_variable connectionQueueSignal;



void EnqueueConnectionUpdate(const wchar_t* deviceId, BluetoothConnectionStatus status)
{
    ConnectionUpdate update{};
    wcsncpy_s(update.deviceId, _countof(update.deviceId), deviceId, _TRUNCATE);
    update.status = static_cast<int32_t>(status);

    {
        lock_guard lock(quitLock);
        if (quitFlag)
            return;
    }
    {
        lock_guard queueLock(connectionQueueLock);
        connectionQueue.push(update);
    }
    connectionQueueSignal.notify_one();
}

void BluetoothLEDevice_ConnectionStatusChanged(BluetoothLEDevice const& sender, IInspectable const&)
{
    EnqueueConnectionUpdate(sender.DeviceId().c_str(), sender.ConnectionStatus());
}

void EnsureStatusSubscription(long key, BluetoothLEDevice const& device)
{
    auto& entry = cache[key];
    if (!entry.statusSubscribed)
    {
        entry.statusChangedRevoker = device.ConnectionStatusChanged(auto_revoke, &BluetoothLEDevice_ConnectionStatusChanged);
        entry.statusSubscribed = true;
        EnqueueConnectionUpdate(device.DeviceId().c_str(), device.ConnectionStatus());
    }
}

bool PollConnection(ConnectionUpdate* update, bool block)
{
    unique_lock<mutex> lock(connectionQueueLock);
    while (connectionQueue.empty())
    {
        if (!block)
            return false;
        if (QuittableWait(connectionQueueSignal, lock))
            return false;
    }
    *update = connectionQueue.front();
    connectionQueue.pop();
    return true;
}

IAsyncOperation<bool> ConnectDeviceAsync(wchar_t* deviceId)
{
    try
    {
        auto device = co_await retrieveDevice(deviceId);
        if (!device)
        {
            saveError(L"%s:%d ConnectDeviceAsync: device not cached/available.", __WFILE__, __LINE__);
            co_return false;
        }

        // Optional validation – touching GATT forces creation and surfaces access failures early.
        auto probe = co_await device.GetGattServicesAsync(BluetoothCacheMode::Cached);
        if (probe.Status() != GattCommunicationStatus::Success)
        {
            saveError(L"%s:%d ConnectDeviceAsync: probe failed with status %d.",
                      __WFILE__, __LINE__, static_cast<int>(probe.Status()));
            co_return false;
        }

        clearError();
        co_return true;
    }
    catch (hresult_error const& e)
    {
        saveError(L"%s:%d ConnectDeviceAsync threw: %s",
                  __WFILE__, __LINE__, e.message().c_str());
        co_return false;
    }
    catch (...)
    {
        saveError(L"%s:%d ConnectDeviceAsync: unknown exception.",
                  __WFILE__, __LINE__);
        co_return false;
    }
}

bool ConnectDevice(wchar_t* deviceId, bool block)
{
    auto op = ConnectDeviceAsync(deviceId);
    return block ? op.get() : (op.Completed([](auto&&, auto&&) {}), true);
}

// Disconnect
bool DisconnectDevice(wchar_t* deviceId)
{
    try
    {
        auto key = hsh(deviceId);
        auto it = cache.find(key);
        if (it == cache.end())
        {
            saveError(L"%s:%d DisconnectDevice: device %s not cached.",
                      __WFILE__, __LINE__, deviceId);
            return false;
        }

        {
            std::lock_guard subLock(subscribeQueueLock);
            for (auto iter = subscriptions.begin(); iter != subscriptions.end();)
            {
                auto* sub = *iter;
                if (sub && sub->characteristic.Service().Device().DeviceId() == deviceId)
                {
                    sub->revoker.revoke();
                    delete sub;
                    iter = subscriptions.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }
        }

        if (it->second.statusSubscribed)
        {
            it->second.statusChangedRevoker.revoke();
            it->second.statusSubscribed = false;
        }
        EnqueueConnectionUpdate(deviceId, BluetoothConnectionStatus::Disconnected);

        for (auto& svcPair : it->second.services)
        {
            svcPair.second.service.Close();
        }
        it->second.device.Close();
        cache.erase(it);

        clearError();
        return true;
    }
    catch (hresult_error const& e)
    {
        saveError(L"%s:%d DisconnectDevice failed: %s",
                  __WFILE__, __LINE__, e.message().c_str());
    }
    catch (...)
    {
        saveError(L"%s:%d DisconnectDevice failed: unknown exception.",
                  __WFILE__, __LINE__);
    }
    return false;
}






fire_and_forget ScanServicesAsync(wchar_t* deviceId) {
	{
		lock_guard queueGuard(serviceQueueLock);
		serviceScanFinished = false;
	}
	try {
		auto bluetoothLeDevice = co_await retrieveDevice(deviceId);
		if (bluetoothLeDevice != nullptr) {
			GattDeviceServicesResult result = co_await bluetoothLeDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached);
			if (result.Status() == GattCommunicationStatus::Success) {
				for (auto&& svc : result.Services())
				{
					if (ShouldQuit()) break;
					EnqueueService(MakeService(svc));
				}
			}
			else {
				saveError(L"%s:%d Failed retrieving services.", __WFILE__, __LINE__);
			}
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d ScanServicesAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	{
		lock_guard queueGuard(serviceQueueLock);
		serviceScanFinished = true;
		serviceQueueSignal.notify_one();
	}
}
void ScanServices(wchar_t* deviceId) {
	ScanServicesAsync(deviceId);
}

ScanStatus PollService(Service* service, bool block) {
	ScanStatus res;
	unique_lock<mutex> lock(serviceQueueLock);
	if (block && serviceQueue.empty() && !serviceScanFinished)
		if (QuittableWait(serviceQueueSignal, lock))
			return ScanStatus::FINISHED;
	if (!serviceQueue.empty()) {
		*service = serviceQueue.front();
		serviceQueue.pop();
		res = ScanStatus::AVAILABLE;
	}
	else if (serviceScanFinished)
		res = ScanStatus::FINISHED;
	else
		res = ScanStatus::PROCESSING;
	return res;
}

fire_and_forget ScanCharacteristicsAsync(wchar_t* deviceId, wchar_t* serviceId) {
	{
		lock_guard lock(characteristicQueueLock);
		characteristicScanFinished = false;
	}
	try {
		auto service = co_await retrieveService(deviceId, serviceId);
		if (service != nullptr) {
			GattCharacteristicsResult charScan = co_await service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached);
			if (charScan.Status() != GattCommunicationStatus::Success)
				saveError(L"%s:%d Error scanning characteristics from service %s width status %d", __WFILE__, __LINE__, serviceId, (int)charScan.Status());
			else {
				for (auto&& c : charScan.Characteristics())
				{
					auto description = ReadCharacteristicDescription(c); // helper that wraps descriptor logic
					if (ShouldQuit()) break;
					EnqueueCharacteristic(MakeCharacteristic(c, description.c_str()));
				}
			}
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d ScanCharacteristicsAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	{
		lock_guard lock(characteristicQueueLock);
		characteristicScanFinished = true;
		characteristicQueueSignal.notify_one();
	}
}

void ScanCharacteristics(wchar_t* deviceId, wchar_t* serviceId) {
	ScanCharacteristicsAsync(deviceId, serviceId);
}

ScanStatus PollCharacteristic(Characteristic* characteristic, bool block) {
	ScanStatus res;
	unique_lock<mutex> lock(characteristicQueueLock);
	if (block && characteristicQueue.empty() && !characteristicScanFinished)
		if (QuittableWait(characteristicQueueSignal, lock))
			return ScanStatus::FINISHED;
	if (!characteristicQueue.empty()) {
		*characteristic = characteristicQueue.front();
		characteristicQueue.pop();
		res = ScanStatus::AVAILABLE;
	}
	else if (characteristicScanFinished)
		res = ScanStatus::FINISHED;
	else
		res = ScanStatus::PROCESSING;
	return res;
}






void Characteristic_ValueChanged(GattCharacteristic const& characteristic, GattValueChangedEventArgs args)
{
	BLEData data;
	wcscpy_s(data.characteristicUuid, sizeof(data.characteristicUuid) / sizeof(wchar_t), to_hstring(characteristic.Uuid()).c_str());
	wcscpy_s(data.serviceUuid, sizeof(data.serviceUuid) / sizeof(wchar_t), to_hstring(characteristic.Service().Uuid()).c_str());
	wcscpy_s(data.deviceId, sizeof(data.deviceId) / sizeof(wchar_t), characteristic.Service().Device().DeviceId().c_str());

	data.size = args.CharacteristicValue().Length();
	// IBuffer to array, copied from https://stackoverflow.com/a/55974934
	memcpy(data.buf, args.CharacteristicValue().data(), data.size);

	{
		lock_guard lock(quitLock);
		if (quitFlag)
			return;
	}
	{
		lock_guard queueGuard(dataQueueLock);
		dataQueue.push(data);
		dataQueueSignal.notify_one();
	}
}

fire_and_forget SubscribeCharacteristicAsync(wchar_t* deviceId,
                                             wchar_t* serviceId,
                                             wchar_t* characteristicId,
                                             bool* completionFlag)
{
    bool succeeded = false;

    try
    {
        auto characteristic = co_await retrieveCharacteristic(deviceId, serviceId, characteristicId);
        if (characteristic != nullptr)
        {
            auto status = co_await characteristic
                .WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::Notify);

            if (status != GattCommunicationStatus::Success)
            {
                saveError(L"%s:%d Error subscribing to characteristic %s (status %d)",
                          __WFILE__, __LINE__, characteristicId, status);
            }
            else
            {
                auto* subscription = new Subscription();
                subscription->characteristic = characteristic;
                subscription->revoker = characteristic.ValueChanged(auto_revoke, &Characteristic_ValueChanged);

                {
                    std::lock_guard guard(subscribeQueueLock);
                    subscriptions.push_back(subscription);
                }

                succeeded = true;
            }
        }
    }
    catch (hresult_error const& ex)
    {
        saveError(L"%s:%d SubscribeCharacteristicAsync catch: %s",
                  __WFILE__, __LINE__, ex.message().c_str());
    }

    if (completionFlag)
        *completionFlag = succeeded;

    subscribeQueueSignal.notify_one();
}

bool SubscribeCharacteristic(wchar_t* deviceId,
                             wchar_t* serviceId,
                             wchar_t* characteristicId,
                             bool /*block*/)
{
    try
    {
        auto characteristic = retrieveCharacteristic(deviceId, serviceId, characteristicId).get();
        if (characteristic == nullptr)
        {
            saveError(L"%s:%d Failed to resolve characteristic %s",
                      __WFILE__, __LINE__, characteristicId);
            return false;
        }

        auto status = characteristic
            .WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify)
            .get();

        if (status != GattCommunicationStatus::Success)
        {
            saveError(L"%s:%d Error subscribing to characteristic %s (status %d)",
                      __WFILE__, __LINE__, characteristicId, status);
            return false;
        }

        auto* subscription = new Subscription();
        subscription->characteristic = characteristic;
        subscription->revoker = characteristic.ValueChanged(auto_revoke, &Characteristic_ValueChanged);

        {
            std::lock_guard guard(subscribeQueueLock);
            subscriptions.push_back(subscription);
        }

        clearError();
        return true;
    }
    catch (winrt::hresult_error const& ex)
    {
        saveError(L"%s:%d SubscribeCharacteristic catch: %s",
                  __WFILE__, __LINE__, ex.message().c_str());
    }
    catch (...)
    {
        saveError(L"%s:%d SubscribeCharacteristic catch: unknown exception.",
                  __WFILE__, __LINE__);
    }
    return false;
}

bool UnsubscribeCharacteristic(wchar_t* deviceId,
                               wchar_t* serviceId,
                               wchar_t* characteristicId)
{
    Subscription* target = nullptr;
    guid serviceGuid = make_guid(serviceId);
    guid characteristicGuid = make_guid(characteristicId);

    try
    {
        {
            std::lock_guard guard(subscribeQueueLock);

            for (auto it = subscriptions.begin(); it != subscriptions.end(); ++it)
            {
                Subscription* sub = *it;
                if (!sub || !sub->characteristic)
                    continue;

                auto svc = sub->characteristic.Service();
                if (!svc)
                    continue;

                auto dev = svc.Device();
                if (dev.DeviceId() != deviceId)
                    continue;

                if (svc.Uuid() != serviceGuid)
                    continue;

                if (sub->characteristic.Uuid() != characteristicGuid)
                    continue;

                target = sub;
                subscriptions.erase(it);
                break;
            }
        }

        if (!target)
        {
            saveError(L"%s:%d UnsubscribeCharacteristic: subscription not found.",
                      __WFILE__, __LINE__);
            return false;
        }

        auto status = target->characteristic
            .WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::None)
            .get();

        if (status != GattCommunicationStatus::Success)
        {
            saveError(L"%s:%d UnsubscribeCharacteristic: CCCD write failed (status %d).",
                      __WFILE__, __LINE__, static_cast<int>(status));

            std::lock_guard guard(subscribeQueueLock);
            subscriptions.push_back(target);
            return false;
        }

        target->revoker.revoke();
        delete target;

        clearError();
        return true;
    }
    catch (hresult_error const& ex)
    {
        saveError(L"%s:%d UnsubscribeCharacteristic catch: %s",
                  __WFILE__, __LINE__, ex.message().c_str());
    }
    catch (...)
    {
        saveError(L"%s:%d UnsubscribeCharacteristic catch: unknown exception.",
                  __WFILE__, __LINE__);
    }

    if (target)
    {
        std::lock_guard guard(subscribeQueueLock);
        subscriptions.push_back(target);
    }

    return false;
}


bool PollData(BLEData* data, bool block) {
	unique_lock<mutex> lock(dataQueueLock);
	if (block && dataQueue.empty())
		if (QuittableWait(dataQueueSignal, lock))
			return false;
	if (!dataQueue.empty()) {
		*data = dataQueue.front();
		dataQueue.pop();
		return true;
	}
	return false;
}

fire_and_forget SendDataAsync(BLEData data, condition_variable* signal, bool* result) {
	try {
		auto characteristic = co_await retrieveCharacteristic(data.deviceId, data.serviceUuid, data.characteristicUuid);
		if (characteristic != nullptr) {
			// create IBuffer from data
			DataWriter writer;
			writer.WriteBytes(array_view<uint8_t const> (data.buf, data.buf + data.size));
			IBuffer buffer = writer.DetachBuffer();
			auto status = co_await characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse);
			if (status != GattCommunicationStatus::Success)
				saveError(L"%s:%d Error writing value to characteristic with uuid %s", __WFILE__, __LINE__, data.characteristicUuid);
			else if (result != 0)
				*result = true;
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d SendDataAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	if (signal != 0)
		signal->notify_one();
}
bool SendData(BLEData* data, bool block) {
	mutex _mutex;
	unique_lock<mutex> lock(_mutex);
	condition_variable signal;
	bool result = false;
	// copy data to stack so that caller can free its memory in non-blocking mode
	SendDataAsync(*data, block ? &signal : 0, block ? &result : 0);
	if (block)
		signal.wait(lock);

	return result;
}

void Quit()
{
    {
        lock_guard lock(quitLock);
        quitFlag = true;
    }
    StopDeviceScan();
    deviceQueueSignal.notify_one();
    {
        lock_guard lock(deviceQueueLock);
        deviceQueue = {};
    }
    serviceQueueSignal.notify_one();
    {
        lock_guard lock(serviceQueueLock);
        serviceQueue = {};
    }
    characteristicQueueSignal.notify_one();
    {
        lock_guard lock(characteristicQueueLock);
        characteristicQueue = {};
    }
    subscribeQueueSignal.notify_one();
    {
        lock_guard lock(subscribeQueueLock);
        for (auto subscription : subscriptions)
            subscription->revoker.revoke();
        subscriptions = {};
    }
    dataQueueSignal.notify_one();
    {
        lock_guard lock(dataQueueLock);
        dataQueue = {};
    }
    {
        lock_guard lock(connectionQueueLock);
        { queue<ConnectionUpdate> empty; std::swap(connectionQueue, empty); }
    }
    for (auto& device : cache)
    {
        if (device.second.statusSubscribed)
        {
            device.second.statusChangedRevoker.revoke();
            device.second.statusSubscribed = false;
        }
        EnqueueConnectionUpdate(device.second.device.DeviceId().c_str(),
                                BluetoothConnectionStatus::Disconnected);
        device.second.device.Close();
        for (auto& service : device.second.services)
        {
            service.second.service.Close();
        }
    }
    cache.clear();
}

void GetError(ErrorMessage* buf) {
	lock_guard error_lock(errorLock);
	wcscpy_s(buf->msg, last_error);
}
