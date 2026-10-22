using System.Text.Json.Serialization;

namespace HellForge.Session;

// The JSON the session runner writes from typed results, generated at build time (NativeAOT has no reflection-based
// serialization). The memory edits' results keep their public fields as the session files have always named them.
[JsonSourceGenerationOptions(WriteIndented = true, IncludeFields = true)]
[JsonSerializable(typeof(HellForgeHostEarlyResult))]
[JsonSerializable(typeof(HellForgeLocalRoutingResult))]
[JsonSerializable(typeof(SortedDictionary<string, string>))]
partial class SessionJson : JsonSerializerContext;
