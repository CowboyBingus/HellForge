using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;

// Synthetic connectivity control; no game services, credentials or memory access.
public static class OfflineControlNetworkProbe {
    static int External(AddressFamily family, string address) {
        using(var socket = new Socket(family, SocketType.Stream, ProtocolType.Tcp)) {
            try {
                var task = socket.ConnectAsync(IPAddress.Parse(address), 443);
                if(!task.Wait(2500)) return -1;
                return 0;
            } catch(AggregateException error) {
                var socketError = error.GetBaseException() as SocketException;
                return socketError == null ? -2 : socketError.ErrorCode;
            } catch(SocketException error) { return error.ErrorCode; }
        }
    }
    public static int Main(string[] args) {
        int port;
        if(args.Length != 2 || !int.TryParse(args[0], out port) || port < 1 || port > 65535) return 2;
        bool local = false;
        using(var client = new TcpClient(AddressFamily.InterNetwork)) {
            try {
                var task = client.ConnectAsync(IPAddress.Loopback, port);
                if(task.Wait(2500)) {
                    client.ReceiveTimeout = 2500; client.SendTimeout = 2500;
                    using(var stream = client.GetStream()) {
                        var request = Encoding.ASCII.GetBytes("GET /config/locator.json HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
                        stream.Write(request, 0, request.Length);
                        var buffer = new byte[2048];
                        int read = stream.Read(buffer, 0, buffer.Length);
                        local = Encoding.ASCII.GetString(buffer, 0, read).StartsWith("HTTP/1.1 200 ");
                    }
                }
            } catch(Exception error) {
                if(!(error is SocketException) && !(error is IOException) && !(error is AggregateException)) throw;
            }
        }
        int v4 = External(AddressFamily.InterNetwork, "192.0.2.1");
        int v6 = External(AddressFamily.InterNetworkV6, "2001:db8::1");
        File.WriteAllText(args[1], "{\"loopbackHttpPassed\":" + (local ? "true" : "false") +
            ",\"externalIpv4TcpError\":" + v4 + ",\"externalIpv6TcpError\":" + v6 + "}");
        return local && v4 == 10013 ? 0 : 1;
    }
}
