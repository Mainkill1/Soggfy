# Current Spotify Repair Scope

The current-client repair is split into two independent pull requests so the loader and the injected code can be reviewed separately.

## Pull request 1: Ogg core compatibility

This extends the existing Ogg BOS fix and changes no injector or installer files.

- Build `SpotifyOggDumper.dll` for Windows x64.
- Support the validated Spotify 1.3.1.234 `Spotify.dll` build.
- Replace the obsolete x86 decoder hook and playback pointer traversal with the validated x64 Ogg page callback.
- Verify the Spotify DLL hash and parser prologue before installing the hook.
- Copy validated pages into a bounded memory queue so Spotify's parser thread performs no file or metadata work.
- Assemble concurrent streams by their parser sync context.
- Preserve a real Vorbis identification BOS page and ignore only non-Vorbis prefix pages.
- Reject discontinuous, incomplete, oversized, or ambiguous streams.
- Associate completed streams with playback IDs using cached player events and duration matching.
- Replace private CEF memory traversal with wrappers on public CEF browser creation exports.
- Support the installed CEF 146.0.10 ABI with structure-size checks.
- Keep the existing Soggfy metadata, tagging, configuration, and output pipeline.
- Add no FLAC capture or unrelated Floggfy features.

The core PR is validated with an existing early loader. Validation covers complete tracks, consecutive tracks, skip, seek, repeat, metadata association, Ogg page order, EOS, duration, and independent decoder playback.

## Pull request 2: x64 early injector

This is based directly on `master` and changes no capture, CEF, metadata, Sprinkles, or installer files.

- Build `Injector.exe` for Windows x64.
- Launch Spotify suspended when early injection is requested.
- Resolve the remote `LoadLibraryW` address using its export offset and the target process module base.
- Write the absolute DLL path into the target and verify the remote load thread result.
- Resume Spotify only after the DLL loads successfully.
- Release remote memory and process/thread handles on every path.
- Terminate a newly launched suspended process when injection fails.
- Retain attach mode with a warning that full browser integration requires early launch.

The injector PR is validated separately against missing-DLL, timeout, remote-load failure, and successful launch paths.

## Publication rules

Captured audio, media names, media hashes, account data, binaries, and validation logs are excluded from both branches.
