#pragma once

#include <cstdint>

namespace deathtrap_music {

// The retail CD contains data as track 1 and fifteen audio tracks as 2..16.
// The Steam depot stores those audio tracks as Sounds/0.mp3..Sounds/14.mp3.
constexpr uint32_t kFirstCdAudioTrack = 2u;
constexpr uint32_t kLastCdAudioTrack = 16u;
constexpr uint32_t kRedbookTrackCount = 16u;
constexpr uint32_t kMp3TrackCount = 15u;

struct TrackRoute {
  uint32_t cd_track = 0;
  uint32_t mp3_index = 0;
  bool valid = false;
};

constexpr TrackRoute RouteCdTrackToSteamMp3(uint32_t cd_track) {
  if (cd_track < kFirstCdAudioTrack || cd_track > kLastCdAudioTrack) {
    return {};
  }
  return {cd_track, cd_track - kFirstCdAudioTrack, true};
}

}  // namespace deathtrap_music
