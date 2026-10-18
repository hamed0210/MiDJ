; Instalador de MiDJ (Inno Setup 6). Compilar despues de la version Release:
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" Installer\MiDJ.iss
; Deja build\installer\MiDJ-Setup-<version>.exe.
;
; Instala el programa con sus DLL (ONNX Runtime y DirectML para los stems), el modelo de stems y
; las DLL de Visual C++ junto al programa (asi no hace falta instalar el paquete de Microsoft).
; Lo demas lo trae Windows 10/11: Direct3D 11, Media Foundation y el motor WebView2 (solo hace
; falta para YouTube; si no esta, se avisa). Los datos del usuario (%APPDATA%\MiDJ: biblioteca,
; escenas, ajustes) no se tocan al instalar ni al desinstalar.

#define AppName "MiDJ"
#define AppVersion "0.2.0"
#define Release "..\build\MiDJ_artefacts\Release"
#define VcRuntime "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"

[Setup]
AppId={{6B3F2C1E-8A4D-4F2B-9C7E-4D1A5E9B7C21}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=MiDJ
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Para el usuario (sin permisos de administrador) o para todos, a eleccion.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir=..\build\installer
OutputBaseFilename=MiDJ-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\MiDJ.exe
CloseApplications=yes

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Release}\MiDJ.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\DirectML.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VcRuntime}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VcRuntime}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VcRuntime}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "LEEME.md"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\MiDJ.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\MiDJ.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MiDJ.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// WebView2 Runtime (el motor de Edge para apps): lo trae Windows 11 y casi todos los Windows 10.
function WebView2Installed(): Boolean;
var
  Version: String;
begin
  Result :=
    (RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0')) or
    (RegQueryStringValue(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  if (CurStep = ssPostInstall) and (not WebView2Installed()) and (not WizardSilent()) then
    if MsgBox('MiDJ esta instalado. Para ver videos de YouTube hace falta el "WebView2 Runtime" de Microsoft, ' +
              'que no esta en este equipo. Abrir la pagina de descarga?', mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://go.microsoft.com/fwlink/p/?LinkId=2124703', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;
