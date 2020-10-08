// 센싱장치 : 3C:71:BF:45:24:74
// 공기청정기 : 84:0D:8E:0B:F3:DC
// 환풍기 : FC:F5:C4:55:1E:74
#include <esp_now.h> // ESP NOW 통신하려고 사용하는 라이브러리
#include "DHT.h" // 온습도 센서
#include "MQ135.h" // 이산화탄소 센서
#include <WiFi.h> // 와이파이
#include <HTTPClient.h> // 프로그램 저장공간 17% 차지
#include "ESPAsyncWebServer.h" // 센싱장치가 웹 서버 열라고 씀
#include <Arduino_JSON.h> // 우리 동네 미세먼지값을 JSON 형식으로 읽어옴
#include <AsyncTCP.h> // 서버에다가 요청해서 값을 읽어온 후 서버의 정보와 일치한지 검사해줌.

const char* PARAM_INPUT_1 = "state";

// 접속할 공유기의 ssid, 비밀번호
const char* ssid = "olleh_WiFi_215A";
const char* password = "000000486a";
const String endpoint = "http://openapi.airkorea.or.kr/openapi/services/rest/ArpltnInforInqireSvc/getMsrstnAcctoRltmMesureDnsty?stationName=%EC%A2%85%EB%A1%9C%EA%B5%AC&dataTerm=month&pageNo=1&numOfRows=10&ServiceKey=bytN9iG7InXPwI3ETOzsh%2Fqy%2BuxCNmHpG9PeFo%2FNSP92e9UjO6CYS7PzKRD%2Fyijj5rOONKhpWrAMc4jyyZ4ktw%3D%3D&ver=1.3";
String line = "";

// ESP now 통신을 할 때 정보를 수신할 ESP의 MAC address
uint8_t broadcastAddress1[] = {0x84, 0x0D, 0x8E, 0x0B, 0xF3, 0xDC};
uint8_t broadcastAddress2[] = {0xFC, 0xF5, 0xC4, 0x55, 0x1E, 0x74};

const int output = 2;
const int buttonPin = 4;
int ledState = LOW;          // the current state of the output pin
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin

unsigned long lastTime = 0;  
unsigned long timerDelay = 3000;  // send readings timer
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

// web 서버를 만들고 포트를 80으로 설정한다.
AsyncWebServer server(80);
AsyncEventSource events("/events");

// 정보를 송신하기 위해 구조체를 만듦.
typedef struct esp_struct {
  int x; // 공기청정기 제어
  int y; // 환풍기
} esp_struct;
// esp_send 라는 이름의 esp_struct 변수를 생성함.
esp_struct esp_send;

// 데이터가 전송되었을 때 현황을 함수를 통해서 보여줌.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  Serial.print("Packet to: ");
  // Copies the sender mac address to a string
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" send status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// 전역 변수로 선언하여 농도, 측정값을 저장함.
float temperature;
float humidity;
float co2gas;
float microdust_GP2Y;
float microdust_pm10;


#define DHTPIN 26     // 온습도 센서가 4번에 연결
#define DHTTYPE DHT11   // DHT11 온습도 센서 사용
DHT dht(DHTPIN, DHTTYPE); // DHT 설정 (4,DHT11)

void readDHTTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) {
    temperature = t;
  }
  else {
    temperature = t;
  }
}

void readDHTHumidity() {
  float h = dht.readHumidity();
  if (isnan(h)) {
    humidity = h;
  }
  else {
    humidity = h;
  }
}

void readCo2gas() {
  analogReadResolution(10);
  // 보통 아두이노는 0~1023으로 아날로그 값을 읽음. 10Bit
  // ESP32는 0~4098 아날로그 값을 읽음. 12Bit
  // analog값을 읽을 때 10bit로 받겠다.
  co2gas = analogRead(34);
}

// 공기청정기 위 등에서 먼지를 가라앉힌 후 voltage값 개별적으로 측정 필요
#define no_dust 0.35    // 미세 먼지 없을 때 초기 V 값 0.35
// 미세 먼지 센서 연결
const int dustout=35;
const int v_led=32;
float vo_value=0;   // 센서로 읽은 값 변수 선언
float sensor_voltage=0; // 센서로 읽은 값을 전압으로 측정 변수
float dust_density=0;   // 실제 미세 먼지 밀도 변수

void readMicrodust() {
  digitalWrite(v_led,LOW); // 적외선 LED ON
  delayMicroseconds(280); // 280us동안 딜레이
  analogReadResolution(10);
  vo_value=analogRead(dustout); // 데이터를 읽음
  delayMicroseconds(40); // 320us - 280us
  digitalWrite(v_led,HIGH); // 적외선 LED OFF
  delayMicroseconds(9680); // 10ms(주기) -320us(펄스 폭) 한 값

  sensor_voltage = vo_value * 5.0 / 1024;
  dust_density = (sensor_voltage-no_dust) / 0.005;
  
  microdust_GP2Y = dust_density;
}

// 공공 데이터 포털에서 대기오염정보를 파싱해오기 위해서 필요한 함수. 초미세먼지는 파싱 안하고 지금은 미세먼지만 해놓음
void get_weather(void) {
  if ((WiFi.status() == WL_CONNECTED)) { //와이파이에 연결되어 있다면
    Serial.println("Starting connection to server...");
    // http client
    HTTPClient http;
    http.begin(endpoint);       //Specify the URL
    int httpCode = http.GET();  //Make the request
    if (httpCode > 0) {         //Check for the returning code
      line = http.getString();
    }
    else {
      Serial.println("Error on HTTP request");
    }
//    Serial.println(line); // 수신한 날씨 정보 시리얼 모니터 출력
    String pm10;  // 문자열을 만들음.
    int pm10Value_start= line.indexOf(F("<pm10Value>")); // "<tm>"문자가 시작되는 인덱스 값('<'의 인덱스)을 반환한다. 
    int pm10Value_end= line.indexOf(F("</pm10Value>"));  
    pm10 = line.substring(pm10Value_start + 11, pm10Value_end); // +1: "<tm>"스트링의 크기 11바이트, 11칸 이동
    Serial.print(F("pm10: ")); Serial.println(pm10); // 시리얼 모니터에 pm10(미세먼지)의 값을 표시함.
    
    int inString1 = pm10.substring(0, pm10.length()).toInt(); // inString1이라는 정수로 반환함.
    Serial.println(inString1);
    if(0<inString1 && inString1<=30) { // 미세먼지의 측정값을 통해서 등급을 매김. 표준
      Serial.println("good"); // 좋음, 초록
    } else if(30<inString1 && inString1<=80) { 
      Serial.println("soso"); // 보통, 파랑
    }else if(80<inString1 && inString1<=150) { 
      Serial.println("bad"); // 나쁨, 노랑
    } else { 
      Serial.println("very bad!"); // 매우 나쁨, 빨강
    }
    
    http.end(); //Free the resources
    microdust_pm10 = inString1;
  }
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Air Care System Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <!--  버튼 javascript -->
  <style>
    html {
      font-family: Arial;
      display: inline-block;
      text-align: center;
      background-color: ivory;
    }

    h4 {
      font-size: 2rem;
    }

    p {
      font-size: 1.2rem;
    }

    body {
      margin: 0;
    }

    .topnav {
      overflow: hidden;
      background-color: lavender;
      color: black;
      font-size: 1.7rem;
    }

    .content {
      padding: 20px;
    }

    .card {
      background-color: white;
      box-shadow: 2px 2px 12px 1px rgba(140, 140, 140, .5);
    }

    .cards {
      max-width: 700px;
      margin: 0 auto;
      display: grid;
      grid-gap: 2rem;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    }

    .reading {
      font-size: 2.8rem;
    }

    .packet {
      color: #bebebe;
    }

    .card.temperature {
      color: #0e7c7b;
    }

    .card.humidity {
      color: #17bebb;
    }

    .card.co2 {
      color: #3fca6b;
    }

    .card.microdust {
      color: #d62246;
    }

    .card.pm10 {
      color: blue;
    }

    .condition {
      color: black;
    }

    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <div class="topnav">
    <h3>Air Care WEB SERVER</h3>
  </div>
  %BUTTONPLACEHOLDER%
<script>function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }
  xhr.send();
}

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var inputChecked;
      var outputStateM;
      if( this.responseText == 1){ 
        inputChecked = true;
        outputStateM = "On";
      }
      else { 
        inputChecked = false;
        outputStateM = "Off";
      }
      document.getElementById("output").checked = inputChecked;
      document.getElementById("outputState").innerHTML = outputStateM;
    }
  };
  xhttp.open("GET", "/state", true);
  xhttp.send();
}, 1000 ) ;
</script>
<div class="content">
    <div class="cards">
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> TEMPERATURE</h4>
        <p class="condition">standard:26&deg;C</p>
        <p><span class="reading"><span id="temp">%TEMPERATURE%</span> &deg;C</span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> HUMIDITY</h4>
        <p class="condition">standard:60</p>
        <p><span class="reading"><span id="hum">%HUMIDITY%</span> &percnt;</span></p>
      </div>
      <div class="card co2 ppm">
        <h4><i class="fas fa-lungs"></i> CO2</h4>
        <p class="condition">standard: 550ppm</p>
        <p><span class="reading"><span id="gas">%co2gas%</span> ppm</span></p>
      </div>
      <div class="card microdust">
        <h4><i class="fas fa-lungs"></i> GP2Y</h4>
        <p class="condition">standard:50ug/m^3</p>
        <p><span class="reading"><span id="microdust">%microdust_GP2Y%</span> ug/m^3</span></p>
      </div>
      <div class="card pm10">
        <h4><i class="fas fa-lungs"></i> pm10</h4>
        <p class="condition">standard:80ug/m^3</p>
        <p><span class="reading"><span id="pm10">%microdust_pm10%</span> ug/m^3</span></p>
      </div>
    </div>
  </div>
<script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');

      source.addEventListener('open', function (e) {
        console.log("Events Connected");
      }, false);
      source.addEventListener('error', function (e) {
        if (e.target.readyState != EventSource.OPEN) {
          console.log("Events Disconnected");
        }
      }, false);
      source.addEventListener('message', function (e) {
        console.log("message", e.data);
      }, false);
      source.addEventListener('temperature', function (e) {
        console.log("temperature", e.data);
        document.getElementById("temp").innerHTML = e.data;
      }, false);
      source.addEventListener('humidity', function (e) {
        console.log("humidity", e.data);
        document.getElementById("hum").innerHTML = e.data;
      }, false);
      source.addEventListener('co2gas', function (e) {
        console.log("co2", e.data);
        document.getElementById("gas").innerHTML = e.data;
      }, false);
      source.addEventListener('microdust_GP2Y', function (e) {
        console.log("microdust", e.data);
        document.getElementById("microdust").innerHTML = e.data;
      }, false);
      source.addEventListener('microdust_pm10', function (e) {
        console.log("pm10", e.data);
        document.getElementById("pm10").innerHTML = e.data;
      }, false);
    }
  </script>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String& var){
  if(var == "TEMPERATURE"){
    return String(temperature);
  }
  else if(var == "HUMIDITY"){
    return String(humidity);
  }
  else if(var == "co2gas"){
    return String(co2gas);
  }
  else if(var == "microdust_GP2Y"){
    return String(microdust_GP2Y);
  }
  else if(var == "microdust_pm10"){
    return String(microdust_pm10);
  }
  //Serial.println(var);
  if(var == "BUTTONPLACEHOLDER"){
    String buttons ="";
    String outputStateValue = outputState();
    buttons+= "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"output\" " + outputStateValue + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(){
  if(digitalRead(output)){
    return "checked";
  }
  else {
    return "";
  }
  return "";
}

void purifier_control() {
  // 내부 미세먼지 농도가 80이 넘거나,
  // 외부 미세먼지 농도가 800이 넘거나,
  // 이산화탄소 농도가 550이 넘으면 가동
  if(microdust_GP2Y > 80 || microdust_pm10 > 80 || co2gas > 550)
  {
    esp_send.x = 1;
    Serial.println("Purifier ON");
  } else {
    esp_send.x = 0;
    Serial.println("Purifier OFF");
  }
}

void ventilator_control() {
  // 외부 미세먼지 농도가 80 이하일 경우 가동
  // 또는 가동온도가 26도보다 높은 경우,
  // 또는 내부 미세먼지보다 외부의 미세먼지가 더 좋은 경우,
  // 또는 이산화탄소의 농도가 600이 넘는 경우,
  // 또는 실내 습도가 70을 넘어가는 경우 가동 
  if(microdust_pm10 <= 80)
  {
    if(microdust_GP2Y < microdust_pm10 || temperature > 26 || co2gas > 600 || humidity > 60)
    esp_send.y = 1;
    Serial.println("Ventilator ON");
  } else {
    esp_send.y = 0;
    Serial.println("Ventilator OFF");
  }
}

void setup() {
  pinMode(output, OUTPUT);
  digitalWrite(output, LOW);
  pinMode(buttonPin, INPUT);
  
  Serial.begin(115200);
  dht.begin();
  pinMode(v_led,OUTPUT);
  WiFi.mode(WIFI_AP_STA);

  // WiFi 환경 설정.
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Setting as a Wi-Fi Station..");
  }
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // ESP now 통신 환경 설정.
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // esp에서 새로운 packet을 보낼 때 OnDataSent함수를 실행하도록 함.
  esp_now_register_send_cb(OnDataSent);
  // register peer
  esp_now_peer_info_t peerInfo;
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr, broadcastAddress1, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  memcpy(peerInfo.peer_addr, broadcastAddress2, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  // Server Events
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
  });

  // Send a GET request to <ESP_IP>/update?state=<inputMessage>
  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/update?state=<inputMessage>
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      inputParam = PARAM_INPUT_1;
      digitalWrite(output, inputMessage.toInt());
      ledState = !ledState;
    }
    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    Serial.println(inputMessage);
    request->send(200, "text/plain", "OK");
  });

  // Send a GET request to <ESP_IP>/state
  server.on("/state", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(digitalRead(output)).c_str());
  });
  
  server.addHandler(&events);
  server.begin();
}

void loop() {
  if ((millis() - lastTime) > timerDelay) {
    //esp_send.x = ledState;
    //esp_send.y = ledState;
    purifier_control();
    ventilator_control();

    esp_err_t result = esp_now_send(0, (uint8_t *) &esp_send, sizeof(esp_struct));
    if (result == ESP_OK) {
      Serial.println("Sent with success");
    }
    else {
      Serial.println("Error sending the data");
    }
    
    readDHTTemperature();
    readDHTHumidity();
    readCo2gas();
    readMicrodust();
    get_weather();

    Serial.printf("Temperature = %.2f ºC \n", temperature);
    Serial.printf("Humidity = %.2f % \n", humidity);
    Serial.printf("Co2 ppm = %.2f KOhm \n", co2gas);
    Serial.printf("Microdust = %.2f ug/m^3 \n", microdust_GP2Y);
    Serial.printf("pm10 = %.2f \n", microdust_pm10);
    Serial.printf("ledState : %d", ledState);
    Serial.println();
    
    events.send("ping",NULL,millis());
    events.send(String(temperature).c_str(),"temperature",millis());
    events.send(String(humidity).c_str(),"humidity",millis());
    events.send(String(co2gas).c_str(),"co2gas",millis());
    events.send(String(microdust_GP2Y).c_str(),"microdust_GP2Y",millis());
    events.send(String(microdust_pm10).c_str(),"microdust_pm10",millis());
    lastTime = millis();
    delay(3000);
  }

  int reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == HIGH) {
        ledState = !ledState;
      }
    }
  }
  lastButtonState = reading;
}
