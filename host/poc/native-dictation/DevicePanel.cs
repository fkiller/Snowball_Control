using System.Net;
using System.Net.Sockets;
using System.Text.Json;

namespace Snowball.Dictation;

/// <summary>Isolated laboratory panel owner; does not attach to a coding session.</summary>
internal sealed class DevicePanel : IDisposable
{
    private readonly UdpClient socket;
    private readonly IPEndPoint device;
    private readonly CancellationTokenSource lifetime = new();
    private readonly HashSet<int> pressed = [];
    private long sequence;
    internal event Action<int>? KeyDown;
    internal event Action<string>? Error;
    internal DateTime LastContact { get; private set; } = DateTime.MinValue;

    internal IPEndPoint LocalEndpoint => (IPEndPoint)socket.Client.LocalEndPoint!;
    internal DevicePanel(int port = 7701, IPEndPoint? target = null)
    {
        device = target ?? new IPEndPoint(IPAddress.Parse(Environment.GetEnvironmentVariable("SNOWBALL_DEVICE_IP") ?? "192.168.1.248"), 7701);
        socket = new UdpClient(AddressFamily.InterNetwork);
        try
        {
            socket.ExclusiveAddressUse = true;
            socket.Client.Bind(new IPEndPoint(target is null ? IPAddress.Any : IPAddress.Loopback, port));
        }
        catch { socket.Dispose(); throw; }
        _ = ReceiveAsync();
    }
    private async Task ReceiveAsync()
    {
        try
        {
            while (!lifetime.IsCancellationRequested)
            {
                var packet = await socket.ReceiveAsync(lifetime.Token);
                if (!packet.RemoteEndPoint.Equals(device) || packet.Buffer.Length > 512) continue;
                try
                {
                    using var json = JsonDocument.Parse(packet.Buffer);
                    var root = json.RootElement;
                    if (!root.TryGetProperty("type", out var type)) continue;
                    if (type.GetString() == "ping") { LastContact = DateTime.UtcNow; continue; }
                    if (type.GetString() != "key" || !root.TryGetProperty("keyId", out var key) ||
                        !key.TryGetInt32(out var id) || id < 1 || id > 20 ||
                        !root.TryGetProperty("isDown", out var down) ||
                        down.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) continue;
                    LastContact = DateTime.UtcNow;
                    if (!down.GetBoolean()) pressed.Remove(id);
                    else if (pressed.Add(id)) KeyDown?.Invoke(id);
                }
                catch (JsonException) { /* Ignore malformed laboratory packets. */ }
                catch (InvalidOperationException) { }
            }
        }
        catch (OperationCanceledException) { }
        catch (ObjectDisposedException) { }
        catch (Exception error) { Error?.Invoke(error.Message); }
    }
    internal void Publish(string phase, bool canStart, bool canStop, bool canCancel)
    {
        var keys = Enumerable.Range(1, 20).Select(id =>
        {
            string top = "", main = ""; bool enabled = false;
            switch (id)
            {
                case 17: top = "MACHINE"; main = "THIS PC"; break;
                case 13: top = "HARNESS"; main = "Codex"; break;
                case 9: top = "PROJECT"; main = "VOICE TEST"; break;
                case 5: top = "SESSION"; main = "DRAFT PAD"; break;
                case 20: top = "VOICE"; main = canStop ? "Finish" : canStart ? "Talk" : "Check app"; enabled = canStart || canStop; break;
                case 16: top = "VOICE"; main = canStop ? "Finish" : ""; enabled = canStop; break;
                case 4: top = "DRAFT"; main = "Cancel"; enabled = canCancel; break;
            }
            return new { id, top, main, sub = "", flags = enabled ? 1 : 8 };
        }).ToArray();
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            type = "v2_sync", seq = ++sequence, viewMode = "session",
            topTitle = "Native voice test", topSubtitle = phase,
            topBody = "MK20 MIC3 -> Codex -> Windows draft\nTalk: start / Finish: transcribe\nCancel: discard this capture\nReview and edit in the Windows app.\nNo coding task submission.",
            topScroll = 0, volume = 75, isMuted = false, keys
        });
        socket.Send(bytes, bytes.Length, device);
    }
    public void Dispose() { lifetime.Cancel(); socket.Dispose(); lifetime.Dispose(); }

    internal static async Task<object> ProbeAsync()
    {
        using var device = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        using var wrongDevice = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        using var panel = new DevicePanel(0, (IPEndPoint)device.Client.LocalEndPoint!);
        var events = new System.Collections.Concurrent.ConcurrentQueue<int>();
        panel.KeyDown += events.Enqueue;
        async Task Send(UdpClient sender, string json) => await sender.SendAsync(System.Text.Encoding.UTF8.GetBytes(json), panel.LocalEndpoint);
        await Send(wrongDevice, "{\"type\":\"key\",\"keyId\":4,\"isDown\":true}");
        await Send(device, "{\"type\":\"key\",\"keyId\":20,\"isDown\":true}");
        await Send(device, "{\"type\":\"key\",\"keyId\":20,\"isDown\":true}");
        await Send(device, "{\"type\":\"key\",\"keyId\":20,\"isDown\":false}");
        await Send(device, "{\"type\":\"key\",\"keyId\":20,\"isDown\":true}");
        await Send(device, "{\"type\":\"key\",\"keyId\":999,\"isDown\":true}");
        await Send(device, "invalid-json");
        // Sentinel confirms all preceding datagrams from the same peer were processed.
        await Send(device, "{\"type\":\"key\",\"keyId\":16,\"isDown\":true}");
        var until = DateTime.UtcNow.AddSeconds(2);
        while (events.Count < 3 && DateTime.UtcNow < until) await Task.Delay(10);
        panel.Publish("Ready", true, false, false);
        var sync = await device.ReceiveAsync().WaitAsync(TimeSpan.FromSeconds(2));
        using var parsed = JsonDocument.Parse(sync.Buffer);
        bool passed = events.ToArray().SequenceEqual(new[] { 20, 20, 16 }) &&
            parsed.RootElement.GetProperty("keys").GetArrayLength() == 20 && sync.Buffer.Length < 4096;
        if (!passed) throw new IOException("Panel protocol regression failed.");
        return new { pass = true, keyEvents = events.ToArray(), syncBytes = sync.Buffer.Length,
            scope = "loopback only: source endpoint, duplicate downs, release rearm, malformed input, 20-key sync; no hardware actions" };
    }
}
