using NAudio.Wave;

namespace Snowball.Dictation;

internal static class CableAudio
{
    internal static object Devices() => new
    {
        outputs = Enumerable.Range(0, WaveOut.DeviceCount).Select(i => new { index = i, name = WaveOut.GetCapabilities(i).ProductName }).ToArray(),
        inputs = Enumerable.Range(0, WaveIn.DeviceCount).Select(i => new { index = i, name = WaveIn.GetCapabilities(i).ProductName }).ToArray()
    };

    internal static int FindOutput()
    {
        var matches = Enumerable.Range(0, WaveOut.DeviceCount)
            .Where(i => WaveOut.GetCapabilities(i).ProductName.StartsWith("CABLE Input", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (matches.Length != 1) throw new IOException("VB-CABLE의 CABLE Input 장치가 필요합니다. 설치 후 앱을 다시 여세요.");
        return matches[0];
    }

    internal sealed record LoopbackResult(bool pass, int receivedBytes, int peak, int activeSamples, string scope);

    internal static async Task<LoopbackResult> CheckLoopbackAsync()
    {
        FindOutput();
        var inputs = Enumerable.Range(0, WaveIn.DeviceCount)
            .Where(i => WaveIn.GetCapabilities(i).ProductName.StartsWith("CABLE Output", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (inputs.Length != 1) throw new IOException("CABLE Output 녹음 장치가 필요합니다.");
        using var capture = new WaveInEvent { DeviceNumber = inputs[0], WaveFormat = new WaveFormat(16000, 16, 1), BufferMilliseconds = 20 };
        using var received = new MemoryStream();
        var ended = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        capture.DataAvailable += (_, e) => { lock (received) { if (received.Length < 32000 * 10) received.Write(e.Buffer, 0, e.BytesRecorded); } };
        capture.RecordingStopped += (_, e) => { if (e.Exception is not null) ended.TrySetException(e.Exception); else ended.TrySetResult(); };
        var tone = new byte[16000];
        for (int i = 0; i < tone.Length / 2; i++)
            System.Buffers.Binary.BinaryPrimitives.WriteInt16LittleEndian(tone.AsSpan(i*2), (short)(2000 * Math.Sin(2 * Math.PI * 1000 * i / 16000)));
        capture.StartRecording();
        try { await PlayAsync(tone, CancellationToken.None); await Task.Delay(300); }
        finally { capture.StopRecording(); }
        await ended.Task.WaitAsync(TimeSpan.FromSeconds(3));
        var bytes = received.ToArray(); int peak = 0, active = 0;
        for (int i = 0; i + 1 < bytes.Length; i += 2)
        {
            int value = Math.Abs((int)System.Buffers.Binary.BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(i)));
            peak = Math.Max(peak, value); if (value > 100) active++;
        }
        return new LoopbackResult(peak > 500 && active > 3000, bytes.Length, peak, active,
            "synthetic tone through CABLE Input to CABLE Output; not Codex transcription");
    }

    internal static byte[] LoadFixture(string path)
    {
        using var reader = new WaveFileReader(path);
        if (reader.WaveFormat.Encoding != WaveFormatEncoding.Pcm || reader.WaveFormat.SampleRate != 16000 ||
            reader.WaveFormat.BitsPerSample != 16 || reader.WaveFormat.Channels != 1)
            throw new IOException("시험 WAV는 PCM16, 16kHz, 모노여야 합니다.");
        if (reader.Length > 16000 * 2 * 60) throw new IOException("시험 WAV는 60초 이내여야 합니다.");
        using var stream = new MemoryStream(); reader.CopyTo(stream); return stream.ToArray();
    }

    internal static async Task PlayAsync(byte[] pcm, CancellationToken token)
    {
        var output = FindOutput(); // Never fall back to speakers or the system default.
        // Native dictation startup has no public ACK. A leading second preserves early speech.
        var padded = new byte[32000 + pcm.Length + 16000];
        pcm.CopyTo(padded, 32000);
        using var stream = new RawSourceWaveStream(new MemoryStream(padded), new WaveFormat(16000, 16, 1));
        using var player = new WaveOutEvent { DeviceNumber = output, DesiredLatency = 100 };
        var stopped = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        player.PlaybackStopped += (_, e) =>
        {
            if (e.Exception is not null) stopped.TrySetException(e.Exception); else stopped.TrySetResult();
        };
        player.Init(stream); player.Play();
        try { await stopped.Task.WaitAsync(TimeSpan.FromSeconds(padded.Length / 32000.0 + 5), token); }
        finally { player.Stop(); }
    }
}

internal sealed class LiveCable : IDisposable
{
    private readonly BufferedWaveProvider buffer = new(new WaveFormat(16000, 16, 1))
    {
        BufferDuration = TimeSpan.FromSeconds(2), DiscardOnBufferOverflow = false, ReadFully = true
    };
    private readonly WaveOutEvent player = new() { DesiredLatency = 100 };
    private Exception? playbackError;
    internal LiveCable()
    {
        player.DeviceNumber = CableAudio.FindOutput();
        player.PlaybackStopped += (_, e) => playbackError = e.Exception;
        player.Init(buffer); player.Play();
    }
    internal void Add(byte[] data)
    {
        if (playbackError is not null) throw new IOException("가상 마이크 재생 오류", playbackError);
        buffer.AddSamples(data, 0, data.Length); // Overflow is an error, never silent dropped audio.
    }
    internal async Task DrainAsync()
    {
        var deadline = DateTime.UtcNow.AddSeconds(3);
        while (buffer.BufferedBytes > 0)
        {
            if (DateTime.UtcNow > deadline) throw new IOException("가상 마이크 버퍼가 비워지지 않습니다.");
            await Task.Delay(30);
        }
        await Task.Delay(180);
    }
    public void Dispose() { player.Stop(); player.Dispose(); }
}
