unit Unit5;

interface

uses
  System.SysUtils,
  System.Types,
  System.UITypes,
  System.Classes,
  System.Variants,
  System.Threading,
  System.Net.HttpClient,
  System.NetEncoding,
  System.JSON,
  System.IOUtils,
  Data.DB,
  FMX.Types,
  FMX.Controls,
  FMX.Forms,
  FMX.Graphics,
  FMX.Dialogs,
  FMX.Controls.Presentation,
  FMX.StdCtrls,
  FMX.Layouts,
  FMX.ExtCtrls,
  FMX.Edit,
  FMX.ListBox,
  FMX.Colors,
  FireDAC.Stan.Intf,
  FireDAC.Stan.Option,
  FireDAC.Stan.Error,
  FireDAC.UI.Intf,
  FireDAC.Phys.Intf,
  FireDAC.Stan.Def,
  FireDAC.Stan.Pool,
  FireDAC.Stan.Async,
  FireDAC.Phys,
  FireDAC.FMXUI.Wait,
  FireDAC.Stan.Param,
  FireDAC.DatS,
  FireDAC.DApt.Intf,
  FireDAC.DApt,
  FireDAC.Comp.DataSet,
  FireDAC.Comp.Client
  {$IFDEF ANDROID}
  , Androidapi.JNIBridge,
  Androidapi.JNI.Embarcadero,
  Androidapi.JNI.GraphicsContentViewText,
  Androidapi.Helpers,
  Androidapi.JNI.JavaTypes
  {$ENDIF};

type
  TForm5 = class;

  {$IFDEF ANDROID}
  TMyReceiver = class(TJavaLocal, JFMXBroadcastReceiverListener)
  private
    FOwnerForm: TForm5;
  public
    constructor Create(AOwnerForm: TForm5);
    procedure onReceive(context: JContext; intent: JIntent); cdecl;
  end;
  {$ENDIF}

  TForm5 = class(TForm)
    ImageViewer1: TImageViewer;
    Panel1: TPanel;
    Label1: TLabel;
    Label2: TLabel;
    ImageViewer2: TImageViewer;
    FDConnection1: TFDConnection;
    FDQuery1: TFDQuery;
    Label3: TLabel;
    Edit1: TEdit;
    ListBox1: TListBox;
    Button1: TButton;
    ColorButton1: TColorButton;
    ColorButton2: TColorButton;
    ImageViewer3: TImageViewer;
    procedure ImageViewer1Click(Sender: TObject);
    procedure ImageViewer2Click(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure Button1Click(Sender: TObject);
  private
    {$IFDEF ANDROID}
    FMyListener: TMyReceiver;
    FBroadcastReceiver: JFMXBroadcastReceiver;
    {$ENDIF}
    FIsBusy: Boolean;
    function DatabasePath: string;
    procedure EnsureDbConnected;
    procedure InitDatabase;
    procedure LoadCachedNames;
    procedure SavePersonWork(const AKod, APersonName, APersonJob: string);
    procedure SetBusy(const AValue: Boolean);
    procedure HandleScanData(const AScanData: string);
    procedure RequestUserDataAsync(const AKod: string);
    procedure ProcessUserDataResponse(const AKod, AContent: string; const AStatusCode: Integer);
  public
  end;

var
  Form5: TForm5;

implementation

{$R *.fmx}

uses
  Unit1,
  Unit2,
  Unit3;

const
  CApiBaseUrl = 'http://93.85.94.58/Mobile/hs/Test/Userdata/';
  CApiUser = 'Буйвидович_А';
  CApiPassword = '111111';
  CWorksKey = 'Виды работ';

{$IFDEF ANDROID}
{ TMyReceiver }

constructor TMyReceiver.Create(AOwnerForm: TForm5);
begin
  inherited Create;
  FOwnerForm := AOwnerForm;
end;

procedure TMyReceiver.onReceive(context: JContext; intent: JIntent);
var
  ScanData: string;
begin
  if (intent = nil) or (FOwnerForm = nil) then
    Exit;

  ScanData := JStringToString(intent.getStringExtra(StringToJString('barcode_string')));
  TThread.Queue(nil,
    procedure
    begin
      if Assigned(FOwnerForm) then
        FOwnerForm.HandleScanData(ScanData);
    end);
end;
{$ENDIF}

{ TForm5 }

procedure TForm5.Button1Click(Sender: TObject);
begin
  Form3.ImageViewer2.Enabled := True;
end;

function TForm5.DatabasePath: string;
begin
  Result := TPath.Combine(TPath.GetDocumentsPath, 'MyDatabase.db');
end;

procedure TForm5.EnsureDbConnected;
begin
  if FDConnection1.Connected then
    Exit;

  FDConnection1.DriverName := 'SQLite';
  FDConnection1.Params.Values['Database'] := DatabasePath;
  FDConnection1.Connected := True;
end;

procedure TForm5.FormCreate(Sender: TObject);
{$IFDEF ANDROID}
var
  Filter: JIntentFilter;
{$ENDIF}
begin
  InitDatabase;
  LoadCachedNames;
  OnDestroy := FormDestroy;

  {$IFDEF ANDROID}
  FMyListener := TMyReceiver.Create(Self);
  FBroadcastReceiver := TJFMXBroadcastReceiver.JavaClass.init(FMyListener);

  Filter := TJIntentFilter.JavaClass.init;
  Filter.addAction(StringToJString('android.intent.ACTION_DECODE_DATA'));
  TAndroidHelper.Context.getApplicationContext.registerReceiver(FBroadcastReceiver, Filter);
  {$ENDIF}
end;

procedure TForm5.FormDestroy(Sender: TObject);
begin
  {$IFDEF ANDROID}
  if Assigned(FBroadcastReceiver) then
    TAndroidHelper.Context.getApplicationContext.unregisterReceiver(FBroadcastReceiver);
  {$ENDIF}

  FDConnection1.Connected := False;
end;

procedure TForm5.HandleScanData(const AScanData: string);
begin
  Label2.Text := AScanData;
end;

procedure TForm5.ImageViewer1Click(Sender: TObject);
begin
  Form3.Show;
end;

procedure TForm5.ImageViewer2Click(Sender: TObject);
var
  Kod: string;
begin
  Kod := Trim(Label2.Text);
  if (Kod = '') or (Kod = '....................................') then
    Kod := Trim(Edit1.Text);

  if Kod = '' then
  begin
    ShowMessage('Введите или отсканируйте код.');
    Exit;
  end;

  RequestUserDataAsync(Kod);
end;

procedure TForm5.InitDatabase;
begin
  EnsureDbConnected;

  FDQuery1.Connection := FDConnection1;
  FDQuery1.SQL.Text :=
    'CREATE TABLE IF NOT EXISTS Names (' +
    '  ID INTEGER PRIMARY KEY AUTOINCREMENT,' +
    '  kod TEXT NOT NULL,' +
    '  PersonName TEXT NOT NULL,' +
    '  PersonJob TEXT NOT NULL,' +
    '  CreatedDate DATETIME DEFAULT CURRENT_TIMESTAMP' +
    ')';
  FDQuery1.ExecSQL;
end;

procedure TForm5.LoadCachedNames;
var
  RecordCount: Integer;
begin
  EnsureDbConnected;

  FDQuery1.Close;
  FDQuery1.SQL.Text := 'SELECT COUNT(*) AS RecordCount FROM Names';
  FDQuery1.Open;
  try
    RecordCount := FDQuery1.FieldByName('RecordCount').AsInteger;
  finally
    FDQuery1.Close;
  end;

  if RecordCount = 0 then
  begin
    ColorButton2.Visible := True;
    ImageViewer2.Visible := True;
    Exit;
  end;

  FDQuery1.SQL.Text := 'SELECT PersonName FROM Names ORDER BY ID DESC';
  FDQuery1.Open;
  try
    if not FDQuery1.IsEmpty then
    begin
      Label2.Text := FDQuery1.FieldByName('PersonName').AsString;
      Label3.Text := Label2.Text;
      Form3.ImageViewer2.Enabled := True;
      ColorButton2.Visible := False;
      ImageViewer2.Visible := False;
    end;
  finally
    FDQuery1.Close;
  end;
end;

procedure TForm5.ProcessUserDataResponse(const AKod, AContent: string; const AStatusCode: Integer);
var
  JsonValue: TJSONValue;
  MainObj: TJSONObject;
  Pair: TJSONPair;
  PersonObj: TJSONObject;
  WorksValue: TJSONValue;
  WorksArray: TJSONArray;
  J: Integer;
  PersonName: string;
begin
  Label3.Text := AContent;
  ListBox1.Clear;

  if AStatusCode <> 200 then
  begin
    ShowMessage(Format('Ошибка запроса: HTTP %d', [AStatusCode]));
    Exit;
  end;

  JsonValue := TJSONObject.ParseJSONValue(AContent);
  try
    if not (JsonValue is TJSONObject) then
      raise Exception.Create('Некорректный JSON-ответ сервера.');

    MainObj := JsonValue as TJSONObject;

    for Pair in MainObj do
    begin
      PersonName := Pair.JsonString.Value;
      Label3.Text := PersonName;

      if not (Pair.JsonValue is TJSONObject) then
        Continue;

      PersonObj := Pair.JsonValue as TJSONObject;
      WorksValue := PersonObj.GetValue(CWorksKey);
      if not (WorksValue is TJSONArray) then
        Continue;

      WorksArray := WorksValue as TJSONArray;
      ListBox1.Visible := WorksArray.Count > 0;

      for J := 0 to WorksArray.Count - 1 do
      begin
        ListBox1.Items.Add(WorksArray.Items[J].Value);
        SavePersonWork(AKod, PersonName, WorksArray.Items[J].Value);
      end;
    end;

    if Label3.Text <> 'Сотрудник в текущей смене не зарегистрирован' then
    begin
      ImageViewer2.Visible := False;
      Form3.ImageViewer2.Enabled := True;
      Form3.ImageViewer3.Enabled := True;
      ColorButton2.Visible := False;
    end;
  finally
    JsonValue.Free;
  end;
end;

procedure TForm5.RequestUserDataAsync(const AKod: string);
var
  Kod: string;
begin
  if FIsBusy then
    Exit;

  Kod := Trim(AKod);
  if Kod = '' then
    Exit;

  SetBusy(True);
  TTask.Run(
    procedure
    var
      HttpClient: THTTPClient;
      Response: IHTTPResponse;
      Content: string;
      AuthString: string;
      StatusCode: Integer;
    begin
      HttpClient := THTTPClient.Create;
      try
        AuthString := TNetEncoding.Base64.Encode(CApiUser + ':' + CApiPassword);
        HttpClient.CustomHeaders['Authorization'] := 'Basic ' + AuthString;

        Response := HttpClient.Get(CApiBaseUrl + Kod);
        Content := Response.ContentAsString;
        StatusCode := Response.StatusCode;
      finally
        HttpClient.Free;
      end;

      TThread.Queue(nil,
        procedure
        begin
          try
            ProcessUserDataResponse(Kod, Content, StatusCode);
          finally
            SetBusy(False);
          end;
        end);
    end);
end;

procedure TForm5.SavePersonWork(const AKod, APersonName, APersonJob: string);
begin
  EnsureDbConnected;

  FDQuery1.Close;
  FDQuery1.SQL.Text :=
    'INSERT INTO Names (kod, PersonName, PersonJob) VALUES (:kod, :PersonName, :PersonJob)';
  FDQuery1.ParamByName('kod').AsString := AKod;
  FDQuery1.ParamByName('PersonName').AsString := APersonName;
  FDQuery1.ParamByName('PersonJob').AsString := APersonJob;
  FDQuery1.ExecSQL;
end;

procedure TForm5.SetBusy(const AValue: Boolean);
begin
  FIsBusy := AValue;
  ImageViewer2.Enabled := not AValue;
  Button1.Enabled := not AValue;
end;

end.
