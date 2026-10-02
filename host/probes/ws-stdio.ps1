param([Parameter(Mandatory=$true)][string]$Url)
# Test-only transport: .NET WebSocket <-> newline JSON, no extra packages.
Add-Type -TypeDefinition @'
using System;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
public class ProbeSocket {
 public static async Task Run(string url) {
  using(var ws=new ClientWebSocket()) using(var cts=new CancellationTokenSource(TimeSpan.FromSeconds(90))) {
   await ws.ConnectAsync(new Uri(url),cts.Token);
   var send=Task.Run(async()=>{string line;while((line=await Console.In.ReadLineAsync())!=null){var bytes=Encoding.UTF8.GetBytes(line);await ws.SendAsync(new ArraySegment<byte>(bytes),WebSocketMessageType.Text,true,cts.Token);}});
   var receive=Task.Run(async()=>{var buffer=new byte[65536];while(ws.State==WebSocketState.Open){var text=new StringBuilder();WebSocketReceiveResult result;do{result=await ws.ReceiveAsync(new ArraySegment<byte>(buffer),cts.Token);if(result.MessageType==WebSocketMessageType.Close)return;text.Append(Encoding.UTF8.GetString(buffer,0,result.Count));}while(!result.EndOfMessage);Console.WriteLine(text.ToString());}});
   await Task.WhenAny(send,receive);ws.Abort();
  }
 }
}
'@
[ProbeSocket]::Run($Url).GetAwaiter().GetResult()
