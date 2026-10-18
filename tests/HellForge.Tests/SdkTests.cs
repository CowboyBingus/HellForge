using HellForge.Core;

static class SdkTests
{
    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        const string catalog = """{"format":1,"build":"1","entities":{"A/G-16 Gatling Sentry":{"id":"0xEF85D6CF58E31D70"}}}""";
        static string Components(string fields) =>
            """{"format":1,"build":"1","components":{"Fixture":{"component":"FixtureComponentData","index":9,"stride":16,"layout":"1","meaning":"m","applies":"now","fields":{""" +
            fields + "}}}}";
        static string Field(string name, int offset, string type, string more = "") =>
            $"\"{name}\":{{\"offset\":{offset},\"type\":\"{type}\",{more}\"meaning\":\"m\",\"verified\":true,\"source\":\"s\"}}";
        var round = Field("round", 4, "u32");
        static string Grouped(string fields, string group) =>
            Components(fields + "},\"groups\":{\"g\":{\"meaning\":\"m\",\"fields\":[" + group + "]}");

        test("The SDK headers and the loader's field table are generated from the committed mappings", () =>
        {
            var stale = SdkMappings.Sync(Directory.GetCurrentDirectory(), check: true);
            check(stale.Count == 0, "Run `hellforge sdk-headers --repo .`; stale: " + string.Join(", ", stale));
        });

        test("Generated records pad unresearched bytes and catalog names become the SDK's constant names", () =>
        {
            var mappings = SdkMappings.Parse(Components(round + "," + Field("rate", 8, "f32", "\"count\":2,").Replace("true", "false")), catalog);
            var files = mappings.Generate();
            var header = files["native/mod-sdk/hellforge_components.h"];
            check(header.Contains("uint8_t _unknown_00[4];") && header.Contains("uint32_t round;") && header.Contains("float rate[2];") &&
                header.Contains("(unverified)") && header.Contains("HF_ASSERT_SIZE(HfFixture, 16);") &&
                header.Contains("#define HfFixture_INFO \"FixtureComponentData\", 1u") && header.Contains("#define HF_TYPE_Fixture_rate HF_TYPE_F32"),
                "Record layout: " + header);
            check(files["native/mod-sdk/hellforge_content.h"].Contains("#define HF_ENTITY_AG16_GATLING_SENTRY 0xEF85D6CF58E31D70ull"), "Content constant.");
            check(files["native/mod-loader/sdk_fields.h"].Contains("{\"rate\", 8, 8, HF_TYPE_F32, HF_APPLIES_NOW, REF_NONE}") &&
                files["native/mod-sdk/hellforge_components.h"].Contains("[now?]"), "Loader table and timing tags.");
            var referenced = SdkMappings.Parse(Components(Field("blast", 4, "u32", "\"ref\":\"explosion\",")), catalog).Generate();
            check(referenced["native/mod-loader/sdk_fields.h"].Contains("{\"blast\", 4, 4, HF_TYPE_U32, HF_APPLIES_NOW, REF_EXPLOSION}") &&
                referenced["docs/sdk-reference.md"].Contains("(an explosion reference)"),
                "A reference field: its kind in the loader's table.");
            check(SdkMappings.Parse(Grouped(round, "\"round\""), catalog).Generate()["native/mod-sdk/hellforge_components.h"]
                .Contains("#define HF_GROUP_Fixture_g(X, ...) X(Fixture, round, __VA_ARGS__)"), "Group macro.");
            var table = SdkMappings.Parse(Components(round).Replace("\"index\":9", "\"row\":\"fixture_row\""), catalog).Generate();
            check(table["native/mod-sdk/hellforge_components.h"].Contains("(HfFixture*)api->fixture_row(...)") &&
                table["native/mod-sdk/hellforge_components.h"].Contains("HfFixture_INFO") && table["native/mod-loader/sdk_fields.h"].Contains("kSdkFieldsFixture, 1, 1, -1}"),
                "A table row is found by id: HF_FIELD reaches it and the loader's table marks it.");
            var kinds = SdkMappings.Parse(Components(round), """{"format":1,"build":"1","traits":{"Anti-Tank":{"id":"0x1000"}}}""").Generate();
            check(kinds["native/mod-sdk/hellforge_content.h"].Contains("#define HF_TRAIT_ANTITANK 0x00001000u") &&
                kinds["native/mod-sdk/hellforge_content.h"].Contains("#define HF_KIND_TRAIT 9u") &&
                kinds["native/mod-loader/sdk_content.h"].Contains("{HF_KIND_TRAIT, \"Anti-Tank\", 0x1000ull}"), "Traits are hex flags that find resolves.");
            check(SdkMappings.Constant("MG-206 Heavy Machine Gun") == "MG206_HEAVY_MACHINE_GUN" &&
                SdkMappings.Constant("Orbital 120mm HE Barrage") == "ORBITAL_120MM_HE_BARRAGE", "Constant names.");
        });

        test("Mappings refuse overlapping, out-of-record, misnamed and unknown fields, bad groups, SDK type names and duplicate constants", () =>
        {
            foreach (var fields in new[]
            {
                round + "," + Field("over", 6, "u16"),
                Field("tail", 12, "u64"),
                Field("odd", 2, "u32"),
                Field("_unknown_04", 4, "u32"),
                Field("bad name", 4, "u32"),
                Field("kind", 4, "u24"),
                Field("extra", 4, "u32", "\"extra\":1,"),
                Field("rate", 4, "f32", "\"ref\":\"explosion\","),
                Field("other", 4, "u32", "\"ref\":\"sound\","),
            }) reject(() => SdkMappings.Parse(Components(fields), catalog));
            foreach (var group in new[] { "\"nope\"", "\"round\",\"round\"", "" }) reject(() => SdkMappings.Parse(Grouped(round, group), catalog));
            reject(() => SdkMappings.Parse(Components(round).Replace("\"Fixture\":", "\"Weapon\":"), catalog));
            reject(() => SdkMappings.Parse(Components(round).Replace("\"applies\":\"now\"", "\"applies\":\"later\""), catalog));
            reject(() => SdkMappings.Parse(Components(round), """{"format":1,"build":"1","tags":{"A":{"id":"1"},"a":{"id":"2"}}}"""));
            reject(() => SdkMappings.Parse(Components(round), """{"format":1,"build":"2"}"""));
            reject(() => SdkMappings.Parse(Components(round), """{"format":1,"build":"1","items":{"Wide":{"id":"0x100000000"}}}"""));
        });
    }
}
