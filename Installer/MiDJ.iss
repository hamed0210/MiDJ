; Instalador de MiDJ (Inno Setup 6). Compilar despues de la version Release:
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" Installer\MiDJ.iss
; Deja build\installer\MiDJ-Setup-<version>.exe.
;
; Instala el programa con sus DLL (ONNX Runtime y DirectML para los stems), el modelo de stems y
; las DLL de Visual C++ junto al programa (asi no hace falta instalar el paquete de Microsoft).
; Lo demas lo trae Windows 10/11: Direct3D 11, Media Foundation y el motor WebView2 (solo hace
; falta para YouTube; si no esta, se avisa). Los datos del usuario (%APPDATA%\MiDJ: biblioteca,
; escenas, ajustes) no se tocan al instalar ni al desinstalar.
;
; Actualizar: si MiDJ ya esta instalado, el instalador lo detecta y solo pregunta "Actualizar":
; usa la misma carpeta, el mismo modo (usuario o todos) y los mismos accesos directos, y cierra
; MiDJ si esta abierto. Si lo instalado es mas nuevo, avisa antes de volver a una version anterior.

#define AppName "MiDJ"
#define AppVersion "0.3.9"
#define AppGuid "6B3F2C1E-8A4D-4F2B-9C7E-4D1A5E9B7C21"
#define Release "..\build\MiDJ_artefacts\Release"
#define Logo "..\Resources\Logo"
#define VcRuntime "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher=MiDJ
SetupIconFile={#Logo}\MiDJ.ico
WizardImageFile={#Logo}\wizard-164.png,{#Logo}\wizard-246.png,{#Logo}\wizard-328.png
WizardSmallImageFile={#Logo}\wizard-small-55.png,{#Logo}\wizard-small-82.png,{#Logo}\wizard-small-110.png
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
Source: "..\Resources\Tools\alpha-a-mate.ps1"; DestDir: "{app}\Herramientas"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\MiDJ.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\MiDJ.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MiDJ.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  InstalledVersion: String;   // la version de MiDJ ya instalada ('' = instalacion nueva)

// La version instalada (la de este AppId), para el usuario o para todos.
function FindInstalledVersion(): String;
var
  Key: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + '{#AppGuid}' + '}_is1';
  if not RegQueryStringValue(HKCU, Key, 'DisplayVersion', Result) then
    if not RegQueryStringValue(HKLM, Key, 'DisplayVersion', Result) then
      Result := '';
end;

// Compara versiones "a.b.c": negativo si A es anterior a B, 0 si son iguales, positivo si es posterior.
function CompareVersions(A, B: String): Integer;
var
  PA, PB: Integer;
begin
  Result := 0;
  while (Result = 0) and ((A <> '') or (B <> '')) do
  begin
    PA := Pos('.', A);
    if PA = 0 then
      PA := Length(A) + 1;
    PB := Pos('.', B);
    if PB = 0 then
      PB := Length(B) + 1;
    Result := StrToIntDef(Copy(A, 1, PA - 1), 0) - StrToIntDef(Copy(B, 1, PB - 1), 0);
    Delete(A, 1, PA);
    Delete(B, 1, PB);
  end;
end;

function IsUpdate(): Boolean;
begin
  Result := InstalledVersion <> '';
end;

// Lo que hace este instalador sobre lo que hay: Actualizar, Reinstalar (la misma) o Instalar.
function ActionName(): String;
begin
  if not IsUpdate() or (CompareVersions(InstalledVersion, '{#AppVersion}') > 0) then
    Result := 'Instalar'
  else if CompareVersions(InstalledVersion, '{#AppVersion}') = 0 then
    Result := 'Reinstalar'
  else
    Result := 'Actualizar';
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  InstalledVersion := FindInstalledVersion();
  if IsUpdate() and (CompareVersions(InstalledVersion, '{#AppVersion}') > 0) and not WizardSilent() then
    Result := MsgBox('Ya esta instalado MiDJ ' + InstalledVersion + ', que es mas nuevo que este ({#AppVersion}). ' +
                     'Instalar la version anterior de todos modos?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

// Al actualizar se usan la carpeta y los accesos directos de antes: solo queda la pagina Listo.
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := IsUpdate() and ((PageID = wpSelectDir) or (PageID = wpSelectTasks));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if not IsUpdate() then
    Exit;
  if CurPageID = wpReady then
  begin
    WizardForm.PageNameLabel.Caption := ActionName() + ' MiDJ';
    WizardForm.PageDescriptionLabel.Caption := 'MiDJ ' + InstalledVersion + ' -> {#AppVersion}';
    WizardForm.ReadyLabel.Caption := 'Pulsa ' + ActionName() + ' para continuar. Si MiDJ esta abierto, se cerrara.';
    WizardForm.NextButton.Caption := ActionName();
  end
  else if CurPageID = wpFinished then
    WizardForm.FinishedHeadingLabel.Caption := 'MiDJ {#AppVersion} esta listo';
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';
  if IsUpdate() then
    Result := 'Version instalada: ' + InstalledVersion + NewLine + 'Version nueva: {#AppVersion}' + NewLine + NewLine +
              'Tus canciones, escenas y ajustes (%APPDATA%\MiDJ) se conservan.' + NewLine + NewLine;
  Result := Result + MemoDirInfo;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
end;

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
