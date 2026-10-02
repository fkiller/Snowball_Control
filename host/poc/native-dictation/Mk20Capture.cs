using System.Diagnostics;

namespace Snowball.Dictation;

// Lab transport: existing ADB auth/connection, unique recording files and owned PID.
// Raw microphone bytes never traverse the legacy shell's terminal encoding.
internal sealed class Mk20Capture : IAsyncDisposable
{
    private readonly string adb = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Temp", "Codex-MK20-ADB", "platform-tools", "adb.exe");
    private readonly string device;
    private readonly string stem = "/tmp/snowball-poc-" + Guid.NewGuid().ToString("N");
    private Process? recorder;
    private Task<string>? errors;
    private Task<string>? output;
    private bool completed;
    internal Mk20Capture(string device = "192.168.1.248:5555") =>
        this.device = Environment.GetEnvironmentVariable("SNOWBALL_DEVICE_ADB") ?? device;

    private ProcessStartInfo Info(params string[] arguments)
    {
        var info = new ProcessStartInfo(adb) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true };
        info.ArgumentList.Add("-s"); info.ArgumentList.Add(device);
        foreach (var argument in arguments) info.ArgumentList.Add(argument);
        return info;
    }

    private async Task<string> RunAsync(params string[] arguments)
    {
        using var process = Process.Start(Info(arguments)) ?? throw new IOException("ADB를 시작하지 못했습니다.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        try { await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(8)); }
        catch { process.Kill(entireProcessTree: true); throw; }
        if (process.ExitCode != 0) throw new IOException($"ADB 오류: {await stderr}");
        return await stdout;
    }

    internal async Task StartAsync()
    {
        if (!File.Exists(adb)) throw new IOException("PoC용 ADB가 없습니다.");
        if (recorder is not null) throw new InvalidOperationException("이미 시작한 녹음입니다.");
        // arecord owns the PID file, and stops itself at 60 seconds if the client disappears.
        recorder = Process.Start(Info("shell",
            $"arecord -q -D hw:0,0 -f S16_LE -r 16000 -c 3 -t raw -d 60 -F 20000 -B 80000 --process-id-file={stem}.pid {stem}.pcm"))
            ?? throw new IOException("MK20 녹음을 시작하지 못했습니다.");
        output = recorder.StandardOutput.ReadToEndAsync();
        errors = recorder.StandardError.ReadToEndAsync();
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(6))
        {
            if (recorder.HasExited)
                throw new IOException("MK20 녹음이 시작 직후 종료됐습니다: " + await output + await errors);
            if ((await RunAsync("shell", $"test -s {stem}.pid && test -s {stem}.pcm && echo READY")).Contains("READY")) return;
            await Task.Delay(150);
        }
        throw new TimeoutException("MK20 마이크 준비 응답이 없습니다.");
    }

    private async Task StopOwnedAsync()
    {
        // stem is generated locally from a GUID. Never signal an unvalidated PID or another recorder.
        await RunAsync("shell", $"if [ -s {stem}.pid ]; then p=$(cat {stem}.pid); case $p in ''|*[!0-9]*) exit 1;; esac; " +
            $"if [ -r /proc/$p/cmdline ] && grep -q '{stem}.pcm' /proc/$p/cmdline; then kill -2 $p; fi; fi");
        if (recorder is not null && !recorder.HasExited)
            await recorder.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
    }

    internal async Task<byte[]> FinishAsync()
    {
        if (recorder is null || completed) throw new InvalidOperationException("진행 중인 녹음이 없습니다.");
        await StopOwnedAsync();
        var path = Path.Combine(Path.GetTempPath(), "snowball-poc-" + Guid.NewGuid().ToString("N") + ".pcm");
        try
        {
            await RunAsync("pull", stem + ".pcm", path);
            var length = new FileInfo(path).Length;
            if (length < 960 || length > 16000 * 6 * 61 || length % 6 != 0)
                throw new IOException($"MK20 PCM 길이가 올바르지 않습니다: {length} bytes");
            var raw = await File.ReadAllBytesAsync(path);
            var pcm = new byte[raw.Length / 3];
            for (int i = 0; i < raw.Length / 6; i++) { pcm[i*2] = raw[i*6+4]; pcm[i*2+1] = raw[i*6+5]; }
            completed = true;
            return pcm;
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }

    public async ValueTask DisposeAsync()
    {
        try
        {
            if (recorder is not null) await StopOwnedAsync();
            await RunAsync("shell", $"rm -f {stem}.pcm {stem}.pid");
        }
        catch { /* Bounded arecord exits within 60s; a disconnected device may retain its unique file. */ }
        finally { recorder?.Dispose(); }
    }
}
