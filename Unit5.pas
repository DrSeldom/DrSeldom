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
  ,Androidapi.JNIBridge,
  Androidapi.JNI.Embarcadero,
  Androidapi.JNI.GraphicsContentViewText,
  Androidapi.Helpers,
  Androidapi.JNI.JavaTypes
  {$ENDIF}
  ;

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
    procedure SavePersonOnly(const AKod, APersonName: string);
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
  if Assigned(Form3) then
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
  FIsBusy := False;
  InitDatabase;
  LoadCachedNames;

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
  begin
    try
      TAndroidHelper.Context.getApplicationContext.unregisterReceiver(FBroadcastReceiver);
    except
      // receiver might be already unregistered by OS lifecycle
    end;
  end;
  {$ENDIF}

  FDConnection1.Connected := False;
end;

procedure TForm5.HandleScanData(const AScanData: string);
begin
  Label2.Text := AScanData;
  if AScanData <> '' then
    RequestUserDataAsync(AScanData);
end;

procedure TForm5.ImageViewer1Click(Sender: TObject);
begin
  if Assigned(Form3) then
    Form3.Show;
end;

procedure TForm5.ImageViewer2Click(Sender: TObject);
var
  Kod: string;
begin
  Kod := Trim(Label2.Text);
  if Kod = '' then
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

  FDQuery1.SQL.Text :=
    'CREATE UNIQUE INDEX IF NOT EXISTS idx_names_unique ON Names(kod, PersonName, PersonJob)';
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

  FDQuery1.SQL.Text :=
    'SELECT PersonName FROM Names ORDER BY CreatedDate DESC, ID DESC LIMIT 1';
  FDQuery1.Open;
  try
    if not FDQuery1.IsEmpty then
    begin
      Label2.Text := FDQuery1.FieldByName('PersonName').AsString;
      Label3.Text := Label2.Text;
      if Assigned(Form3) then
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
  WorksArray: TJSONArray;
  WorkItem: TJSONValue;
  J: Integer;
  PersonName: string;
  HasWorks: Boolean;
begin
  Label3.Text := AContent;
  ListBox1.Clear;

  if AStatusCode <> 200 then
  begin
    if AStatusCode < 0 then
      ShowMessage('Ошибка сети: ' + AContent)
    else
      ShowMessage(Format('Ошибка запроса: HTTP %d', [AStatusCode]));
    Exit;
  end;

  JsonValue := TJSONObject.ParseJSONValue(AContent);
  try
    if not (JsonValue is TJSONObject) then
      raise Exception.Create('Некорректный JSON-ответ сервера.');

    MainObj := JsonValue as TJSONObject;
    HasWorks := False;

    for Pair in MainObj do
    begin
      PersonName := Pair.JsonString.Value;
      Label3.Text := PersonName;
      SavePersonOnly(AKod, PersonName);

      if not (Pair.JsonValue is TJSONObject) then
        Continue;

      PersonObj := Pair.JsonValue as TJSONObject;
      WorksArray := PersonObj.GetValue<TJSONArray>(CWorksKey, nil);
      if WorksArray = nil then
        Continue;

      for J := 0 to WorksArray.Count - 1 do
      begin
        WorkItem := WorksArray.Items[J];
        if WorkItem = nil then
          Continue;

        ListBox1.Items.Add(WorkItem.Value);
        SavePersonWork(AKod, PersonName, WorkItem.Value);
        HasWorks := True;
      end;
    end;

    if (Label3.Text <> 'Сотрудник в текущей смене не зарегистрирован') and HasWorks then
    begin
      ImageViewer2.Visible := False;
      if Assigned(Form3) then
      begin
        Form3.ImageViewer2.Enabled := True;
        Form3.ImageViewer3.Enabled := True;
      end;
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
      Content := '';
      StatusCode := -1;

      HttpClient := THTTPClient.Create;
      try
        try
          AuthString := TNetEncoding.Base64.Encode('Буйвидович_А:111111');
          HttpClient.CustomHeaders['Authorization'] := 'Basic ' + AuthString;
          HttpClient.ConnectionTimeout := 10000;
          HttpClient.ResponseTimeout := 15000;

          Response := HttpClient.Get(CApiBaseUrl + Kod);
          Content := Response.ContentAsString;
          StatusCode := Response.StatusCode;
        except
          on E: Exception do
          begin
            Content := E.Message;
            StatusCode := -1;
          end;
        end;
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
  if Trim(APersonJob) = '' then
    Exit;

  EnsureDbConnected;

  FDQuery1.Close;
  FDQuery1.SQL.Text :=
    'INSERT OR IGNORE INTO Names (kod, PersonName, PersonJob) VALUES (:kod, :PersonName, :PersonJob)';
  FDQuery1.ParamByName('kod').AsString := AKod;
  FDQuery1.ParamByName('PersonName').AsString := APersonName;
  FDQuery1.ParamByName('PersonJob').AsString := APersonJob;
  FDQuery1.ExecSQL;
end;

procedure TForm5.SavePersonOnly(const AKod, APersonName: string);
begin
  if Trim(APersonName) = '' then
    Exit;

  SavePersonWork(AKod, APersonName, '-');
end;

procedure TForm5.SetBusy(const AValue: Boolean);
begin
  FIsBusy := AValue;
  ImageViewer2.Enabled := not AValue;
  Button1.Enabled := not AValue;
end;

end.
