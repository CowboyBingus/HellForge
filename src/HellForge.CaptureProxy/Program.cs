// Sync Inventory's capture: reads the player's own HELLDIVERS 2 inventory and progression from the live API while the
// normal game logs in (HellForge.Core InventorySync starts it elevated from the launcher).
//
// It maps the API host to 127.0.0.1 in the hosts file, trusts a per-run CA in LocalMachine\Root (with a CRL served on
// loopback, since Schannel checks revocation), terminates the game's TLS and forwards every request unchanged to the real
// API by IP. With the session the login returns it reads GET /api/Progression and GET /api/Progression/inventory, writes
// them to --progression and --output, removes the hosts line and the CA, and exits 0 (1 when nothing was captured before
// the timeout). Nothing else is kept: the log holds methods, the first two path segments, status codes and sizes, never
// bodies, query strings, header values or the session.
//
//   --cleanup      only remove a hosts line and CA left by a run that was killed
//   --no-hosts --no-ca --loopback --listen-port 8443 --real-ip <ip>   local plumbing test, no system changes

using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Numerics;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;

const string HostsMarker = "# HellForge-CaptureProxy";
const string CaName = "HellForge Capture CA";
const string LoginPath = "/api/Account/Login", ProgressionPath = "/api/Progression", InventoryPath = "/api/Progression/inventory";
var hostsPath = Path.Combine(Environment.SystemDirectory, "drivers", "etc", "hosts");

var host = "api.live.prod.thehelldiversgame.com";
var listenPort = 443;
List<IPAddress>? fixedUpstream = null;
var outputPath = Path.Combine(AppContext.BaseDirectory, "real-inventory.json");
string? progressionPath = null;
var evidenceDir = Path.Combine(AppContext.BaseDirectory, "capture");
bool manageHosts = true, manageCa = true, loopbackOnly = false, cleanupOnly = false;
var timeoutMin = 10;

try
{
    for (var i = 0; i < args.Length; i++)
    {
        string Next() => i + 1 < args.Length ? args[++i] : throw new ArgumentException(args[i] + " needs a value.");
        switch (args[i])
        {
            case "--host": host = Next(); break;
            case "--listen-port": listenPort = int.Parse(Next(), CultureInfo.InvariantCulture); break;
            case "--real-ip": fixedUpstream = [IPAddress.Parse(Next())]; break;
            case "--output": outputPath = Next(); break;
            case "--progression": progressionPath = Next(); break;
            case "--evidence": evidenceDir = Next(); break;
            case "--timeout-min": timeoutMin = Math.Max(1, int.Parse(Next(), CultureInfo.InvariantCulture)); break;
            case "--no-hosts": manageHosts = false; break;
            case "--no-ca": manageCa = false; break;
            case "--loopback": loopbackOnly = true; break;
            case "--cleanup": cleanupOnly = true; break;
            case "--help":
                Console.WriteLine("HellForge.CaptureProxy [--output <file>] [--progression <file>] [--evidence <dir>] [--timeout-min <n>]");
                Console.WriteLine("  [--host <name>] [--listen-port <n>] [--real-ip <ip>] [--no-hosts] [--no-ca] [--loopback] | --cleanup");
                return 0;
            default: throw new ArgumentException("Unknown argument: " + args[i]);
        }
    }
}
catch (Exception error) when (error is ArgumentException or FormatException or OverflowException)
{
    Console.Error.WriteLine(error.Message);
    return 2;
}

if (cleanupOnly)
{
    var removed = Step("hosts cleanup", RemoveHosts) & Step("CA cleanup", () => RemoveCas(null));
    return removed ? 0 : 1;
}

try { Directory.CreateDirectory(evidenceDir); }
catch (Exception error) when (error is IOException or UnauthorizedAccessException)
{
    Console.Error.WriteLine("Cannot use " + evidenceDir + ": " + error.Message);
    return 1;
}
var statePath = Path.Combine(evidenceDir, "capture-state.json");
// The sync folder stays private to this user.
Run("icacls", $"\"{evidenceDir}\" /inheritance:r /grant:r \"{System.Security.Principal.WindowsIdentity.GetCurrent().Name}:(OI)(CI)F\"");

// Resolve the real host before the hosts line points it at loopback; clear a line an earlier killed run left first.
if (manageHosts && !Step("hosts cleanup", RemoveHosts)) return 1;
List<IPAddress> upstreamIps;
try
{
    upstreamIps = fixedUpstream ?? [.. Dns.GetHostAddresses(host).Where(address => address.AddressFamily == AddressFamily.InterNetwork)];
}
catch (SocketException error)
{
    Log("could not resolve " + host + ": " + error.SocketErrorCode);
    return 1;
}
if (upstreamIps.Count == 0) { Log("no IPv4 address for " + host); return 1; }
Log("upstream " + string.Join(", ", upstreamIps) + ":443");

string? caThumbprint = null;
X509Certificate2 serverCert;
if (manageCa)
{
    var crlListener = new TcpListener(IPAddress.Loopback, 0);
    crlListener.Start();
    var cdp = EncodeCdp($"http://127.0.0.1:{((IPEndPoint)crlListener.LocalEndpoint).Port}/ca-{Guid.NewGuid():N}.crl");
    using var caKey = RSA.Create(2048);
    var caRequest = new CertificateRequest("CN=" + CaName + ", O=HellForge", caKey, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
    caRequest.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
    caRequest.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign | X509KeyUsageFlags.CrlSign, true));
    caRequest.CertificateExtensions.Add(new X509Extension("2.5.29.31", cdp, false));
    using var ca = caRequest.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
    if (!Step("CA cleanup", () => RemoveCas(null))) return 1;
    using (var store = new X509Store(StoreName.Root, StoreLocation.LocalMachine))
    {
        store.Open(OpenFlags.ReadWrite);
        using var publicCa = X509CertificateLoader.LoadCertificate(ca.Export(X509ContentType.Cert));
        store.Add(publicCa);
    }
    caThumbprint = ca.Thumbprint;
    Log("CA installed");
    serverCert = MakeLeaf(ca, host, cdp);
    StartCrlServer(crlListener, new CertificateRevocationListBuilder().Build(ca, BigInteger.One, DateTimeOffset.UtcNow.AddDays(1),
        HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1, DateTimeOffset.UtcNow.AddDays(-1)));
}
else serverCert = MakeLeaf(null, host, null);

var listener = new TcpListener(loopbackOnly ? IPAddress.Loopback : IPAddress.Any, listenPort);
var cleaned = 0;
void Cleanup()
{
    if (Interlocked.Exchange(ref cleaned, 1) == 1) return;
    listener.Stop();
    if (manageHosts) Step("hosts cleanup", RemoveHosts);
    if (caThumbprint != null) Step("CA cleanup", () => RemoveCas(caThumbprint));
    Step("state cleanup", () => File.Delete(statePath));
}
Console.CancelKeyPress += (_, e) => { e.Cancel = true; Cleanup(); Environment.Exit(1); };
AppDomain.CurrentDomain.ProcessExit += (_, _) => Cleanup();

using var handler = new SocketsHttpHandler
{
    // A plain TCP stream to the real API by IP (the hosts line points the name at us); the handler does TLS against the
    // real certificate. Every address is tried before giving up.
    ConnectCallback = async (_, token) =>
    {
        Exception? last = null;
        foreach (var ip in upstreamIps)
        {
            var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            try
            {
                using var connectTimeout = CancellationTokenSource.CreateLinkedTokenSource(token);
                connectTimeout.CancelAfter(TimeSpan.FromSeconds(6));
                await socket.ConnectAsync(new IPEndPoint(ip, 443), connectTimeout.Token);
                return new NetworkStream(socket, ownsSocket: true);
            }
            catch (Exception error) when (error is SocketException or OperationCanceledException)
            {
                last = error;
                socket.Dispose();
            }
        }
        throw new IOException("no upstream address reachable: " + last?.Message);
    },
    AllowAutoRedirect = false,
    UseCookies = false,
    AutomaticDecompression = DecompressionMethods.All,
    ConnectTimeout = TimeSpan.FromSeconds(15)
};
using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(30) };

var captured = 0;
using var stop = new CancellationTokenSource(TimeSpan.FromMinutes(timeoutMin));
try
{
    if (manageHosts) AddHosts();
    listener.Start();
    Log($"listening on port {listenPort}");
    File.WriteAllText(statePath, JsonSerializer.Serialize(new { pid = Environment.ProcessId, host, hosts = manageHosts, ca = caThumbprint != null }));
    while (!stop.IsCancellationRequested)
    {
        TcpClient client;
        try { client = await listener.AcceptTcpClientAsync(stop.Token); }
        catch (OperationCanceledException) { break; }
        _ = Task.Run(() => Handle(client));
    }
}
finally
{
    Cleanup();
}
Log(captured == 1 ? "capture complete" : "nothing captured");
return captured == 1 ? 0 : 1;

async Task Handle(TcpClient client)
{
    using var connection = client;
    using var ssl = new SslStream(client.GetStream(), leaveInnerStreamOpen: false);
    try
    {
        await ssl.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
        {
            ServerCertificate = serverCert,
            EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13,
            ApplicationProtocols = [SslApplicationProtocol.Http11]
        });
    }
    catch (Exception error) when (error is AuthenticationException or IOException)
    {
        // A client that pins the API's certificate fails here.
        Log("TLS handshake failed: " + error.GetType().Name + " 0x" + (error.InnerException ?? error).HResult.ToString("X8", CultureInfo.InvariantCulture));
        return;
    }
    try
    {
        if (await ReadRequest(ssl) is not var (method, target, headers, body)) return;
        var path = target.Split('?')[0];
        using var upstream = new HttpRequestMessage(new HttpMethod(method), $"https://{host}{target}");
        foreach (var (name, value) in headers)
            if (name is not ("Host" or "Connection" or "Content-Length" or "Content-Type" or "Transfer-Encoding" or "Accept-Encoding" or "Expect" or "Proxy-Connection" or "Upgrade" or "TE"))
                upstream.Headers.TryAddWithoutValidation(name, value);
        if (body.Length > 0 || method is "POST" or "PUT")
        {
            upstream.Content = new ByteArrayContent(body);
            if (headers.TryGetValue("Content-Type", out var contentType)) upstream.Content.Headers.TryAddWithoutValidation("Content-Type", contentType);
        }
        using var response = await http.SendAsync(upstream);
        var responseBody = await response.Content.ReadAsByteArrayAsync();
        Log($"{method} {Shorten(path)} {(int)response.StatusCode} {responseBody.Length} bytes");
        if (response.IsSuccessStatusCode) await Capture(path, responseBody);

        var head = new StringBuilder($"HTTP/1.1 {(int)response.StatusCode} {response.ReasonPhrase}\r\n");
        foreach (var header in response.Headers.Concat(response.Content.Headers))
        {
            if (header.Key is "Transfer-Encoding" or "Connection" or "Keep-Alive" or "Content-Length" or "Content-Encoding") continue;
            foreach (var value in header.Value) head.Append(header.Key).Append(": ").Append(value).Append("\r\n");
        }
        head.Append("Content-Length: ").Append(responseBody.Length).Append("\r\nConnection: close\r\n\r\n");
        await ssl.WriteAsync(Encoding.ASCII.GetBytes(head.ToString()));
        await ssl.WriteAsync(responseBody);
        await ssl.FlushAsync();
    }
    catch (Exception error) when (error is IOException or HttpRequestException or TaskCanceledException or InvalidOperationException or FormatException)
    {
        Log("request failed: " + error.GetType().Name);
    }
    finally
    {
        // Only once this response is on its way, so the game's login completes before the redirect goes.
        if (Volatile.Read(ref captured) == 1) stop.Cancel();
    }
}

// A failure here is logged and never keeps the response from the game.
async Task Capture(string path, byte[] body)
{
    try
    {
        if (path.Equals(LoginPath, StringComparison.OrdinalIgnoreCase)) await ReadAccount(body);
        else if (path.Equals(ProgressionPath, StringComparison.OrdinalIgnoreCase)) SaveProgression(body);
        else if (path.Equals(InventoryPath, StringComparison.OrdinalIgnoreCase)) SaveInventory(body);
    }
    catch (Exception error) when (error is IOException or UnauthorizedAccessException or HttpRequestException or TaskCanceledException)
    {
        Log("capture failed: " + error.GetType().Name + " " + error.Message);
    }
}

// The login's session reads what the game reads next: progression first, since the capture ends with the inventory.
async Task ReadAccount(byte[] loginResponse)
{
    string? session;
    try
    {
        using var login = JsonDocument.Parse(loginResponse);
        session = login.RootElement.TryGetProperty("sessionId", out var id) ? id.GetString()
            : login.RootElement.TryGetProperty("sessionToken", out var token) ? token.GetString() : null;
    }
    catch (JsonException)
    {
        Log("login response is not JSON");
        return;
    }
    if (string.IsNullOrEmpty(session)) { Log("login response has no session"); return; }
    if (progressionPath != null && await Get(ProgressionPath, session) is { } progression) SaveProgression(progression);
    if (await Get(InventoryPath, session) is { } inventory) SaveInventory(inventory);
}

async Task<byte[]?> Get(string path, string session)
{
    using var request = new HttpRequestMessage(HttpMethod.Get, $"https://{host}{path}");
    request.Headers.TryAddWithoutValidation("Authorization", "session " + session);
    using var response = await http.SendAsync(request);
    var body = await response.Content.ReadAsByteArrayAsync();
    Log($"GET {path} {(int)response.StatusCode} {body.Length} bytes (login session)");
    return response.IsSuccessStatusCode ? body : null;
}

void SaveProgression(byte[] body)
{
    if (progressionPath == null) return;
    if (!IsJsonObject(body, null)) { Log("progression response is not an object"); return; }
    WriteAtomically(progressionPath, body);
    Log("progression saved");
}

void SaveInventory(byte[] body)
{
    if (!IsJsonObject(body, "items")) { Log("inventory response has no items list"); return; }
    if (Volatile.Read(ref captured) == 1) return;
    WriteAtomically(outputPath, body);
    Volatile.Write(ref captured, 1);
    Log("inventory saved");
}

static bool IsJsonObject(byte[] body, string? requiredArray)
{
    try
    {
        using var document = JsonDocument.Parse(body);
        return document.RootElement.ValueKind == JsonValueKind.Object && (requiredArray == null ||
            document.RootElement.TryGetProperty(requiredArray, out var array) && array.ValueKind == JsonValueKind.Array);
    }
    catch (JsonException) { return false; }
}

static void WriteAtomically(string path, byte[] body)
{
    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
    File.WriteAllBytes(path + ".tmp", body);
    File.Move(path + ".tmp", path, overwrite: true);
}

// "/api/FriendsV2/Recent" stays, "/api/Proxy/<id>/<id>" becomes "/api/Proxy/...": later segments can carry account ids.
static string Shorten(string path)
{
    var segments = path.Split('/', StringSplitOptions.RemoveEmptyEntries);
    return segments.Length <= 3 && path is LoginPath or ProgressionPath or InventoryPath ? path
        : "/" + string.Join('/', segments.Take(2)) + (segments.Length > 2 ? "/..." : "");
}

static async Task<(string Method, string Target, Dictionary<string, string> Headers, byte[] Body)?> ReadRequest(SslStream ssl)
{
    var buffer = new MemoryStream();
    var chunk = new byte[8192];
    var headerEnd = -1;
    while (headerEnd < 0 && buffer.Length < 65536)
    {
        var n = await ssl.ReadAsync(chunk);
        if (n == 0) return null;
        buffer.Write(chunk, 0, n);
        headerEnd = buffer.GetBuffer().AsSpan(0, (int)buffer.Length).IndexOf("\r\n\r\n"u8);
    }
    if (headerEnd < 0) return null;
    var lines = Encoding.ASCII.GetString(buffer.GetBuffer(), 0, headerEnd).Split("\r\n");
    var first = lines[0].Split(' ');
    if (first.Length < 3) return null;
    var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    foreach (var line in lines.Skip(1))
    {
        var colon = line.IndexOf(':');
        if (colon > 0) headers[line[..colon].Trim()] = line[(colon + 1)..].Trim();
    }
    if (headers.ContainsKey("Transfer-Encoding")) return null;
    if (headers.TryGetValue("Expect", out var expect) && expect.Contains("100-continue", StringComparison.OrdinalIgnoreCase))
        await ssl.WriteAsync("HTTP/1.1 100 Continue\r\n\r\n"u8.ToArray());
    var length = headers.TryGetValue("Content-Length", out var text) && int.TryParse(text, out var parsed) && parsed is >= 0 and <= 1 << 20 ? parsed : 0;
    var body = new byte[length];
    var read = Math.Min((int)buffer.Length - (headerEnd + 4), length);
    Array.Copy(buffer.GetBuffer(), headerEnd + 4, body, 0, read);
    while (read < length)
    {
        var n = await ssl.ReadAsync(body.AsMemory(read, length - read));
        if (n == 0) break;
        read += n;
    }
    return (first[0], first[1], headers, body);
}

void Log(string message)
{
    var line = DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture) + " " + message;
    Console.WriteLine(line);
    try { File.AppendAllText(Path.Combine(evidenceDir, "proxy.log"), line + Environment.NewLine); }
    catch (Exception error) when (error is IOException or UnauthorizedAccessException) { Console.Error.WriteLine("log write failed: " + error.Message); }
}

// Each cleanup step runs on its own, so a failed one never skips the next.
bool Step(string name, Action action)
{
    try
    {
        action();
        return true;
    }
    catch (Exception error) when (error is IOException or UnauthorizedAccessException or CryptographicException)
    {
        Log(name + " failed: " + error.Message);
        return false;
    }
}

static void Run(string file, string arguments)
{
    using var process = Process.Start(new ProcessStartInfo(file, arguments) { UseShellExecute = false, CreateNoWindow = true });
    process?.WaitForExit(8000);
}

void RemoveHosts()
{
    if (!File.Exists(hostsPath)) return;
    var lines = File.ReadAllLines(hostsPath);
    var kept = lines.Where(line => !line.Contains(HostsMarker, StringComparison.OrdinalIgnoreCase) &&
        !line.Trim().Equals("127.0.0.1 " + host, StringComparison.OrdinalIgnoreCase)).ToArray();
    if (kept.Length == lines.Length) return;
    File.WriteAllLines(hostsPath, kept);
    Run("ipconfig", "/flushdns");
    Log("hosts line removed");
}

void AddHosts()
{
    File.AppendAllLines(hostsPath, [HostsMarker, "127.0.0.1 " + host]);
    Run("ipconfig", "/flushdns");
}

// This run's CA by thumbprint, or with null every CA this tool installed.
void RemoveCas(string? thumbprint)
{
    foreach (var location in new[] { StoreLocation.LocalMachine, StoreLocation.CurrentUser })
    {
        using var store = new X509Store(StoreName.Root, location);
        store.Open(OpenFlags.ReadWrite);
        foreach (var certificate in store.Certificates)
        {
            if (thumbprint != null ? certificate.Thumbprint == thumbprint : certificate.Subject.Contains("CN=" + CaName, StringComparison.Ordinal))
            {
                store.Remove(certificate);
                Log("CA removed from " + location);
            }
            certificate.Dispose();
        }
    }
}

static void StartCrlServer(TcpListener listener, byte[] crl) => _ = Task.Run(async () =>
{
    var head = Encoding.ASCII.GetBytes($"HTTP/1.1 200 OK\r\nContent-Type: application/pkix-crl\r\nContent-Length: {crl.Length}\r\nConnection: close\r\n\r\n");
    while (true)
    {
        TcpClient client;
        try { client = await listener.AcceptTcpClientAsync(); }
        catch (Exception error) when (error is SocketException or ObjectDisposedException) { return; }
        _ = Task.Run(async () =>
        {
            using var connection = client;
            try
            {
                var stream = client.GetStream();
                if (await stream.ReadAsync(new byte[2048]) == 0) return;
                await stream.WriteAsync(head);
                await stream.WriteAsync(crl);
            }
            catch (IOException) { } // the client hung up; it asks again on its next handshake
        });
    }
});

static byte[] Tlv(byte tag, byte[] content)
{
    var length = content.Length;
    byte[] prefix = length < 0x80 ? [tag, (byte)length] : length <= 0xFF ? [tag, 0x81, (byte)length] : [tag, 0x82, (byte)(length >> 8), (byte)length];
    return [.. prefix, .. content];
}

// CRLDistributionPoints: SEQUENCE { DistributionPoint { [0] { [0] { [6] uri } } } }
static byte[] EncodeCdp(string url) => Tlv(0x30, Tlv(0x30, Tlv(0xA0, Tlv(0xA0, Tlv(0x86, Encoding.ASCII.GetBytes(url))))));

static X509Certificate2 MakeLeaf(X509Certificate2? issuer, string name, byte[]? cdp)
{
    using var key = RSA.Create(2048);
    var request = new CertificateRequest("CN=" + name, key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
    var san = new SubjectAlternativeNameBuilder();
    san.AddDnsName(name);
    request.CertificateExtensions.Add(san.Build());
    request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, false));
    request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
    request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection { new Oid("1.3.6.1.5.5.7.3.1") }, false));
    if (cdp != null) request.CertificateExtensions.Add(new X509Extension("2.5.29.31", cdp, false));
    var notBefore = DateTimeOffset.UtcNow.AddDays(-1);
    var notAfter = DateTimeOffset.UtcNow.AddDays(1);
    using var certificate = issuer == null ? request.CreateSelfSigned(notBefore, notAfter)
        : request.Create(issuer, notBefore, notAfter, RandomNumberGenerator.GetBytes(8)).CopyWithPrivateKey(key);
    // SslStream needs the key in a form Schannel can use; a PKCS#12 round trip gives it one.
    return X509CertificateLoader.LoadPkcs12(certificate.Export(X509ContentType.Pfx), null, X509KeyStorageFlags.Exportable);
}
