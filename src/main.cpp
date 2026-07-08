/*
 * HamQSL Solar Propagation Monitor
 * Waveshare ESP32-C6-LCD-1.47 | ST7789 320x172 landscape
 * Biblioteka: LovyanGFX
 *
 * Ekrany: 0=HF  1=Zegar  2=DX Spots  3=Pogoda
 * Panel:  http://<IP>/     - status + zmiana lokalizacji pogody
 *         http://<IP>/log  - debug log
 */

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
public:
    LGFX() {
        auto bcfg = _bus.config();
        bcfg.spi_host    = SPI2_HOST;
        bcfg.spi_mode    = 0;
        bcfg.freq_write  = 40000000;
        bcfg.spi_3wire   = true;
        bcfg.use_lock    = true;
        bcfg.dma_channel = SPI_DMA_CH_AUTO;
        bcfg.pin_sclk    = 7;
        bcfg.pin_mosi    = 6;
        bcfg.pin_miso    = -1;
        bcfg.pin_dc      = 15;
        _bus.config(bcfg);
        _panel.setBus(&_bus);
        auto pcfg = _panel.config();
        pcfg.pin_cs=14; pcfg.pin_rst=21; pcfg.pin_busy=-1;
        pcfg.panel_width=172; pcfg.panel_height=320;
        pcfg.offset_x=34; pcfg.offset_y=0;
        pcfg.invert=true; pcfg.rgb_order=false; pcfg.bus_shared=true;
        _panel.config(pcfg);
        auto lcfg = _light.config();
        lcfg.pin_bl=22; lcfg.invert=false; lcfg.freq=5000; lcfg.pwm_channel=0;
        _light.config(lcfg);
        _panel.setLight(&_light);
        setPanel(&_panel);
    }
};
LGFX tft;

#define C_BG     TFT_BLACK
#define C_WHITE  TFT_WHITE
#define C_GREEN  TFT_GREEN
#define C_RED    TFT_RED
#define C_ORANGE 0xFD20
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_LGRAY  0x8410
#define C_DGRAY  0x4208
#define C_ROW2   0x1082
#define C_LINE   0x3186

#define RESET_PIN     9
#define UPD_HF        3600000UL
#define UPD_DX        300000UL
#define UPD_WX        900000UL
#define UPD_CLOCK     1000UL
#define PAGE_INTERVAL 10000UL
#define NUM_PAGES     4
#define XML_URL       "https://www.hamqsl.com/solarxml.php"
#define DX_URL        "http://www.dxsummit.fi/api/v1/spots?limit=5"
#define WIFI_TIMEOUT  180
#define BL_BRIGHT     120
#define TZ_PL         "CET-1CEST,M3.5.0,M10.5.0/3"
#define LOG_MAX       4096

#define DEF_LAT   "50.110"
#define DEF_LON   "22.019"
#define DEF_NAME  "Jasionka"

String logBuf = "";
void addLog(const String& msg) {
    logBuf += msg + "\n";
    if (logBuf.length() > LOG_MAX)
        logBuf = logBuf.substring(logBuf.length() - LOG_MAX);
    Serial.println(msg);
}

struct SolarData {
    String solarflux, aindex, kindex, sunspots;
    int    prop[4][2];
    bool   valid = false;
};
const char* BAND_NAMES[4] = {"80m-40m","30m-20m","17m-15m","12m-10m"};
const char* BAND_DISP[4]  = {"80-40m","30-20m","17-15m","12-10m"};
SolarData sd;

struct DxSpot { String band, callsign, freq; };
DxSpot dxSpots[5];
int    dxCount = 0;

struct Weather {
    float temp=0, pressure=0, windSpeed=0, pressTrend=0;
    int   windDir=0, humidity=0, wcode=-1;
    bool  valid = false;
};
Weather wx;
String wxLat, wxLon, wxName;
Preferences prefs;

unsigned long lastHF=0, lastDX=0, lastWX=0, lastClk=0, pageT=0;
int  page = 0;
bool wifiOK = false;
WebServer webServer(80);

void setupWiFi();
void setupWebServer();
bool fetchHF();
bool fetchDX();
bool fetchWX();
String xmlTag(const String& x, const String& t);
String xmlAttr(const String& t, const String& a);
int toCond(const String& v);
uint32_t condCol(int c);
String freqToBand(float f);
String jsonStr(const String& obj, const String& key);
void drawCentered(int ax, int aw, int y, const char* txt, uint32_t col);
void drawDots(int active);
void drawHF();
void drawClock();
void drawDX();
void drawWX();
void drawPage();
void showMsg(const char* l1, const char* l2="", uint32_t col=C_YELLOW);
void showIP();
const char* wcodeDesc(int c);
const char* windDirName(int deg);

void setup() {
    Serial.begin(115200);
    delay(300);
    tft.init();
    tft.setRotation(3);
    tft.setBrightness(BL_BRIGHT);
    tft.fillScreen(C_BG);

    prefs.begin("hamqsl", false);
    wxLat  = prefs.getString("lat",  DEF_LAT);
    wxLon  = prefs.getString("lon",  DEF_LON);
    wxName = prefs.getString("name", DEF_NAME);
    prefs.end();

    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        showMsg("Reset WiFi...", "", C_RED);
        WiFiManager wm; wm.resetSettings(); delay(1000);
    }

    setupWiFi();

    if (wifiOK) {
        showIP();
        delay(3000);
        configTzTime(TZ_PL, "pool.ntp.org", "time.cloudflare.com");
        showMsg("Sync NTP...");
        for (int i=0;i<50;i++){ delay(100); if(time(nullptr)>1700000000UL) break; }
        setupWebServer();
        showMsg("Pobieranie HF...");     fetchHF();
        showMsg("Pobieranie DX...");     fetchDX();
        showMsg("Pobieranie pogody...", wxName.c_str()); fetchWX();
        lastHF=lastDX=lastWX=lastClk=pageT=millis();
        drawHF();
    }
}

void loop() {
    webServer.handleClient();
    unsigned long now = millis();

    if (now - pageT > PAGE_INTERVAL) {
        page = (page + 1) % NUM_PAGES;
        drawPage();
        pageT = now;
        if (page == 1) lastClk = now;
    }

    if (page==1 && now-lastClk > UPD_CLOCK) { drawClock(); lastClk=now; }

    if (wifiOK) {
        if (now-lastHF > UPD_HF) { if(fetchHF()) lastHF=now; }
        if (now-lastDX > UPD_DX) { if(fetchDX()) { lastDX=now; if(page==2) drawDX(); } }
        if (now-lastWX > UPD_WX) { if(fetchWX()) { lastWX=now; if(page==3) drawWX(); } }
    }

    if (WiFi.status()!=WL_CONNECTED && wifiOK) {
        wifiOK=false;
        addLog("[WIFI] Rozlaczono");
        WiFi.reconnect();
        for(int i=0;i<30 && WiFi.status()!=WL_CONNECTED;i++) delay(1000);
        wifiOK=(WiFi.status()==WL_CONNECTED);
        if(wifiOK) addLog("[WIFI] Reconnect OK");
    }
    delay(50);
}

void drawPage() {
    switch(page) {
        case 0: drawHF();    break;
        case 1: drawClock(); break;
        case 2: drawDX();    break;
        case 3: drawWX();    break;
    }
}

void showIP() {
    tft.fillScreen(C_BG);
    tft.setFont(&fonts::Font4);
    tft.setTextColor(C_GREEN);
    tft.setCursor(6,10); tft.print("WiFi OK!");
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.setCursor(6,40); tft.print("Adres IP:");
    tft.setFont(&fonts::Font4);
    tft.setTextColor(C_YELLOW);
    String ip = WiFi.localIP().toString();
    tft.setCursor(6,56); tft.print(ip);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(6,88); tft.print("Panel: http://" + ip + "/");
    tft.setTextColor(C_DGRAY);
    tft.setCursor(6,106); tft.print("Log:   http://" + ip + "/log");
    addLog("[WIFI] IP: " + ip);
}

void showMsg(const char* l1, const char* l2, uint32_t col) {
    tft.fillScreen(C_BG);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_DGRAY);
    tft.setCursor(4,4); tft.print("HamQSL Monitor");
    tft.setFont(&fonts::Font4);
    tft.setTextColor(col);
    tft.setCursor(6, strlen(l2)==0?75:55); tft.print(l1);
    if(strlen(l2)>0){
        tft.setFont(&fonts::Font2);
        tft.setTextColor(C_DGRAY);
        tft.setCursor(6,90); tft.print(l2);
    }
}

void setupWiFi() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_TIMEOUT);
    wm.setConnectTimeout(30);
    wm.setAPCallback([](WiFiManager*) {
        tft.fillScreen(C_BG);
        tft.setFont(&fonts::Font4);
        tft.setTextColor(C_WHITE);
        tft.setCursor(6,8); tft.print("KONFIGURACJA WiFi");
        tft.setFont(&fonts::Font2);
        tft.setTextColor(C_LGRAY);
        tft.setCursor(6,38); tft.print("Polacz sie:");
        tft.setFont(&fonts::Font4);
        tft.setTextColor(C_GREEN);
        tft.setCursor(6,54); tft.print("HamQSL-Setup");
        tft.setFont(&fonts::Font2);
        tft.setTextColor(C_LGRAY);
        tft.setCursor(6,82); tft.print("(bez hasla) => 192.168.4.1");
    });
    if(wm.autoConnect("HamQSL-Setup")) wifiOK=true;
    else { showMsg("Brak WiFi - restart","",C_RED); delay(4000); ESP.restart(); }
}

void setupWebServer() {
    webServer.on("/", HTTP_GET, [](){
        String tempStr  = wx.valid ? String(wx.temp,1)+" C" : "---";
        String presStr  = wx.valid ? String(wx.pressure,0)+" hPa" : "---";
        String trendStr = wx.valid ? String(wx.pressTrend>0?"+":"")+String(wx.pressTrend,1) : "---";
        String html =
        "<html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>HamQSL Monitor</title>"
        "<style>body{background:#111;color:#ddd;font-family:sans-serif;padding:20px;max-width:500px;margin:auto}"
        "h2{color:#ffd700}h3{color:#0cf}input{background:#222;color:#fff;border:1px solid #555;"
        "padding:8px;border-radius:4px;width:100%;box-sizing:border-box;margin:4px 0}"
        "button{background:#0a6e0a;color:#fff;border:0;padding:10px 24px;border-radius:4px;"
        "cursor:pointer;margin-top:8px;font-size:15px}"
        "a{color:#0cf}.box{background:#1a1a1a;padding:14px;border-radius:8px;margin:12px 0}"
        "label{color:#999;font-size:13px}</style></head><body>"
        "<h2>HamQSL Solar Monitor</h2>"
        "<div class='box'><h3>Pogoda: " + wxName + "</h3>"
        "Temperatura: <b>" + tempStr + "</b><br>"
        "Cisnienie: <b>" + presStr + "</b> (trend 3h: " + trendStr + " hPa)<br>"
        "Lokalizacja: " + wxLat + ", " + wxLon + "</div>"
        "<div class='box'><h3>Zmien lokalizacje pogody</h3>"
        "<form action='/setloc' method='get'>"
        "<label>Nazwa (wyswietlana):</label>"
        "<input name='name' value='" + wxName + "' maxlength='20'>"
        "<label>Szerokosc geogr. (lat):</label>"
        "<input name='lat' value='" + wxLat + "'>"
        "<label>Dlugosc geogr. (lon):</label>"
        "<input name='lon' value='" + wxLon + "'>"
        "<button type='submit'>Zapisz</button></form>"
        "<p style='font-size:12px;color:#777'>Przyklady: Jasionka 50.110, 22.019 | "
        "Rzeszow 50.041, 21.999 | Solina 49.395, 22.462</p></div>"
        "<div class='box'>Uptime: " + String(millis()/1000) + "s | "
        "<a href='/log'>Debug log</a> | <a href='/refresh'>Odswiez dane</a></div>"
        "</body></html>";
        webServer.send(200, "text/html", html);
    });

    webServer.on("/setloc", HTTP_GET, [](){
        if (webServer.hasArg("lat") && webServer.hasArg("lon")) {
            wxLat = webServer.arg("lat");
            wxLon = webServer.arg("lon");
            if (webServer.hasArg("name") && webServer.arg("name").length()>0)
                wxName = webServer.arg("name");
            prefs.begin("hamqsl", false);
            prefs.putString("lat",  wxLat);
            prefs.putString("lon",  wxLon);
            prefs.putString("name", wxName);
            prefs.end();
            addLog("[WX] Nowa lokalizacja: " + wxName + " (" + wxLat + "," + wxLon + ")");
            fetchWX();
            if (page==3) drawWX();
        }
        webServer.sendHeader("Location","/");
        webServer.send(302);
    });

    webServer.on("/log", HTTP_GET, [](){
        webServer.send(200, "text/html",
            "<html><head><meta charset='utf-8'>"
            "<meta http-equiv='refresh' content='5'>"
            "<style>body{background:#111;color:#0f0;font-family:monospace;font-size:13px;padding:10px}"
            "pre{white-space:pre-wrap}a{color:#0cf}</style></head><body>"
            "<a href='/'>&lt; Powrot</a> | auto-refresh 5s<br><br>"
            "<pre>" + logBuf + "</pre></body></html>");
    });

    webServer.on("/refresh", HTTP_GET, [](){
        fetchHF(); fetchDX(); fetchWX();
        drawPage();
        webServer.sendHeader("Location","/");
        webServer.send(302);
    });

    webServer.begin();
    addLog("[WEB] Panel: http://" + WiFi.localIP().toString() + "/");
}

String xmlTag(const String& x, const String& t) {
    String o="<"+t+">", c="</"+t+">";
    int s=x.indexOf(o); if(s<0) return "";
    s+=o.length(); int e=x.indexOf(c,s); if(e<0) return "";
    String v=x.substring(s,e); v.trim(); return v;
}
String xmlAttr(const String& t, const String& a) {
    String s=a+"=\""; int p=t.indexOf(s); if(p<0) return "";
    p+=s.length(); int e=t.indexOf("\"",p); return t.substring(p,e);
}
int toCond(const String& v) {
    String l=v; l.toLowerCase(); l.trim();
    if(l=="good") return 2; if(l=="fair") return 1; if(l=="poor") return 0; return -1;
}
uint32_t condCol(int c) {
    if(c==2) return C_GREEN; if(c==1) return C_ORANGE; if(c==0) return C_RED; return C_LGRAY;
}
String jsonStr(const String& obj, const String& key) {
    String s="\""+key+"\":\""; int a=obj.indexOf(s);
    if(a>=0){ a+=s.length(); int b=obj.indexOf("\"",a); return obj.substring(a,b); }
    s="\""+key+"\":"; a=obj.indexOf(s);
    if(a<0) return "";
    a+=s.length();
    while(a<(int)obj.length() && obj[a]==' ') a++;
    int b=a;
    while(b<(int)obj.length() && obj[b]!=',' && obj[b]!='}' && obj[b]!=']') b++;
    String v=obj.substring(a,b); v.trim(); return v;
}
String freqToBand(float f) {
    if(f>=1800&&f<=2000)   return "160m";
    if(f>=3500&&f<=4000)   return "80m";
    if(f>=5351&&f<=5367)   return "60m";
    if(f>=7000&&f<=7300)   return "40m";
    if(f>=10100&&f<=10150) return "30m";
    if(f>=14000&&f<=14350) return "20m";
    if(f>=18068&&f<=18168) return "17m";
    if(f>=21000&&f<=21450) return "15m";
    if(f>=24890&&f<=24990) return "12m";
    if(f>=28000&&f<=29700) return "10m";
    if(f>=50000&&f<=54000) return "6m";
    if(f>=144000&&f<=148000) return "2m";
    return "?";
}

bool fetchHF() {
    addLog("[HF] GET...");
    WiFiClientSecure cl; cl.setInsecure();
    HTTPClient http; http.begin(cl, XML_URL);
    http.setTimeout(20000);
    http.addHeader("User-Agent","ESP32C6/1.0");
    int code=http.GET();
    if(code!=HTTP_CODE_OK){ addLog("[HF] err "+String(code)); http.end(); return false; }
    String xml=http.getString(); http.end();
    if(xml.length()<200) return false;
    sd.solarflux=xmlTag(xml,"solarflux");
    sd.kindex=xmlTag(xml,"kindex");
    sd.aindex=xmlTag(xml,"aindex");
    sd.sunspots=xmlTag(xml,"sunspots");
    for(int i=0;i<4;i++) sd.prop[i][0]=sd.prop[i][1]=-1;
    int pos=0;
    while(true){
        int s=xml.indexOf("<band ",pos); if(s<0) break;
        int c=xml.indexOf(">",s), e=xml.indexOf("</band>",c);
        if(c<0||e<0) break;
        String ot=xml.substring(s,c+1);
        String val=xml.substring(c+1,e); val.trim();
        String name=xmlAttr(ot,"name"), tm=xmlAttr(ot,"time");
        int ti=(tm=="day")?0:(tm=="night")?1:-1;
        if(ti>=0) for(int i=0;i<4;i++) if(name==BAND_NAMES[i]){sd.prop[i][ti]=toCond(val);break;}
        pos=e+7;
    }
    sd.valid=true;
    addLog("[HF] OK SFI="+sd.solarflux+" K="+sd.kindex);
    return true;
}

bool fetchDX() {
    addLog("[DX] GET...");
    WiFiClient cl; HTTPClient http;
    http.begin(cl, DX_URL);
    http.setTimeout(15000);
    http.addHeader("User-Agent","ESP32C6/1.0");
    int code=http.GET();
    if(code!=HTTP_CODE_OK){ addLog("[DX] err "+String(code)); http.end(); return false; }
    String json=http.getString(); http.end();
    if(json.length()<10) return false;
    dxCount=0; int pos=0;
    while(dxCount<5){
        int s=json.indexOf("{",pos); if(s<0) break;
        int depth=1, e=s+1;
        while(e<(int)json.length()&&depth>0){
            if(json[e]=='{')depth++; else if(json[e]=='}')depth--; e++;
        }
        String obj=json.substring(s,e);
        DxSpot sp;
        sp.callsign=jsonStr(obj,"dx_call");
        String fq=jsonStr(obj,"frequency");
        if(!fq.isEmpty()){
            float fk=fq.toFloat();
            sp.band=freqToBand(fk);
            char b[10]; snprintf(b,sizeof(b),"%.3f",fk/1000.0f);
            sp.freq=String(b);
        } else { sp.band="?"; sp.freq="?"; }
        if(!sp.callsign.isEmpty()) dxSpots[dxCount++]=sp;
        pos=e;
    }
    addLog("[DX] OK spotow: "+String(dxCount));
    return dxCount>0;
}

bool fetchWX() {
    addLog("[WX] GET " + wxName + "...");
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + wxLat +
                 "&longitude=" + wxLon +
                 "&current=temperature_2m,relative_humidity_2m,surface_pressure,"
                 "wind_speed_10m,wind_direction_10m,weather_code"
                 "&hourly=surface_pressure&past_hours=3&forecast_hours=1"
                 "&wind_speed_unit=kmh&timezone=auto";

    WiFiClientSecure cl; cl.setInsecure();
    HTTPClient http; http.begin(cl, url);
    http.setTimeout(15000);
    http.addHeader("User-Agent","ESP32C6-HamQSL/1.0");
    int code=http.GET();
    if(code!=HTTP_CODE_OK){ addLog("[WX] err "+String(code)); http.end(); return false; }
    String json=http.getString(); http.end();
    addLog("[WX] JSON len="+String(json.length()));
    if(json.length()<100) return false;

    int cs = json.indexOf("\"current\":");
    if(cs<0){ addLog("[WX] brak sekcji current!"); return false; }
    int ce = json.indexOf("}", cs);
    String cur = json.substring(cs, ce+1);

    wx.temp      = jsonStr(cur,"temperature_2m").toFloat();
    wx.humidity  = jsonStr(cur,"relative_humidity_2m").toInt();
    wx.pressure  = jsonStr(cur,"surface_pressure").toFloat();
    wx.windSpeed = jsonStr(cur,"wind_speed_10m").toFloat();
    wx.windDir   = jsonStr(cur,"wind_direction_10m").toInt();
    wx.wcode     = jsonStr(cur,"weather_code").toInt();

    wx.pressTrend = 0;
    int hs = json.indexOf("\"surface_pressure\":[", ce);
    if (hs < 0) hs = json.lastIndexOf("\"surface_pressure\":[");
    if (hs >= 0) {
        int arrS = hs + 20;
        int he = json.indexOf("]", arrS);
        String arr = json.substring(arrS, he);
        int comma1 = arr.indexOf(",");
        if (comma1 > 0) {
            float firstP = arr.substring(0, comma1).toFloat();
            int lastC = arr.lastIndexOf(",");
            float lastP = arr.substring(lastC+1).toFloat();
            if (firstP > 800 && lastP > 800)
                wx.pressTrend = lastP - firstP;
        }
    }

    wx.valid = true;
    addLog("[WX] OK T="+String(wx.temp,1)+" P="+String(wx.pressure,0)+
           " trend="+String(wx.pressTrend,1)+" W="+String(wx.windSpeed,0));
    return true;
}

const char* wcodeDesc(int c) {
    if(c==0) return "Bezchmurnie";
    if(c<=2) return "Czesc. pochm.";
    if(c==3) return "Pochmurno";
    if(c<=48) return "Mgla";
    if(c<=55) return "Mzawka";
    if(c<=65) return "Deszcz";
    if(c<=77) return "Snieg";
    if(c<=82) return "Przelotny d.";
    if(c<=86) return "Przelotny sn.";
    if(c<=99) return "Burza";
    return "---";
}

const char* windDirName(int deg) {
    const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    return dirs[((deg + 22) / 45) % 8];
}

void drawCentered(int ax, int aw, int y, const char* txt, uint32_t col) {
    tft.setTextColor(col);
    int tw = tft.textWidth(txt);
    int cx = ax + (aw - tw) / 2;
    if (cx < ax) cx = ax;
    tft.setCursor(cx, y);
    tft.print(txt);
}

void drawDots(int active) {
    int startX = 160 - (NUM_PAGES-1)*6;
    for (int i=0;i<NUM_PAGES;i++) {
        if (i==active) tft.fillCircle(startX + i*12, 167, 3, C_WHITE);
        else           tft.drawCircle(startX + i*12, 167, 3, C_LGRAY);
    }
}

void drawHF() {
    tft.fillScreen(C_BG);
    const int CA=0, CAw=95, CB=95, CBw=112, CC=207, CCw=113;
    const int HDR=16, ROWH=36;

    tft.drawFastVLine(CB, 0, 160, C_LINE);
    tft.drawFastVLine(CC, 0, 160, C_LINE);
    tft.drawFastHLine(0, HDR, 320, C_LINE);

    tft.setFont(&fonts::Font2);
    drawCentered(CA, CAw, 3, "PASMO", C_LGRAY);
    drawCentered(CB, CBw, 3, "DZIEN", C_GREEN);
    drawCentered(CC, CCw, 3, "NOC",   C_CYAN);

    for(int i=0;i<4;i++){
        int y = HDR + i*ROWH + 1;
        tft.fillRect(0, y, 320, ROWH-1, (i%2==0)?C_BG:C_ROW2);
        if(i>0) tft.drawFastHLine(0, y, 320, C_LINE);
        tft.setFont(&fonts::Font4);
        drawCentered(CA, CAw, y+8, BAND_DISP[i], C_WHITE);
        const char* dt; switch(sd.prop[i][0]){case 2:dt="Good";break;case 1:dt="Fair";break;case 0:dt="Poor";break;default:dt="---";}
        const char* nt; switch(sd.prop[i][1]){case 2:nt="Good";break;case 1:nt="Fair";break;case 0:nt="Poor";break;default:nt="---";}
        drawCentered(CB, CBw, y+8, dt, condCol(sd.prop[i][0]));
        drawCentered(CC, CCw, y+8, nt, condCol(sd.prop[i][1]));
    }
    drawDots(0);
}

void drawClock() {
    tft.fillScreen(C_BG);
    struct tm tmPL, tmUTC;
    time_t now = time(nullptr);
    localtime_r(&now, &tmPL);
    gmtime_r(&now, &tmUTC);
    bool valid = (now > 1700000000UL);

    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.setCursor(6, 4); tft.print("POLSKA");
    char tz[8]; strftime(tz,sizeof(tz),"%Z",&tmPL);
    tft.setTextColor(C_DGRAY);
    tft.setCursor(66, 4); tft.print(tz);

    tft.setFont(&fonts::Font6);
    if(valid){
        char b[9]; snprintf(b,sizeof(b),"%02d:%02d:%02d",tmPL.tm_hour,tmPL.tm_min,tmPL.tm_sec);
        drawCentered(0,320,16,b,C_WHITE);
    } else drawCentered(0,320,16,"--:--:--",C_DGRAY);

    tft.setFont(&fonts::Font2);
    if(valid){
        char d[12]; snprintf(d,sizeof(d),"%02d.%02d.%04d",tmPL.tm_mday,tmPL.tm_mon+1,tmPL.tm_year+1900);
        drawCentered(0,320,60,d,C_LGRAY);
    }

    tft.drawFastHLine(10,78,300,C_LINE);

    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(6,84); tft.print("UTC");

    tft.setFont(&fonts::Font6);
    if(valid){
        char u[9]; snprintf(u,sizeof(u),"%02d:%02d:%02d",tmUTC.tm_hour,tmUTC.tm_min,tmUTC.tm_sec);
        drawCentered(0,320,96,u,C_CYAN);
    } else drawCentered(0,320,96,"--:--:--",C_DGRAY);

    drawDots(1);
}

void drawDX() {
    tft.fillScreen(C_BG);
    const int BA=0,BAw=60, CA=60,CAw=134, FA=194,FAw=126;
    const int HDR=16, ROWH=27;

    tft.drawFastVLine(CA,0,152,C_LINE);
    tft.drawFastVLine(FA,0,152,C_LINE);
    tft.drawFastHLine(0,HDR,320,C_LINE);

    tft.setFont(&fonts::Font2);
    drawCentered(BA,BAw,3,"BAND",C_LGRAY);
    drawCentered(CA,CAw,3,"ZNAK",C_YELLOW);
    drawCentered(FA,FAw,3,"MHz",C_CYAN);

    if(dxCount==0){
        tft.setFont(&fonts::Font4);
        drawCentered(0,320,70,"Brak spotow",C_DGRAY);
    }

    auto bcolor=[](const String& b)->uint32_t{
        if(b=="160m")return 0xF81F; if(b=="80m")return 0xFBE0;
        if(b=="60m")return 0xFFFF;  if(b=="40m")return 0xFFE0;
        if(b=="30m")return 0x07FF;  if(b=="20m")return (uint32_t)TFT_GREEN;
        if(b=="17m")return 0x07E0;  if(b=="15m")return 0x3AFF;
        if(b=="12m")return 0xFD20;  if(b=="10m")return (uint32_t)TFT_RED;
        if(b=="6m")return 0xF81F;   if(b=="2m")return 0x001F;
        return 0x8410U;
    };

    for(int i=0;i<min(dxCount,5);i++){
        int y=HDR+i*ROWH+1;
        tft.fillRect(0,y,320,ROWH-1,(i%2==0)?C_BG:C_ROW2);
        if(i>0) tft.drawFastHLine(0,y,320,C_LINE);
        tft.setFont(&fonts::Font4);
        String band=dxSpots[i].band;
        drawCentered(BA,BAw,y+4,band.c_str(),bcolor(band));
        String call=dxSpots[i].callsign;
        if(call.length()>9) call=call.substring(0,9);
        drawCentered(CA,CAw,y+4,call.c_str(),C_WHITE);
        drawCentered(FA,FAw,y+4,dxSpots[i].freq.c_str(),C_CYAN);
    }

    tft.drawFastHLine(0,153,320,C_LINE);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_DGRAY);
    tft.setCursor(4,156); tft.print("dxsummit.fi");
    drawDots(2);
}

void drawWX() {
    tft.fillScreen(C_BG);

    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.setCursor(6, 4); tft.print(wxName);
    if (wx.valid) {
        const char* desc = wcodeDesc(wx.wcode);
        int dw = tft.textWidth(desc);
        tft.setTextColor(C_CYAN);
        tft.setCursor(314 - dw, 4);
        tft.print(desc);
    }
    tft.drawFastHLine(0, 18, 320, C_LINE);

    if (!wx.valid) {
        tft.setFont(&fonts::Font4);
        drawCentered(0,320,70,"Brak danych pogody",C_DGRAY);
        drawDots(3);
        return;
    }

    tft.drawFastVLine(150, 18, 138, C_LINE);

    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.1f", wx.temp);
    tft.setFont(&fonts::Font6);
    uint32_t tc = (wx.temp <= 0) ? C_CYAN : (wx.temp >= 28) ? C_RED : C_WHITE;
    drawCentered(0, 150, 42, tempStr, tc);

    tft.setFont(&fonts::Font4);
    drawCentered(0, 150, 90, "st.C", C_LGRAY);

    tft.setFont(&fonts::Font2);
    char humStr[16];
    snprintf(humStr, sizeof(humStr), "Wilg: %d%%", wx.humidity);
    drawCentered(0, 150, 126, humStr, C_LGRAY);

    int rx = 152;

    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.setCursor(rx+6, 26); tft.print("CISNIENIE");
    char presStr[12];
    snprintf(presStr, sizeof(presStr), "%.0f", wx.pressure);
    tft.setFont(&fonts::Font4);
    tft.setTextColor(C_YELLOW);
    tft.setCursor(rx+6, 40); tft.print(presStr);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.print(" hPa");

    tft.setFont(&fonts::Font2);
    uint32_t trendCol = C_LGRAY;
    const char* trendSym = "=";
    if (wx.pressTrend > 0.5)      { trendSym="^"; trendCol=C_GREEN; }
    else if (wx.pressTrend < -0.5){ trendSym="v"; trendCol=C_RED; }
    char trendStr[24];
    snprintf(trendStr, sizeof(trendStr), "%s %+.1f hPa/3h", trendSym, wx.pressTrend);
    tft.setTextColor(trendCol);
    tft.setCursor(rx+6, 66); tft.print(trendStr);

    tft.drawFastHLine(rx, 84, 168, C_LINE);

    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.setCursor(rx+6, 90); tft.print("WIATR");
    char windStr[12];
    snprintf(windStr, sizeof(windStr), "%.0f", wx.windSpeed);
    tft.setFont(&fonts::Font4);
    uint32_t wc = (wx.windSpeed >= 40) ? C_RED : (wx.windSpeed >= 20) ? C_ORANGE : C_WHITE;
    tft.setTextColor(wc);
    tft.setCursor(rx+6, 104); tft.print(windStr);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_LGRAY);
    tft.print(" km/h ");
    tft.setTextColor(C_CYAN);
    tft.print(windDirName(wx.windDir));
    char dirStr[8];
    snprintf(dirStr, sizeof(dirStr), " %d", wx.windDir);
    tft.setTextColor(C_DGRAY);
    tft.print(dirStr);

    tft.drawFastHLine(0, 153, 320, C_LINE);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(C_DGRAY);
    tft.setCursor(4, 156); tft.print("open-meteo.com | zmiana: panel www");

    drawDots(3);
}
