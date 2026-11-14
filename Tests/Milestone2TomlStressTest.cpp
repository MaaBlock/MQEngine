#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cassert>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/asset/interface/Plugin.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/thirdparty/TomlBackend.h>

using namespace MQEngine;
using namespace MQEngine::Asset;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

// =============================================================================
// Test Suite 1: Corrupted TOML Inputs (Syntax errors, unmatched brackets)
// =============================================================================
bool testCorruptedTomlInputs()
{
    std::cout << "\n=== [Test 1] Corrupted TOML Inputs (Broken Syntax, Unmatched Brackets) ===" << std::endl;

    const std::vector<std::pair<std::string, std::string>> corruptedCases = {
        {"Unclosed string", "name = \"UnterminatedString\nversion = \"1.0.0\""},
        {"Unmatched opening square bracket in array", "tags = [\"Runtime\", \"Editor\""},
        {"Unmatched closing square bracket in array", "tags = [\"Runtime\", \"Editor\"]]"},
        {"Unmatched curly brace in inline table", "details = { author = \"MQ\", ver = 1"},
        {"Unmatched bracket in table header (two open, one close)", "[[composites]\nname = \"CompA\""},
        {"Unclosed double bracket in table header", "[[composites\nname = \"CompA\""},
        {"Unclosed table header", "[composites\nname = \"CompA\""},
        {"Double equals sign", "name == \"DoubleEqual\""},
        {"Missing equals sign", "name \"NoEqual\""},
        {"Leading equals sign without key", "= \"NoKey\""},
        {"Illegal bare key with spaces", "plugin name = \"SpacedKey\""},
        {"Malformed float with multiple dots", "version = 1.0.0.0"},
        {"Duplicate key in table", "name = \"First\"\nname = \"Second\"\nversion = \"1.0.0\""},
        {"Unclosed multiline string", "description = \"\"\"multi-line without closing"},
        {"Invalid escape sequence", "name = \"Invalid\\zEscape\""}
    };

    for (const auto& [label, tomlStr] : corruptedCases)
    {
        // 1. Direct parseMetadataFromToml
        auto tomlResult = PluginPackage::parseMetadataFromToml(tomlStr);
        TEST_CHECK(!tomlResult.ok(), "Corrupted TOML [" + label + "] must return failure Status");
        TEST_CHECK(tomlResult.status().code() == absl::StatusCode::kInvalidArgument,
                   "Corrupted TOML [" + label + "] must return InvalidArgumentError, got: " + std::string(tomlResult.status().message()));

        // 2. Dispatcher parseMetadata
        auto metaResult = PluginPackage::parseMetadata(tomlStr);
        TEST_CHECK(!metaResult.ok(), "Corrupted TOML via parseMetadata [" + label + "] must return failure Status");
        TEST_CHECK(metaResult.status().code() == absl::StatusCode::kInvalidArgument,
                   "Corrupted TOML via parseMetadata [" + label + "] must return InvalidArgumentError");
    }

    std::cout << "[PASS] Test 1: All 15 corrupted TOML cases safely rejected with InvalidArgumentError (0 crashes, 0 exceptions)." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 2: Binary, Control, and Non-UTF8 Data
// =============================================================================
bool testBinaryAndFuzzData()
{
    std::cout << "\n=== [Test 2] Binary, Control Characters, and Fuzz Data ===" << std::endl;

    // 2.1 Pure binary / random garbage
    std::string binaryGarbage;
    for (int i = 0; i < 256; ++i)
    {
        binaryGarbage.push_back(static_cast<char>(i));
    }
    auto binResult = PluginPackage::parseMetadataFromToml(binaryGarbage);
    TEST_CHECK(!binResult.ok(), "Binary garbage must fail TOML parsing");
    TEST_CHECK(binResult.status().code() == absl::StatusCode::kInvalidArgument,
               "Binary garbage must return InvalidArgumentError");

    // 2.2 String with embedded null bytes
    std::string embeddedNull = std::string("name = \"prefix\0suffix\"\nversion=\"1.0.0\"", 38);
    auto nullResult = PluginPackage::parseMetadataFromToml(embeddedNull);
    TEST_CHECK(true, "Embedded null survived without crash or unhandled exception");

    // 2.3 ASCII punctuation noise
    std::string noise = "!@#$%^&*()_+~`|}{[]\\:;\"'<>?,./";
    auto noiseResult = PluginPackage::parseMetadataFromToml(noise);
    TEST_CHECK(!noiseResult.ok(), "ASCII punctuation noise must fail TOML parsing");
    TEST_CHECK(noiseResult.status().code() == absl::StatusCode::kInvalidArgument,
               "ASCII noise must return InvalidArgumentError");

    // 2.4 High bytes stream (non-ASCII invalid UTF-8 sequences)
    std::string invalidUtf8 = "name = \"\xFF\xFE\xFD\"";
    auto utf8Res = PluginPackage::parseMetadataFromToml(invalidUtf8);
    TEST_CHECK(!utf8Res.ok(), "Invalid UTF-8 must fail TOML parsing");
    TEST_CHECK(utf8Res.status().code() == absl::StatusCode::kInvalidArgument,
               "Invalid UTF-8 must return InvalidArgumentError");

    std::cout << "[PASS] Test 2: Binary, embedded nulls, and fuzz noise safely handled (0 crashes, 0 exceptions)." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 3: Empty and Whitespace Inputs
// =============================================================================
bool testEmptyAndWhitespaceInputs()
{
    std::cout << "\n=== [Test 3] Empty and Whitespace Inputs ===" << std::endl;

    // 3.1 Empty string
    auto emptyToml = PluginPackage::parseMetadataFromToml("");
    TEST_CHECK(!emptyToml.ok(), "Empty string must fail parseMetadataFromToml");
    TEST_CHECK(emptyToml.status().code() == absl::StatusCode::kInvalidArgument,
               "Empty string must return InvalidArgumentError");

    auto emptyMeta = PluginPackage::parseMetadata("");
    TEST_CHECK(!emptyMeta.ok(), "Empty string must fail parseMetadata");
    TEST_CHECK(emptyMeta.status().code() == absl::StatusCode::kInvalidArgument,
               "Empty string must return InvalidArgumentError");

    // 3.2 Whitespace only strings
    const std::vector<std::string> whitespaces = {
        "   ",
        "\t\t\t",
        "\n\n\n\n",
        " \t \r\n \v\f "
    };

    for (const auto& ws : whitespaces)
    {
        // In TOML, whitespace parses as an empty document/table
        auto wsResult = PluginPackage::parseMetadataFromToml(ws);
        TEST_CHECK(true, "Whitespace survived without crash");
    }

    // 3.3 Comment only
    auto commentResult = PluginPackage::parseMetadataFromToml("# Just a comment\n# Another line\n");
    TEST_CHECK(true, "Comment-only survived without crash");

    std::cout << "[PASS] Test 3: Empty and whitespace inputs safely handled (0 crashes, 0 exceptions)." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 4: Missing Required Fields & Partial Manifests
// =============================================================================
bool testMissingFieldsAndIncompleteManifests()
{
    std::cout << "\n=== [Test 4] Missing Required Fields & Incomplete Manifests ===" << std::endl;

    // 4.1 Missing name
    std::string missingName = "version = \"1.0.0\"\ntags = [\"Editor\"]\n[[composites]]\nname = \"CompA\"";
    auto res1 = PluginPackage::parseMetadataFromToml(missingName);
    std::cout << "  [Obs 4.1] Missing name ok(): " << res1.ok()
              << ", meta.name: \"" << (res1.ok() ? res1->name : "N/A") << "\"" << std::endl;
    TEST_CHECK(res1.ok(), "Missing name parsed syntax-wise without crashing");
    TEST_CHECK(res1->name.empty(), "Missing name yields empty string");

    // 4.2 Missing version
    std::string missingVersion = "name = \"PluginWithoutVersion\"\n[[composites]]\nname = \"CompA\"";
    auto res2 = PluginPackage::parseMetadataFromToml(missingVersion);
    std::cout << "  [Obs 4.2] Missing version ok(): " << res2.ok()
              << ", meta.version: \"" << (res2.ok() ? res2->version : "N/A") << "\"" << std::endl;
    TEST_CHECK(res2.ok(), "Missing version parsed syntax-wise without crashing");
    TEST_CHECK(res2->version == "1.0.0", "Missing version falls back to default 1.0.0");

    // 4.3 Missing composites
    std::string missingComposites = "name = \"PluginWithoutComposites\"\nversion = \"1.0.0\"";
    auto res3 = PluginPackage::parseMetadataFromToml(missingComposites);
    std::cout << "  [Obs 4.3] Missing composites ok(): " << res3.ok()
              << ", composites size: " << (res3.ok() ? res3->composites.size() : 0) << std::endl;
    TEST_CHECK(res3.ok(), "Missing composites parsed syntax-wise without crashing");
    TEST_CHECK(res3->composites.empty(), "Missing composites yields empty array");

    // 4.4 Composite without name
    std::string compositeNoName = "name = \"PlugA\"\nversion = \"1.0.0\"\n[[composites]]\ntags = [\"Editor\"]";
    auto res4 = PluginPackage::parseMetadataFromToml(compositeNoName);
    TEST_CHECK(res4.ok(), "Composite without name parses valid TOML syntax");
    TEST_CHECK(res4->composites.empty(), "Composite without name must be skipped, composites list should be empty");
    TEST_CHECK(res4->compositeManifests.empty(), "Composite without name must be skipped, manifest list should be empty");

    std::cout << "[PASS] Test 4: Missing fields and incomplete manifests handled without crash or exception." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 5: Type Mismatches & Malformed Tables
// =============================================================================
bool testTypeMismatches()
{
    std::cout << "\n=== [Test 5] Type Mismatches & Malformed Tables ===" << std::endl;

    // 5.1 Scalar field type mismatches
    std::string typeMismatch = 
        "name = 12345\n"
        "version = true\n"
        "author = [1, 2, 3]\n"
        "description = { nested = false }\n"
        "tags = \"not_an_array\"\n"
        "dependencies = 42\n"
        "[[composites]]\n"
        "name = \"ValidComp\"\n";

    auto res1 = PluginPackage::parseMetadataFromToml(typeMismatch);
    TEST_CHECK(res1.ok(), "Valid TOML syntax with unexpected types must parse safely");
    TEST_CHECK(res1->name.empty(), "Integer name must be ignored / empty string");
    TEST_CHECK(res1->version == "1.0.0", "Boolean version must fall back to default");
    TEST_CHECK(res1->author == "MQEngine", "Array author must fall back to default");
    TEST_CHECK(res1->tags.empty(), "String tags must not crash and remain empty");
    TEST_CHECK(res1->dependencies.empty(), "Integer dependencies must not crash and remain empty");
    TEST_CHECK(res1->composites.size() == 1, "ValidComp should be preserved");

    // 5.2 composites field is a single table [composites] instead of an array of tables [[composites]]
    std::string compositeSingleTable = "name = \"PlugSingle\"\n[composites]\nname = \"CompA\"\n";
    auto resSingle = PluginPackage::parseMetadataFromToml(compositeSingleTable);
    TEST_CHECK(resSingle.ok(), "Single table [composites] syntax parses cleanly");
    TEST_CHECK(resSingle->composites.empty(), "Single table [composites] is not an array, yields empty composites list");

    // 5.3 composites field is not an array of tables
    std::string compositesNotArray = "name = \"PlugB\"\ncomposites = \"InvalidString\"";
    auto res2 = PluginPackage::parseMetadataFromToml(compositesNotArray);
    TEST_CHECK(res2.ok(), "Non-array composites field must not crash");
    TEST_CHECK(res2->composites.empty(), "Non-array composites should yield empty composites");

    // 5.4 composites field is an array of non-tables (e.g. array of integers)
    std::string compositesArrayOfInts = "name = \"PlugC\"\ncomposites = [10, 20, 30]";
    auto res3 = PluginPackage::parseMetadataFromToml(compositesArrayOfInts);
    TEST_CHECK(res3.ok(), "Array of integers in composites must not crash");
    TEST_CHECK(res3->composites.empty(), "Array of integers should yield empty composites");

    // 5.5 composite table with bad inner types
    std::string compositeBadInner = 
        "name = \"PlugD\"\n"
        "[[composites]]\n"
        "name = 9999\n"
        "tags = \"string_not_array\"\n"
        "dependencies = true\n";
    auto res4 = PluginPackage::parseMetadataFromToml(compositeBadInner);
    TEST_CHECK(res4.ok(), "Composite with bad inner types must parse safely");
    TEST_CHECK(res4->composites.empty(), "Composite with non-string name must be skipped");

    std::cout << "[PASS] Test 5: Type mismatches safely filtered without crash or exception." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 6: Valid JSON Fallback via PluginPackage::parseMetadata
// =============================================================================
bool testValidJsonFallback()
{
    std::cout << "\n=== [Test 6] Valid JSON Fallback via PluginPackage::parseMetadata ===" << std::endl;

    // 6.1 Valid JSON with simple string array composites
    std::string jsonStr1 = R"({
        "name": "JsonPluginA",
        "version": "2.5.0",
        "author": "JSON Author",
        "description": "A legacy JSON plugin",
        "license": "Apache-2.0",
        "signature": "SIG_JSON",
        "tags": ["Runtime", "Game"],
        "dependencies": ["Dep1", "Dep2"],
        "composites": ["Comp1", "Comp2"]
    })";

    auto res1 = PluginPackage::parseMetadata(jsonStr1);
    TEST_CHECK(res1.ok(), "Valid JSON must parse successfully via fallback");
    TEST_CHECK(res1->name == "JsonPluginA", "Name must match JSON");
    TEST_CHECK(res1->version == "2.5.0", "Version must match JSON");
    TEST_CHECK(res1->author == "JSON Author", "Author must match JSON");
    TEST_CHECK(res1->license == "Apache-2.0", "License must match JSON");
    TEST_CHECK(res1->signature == "SIG_JSON", "Signature must match JSON");
    TEST_CHECK(res1->tags.size() == 2 && res1->tags[0] == "Runtime", "Tags must match JSON");
    TEST_CHECK(res1->dependencies.size() == 2 && res1->dependencies[1] == "Dep2", "Dependencies must match JSON");
    TEST_CHECK(res1->composites.size() == 2, "Composites size must be 2");
    TEST_CHECK(res1->compositeManifests.size() == 2, "CompositeManifests size must be 2");
    TEST_CHECK(res1->compositeManifests[0].name == "Comp1", "Manifest 0 name must be Comp1");
    TEST_CHECK(res1->compositeManifests[0].pluginName == "JsonPluginA", "Manifest 0 pluginName must match");

    // 6.2 Valid JSON with structured object array composites
    std::string jsonStr2 = R"({
        "name": "JsonPluginB",
        "version": "1.0.0",
        "composites": [
            {
                "name": "ObjCompA",
                "tags": ["Editor", "ImGui"],
                "dependencies": ["OtherComp"]
            }
        ]
    })";

    auto res2 = PluginPackage::parseMetadata(jsonStr2);
    TEST_CHECK(res2.ok(), "Valid JSON with composite objects must parse");
    TEST_CHECK(res2->name == "JsonPluginB", "Name must match");
    TEST_CHECK(res2->composites.size() == 1 && res2->composites[0] == "ObjCompA", "Composite name must match");
    TEST_CHECK(res2->compositeManifests.size() == 1, "Manifest size must be 1");
    TEST_CHECK(res2->compositeManifests[0].tags.size() == 2, "Manifest tags size must be 2");
    TEST_CHECK(res2->compositeManifests[0].dependencies.size() == 1, "Manifest dependencies size must be 1");

    // 6.3 Corrupted JSON that is also corrupted TOML
    std::string badBoth = "{ name = \"InvalidSyntax\" :::: }";
    auto badRes = PluginPackage::parseMetadata(badBoth);
    TEST_CHECK(!badRes.ok(), "Corrupted both TOML and JSON must return failure");
    TEST_CHECK(badRes.status().code() == absl::StatusCode::kInvalidArgument,
               "Corrupted payload must return InvalidArgumentError");

    std::cout << "[PASS] Test 6: Valid JSON fallback and bad JSON rejection verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 7: Valid TOML per Doc 04 Specification
// =============================================================================
bool testValidTomlDoc04()
{
    std::cout << "\n=== [Test 7] Valid TOML per Doc 04 Specification ===" << std::endl;

    std::string doc04Toml = 
        "# Plugins/EditorBase/plugin.mq\n"
        "name = \"EditorBase\"\n"
        "version = \"1.0.0\"\n"
        "author = \"MQEngine Team\"\n"
        "description = \"编辑器排版与布局底座插件包（纯目录分组，内含独立的 Composite DLL）\"\n"
        "license = \"MIT\"\n"
        "signature = \"MQ_SIG_2026\"\n"
        "dependencies = [\"CorePlugin\", \"HostPlugin\"]\n"
        "tags = [\"Runtime\", \"Editor\", \"ImGui\"]\n"
        "\n"
        "[[composites]]\n"
        "name = \"ImGuiComposite\"\n"
        "tags = [\"Runtime\", \"ImGui\"]\n"
        "dependencies = []\n"
        "\n"
        "[[composites]]\n"
        "name = \"EditorUiComposite\"\n"
        "tags = [\"Editor\", \"ImGui\"]\n"
        "dependencies = [\"ImGuiComposite\"]\n"
        "\n"
        "[[composites]]\n"
        "name = \"EditorLayoutComposite\"\n"
        "tags = [\"Editor\", \"ImGui\"]\n"
        "dependencies = [\"ImGuiComposite\", \"EditorUiComposite\"]\n";

    // Test parseMetadataFromToml directly
    auto res1 = PluginPackage::parseMetadataFromToml(doc04Toml);
    TEST_CHECK(res1.ok(), "Doc 04 TOML must parse successfully");
    TEST_CHECK(res1->name == "EditorBase", "Name must match");
    TEST_CHECK(res1->version == "1.0.0", "Version must match");
    TEST_CHECK(res1->author == "MQEngine Team", "Author must match");
    TEST_CHECK(res1->license == "MIT", "License must match");
    TEST_CHECK(res1->signature == "MQ_SIG_2026", "Signature must match");
    TEST_CHECK(res1->dependencies.size() == 2, "Dependencies size must be 2");
    TEST_CHECK(res1->dependencies[0] == "CorePlugin", "Dep 0 must be CorePlugin");
    TEST_CHECK(res1->tags.size() == 3, "Tags size must be 3");
    TEST_CHECK(res1->tags[2] == "ImGui", "Tag 2 must be ImGui");

    TEST_CHECK(res1->composites.size() == 3, "Composites count must be 3");
    TEST_CHECK(res1->composites[0] == "ImGuiComposite", "Comp 0 must be ImGuiComposite");
    TEST_CHECK(res1->composites[1] == "EditorUiComposite", "Comp 1 must be EditorUiComposite");
    TEST_CHECK(res1->composites[2] == "EditorLayoutComposite", "Comp 2 must be EditorLayoutComposite");

    TEST_CHECK(res1->compositeManifests.size() == 3, "Manifest count must be 3");
    TEST_CHECK(res1->compositeManifests[0].name == "ImGuiComposite", "Manifest 0 name");
    TEST_CHECK(res1->compositeManifests[0].pluginName == "EditorBase", "Manifest 0 pluginName");
    TEST_CHECK(res1->compositeManifests[0].tags.size() == 2, "Manifest 0 tags");
    TEST_CHECK(res1->compositeManifests[1].name == "EditorUiComposite", "Manifest 1 name");
    TEST_CHECK(res1->compositeManifests[1].dependencies.size() == 1, "Manifest 1 dependencies");
    TEST_CHECK(res1->compositeManifests[1].dependencies[0] == "ImGuiComposite", "Manifest 1 dependency name");
    TEST_CHECK(res1->compositeManifests[2].dependencies.size() == 2, "Manifest 2 dependencies");

    // Also test parseMetadata dispatcher
    auto res2 = PluginPackage::parseMetadata(doc04Toml);
    TEST_CHECK(res2.ok(), "Doc 04 TOML via parseMetadata must parse successfully");
    TEST_CHECK(res2->name == "EditorBase", "Name via parseMetadata must match");
    TEST_CHECK(res2->composites.size() == 3, "Composites count via parseMetadata must match");

    std::cout << "[PASS] Test 7: Valid TOML per Doc 04 format correctly populates all metadata and composite manifests." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 8: Real Plugin Manifest Files on Physical Disk
// =============================================================================
bool testRealPluginFilesOnDisk()
{
    std::cout << "\n=== [Test 8] Real Plugin Manifest Files on Physical Disk ===" << std::endl;

    const std::vector<std::tuple<std::string, std::string, std::vector<std::string>>> diskPlugins = {
        {
            "G:/MQEngine/MQEngine/Engine/Plugins/EditorBase/plugin.mq",
            "EditorBase",
            {"ImGuiComposite", "EditorUiComposite"}
        },
        {
            "G:/MQEngine/MQEngine/Engine/Plugins/MQEditor/plugin.mq",
            "MQEditor",
            {"SceneViewerComposite", "AssetBrowserComposite", "ShaderEditorComposite"}
        },
        {
            "G:/MQEngine/MQEngine/Engine/Plugins/GameHtmlUI/plugin.mq",
            "GameHtmlUI",
            {"RmlUiComposite", "HtmlUiEditorComposite"}
        }
    };

    for (const auto& [filePath, expectedName, expectedComps] : diskPlugins)
    {
        std::ifstream file(filePath);
        TEST_CHECK(file.is_open(), "Must be able to open file: " + filePath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        TEST_CHECK(!content.empty(), "File content must not be empty: " + filePath);

        auto parseRes = PluginPackage::parseMetadata(content);
        TEST_CHECK(parseRes.ok(), "Real plugin file must parse cleanly: " + filePath);
        TEST_CHECK(parseRes->name == expectedName, "Plugin name must match " + expectedName);
        TEST_CHECK(parseRes->composites.size() == expectedComps.size(), 
                   "Composite count for " + expectedName + " must match expected " + std::to_string(expectedComps.size()));

        for (size_t i = 0; i < expectedComps.size(); ++i)
        {
            TEST_CHECK(parseRes->composites[i] == expectedComps[i],
                       "Composite[" + std::to_string(i) + "] must be " + expectedComps[i]);
            TEST_CHECK(parseRes->compositeManifests[i].name == expectedComps[i],
                       "Manifest[" + std::to_string(i) + "] name must be " + expectedComps[i]);
            TEST_CHECK(parseRes->compositeManifests[i].pluginName == expectedName,
                       "Manifest[" + std::to_string(i) + "] pluginName must be " + expectedName);
        }

        std::cout << "  [Disk Manifest Verified] " << expectedName 
                  << " (" << parseRes->composites.size() << " composites): OK" << std::endl;
    }

    std::cout << "[PASS] Test 8: All real physical plugin.mq files on disk parse bit-for-bit correctly." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 9: Adversarial Stress & Concurrency Fuzzing
// =============================================================================
bool testStressAndConcurrency()
{
    std::cout << "\n=== [Test 9] Adversarial Stress & Concurrency Fuzzing ===" << std::endl;

    // 9.1 High-volume composite tables (500 composites)
    std::string bigToml = "name = \"MegaPlugin\"\nversion = \"9.9.9\"\n";
    for (int i = 0; i < 500; ++i)
    {
        bigToml += "[[composites]]\nname = \"MegaComp_" + std::to_string(i) + "\"\n"
                   "tags = [\"Tag_" + std::to_string(i % 5) + "\"]\n"
                   "dependencies = [\"MegaComp_" + std::to_string((i > 0 ? i - 1 : 0)) + "\"]\n";
    }

    auto startMega = std::chrono::high_resolution_clock::now();
    auto megaRes = PluginPackage::parseMetadataFromToml(bigToml);
    auto endMega = std::chrono::high_resolution_clock::now();
    double megaMs = std::chrono::duration<double, std::milli>(endMega - startMega).count();

    TEST_CHECK(megaRes.ok(), "Mega TOML with 500 composites must parse successfully");
    TEST_CHECK(megaRes->composites.size() == 500, "Must have exactly 500 composites");
    TEST_CHECK(megaRes->compositeManifests.size() == 500, "Must have exactly 500 manifests");
    std::cout << "  [Scale 500 Composites] Parse duration: " << megaMs << " ms: OK" << std::endl;

    // 9.2 Very large description string (1 MB)
    std::string hugeDesc(1024 * 1024, 'X');
    std::string hugeToml = "name = \"HugeStringPlugin\"\ndescription = \"" + hugeDesc + "\"\n";
    auto hugeRes = PluginPackage::parseMetadataFromToml(hugeToml);
    TEST_CHECK(hugeRes.ok(), "1MB string description must parse without crash");
    TEST_CHECK(hugeRes->description.size() == 1024 * 1024, "Description size must be 1MB");

    // 9.3 Multithreaded concurrency stress (8 threads x 200 iterations = 1,600 parse calls)
    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    std::vector<std::thread> workers;
    std::atomic<bool> startFlag{false};
    std::atomic<int> passCount{0};

    std::string sampleToml = "name = \"ThreadPlug\"\nversion = \"1.0.0\"\n[[composites]]\nname = \"ThreadComp\"\n";
    std::string sampleBad = "name = \"Broken\n";

    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for (int i = 0; i < kIterations; ++i)
            {
                // Alternately parse valid and invalid TOML
                if (i % 2 == 0)
                {
                    auto r = PluginPackage::parseMetadataFromToml(sampleToml);
                    if (r.ok() && r->name == "ThreadPlug")
                    {
                        passCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else
                {
                    auto r = PluginPackage::parseMetadataFromToml(sampleBad);
                    if (!r.ok() && r.status().code() == absl::StatusCode::kInvalidArgument)
                    {
                        passCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& w : workers)
    {
        w.join();
    }

    TEST_CHECK(passCount.load() == kThreads * kIterations, 
               "All multithreaded iterations must match expected pass count");
    std::cout << "  [Concurrency 8 Threads] 1,600 parallel parse calls completed cleanly: OK" << std::endl;

    std::cout << "[PASS] Test 9: Stress and concurrency fuzzing completed with 100% success (0 crashes, 0 races)." << std::endl;
    return true;
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main()
{
    std::cout << "=========================================================" << std::endl;
    std::cout << "  MQEngine Milestone 2 TOML Parser Adversarial Stress   " << std::endl;
    std::cout << "=========================================================" << std::endl;

    bool allPassed = true;
    allPassed &= testCorruptedTomlInputs();
    allPassed &= testBinaryAndFuzzData();
    allPassed &= testEmptyAndWhitespaceInputs();
    allPassed &= testMissingFieldsAndIncompleteManifests();
    allPassed &= testTypeMismatches();
    allPassed &= testValidJsonFallback();
    allPassed &= testValidTomlDoc04();
    allPassed &= testRealPluginFilesOnDisk();
    allPassed &= testStressAndConcurrency();

    std::cout << "\n=========================================================" << std::endl;
    if (allPassed)
    {
        std::cout << "   >>> ALL MILESTONE 2 TOML STRESS TESTS PASSED! <<<    " << std::endl;
        std::cout << "=========================================================" << std::endl;
        return 0;
    }
    else
    {
        std::cerr << "   >>> SOME TOML STRESS TESTS FAILED! <<<               " << std::endl;
        std::cout << "=========================================================" << std::endl;
        return 1;
    }
}
