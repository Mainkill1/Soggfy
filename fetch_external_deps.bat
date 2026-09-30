rmdir /s /q external
mkdir external
curl https://cef-builds.spotifycdn.com/cef_binary_146.0.10+g8219561+chromium-146.0.7680.179_windows64_minimal.tar.bz2 --output external/cef_bin.tar.bz2
7z x "external/cef_bin.tar.bz2" -so | 7z x -aoa -si -ttar "-xr!*.dll" "-xr!cef_sandbox.lib" -o"external/"
move /y "external\cef_binary_*" "external\cef_bin"
