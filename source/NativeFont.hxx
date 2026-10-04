#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Read the installed game's TIM font. No redistributed game artwork or GDI fonts.
namespace NativeFont
{
    struct Bitmap
    {
        int width = 0, height = 0, colors = 0, palettes = 0;
        int textPalette = 0;
        std::vector<uint8_t> indices;
        std::vector<uint32_t> palette;
        bool Load(std::span<const uint8_t> data)
        {
            const auto word = [&](size_t p) -> unsigned { return data[p] | (unsigned(data[p + 1]) << 8); };
            const auto dword = [&](size_t p) -> unsigned { return word(p) | (word(p + 2) << 16); };
            if (data.size() < 32 || dword(0) != 0x10) return false;
            const auto mode = dword(4);
            if (!(mode & 8) || (mode & 7) > 1) return false;
            const size_t clut = dword(8);
            const int count = int(word(16)), rows = int(word(18));
            if (clut < 12 || clut > data.size() - 20 || count < 16 || rows < 1
                || size_t(count) * rows * 2 > clut - 12) return false;
            const size_t image = 8 + clut;
            if (data.size() - image < 12) return false;
            const size_t length = dword(image);
            const int w = int(word(image + 8)) * ((mode & 7) ? 2 : 4), h = int(word(image + 10));
            if (length < 12 || length > data.size() - image || w < 144 || w > 2048 || h < 24 || h > 1024
                || size_t(w) * h / ((mode & 7) ? 1 : 2) > length - 12) return false;
            std::vector<uint32_t> colorsOut(size_t(count) * rows);
            for (size_t i = 0; i < colorsOut.size(); ++i)
            {
                const auto c = word(20 + 2 * i);
                // PSX STP is not transparency: only colour zero is transparent.
                colorsOut[i] = c ? 0xFF000000 | ((c & 31) * 255 / 31 << 16)
                    | (((c >> 5) & 31) * 255 / 31 << 8) | ((c >> 10) & 31) * 255 / 31 : 0;
            }
            std::vector<uint8_t> pixels(size_t(w) * h);
            for (size_t i = 0; i < pixels.size(); ++i)
            {
                const auto b = data[image + 12 + ((mode & 7) ? i : i / 2)];
                pixels[i] = (mode & 7) ? b : ((i & 1) ? b >> 4 : b & 15);
                if (pixels[i] >= count) return false;
            }
            width = w; height = h; colors = count; palettes = rows;
            palette = std::move(colorsOut); indices = std::move(pixels);
            return true;
        }
        bool File(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file ? file.tellg() : std::streampos(-1);
            if (size < 32 || size > 1024 * 1024) return false;
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            file.seekg(0);
            return bool(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) && Load(bytes);
        }
        uint32_t Pixel(int x, int y, bool green) const
        {
            if (x < 0 || y < 0 || x >= width || y >= height) return 0;
            const auto index = indices[y * width + x];
            // RE3's packed Japanese font uses CLUT 490/492 to select one
            // glyph layer. Western fonts use CLUT 480/482 instead.
            const int row = textPalette + (green && palettes >= 5 ? (colors == 32 ? 2 : 1) : 0);
            auto c = palette[row * colors + index];
            if (green && row == 0) c &= 0xFF00FF00; // RE1's green modulation.
            return c;
        }
    };
    struct Glyph { int atlas, x, y, width, height, advance; };
    struct Image { int width = 0, height = 0; std::vector<uint32_t> pixels; };

    class Font
    {
        std::vector<Bitmap> atlases;
        std::unordered_map<wchar_t, Glyph> glyphs;
        int cellWidth = 8, cellHeight = 14;
        bool japanese = false;

        void Row(std::wstring_view text, int atlas, int x, int y, int w, int h)
        {
            for (const auto c : text)
            {
                if (c != L' ') glyphs.try_emplace(c, Glyph{atlas, x, y, w, h, w});
                x += w;
            }
        }
        static wchar_t Normalize(wchar_t c)
        {
            if (c == L'\u2019' || c == L'\u2018') return L'\'';
            if (c == L'\u201c' || c == L'\u201d') return L'"';
            if (c == L'\u2013' || c == L'\u2014') return L'-';
            if (c == L'\u3000' || c == L'\u00a0') return L' ';
            if (c == L'「') return L'(';
            if (c == L'」') return L')';
            if (c == L'・') return L'_';
            if (c >= 0xFF01 && c <= 0xFF5E) return c - 0xFEE0;
            return c;
        }
    public:
        bool Ready() const { return !atlases.empty(); }
        bool Load(std::span<const uint8_t> bytes, int game, bool jp)
        {
            Bitmap bitmap;
            if (!bitmap.Load(bytes)) return false;
            return Set(std::move(bitmap), game, jp);
        }
        bool Set(Bitmap bitmap, int game, bool jp)
        {
            if (game == 3 && bitmap.colors == 32 && bitmap.palettes >= 13)
            {
                // Western TIMs reserve the Japanese CLUT rows but leave the
                // highlight row empty. Select packed glyph palettes only
                // when the asset actually supplies them.
                const auto first = bitmap.palette.begin() + 12 * bitmap.colors;
                // Four-bit glyph pixels address only the first 16 colours;
                // the other half of each CLUT belongs to unrelated artwork.
                if (std::any_of(first, first + 16, [](uint32_t c) { return c != 0; }))
                    bitmap.textPalette = 10;
            }
            atlases.clear(); glyphs.clear(); atlases.push_back(std::move(bitmap)); japanese = jp;
            cellHeight = 14;
            cellWidth = game == 1 && jp ? 14 : 8;
            // Western releases store 8-pixel letters; Japanese stores 14-pixel
            // letters. English RE2/3 ship the Japanese atlas with Latin rows.
            if (game != 1)
            {
                bool wide = false;
                for (int y = 28; y < 42; ++y)
                    for (int x = 200; x < 252; ++x) wide |= atlases[0].indices[y * atlases[0].width + x] != 0;
                cellWidth = wide ? 14 : 8;
            }
            // Explicit game encoding (A=29, a=61), shared by all three engines.
            const auto add = [&](wchar_t c, int code)
            { glyphs[c] = {0, (code % 18) * cellWidth, (code / 18 + 2) * cellHeight, cellWidth, cellHeight, cellWidth}; };
            add(L' ', 0); add(L'.', 1); add(L'\u25b6', 2); add(L'>', 2);
            for (int i = 0; i < 10; ++i) add(wchar_t(L'0' + i), 12 + i);
            for (int i = 0; i < 26; ++i) { add(wchar_t(L'A' + i), 29 + i); add(wchar_t(L'a' + i), 61 + i); }
            for (const auto [c, code] : std::array<std::pair<wchar_t, int>, 16>{ {{L':',22},{L';',23},{L',',24},{L'"',25},
                {L'!',26},{L'?',27},{L'[',55},{L'/',56},{L']',57},{L'\'',58},{L'-',59},{L'_',60},
                {L'(',5},{L')',6},{L'+',125},{L'=',126}} }) add(c, code);
            if (!jp && cellWidth == 8)
            {
                constexpr std::wstring_view accents = L"ÄäÖöÜüßÀàÂâÈèÉéÊêÏïÎîÔôÙùÛûÇç";
                for (int i = 0; i < int(accents.size()); ++i) add(accents[i], 87 + i);
                constexpr std::wstring_view extended = L"ÑñËë°ªÁáÍíÓóÚú¿¡ÌìÒò$*";
                for (int i = 0; i < int(extended.size()); ++i) add(extended[i], 132 + i);
            }
            else
            {
                constexpr std::wstring_view kana = L"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんがぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽぁぃぅぇぉゃゅょっアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポァィゥェォャュョッヴー";
                for (int i = 0; i < int(kana.size()); ++i) add(kana[i], 87 + i);
                add(L'。', 1); add(L'、', 24);
                if (jp)
                {
                    // The Japanese atlas reserves these cells for controller
                    // symbols. Use its ASCII punctuation rather than a marker.
                    glyphs[L'。'] = {0, (L'.' - 32) * 8, 0, 8, 8, 8};
                }
            }
            return true;
        }
        bool Files(int game, std::string_view language)
        {
            wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr, executable, MAX_PATH);
            const auto folder = std::filesystem::path(executable).parent_path();
            const bool jp = language == "japanese";
            const auto code = language == "french" ? L"f" : language == "german" ? L"g" : language == "italian" ? L"i"
                : language == "spanish" ? L"s" : jp ? L"a" : L"u";
            const auto path = game == 1 ? folder / (jp ? L"JPN/Data/FONT.TIM" : language == "french" ? L"FRA/Data/fontus.tim"
                : language == "german" ? L"GER/Data/fontus.tim" : L"USA/Data/fontus.tim")
                : folder / (std::wstring(L"Common/Dat") + code) / (jp ? L"font0.tim" : L"font0p.tim");
            Bitmap bitmap;
            return bitmap.File(path) && Set(std::move(bitmap), game, jp);
        }
        bool Supports(std::wstring_view text) const
        {
            for (auto c : text) if (!glyphs.contains(Normalize(c))) return false;
            return true;
        }
        Image Render(std::wstring_view text, bool green) const
        {
            Image image;
            if (!Ready() || text.empty()) return image;
            std::vector<Glyph> letters;
            for (auto c : text)
            {
                c = Normalize(c);
                auto found = glyphs.find(c);
                auto glyph = found != glyphs.end() ? found->second : glyphs.at(L'?');
                // Space is a control code in the game's text stream. Its atlas
                // cell can contain a debug marker; never sample it as a glyph.
                if (c == L' ') glyph = {0, 0, 0, 0, cellHeight, cellWidth};
                // Preserve the native glyph's aspect ratio and one-pixel spacing.
                // Strip unused horizontal padding for the engine's proportional text.
                if (cellWidth > 8 && c != L' ')
                {
                    int left = glyph.width, right = -1;
                    for (int y = 0; y < glyph.height; ++y) for (int x = 0; x < glyph.width; ++x)
                        if (atlases[glyph.atlas].Pixel(glyph.x + x, glyph.y + y, false) >> 24)
                        { left = std::min(left, x); right = std::max(right, x); }
                    if (right >= left) { glyph.x += left; glyph.width = right - left + 1; glyph.advance = glyph.width + 1; }
                }
                image.width += glyph.advance; image.height = std::max(image.height, glyph.height);
                letters.push_back(glyph);
            }
            image.pixels.resize(size_t(image.width) * image.height);
            int offset = 0;
            for (const auto& glyph : letters)
            {
                for (int y = 0; y < glyph.height; ++y) for (int x = 0; x < glyph.width; ++x)
                    image.pixels[y * image.width + offset + x] = atlases[glyph.atlas].Pixel(glyph.x + x, glyph.y + y, green);
                offset += glyph.advance;
            }
            return image;
        }
    };
}
