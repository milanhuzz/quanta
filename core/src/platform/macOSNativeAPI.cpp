/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#if defined(__APPLE__) && !defined(TARGET_OS_IOS)

#include "../../include/platform/NativeAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/hid/IOHIDLib.h>
#include <CoreLocation/CoreLocation.h>
#include <UserNotifications/UserNotifications.h>
#include <AppKit/AppKit.h>
#include <AVFoundation/AVFoundation.h>
#include <Speech/Speech.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreWLAN/CoreWLAN.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <thread>
#include <chrono>
#include <iostream>

namespace Quanta {

BatteryInfo macOSNativeAPI::get_battery_info_macos() {
    BatteryInfo info;
    info.supported = false;
    
    // Get power source information
    CFTypeRef powerSourcesInfo = IOPSCopyPowerSourcesInfo();
    if (!powerSourcesInfo) return info;
    
    CFArrayRef powerSourcesList = IOPSCopyPowerSourcesList(powerSourcesInfo);
    if (!powerSourcesList) {
        CFRelease(powerSourcesInfo);
        return info;
    }
    
    CFIndex count = CFArrayGetCount(powerSourcesList);
    for (CFIndex i = 0; i < count; ++i) {
        CFTypeRef powerSource = CFArrayGetValueAtIndex(powerSourcesList, i);
        CFDictionaryRef description = IOPSGetPowerSourceDescription(powerSourcesInfo, powerSource);
        
        if (!description) continue;
        
        // Check if this is a battery
        CFStringRef type = (CFStringRef)CFDictionaryGetValue(description, CFSTR(kIOPSTypeKey));
        if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType))) continue;
        
        info.supported = true;
        
        // Get charging status
        CFStringRef powerSourceState = (CFStringRef)CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
        if (powerSourceState) {
            info.charging = CFEqual(powerSourceState, CFSTR(kIOPSACPowerValue));
        }
        
        // Get battery level
        CFNumberRef currentCapacity = (CFNumberRef)CFDictionaryGetValue(description, CFSTR(kIOPSCurrentCapacityKey));
        CFNumberRef maxCapacity = (CFNumberRef)CFDictionaryGetValue(description, CFSTR(kIOPSMaxCapacityKey));
        
        if (currentCapacity && maxCapacity) {
            int current, max;
            CFNumberGetValue(currentCapacity, kCFNumberIntType, &current);
            CFNumberGetValue(maxCapacity, kCFNumberIntType, &max);
            if (max > 0) {
                info.level = static_cast<double>(current) / static_cast<double>(max);
            }
        }
        
        // Get time remaining
        CFNumberRef timeToEmpty = (CFNumberRef)CFDictionaryGetValue(description, CFSTR(kIOPSTimeToEmptyKey));
        CFNumberRef timeToFullCharge = (CFNumberRef)CFDictionaryGetValue(description, CFSTR(kIOPSTimeToFullChargeKey));
        
        if (info.charging && timeToFullCharge) {
            int time;
            CFNumberGetValue(timeToFullCharge, kCFNumberIntType, &time);
            info.charging_time = static_cast<double>(time * 60); // Convert minutes to seconds
        } else if (!info.charging && timeToEmpty) {
            int time;
            CFNumberGetValue(timeToEmpty, kCFNumberIntType, &time);
            info.discharging_time = static_cast<double>(time * 60);
        }
        
        break; // Use first battery found
    }
    
    CFRelease(powerSourcesList);
    CFRelease(powerSourcesInfo);
    return info;
}

bool macOSNativeAPI::vibrate_macos(const std::vector<long>& pattern) {
    // macOS doesn't have system-wide vibration, but we can use sound/haptic feedback for MacBooks with Force Touch
    
    for (size_t i = 0; i < pattern.size(); i += 2) {
        if (i < pattern.size()) {
            long duration = pattern[i];
            if (duration > 0) {
                // Use AudioServicesPlaySystemSound for haptic feedback on supported devices
                AudioServicesPlaySystemSoundWithCompletion(kSystemSoundID_Vibrate, ^{
                    // Completion block
                });
                
                // Fallback to system beep
                NSBeep();
                
                // Wait for duration
                std::this_thread::sleep_for(std::chrono::milliseconds(duration));
            }
        }
        
        // Pause between vibrations
        if (i + 1 < pattern.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pattern[i + 1]));
        }
    }
    
    return true;
}

bool macOSNativeAPI::show_notification_macos(const std::string& title, const std::string& body, 
                                            const std::string& icon, const std::string& tag) {
    @autoreleasepool {
        // Use modern UserNotifications framework (macOS 10.14+)
        UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
        
        // Request permission first
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                completionHandler:^(BOOL granted, NSError * _Nullable error) {
            if (granted) {
                // Create notification content
                UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
                content.title = [NSString stringWithUTF8String:title.c_str()];
                content.body = [NSString stringWithUTF8String:body.c_str()];
                content.sound = [UNNotificationSound defaultSound];
                
                // Create request
                NSString* identifier = tag.empty() ? [[NSUUID UUID] UUIDString] : [NSString stringWithUTF8String:tag.c_str()];
                UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:identifier
                                                                                        content:content
                                                                                        trigger:nil];
                
                // Schedule notification
                [center addNotificationRequest:request withCompletionHandler:^(NSError * _Nullable error) {
                    if (error) {
                        NSLog(@"Notification error: %@", error.localizedDescription);
                    }
                }];
            }
        }];
    }
    
    return true;
}

GeolocationInfo macOSNativeAPI::get_position_macos() {
    GeolocationInfo info;
    info.supported = true;
    
    // This would require Core Location framework and proper authorization
    // For demonstration, return San Francisco coordinates
    info.latitude = 37.7749;
    info.longitude = -122.4194;
    info.accuracy = 1000.0;
    info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return info;
}

ScreenInfo macOSNativeAPI::get_screen_info_macos() {
    ScreenInfo info;
    
    @autoreleasepool {
        NSScreen* mainScreen = [NSScreen mainScreen];
        if (mainScreen) {
            NSRect frame = [mainScreen frame];
            NSRect visibleFrame = [mainScreen visibleFrame];
            
            info.width = static_cast<int>(frame.size.width);
            info.height = static_cast<int>(frame.size.height);
            info.available_width = static_cast<int>(visibleFrame.size.width);
            info.available_height = static_cast<int>(visibleFrame.size.height);
            
            // Get color depth
            NSWindowDepth depth = [mainScreen depth];
            info.color_depth = NSBitsPerPixelFromDepth(depth);
            info.pixel_depth = info.color_depth;
            
            // Get device pixel ratio (Retina scaling)
            info.device_pixel_ratio = static_cast<float>([mainScreen backingScaleFactor]);
            
            // macOS doesn't rotate like mobile devices
            info.orientation_angle = 0;
            info.orientation_type = info.width >= info.height ? "landscape-primary" : "portrait-primary";
        }
    }
    
    return info;
}

std::string macOSNativeAPI::read_clipboard_text_macos() {
    std::string result;
    
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSString* string = [pasteboard stringForType:NSPasteboardTypeString];
        
        if (string) {
            result = [string UTF8String];
        }
    }
    
    return result;
}

bool macOSNativeAPI::write_clipboard_text_macos(const std::string& text) {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        
        NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
        BOOL success = [pasteboard setString:nsText forType:NSPasteboardTypeString];
        
        return success == YES;
    }
}

bool macOSNativeAPI::speak_text_macos(const std::string& text, const std::string& lang, 
                                     float rate, float pitch, float volume) {
    @autoreleasepool {
        AVSpeechSynthesizer* synthesizer = [[AVSpeechSynthesizer alloc] init];
        
        AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:
                                       [NSString stringWithUTF8String:text.c_str()]];
        
        // Set voice parameters
        utterance.rate = rate * AVSpeechUtteranceDefaultSpeechRate;
        utterance.pitchMultiplier = pitch;
        utterance.volume = volume;
        
        if (!lang.empty()) {
            AVSpeechSynthesisVoice* voice = [AVSpeechSynthesisVoice voiceWithLanguage:
                                            [NSString stringWithUTF8String:lang.c_str()]];
            if (voice) {
                utterance.voice = voice;
            }
        }
        
        [synthesizer speakUtterance:utterance];
        
        return true;
    }
}

std::vector<GamepadState> macOSNativeAPI::get_gamepads_macos() {
    std::vector<GamepadState> gamepads;
    
    // Use IOHIDManager to enumerate game controllers
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) return gamepads;
    
    // Set matching criteria for game controllers
    CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, 
                                                                   &kCFTypeDictionaryKeyCallBacks, 
                                                                   &kCFTypeDictionaryValueCallBacks);
    
    // Match joysticks and gamepads
    CFNumberRef usagePage = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &(int){kHIDPage_GenericDesktop});
    CFNumberRef usage1 = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &(int){kHIDUsage_GD_Joystick});
    CFNumberRef usage2 = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &(int){kHIDUsage_GD_GamePad});
    
    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDDeviceUsagePageKey), usagePage);
    
    IOHIDManagerSetDeviceMatching(hidManager, matchingDict);
    IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    
    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    if (deviceSet) {
        CFIndex deviceCount = CFSetGetCount(deviceSet);
        
        if (deviceCount > 0) {
            IOHIDDeviceRef* devices = (IOHIDDeviceRef*)malloc(sizeof(IOHIDDeviceRef) * deviceCount);
            CFSetGetValues(deviceSet, (const void**)devices);
            
            for (CFIndex i = 0; i < deviceCount; ++i) {
                IOHIDDeviceRef device = devices[i];
                
                GamepadState pad;
                pad.index = static_cast<int>(i);
                pad.connected = true;
                pad.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                
                // Get device name
                CFStringRef productName = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
                if (productName) {
                    char name[256];
                    CFStringGetCString(productName, name, sizeof(name), kCFStringEncodingUTF8);
                    pad.id = std::string(name);
                } else {
                    pad.id = "macOS Game Controller " + std::to_string(i);
                }
                
                pad.mapping = "standard";
                pad.has_vibration = false; // Most controllers don't support vibration on macOS
                
                // Get elements (buttons, axes)
                CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
                if (elements) {
                    CFIndex elementCount = CFArrayGetCount(elements);
                    
                    for (CFIndex j = 0; j < elementCount; ++j) {
                        IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, j);
                        IOHIDElementType type = IOHIDElementGetType(element);
                        
                        if (type == kIOHIDElementTypeInput_Button) {
                            pad.buttons_pressed.push_back(false);
                            pad.buttons_touched.push_back(false);
                            pad.buttons_values.push_back(0.0);
                        } else if (type == kIOHIDElementTypeInput_Axis) {
                            pad.axes.push_back(0.0);
                        }
                    }
                    
                    CFRelease(elements);
                }
                
                gamepads.push_back(pad);
            }
            
            free(devices);
        }
        
        CFRelease(deviceSet);
    }
    
    CFRelease(usagePage);
    CFRelease(usage1);
    CFRelease(usage2);
    CFRelease(matchingDict);
    IOHIDManagerClose(hidManager, kIOHIDOptionsTypeNone);
    CFRelease(hidManager);
    
    return gamepads;
}

std::string macOSNativeAPI::get_connection_type_macos() {
    std::string connection_type = "unknown";
    
    // Use SystemConfiguration framework to check network status
    SCNetworkReachabilityRef reachability = SCNetworkReachabilityCreateWithName(kCFAllocatorDefault, "www.apple.com");
    if (reachability) {
        SCNetworkReachabilityFlags flags;
        if (SCNetworkReachabilityGetFlags(reachability, &flags)) {
            if (flags & kSCNetworkReachabilityFlagsReachable) {
                if (flags & kSCNetworkReachabilityFlagsIsWWAN) {
                    connection_type = "cellular";
                } else {
                    // Check if it's WiFi or Ethernet
                    // This is simplified - real implementation would check interface types
                    connection_type = "wifi"; // Default assumption for macOS
                }
            } else {
                connection_type = "none";
            }
        }
        
        CFRelease(reachability);
    }
    
    return connection_type;
}

std::vector<std::string> macOSNativeAPI::enumerate_media_devices_macos() {
    std::vector<std::string> devices;
    
    @autoreleasepool {
        // Enumerate audio input devices
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSArray<AVAudioSessionPortDescription*>* inputs = [session availableInputs];
        
        for (AVAudioSessionPortDescription* input in inputs) {
            std::string deviceName = std::string([[input portName] UTF8String]);
            devices.push_back("audioinput:" + deviceName);
        }
        
        // Enumerate video capture devices
        NSArray<AVCaptureDevice*>* videoDevices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
        for (AVCaptureDevice* device in videoDevices) {
            std::string deviceName = std::string([[device localizedName] UTF8String]);
            devices.push_back("videoinput:" + deviceName);
        }
        
        // Add default audio output
        devices.push_back("audiooutput:Default Audio Output");
    }
    
    return devices;
}

} // namespace Quanta

#endif // __APPLE__ && !TARGET_OS_IOS