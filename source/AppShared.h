#pragma once

// Paths and constants shared by the App*.cpp files. Internal to App.
namespace app_shared
{
constexpr const char* kConfigDir = "sdmc:/config/EhViewerSwitch";
constexpr const char* kCookieFile = "sdmc:/config/EhViewerSwitch/cookies.ini";
constexpr const char* kCaFile = "sdmc:/config/EhViewerSwitch/cacert.pem";
constexpr const char* kLibraryRoot = "sdmc:/manga/EhViewerSwitch";
constexpr const char* kSettingsFile = "sdmc:/config/EhViewerSwitch/settings.ini";
constexpr const char* kHistoryFile = "sdmc:/config/EhViewerSwitch/history.json";
constexpr const char* kSubscriptionsFile = "sdmc:/config/EhViewerSwitch/subscriptions.json";
// A touch that moves less than this (panel pixels) is a tap.
constexpr int kTapSlop = 14;
// Holding a touch this long without moving is a long press.
constexpr unsigned kLongPressMilliseconds = 500;
// toplist.php tl values: yesterday, past month, past year, all time.
constexpr int kToplistIds[] = {15, 13, 12, 11};
constexpr const char* kToplistNames[] = {"昨日", "本月", "本年", "总榜"};
}
