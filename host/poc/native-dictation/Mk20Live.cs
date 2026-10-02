using System.Buffers.Binary;
using System.Diagnostics;
using System.Net.Sockets;
using System.Security.Cryptography;

namespace Snowball.Dictation;

internal sealed class Mk20Live : IAsyncDisposable
{
    private readonly string adb = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Temp", "Codex-MK20-ADB", "platform-tools", "adb.exe");
    private readonly string token = Convert.ToHexString(RandomNumberGenerator.GetBytes(16)).ToLowerInvariant();
    private Process? helper;
    private TcpClient? client;
    private string? localPort;
    private Task? reader;
    private Task<string>? helperError;
    private readonly CancellationTokenSource lifetime = new(TimeSpan.FromSeconds(70));
    private readonly TaskCompletionSource firstFrame = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal long BytesReceived { get; private set; }
    internal int Peak { get; private set; }
    internal Task Completion => reader ?? Task.CompletedTask;

    private ProcessStartInfo Info(params string[] args)
    {
        var p = new ProcessStartInfo(adb) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardError = true, RedirectStandardOutput = true };
        p.ArgumentList.Add("-s");
        p.ArgumentList.Add(Environment.GetEnvironmentVariable("SNOWBALL_DEVICE_ADB") ?? "192.168.1.248:5555");
        foreach (var arg in args) p.ArgumentList.Add(arg);
        return p;
    }
    private async Task<string> RunAsync(params string[] args)
    {
        using var p = Process.Start(Info(args)) ?? throw new IOException("ADB를 시작하지 못했습니다.");
        var stdout = p.StandardOutput.ReadToEndAsync(); var stderr = p.StandardError.ReadToEndAsync();
        try { await p.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5)); }
        catch { p.Kill(entireProcessTree: true); throw; }
        if (p.ExitCode != 0) throw new IOException("ADB 연결 오류: " + await stderr);
        return (await stdout).Trim();
    }
    internal async Task StartAsync(Action<byte[]> onFrame)
    {
        if (helper is not null) throw new InvalidOperationException("스트림이 이미 시작됐습니다.");
        int port = RandomNumberGenerator.GetInt32(19000, 29000);
        localPort = await RunAsync("forward", "tcp:0", "tcp:" + port);
        if (!int.TryParse(localPort, out var forwardPort)) throw new IOException("ADB 포트 응답 오류");
        helper = Process.Start(Info("shell", $"/mnt/SDCARD/snowball-pcm-poc {port} {token} 3"))
            ?? throw new IOException("MK20 스트림 도우미를 시작하지 못했습니다.");
        helperError = helper.StandardError.ReadToEndAsync(); _ = helper.StandardOutput.ReadToEndAsync();
        await Task.Delay(350, lifetime.Token);
        client = new TcpClient();
        await client.ConnectAsync("127.0.0.1", forwardPort, lifetime.Token);
        var stream = client.GetStream();
        await stream.WriteAsync(System.Text.Encoding.ASCII.GetBytes(token + "\n"), lifetime.Token);
        var header = new byte[8];
        await stream.ReadExactlyAsync(header, lifetime.Token).AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        if (!header.AsSpan(0, 4).SequenceEqual("SBP1"u8) || BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4)) != 16000)
            throw new IOException("MK20 PCM 프로토콜 버전/주파수가 다릅니다.");
        reader = ReadAsync(stream, onFrame);
        await firstFrame.Task.WaitAsync(TimeSpan.FromSeconds(5));
    }
    private async Task ReadAsync(NetworkStream stream, Action<byte[]> onFrame)
    {
        try
        {
            var size = new byte[4];
            while (true)
            {
                await stream.ReadExactlyAsync(size, lifetime.Token);
                uint count = BinaryPrimitives.ReadUInt32LittleEndian(size);
                if (count == 0)
                {
                    await stream.ReadExactlyAsync(size, lifetime.Token);
                    if (BinaryPrimitives.ReadUInt32LittleEndian(size) != 0) throw new IOException("MK20 마이크가 오류로 종료됐습니다.");
                    if (BytesReceived == 0) throw new IOException("MK20 음성 데이터가 없습니다.");
                    return;
                }
                if (count > 4096 || count % 2 != 0 || BytesReceived + count > 16000 * 2 * 61)
                    throw new IOException("MK20 음성 프레임 범위를 벗어났습니다.");
                var data = new byte[count]; await stream.ReadExactlyAsync(data, lifetime.Token);
                for (int i = 0; i < data.Length; i += 2)
                    Peak = Math.Max(Peak, Math.Abs((int)BinaryPrimitives.ReadInt16LittleEndian(data.AsSpan(i))));
                onFrame(data); BytesReceived += count; firstFrame.TrySetResult();
            }
        }
        catch (Exception ex)
        {
            string detail = "";
            if (helperError is not null)
            {
                try { detail = (await helperError.WaitAsync(TimeSpan.FromMilliseconds(400))).Trim(); }
                catch { }
            }
            var failure = new IOException($"MK20 스트림 종료 ({BytesReceived} bytes). " +
                (detail.Length > 0 ? detail : ex.Message), ex);
            firstFrame.TrySetException(failure); throw failure;
        }
    }
    internal async Task StopAsync()
    {
        if (client is not null && reader is not null && !reader.IsCompleted)
        {
            await client.GetStream().WriteAsync("STOP\n"u8.ToArray());
            await reader.WaitAsync(TimeSpan.FromSeconds(4));
        }
        else if (reader is not null) await reader;
    }
    public async ValueTask DisposeAsync()
    {
        try { await StopAsync(); } catch { }
        lifetime.Cancel(); client?.Dispose();
        if (reader is not null) { try { await reader; } catch { } }
        if (helper is not null)
        {
            try { await helper.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(4)); }
            catch { helper.Kill(entireProcessTree: true); }
            helper.Dispose();
        }
        if (localPort is not null) { try { await RunAsync("forward", "--remove", "tcp:" + localPort); } catch { } }
        lifetime.Dispose();
    }
}
