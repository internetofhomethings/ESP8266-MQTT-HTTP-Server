/********************************************************************
 * web_server - example code providing a framework that uses
 * either:
 * 1. An Arduino library (ESP8266WiFi.h)
 * 2. EspressIf SDK API
 * 3. MQTT
 * 
 * created Nov-30, 2015
 * by Dave St. Aubin
 *
 * This example code is in the public domain.
 ********************************************************************/

#include <ESP8266WiFi.h>          //http server library
#include <PubSubClient.h>         //MQTT server library
#include <UtilityFunctions.h>

// Include API-Headers
extern "C" {                      //SDK functions for Arduino IDE access
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem_manager.h"
#include "mem.h"
#include "user_interface.h"
#include "cont.h"
#include "espconn.h"
#include "eagle_soc.h"
void * pvPortZalloc(int size);
}

//Server actions
#define SET_LED_OFF 0
#define SET_LED_ON  1
#define Get_SENSORS 2
#define INVALID_REQUEST 99

//Server Type
#define SVR_MQTT 0
#define SVR_HTTP_LIB 1
#define SVR_HTTP_SDK 2

//Set this to desired server type
#define SVR_TYPE SVR_HTTP_LIB
#define MQTT_SVR_ENABLE 1

#define SERBAUD 115200
#define SVRPORT 9701

//JSON string type
#define ONEJSON 0
#define FIRSTJSON 1
#define NEXTJSON 2
#define LASTJSON 3

//GPIO pin assignments
#define AMUXSEL0 14     // AMUX Selector 0
#define AMUXSEL1 12     // AMUX Selector 1
#define AMUXSEL2 13     // AMUX Selector 2
#define LED_IND 16      // LED used for initial code testing (not included in final hardware design)

#define URLSize 10
#define DATASize 10

typedef enum ProtocolType {
    GET = 0,
    POST,
    GET_FAVICON
} ProtocolType;

typedef struct URL_Param {
    enum ProtocolType Type;
    char pParam[URLSize][URLSize];
    char pParVal[URLSize][URLSize];
    int nPar;
} URL_Param;

#define mqtt_server "test.mosquitto.org"
#define mqtt_user "your_username"
#define mqtt_password "your_password"

#define rx_topic "MyMqttSvrRqst"
#define tx_topic "MyMqttSvrRply"

const char* ssid = "YourWifiSSID";
const char* password = "YourWifiPASSWORD";
const IPAddress ipadd(192,168,0,132);     
const IPAddress ipgat(192,168,0,1);       
const IPAddress ipsub(255,255,255,0);     

long lastMsg = 0;
uint32_t state=0;
int initRx=0;
int stoprepub = 0;
float Ain;
char Ain0[20],Ain1[20],Ain2[20],Ain3[20],Ain4[20],Ain5[20],Ain6[20],Ain7[20]; 
bool complete=false;
int lc=0;

//sdk web server
char *precvbuffer;
static uint32 dat_sumlength = 0;

/********************************************************
 * Local Function Prototypes
 ********************************************************/
void SdkWebServer_Init(int port);
void SdkWebServer_listen(void *arg);
void SdkWebServer_recv(void *arg, char *pusrdata, unsigned short length);
void SdkWebServer_discon(void *arg);
void SdkWebServer_recon(void *arg, sint8 err);
void SdkWebServer_senddata(void *arg, bool responseOK, char *psend);
bool SdkWebServer_savedata(char *precv, uint16 length);
void SdkWebServer_parse_url_params(char *precv, URL_Param *purl_param);

void util_printStatus(char * status, int s);
void util_startWIFI(void);

void jsonEncode(int pos, String * s, String key, String val);
void jsonAdd(String *s, String key,String val);

void ArduinoWebServer_Init(void);
void ArduinoWebServer_Processor(void);
void ArduinoWebServer_KillClient(WiFiClient client, bool *busy);

void MqttServer_Init(void);
void MqttServer_reconnect(void);  
void MqttServer_callback(char* topic, byte* payload, unsigned int length);
void MqttServer_Processor(void);

int Server_GetRequest(String payload);
String Server_ExecuteRequest(int servertype, int request);

void ReadSensors(int interval);

/********************************************************
 * Instantiate class objects
 ********************************************************/
#if SVR_TYPE==SVR_HTTP_LIB
WiFiServer server(SVRPORT);       //Create Arduino Library web server object
#endif

WiFiClient espClient;             //Create Wifi Client object
PubSubClient client(espClient);   //Create Mqtt Client object

/********************************************************
 * SDK API Web Server Initialization
 * Function: SdkWebServer_Init(int port)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * port         http server listen port
 * return       no return value
 ********************************************************/
void SdkWebServer_Init(int port) {
    LOCAL struct espconn esp_conn;
    LOCAL esp_tcp esptcp;
    //Fill the connection structure, including "listen" port
    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    esp_conn.recv_callback = NULL;
    esp_conn.sent_callback = NULL;
    esp_conn.reverse = NULL;
    //Register the connection timeout(0=no timeout)
    espconn_regist_time(&esp_conn,0,0);
    //Register connection callback
    espconn_regist_connectcb(&esp_conn, SdkWebServer_listen);
    //Start Listening for connections
    espconn_accept(&esp_conn); 
    Serial.print("Web Server initialized: Type = SDK API\n");
}

/********************************************************
 * SDK API Web Server TCP Client Connection Callback
 * Function: SdkWebServer_listen(void *arg)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * return       no return value
 ********************************************************/
void SdkWebServer_listen(void *arg)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    espconn_regist_recvcb(pesp_conn, SdkWebServer_recv);
    espconn_regist_reconcb(pesp_conn, SdkWebServer_recon);
    espconn_regist_disconcb(pesp_conn, SdkWebServer_discon);
}

/********************************************************
 * SDK API Web Server Receive Data Callback
 * Function: SdkWebServer_recv(void *arg, char *pusrdata, 
 *                          unsigned short length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * *pusrdata    data received after IP:port
 * length       data length (bytes)
 * return       no return value
 ********************************************************/
void SdkWebServer_recv(void *arg, char *pusrdata, unsigned short length)
{
    URL_Param *pURL_Param = NULL;
    char *pParseBuffer = NULL;
    bool parse_flag = false;
    struct espconn *ptrespconn = ( espconn *)arg;
    int i;
    String payld = "";
    espconn_set_opt(ptrespconn, ESPCONN_REUSEADDR);

    parse_flag = SdkWebServer_savedata(pusrdata, length);
    pURL_Param = (URL_Param *)os_zalloc(sizeof(URL_Param));
     
    SdkWebServer_parse_url_params(precvbuffer, pURL_Param);
    Serial.print("Param1: ");
    Serial.println(pURL_Param->pParam[0]);
    Serial.print("Value1: ");
    Serial.println(pURL_Param->pParVal[0]);

    char * pRx = NULL;
    pRx = (char *)os_zalloc(512);
    os_memcpy(pRx, precvbuffer, length);

    switch (pURL_Param->Type) {
        case GET:
            if(os_strcmp(pURL_Param->pParam[0], "request")==0) {
                // GetSensors is 1 of 3 requests the server currently supports
                if(os_strcmp(pURL_Param->pParVal[0], "GetSensors")==0) {
                      // Execute request & get reply string
                      payld = Server_ExecuteRequest(SVR_HTTP_SDK, Get_SENSORS);
                      SdkWebServer_senddata(ptrespconn, true, (char *)payld.c_str());
                }
                // LedOn is 2 of 3 requests the server currently supports
                if(os_strcmp(pURL_Param->pParVal[0], "LedOn")==0) {
                      // Execute request & get reply string
                      payld = Server_ExecuteRequest(SVR_HTTP_SDK, SET_LED_ON);
                      SdkWebServer_senddata(ptrespconn, true, (char *)payld.c_str());
                }
                // LedOff is 3 of 3 requests the server currently supports
                if(os_strcmp(pURL_Param->pParVal[0], "LedOff")==0) {
                      // Execute request & get reply string
                      payld = Server_ExecuteRequest(SVR_HTTP_SDK, SET_LED_OFF);
                      SdkWebServer_senddata(ptrespconn, true, (char *)payld.c_str());
                }
                // Add additional requests here
            }
            break;

        case POST:
           Serial.print("We have a POST request.\n");
             break;
    }

    if (precvbuffer != NULL){
      os_free(precvbuffer);
      precvbuffer = NULL;
    }
    os_free(pURL_Param);
    pURL_Param = NULL;
    os_free(pRx);
    pRx=NULL;
}

/********************************************************
 * SDK API Web Server TCP Connection Closed Callback
 * Function: SdkWebServer_discon(void *arg)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * return       no return value
 ********************************************************/
void SdkWebServer_discon(void *arg)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    os_printf("webserver's %d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
            pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
            pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port);
}

/********************************************************
 * SDK API Web Server TCP Disconnect on error Callback
 * Function: SdkWebServer_recon(void *arg, sint8 err)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * err          error code
 * return       no return value
 ********************************************************/
void SdkWebServer_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = ( espconn *)arg;

    os_printf("webserver's %d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
        pesp_conn->proto.tcp->remote_ip[1],pesp_conn->proto.tcp->remote_ip[2],
        pesp_conn->proto.tcp->remote_ip[3],pesp_conn->proto.tcp->remote_port, err);
}

/********************************************************
 * SDK API Web Server send data to connected client
 * Function: SdkWebServer_senddata(void *arg, 
 *                                 bool responseOK, 
 *                                 char *psend)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *arg         pointer to espconn structure
 * responseOK   reply status
 * *psend       string to send
 * return       no return value
 ********************************************************/
void SdkWebServer_senddata(void *arg, bool responseOK, char *psend)
{
    uint16 length = 0;
    char *pbuf = NULL;
    char httphead[256];
    struct espconn *ptrespconn = ( espconn *)arg;
    os_memset(httphead, 0, 256);

    if (responseOK) {
        os_sprintf(httphead,
                   "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nServer: lwIP/1.4.0\r\nAccess-Control-Allow-Origin: *\r\n",
                   psend ? os_strlen(psend) : 0);

        if (psend) {
            os_sprintf(httphead + os_strlen(httphead),
                       "Content-type: application/json\r\nExpires: Fri, 10 Apr 2015 14:00:00 GMT\r\nPragma: no-cache\r\n\r\n");
            length = os_strlen(httphead) + os_strlen(psend);
            pbuf = (char *)os_zalloc(length + 1);
            os_memcpy(pbuf, httphead, os_strlen(httphead));
            os_memcpy(pbuf + os_strlen(httphead), psend, os_strlen(psend));
        } else {
            os_sprintf(httphead + os_strlen(httphead), "\n");
            length = os_strlen(httphead);
        }
    } else {
        os_sprintf(httphead, "HTTP/1.0 400 BadRequest\r\n\
Content-Length: 0\r\nServer: lwIP/1.4.0\r\n\n");
        length = os_strlen(httphead);
    }

    if (psend) {
        espconn_sent(ptrespconn, (uint8 *)pbuf, length);
    } else {
        espconn_sent(ptrespconn, (uint8 *)httphead, length);
    }

    if (pbuf) {
        os_free(pbuf);
        pbuf = NULL;
    }
}

/********************************************************
 * SDK API Web Server save data from connected client
 * Function: bool SdkWebServer_savedata(char *precv, 
 *                                      uint16 length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *precv       string received
 * length       string length
 * return       true if successful save
 ********************************************************/
bool SdkWebServer_savedata(char *precv, uint16 length)
{
    bool flag = false;
    char length_buf[10] = {0};
    char *ptemp = NULL;
    char *pdata = NULL;
    uint16 headlength = 0;
    static uint32 totallength = 0;

    ptemp = (char *)os_strstr(precv, "\r\n\r\n");

    if (ptemp != NULL) {
        length -= ptemp - precv;
        length -= 4;
        totallength += length;
        headlength = ptemp - precv + 4;
        pdata = (char *)os_strstr(precv, "Content-Length: ");

        if (pdata != NULL) {
            pdata += 16;
            precvbuffer = (char *)os_strstr(pdata, "\r\n");

            if (precvbuffer != NULL) {
                os_memcpy(length_buf, pdata, precvbuffer - pdata);
                dat_sumlength = atoi(length_buf);
            }
        } else {
          if (totallength != 0x00){
            totallength = 0;
            dat_sumlength = 0;
            return false;
          }
        }
        if ((dat_sumlength + headlength) >= 1024) {
          precvbuffer = (char *)os_zalloc(headlength + 1);
            os_memcpy(precvbuffer, precv, headlength + 1);
        } else {
          precvbuffer = (char *)os_zalloc(dat_sumlength + headlength + 1);
          os_memcpy(precvbuffer, precv, os_strlen(precv));
        }
    } else {
        if (precvbuffer != NULL) {
            totallength += length;
            os_memcpy(precvbuffer + os_strlen(precvbuffer), precv, length);
        } else {
            totallength = 0;
            dat_sumlength = 0;
            return false;
        }
    }

    if (totallength == dat_sumlength) {
        totallength = 0;
        dat_sumlength = 0;
        return true;
    } else {
        return false;
    }
}

/********************************************************
 * SDK API Web Server parse received parameters
 * Function: void SdkWebServer_ parse_url_params(
 *           char *precv, URL_Param *purl_param) 
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *precv       string received
 * *purl_param  parsed parameter structure
 * return       no return value
 ********************************************************/
void SdkWebServer_parse_url_params(char *precv, URL_Param *purl_param)
  {
    char *str = NULL;
    uint8 length = 0;
    char *pbuffer = NULL;
    char *pbufer = NULL;
    int ipar=0;

    if (purl_param == NULL || precv == NULL) {
        return;
    }

    //pbuffer = (char *)os_strstr(precv, "Host:");
    pbuffer = (char *)os_strstr(precv, "Accept:");

    if (pbuffer != NULL) {
        length = pbuffer - precv;
        pbufer = (char *)os_zalloc(length + 1);
        pbuffer = pbufer;
        os_memcpy(pbuffer, precv, length);
        os_memset(purl_param->pParam, 0, URLSize*URLSize);
        os_memset(purl_param->pParVal, 0, URLSize*URLSize);

        /////os_memcpy(purl_param->pParVal[ipar], precv, 48);

        if (os_strncmp(pbuffer, "GET /favicon.ico", 16) == 0) {
      purl_param->Type = GET_FAVICON;
      os_free(pbufer);
      return;
        } else if (os_strncmp(pbuffer, "GET ", 4) == 0) {
          purl_param->Type = GET;
            pbuffer += 4;
        } else if (os_strncmp(pbuffer, "POST ", 5) == 0) {
          purl_param->Type = POST;
            pbuffer += 5;
        }

        pbuffer ++;
        str = (char *)os_strstr(pbuffer, "?");

        if (str != NULL) {
            str ++;
            do {
              pbuffer = (char *)os_strstr(str, "=");
              length = pbuffer - str;
              os_memcpy(purl_param->pParam[ipar], str, length);
              str = (char *)os_strstr(++pbuffer, "&");
              if(str != NULL) {
                length = str - pbuffer;
                os_memcpy(purl_param->pParVal[ipar++], pbuffer, length);
                str++;
              }
              else {
                str = (char *)os_strstr(pbuffer, " HTTP");
                if(str != NULL) {
                    length = str - pbuffer;
                    os_memcpy(purl_param->pParVal[ipar++], pbuffer, length);
                    str = NULL;
                }
                else {
                    //os_memcpy(purl_param->pParVal[ipar++], pbuffer, 16);
                }
              }
            }
            while (str!=NULL);
        }

        purl_param->nPar = ipar;
        os_free(pbufer);
    } else {
        return;
    }
}


/********************************************************
 * print status to serial port
 * Function: util_printStatus(char * status, int s)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *status      status string
 * s            status code
 * return       no return value
 ********************************************************/

void util_printStatus(char * status, int s) {
      Serial.print(system_get_free_heap_size());
      delay(100);
      Serial.print(" ");
      delay(100);
      Serial.print(millis()/1000);
      delay(100);
      Serial.print(" ");
      delay(100);
      if(s>=0) Serial.print(s);
      else Serial.print("");
      delay(100);
      Serial.print(" ");
      delay(100);
      Serial.println(status);
//    }
    delay(100);
}

/********************************************************
 * connect to local Wifi
 * Function: util_startWIFI(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void util_startWIFI(void) {
  //set IP if not correct
  IPAddress ip = WiFi.localIP();
  if( ip!= ipadd) { 
      WiFi.config(ipadd, ipgat, ipsub);  //dsa added 12.04.2015
      Serial.println();
      delay(10);
      Serial.print("ESP8266 IP:");
      delay(10);
      Serial.println(ip);
      delay(10);
      Serial.print("Fixed   IP:");
      delay(10);
      Serial.println(ipadd);
      delay(10);
      Serial.print("IP now set to: ");
      delay(10);
      Serial.println(WiFi.localIP());
      delay(10);
  }
  // Connect to WiFi network
  Serial.println();
  delay(10);
  Serial.println();
  delay(10);
  Serial.print("Connecting to ");
  delay(10);
  Serial.println(ssid);
  delay(10); 
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Print the IP address
  Serial.print("ESP8266 IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("ESP8266 WebServer Port: ");
  Serial.println(SVRPORT);
  delay(300);

}

/********************************************************
 * Start Arduino library based web server
 * Function: ArduinoWebServer_Init(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
#if SVR_TYPE==SVR_HTTP_LIB
void ArduinoWebServer_Init(void) {
    server.begin(); 
    Serial.print("Web Server initialized: Type = Arduino library\n");  
}
#endif

/********************************************************
 * terminate TCP client connection
 * Function: ArduinoWebServer_KillClient(WiFiClient client, bool *busy)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * client       Wifi client object
 * busy         busy flag
 * return       no return value
 ********************************************************/
void ArduinoWebServer_KillClient(WiFiClient client, bool *busy) {
    lc=0;
    delay(1);
    client.flush();
    client.stop();
    complete=false;
    *busy = false;  
}

/********************************************************
 * Process http GET request using Arduino library
 * Function: ArduinoWebServer_Processor(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
#if SVR_TYPE==SVR_HTTP_LIB
void ArduinoWebServer_Processor(void) {
    static bool busy=false;
    static int timeout_busy=0;
    int amux;

    //connect wifi if not connected
    if (WiFi.status() != WL_CONNECTED) {
        delay(1);
        util_startWIFI();
        return;
    }
    //return if busy
    if(busy) {
        delay(1);
        if(timeout_busy++ >10000) {
            util_printStatus((char *)" Status: Busy timeout-resetting..",-1);
            ESP.reset(); 
            busy = false;
        }
        return;
    }
    else {
        busy = true;
        timeout_busy=0;
    }
    delay(1);

    // Check if a client has connected
    WiFiClient client = server.available();
    if (!client) {
       busy = false;
       return;
    } 
    // Wait until the client sends some data
    while((!client.available())&&(timeout_busy++<5000)){
        delay(1);
        if(complete==true) {
            ArduinoWebServer_KillClient(client, &busy);
            return;
        }
    }
    //kill client if timeout
    if(timeout_busy>=5000) {
      ArduinoWebServer_KillClient(client, &busy);
      return;
    }
    Serial.println("Connection to http server made...");
    complete=false; 
    ESP.wdtFeed(); 
    
    // Read the first line of the request
    String payld = client.readStringUntil('\r');
    client.flush();
    if (payld.indexOf("/favicon.ico") != -1) {
        client.stop();
        complete=true;
        busy = false;
        return;
    }
    Serial.print("Recv http: ");  
    Serial.println(payld);
    delay(100);

    // Identify the request
    int request = Server_GetRequest(payld);
    if(request == INVALID_REQUEST) {
        client.stop();
        complete=true;
        busy = false;
    }
    client.flush();

    // Ignore if invalid
    if(request == INVALID_REQUEST) {
        Serial.println("Invalid Request");
        return;    
    }
    // Execute request & get reply string
    payld = Server_ExecuteRequest(SVR_HTTP_LIB, request);

    // Reply to Request
    client.print(payld);
    yield();

    delay(150);
    complete=true;
    busy = false;
    ESP.wdtFeed(); 
    ArduinoWebServer_KillClient(client, &busy);
}
#endif

/********************************************************
 * Read 1 sensor each time function is called
 * Current sensors: ESP8266 ADC with 8-1 MUX input
 * Function: void ReadSensors(int interval)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * interval     milliseconds (minimum) between reads
 * return       no return value
 ********************************************************/
void ReadSensors(int interval) {
    yield();
    long now = millis();                 
    if (now - lastMsg > interval) {  // Read 1 sensor every "interval" milliseconds or longer
        lastMsg = now;
    }
    else {
        return;
    }
    switch(state++) {
        case 0:
            //Set 8-1 amux to position 0
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
            //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain0, 2);
            break;
        case 1:
            //Set 8-1 amux to position 1
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain1, 2);
            break;
        case 2:
            //Set 8-1 amux to position 2
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain2, 2);
            break;
        case 3:
            //Set 8-1 amux to position 3
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 0);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain3, 2);
            break;
        case 4:
            //Set 8-1 amux to position 4
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain4, 2);
            break;
        case 5:
            //Set 8-1 amux to position 5
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 0);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain5, 2);
            break;
        case 6:
            //Set 8-1 amux to position 6
            digitalWrite(AMUXSEL0, 0);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain6, 2);
            break;
        case 7:
            //Set 8-1 amux to position 7
            digitalWrite(AMUXSEL0, 1);
            digitalWrite(AMUXSEL1, 1);
            digitalWrite(AMUXSEL2, 1);
            delay(100);
             //Read analog input
            Ain = (float) analogRead(A0);
            ftoa(Ain,Ain7, 2);
            state = 0;
            break;
        default:
            break;
    }
    ESP.wdtFeed(); 
    yield();
}

/********************************************************
 * add key/value entry into json string
 * Function: jsonAdd( 
 *                      String * s, 
 *                      String key, 
 *                      String val)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * *s           pointer to current json string
 * key          this json string key
 * val          this json string value
 * return       no return value
 ********************************************************/
void jsonAdd(String *s, String key,String val) {
    *s += '"' + key + '"' + ":" + '"' + val + '"';
}

/********************************************************
 * encode key/value entry into json string
 * Function: jsonEncode(int pos, 
 *                      String * s, 
 *                      String key, 
 *                      String val)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * pos          position of this json entry
 * *s           pointer to current json string
 * key          this json string key
 * val          this json string value
 * return       no return value
 ********************************************************/
void jsonEncode(int pos, String * s, String key, String val) {
    switch (pos) {
      case ONEJSON:      
      case FIRSTJSON:
        *s += "{\r\n";
        jsonAdd(s,key,val);
        *s+= (pos==ONEJSON) ? "\r\n}" : ",\r\n";
        break;
      case NEXTJSON:    
        jsonAdd(s,key,val);
        *s+= ",\r\n";
         break;
      case LASTJSON:    
        jsonAdd(s,key,val);
        *s+= "\r\n}";
        break;
    }
}

/********************************************************
 * Get Request from received data
 * Function: Server_GetRequest(String payload)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * payload      request string
 * return       request enumberation
 ********************************************************/
int Server_GetRequest(String payload) {
    int val;
    if (payload.indexOf("/?request=LedOff") != -1)
        val = SET_LED_OFF;
    else if (payload.indexOf("/?request=LedOn") != -1)
        val = SET_LED_ON;
    else if (payload.indexOf("/?request=GetSensors") != -1) {
        val = Get_SENSORS;
    }  
    else {
        val = INVALID_REQUEST;
    }
    return val;
}

/********************************************************
 * Create request reply string to request
 * Function: Server_ExecuteRequest(int servertype, int request)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * servertype   server type (SDK or LIB)
 * request      request enumberation
 * return       reply string
 ********************************************************/
String Server_ExecuteRequest(int servertype, int request) {
    String s = "";  
    String v = "";
    if(servertype == SVR_HTTP_LIB) {
        // Prepare Response header
        s = "HTTP/1.1 200 OK\r\n";
        s += "Access-Control-Allow-Origin: *\r\n";
        ESP.wdtFeed();
    }
    switch (request) {
        case SET_LED_OFF:
        case SET_LED_ON:
            // Set GPIOn according to the request
            digitalWrite(LED_IND , request); //SET_LED_OFF=0.SET_LED_ON=1
            // Prepare the response for GPIO state
            switch(servertype) {
                case SVR_HTTP_LIB:            
                    s += "Content-Type: text/html\r\n\r\n";
                    s += "<!DOCTYPE HTML>\r\nLED is now ";
                    s += (request)?"ON":"OFF";
                    s += "</html>\n";
                    break;
                case SVR_MQTT:            
                case SVR_HTTP_SDK:
                    s += "LED is now ";
                    s += (request)?"ON":"OFF";
                    s += "\n";
                    break;
            }
            break;
        case Get_SENSORS:
            //Create JSON return string
            if(servertype == SVR_HTTP_LIB) {
                s += "Content-Type: application/json\r\n\r\n";
            }
            jsonEncode(FIRSTJSON,&s,"Ain0", Ain0);
            jsonEncode(NEXTJSON,&s,"Ain1", Ain1);
            jsonEncode(NEXTJSON,&s,"Ain2", Ain2);
            jsonEncode(NEXTJSON,&s,"Ain3", Ain3);
            jsonEncode(NEXTJSON,&s,"Ain4", Ain4);
            jsonEncode(NEXTJSON,&s,"Ain5", Ain5);
            jsonEncode(NEXTJSON,&s,"Ain6", Ain6);
            jsonEncode(NEXTJSON,&s,"Ain7", Ain7);
            v = system_get_free_heap_size();
            jsonEncode(NEXTJSON,&s,"SYS_Heap", v);
            v = millis()/1000;
            jsonEncode(LASTJSON,&s,"SYS_Time", v);
            break;
        default:
            break;
   }
   return s;
}


/********************************************************
 * Connect to MQTT broker - then subscribe to server topic
 * Function: MqttServer_reconnect(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_reconnect(void) {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Connect to MQTT Server
        if (client.connect("Test4MyMQTT")) {
            // Successful connection message & subscribe
            Serial.println("connected");
            client.subscribe(rx_topic);
        } else {
            // Failed to connect message
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}
/********************************************************
 * MQTT server topic callback
 * Function: MqttServer_callback(char* topic, 
 *                               byte* payload, 
 *                               unsigned int length)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * topic        topic string
 * payload      value received
 * length       value length (bytes)
 * return       no return value
 ********************************************************/
void MqttServer_callback(char* topic, byte* payload, unsigned int length) {
    String payld = "";
    //Msg rx message
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    // Extract payload
    for (int i = 0; i < length; i++) {
        payld = payld + String((char)payload[i]);
    }
    // Ignore if not Server Request
    if(String(topic) != rx_topic) {
        return;
    }
    // Identify the request
    int request = Server_GetRequest(payld);
  
    // Ignore if invalid
    if(request == INVALID_REQUEST) {
        Serial.println("Invalid Request");
        return;    
    }
    // Execute request & get reply string
    payld = Server_ExecuteRequest(SVR_MQTT, request);

    Serial.println(payld);
    // Reply to Request
    client.publish(tx_topic, payld.c_str(), false);    
}

/********************************************************
 * MQTT loop() service
 * Function: MqttServer_Processor(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_Processor(void) {
    yield();
    if (!client.connected()) {
        MqttServer_reconnect();
    }
    client.loop();
    yield();
}

/********************************************************
 * Initialize MQTT Broker Parameters
 * Function: MqttServer_Init(void)
 * 
 * Parameter    Description
 * ---------    ---------------------------------------
 * return       no return value
 ********************************************************/
void MqttServer_Init(void) {
    client.setServer(mqtt_server, 1883); // Start Mqtt server
    client.setCallback(MqttServer_callback);
}

void setup() {
    Serial.begin(SERBAUD);               // Initialize serial port
    util_startWIFI();                    // Connect to local Wifi

    #if SVR_TYPE==SVR_HTTP_LIB
    ArduinoWebServer_Init();             // Start Arduino library based web server
    #endif
    #if SVR_TYPE==SVR_HTTP_SDK
    SdkWebServer_Init(SVRPORT);          // Start SDK based web server
    #endif

    #if MQTT_SVR_ENABLE==1
    MqttServer_Init();                   // Start MQTT Server
    #endif
  
    pinMode(LED_IND , OUTPUT);           // Set Indicator LED as output
    digitalWrite(LED_IND, 0);            // Turn LED off
}

void loop() {
    #if MQTT_SVR_ENABLE==1
    MqttServer_Processor();              // Service MQTT
    #endif

    #if SVR_TYPE==SVR_HTTP_LIB
    ArduinoWebServer_Processor();        // Service Web Server
    #endif

    ReadSensors(2500);                   // Read 1 sensor every 2.5 seconds or longer
}

