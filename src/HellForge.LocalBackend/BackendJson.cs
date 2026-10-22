using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace HellForge.LocalBackend;

// JSON the backend converts with generated code (it is compiled ahead of time, without reflection-based serialization).
[JsonSerializable(typeof(JsonNode))]
partial class BackendJson : JsonSerializerContext;
