using System.Reflection.PortableExecutable;
using System.Text;

namespace HellForge.SdkShimGen;

public sealed record PeExport(string Name, uint Ordinal);

// Export and import tables of a PE image, read directly from its data directories.
public static class PeTables
{
    public static IReadOnlyList<PeExport> Exports(string path)
    {
        using var pe = new PEReader(File.OpenRead(path));
        var directory = pe.PEHeaders.PEHeader!.ExportTableDirectory;
        if (directory.Size == 0) return [];
        var at = directory.RelativeVirtualAddress;
        var ordinalBase = U32(pe, at + 16);
        var nameCount = U32(pe, at + 24);
        var names = (int)U32(pe, at + 32);
        var ordinals = (int)U32(pe, at + 36);
        var exports = new List<PeExport>();
        for (var i = 0; i < nameCount; i++)
            exports.Add(new(CString(pe, (int)U32(pe, names + i * 4)), ordinalBase + U16(pe, ordinals + i * 2)));
        return exports;
    }

    // Exports whose address lies in the export directory are forwarders: "module.name" strings instead of code.
    public static IReadOnlyDictionary<string, string> Forwarders(string path)
    {
        using var pe = new PEReader(File.OpenRead(path));
        var directory = pe.PEHeaders.PEHeader!.ExportTableDirectory;
        var forwarders = new Dictionary<string, string>(StringComparer.Ordinal);
        if (directory.Size == 0) return forwarders;
        foreach (var (name, rva) in NamedAddresses(pe, directory.RelativeVirtualAddress))
            if (rva >= directory.RelativeVirtualAddress && rva < directory.RelativeVirtualAddress + directory.Size)
                forwarders[name] = CString(pe, (int)rva);
        return forwarders;
    }

    // Exports that point into a section without code: data, which a forwarder must mark DATA.
    public static IReadOnlySet<string> DataExports(string path)
    {
        using var pe = new PEReader(File.OpenRead(path));
        var directory = pe.PEHeaders.PEHeader!.ExportTableDirectory;
        var data = new HashSet<string>(StringComparer.Ordinal);
        if (directory.Size == 0) return data;
        foreach (var (name, rva) in NamedAddresses(pe, directory.RelativeVirtualAddress))
        {
            var section = pe.PEHeaders.SectionHeaders.FirstOrDefault(s => rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.SizeOfRawData));
            if ((section.SectionCharacteristics & SectionCharacteristics.ContainsCode) == 0) data.Add(name);
        }
        return data;
    }

    static IEnumerable<(string Name, uint Rva)> NamedAddresses(PEReader pe, int at)
    {
        var nameCount = U32(pe, at + 24);
        var functions = (int)U32(pe, at + 28);
        var names = (int)U32(pe, at + 32);
        var ordinals = (int)U32(pe, at + 36);
        for (var i = 0; i < nameCount; i++)
            yield return (CString(pe, (int)U32(pe, names + i * 4)), U32(pe, functions + U16(pe, ordinals + i * 2) * 4));
    }

    // Names of the DLLs the image imports.
    public static IReadOnlyList<string> ImportedModules(string path)
    {
        using var pe = new PEReader(File.OpenRead(path));
        var directory = pe.PEHeaders.PEHeader!.ImportTableDirectory;
        var modules = new List<string>();
        for (var at = directory.RelativeVirtualAddress; directory.Size != 0; at += 20)
        {
            var name = U32(pe, at + 12);
            if (name == 0) break;
            modules.Add(CString(pe, (int)name));
        }
        return modules;
    }

    static uint U32(PEReader pe, int rva) => BitConverter.ToUInt32(pe.GetSectionData(rva).GetContent(0, 4).AsSpan());
    static ushort U16(PEReader pe, int rva) => BitConverter.ToUInt16(pe.GetSectionData(rva).GetContent(0, 2).AsSpan());

    static string CString(PEReader pe, int rva)
    {
        var bytes = pe.GetSectionData(rva).GetContent();
        var length = bytes.IndexOf((byte)0);
        return Encoding.ASCII.GetString(bytes.AsSpan(0, length < 0 ? bytes.Length : length));
    }
}
