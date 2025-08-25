/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "../../include/platform/NativeAPI.h"
#include <thread>
#include <chrono>
#include <map>
#include <functional>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32) && !defined(__MINGW32__)
#include <windows.h>
#include <xinput.h>
#pragma comment(lib, "xinput.lib")
#endif

namespace Quanta {

// Static member definitions
Platform NativeAPI::current_platform_ = Platform::UNKNOWN;
DeviceInfo NativeAPI::device_info_;
bool NativeAPI::initialized_ = false;
std::map<int, std::function<void(const GeolocationInfo&)>> NativeAPI::geolocation_watchers_;
int NativeAPI::next_watch_id_ = 1;

Platform NativeAPI::detect_platform() {
    if (current_platform_ != Platform::UNKNOWN) {
        return current_platform_;
    }

#ifdef _WIN32
    current_platform_ = Platform::WINDOWS;
#elif defined(__ANDROID__)
    current_platform_ = Platform::ANDROID;
#elif defined(__APPLE__)
    #ifdef TARGET_OS_IOS
    current_platform_ = Platform::IOS;
    #else
    current_platform_ = Platform::MACOS;
    #endif
#elif defined(__linux__)
    current_platform_ = Platform::LINUX;
#else
    current_platform_ = Platform::UNKNOWN;
#endif

    return current_platform_;
}

bool NativeAPI::initialize_platform_apis() {
    if (initialized_) return true;
    
    current_platform_ = detect_platform();
    initialized_ = true;
    return true;
}

void NativeAPI::shutdown_platform_apis() {
    initialized_ = false;
    geolocation_watchers_.clear();
}

DeviceInfo NativeAPI::get_device_info() {
    if (!device_info_.platform_name.empty()) {
        return device_info_;
    }
    
    switch (current_platform_) {
        case Platform::WINDOWS:
            device_info_.platform_name = "Windows";
            device_info_.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Quanta/1.0";
            device_info_.language = "en-US";
            device_info_.languages = {"en-US", "en"};
            device_info_.hardware_concurrency = std::thread::hardware_concurrency();
            device_info_.supported_capabilities = 
                static_cast<uint32_t>(DeviceCapability::NOTIFICATION_SYSTEM) |
                static_cast<uint32_t>(DeviceCapability::BATTERY_INFO) |
                static_cast<uint32_t>(DeviceCapability::CLIPBOARD) |
                static_cast<uint32_t>(DeviceCapability::FILE_SYSTEM) |
                static_cast<uint32_t>(DeviceCapability::GAMEPAD) |
                static_cast<uint32_t>(DeviceCapability::SCREEN_INFO) |
                static_cast<uint32_t>(DeviceCapability::SPEECH_SYNTHESIS) |
                static_cast<uint32_t>(DeviceCapability::NETWORK_INFO);
            break;
            
        default:
            device_info_.platform_name = "Unknown";
            device_info_.user_agent = "Quanta/1.0";
            device_info_.language = "en-US";
            device_info_.languages = {"en-US", "en"};
            device_info_.hardware_concurrency = 1;
            device_info_.supported_capabilities = 0;
            break;
    }
    
    device_info_.online = true;
    return device_info_;
}

uint32_t NativeAPI::get_device_capabilities() {
    return get_device_info().supported_capabilities;
}

// Simplified implementations that just return default values for now
BatteryInfo NativeAPI::get_battery_info() {
    BatteryInfo info;
    
#if defined(_WIN32) && !defined(__MINGW32__)
    // Windows battery detection using GetSystemPowerStatus
    std::cout << " Getting Windows battery information..." << std::endl;
    
    SYSTEM_POWER_STATUS powerStatus;
    if (GetSystemPowerStatus(&powerStatus)) {
        info.supported = true;
        
        // Battery charging status
        if (powerStatus.ACLineStatus == 1) {
            info.charging = true;
            std::cout << " AC Power: Connected (charging)" << std::endl;
        } else if (powerStatus.ACLineStatus == 0) {
            info.charging = false;
            std::cout << " AC Power: Disconnected (battery)" << std::endl;
        } else {
            info.charging = false; // Unknown AC status
            std::cout << " AC Power: Unknown status" << std::endl;
        }
        
        // Battery level (percentage)
        if (powerStatus.BatteryLifePercent != 255) { // 255 means unknown
            info.level = powerStatus.BatteryLifePercent / 100.0;
            std::cout << " Battery Level: " << powerStatus.BatteryLifePercent << "%" << std::endl;
        } else {
            info.level = 1.0; // Unknown, assume full
            std::cout << " Battery Level: Unknown" << std::endl;
        }
        
        // Battery life time (remaining time when on battery)
        if (powerStatus.BatteryLifeTime != 0xFFFFFFFF) { // -1 means unknown
            info.discharging_time = powerStatus.BatteryLifeTime; // seconds
            std::cout << "⏱ Remaining Time: " << (powerStatus.BatteryLifeTime / 60) << " minutes" << std::endl;
        } else {
            info.discharging_time = 0; // Unknown
            std::cout << " Remaining Time: Unknown" << std::endl;
        }
        
        // Charging time estimation (Windows doesn't provide this directly)
        if (info.charging && info.level < 1.0) {
            // Estimate based on current level (very rough approximation)
            double remaining_capacity = 1.0 - info.level;
            info.charging_time = remaining_capacity * 3600; // Assume 1 hour per 100%
            std::cout << " Estimated Charge Time: " << (info.charging_time / 60) << " minutes" << std::endl;
        } else {
            info.charging_time = 0; // Not charging or already full
        }
        
        std::cout << " Battery API supported on this Windows device" << std::endl;
        
    } else {
        std::cout << " Failed to get battery information from Windows" << std::endl;
        DWORD error = GetLastError();
        std::cout << "   - Windows Error Code: " << error << std::endl;
        
        // Check if this is a desktop without battery
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_DEVICE_NOT_AVAILABLE) {
            std::cout << "   - This appears to be a desktop system without battery" << std::endl;
            info.supported = false;
        } else {
            info.supported = false;
        }
    }
#elif defined(_WIN32) && defined(__MINGW32__)
    // MSYS2/MinGW build - Windows battery API not available  
    std::cout << " Windows battery API disabled in MSYS2/MinGW build" << std::endl;
    info.supported = false;
#else
    // Linux/Unix battery detection
    std::cout << " Checking Linux battery support..." << std::endl;
    
    // Check for battery in /sys/class/power_supply/
    std::ifstream battery_present("/sys/class/power_supply/BAT0/present");
    if (battery_present.is_open()) {
        std::string present;
        battery_present >> present;
        battery_present.close();
        
        if (present == "1") {
            info.supported = true;
            std::cout << " Battery detected in /sys/class/power_supply/BAT0" << std::endl;
            
            // Read capacity
            std::ifstream capacity_file("/sys/class/power_supply/BAT0/capacity");
            if (capacity_file.is_open()) {
                int capacity;
                capacity_file >> capacity;
                info.level = capacity / 100.0;
                capacity_file.close();
                std::cout << " Battery Level: " << capacity << "%" << std::endl;
            }
            
            // Read charging status
            std::ifstream status_file("/sys/class/power_supply/BAT0/status");
            if (status_file.is_open()) {
                std::string status;
                status_file >> status;
                info.charging = (status == "Charging");
                status_file.close();
                std::cout << " Charging Status: " << status << std::endl;
            }
            
        } else {
            info.supported = false;
            std::cout << " No battery present" << std::endl;
        }
    } else {
        info.supported = false;
        std::cout << " Battery information not available on this Linux system" << std::endl;
    }
#endif
    
    return info;
}

bool NativeAPI::vibrate(const std::vector<long>& pattern) {
    (void)pattern; // Suppress unused parameter warning
    return false;
}

bool NativeAPI::cancel_vibration() {
    return false;
}

bool NativeAPI::show_notification(const std::string& title, const std::string& body, 
                                 const std::string& icon, const std::string& tag) {
    (void)title; (void)body; (void)icon; (void)tag; // Suppress unused parameter warnings
    return false;
}

bool NativeAPI::request_notification_permission() {
    return true;
}

std::string NativeAPI::get_notification_permission() {
    return "granted";
}

bool NativeAPI::close_notification(const std::string& tag) {
    (void)tag; // Suppress unused parameter warning
    return true;
}

GeolocationInfo NativeAPI::get_current_position(bool high_accuracy) {
    (void)high_accuracy; // Suppress unused parameter warning
    GeolocationInfo info;
    info.supported = false;
    return info;
}

int NativeAPI::watch_position(std::function<void(const GeolocationInfo&)> success_callback,
                             std::function<void(const std::string&)> error_callback,
                             bool high_accuracy) {
    (void)success_callback; (void)error_callback; (void)high_accuracy; // Suppress unused parameter warnings
    return -1;
}

bool NativeAPI::clear_watch_position(int watch_id) {
    (void)watch_id; // Suppress unused parameter warning
    return false;
}

ScreenInfo NativeAPI::get_screen_info() {
    ScreenInfo info;
    
#if defined(_WIN32) && !defined(__MINGW32__)
    // Get real Windows screen information
    std::cout << " Getting Windows screen information..." << std::endl;
    
    // Get primary display dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int workAreaWidth = GetSystemMetrics(SM_CXFULLSCREEN);
    int workAreaHeight = GetSystemMetrics(SM_CYFULLSCREEN);
    
    if (screenWidth > 0 && screenHeight > 0) {
        info.width = screenWidth;
        info.height = screenHeight;
        info.available_width = workAreaWidth;
        info.available_height = workAreaHeight;
        
        // Get real color depth
        HDC hdc = GetDC(NULL);
        if (hdc) {
            info.color_depth = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
            info.pixel_depth = info.color_depth;
            
            // Get DPI for device pixel ratio
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            info.device_pixel_ratio = dpiX / 96.0f; // 96 DPI is standard
            
            ReleaseDC(NULL, hdc);
        } else {
            info.color_depth = 24; // Reasonable fallback
            info.pixel_depth = 24;
            info.device_pixel_ratio = 1.0f;
        }
        
        // Determine orientation
        info.orientation_type = (screenWidth > screenHeight) ? "landscape" : "portrait";
        
        std::cout << " Screen: " << info.width << "x" << info.height 
                  << ", " << info.color_depth << "-bit, " << info.orientation_type << std::endl;
    } else {
        std::cout << " Failed to get screen information from Windows API" << std::endl;
        throw std::runtime_error("Screen information not available");
    }
#elif defined(_WIN32) && defined(__MINGW32__)
    // MSYS2/MinGW build - Windows screen API not available
    std::cout << " Windows screen API disabled in MSYS2/MinGW build" << std::endl;
    throw std::runtime_error("Screen information not available in MSYS2/MinGW build");
#else
    // Linux/Unix screen detection
    std::cout << " Attempting Linux screen detection..." << std::endl;
    
    // Try to get display info from environment or X11
    const char* display = getenv("DISPLAY");
    if (display) {
        std::cout << " X11 screen detection not implemented" << std::endl;
        throw std::runtime_error("Screen information not available on this platform");
    } else {
        std::cout << " No display environment found" << std::endl;
        throw std::runtime_error("Screen information not available (no display)");
    }
#endif
    
    return info;
}

bool NativeAPI::lock_screen_orientation(const std::string& orientation) {
    (void)orientation; // Suppress unused parameter warning
    return false;
}

bool NativeAPI::unlock_screen_orientation() {
    return true;
}

std::string NativeAPI::read_clipboard_text() {
    return "";
}

bool NativeAPI::write_clipboard_text(const std::string& text) {
    (void)text; // Suppress unused parameter warning
    return false;
}

bool NativeAPI::speak_text(const std::string& text, const std::string& lang,
                          float rate, float pitch, float volume) {
    (void)text; (void)lang; (void)rate; (void)pitch; (void)volume; // Suppress unused parameter warnings
    return false;
}

std::vector<GamepadState> NativeAPI::get_gamepads() {
    std::vector<GamepadState> gamepads;
    
#if defined(_WIN32) && !defined(__MINGW32__)
    std::cout << " Scanning for Windows XInput controllers..." << std::endl;
    
    // Check all 4 possible XInput controller slots
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(XINPUT_STATE));
        
        // Get the state of the controller
        DWORD result = XInputGetState(i, &state);
        
        if (result == ERROR_SUCCESS) {
            // Controller is connected
            GamepadState gamepad;
            gamepad.index = static_cast<int>(i);
            gamepad.connected = true;
            gamepad.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            gamepad.mapping = "standard";
            gamepad.has_vibration = true;
            
            // Get controller capabilities to determine device name
            XINPUT_CAPABILITIES caps;
            ZeroMemory(&caps, sizeof(XINPUT_CAPABILITIES));
            if (XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) {
                if (caps.Type == XINPUT_DEVTYPE_GAMEPAD) {
                    gamepad.id = "Xbox Controller (XInput STANDARD GAMEPAD)";
                } else {
                    gamepad.id = "Unknown XInput Device";
                }
            } else {
                gamepad.id = "Xbox Controller " + std::to_string(i);
            }
            
            // Map XInput buttons to standard gamepad layout
            // Buttons array: [A, B, X, Y, LB, RB, LT, RT, Back, Start, LS, RS, DPad-Up, DPad-Down, DPad-Left, DPad-Right]
            gamepad.buttons_pressed.resize(16, false);
            gamepad.buttons_touched.resize(16, false);
            gamepad.buttons_values.resize(16, 0.0);
            
            WORD buttons = state.Gamepad.wButtons;
            
            // Face buttons
            gamepad.buttons_pressed[0] = (buttons & XINPUT_GAMEPAD_A) != 0;           // A
            gamepad.buttons_pressed[1] = (buttons & XINPUT_GAMEPAD_B) != 0;           // B  
            gamepad.buttons_pressed[2] = (buttons & XINPUT_GAMEPAD_X) != 0;           // X
            gamepad.buttons_pressed[3] = (buttons & XINPUT_GAMEPAD_Y) != 0;           // Y
            
            // Shoulder buttons
            gamepad.buttons_pressed[4] = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;  // LB
            gamepad.buttons_pressed[5] = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0; // RB
            
            // Triggers (analog, converted to digital)
            float leftTrigger = state.Gamepad.bLeftTrigger / 255.0f;
            float rightTrigger = state.Gamepad.bRightTrigger / 255.0f;
            gamepad.buttons_pressed[6] = leftTrigger > 0.1f;                          // LT
            gamepad.buttons_pressed[7] = rightTrigger > 0.1f;                         // RT
            gamepad.buttons_values[6] = leftTrigger;
            gamepad.buttons_values[7] = rightTrigger;
            
            // Menu buttons
            gamepad.buttons_pressed[8] = (buttons & XINPUT_GAMEPAD_BACK) != 0;        // Back/Select
            gamepad.buttons_pressed[9] = (buttons & XINPUT_GAMEPAD_START) != 0;       // Start/Menu
            
            // Stick buttons
            gamepad.buttons_pressed[10] = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;  // Left stick click
            gamepad.buttons_pressed[11] = (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0; // Right stick click
            
            // D-Pad
            gamepad.buttons_pressed[12] = (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;     // D-Pad Up
            gamepad.buttons_pressed[13] = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;   // D-Pad Down
            gamepad.buttons_pressed[14] = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;   // D-Pad Left
            gamepad.buttons_pressed[15] = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;  // D-Pad Right
            
            // Set button values (0.0 or 1.0 for digital buttons)
            for (size_t j = 0; j < gamepad.buttons_pressed.size(); j++) {
                if (j != 6 && j != 7) { // Skip triggers (already set above)
                    gamepad.buttons_values[j] = gamepad.buttons_pressed[j] ? 1.0 : 0.0;
                }
                gamepad.buttons_touched[j] = gamepad.buttons_pressed[j];
            }
            
            // Analog sticks - convert from -32768 to 32767 range to -1.0 to 1.0
            gamepad.axes.resize(4);
            
            // Apply deadzone to prevent drift
            auto applyDeadzone = [](SHORT value, SHORT deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) -> float {
                if (abs(value) < deadzone) return 0.0f;
                float normalized = (value - (value > 0 ? deadzone : -deadzone)) / (32767.0f - deadzone);
                return std::max(-1.0f, std::min(1.0f, normalized));
            };
            
            gamepad.axes[0] = applyDeadzone(state.Gamepad.sThumbLX);  // Left stick X
            gamepad.axes[1] = -applyDeadzone(state.Gamepad.sThumbLY); // Left stick Y (inverted)
            gamepad.axes[2] = applyDeadzone(state.Gamepad.sThumbRX);  // Right stick X  
            gamepad.axes[3] = -applyDeadzone(state.Gamepad.sThumbRY); // Right stick Y (inverted)
            
            gamepads.push_back(gamepad);
            
            std::cout << " Controller " << i << ": " << gamepad.id << std::endl;
            std::cout << "   - Buttons pressed: ";
            bool hasPressed = false;
            const char* buttonNames[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "LS", "RS", "Up", "Down", "Left", "Right"};
            for (size_t j = 0; j < gamepad.buttons_pressed.size(); j++) {
                if (gamepad.buttons_pressed[j]) {
                    if (hasPressed) std::cout << ", ";
                    std::cout << buttonNames[j];
                    hasPressed = true;
                }
            }
            if (!hasPressed) std::cout << "none";
            std::cout << std::endl;
            
            std::cout << "   - Left stick: (" << gamepad.axes[0] << ", " << gamepad.axes[1] << ")" << std::endl;
            std::cout << "   - Right stick: (" << gamepad.axes[2] << ", " << gamepad.axes[3] << ")" << std::endl;
            std::cout << "   - Triggers: L=" << gamepad.buttons_values[6] << ", R=" << gamepad.buttons_values[7] << std::endl;
        }
    }
    
    if (gamepads.empty()) {
        std::cout << " No XInput controllers connected" << std::endl;
        std::cout << "   - Connect an Xbox 360/One/Series controller to test" << std::endl;
        std::cout << "   - Make sure controller is properly paired/plugged in" << std::endl;
    } else {
        std::cout << " Found " << gamepads.size() << " connected controller(s)" << std::endl;
    }
#elif defined(_WIN32) && defined(__MINGW32__)
    // MSYS2/MinGW build - XInput not available  
    std::cout << " XInput gamepad support disabled in MSYS2/MinGW build" << std::endl;
    std::cout << "   - Use build-native-windows.bat for full XInput support" << std::endl;
#else
    // Linux/macOS gamepad support would go here
    std::cout << " Gamepad API not implemented for this platform" << std::endl;
#endif
    
    return gamepads;
}

std::string NativeAPI::get_connection_type() {
    return "wifi";
}

bool NativeAPI::is_online() {
    return true;
}

std::vector<std::string> NativeAPI::enumerate_media_devices() {
    return {};
}

NetworkInfo NativeAPI::get_network_info() {
    NetworkInfo info;
    
#ifdef _WIN32
    // Windows network information using WMI and native APIs
    std::cout << " Getting Windows network information..." << std::endl;
    
    // Check if online using simple ping test
    std::string pingCommand = "ping -n 1 8.8.8.8 >nul 2>&1";
    int pingResult = system(pingCommand.c_str());
    info.online = (pingResult == 0);
    
    if (info.online) {
        // Get network adapter information using PowerShell with better speed detection
        std::string networkCommand = "powershell -Command \"& {"
            "Get-NetAdapter | Where-Object {$_.Status -eq 'Up'} | Select-Object -First 1 | ForEach-Object {"
                "Write-Host ('TYPE:' + $_.MediaType);"
                "Write-Host ('LINKSPEED:' + $_.LinkSpeed);"
                "Write-Host ('NAME:' + $_.Name);"
            "};"
            "Get-NetConnectionProfile | Where-Object {$_.NetworkConnectivityLevel -eq 'Internet'} | Select-Object -First 1 | ForEach-Object {"
                "Write-Host ('PROFILE:' + $_.Name);"
            "};"
            "Get-NetIPAddress | Where-Object {$_.AddressFamily -eq 'IPv4' -and $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*'} | Select-Object -First 1 | ForEach-Object {"
                "Write-Host ('IP:' + $_.IPAddress);"
            "};"
        "}\"";
        
        FILE* pipe = popen(networkCommand.c_str(), "r");
        if (pipe) {
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line(buffer);
                line.erase(line.find_last_not_of(" \n\r\t") + 1); // trim
                
                if (line.find("TYPE:") == 0) {
                    std::string mediaType = line.substr(5);
                    if (mediaType.find("802.11") != std::string::npos || mediaType.find("Wireless") != std::string::npos) {
                        info.connection_type = "wifi";
                    } else if (mediaType.find("Ethernet") != std::string::npos || mediaType.find("802.3") != std::string::npos) {
                        info.connection_type = "ethernet";
                    } else {
                        info.connection_type = "other";
                    }
                } else if (line.find("LINKSPEED:") == 0) {
                    std::string speedStr = line.substr(10);
                    // Parse different speed formats: "1 Gbps", "100 Mbps", "10000000000" (bps)
                    if (speedStr.find("Gbps") != std::string::npos) {
                        try {
                            double speed = std::stod(speedStr) * 1000; // Convert Gbps to Mbps
                            info.downlink = speed;
                            info.uplink = speed * 0.1; // Estimate upload as 10% of download
                        } catch (...) {}
                    } else if (speedStr.find("Mbps") != std::string::npos) {
                        try {
                            double speed = std::stod(speedStr);
                            info.downlink = speed;
                            info.uplink = speed * 0.1;
                        } catch (...) {}
                    } else {
                        // Try to parse as raw bps number
                        try {
                            double speed = std::stod(speedStr) / 1000000.0; // Convert bps to Mbps
                            info.downlink = speed;
                            info.uplink = speed * 0.1;
                        } catch (...) {
                            std::cout << " Could not parse network speed from: " << speedStr << std::endl;
                            info.downlink = 0.0; // Unknown speed
                            info.uplink = 0.0;   // Unknown speed
                        }
                    }
                } else if (line.find("NAME:") == 0) {
                    info.ssid = line.substr(5);
                } else if (line.find("PROFILE:") == 0) {
                    info.ssid = line.substr(8);
                } else if (line.find("IP:") == 0) {
                    info.ip_address = line.substr(3);
                }
            }
            pclose(pipe);
        }
        
        // Estimate effective type based on speed
        if (info.downlink >= 1000) {
            info.effective_type = "5g";
        } else if (info.downlink >= 100) {
            info.effective_type = "4g";
        } else if (info.downlink >= 10) {
            info.effective_type = "3g";
        } else if (info.downlink >= 1) {
            info.effective_type = "2g";
        } else {
            info.effective_type = "slow-2g";
        }
        
        // Estimate RTT based on connection type
        if (info.connection_type == "ethernet") {
            info.rtt = 5.0; // Very low latency
        } else if (info.connection_type == "wifi") {
            info.rtt = 20.0; // Low latency
        } else {
            info.rtt = 100.0; // Higher latency for other connections
        }
        
        // Default values for Windows
        info.signal_strength = (info.connection_type == "wifi") ? 85 : 100;
        info.supported = true;
        
        std::cout << " Network info detected:" << std::endl;
        std::cout << "   - Type: " << info.connection_type << std::endl;
        std::cout << "   - Speed: " << info.downlink << " Mbps" << std::endl;
        std::cout << "   - IP: " << info.ip_address << std::endl;
        std::cout << "   - Online: " << (info.online ? "Yes" : "No") << std::endl;
    } else {
        std::cout << " System appears to be offline" << std::endl;
        info.connection_type = "none";
        info.effective_type = "none";
        info.downlink = 0.0;
        info.uplink = 0.0;
        info.rtt = 0.0;
        info.supported = true;
    }
#else
    // Linux/Unix network information
    std::ifstream routeFile("/proc/net/route");
    if (routeFile.is_open()) {
        std::string line;
        bool hasDefaultRoute = false;
        while (std::getline(routeFile, line)) {
            if (line.find("00000000") != std::string::npos) {
                hasDefaultRoute = true;
                break;
            }
        }
        info.online = hasDefaultRoute;
        routeFile.close();
    }
    
    if (info.online) {
        std::cout << " Linux network detection not fully implemented" << std::endl;
        // Try to detect connection type from network interfaces
        std::ifstream wireless("/proc/net/wireless");
        if (wireless.is_open()) {
            std::string line;
            std::getline(wireless, line); // Skip header
            if (std::getline(wireless, line) && !line.empty()) {
                info.connection_type = "wifi";
                std::cout << " Detected WiFi connection" << std::endl;
            }
            wireless.close();
        }
        
        if (info.connection_type.empty()) {
            info.connection_type = "ethernet"; // Assume wired if no wireless
            std::cout << " Assuming ethernet connection" << std::endl;
        }
        
        // Set unknown values instead of fake ones
        info.effective_type = "unknown";
        info.downlink = 0.0;  // Speed unknown
        info.uplink = 0.0;    // Speed unknown  
        info.rtt = 0.0;       // RTT unknown
        info.supported = true;
        
        std::cout << " Network speed detection not implemented for Linux" << std::endl;
    }
#endif
    
    return info;
}

DeviceOrientationInfo NativeAPI::get_device_orientation() {
    DeviceOrientationInfo info;
    
#ifdef _WIN32
    // Windows sensor detection using WMI
    std::cout << " Getting Windows device orientation..." << std::endl;
    
    // Check for orientation sensor using PowerShell
    std::string sensorCommand = "powershell -Command \"& {"
        "Get-WmiObject -Namespace 'root\\wmi' -Class 'MSAcpi_ThermalZoneTemperature' -ErrorAction SilentlyContinue | Measure-Object | Select-Object -ExpandProperty Count;"
        "Get-CimInstance -ClassName 'Win32_PnPEntity' | Where-Object {$_.Name -like '*accelerometer*' -or $_.Name -like '*gyroscope*' -or $_.Name -like '*orientation*'} | Measure-Object | Select-Object -ExpandProperty Count;"
    "}\"";
    
    FILE* pipe = popen(sensorCommand.c_str(), "r");
    bool hasSensors = false;
    
    if (pipe) {
        char buffer[128];
        int sensorCount = 0;
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            try {
                sensorCount = std::stoi(buffer);
                hasSensors = (sensorCount > 0);
            } catch (...) {}
        }
        pclose(pipe);
    }
    
    if (hasSensors) {
        // Simulate sensor data for now (real implementation would use Windows.Devices.Sensors)
        std::cout << " Orientation sensors detected" << std::endl;
        
        // Get current time for timestamp
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
        // For desktop, simulate stable horizontal position with slight variations
        info.alpha = 0.0;   // No compass rotation for desktop
        info.beta = 0.0;    // Flat (no front-to-back tilt)  
        info.gamma = 0.0;   // Flat (no side-to-side tilt)
        info.absolute = false;
        info.timestamp = now;
        info.supported = true;
        
        std::cout << " Orientation: α=" << info.alpha << "°, β=" << info.beta << "°, γ=" << info.gamma << "°" << std::endl;
    } else {
        std::cout << " No orientation sensors detected on this device" << std::endl;
        info.supported = false;
    }
#else
    // Linux/Unix orientation detection
    // Check for /sys/class/input/ devices or /dev/input/event* for accelerometer
    std::ifstream inputDevices("/proc/bus/input/devices");
    if (inputDevices.is_open()) {
        std::string line;
        while (std::getline(inputDevices, line)) {
            if (line.find("accelerometer") != std::string::npos ||
                line.find("gyroscope") != std::string::npos) {
                info.supported = true;
                break;
            }
        }
        inputDevices.close();
    }
    
    if (info.supported) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        info.timestamp = now;
    }
#endif
    
    return info;
}

DeviceMotionInfo NativeAPI::get_device_motion() {
    DeviceMotionInfo info;
    
#ifdef _WIN32
    std::cout << " Getting Windows device motion data..." << std::endl;
    
    // Check for motion sensors (accelerometer + gyroscope)
    DeviceOrientationInfo orientationInfo = get_device_orientation();
    info.supported = orientationInfo.supported;
    
    if (info.supported) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
        // For desktop, simulate minimal motion (stable device)
        info.acceleration_x = 0.0;
        info.acceleration_y = 0.0;
        info.acceleration_z = 0.0;
        
        // Include gravity (desktop assumed horizontal, gravity downward)
        info.acceleration_including_gravity_x = 0.0;
        info.acceleration_including_gravity_y = 0.0;
        info.acceleration_including_gravity_z = 9.81; // Standard gravity
        
        // No rotation for stable desktop
        info.rotation_rate_alpha = 0.0;
        info.rotation_rate_beta = 0.0;
        info.rotation_rate_gamma = 0.0;
        
        info.interval = 16.0; // ~60 FPS
        info.timestamp = now;
        
        std::cout << " Motion: accel=(" << info.acceleration_x << "," << info.acceleration_y << "," << info.acceleration_z << ") m/s²" << std::endl;
    }
#endif
    
    return info;
}

bool NativeAPI::has_orientation_sensor() {
    DeviceOrientationInfo info = get_device_orientation();
    return info.supported;
}

bool NativeAPI::has_motion_sensor() {
    DeviceMotionInfo info = get_device_motion();
    return info.supported;
}

// Platform-specific initialization functions (empty for now)
void NativeAPI::initialize_windows_apis() {}
void NativeAPI::initialize_linux_apis() {}
void NativeAPI::initialize_macos_apis() {}
void NativeAPI::initialize_android_apis() {}
void NativeAPI::initialize_ios_apis() {}

// Placeholder implementations for APIs not yet implemented
std::vector<uint8_t> NativeAPI::read_clipboard_data(const std::string& mime_type) { (void)mime_type; return {}; }
bool NativeAPI::write_clipboard_data(const std::string& mime_type, const std::vector<uint8_t>& data) { (void)mime_type; (void)data; return false; }
std::vector<uint8_t> NativeAPI::read_file(const std::string& path) { (void)path; return {}; }
bool NativeAPI::write_file(const std::string& path, const std::vector<uint8_t>& data) { (void)path; (void)data; return false; }
bool NativeAPI::file_exists(const std::string& path) { (void)path; return false; }
bool NativeAPI::create_directory(const std::string& path) { (void)path; return false; }
bool NativeAPI::delete_file(const std::string& path) { (void)path; return false; }
std::vector<std::string> NativeAPI::list_directory(const std::string& path) { (void)path; return {}; }
bool NativeAPI::stop_speaking() { return false; }
bool NativeAPI::pause_speaking() { return false; }
bool NativeAPI::resume_speaking() { return false; }
std::vector<std::string> NativeAPI::get_available_voices() { return {}; }
bool NativeAPI::start_speech_recognition(const std::string& lang) { (void)lang; return false; }
bool NativeAPI::stop_speech_recognition() { return false; }
bool NativeAPI::abort_speech_recognition() { return false; }
bool NativeAPI::set_speech_recognition_callback(std::function<void(const std::string&, bool)> callback) { (void)callback; return false; }
bool NativeAPI::gamepad_vibrate(int gamepad_index, double strong_magnitude, double weak_magnitude, long duration) {
#if defined(_WIN32) && !defined(__MINGW32__)
    std::cout << " Vibrating controller " << gamepad_index 
              << " (Strong: " << strong_magnitude << ", Weak: " << weak_magnitude 
              << ", Duration: " << duration << "ms)" << std::endl;
    
    // Validate gamepad index
    if (gamepad_index < 0 || gamepad_index >= XUSER_MAX_COUNT) {
        std::cout << " Invalid gamepad index: " << gamepad_index << std::endl;
        return false;
    }
    
    // Check if controller is connected
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    if (XInputGetState(gamepad_index, &state) != ERROR_SUCCESS) {
        std::cout << " Controller " << gamepad_index << " not connected" << std::endl;
        return false;
    }
    
    // Clamp magnitudes to valid range (0.0 - 1.0)
    strong_magnitude = std::max(0.0, std::min(1.0, strong_magnitude));
    weak_magnitude = std::max(0.0, std::min(1.0, weak_magnitude));
    
    // Convert to XInput format (0-65535)
    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = static_cast<WORD>(strong_magnitude * 65535);  // Low-frequency rumble
    vibration.wRightMotorSpeed = static_cast<WORD>(weak_magnitude * 65535);   // High-frequency rumble
    
    // Set vibration
    DWORD result = XInputSetState(gamepad_index, &vibration);
    
    if (result == ERROR_SUCCESS) {
        std::cout << " Vibration started on controller " << gamepad_index << std::endl;
        
        // If duration is specified, schedule vibration stop
        if (duration > 0) {
            // Create a thread to stop vibration after duration
            std::thread([gamepad_index, duration]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(duration));
                
                // Stop vibration
                XINPUT_VIBRATION stop_vibration;
                stop_vibration.wLeftMotorSpeed = 0;
                stop_vibration.wRightMotorSpeed = 0;
                XInputSetState(gamepad_index, &stop_vibration);
                
                std::cout << " Vibration stopped on controller " << gamepad_index << " after " << duration << "ms" << std::endl;
            }).detach();
        }
        
        return true;
    } else {
        std::cout << " Failed to set vibration on controller " << gamepad_index 
                  << " (Error: " << result << ")" << std::endl;
        return false;
    }
#elif defined(_WIN32) && defined(__MINGW32__)
    // MSYS2/MinGW build - XInput vibration not available
    std::cout << " XInput gamepad vibration disabled in MSYS2/MinGW build" << std::endl;
    (void)gamepad_index; (void)strong_magnitude; (void)weak_magnitude; (void)duration;
    return false;
#else
    (void)gamepad_index; (void)strong_magnitude; (void)weak_magnitude; (void)duration;
    std::cout << " Gamepad vibration not supported on this platform" << std::endl;
    return false;
#endif
}
bool NativeAPI::register_gamepad_callback(std::function<void(const GamepadState&, bool)> callback) { (void)callback; return false; }
double NativeAPI::get_download_speed() { 
    NetworkInfo info = get_network_info();
    return info.downlink;
}
double NativeAPI::get_upload_speed() { 
    NetworkInfo info = get_network_info();
    return info.uplink;
}
bool NativeAPI::is_metered_connection() { 
    NetworkInfo info = get_network_info();
    return info.metered;
}
bool NativeAPI::register_network_change_callback(std::function<void(const NetworkInfo&)> callback) { (void)callback; return false; }
bool NativeAPI::start_device_orientation(std::function<void(const DeviceOrientationInfo&)> callback) { (void)callback; return false; }
bool NativeAPI::stop_device_orientation() { return false; }
bool NativeAPI::start_device_motion(std::function<void(const DeviceMotionInfo&)> callback) { (void)callback; return false; }
bool NativeAPI::stop_device_motion() { return false; }
bool NativeAPI::request_camera_permission() { return false; }
bool NativeAPI::request_microphone_permission() { return false; }
bool NativeAPI::has_camera() { return false; }
bool NativeAPI::has_microphone() { return false; }
bool NativeAPI::register_battery_change_callback(std::function<void(const BatteryInfo&)> callback) { (void)callback; return false; }

} // namespace Quanta