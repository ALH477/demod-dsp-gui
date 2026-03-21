// Preset serialization round-trip tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/preset.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unistd.h>

using namespace demod;
using Catch::Approx;

static Preset make_test_preset() {
    Preset p;
    p.name = "test_preset";

    p.slots[0].dsp_path = "/path/to/dsp1.dsp";
    p.slots[0].bypassed = false;
    p.slots[0].wet_mix = 0.75f;
    p.slots[0].params["/synth/cutoff"] = 1200.0f;
    p.slots[0].params["/synth/resonance"] = 0.5f;

    p.slots[1].dsp_path = "/path/to/dsp2.so";
    p.slots[1].bypassed = true;
    p.slots[1].wet_mix = 0.0f;

    p.slots[3].dsp_path = "overdrive.dsp";
    p.slots[3].bypassed = false;
    p.slots[3].wet_mix = 1.0f;
    p.slots[3].params["/drive/gain"] = 3.5f;

    p.post_fx.scanlines = false;
    p.post_fx.scanline_intensity = 0.8f;
    p.post_fx.bloom = true;
    p.post_fx.bloom_intensity = 0.3f;
    p.post_fx.vignette = false;
    p.post_fx.barrel = true;

    return p;
}

static void check_preset_eq(const Preset& a, const Preset& b) {
    CHECK(a.name == b.name);
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        CHECK(a.slots[i].dsp_path == b.slots[i].dsp_path);
        CHECK(a.slots[i].bypassed == b.slots[i].bypassed);
        CHECK(a.slots[i].wet_mix == Approx(b.slots[i].wet_mix));
        CHECK(a.slots[i].params.size() == b.slots[i].params.size());
        for (const auto& [k, v] : a.slots[i].params) {
            CHECK(b.slots[i].params.count(k) == 1);
            if (b.slots[i].params.count(k))
                CHECK(b.slots[i].params.at(k) == Approx(v));
        }
    }
    CHECK(a.post_fx.scanlines == b.post_fx.scanlines);
    CHECK(a.post_fx.scanline_intensity == Approx(b.post_fx.scanline_intensity));
    CHECK(a.post_fx.bloom == b.post_fx.bloom);
    CHECK(a.post_fx.bloom_intensity == Approx(b.post_fx.bloom_intensity));
    CHECK(a.post_fx.vignette == b.post_fx.vignette);
    CHECK(a.post_fx.barrel == b.post_fx.barrel);
}

static std::string test_dir() {
    static std::string dir;
    if (dir.empty()) {
        dir = "/tmp/demod_test_presets_" + std::to_string(getpid());
        std::filesystem::create_directories(dir);
    }
    return dir;
}

TEST_CASE("Preset format names and extensions", "[preset]") {
    CHECK(std::string(preset_format_name(PresetFormat::JSON)) == "JSON");
    CHECK(std::string(preset_format_name(PresetFormat::KEY_VALUE_TEXT)) == "Key=Value");
    CHECK(std::string(preset_format_name(PresetFormat::BINARY_BLOB)) == "Binary");
    CHECK(std::string(preset_format_name(PresetFormat::REVERSE_TRIE_DB)) == "Trie DB");

    CHECK(std::string(preset_format_ext(PresetFormat::JSON)) == ".json");
    CHECK(std::string(preset_format_ext(PresetFormat::KEY_VALUE_TEXT)) == ".cfg");
    CHECK(std::string(preset_format_ext(PresetFormat::BINARY_BLOB)) == ".bin");
    CHECK(std::string(preset_format_ext(PresetFormat::REVERSE_TRIE_DB)) == ".rtdb");
}

TEST_CASE("Preset default values", "[preset]") {
    Preset p;
    CHECK(p.name.empty());
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        CHECK(p.slots[i].dsp_path.empty());
        CHECK(p.slots[i].bypassed == true);
        CHECK(p.slots[i].wet_mix == Approx(1.0f));
        CHECK(p.slots[i].params.empty());
    }
    CHECK(p.post_fx.scanlines == true);
    CHECK(p.post_fx.bloom == true);
    CHECK(p.post_fx.vignette == true);
    CHECK(p.post_fx.barrel == true);
}

#ifdef HAVE_NLOHMANN_JSON
TEST_CASE("JSON round-trip", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset original = make_test_preset();

    REQUIRE(mgr.save(original, PresetFormat::JSON));

    Preset loaded;
    REQUIRE(mgr.load("test_preset", loaded));
    check_preset_eq(original, loaded);
}
#endif

TEST_CASE("Key=Value text round-trip", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset original = make_test_preset();

    REQUIRE(mgr.save(original, PresetFormat::KEY_VALUE_TEXT));

    Preset loaded;
    REQUIRE(mgr.load("test_preset", loaded));
    check_preset_eq(original, loaded);
}

TEST_CASE("Binary blob round-trip", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset original = make_test_preset();

    REQUIRE(mgr.save(original, PresetFormat::BINARY_BLOB));

    Preset loaded;
    REQUIRE(mgr.load("test_preset", loaded));
    check_preset_eq(original, loaded);
}

TEST_CASE("Reverse trie DB round-trip", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset original = make_test_preset();

    REQUIRE(mgr.save(original, PresetFormat::REVERSE_TRIE_DB));

    Preset loaded;
    REQUIRE(mgr.load("test_preset", loaded));

    // Trie DB only stores float values, not string paths
    CHECK(loaded.name == original.name);
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        CHECK(loaded.slots[i].bypassed == original.slots[i].bypassed);
        CHECK(loaded.slots[i].wet_mix == Approx(original.slots[i].wet_mix));
        CHECK(loaded.slots[i].params.size() == original.slots[i].params.size());
        for (const auto& [k, v] : original.slots[i].params) {
            CHECK(loaded.slots[i].params.count(k) == 1);
            if (loaded.slots[i].params.count(k))
                CHECK(loaded.slots[i].params.at(k) == Approx(v));
        }
    }
    CHECK(loaded.post_fx.scanlines == original.post_fx.scanlines);
    CHECK(loaded.post_fx.bloom == original.post_fx.bloom);
    CHECK(loaded.post_fx.vignette == original.post_fx.vignette);
    CHECK(loaded.post_fx.barrel == original.post_fx.barrel);
}

TEST_CASE("Load nonexistent preset returns false", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset loaded;
    CHECK_FALSE(mgr.load("does_not_exist", loaded));
}

TEST_CASE("List presets returns saved files", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());

    // Save a preset first to ensure there's something to list
    Preset p;
    p.name = "list_test";
    mgr.save(p, PresetFormat::KEY_VALUE_TEXT);

    auto list = mgr.list_presets();
    CHECK(list.size() >= 1);
    bool found = false;
    for (const auto& s : list)
        if (s == "list_test" || s == "test_preset" || s == "empty_test")
            found = true;
    CHECK(found);
}

TEST_CASE("Empty preset round-trip", "[preset]") {
    PresetManager mgr;
    mgr.set_preset_dir(test_dir());
    Preset original;
    original.name = "empty_test";

    REQUIRE(mgr.save(original, PresetFormat::KEY_VALUE_TEXT));
    Preset loaded;
    REQUIRE(mgr.load("empty_test", loaded));
    CHECK(loaded.name == "empty_test");
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        CHECK(loaded.slots[i].dsp_path.empty());
        CHECK(loaded.slots[i].params.empty());
    }
}
