// smoke_overlay_style_unit: the overlay scale, theme palette, and embedded font bytes
// (suyu_cmd/overlay/overlay_style.h). argv[1] is the source root holding dist/fonts.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "suyu_cmd/overlay/overlay_style.h"

using namespace SuyuCmd;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr float kWindowAlphas[] = {0.94f, 0.5f, 1.0f};

constexpr bool IsThemeRgb(const Rgba& color, float lighten_share) {
    const Rgba expected = ThemeColor(color.a, lighten_share);
    return color == expected;
}

// Negative control: bar and title surfaces are opaque.
constexpr ThemePalette OpaqueBars(float window_bg_alpha) {
    ThemePalette palette = MakeThemePalette(window_bg_alpha);
    palette.menu_bar.a = 1.0f;
    palette.title_bg.a = 1.0f;
    return palette;
}

// Negative control: the alpha is a constant instead of the window's.
constexpr ThemePalette FixedAlpha(float) {
    return MakeThemePalette(0.94f);
}

// Negative control: red and blue swapped.
constexpr ThemePalette SwappedChannels(float window_bg_alpha) {
    ThemePalette palette = MakeThemePalette(window_bg_alpha);
    palette.button = {palette.button.b, palette.button.g, palette.button.r, 1.0f};
    return palette;
}

// The palette rules, shared by the real maker and the controls. False on a breach.
template <class MakePalette>
bool PaletteRulesHold(MakePalette make_palette) {
    for (const float window_alpha : kWindowAlphas) {
        const ThemePalette p = make_palette(window_alpha);
        const bool translucent = p.menu_bar.a == window_alpha && p.title_bg.a == window_alpha &&
                                 p.title_bg_active.a == window_alpha &&
                                 p.title_bg_collapsed.a == window_alpha;
        const bool opaque_controls = p.button.a == 1.0f && p.button_hovered.a == 1.0f &&
                                     p.button_active.a == 1.0f && p.combo_frame.a == 1.0f &&
                                     p.combo_frame_hovered.a == 1.0f &&
                                     p.combo_frame_active.a == 1.0f;
        const bool theme_rgb = IsThemeRgb(p.menu_bar, 0.0f) && IsThemeRgb(p.title_bg, 0.0f) &&
                               IsThemeRgb(p.button, 0.0f) && IsThemeRgb(p.combo_frame, 0.0f) &&
                               IsThemeRgb(p.button_hovered, kHoveredLightenShare) &&
                               IsThemeRgb(p.button_active, kActiveLightenShare);
        if (!translucent || !opaque_controls || !theme_rgb) {
            return false;
        }
    }
    return true;
}

int ThemeIs46478B() {
    constexpr Rgba theme = ThemeColor(1.0f);
    CHECK(theme.r == 70.0f / 255.0f && theme.g == 71.0f / 255.0f && theme.b == 139.0f / 255.0f);
    CHECK(PaletteRulesHold(MakeThemePalette));
    CHECK(!PaletteRulesHold(OpaqueBars));
    CHECK(!PaletteRulesHold(FixedAlpha));
    CHECK(!PaletteRulesHold(SwappedChannels));
    return 0;
}

int HoverAndPressAreLighter() {
    const ThemePalette p = MakeThemePalette(0.94f);
    CHECK(p.button_hovered.r > p.button.r && p.button_active.r > p.button_hovered.r);
    CHECK(p.button_hovered.b > p.button.b && p.button_active.b > p.button_hovered.b);
    CHECK(p.button_active.b <= 1.0f);
    return 0;
}

int ScaleIs125Percent() {
    CHECK(Scaled(100.0f) == 125.0f);
    CHECK(kBaseFontSizePx * kUiScaleFactor == 16.25f);
    return 0;
}

// The embedded bytes against a reference; the controls feed a corrupted copy.
bool BytesMatch(const std::uint64_t* words, std::size_t size_bytes,
                const std::vector<unsigned char>& reference) {
    if (size_bytes != reference.size()) {
        return false;
    }
    return std::memcmp(words, reference.data(), reference.size()) == 0;
}

int EmbeddedFontMatchesTheTtf(const std::string& source_root, const char* relative_path,
                              const std::uint64_t* words, std::size_t size_bytes,
                              std::size_t min_size_bytes) {
    std::ifstream file(source_root + "/dist/fonts/" + relative_path, std::ios::binary);
    CHECK(file.good());
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
    CHECK(ttf.size() > min_size_bytes);
    CHECK(ttf[0] == 0x00 && ttf[1] == 0x01 && ttf[2] == 0x00 && ttf[3] == 0x00);
    CHECK(BytesMatch(words, size_bytes, ttf));

    std::vector<unsigned char> flipped = ttf;
    flipped[flipped.size() / 2] ^= 0x01;
    CHECK(!BytesMatch(words, size_bytes, flipped));
    std::vector<unsigned char> truncated(ttf.begin(), ttf.end() - 1);
    CHECK(!BytesMatch(words, size_bytes, truncated));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: smoke_overlay_style_unit <source root>\n");
        return 2;
    }
    CHECK(ThemeIs46478B() == 0);
    CHECK(HoverAndPressAreLighter() == 0);
    CHECK(ScaleIs125Percent() == 0);
    CHECK(EmbeddedFontMatchesTheTtf(argv[1], "Roboto/Roboto-VariableFont.ttf",
                                    aevora_overlay_font_latin_words,
                                    aevora_overlay_font_latin_size_bytes, 100000) == 0);
    CHECK(EmbeddedFontMatchesTheTtf(argv[1], "Roboto/Roboto-Bold.ttf",
                                    aevora_overlay_font_latin_bold_words,
                                    aevora_overlay_font_latin_bold_size_bytes, 100000) == 0);
    CHECK(EmbeddedFontMatchesTheTtf(argv[1], "BIZUDPGothic/BIZUDPGothic-Regular.ttf",
                                    aevora_overlay_font_japanese_words,
                                    aevora_overlay_font_japanese_size_bytes, 1000000) == 0);
    std::puts("All synthetic OVERLAY STYLE smoke checks passed.");
    return 0;
}
