#include "test_harness.h"
#include "gbaimp/view_aspect.h"

using namespace gbaimp;

int main() {
    AspectSettings s;
    CHECK_EQ(resolve_view_width(s), 240);                       // 3:2 default
    s.preset = AspectPreset::Wide16x10; CHECK_EQ(resolve_view_width(s), 256);
    s.preset = AspectPreset::Wide16x9;  CHECK_EQ(resolve_view_width(s), 284);
    s.preset = AspectPreset::Ultra21x9; CHECK_EQ(resolve_view_width(s), 373);
    s.preset = AspectPreset::Super32x9; CHECK_EQ(resolve_view_width(s), 569);

    s.max_view_width = 300;                                     // cap applies to every preset
    CHECK_EQ(resolve_view_width(s), 300);
    s.preset = AspectPreset::Wide16x9; CHECK_EQ(resolve_view_width(s), 284);
    s.max_view_width = 100;                                     // never below native
    CHECK_EQ(resolve_view_width(s), 240);
    s.max_view_width = 0;

    s.preset = AspectPreset::Custom; s.custom_w = 5; s.custom_h = 4;   // narrower than 3:2
    CHECK_EQ(resolve_view_width(s), 240);
    s.custom_w = 0; CHECK_EQ(resolve_view_width(s), 240);       // invalid
    s.custom_w = 48; s.custom_h = 9; CHECK_EQ(resolve_view_width(s), 640);   // capped by default

    s.preset = AspectPreset::Adaptive;
    CHECK_EQ(resolve_view_width(s, 1920, 1080), 284);
    CHECK_EQ(resolve_view_width(s, 3440, 1440), 382);
    CHECK_EQ(resolve_view_width(s, 1000, 1000), 240);
    CHECK_EQ(resolve_view_width(s, 0, 0), 240);

    int l = -1, r = -1;
    split_extra(373, &l, &r); CHECK_EQ(l, 66); CHECK_EQ(r, 67);
    split_extra(240, &l, &r); CHECK_EQ(l, 0); CHECK_EQ(r, 0);
    split_extra(100, &l, &r); CHECK_EQ(l, 0); CHECK_EQ(r, 0);

    CHECK_EQ(view_width_for_ratio(0.0), 240);
    CHECK_EQ(view_width_for_ratio(2.39), 382);
    CHECK_EQ(aspect_choices().size(), size_t(10));
    CHECK(std::string(aspect_label(AspectPreset::Ultra21x9)).find("21:9") != std::string::npos);

    {   // taller-than-native views
        AspectSettings v;
        auto size = [&](AspectPreset p, int w = 0, int h = 0) { v.preset = p; return resolve_view_size(v, w, h); };
        CHECK_EQ(size(AspectPreset::Native).width, 240);    CHECK_EQ(size(AspectPreset::Native).height, 160);
        CHECK_EQ(size(AspectPreset::Wide16x9).width, 284);  CHECK_EQ(size(AspectPreset::Wide16x9).height, 160);
        CHECK_EQ(size(AspectPreset::Standard4x3).width, 240);  CHECK_EQ(size(AspectPreset::Standard4x3).height, 180);
        CHECK_EQ(size(AspectPreset::Standard5x4).width, 240);  CHECK_EQ(size(AspectPreset::Standard5x4).height, 192);
        CHECK_EQ(size(AspectPreset::Console10x7).width, 320);  CHECK_EQ(size(AspectPreset::Console10x7).height, 224);
        // Adaptive: only a window narrower than 3:2 gets rows; a wider one never does.
        CHECK_EQ(size(AspectPreset::Adaptive, 1600, 1200).height, 180);
        CHECK_EQ(size(AspectPreset::Adaptive, 1600, 1200).width, 240);
        CHECK_EQ(size(AspectPreset::Adaptive, 1920, 1080).height, 160);
        CHECK_EQ(size(AspectPreset::Adaptive, 1920, 1080).width, 284);
        CHECK_EQ(size(AspectPreset::Adaptive, 1500, 1000).height, 160);   // exactly 3:2
        CHECK_EQ(size(AspectPreset::Adaptive, 0, 0).height, 160);
        CHECK_EQ(size(AspectPreset::Adaptive, 100, 1000).height, kDefaultMaxViewHeight);   // capped
        v.preset = AspectPreset::Custom; v.custom_w = 5; v.custom_h = 4;
        CHECK_EQ(resolve_view_size(v).height, 192);
        CHECK_EQ(resolve_view_width(v), 240);
        v.preset = AspectPreset::Custom; v.custom_w = 16; v.custom_h = 9;
        CHECK_EQ(resolve_view_size(v).width, 284); CHECK_EQ(resolve_view_size(v).height, 160);
        v.max_view_height = 170; v.preset = AspectPreset::Standard5x4;
        CHECK_EQ(resolve_view_size(v).height, 170);
        v.preset = AspectPreset::Console10x7; CHECK_EQ(resolve_view_size(v).height, 170);
        v.max_view_height = 100; v.preset = AspectPreset::Standard4x3;     // never below native
        CHECK_EQ(resolve_view_size(v).height, 160);
        v.max_view_height = 0; v.max_view_width = 300; v.preset = AspectPreset::Console10x7;
        CHECK_EQ(resolve_view_size(v).width, 300);
        CHECK_EQ(view_size_for_ratio(0.0).height, 160);
        CHECK_EQ(view_size_for_ratio(0.5).height, kDefaultMaxViewHeight);
        int t = -1, b = -1;
        split_extra_rows(224, &t, &b); CHECK_EQ(t, 32); CHECK_EQ(b, 32);
        split_extra_rows(181, &t, &b); CHECK_EQ(t, 10); CHECK_EQ(b, 11);
        split_extra_rows(160, &t, &b); CHECK_EQ(t, 0); CHECK_EQ(b, 0);
        split_extra_rows(100, &t, &b); CHECK_EQ(t, 0); CHECK_EQ(b, 0);
        split_extra_rows(224, &t, &b, 0.75); CHECK_EQ(t, 48); CHECK_EQ(b, 16);
        split_extra_rows(224, &t, &b, 2.0); CHECK_EQ(t, 64); CHECK_EQ(b, 0);
    }

    {   // launcher table agrees with the presets
        const LauncherAspectTable t = launcher_aspect_table();
        CHECK_EQ(t.count, 5);
        const AspectPreset order[] = {AspectPreset::Native, AspectPreset::Wide16x10, AspectPreset::Wide16x9,
                                      AspectPreset::Ultra21x9, AspectPreset::Super32x9};
        for (int i = 0; i < t.count; ++i) {
            AspectSettings p; p.preset = order[i];
            CHECK_EQ(int(t.widths[i]), resolve_view_width(p));
            CHECK_EQ(std::string(t.labels[i]), std::string(aspect_label(order[i])));
        }
    }

    {   // table limited to a game's validated maximum
        LauncherAspectTable t = launcher_aspect_table(284);
        CHECK_EQ(t.count, 3); CHECK_EQ(int(t.widths[2]), 284);
        t = launcher_aspect_table(320);
        CHECK_EQ(t.count, 4); CHECK_EQ(int(t.widths[3]), 320);
        CHECK(std::string(t.labels[3]).find("320") != std::string::npos);
        t = launcher_aspect_table(569); CHECK_EQ(t.count, 5);
        t = launcher_aspect_table(0); CHECK_EQ(t.count, 5);
        t = launcher_aspect_table(240); CHECK_EQ(t.count, 1); CHECK_EQ(int(t.widths[0]), 240);
        t = launcher_aspect_table(100); CHECK_EQ(t.count, 1);
    }

    AspectSettings c;
    CHECK(parse_custom_ratio("16:9", &c)); CHECK_EQ(c.custom_w, 16); CHECK_EQ(c.custom_h, 9);
    CHECK(parse_custom_ratio("2.39", &c));  CHECK_EQ(c.custom_w, 239); CHECK_EQ(c.custom_h, 100);
    c.custom_w = 7; c.custom_h = 7;
    CHECK(!parse_custom_ratio("banana", &c)); CHECK(!parse_custom_ratio("0:9", &c));
    CHECK(!parse_custom_ratio("1:9", &c)); CHECK(!parse_custom_ratio("", &c));
    CHECK(!parse_custom_ratio("9.5", &c));
    CHECK_EQ(c.custom_w, 7); CHECK_EQ(c.custom_h, 7);          // untouched on failure
    return testh::finish("view_aspect");
}
