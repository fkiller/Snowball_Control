using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Snowball.Dictation;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--probe-panel")
        {
            try { File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(DevicePanel.ProbeAsync().GetAwaiter().GetResult())); }
            catch (Exception error) { File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(new { pass = false, error = error.Message })); Environment.ExitCode = 1; }
            return;
        }
        if (args.Length == 2 && args[0] == "--devices")
        {
            File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(CableAudio.Devices(), new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
            return;
        }
        if (args.Length == 2 && args[0] == "--probe-cable")
        {
            try
            {
                var result = CableAudio.CheckLoopbackAsync().GetAwaiter().GetResult();
                File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(result));
                Environment.ExitCode = result.pass ? 0 : 1;
            }
            catch (Exception e) { File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(new { pass = false, error = e.Message })); Environment.ExitCode = 1; }
            return;
        }
        if (args.Length == 2 && args[0] == "--probe-mk20")
        {
            ProbeAsync(args[1]).GetAwaiter().GetResult(); return;
        }
        if (args.Length == 2 && (args[0] == "--probe-live" || args[0] == "--probe-live-cable"))
        {
            ProbeLiveAsync(args[1], args[0] == "--probe-live-cable").GetAwaiter().GetResult(); return;
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new DictationPad(args.Contains("--panel"), args.Contains("--worker")));
    }

    private static async Task ProbeLiveAsync(string report, bool useCable)
    {
        try
        {
            await using var live = new Mk20Live();
            using var cable = useCable ? new LiveCable() : null;
            var clock = Stopwatch.StartNew();
            await live.StartAsync(data => cable?.Add(data));
            var firstFrameMs = clock.ElapsedMilliseconds;
            clock.Restart(); await Task.Delay(2000); await live.StopAsync();
            double clockRatio = live.BytesReceived / 32000.0 / clock.Elapsed.TotalSeconds;
            if (cable is not null) await cable.DrainAsync();
            bool passed = live.BytesReceived > 32000 && clockRatio > .7 && clockRatio < 1.3;
            Environment.ExitCode = passed ? 0 : 1;
            File.WriteAllText(report, System.Text.Json.JsonSerializer.Serialize(new
            {
                pass = passed, bytes = live.BytesReceived, firstFrameMs, clockRatio,
                durationMs = clock.ElapsedMilliseconds, useCable, scope = "C# framed live capture and owned stop; not STT"
            }));
        }
        catch (Exception e) { File.WriteAllText(report, System.Text.Json.JsonSerializer.Serialize(new { pass = false, error = e.Message })); Environment.ExitCode = 1; }
    }

    private static async Task ProbeAsync(string report)
    {
        try
        {
            await using var capture = new Mk20Capture();
            await capture.StartAsync(); await Task.Delay(2000);
            var bytes = await capture.FinishAsync();
            File.WriteAllText(report, System.Text.Json.JsonSerializer.Serialize(new
            {
                pass = true, bytes = bytes.Length, seconds = bytes.Length / 32000.0,
                scope = "owned recorder start/stop and binary ADB pull; not STT"
            }));
        }
        catch (Exception e) { File.WriteAllText(report, System.Text.Json.JsonSerializer.Serialize(new { pass = false, error = e.Message })); Environment.ExitCode = 1; }
    }
}

internal sealed class DictationPad : Form
{
    private readonly TextBox draft = new()
    {
        Multiline = true, AcceptsReturn = true, AcceptsTab = true,
        ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill,
        Font = new Font("Malgun Gothic", 14), AccessibleName = "받아쓰기 초안",
        BorderStyle = BorderStyle.FixedSingle
    };
    private readonly Label status = new() { AutoSize = true, Text = "준비 · Codex에 로그인한 상태에서 시작하세요." };
    private readonly Button start = MakeButton("받아쓰기 시작");
    private readonly Button stop = MakeButton("중지 · 결과 받기");
    private readonly Button reset = MakeButton("다시 준비");
    private readonly Button copy = MakeButton("초안 복사");
    private readonly Button mkStart = MakeButton("MK20 녹음");
    private readonly Button mkStop = MakeButton("MK20 중지 후 전사");
    private readonly Button fixture = MakeButton("시험 음성 전사");
    private readonly Button liveStart = MakeButton("MK20 실시간 시작");
    private readonly Button liveStop = MakeButton("실시간 중지");
    private readonly Button install = MakeButton("가상 마이크 설치");
    private readonly Button check = MakeButton("장치 확인");
    private readonly Button panelConnect = MakeButton("장치 연결");
    private readonly Button cancelDraft = MakeButton("음성 취소");
    private readonly System.Windows.Forms.Timer panelTimer = new() { Interval = 1000 };
    private DevicePanel? panel;
    private bool cancelPending;
    private bool discarding;
    private bool restoringDraft;
    private bool hasVoiceDraft;
    private bool transcriptTimedOut;
    private string lastAudioSummary = "";
    private string? workerCapture;
    private TaskCompletionSource<string>? workerTranscript;
    private string preservedDraft = "";
    private Mk20Capture? capture;
    private Mk20Live? live;
    private LiveCable? liveCable;
    private bool liveStopping;
    private bool bridgeBusy;
    private readonly CancellationTokenSource lifetime = new();
    private byte[]? capturedPcm;
    private readonly System.Windows.Forms.Timer waiting = new() { Interval = 15000 };
    private bool startRequested;
    private bool sendingKeys;
    private bool waitingForText;
    private readonly Stopwatch elapsed = new();
    private string beforeStop = "";

    public DictationPad(bool connectPanel = false, bool workerMode = false)
    {
        Text = "Snowball — MK20 Native Dictation PoC";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1100, 720);
        MinimumSize = new Size(700, 460);
        Font = new Font("Malgun Gothic", 10);
        BackColor = Color.FromArgb(246, 248, 252);
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill, Padding = new Padding(24), ColumnCount = 1, RowCount = 6
        };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 45));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 170));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
        layout.Controls.Add(new Label
        {
            Text = "Codex 받아쓰기 → 내 앱의 초안", AutoSize = true,
            Font = new Font("Malgun Gothic", 19, FontStyle.Bold)
        }, 0, 0);
        layout.Controls.Add(new Label
        {
            Dock = DockStyle.Fill,
            Text = "1. Codex → 음성 → 입력 장치: CABLE Output / 말하기 켜기·끄기: Ctrl+Alt+Shift+F9\n" +
                   "2. 먼저 ‘시험 음성 전사’로 연결 확인 → ‘MK20 녹음’ → 말하기 → ‘MK20 중지 후 전사’\n" +
                   "3. 결과를 아래에서 확인·수정합니다. 코딩 작업에 자동 전송하지 않습니다.\n" +
                   "PC 마이크로 직접 말하려면 Codex 입력 장치를 원래 마이크로 바꾸고 첫 줄 버튼을 쓰세요."
        }, 0, 1);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = true };
        actions.Controls.AddRange([start, stop, reset, copy]);
        actions.SetFlowBreak(copy, true);
        actions.Controls.AddRange([fixture, mkStart, mkStop]);
        actions.SetFlowBreak(mkStop, true);
        actions.Controls.AddRange([liveStart, liveStop]);
        actions.Controls.AddRange([install, check]);
        actions.SetFlowBreak(check, true);
        actions.Controls.AddRange([panelConnect, cancelDraft]);
        panelConnect.Click += (_, _) => ConnectPanel();
        cancelDraft.Click += async (_, _) => await CancelDraftAsync();
        panelTimer.Tick += (_, _) => PublishPanel();
        liveStop.Enabled = false;
        mkStop.Enabled = false;
        layout.Controls.Add(actions, 0, 2);
        layout.Controls.Add(status, 0, 3);
        layout.Controls.Add(draft, 0, 4);
        layout.Controls.Add(new Label
        {
            Dock = DockStyle.Fill, Padding = new Padding(0, 12, 0, 0),
            Text = "MK20 MIC3 → 16kHz 모노 → 가상 마이크 → Codex 자체 받아쓰기 · 실시간/녹음 후 전사 모두 시험 가능",
            ForeColor = Color.DimGray
        }, 0, 5);
        Controls.Add(layout);
        if (workerMode)
        {
            Text = "Snowball — Voice Draft";
            actions.Visible = false;
            layout.RowStyles[2].Height = 0;
            layout.GetControlFromPosition(0, 0)!.Text = "Snowball 음성 초안";
            layout.GetControlFromPosition(0, 1)!.Text = "MK20의 Talk로 말하고 Finish로 초안을 확인하세요.\n이 창은 음성 입력을 받습니다. 세션 선택과 전송은 MK20에서 진행합니다.";
        }
        stop.Enabled = false;
        copy.Enabled = false;
        start.Click += async (_, _) => await ToggleNativeAsync(true);
        stop.Click += async (_, _) => await ToggleNativeAsync(false);
        mkStart.Click += async (_, _) => await StartMk20Async();
        mkStop.Click += async (_, _) => await StopMk20Async();
        liveStart.Click += async (_, _) => await StartLiveAsync();
        liveStop.Click += async (_, _) => await StopLiveAsync();
        install.Click += (_, _) =>
        {
            if (bridgeBusy || capture is not null || live is not null || startRequested) return;
            string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "SnowballControl", "dependencies", "vb-cable", "VBCABLE_Setup_x64.exe");
            try
            {
                if (!File.Exists(path)) throw new IOException("설치 파일이 없습니다. vb-audio.com/Cable 에서 공식 드라이버를 설치하세요.");
                Process.Start(new ProcessStartInfo(path) { UseShellExecute = true, Verb = "runas", WorkingDirectory = Path.GetDirectoryName(path)! });
                status.Text = "설치창에서 Install Driver → 완료 후 ‘장치 확인’. Codex 입력 장치는 CABLE Output으로 선택하세요.";
            }
            catch (Exception ex) { status.Text = "관리자 설치가 필요합니다: " + ex.Message; }
        };
        check.Click += (_, _) =>
        {
            try
            {
                CableAudio.FindOutput();
                status.Text = "CABLE Input 준비됨 · Codex 입력을 CABLE Output으로 선택한 뒤 ‘시험 음성 전사’를 누르세요.";
            }
            catch (Exception ex) { status.Text = ex.Message; }
        };
        fixture.Click += async (_, _) =>
        {
            if (bridgeBusy || capture is not null || live is not null || startRequested || waitingForText) return;
            try { await TranscribePcmAsync(CableAudio.LoadFixture(Path.Combine(AppContext.BaseDirectory, "fixture.wav"))); }
            catch (Exception ex) { status.Text = ex.Message; }
        };
        reset.Click += (_, _) =>
        {
            if (bridgeBusy || capture is not null || live is not null) return;
            if (discarding)
            {
                if (MessageBox.Show(this, "Codex 녹음/전사가 끝난 것을 확인했나요? 끝난 뒤 다시 준비하세요.",
                    "늦게 도착하는 전사 확인", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
                discarding = false;
                cancelPending = false;
            }
            // No synthetic toggle here: an uncertain start must not become a new recording.
            if (startRequested)
            {
                MessageBox.Show(this, "Codex 녹음 표시에서 먼저 중지하세요. 이후 다시 준비를 누르세요.", "녹음 상태 확인");
                startRequested = false;
                return;
            }
            waiting.Stop();
            waitingForText = false;
            transcriptTimedOut = false;
            start.Enabled = true;
            stop.Enabled = false;
            status.Text = "준비 · 기존 초안은 유지됩니다.";
            BridgeButtons(false);
            draft.Focus();
        };
        copy.Click += (_, _) =>
        {
            try { if (draft.TextLength > 0) Clipboard.SetText(draft.Text); }
            catch (ExternalException) { status.Text = "클립보드를 사용할 수 없습니다. 다시 눌러 주세요."; }
        };
        draft.TextChanged += (_, _) =>
        {
            if (discarding)
            {
                if (!restoringDraft && draft.Text != preservedDraft)
                {
                    restoringDraft = true; draft.Text = preservedDraft; restoringDraft = false;
                }
                return;
            }
            copy.Enabled = draft.TextLength > 0;
            if (waitingForText && draft.Text != beforeStop)
            {
                hasVoiceDraft = true;
                transcriptTimedOut = false;
                waiting.Stop(); waitingForText = false; start.Enabled = true;
                status.Text = $"초안 변경 감지 · {elapsed.Elapsed.TotalSeconds:F1}초 · 확인 후 수정하세요.";
                workerTranscript?.TrySetResult(draft.Text);
                if (!bridgeBusy && live is null) BridgeButtons(false);
                // Text arrival alone cannot identify the source. Native overlay is the acceptance evidence.
            }
        };
        waiting.Tick += (_, _) =>
        {
            waiting.Stop();
            transcriptTimedOut = true;
            status.Text = "전사가 지연되고 있습니다 · " + lastAudioSummary + " · 결과를 기다리거나 Codex 녹음 표시를 확인하세요.";
            PublishPanel();
            reset.Enabled = true;
        };
        FormClosing += (_, e) =>
        {
            if (bridgeBusy || capture is not null || live is not null)
            {
                e.Cancel = true;
                status.Text = "MK20 녹음/전사를 먼저 마쳐 주세요. 녹음은 최대 60초입니다.";
                return;
            }
            if (startRequested || waitingForText)
            {
                e.Cancel = MessageBox.Show(this,
                    "Codex 녹음/전사가 계속될 수 있습니다. Codex에서 중지한 뒤 닫으세요. 창을 닫을까요?",
                    "받아쓰기 상태 확인", MessageBoxButtons.YesNo) != DialogResult.Yes;
            }
        };
        FormClosed += (_, _) => { panelTimer.Dispose(); panel?.Dispose(); lifetime.Cancel(); lifetime.Dispose(); waiting.Dispose(); };
        Shown += (_, _) => { draft.Focus(); if (connectPanel) ConnectPanel(); if (workerMode) _ = WorkerBridge.RunAsync(this); };
    }

    internal async Task<string> WorkerCommandAsync(string method, string captureId)
    {
        if (method == "capabilities") return "snowball-native-voice/1 windows";
        if (method == "status") return "{" +
            "\"capture\":" + (workerCapture is null ? "null" : "\"" + workerCapture + "\"") + "," +
            "\"startRequested\":" + startRequested.ToString().ToLowerInvariant() + "," +
            "\"waitingForText\":" + waitingForText.ToString().ToLowerInvariant() + "," +
            "\"live\":" + (live is null ? "false" : "true") + "," +
            "\"liveBytes\":" + (live is null ? "0" : live.BytesReceived.ToString()) + "," +
            "\"busy\":" + bridgeBusy.ToString().ToLowerInvariant() + "," +
            "\"status\":" + System.Text.Json.JsonSerializer.Serialize(status.Text) + "}";
        if (method == "start")
        {
            if (workerCapture is not null || bridgeBusy || live is not null || waitingForText || discarding || startRequested)
                throw new IOException("Native voice is still busy; finish/reset the existing capture first.");
            workerCapture = captureId;
            draft.Clear();
            await StartLiveAsync();
            if (live is null) { workerCapture = null; throw new IOException(status.Text); }
            return "";
        }
        if (workerCapture != captureId) throw new IOException("Capture no longer active.");
        if (method == "finish")
        {
            workerTranscript = new(TaskCreationOptions.RunContinuationsAsynchronously);
            await StopLiveAsync();
            if (!waitingForText && !workerTranscript.Task.IsCompleted) throw new IOException(status.Text);
            try { string text = await workerTranscript.Task.WaitAsync(TimeSpan.FromSeconds(80)); workerCapture = null; return text; }
            catch (TimeoutException)
            {
                // No transcript arrived: fully release so the next middleware
                // start is not rejected as "still busy". A late insertion must
                // not resurrect this capture.
                waiting.Stop(); waitingForText = false; transcriptTimedOut = true; workerCapture = null;
                throw new IOException("Codex가 80초 안에 전사 결과를 넣지 않았습니다. Codex 녹음 표시를 확인하세요.");
            }
        }
        if (method == "cancel")
        {
            workerTranscript?.TrySetCanceled();
            await CancelDraftAsync(); workerCapture = null;
            // Middleware owns this lifecycle; there is no UI reset step here.
            // Without this, discarding stays true and every later start fails.
            discarding = false; cancelPending = false;
            return "";
        }
        throw new IOException("Unknown voice command.");
    }

    internal async Task WorkerDisconnectAsync()
    {
        workerTranscript?.TrySetCanceled();
        if (live is not null) await live.DisposeAsync();
        live = null; liveCable?.Dispose(); liveCable = null;
    }

    private void ConnectPanel()
    {
        if (panel is not null) return;
        try
        {
            panel = new DevicePanel();
            panel.KeyDown += key =>
            {
                if (IsDisposed || !IsHandleCreated) return;
                BeginInvoke((Action)(async () =>
                {
                    if (key == 4) await CancelDraftAsync();
                    else if (key == 20 && live is null && !discarding) await StartLiveAsync();
                    else if ((key == 20 || key == 16) && live is not null) await StopLiveAsync();
                    PublishPanel();
                }));
            };
            panel.Error += error => { if (!IsDisposed && IsHandleCreated) BeginInvoke((Action)(() => status.Text = error)); };
            panelConnect.Enabled = false; panelTimer.Start(); PublishPanel();
            status.Text = "장치 음성 시험 연결 · MK20 Talk로 시작합니다. 실제 프로젝트/세션 제어는 별도입니다.";
        }
        catch (Exception error) { panel?.Dispose(); panel = null; status.Text = "장치 연결 실패 (다른 미들웨어가 실행 중인지 확인): " + error.Message; }
    }

    private void PublishPanel()
    {
        if (panel is null) return;
        try
        {
            string phase = discarding ? "Discarded - reset in Windows" : transcriptTimedOut ? "Transcript delayed - still waiting" : bridgeBusy ? "Preparing / finishing" :
                live is not null ? $"Audio {live.BytesReceived / 32000.0:F1}s / peak {live.Peak}" : waitingForText ? "Finish received - transcribing" :
                startRequested ? "Check native recording overlay" : hasVoiceDraft ? "Review draft in Windows" : "Ready - press Talk";
            panel.Publish(phase, !bridgeBusy && live is null && capture is null && !startRequested && !waitingForText && !discarding,
                !bridgeBusy && live is not null, live is not null || bridgeBusy || waitingForText || hasVoiceDraft);
        }
        catch (Exception error) { status.Text = "장치 상태 전달 오류: " + error.Message; }
    }

    private async Task CancelDraftAsync()
    {
        if (capture is not null || (!bridgeBusy && live is null && !waitingForText && !hasVoiceDraft)) return;
        if (!discarding) { preservedDraft = beforeStop; discarding = true; }
        hasVoiceDraft = false;
        // A cancelled wait must fully release the worker: otherwise the next
        // middleware start is rejected as "still busy" forever. The pending
        // transcript waiter is cancelled so a late insertion cannot leak in.
        waiting.Stop(); waitingForText = false; transcriptTimedOut = false;
        draft.Text = preservedDraft;
        if (bridgeBusy) { cancelPending = true; PublishPanel(); return; }
        if (live is not null) await StopLiveAsync();
        status.Text = "취소됨 · 기존 초안 유지. Codex 전사가 끝나면 ‘다시 준비’를 누르세요.";
        BridgeButtons(false); PublishPanel();
    }

    /// <summary>
    /// Middleware-owned focus acquisition. Keeps the mandatory check that the
    /// transcript target is our draft box, but steals the foreground reliably
    /// when triggered from hardware/child process (plain Activate is ignored
    /// by Windows then). Throws IOException with a specific reason for the
    /// middleware, which reports it on the MK20 screen.
    /// </summary>
    private async Task AcquireDictationTargetAsync()
    {
        string? reason = await NativeInput.AcquireDictationTarget(Handle, draft.Handle, () => draft.Focused);
        if (reason is not null) throw new IOException("이 창을 먼저 선택하세요. (" + reason + ")");
    }

    private async Task ToggleNativeAsync(bool begin)
    {
        if (discarding) return;
        if (bridgeBusy || capture is not null || live is not null) return;
        if (sendingKeys || (begin && startRequested) || (!begin && !startRequested)) return;
        if (!Process.GetProcessesByName("ChatGPT").Any() && !Process.GetProcessesByName("Codex").Any())
        {
            status.Text = "Codex 데스크톱 앱을 먼저 열고 로그인하세요.";
            return;
        }
        sendingKeys = true;
        start.Enabled = stop.Enabled = reset.Enabled = false;
        try
        {
            await AcquireDictationTargetAsync();
            NativeInput.ToggleDictation();
            startRequested = begin;
            if (begin)
            {
                status.Text = "시작 요청 · Codex 녹음 표시를 확인한 뒤 말하세요.";
                stop.Enabled = true;
            }
            else
            {
                beforeStop = draft.Text;
                elapsed.Restart(); waitingForText = true; waiting.Start();
                status.Text = "중지 요청 · Codex가 이 입력창에 결과를 넣을 때까지 기다리세요.";
            }
        }
        catch (Exception ex)
        {
            status.Text = ex.Message;
            start.Enabled = !startRequested;
            stop.Enabled = startRequested;
        }
        finally { sendingKeys = false; reset.Enabled = true; }
    }

    private void BridgeButtons(bool busy)
    {
        bridgeBusy = busy;
        start.Enabled = !busy && live is null && !startRequested && !waitingForText && !discarding;
        stop.Enabled = !busy && live is null && startRequested;
        reset.Enabled = !busy && live is null;
        liveStart.Enabled = mkStart.Enabled = fixture.Enabled = !busy && live is null && capture is null && !startRequested && !waitingForText && !discarding;
        mkStop.Enabled = !busy && capture is not null;
        liveStop.Enabled = !busy && live is not null;
    }

    private async Task StartLiveAsync()
    {
        if (bridgeBusy || live is not null || capture is not null || startRequested || waitingForText || discarding) return;
        beforeStop = draft.Text;
        hasVoiceDraft = false;
        transcriptTimedOut = false; lastAudioSummary = "";
        BridgeButtons(true);
        try
        {
            CableAudio.FindOutput();
            if (!Process.GetProcessesByName("ChatGPT").Any()) throw new IOException("Codex 앱을 먼저 열어 주세요.");
            await AcquireDictationTargetAsync();
            NativeInput.ToggleDictation(); startRequested = true;
            status.Text = "Codex 및 MK20 마이크 준비 중…";
            await Task.Delay(1500);
            liveCable = new LiveCable(); live = new Mk20Live();
            await live.StartAsync(liveCable.Add);
            status.Text = "MK20 실시간 연결됨 · 장치에 말하세요. 완료하면 ‘실시간 중지’를 누르세요. (최대 60초)";
        }
        catch (Exception e)
        {
            if (live is not null) await live.DisposeAsync(); live = null;
            liveCable?.Dispose(); liveCable = null;
            status.Text = e.Message + (startRequested ? " · Codex 녹음은 직접 중지해 주세요." : "");
        }
        finally { BridgeButtons(false); }
        if (cancelPending) { cancelPending = false; await CancelDraftAsync(); }
        // Observe only after startup releases the busy guard, including already-ended streams.
        if (live is not null) _ = ObserveLiveAsync(live);
    }

    private async Task ObserveLiveAsync(Mk20Live owner)
    {
        try { await owner.Completion; }
        catch (Exception e) { status.Text = "실시간 연결 오류: " + e.Message; }
        if (ReferenceEquals(live, owner) && !liveStopping && !bridgeBusy) await StopLiveAsync();
    }

    private async Task StopLiveAsync()
    {
        if (live is null || liveStopping || bridgeBusy) return;
        liveStopping = true; BridgeButtons(true);
        status.Text = "Finish 수신 · MK20 녹음을 끝내는 중…";
        PublishPanel();
        try
        {
            await live.StopAsync();
            lastAudioSummary = $"음성 {live.BytesReceived / 32000.0:F1}초 / 최대 레벨 {live.Peak}";
            if (liveCable is not null && !discarding) await liveCable.DrainAsync();
            if (discarding) { liveCable?.Dispose(); liveCable = null; }
            await AcquireDictationTargetAsync();
            NativeInput.ToggleDictation(); startRequested = false;
            elapsed.Restart(); waitingForText = !discarding; if (waitingForText) waiting.Start();
            status.Text = "Finish 처리 완료 · " + lastAudioSummary + " · Codex 전사 결과 대기 중…";
        }
        catch (Exception e) { status.Text = e.Message + " · Codex 녹음 표시를 확인하세요."; }
        finally
        {
            await live.DisposeAsync(); live = null;
            liveCable?.Dispose(); liveCable = null;
            liveStopping = false; BridgeButtons(false);
        }
    }

    private async Task StartMk20Async()
    {
        if (bridgeBusy || capture is not null || startRequested || waitingForText || discarding) return;
        BridgeButtons(true);
        try
        {
            CableAudio.FindOutput();
            capture = new Mk20Capture();
            status.Text = "MK20 마이크 준비 중…";
            await capture.StartAsync();
            status.Text = "MK20 녹음 중 · 장치 마이크에 말한 뒤 ‘MK20 중지 후 전사’를 누르세요. (최대 60초)";
        }
        catch (Exception ex)
        {
            if (capture is not null) await capture.DisposeAsync(); capture = null;
            status.Text = ex.Message;
        }
        finally { BridgeButtons(false); start.Enabled = capture is null && !waitingForText; }
    }

    private async Task StopMk20Async()
    {
        if (bridgeBusy || capture is null) return;
        BridgeButtons(true);
        try
        {
            status.Text = "MK20 녹음을 마치고 Windows로 가져오는 중…";
            capturedPcm = await capture.FinishAsync();
            await capture.DisposeAsync(); capture = null;
            await TranscribePcmAsync(capturedPcm);
        }
        catch (Exception ex)
        {
            status.Text = ex.Message;
            if (capture is not null) await capture.DisposeAsync(); capture = null;
        }
        finally { BridgeButtons(false); }
    }

    private async Task TranscribePcmAsync(byte[] pcm)
    {
        CableAudio.FindOutput();
        if (pcm.Length == 0) throw new IOException("빈 녹음입니다.");
        if (!Process.GetProcessesByName("ChatGPT").Any()) throw new IOException("Codex 앱을 먼저 열어 주세요.");
        BridgeButtons(true);
        try
        {
            await AcquireDictationTargetAsync();
            beforeStop = draft.Text;
            NativeInput.ToggleDictation(); startRequested = true;
            status.Text = $"Codex 받아쓰기 준비 → 녹음 {pcm.Length / 32000.0:F1}초를 가상 마이크로 전달합니다…";
            await Task.Delay(1500, lifetime.Token);
            await CableAudio.PlayAsync(pcm, lifetime.Token);
            await AcquireDictationTargetAsync();
            NativeInput.ToggleDictation(); startRequested = false;
            elapsed.Restart(); waitingForText = true; waiting.Start();
            status.Text = "음성 전달 완료 · Codex 전사 결과를 기다립니다…";
        }
        finally { BridgeButtons(false); }
    }

    private static Button MakeButton(string text) => new()
    {
        Text = text, AutoSize = true, Height = 36, Padding = new Padding(8, 3, 8, 3),
        Margin = new Padding(0, 0, 10, 0), TabStop = false
    };
}

internal static class NativeInput
{
    [StructLayout(LayoutKind.Sequential)] private struct Input { public uint Type; public InputUnion Data; }
    [StructLayout(LayoutKind.Explicit)] private struct InputUnion
    {
        [FieldOffset(0)] public KeyboardInput Keyboard;
        [FieldOffset(0)] public MouseInput Mouse;
    }
    [StructLayout(LayoutKind.Sequential)] private struct KeyboardInput
    {
        public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra;
    }
    [StructLayout(LayoutKind.Sequential)] private struct MouseInput
    {
        public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra;
    }
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, Input[] inputs, int size);
    [DllImport("user32.dll")] internal static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int key);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] private static extern bool AttachThreadInput(uint fromThreadId, uint toThreadId, bool attach);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    private const int SW_RESTORE = 9;

    internal static void ToggleDictation()
    {
        ushort[] chord = [0x11, 0x12, 0x10, 0x78]; // Ctrl+Alt+Shift+F9, configured through normal Codex settings.
        if (chord.Any(key => (GetAsyncKeyState(key) & 0x8000) != 0))
            throw new InvalidOperationException("Ctrl/Alt/Shift/F9 키에서 손을 뗀 뒤 눌러 주세요.");
        Input Key(ushort key, bool up) => new()
        {
            Type = 1, Data = new InputUnion { Keyboard = new KeyboardInput { Key = key, Flags = up ? 2u : 0u } }
        };
        var inputs = chord.Select(k => Key(k, false)).Concat(chord.Reverse().Select(k => Key(k, true))).ToArray();
        if (SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>()) != inputs.Length)
        {
            var release = chord.Reverse().Select(k => Key(k, true)).ToArray();
            SendInput((uint)release.Length, release, Marshal.SizeOf<Input>());
            throw new InvalidOperationException("단축키 전달 결과가 불확실합니다. Codex 녹음 상태를 확인하세요.");
        }
    }

    /// <summary>
    /// Brings the middleware-owned dictation target forward so the Codex global
    /// dictation shortcut inserts into our draft box. The focus check is kept:
    /// a transcript must never land in an unknown window. Returns null on
    /// success, or a specific English reason code for host error reporting.
    /// </summary>
    internal static async Task<string?> AcquireDictationTarget(IntPtr window, IntPtr edit, Func<bool> isEditFocused)
    {
        if (window == IntPtr.Zero || edit == IntPtr.Zero) return "voice window not ready";
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        uint workerThread = GetCurrentThreadId();
        for (int attempt = 0; attempt < 6; attempt++)
        {
            IntPtr foreground = GetForegroundWindow();
            if (foreground != window)
            {
                uint foregroundThread = GetWindowThreadProcessId(foreground, out _);
                bool attached = false;
                if (foregroundThread != 0 && foregroundThread != workerThread)
                {
                    attached = AttachThreadInput(workerThread, foregroundThread, true);
                }
                SetForegroundWindow(window);
                SetFocus(edit);
                if (attached) AttachThreadInput(workerThread, foregroundThread, false);
            }
            else
            {
                SetFocus(edit);
            }
            await Task.Delay(150);
            if (isEditFocused() && GetForegroundWindow() == window) return null;
        }
        if (GetForegroundWindow() != window) return "another app holds the foreground; select the PC once, then press Talk again";
        return "voice window did not take keyboard focus; select the PC once, then press Talk again";
    }
}
