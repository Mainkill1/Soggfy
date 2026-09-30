#include "../SpotifyOggDumper/CefAbi.h"

#include <cassert>

int main()
{
    static_assert(sizeof(CefAbi::Base) == 40);
    static_assert(sizeof(CefAbi::Object<19>) == 192);
    static_assert(sizeof(CefAbi::Object<4>) == 72);
    static_assert(sizeof(CefAbi::Object<21>) == 208);
    static_assert(sizeof(CefAbi::Object<26>) == 248);

    assert(CefAbi::IsSupportedVersion(146, 0, 10));
    assert(!CefAbi::IsSupportedVersion(145, 0, 10));
    assert(!CefAbi::IsSupportedVersion(146, 1, 10));
    assert(!CefAbi::IsSupportedVersion(146, 0, 11));
}
