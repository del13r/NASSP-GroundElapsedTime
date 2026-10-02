# Prints each Ground Elapsed Time update published by the Orbiter plugin.
# Usage: powershell -ExecutionPolicy Bypass -File .\Read-GroundElapsedTime.ps1
# Press Ctrl+C to stop. Reconnects automatically if Orbiter restarts.
while ($true) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'GroundElapsedTime', [System.IO.Pipes.PipeDirection]::In)
    try {
        $pipe.Connect(2000)
        Write-Host 'Connected.'
        $buf = New-Object byte[] 256
        while ($pipe.IsConnected) {
            # Byte-mode clients can receive several queued updates in one Read; the newest is the last two lines.
            $n = $pipe.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $lines = [Text.Encoding]::ASCII.GetString($buf, 0, $n).Split("`n", [StringSplitOptions]::RemoveEmptyEntries)
            Write-Host ("GET={0}  acceleration={1}x" -f $lines[-2].Trim(), $lines[-1].Trim())
        }
    } catch [System.TimeoutException] {
        Write-Host 'Waiting for Orbiter plugin...'
    } catch {
        Write-Host "Disconnected: $($_.Exception.Message)"
    } finally {
        $pipe.Dispose()
    }
    Start-Sleep -Seconds 1
}



