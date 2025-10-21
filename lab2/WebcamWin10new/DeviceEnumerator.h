#pragma once
#include "MFHelpers.h"
#include <vector>

class DeviceEnumerator {
public:
    DeviceEnumerator();
    ~DeviceEnumerator();
    std::vector<DeviceInfo> ListDevices();
private:
    bool initialized_ = false;
};
