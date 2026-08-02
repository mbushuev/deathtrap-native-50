#include <cstdint>
#include <cstdio>

#include "deathtrap_music_route.h"

int main() {
  using namespace deathtrap_music;
  static_assert(kRedbookTrackCount == 16u);
  static_assert(kMp3TrackCount == 15u);
  static_assert(!RouteCdTrackToSteamMp3(0u).valid);
  static_assert(!RouteCdTrackToSteamMp3(1u).valid);
  static_assert(RouteCdTrackToSteamMp3(2u).mp3_index == 0u);
  static_assert(RouteCdTrackToSteamMp3(9u).mp3_index == 7u);
  static_assert(RouteCdTrackToSteamMp3(16u).mp3_index == 14u);
  static_assert(!RouteCdTrackToSteamMp3(17u).valid);

  for (uint32_t track = kFirstCdAudioTrack;
       track <= kLastCdAudioTrack; ++track) {
    const TrackRoute route = RouteCdTrackToSteamMp3(track);
    if (!route.valid || route.cd_track != track ||
        route.mp3_index >= kMp3TrackCount) {
      std::fprintf(stderr, "invalid route for CD track %u\n", track);
      return 1;
    }
  }
  std::puts("Deathtrap music track routing test passed");
  return 0;
}
