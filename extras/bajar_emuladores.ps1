# Descarga los emuladores aparte (los que no existen como núcleo libretro) a dist\emuladores\<nombre>\.
# Lo llama build_windows.bat; también se puede ejecutar solo:
#     powershell -ExecutionPolicy Bypass -File extras\bajar_emuladores.ps1
# Solo baja los que falten: uno ya instalado no se toca (conserva su configuración y sus partidas).
# Son programas de sus autores, con su propia licencia (va dentro de cada carpeta). No incluyen BIOS
# ni juegos: eso lo pone cada usuario.
param([string]$Destino = (Join-Path $PSScriptRoot '..\dist\emuladores'))

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# carpeta, repositorio de GitHub, patrón del archivo a bajar, ejecutable esperado, archivo que activa el modo portátil
$emuladores = @(
    @{ Carpeta = 'xemu';   Repo = 'xemu-project/xemu';                    Patron = '^xemu-win-x86_64-release\.zip$'; Exe = 'xemu.exe';   Portatil = '' },
    @{ Carpeta = 'xenia';  Repo = 'xenia-project/release-builds-windows'; Patron = '^xenia_master\.zip$';            Exe = 'xenia.exe';  Portatil = 'portable.txt' },
    @{ Carpeta = 'cemu';   Repo = 'cemu-project/Cemu';                    Patron = 'windows-x64\.zip$';              Exe = 'Cemu.exe';   Portatil = 'portable\' },
    @{ Carpeta = 'rpcs3';  Repo = 'RPCS3/rpcs3-binaries-win';             Patron = '_win64_msvc\.7z$';               Exe = 'rpcs3.exe';  Portatil = '' },
    @{ Carpeta = 'vita3k'; Repo = 'Vita3K/Vita3K';                        Patron = '^windows-latest\.zip$';          Exe = 'Vita3K.exe'; Portatil = '' }
)

New-Item -ItemType Directory -Force $Destino | Out-Null
$Destino = (Resolve-Path $Destino).Path
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("arcade_emus_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null

foreach ($e in $emuladores) {
    $dir = Join-Path $Destino $e.Carpeta
    if (Test-Path (Join-Path $dir $e.Exe)) { Write-Host "$($e.Carpeta): ya instalado"; continue }
    try {
        $rel = Invoke-RestMethod "https://api.github.com/repos/$($e.Repo)/releases/latest" -Headers @{ 'User-Agent' = 'ArcadeMultijuegos' }
        $asset = $rel.assets | Where-Object { $_.name -match $e.Patron } | Select-Object -First 1
        if (-not $asset) { throw "no hay archivo que coincida con $($e.Patron)" }
        $file = Join-Path $tmp $asset.name
        Write-Host ("{0}: descargando {1} ({2:N0} MB)..." -f $e.Carpeta, $asset.name, ($asset.size / 1MB))
        Invoke-WebRequest $asset.browser_download_url -OutFile $file
        $out = Join-Path $tmp $e.Carpeta
        New-Item -ItemType Directory -Force $out | Out-Null
        if ($file -like '*.zip') { Expand-Archive $file -DestinationPath $out -Force }
        else { & "$env:SystemRoot\System32\tar.exe" -xf $file -C $out; if ($LASTEXITCODE -ne 0) { throw "tar no pudo abrir $($asset.name)" } }
        # Algunos paquetes traen todo dentro de una única carpeta (Cemu_2.6\...)
        $exe = Get-ChildItem $out -Recurse -Filter $e.Exe | Select-Object -First 1
        if (-not $exe) { throw "el paquete no trae $($e.Exe)" }
        New-Item -ItemType Directory -Force $dir | Out-Null
        Copy-Item (Join-Path $exe.DirectoryName '*') $dir -Recurse -Force
        if ($e.Portatil -like '*\') { New-Item -ItemType Directory -Force (Join-Path $dir $e.Portatil) | Out-Null }
        elseif ($e.Portatil) { New-Item -ItemType File -Force (Join-Path $dir $e.Portatil) | Out-Null }
        Write-Host "$($e.Carpeta): instalado ($($rel.tag_name))"
    } catch {
        Write-Host "  no se pudo instalar $($e.Carpeta): $($_.Exception.Message)"
    }
}
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
