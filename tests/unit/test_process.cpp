/**
 * @file tests/unit/test_process.cpp
 * @brief Test src/process.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <thread>

// lib imports
#include <boost/process/v1.hpp>

// local includes
#include <src/config.h>
#include <src/process.h>

namespace fs = std::filesystem;

TEST(ProcessTest, PrepareCommand) {
#ifdef SUNSHINE_BUILD_FLATPAK
  EXPECT_EQ(proc::prepare_command("steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  flatpak-spawn --host steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
  EXPECT_EQ(proc::prepare_command("  \t"), "");
#else
  EXPECT_EQ(proc::prepare_command("steam"), "steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "  steam  ");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
#endif
}

class ProcessPNGTest: public BaseTest {  // NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Create test directory
    test_dir = fs::temp_directory_path() / "sunshine_process_png_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Helper function to create a file with specific content
  void createTestFile(const fs::path &path, const std::vector<unsigned char> &content) const {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(content.data()), content.size());
    file.close();
  }

  fs::path test_dir;
};

// Tests for check_valid_png function
TEST_F(ProcessPNGTest, CheckValidPNG_ValidSignature) {
  // Valid PNG signature
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,  // PNG signature
    // Add some dummy data to make it more realistic
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  EXPECT_TRUE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_WrongSignature) {
  // Invalid PNG signature (wrong magic bytes)
  const std::vector<unsigned char> invalid_png_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_TooShort) {
  // File too short (less than 8 bytes)
  const std::vector<unsigned char> short_data = {
    0x89,
    0x50,
    0x4E,
    0x47
  };

  const fs::path test_file = test_dir / "short.png";
  createTestFile(test_file, short_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_EmptyFile) {
  // Empty file
  const std::vector<unsigned char> empty_data = {};

  const fs::path test_file = test_dir / "empty.png";
  createTestFile(test_file, empty_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_NonExistentFile) {
  // File doesn't exist
  const fs::path test_file = test_dir / "nonexistent.png";

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_RealFile) {
  // Test with the actual sunshine.png from the project root

  // Only run this test if the file exists
  if (const fs::path sunshine_png = fs::path(SUNSHINE_SOURCE_DIR) / "sunshine.png"; fs::exists(sunshine_png)) {
    EXPECT_TRUE(proc::check_valid_png(sunshine_png));
  } else {
    GTEST_SKIP() << "sunshine.png not found in project root";
  }
}

TEST_F(ProcessPNGTest, CheckValidPNG_JPEGFile) {
  // JPEG signature (not PNG)
  const std::vector<unsigned char> jpeg_data = {
    0xFF,
    0xD8,
    0xFF,
    0xE0,
    0x00,
    0x10,
    0x4A,
    0x46
  };

  const fs::path test_file = test_dir / "fake.png";
  createTestFile(test_file, jpeg_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_PartialSignature) {
  // Partial PNG signature (first 4 bytes correct, rest wrong)
  const std::vector<unsigned char> partial_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "partial.png";
  createTestFile(test_file, partial_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

// Tests for validate_app_image_path function
TEST_F(ProcessPNGTest, ValidateAppImagePath_EmptyPath) {
  // Empty path should return default
  const std::string result = proc::validate_app_image_path("");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonPNGExtension) {
  // Non-PNG extension should return default
  const std::string result = proc::validate_app_image_path("image.jpg");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_CaseInsensitiveExtension) {
  // Test that .PNG (uppercase) is recognized
  // Create a valid PNG file
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "test.PNG";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  // Should accept uppercase .PNG extension
  EXPECT_NE(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonExistentFile) {
  // Non-existent PNG file should return default
  const std::string result = proc::validate_app_image_path("/nonexistent/path/image.png");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_InvalidPNGSignature) {
  // File with .png extension but invalid signature should return default
  const std::vector<unsigned char> invalid_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_ValidPNG) {
  // Valid PNG file should return the path
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, test_file.string());
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_OldSteamDefault) {
  // Test the special case for old steam image path
  const std::string result = proc::validate_app_image_path("./assets/steam.png");
  EXPECT_EQ(result, SUNSHINE_ASSETS_DIR "/steam.png");
}

/**
 * @brief Fixture that parses an in-memory apps.json-style document via proc::parse().
 */
class ProcessParseTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "sunshine_process_parse_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Writes `contents` to a JSON file in the test directory and parses it via proc::parse().
  std::optional<proc::proc_t> parseAppsJson(const std::string &contents) const {
    const fs::path apps_file = test_dir / "apps.json";
    std::ofstream file(apps_file);
    file << contents;
    file.close();
    return proc::parse(apps_file.string());
  }

  fs::path test_dir;
};

TEST_F(ProcessParseTest, DisplayOverrideFieldsParsed) {
  constexpr auto json = R"({
    "env": {},
    "apps": [
      {
        "name": "Stardew Valley",
        "display-output-name": "SurroundMerged",
        "display-capture-crop": "3840,0,3840,2160"
      }
    ]
  })";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 1u);

  ASSERT_TRUE(apps[0].display_output_name.has_value());
  EXPECT_EQ(*apps[0].display_output_name, "SurroundMerged");
  ASSERT_TRUE(apps[0].display_capture_crop.has_value());
  EXPECT_EQ(*apps[0].display_capture_crop, "3840,0,3840,2160");
}

TEST_F(ProcessParseTest, DisplayOverrideFieldsAbsentByDefault) {
  constexpr auto json = R"({
    "env": {},
    "apps": [
      {
        "name": "Desktop"
      }
    ]
  })";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 1u);

  // Every other app entry must keep defaulting to "no override" so this feature is opt-in.
  EXPECT_FALSE(apps[0].display_output_name.has_value());
  EXPECT_FALSE(apps[0].display_capture_crop.has_value());
}

TEST_F(ProcessParseTest, DisplayOverrideFieldsEnvSubstitution) {
  // Custom delimiter: the value contains `)"`, which would prematurely close a default R"(...)".
  constexpr auto json = R"json({
    "env": {
      "SURROUND_OUTPUT": "SurroundMerged"
    },
    "apps": [
      {
        "name": "Stardew Valley",
        "display-output-name": "$(SURROUND_OUTPUT)"
      }
    ]
  })json";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 1u);

  ASSERT_TRUE(apps[0].display_output_name.has_value());
  EXPECT_EQ(*apps[0].display_output_name, "SurroundMerged");
}

TEST_F(ProcessParseTest, InputOverrideFieldsParsed) {
  constexpr auto json = R"({
    "env": {},
    "apps": [
      {
        "name": "Stardew Valley",
        "keyboard": false,
        "mouse": false,
        "controller": true,
        "gamepad": "ds4"
      }
    ]
  })";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 1u);

  ASSERT_TRUE(apps[0].input_keyboard.has_value());
  EXPECT_FALSE(*apps[0].input_keyboard);
  ASSERT_TRUE(apps[0].input_mouse.has_value());
  EXPECT_FALSE(*apps[0].input_mouse);
  ASSERT_TRUE(apps[0].input_controller.has_value());
  EXPECT_TRUE(*apps[0].input_controller);
  ASSERT_TRUE(apps[0].input_gamepad.has_value());
  EXPECT_EQ(*apps[0].input_gamepad, "ds4");
}

TEST_F(ProcessParseTest, InputOverrideFieldsAbsentByDefault) {
  constexpr auto json = R"({
    "env": {},
    "apps": [
      {
        "name": "Desktop"
      }
    ]
  })";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 1u);

  // Absent keys must stay unset so the app inherits the instance-global input settings.
  EXPECT_FALSE(apps[0].input_keyboard.has_value());
  EXPECT_FALSE(apps[0].input_mouse.has_value());
  EXPECT_FALSE(apps[0].input_controller.has_value());
  EXPECT_FALSE(apps[0].input_gamepad.has_value());
}

TEST_F(ProcessParseTest, InputGamepadOverrideAcceptsSupportedTypesAndRejectsInvalidTypes) {
  constexpr auto json = R"({
    "env": {},
    "apps": [
      { "name": "Automatic", "gamepad": "auto" },
      { "name": "DS4", "gamepad": "ds4" },
      { "name": "Xbox", "gamepad": "x360" },
      { "name": "Global", "gamepad": "unsupported" }
    ]
  })";

  auto proc_opt = parseAppsJson(json);
  ASSERT_TRUE(proc_opt.has_value());

  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 4u);
  ASSERT_TRUE(apps[0].input_gamepad.has_value());
  EXPECT_EQ(*apps[0].input_gamepad, "auto");
  ASSERT_TRUE(apps[1].input_gamepad.has_value());
  EXPECT_EQ(*apps[1].input_gamepad, "ds4");
  ASSERT_TRUE(apps[2].input_gamepad.has_value());
  EXPECT_EQ(*apps[2].input_gamepad, "x360");
  EXPECT_FALSE(apps[3].input_gamepad.has_value());
}

/**
 * @brief Fixture that snapshots/restores the global `config::video` display fields, since
 * `config::apply_app_display_override`/`clear_app_display_override` mutate global state.
 */
class ConfigAppDisplayOverrideTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    config::clear_app_display_override();
    original_output_name = config::video.output_name;
    original_capture_crop = config::video.capture_crop;
  }

  void TearDown() override {
    config::clear_app_display_override();
    config::video.output_name = original_output_name;
    config::video.capture_crop = original_capture_crop;
    BaseTest::TearDown();
  }

  std::string original_output_name;
  std::string original_capture_crop;
};

TEST_F(ConfigAppDisplayOverrideTest, ApplyOverridesBothFieldsAndClearRestoresThem) {
  config::video.output_name = "OriginalOutput";
  config::video.capture_crop = "";

  config::apply_app_display_override(std::optional<std::string> {"SurroundMerged"}, std::optional<std::string> {"3840,0,3840,2160"});

  EXPECT_EQ(config::video.output_name, "SurroundMerged");
  EXPECT_EQ(config::video.capture_crop, "3840,0,3840,2160");

  config::clear_app_display_override();

  EXPECT_EQ(config::video.output_name, "OriginalOutput");
  EXPECT_EQ(config::video.capture_crop, "");
}

TEST_F(ConfigAppDisplayOverrideTest, ApplyIsNoOpWhenBothArgumentsEmpty) {
  config::video.output_name = "OriginalOutput";
  config::video.capture_crop = "OriginalCrop";

  config::apply_app_display_override(std::nullopt, std::nullopt);

  EXPECT_EQ(config::video.output_name, "OriginalOutput");
  EXPECT_EQ(config::video.capture_crop, "OriginalCrop");

  // Nothing was applied, so clearing must also be a no-op.
  config::clear_app_display_override();
  EXPECT_EQ(config::video.output_name, "OriginalOutput");
  EXPECT_EQ(config::video.capture_crop, "OriginalCrop");
}

TEST_F(ConfigAppDisplayOverrideTest, ClearIsIdempotent) {
  config::video.output_name = "OriginalOutput";

  config::apply_app_display_override(std::optional<std::string> {"SurroundMerged"}, std::nullopt);
  config::clear_app_display_override();
  EXPECT_EQ(config::video.output_name, "OriginalOutput");

  // Guards against both proc_t::terminate() and nvhttp's cancel()/fail-guard paths firing.
  config::clear_app_display_override();
  EXPECT_EQ(config::video.output_name, "OriginalOutput");
}

TEST_F(ConfigAppDisplayOverrideTest, RepeatedApplyDoesNotClobberSavedState) {
  config::video.output_name = "OriginalOutput";

  // Simulates a Moonlight client calling /resume again while the override from an earlier
  // /launch is still active, since Sunshine does not revert display config on plain disconnect.
  config::apply_app_display_override(std::optional<std::string> {"SurroundMerged"}, std::nullopt);
  config::apply_app_display_override(std::optional<std::string> {"SurroundMerged"}, std::nullopt);

  config::clear_app_display_override();

  EXPECT_EQ(config::video.output_name, "OriginalOutput");
}

/**
 * @brief Fixture that snapshots/restores the global `config::input` flags, since
 * `config::apply_app_input_override`/`clear_app_input_override` mutate global state.
 */
class ConfigAppInputOverrideTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    config::clear_app_input_override();
    original_keyboard = config::input.keyboard;
    original_mouse = config::input.mouse;
    original_controller = config::input.controller;
    original_gamepad = config::input.gamepad;
  }

  void TearDown() override {
    config::clear_app_input_override();
    config::input.keyboard = original_keyboard;
    config::input.mouse = original_mouse;
    config::input.controller = original_controller;
    config::input.gamepad = original_gamepad;
    BaseTest::TearDown();
  }

  bool original_keyboard;
  bool original_mouse;
  bool original_controller;
  std::string original_gamepad;
};

TEST_F(ConfigAppInputOverrideTest, AppliesOnlySpecifiedFieldsAndClearRestores) {
  config::input.keyboard = true;
  config::input.mouse = true;
  config::input.controller = true;

  // Force keyboard+mouse off (gamepad-only); leave controller unset so it inherits.
  config::apply_app_input_override(std::optional<bool> {false}, std::optional<bool> {false}, std::nullopt, std::nullopt);

  EXPECT_FALSE(config::input.keyboard);
  EXPECT_FALSE(config::input.mouse);
  EXPECT_TRUE(config::input.controller);  // untouched (inherited)

  config::clear_app_input_override();

  EXPECT_TRUE(config::input.keyboard);
  EXPECT_TRUE(config::input.mouse);
  EXPECT_TRUE(config::input.controller);
}

TEST_F(ConfigAppInputOverrideTest, ApplyIsNoOpWhenAllArgumentsEmpty) {
  config::input.keyboard = true;
  config::input.mouse = false;
  config::input.controller = true;

  config::apply_app_input_override(std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  EXPECT_TRUE(config::input.keyboard);
  EXPECT_FALSE(config::input.mouse);
  EXPECT_TRUE(config::input.controller);

  // Nothing was applied, so clearing must also be a no-op.
  config::clear_app_input_override();
  EXPECT_TRUE(config::input.keyboard);
  EXPECT_FALSE(config::input.mouse);
  EXPECT_TRUE(config::input.controller);
}

TEST_F(ConfigAppInputOverrideTest, RepeatedApplyDoesNotClobberSavedStateAndClearIsIdempotent) {
  config::input.keyboard = true;

  // Simulates /resume re-applying while the /launch override is still active.
  config::apply_app_input_override(std::optional<bool> {false}, std::nullopt, std::nullopt, std::nullopt);
  config::apply_app_input_override(std::optional<bool> {false}, std::nullopt, std::nullopt, std::nullopt);

  config::clear_app_input_override();
  EXPECT_TRUE(config::input.keyboard);

  // Guards against both terminate() and nvhttp's cancel()/fail-guard paths firing.
  config::clear_app_input_override();
  EXPECT_TRUE(config::input.keyboard);
}

TEST_F(ConfigAppInputOverrideTest, AppliesGamepadTypeAndRestoresGlobalType) {
  config::input.gamepad = "x360";

  config::apply_app_input_override(std::nullopt, std::nullopt, std::nullopt, std::optional<std::string> {"ds4"});
  EXPECT_EQ(config::input.gamepad, "ds4");

  config::apply_app_input_override(std::nullopt, std::nullopt, std::nullopt, std::optional<std::string> {"auto"});
  EXPECT_EQ(config::input.gamepad, "auto");

  config::clear_app_input_override();
  EXPECT_EQ(config::input.gamepad, "x360");
}

TEST(ParseCaptureCropTest, ParsesWellFormedRect) {
  auto crop = config::parse_capture_crop("3840,0,3840,2160");
  ASSERT_TRUE(crop.has_value());
  EXPECT_EQ(crop->x, 3840);
  EXPECT_EQ(crop->y, 0);
  EXPECT_EQ(crop->width, 3840);
  EXPECT_EQ(crop->height, 2160);
}

TEST(ParseCaptureCropTest, TrimsSurroundingWhitespace) {
  auto crop = config::parse_capture_crop("  1,2,3,4\n");
  ASSERT_TRUE(crop.has_value());
  EXPECT_EQ(crop->x, 1);
  EXPECT_EQ(crop->y, 2);
  EXPECT_EQ(crop->width, 3);
  EXPECT_EQ(crop->height, 4);
}

TEST(ParseCaptureCropTest, EmptyOrWhitespaceYieldsNoCrop) {
  EXPECT_FALSE(config::parse_capture_crop("").has_value());
  EXPECT_FALSE(config::parse_capture_crop("   ").has_value());
  EXPECT_FALSE(config::parse_capture_crop("\t\n").has_value());
}

TEST(ParseCaptureCropTest, RejectsWrongFieldCount) {
  EXPECT_FALSE(config::parse_capture_crop("1,2,3").has_value());  // too few
  EXPECT_FALSE(config::parse_capture_crop("1,2,3,4,5").has_value());  // too many
  EXPECT_FALSE(config::parse_capture_crop("1,2,3,").has_value());  // trailing comma / empty field
  EXPECT_FALSE(config::parse_capture_crop("1,,3,4").has_value());  // empty middle field
}

TEST(ParseCaptureCropTest, RejectsNonIntegerAndNegativeFields) {
  EXPECT_FALSE(config::parse_capture_crop("a,b,c,d").has_value());
  EXPECT_FALSE(config::parse_capture_crop("1,2,3,x").has_value());
  EXPECT_FALSE(config::parse_capture_crop("1,2,3.5,4").has_value());  // trailing junk after integer
  EXPECT_FALSE(config::parse_capture_crop("-1,0,10,10").has_value());  // negative offset
}

TEST(ParseCaptureCropTest, RejectsNonPositiveArea) {
  EXPECT_FALSE(config::parse_capture_crop("0,0,0,100").has_value());  // zero width
  EXPECT_FALSE(config::parse_capture_crop("0,0,100,0").has_value());  // zero height
}

TEST(ParseCaptureCropTest, AllowsZeroOffsets) {
  auto crop = config::parse_capture_crop("0,0,1920,1080");
  ASSERT_TRUE(crop.has_value());
  EXPECT_EQ(crop->x, 0);
  EXPECT_EQ(crop->y, 0);
  EXPECT_EQ(crop->width, 1920);
  EXPECT_EQ(crop->height, 1080);
}

/**
 * @brief Test fixture for proc_t::update_apps_and_env and proc::refresh.
 */
class ProcessRefreshTest: public BaseTest {  // NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "sunshine_process_refresh_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  /**
   * @brief Write a minimal valid apps.json file.
   *
   * @param path Target path for the JSON file.
   * @param app_names List of application names to include.
   */
  void writeAppsJson(const fs::path &path, const std::vector<std::string> &app_names) const {
    std::ofstream file(path);
    file << "{\n  \"env\": {},\n  \"apps\": [\n";
    for (size_t i = 0; i < app_names.size(); ++i) {
      file << R"(    { "name": ")" << app_names[i] << R"(" })";
      if (i + 1 < app_names.size()) {
        file << ",";
      }
      file << "\n";
    }
    file << "  ]\n}\n";
    file.close();

    // Artificially advance the modification time to guarantee strict monotonicity
    // across tests. This prevents the static last_apps_file_update in refresh()
    // from skipping parses when tests execute rapidly in the same clock tick.
    static int test_time_offset = 1;
    auto new_time = fs::file_time_type::clock::now() + std::chrono::seconds(test_time_offset++);
    fs::last_write_time(path, new_time);
  }

  fs::path test_dir;
};

/**
 * @brief Test fixture for testing updates to process apps and environment.
 */
class ProcessUpdateTest: public ProcessRefreshTest {  // NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
protected:
  proc::proc_t saved_global_proc;

  void SetUp() override {
    ProcessRefreshTest::SetUp();

    saved_global_proc = std::move(proc::proc);

    boost::process::v1::environment env = boost::this_process::environment();
    std::vector<proc::ctx_t> apps_initial;
    proc::ctx_t ctx;
    ctx.name = "Desktop";
    ctx.id = "42";
    apps_initial.push_back(std::move(ctx));

    proc::proc_t target(std::move(env), std::move(apps_initial));
    proc::proc = std::move(target);

    auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();
    launch_session->width = 1920;
    launch_session->height = 1080;
    launch_session->fps = 60;
    launch_session->gcmap = 0;
    launch_session->enable_hdr = false;
    launch_session->host_audio = false;
    launch_session->enable_sops = false;
    launch_session->surround_info = 2;
    int rc = proc::proc.execute(42, launch_session);
    ASSERT_EQ(rc, 0);
  }

  void TearDown() override {
    proc::proc.terminate();
    proc::proc = std::move(saved_global_proc);
    ProcessRefreshTest::TearDown();
  }

  /**
   * @brief Build a simulated source environment and app list for the update.
   *
   * @return A constructed proc_t containing the new applications and environment.
   */
  proc::proc_t buildNewSource() const {
    boost::process::v1::environment env2 = boost::this_process::environment();
    std::vector<proc::ctx_t> apps_new;
    proc::ctx_t ctx_new;
    ctx_new.name = "NewApp";
    ctx_new.id = "99";
    apps_new.push_back(std::move(ctx_new));
    return proc::proc_t(std::move(env2), std::move(apps_new));
  }
};

TEST_F(ProcessRefreshTest, UpdateAppsAndEnv_UpdatesAppsList) {
  // Build an initial proc_t with one app
  boost::process::v1::environment env = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_initial;
  proc::ctx_t ctx_a;
  ctx_a.name = "AppA";
  ctx_a.id = "100";
  apps_initial.push_back(std::move(ctx_a));

  proc::proc_t target(std::move(env), std::move(apps_initial));
  ASSERT_EQ(target.get_apps().size(), 1u);
  EXPECT_EQ(target.get_apps()[0].name, "AppA");

  // Build a replacement proc_t with two apps
  boost::process::v1::environment env2 = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_new;
  proc::ctx_t ctx_b;
  ctx_b.name = "AppB";
  ctx_b.id = "200";
  apps_new.push_back(std::move(ctx_b));
  proc::ctx_t ctx_c;
  ctx_c.name = "AppC";
  ctx_c.id = "300";
  apps_new.push_back(std::move(ctx_c));

  proc::proc_t source(std::move(env2), std::move(apps_new));

  // Act
  target.update_apps_and_env(std::move(source));

  // Assert — apps list must now contain the new apps
  ASSERT_EQ(target.get_apps().size(), 2u);
  EXPECT_EQ(target.get_apps()[0].name, "AppB");
  EXPECT_EQ(target.get_apps()[1].name, "AppC");
}

TEST_F(ProcessUpdateTest, UpdateAppsAndEnv_PreservesRunningState) {
  proc::proc.update_apps_and_env(buildNewSource());

  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "NewApp");
  EXPECT_EQ(proc::proc.running(), 42) << "update_apps_and_env must not reset _app_id or placebo";
}

TEST_F(ProcessUpdateTest, UpdateAppsAndEnv_PreservesSessionEnvironment) {
  proc::proc.update_apps_and_env(buildNewSource());

  const auto &active_env = proc::proc.get_env();
  EXPECT_TRUE(active_env.count("SUNSHINE_APP_NAME") > 0)
    << "SUNSHINE_APP_NAME was wiped from the environment during update_apps_and_env";

  if (active_env.count("SUNSHINE_APP_NAME") > 0) {
    EXPECT_EQ(active_env.at("SUNSHINE_APP_NAME").to_string(), "Desktop");
  }
}

TEST_F(ProcessRefreshTest, Refresh_ParsesFileOnFirstCall) {
  // The first call to refresh() must always parse the file, regardless
  // of what file_time_type{} compares to relative to the file timestamp.
  //
  // NOTE: Because refresh() uses static local variables (has_parsed, last_apps_file_update),
  // this test exercises the very first call in this test process.  Subsequent
  // tests that call refresh() share the same static state.
  const fs::path apps_file = test_dir / "apps_initial.json";
  writeAppsJson(apps_file, {"TestApp1", "TestApp2"});

  // Save and clear global proc
  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  ASSERT_TRUE(proc::proc.get_apps().empty());

  // Act
  proc::refresh(apps_file.string());

  // Assert — must have been parsed
  ASSERT_EQ(proc::proc.get_apps().size(), 2u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "TestApp1");
  EXPECT_EQ(proc::proc.get_apps()[1].name, "TestApp2");

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_SkipsUnchangedFile) {
  const fs::path apps_file = test_dir / "apps_skip.json";
  writeAppsJson(apps_file, {"OriginalApp"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  // First refresh — should parse
  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);

  // Mutate apps in-memory to detect whether the second refresh re-parses
  proc::proc.get_apps()[0].name = "Mutated";

  // Second refresh with unchanged file — should be skipped
  proc::refresh(apps_file.string());
  EXPECT_EQ(proc::proc.get_apps()[0].name, "Mutated")
    << "refresh() should skip re-parse when the file timestamp has not changed";

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_ReparseAfterFileModified) {
  const fs::path apps_file = test_dir / "apps_modified.json";
  writeAppsJson(apps_file, {"BeforeEdit"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "BeforeEdit");

  // Rewrite to ensure a new timestamp
  writeAppsJson(apps_file, {"AfterEdit"});

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "AfterEdit");

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_ReparseAfterFileMtimeDecreased) {
  const fs::path apps_file = test_dir / "apps_reverted.json";
  writeAppsJson(apps_file, {"Initial"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "Initial");

  // Write a new file but artificially set its timestamp to the PAST
  // simulating a backup restoration or git checkout
  writeAppsJson(apps_file, {"OlderBackup"});
  auto past_time = fs::file_time_type::clock::now() - std::chrono::hours(1);
  fs::last_write_time(apps_file, past_time);

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "OlderBackup")
    << "refresh() must re-parse if the modification time differs, even if it is strictly earlier";

  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_PreservesRunningAppDuringReparse) {
  // This is the critical regression test: refreshing the app list while an
  // app is running must not lose the active process state.
  const fs::path apps_file = test_dir / "apps_running.json";
  writeAppsJson(apps_file, {"Desktop"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  // Initial parse
  proc::refresh(apps_file.string());
  ASSERT_FALSE(proc::proc.get_apps().empty());

  // Find the ID of "Desktop" as assigned by parse
  auto desktop_id = std::stoi(proc::proc.get_apps()[0].id);

  // Execute it in placebo mode (empty cmd)
  auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();
  launch_session->width = 1920;
  launch_session->height = 1080;
  launch_session->fps = 60;
  launch_session->gcmap = 0;
  launch_session->enable_hdr = false;
  launch_session->host_audio = false;
  launch_session->enable_sops = false;
  launch_session->surround_info = 2;
  int rc = proc::proc.execute(desktop_id, launch_session);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(proc::proc.running(), desktop_id);

  // Simulate an external edit to the apps file
  writeAppsJson(apps_file, {"Desktop", "NewApp"});

  // Act — refresh while the app is "running"
  proc::refresh(apps_file.string());

  // Assert — app list is updated but the running state is preserved
  ASSERT_EQ(proc::proc.get_apps().size(), 2u);
  EXPECT_EQ(proc::proc.running(), desktop_id)
    << "refresh() during a running session must preserve active process state";

  // Cleanup — terminate before restoring
  proc::proc.terminate();
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, FileTimeType_DefaultValueComparison) {
  // This test documents the behavior that triggered the timestamp guard
  // regression. On some toolchains (MSYS2 UCRT64 GCC 16.2), file timestamps
  // and clock::now() can compare less-or-equal to file_time_type{}.
  //
  // The fix uses std::optional<file_time_type> instead of relying on
  // file_time_type{} as a "never parsed" indicator.
  const auto default_time = std::filesystem::file_time_type {};
  const auto now_time = std::filesystem::file_time_type::clock::now();

  // We cannot assert the comparison direction since it is platform-dependent.
  // Instead, just log and document both cases.
  if (now_time <= default_time) {
    BOOST_LOG(info) << "file_time_type::clock::now() <= file_time_type{} is TRUE on this platform "
                    << "(this is the UCRT64 case that requires the optional sentinel)";
  } else {
    BOOST_LOG(info) << "file_time_type::clock::now() > file_time_type{} on this platform "
                    << "(default-init guard would have worked, but optional is still safer)";
  }

  // The actual correctness is verified by Refresh_ParsesFileOnFirstCall above —
  // if that test passes, the optional fix works regardless of platform behavior.
  SUCCEED();
}
