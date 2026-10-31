#include "doctest/doctest.h"
#include "fake_memory.h"

using namespace corrupt;

TEST_CASE("every target name round-trips")
{
    for (auto t : kAllTargets)
    {
        auto parsed = parse_target(target_name(t));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == t);
    }
    CHECK(parse_target("WRAM") == Target::WRAM); // case-insensitive
}

TEST_CASE("unknown target names are rejected")
{
    CHECK_FALSE(parse_target("dsp").has_value());
    CHECK_FALSE(parse_target("").has_value());
}

TEST_CASE("nominal sizes match the hardware")
{
    CHECK(nominal_size(Target::WRAM) == 0x20000);
    CHECK(nominal_size(Target::VRAM) == 0x10000);
    CHECK(nominal_size(Target::OAM) == 544);
    CHECK(nominal_size(Target::CGRAM) == 512);
    CHECK(nominal_size(Target::ARAM) == 0x10000);
}

TEST_CASE("range validation")
{
    FakeMemory mem;
    CHECK_FALSE(validate_range({ Target::WRAM, 0x0000, 0x1FFFF }, mem).has_value());
    CHECK_FALSE(validate_range({ Target::OAM, 10, 10 }, mem).has_value());

    auto past_end = validate_range({ Target::CGRAM, 0x000, 0x200 }, mem);
    REQUIRE(past_end.has_value());
    CHECK(past_end->find("0x200") != std::string::npos); // names the size

    CHECK(validate_range({ Target::WRAM, 0x20, 0x10 }, mem).has_value()); // reversed
}

TEST_CASE("range length")
{
    CHECK(AddressRange{ Target::WRAM, 0x10, 0x1F }.length() == 16);
}

TEST_CASE("an empty sram is reported as such")
{
    FakeMemory mem;
    mem.of(Target::SRAM).clear(); // cartridge without battery RAM
    auto err = validate_range({ Target::SRAM, 0, 0 }, mem);
    REQUIRE(err.has_value());
    CHECK(err->find("no sram") != std::string::npos);
}

TEST_CASE("addresses are padded the same everywhere: as many digits as the memory can need")
{
    CHECK(address_digits(Target::WRAM) == 5);  // 0x1FFFF
    CHECK(address_digits(Target::ROM) == 6);   // up to 0x7FFFFF
    CHECK(address_digits(Target::SRAM) == 5);  // up to 128 KB
    CHECK(address_digits(Target::VRAM) == 4);
    CHECK(address_digits(Target::ARAM) == 4);
    CHECK(address_digits(Target::OAM) == 4);
    CHECK(address_digits(Target::CGRAM) == 4);
    CHECK(hex_address(0x1FF, Target::WRAM) == "0x001FF");
    CHECK(hex_address(0xC0FFEE, Target::ROM) == "0xC0FFEE");
    CHECK(hex_address(0x12, Target::OAM) == "0x0012");
    CHECK(hex_address(0x123456, Target::VRAM) == "0x123456"); // never cut short
}
