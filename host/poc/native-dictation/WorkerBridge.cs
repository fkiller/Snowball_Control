using System.Text.Json;

namespace Snowball.Dictation;

internal static class WorkerBridge
{
    internal static async Task RunAsync(DictationPad pad)
    {
        using var input = new StreamReader(Console.OpenStandardInput());
        using var output = new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true };
        var gate = new SemaphoreSlim(1);
        async Task Reply(object value)
        {
            await gate.WaitAsync();
            try { await output.WriteLineAsync(JsonSerializer.Serialize(value)); }
            catch (IOException) { }
            catch (ObjectDisposedException) { }
            finally { gate.Release(); }
        }
        while (await input.ReadLineAsync() is { } line)
        {
            if (line.Length > 4096) continue;
            try
            {
                using var json = JsonDocument.Parse(line);
                int id = json.RootElement.GetProperty("id").GetInt32();
                string method = json.RootElement.GetProperty("method").GetString() ?? "";
                string capture = json.RootElement.GetProperty("captureId").GetString() ?? "";
                if (!Guid.TryParse(capture, out _)) { await Reply(new { id, error = "Invalid capture ID" }); continue; }
                pad.BeginInvoke((Action)(async () =>
                {
                    try { string text = await pad.WorkerCommandAsync(method, capture); await Reply(new { id, text }); }
                    catch (Exception error) { await Reply(new { id, error = error.Message }); }
                }));
            }
            catch (JsonException) { }
            catch (InvalidOperationException) { }
            catch (KeyNotFoundException) { }
        }
        // Parent disconnected: stop this worker's capture before releasing its window.
        pad.BeginInvoke((Action)(async () => { await pad.WorkerDisconnectAsync(); pad.Close(); }));
    }
}
