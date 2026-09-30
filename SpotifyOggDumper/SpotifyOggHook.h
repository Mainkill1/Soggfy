#pragma once

#include "OggCapture.h"

#include <Windows.h>
#include <functional>

void InstallSpotifyOggHook(HMODULE spotifyModule, std::function<void(const OggPageView&)> pageSink);
void StopSpotifyOggHook();
