# sh.ps1 - send a command batch over the pty-free nc shell (TCP 2323) and print output.
# Usage:  powershell -ExecutionPolicy Bypass -File sh.ps1 <commands.sh> [host] [port]
# The single-connection nc shell respawns per connection, so each run is one batch.
# MIT licensed. Use at your own risk; see DISCLAIMER.md.
param(
  [Parameter(Mandatory=$true)][string]$CmdFile,
  [string]$TargetHost = '192.168.0.1',   # the unit's hotspot gateway
  [int]$Port = 2323
)
$cmds = Get-Content -Raw $CmdFile
$c = New-Object System.Net.Sockets.TcpClient
$iar = $c.BeginConnect($TargetHost,$Port,$null,$null)
if(-not ($iar.AsyncWaitHandle.WaitOne(5000) -and $c.Connected)){ Write-Output 'CONNECT_FAIL'; exit 1 }
$c.EndConnect($iar)
$s = $c.GetStream(); $s.ReadTimeout = 20000
$enc = [System.Text.Encoding]::ASCII
# Wrap the batch in markers so we know when it's done, then exit to close the connection.
$payload = "echo __BEGIN__`n" + $cmds + "`necho __END__`nexit`n"
$bytes = $enc.GetBytes($payload)
$s.Write($bytes,0,$bytes.Length); $s.Flush()
$sb = New-Object System.Text.StringBuilder
$buf = New-Object byte[] 4096
try{
  while($true){
    $n = $s.Read($buf,0,$buf.Length)
    if($n -le 0){ break }
    [void]$sb.Append($enc.GetString($buf,0,$n))
    if($sb.ToString() -match '__END__'){ break }
  }
}catch{}
$c.Close()
Write-Output $sb.ToString()
